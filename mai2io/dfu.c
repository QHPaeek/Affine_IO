// Wrote for Affine IO
// DFU firmware loader for STM32 devices using libusb on Windows

#include "dfu_loader.h"

#include <windows.h>
#include <libusb.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define STM32_VENDOR_ID            0x0483
#define STM32_PRODUCT_ID           0xDF11
#define STM32_BASE_ADDRESS         0x08000000u
#define STM32_DEFAULT_TRANSFER     1024u

#define DFU_REQUEST_DNLOAD         0x01
#define DFU_REQUEST_UPLOAD         0x02
#define DFU_REQUEST_GETSTATUS      0x03
#define DFU_REQUEST_CLRSTATUS      0x04
#define DFU_REQUEST_GETSTATE       0x05
#define DFU_REQUEST_ABORT          0x06

#define DFU_STATE_appIDLE                 0
#define DFU_STATE_appDETACH               1
#define DFU_STATE_dfuIDLE                 2
#define DFU_STATE_dfuDNLOAD_SYNC          3
#define DFU_STATE_dfuDNBUSY               4
#define DFU_STATE_dfuDNLOAD_IDLE          5
#define DFU_STATE_dfuMANIFEST_SYNC        6
#define DFU_STATE_dfuMANIFEST             7
#define DFU_STATE_dfuMANIFEST_WAIT_RESET  8
#define DFU_STATE_dfuUPLOAD_IDLE          9
#define DFU_STATE_dfuERROR                10

typedef struct
{
    uint32_t start;
    uint32_t end;
    uint32_t page_size;
} stm32_dfu_segment_t;

typedef struct
{
    stm32_dfu_segment_t segments[8];
    size_t count;
} stm32_dfu_memory_layout_t;

typedef struct
{
    libusb_device_handle *handle;
    uint8_t interface_number;
    uint8_t alt_setting;
    uint16_t transfer_size;
    char alt_name[128];
    uint32_t last_erased_page;
    bool layout_valid;
    bool mass_erased;
    stm32_dfu_memory_layout_t layout;
} stm32_dfu_device_t;

static bool parse_alt_memory_layout(const char *alt_name, stm32_dfu_memory_layout_t *layout);
static const stm32_dfu_segment_t *find_segment(const stm32_dfu_memory_layout_t *layout, uint32_t address);
static bool dfu_download_block(stm32_dfu_device_t *device,
                               uint16_t block_number,
                               const uint8_t *data,
                               uint16_t length,
                               char *status_buf,
                               size_t status_len,
                               void *status_ctx,
                               bool allow_manifest);

typedef struct
{
    uint8_t status;
    uint8_t state;
    uint32_t poll_timeout_ms;
} dfu_status_t;

static dfu_loader_client_t *resolve_client(void *ctx)
{
    if (!ctx) {
        return NULL;
    }
    dfu_loader_client_t *client = (dfu_loader_client_t *)ctx;
    if (client->magic != DFU_LOADER_CLIENT_MAGIC) {
        return NULL;
    }
    return client;
}

static void set_status(char *buffer, size_t buffer_len, void *ctx, const char *fmt, ...)
{
    dfu_loader_client_t *client = resolve_client(ctx);

    va_list ap;
    va_start(ap, fmt);

    if (buffer && buffer_len > 0) {
        va_list copy;
        va_copy(copy, ap);
        _vsnprintf_s(buffer, buffer_len, _TRUNCATE, fmt, copy);
        va_end(copy);
    }

    if (client && client->status_cb) {
        char message[256];
        va_list copy;
        va_copy(copy, ap);
        _vsnprintf_s(message, sizeof(message), _TRUNCATE, fmt, copy);
        va_end(copy);
        client->status_cb(message, client->user_ctx);
    }

    va_end(ap);
}

static void report_progress(dfu_loader_progress_cb cb, void *ctx, int percent)
{
    dfu_loader_client_t *client = resolve_client(ctx);
    if (client && client->progress_cb) {
        client->progress_cb(percent, client->user_ctx);
    } else if (cb) {
        cb(percent, ctx);
    }
}

static uint32_t parse_size_bytes(const char *text, const char **out_next)
{
    unsigned long value = 0;
    const char *cursor = text;
    while (*cursor && *cursor >= '0' && *cursor <= '9') {
        value = value * 10 + (unsigned long)(*cursor - '0');
        ++cursor;
    }
    unsigned long multiplier = 1;
    if (*cursor == 'K' || *cursor == 'k') {
        multiplier = 1024UL;
        ++cursor;
    } else if (*cursor == 'M' || *cursor == 'm') {
        multiplier = 1024UL * 1024UL;
        ++cursor;
    } else if (*cursor == 'B' || *cursor == 'b') {
        ++cursor;
    }
    if (*cursor == 'g' || *cursor == 'G') {
        ++cursor;
    }
    if (out_next) {
        *out_next = cursor;
    }
    return (uint32_t)(value * multiplier);
}

static bool parse_alt_memory_layout(const char *alt_name, stm32_dfu_memory_layout_t *layout)
{
    if (!layout) {
        return false;
    }
    layout->count = 0;
    if (!alt_name) {
        return false;
    }

    const char *cursor = alt_name;
    while ((cursor = strchr(cursor, '/')) != NULL) {
        ++cursor;
        if (!(cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X'))) {
            continue;
        }
        char *endptr = NULL;
        unsigned long start = strtoul(cursor, &endptr, 16);
        if (!endptr || *endptr != '/') {
            cursor = endptr ? endptr : cursor + 1;
            continue;
        }
        cursor = endptr + 1;
        unsigned long count = strtoul(cursor, &endptr, 10);
        if (!endptr || *endptr != '*') {
            cursor = endptr ? endptr : cursor + 1;
            continue;
        }
        cursor = endptr + 1;
        uint32_t page_size = parse_size_bytes(cursor, &cursor);
        if (page_size == 0) {
            continue;
        }
        if (layout->count < sizeof(layout->segments) / sizeof(layout->segments[0])) {
            stm32_dfu_segment_t *segment = &layout->segments[layout->count++];
            segment->start = (uint32_t)start;
            segment->page_size = page_size;
            uint64_t length = (uint64_t)count * page_size;
            segment->end = segment->start + (uint32_t)length;
        }
    }
    return layout->count > 0;
}

static const stm32_dfu_segment_t *find_segment(const stm32_dfu_memory_layout_t *layout, uint32_t address)
{
    if (!layout) {
        return NULL;
    }
    for (size_t i = 0; i < layout->count; ++i) {
        const stm32_dfu_segment_t *segment = &layout->segments[i];
        if (address >= segment->start && address < segment->end) {
            return segment;
        }
    }
    return NULL;
}

static bool load_firmware_file(const wchar_t *path,
                               unsigned char **out_data,
                               size_t *out_len,
                               char *status_buf,
                               size_t status_len,
                               void *status_ctx)
{
    if (!path || !out_data || !out_len) {
        set_status(status_buf, status_len, status_ctx, "Error: firmware path invalid");
        return false;
    }

    FILE *fp = NULL;
    if (_wfopen_s(&fp, path, L"rb") != 0 || !fp) {
        set_status(status_buf, status_len, status_ctx, "Error: unable to open firmware file");
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        set_status(status_buf, status_len, status_ctx, "Error: unable to read firmware size");
        return false;
    }
    long file_size = ftell(fp);
    if (file_size <= 0) {
        fclose(fp);
        set_status(status_buf, status_len, status_ctx, "Error: firmware file is empty");
        return false;
    }
    rewind(fp);

    unsigned char *data = (unsigned char *)malloc((size_t)file_size);
    if (!data) {
        fclose(fp);
        set_status(status_buf, status_len, status_ctx, "Error: insufficient memory for firmware");
        return false;
    }

    size_t read = fread(data, 1, (size_t)file_size, fp);
    fclose(fp);

    if (read != (size_t)file_size) {
        free(data);
        set_status(status_buf, status_len, status_ctx, "Error: failed to read firmware file");
        return false;
    }

    *out_data = data;
    *out_len = (size_t)file_size;
    return true;
}

static uint16_t parse_transfer_size(const struct libusb_interface_descriptor *alt)
{
    if (!alt || alt->extra_length < 9) {
        return STM32_DEFAULT_TRANSFER;
    }

    const unsigned char *extra = alt->extra;
    int remaining = alt->extra_length;
    while (remaining >= 9) {
        uint8_t length = extra[0];
        uint8_t type = extra[1];
        if (length < 3 || length > remaining) {
            break;
        }
        if (type == 0x21 && length >= 9) {
            uint16_t transfer = (uint16_t)(extra[5] | (extra[6] << 8));
            if (transfer == 0) {
                transfer = STM32_DEFAULT_TRANSFER;
            }
            return transfer;
        }
        extra += length;
        remaining -= length;
    }
    return STM32_DEFAULT_TRANSFER;
}

static bool select_dfu_interface(libusb_device *device,
                                 libusb_device_handle *handle,
                                 stm32_dfu_device_t *out_device)
{
    struct libusb_config_descriptor *config = NULL;
    int ret = libusb_get_active_config_descriptor(device, &config);
    if (ret != LIBUSB_SUCCESS || !config) {
        return false;
    }

    bool found = false;
    stm32_dfu_device_t selected = {0};

    for (int i = 0; i < config->bNumInterfaces && !found; ++i) {
        const struct libusb_interface *iface = &config->interface[i];
        for (int j = 0; j < iface->num_altsetting; ++j) {
            const struct libusb_interface_descriptor *alt = &iface->altsetting[j];
            if (alt->bInterfaceClass != LIBUSB_CLASS_APPLICATION ||
                alt->bInterfaceSubClass != 1 ||
                alt->bInterfaceProtocol != 2) {
                continue;
            }

            char name[sizeof(selected.alt_name)] = {0};
            if (alt->iInterface != 0) {
                libusb_get_string_descriptor_ascii(handle,
                                                   alt->iInterface,
                                                   (unsigned char *)name,
                                                   (int)sizeof(name));
            }

            bool is_internal = strstr(name, "Internal") != NULL;

            selected.handle = handle;
            selected.interface_number = alt->bInterfaceNumber;
            selected.alt_setting = alt->bAlternateSetting;
            selected.transfer_size = parse_transfer_size(alt);
            strncpy_s(selected.alt_name, sizeof(selected.alt_name), name, _TRUNCATE);

            found = true;
            if (is_internal) {
                break;
            }
        }
    }

    libusb_free_config_descriptor(config);

    if (found && out_device) {
        *out_device = selected;
    }
    return found;
}

static bool open_stm32_dfu_device_once(libusb_context *ctx,
                                       stm32_dfu_device_t *out_device,
                                       char *status_buf,
                                       size_t status_len,
                                       void *status_ctx)
{
    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        set_status(status_buf, status_len, status_ctx,
                   "Error: failed to enumerate USB devices (%s)",
                   libusb_error_name((int)count));
        return false;
    }

    bool success = false;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device *dev = list[i];
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(dev, &desc) != LIBUSB_SUCCESS) {
            continue;
        }
        if (desc.idVendor != STM32_VENDOR_ID || desc.idProduct != STM32_PRODUCT_ID) {
            continue;
        }

        libusb_device_handle *handle = NULL;
        int ret = libusb_open(dev, &handle);
        if (ret != LIBUSB_SUCCESS || !handle) {
            set_status(status_buf, status_len, status_ctx,
                       "Error: cannot open DFU device (%s)",
                       libusb_error_name(ret));
            continue;
        }

        stm32_dfu_device_t device = {0};
        if (!select_dfu_interface(dev, handle, &device)) {
            set_status(status_buf, status_len, status_ctx,
                       "Error: DFU interface not found on device");
            libusb_close(handle);
            continue;
        }

        #ifdef LIBUSB_API_VERSION
        libusb_detach_kernel_driver(handle, device.interface_number);
        #endif

        ret = libusb_claim_interface(handle, device.interface_number);
        if (ret != LIBUSB_SUCCESS) {
            set_status(status_buf, status_len, status_ctx,
                       "Error: failed to claim DFU interface (%s)",
                       libusb_error_name(ret));
            libusb_close(handle);
            continue;
        }

        ret = libusb_set_interface_alt_setting(handle,
                                               device.interface_number,
                                               device.alt_setting);
        if (ret != LIBUSB_SUCCESS) {
            set_status(status_buf, status_len, status_ctx,
                       "Error: failed to set DFU alt setting (%s)",
                       libusb_error_name(ret));
            libusb_release_interface(handle, device.interface_number);
            libusb_close(handle);
            continue;
        }

        device.handle = handle;
        device.last_erased_page = 0xFFFFFFFFu;
        device.layout_valid = parse_alt_memory_layout(device.alt_name, &device.layout);

        if (out_device) {
            *out_device = device;
        }
        success = true;
        break;
    }

    libusb_free_device_list(list, 1);
    return success;
}

static bool wait_for_stm32_dfu_device(libusb_context *ctx,
                                      stm32_dfu_device_t *out_device,
                                      DWORD timeout_ms,
                                      char *status_buf,
                                      size_t status_len,
                                      void *status_ctx)
{
    DWORD elapsed = 0;
    char last_error[160] = {0};

    while (elapsed <= timeout_ms) {
        if (open_stm32_dfu_device_once(ctx, out_device,
                                       last_error, sizeof(last_error),
                                       status_ctx)) {
            if (status_buf && status_len) {
                if (out_device->alt_name[0]) {
                    set_status(status_buf, status_len, status_ctx,
                               "Using DFU interface %u: %s",
                               out_device->alt_setting,
                               out_device->alt_name);
                } else {
                    set_status(status_buf, status_len, status_ctx,
                               "Using DFU interface %u",
                               out_device->alt_setting);
                }
            }
            return true;
        }

        Sleep(100);
        elapsed += 100;
    }

    if (status_buf && status_len) {
        if (last_error[0]) {
            set_status(status_buf, status_len, status_ctx, "%s", last_error);
        } else {
            set_status(status_buf, status_len, status_ctx,
                       "Error: DFU device not found (VID=0x%04X PID=0x%04X)",
                       STM32_VENDOR_ID, STM32_PRODUCT_ID);
        }
    }
    return false;
}

static void release_device(stm32_dfu_device_t *device)
{
    if (!device || !device->handle) {
        return;
    }
    libusb_release_interface(device->handle, device->interface_number);
    libusb_close(device->handle);
    device->handle = NULL;
}

static int dfu_get_status(stm32_dfu_device_t *device, dfu_status_t *out_status)
{
    unsigned char buffer[6] = {0};
    int transferred = libusb_control_transfer(
        device->handle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
        DFU_REQUEST_GETSTATUS,
        0,
        device->interface_number,
        buffer,
        sizeof(buffer),
        1000);

    if (transferred < 0) {
        return transferred;
    }
    if (transferred != (int)sizeof(buffer)) {
        return LIBUSB_ERROR_IO;
    }

    if (out_status) {
        out_status->status = buffer[0];
        out_status->poll_timeout_ms = (uint32_t)buffer[1]
                                     | ((uint32_t)buffer[2] << 8)
                                     | ((uint32_t)buffer[3] << 16);
        out_status->state = buffer[4];
    }
    return LIBUSB_SUCCESS;
}

static void dfu_clear_status(stm32_dfu_device_t *device)
{
    libusb_control_transfer(device->handle,
        LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
        DFU_REQUEST_CLRSTATUS,
        0,
        device->interface_number,
        NULL,
        0,
        1000);
}

static void dfu_abort(stm32_dfu_device_t *device)
{
    libusb_control_transfer(device->handle,
        LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
        DFU_REQUEST_ABORT,
        0,
        device->interface_number,
        NULL,
        0,
        1000);
}

static bool dfu_wait_ready(stm32_dfu_device_t *device,
                           char *status_buf,
                           size_t status_len,
                           void *status_ctx,
                           bool allow_manifest)
{
    for (;;) {
        dfu_status_t st = {0};
        int ret = dfu_get_status(device, &st);
        if (ret == LIBUSB_ERROR_NO_DEVICE) {
            return allow_manifest;
        }
        if (ret != LIBUSB_SUCCESS) {
            set_status(status_buf, status_len, status_ctx,
                       "Error: failed to read DFU status (%s)",
                       libusb_error_name(ret));
            return false;
        }

        if (st.status != 0) {
            dfu_clear_status(device);
            set_status(status_buf, status_len, status_ctx,
                       "Error: DFU status 0x%02X (state=0x%02X)",
                       st.status, st.state);
            return false;
        }

        switch (st.state) {
        case DFU_STATE_dfuIDLE:
        case DFU_STATE_dfuDNLOAD_IDLE:
            return true;
        case DFU_STATE_dfuMANIFEST_SYNC:
        case DFU_STATE_dfuMANIFEST:
        case DFU_STATE_dfuMANIFEST_WAIT_RESET:
            return allow_manifest;
        case DFU_STATE_dfuERROR:
            dfu_clear_status(device);
            set_status(status_buf, status_len, status_ctx,
                       "Error: DFU entered error state");
            return false;
        default:
            break;
        }

        DWORD wait_ms = st.poll_timeout_ms > 0 ? st.poll_timeout_ms : 5;
        Sleep(wait_ms);
    }
}

static bool dfu_download_block(stm32_dfu_device_t *device,
                               uint16_t block_number,
                               const uint8_t *data,
                               uint16_t length,
                               char *status_buf,
                               size_t status_len,
                               void *status_ctx,
                               bool allow_manifest)
{
    int ret = libusb_control_transfer(
        device->handle,
        LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
        DFU_REQUEST_DNLOAD,
        block_number,
        device->interface_number,
        (unsigned char *)data,
        length,
        1000);

    if (ret < 0) {
        set_status(status_buf, status_len, status_ctx,
                   "Error: failed to send DFU data (%s)",
                   libusb_error_name(ret));
        return false;
    }
    if (ret != length) {
        set_status(status_buf, status_len, status_ctx,
                   "Error: DFU data sent incomplete");
        return false;
    }

    return dfu_wait_ready(device, status_buf, status_len, status_ctx, allow_manifest);
}

static bool dfu_send_special_command(stm32_dfu_device_t *device,
                                     uint8_t command,
                                     uint32_t address,
                                     uint8_t length,
                                     char *status_buf,
                                     size_t status_len,
                                     void *status_ctx,
                                     bool allow_manifest)
{
    uint8_t payload[5] = {
        command,
        (uint8_t)(address & 0xFF),
        (uint8_t)((address >> 8) & 0xFF),
        (uint8_t)((address >> 16) & 0xFF),
        (uint8_t)((address >> 24) & 0xFF)
    };
    if (length > sizeof(payload)) {
        length = sizeof(payload);
    }
    return dfu_download_block(device,
                              0,
                              payload,
                              length,
                              status_buf,
                              status_len,
                              status_ctx,
                              allow_manifest);
}

static bool dfu_set_address_pointer(stm32_dfu_device_t *device,
                                    uint32_t address,
                                    char *status_buf,
                                    size_t status_len,
                                    void *status_ctx)
{
    return dfu_send_special_command(device, 0x21, address, 5,
                                    status_buf, status_len, status_ctx, false);
}

static bool dfu_mass_erase(stm32_dfu_device_t *device,
                           char *status_buf,
                           size_t status_len,
                           void *status_ctx)
{
    return dfu_send_special_command(device, 0x41, 0, 1,
                                    status_buf, status_len, status_ctx, false);
}

static bool dfu_erase_range(stm32_dfu_device_t *device,
                            uint32_t address,
                            uint32_t length,
                            char *status_buf,
                            size_t status_len,
                            void *status_ctx)
{
    if (!device->layout_valid || device->layout.count == 0) {
        return true;
    }

    uint32_t start = address;
    uint32_t end = address + length;
    while (start < end) {
        const stm32_dfu_segment_t *segment = find_segment(&device->layout, start);
        if (!segment) {
            set_status(status_buf, status_len, status_ctx,
                       "Error: address 0x%08X outside DFU segment", start);
            return false;
        }

        uint32_t page_base = segment->start +
            ((start - segment->start) / segment->page_size) * segment->page_size;
        while (page_base < segment->end && page_base < end) {
            if (device->last_erased_page != page_base) {
                if (!dfu_send_special_command(device, 0x41, page_base, 5,
                                              status_buf, status_len, status_ctx, false)) {
                    return false;
                }
                device->last_erased_page = page_base;
            }
            page_base += segment->page_size;
        }

        start = segment->end;
    }
    return true;
}

static bool dfu_prepare_idle(stm32_dfu_device_t *device,
                             char *status_buf,
                             size_t status_len,
                             void *status_ctx)
{
    if (!dfu_wait_ready(device, status_buf, status_len, status_ctx, true)) {
        dfu_abort(device);
        Sleep(5);
        return dfu_wait_ready(device, status_buf, status_len, status_ctx, true);
    }
    return true;
}

bool dfu_loader_flash(const wchar_t *firmware_path,
                      dfu_loader_progress_cb progress_cb,
                      void *progress_ctx,
                      char *status_buf,
                      size_t status_buf_len)
{
    unsigned char *firmware_data = NULL;
    size_t firmware_len = 0;
    libusb_context *ctx = NULL;
    stm32_dfu_device_t device = {0};
    uint8_t *chunk_buffer = NULL;
    bool success = false;

    report_progress(progress_cb, progress_ctx, 0);
    set_status(status_buf, status_buf_len, progress_ctx, "Loading firmware file...");

    if (!load_firmware_file(firmware_path, &firmware_data, &firmware_len,
                            status_buf, status_buf_len, progress_ctx)) {
        goto cleanup;
    }

    set_status(status_buf, status_buf_len, progress_ctx, "Initialising USB...");
    int ret = libusb_init(&ctx);
    if (ret != LIBUSB_SUCCESS) {
        set_status(status_buf, status_buf_len, progress_ctx,
                   "Error: libusb init failed (%s)", libusb_error_name(ret));
        goto cleanup;
    }

    set_status(status_buf, status_buf_len, progress_ctx, "Waiting for DFU device...");
    if (!wait_for_stm32_dfu_device(ctx, &device, 10000, status_buf, status_buf_len, progress_ctx)) {
        goto cleanup;
    }

    set_status(status_buf, status_buf_len, progress_ctx, "Preparing DFU state...");
    if (!dfu_prepare_idle(&device, status_buf, status_buf_len, progress_ctx)) {
        goto cleanup;
    }

    if (device.layout_valid) {
        device.mass_erased = false;
        device.last_erased_page = 0xFFFFFFFFu;
    } else {
        set_status(status_buf, status_buf_len, progress_ctx, "Performing mass erase...");
        report_progress(progress_cb, progress_ctx, 3);
        if (!dfu_mass_erase(&device, status_buf, status_buf_len, progress_ctx)) {
            goto cleanup;
        }
        device.mass_erased = true;
        if (!dfu_prepare_idle(&device, status_buf, status_buf_len, progress_ctx)) {
            goto cleanup;
        }
        device.last_erased_page = 0xFFFFFFFFu;
    }

    set_status(status_buf, status_buf_len, progress_ctx,
               "Setting write address 0x%08X", STM32_BASE_ADDRESS);
    if (!dfu_set_address_pointer(&device, STM32_BASE_ADDRESS, status_buf, status_buf_len, progress_ctx)) {
        goto cleanup;
    }

    report_progress(progress_cb, progress_ctx, device.layout_valid ? 6 : 5);
    set_status(status_buf, status_buf_len, progress_ctx, "Writing firmware...");

    chunk_buffer = (uint8_t *)malloc(device.transfer_size + 1);
    if (!chunk_buffer) {
        set_status(status_buf, status_buf_len, progress_ctx,
                   "Error: insufficient memory for transfer buffer");
        goto cleanup;
    }

    const uint16_t data_block_number = 2;
    size_t written = 0;
    while (written < firmware_len) {
        size_t remaining = firmware_len - written;
        size_t chunk_size = (remaining > device.transfer_size)
                               ? device.transfer_size
                               : remaining;

        uint32_t chunk_address = STM32_BASE_ADDRESS + (uint32_t)written;
        const stm32_dfu_segment_t *segment = NULL;
        if (device.layout_valid) {
            segment = find_segment(&device.layout, chunk_address);
            if (!segment) {
                set_status(status_buf, status_buf_len, progress_ctx,
                           "Error: address 0x%08X outside DFU segment",
                           chunk_address);
                goto cleanup;
            }
            uint32_t seg_remaining = segment->end - chunk_address;
            if (seg_remaining < chunk_size) {
                chunk_size = seg_remaining;
            }
        }

        if (!device.mass_erased && segment) {
            if (!dfu_erase_range(&device,
                                 chunk_address,
                                 (uint32_t)chunk_size,
                                 status_buf,
                                 status_buf_len,
                                 progress_ctx)) {
                goto cleanup;
            }
        }

        if (!dfu_set_address_pointer(&device, chunk_address, status_buf, status_buf_len, progress_ctx)) {
            goto cleanup;
        }

        size_t data_len = chunk_size;
        memcpy(chunk_buffer, firmware_data + written, data_len);
        size_t aligned_len = (data_len % 2) ? (data_len + 1) : data_len;
        if (aligned_len > data_len) {
            chunk_buffer[data_len] = 0xFF;
        }

        if (!dfu_download_block(&device,
                                data_block_number,
                                chunk_buffer,
                                (uint16_t)aligned_len,
                                status_buf,
                                status_buf_len,
                                progress_ctx,
                                false)) {
            char detail[128];
            _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                        "Error: write failed at 0x%08X (len=%u)",
                        chunk_address,
                        (unsigned)aligned_len);
            set_status(status_buf, status_buf_len, progress_ctx, "%s", detail);
            goto cleanup;
        }

        written += data_len;

        int percent = (device.layout_valid ? 6 : 5) + (int)((written * 94ULL) / firmware_len);
        report_progress(progress_cb, progress_ctx, percent);
    }

    set_status(status_buf, status_buf_len, progress_ctx, "Finalizing firmware...");
    report_progress(progress_cb, progress_ctx, 100);

    if (!dfu_download_block(&device, data_block_number, NULL, 0,
                            status_buf, status_buf_len, progress_ctx, true)) {
        goto cleanup;
    }

    set_status(status_buf, status_buf_len, progress_ctx, "Firmware written, device restarting");
    success = true;

cleanup:
    if (chunk_buffer) {
        free(chunk_buffer);
    }

    release_device(&device);

    if (ctx) {
        libusb_exit(ctx);
    }

    if (firmware_data) {
        free(firmware_data);
    }

    if (!success && status_buf && status_buf_len && status_buf[0] == '\0') {
        set_status(status_buf, status_buf_len, progress_ctx, "Unknown DFU failure");
    }

    return success;
}

bool dfu_loader_flash_memory(const unsigned char *input_firmware_data,
                             size_t input_firmware_len,
                             dfu_loader_progress_cb progress_cb,
                             void *progress_ctx,
                             char *status_buf,
                             size_t status_buf_len)
{
    unsigned char *firmware_data = NULL;
    size_t firmware_len = 0;
    libusb_context *ctx = NULL;
    stm32_dfu_device_t device = {0};
    uint8_t *chunk_buffer = NULL;
    bool success = false;

    if (!input_firmware_data || input_firmware_len == 0) {
        set_status(status_buf, status_buf_len, progress_ctx, "Error: invalid firmware data");
        return false;
    }

    // 复制固件数据
    firmware_data = (unsigned char *)malloc(input_firmware_len);
    if (!firmware_data) {
        set_status(status_buf, status_buf_len, progress_ctx, "Error: insufficient memory for firmware");
        return false;
    }
    memcpy(firmware_data, input_firmware_data, input_firmware_len);
    firmware_len = input_firmware_len;

    report_progress(progress_cb, progress_ctx, 0);
    set_status(status_buf, status_buf_len, progress_ctx, "Firmware loaded from memory");

    set_status(status_buf, status_buf_len, progress_ctx, "Initialising USB...");
    int ret = libusb_init(&ctx);
    if (ret != LIBUSB_SUCCESS) {
        set_status(status_buf, status_buf_len, progress_ctx,
                   "Error: libusb init failed (%s)", libusb_error_name(ret));
        goto cleanup;
    }

    set_status(status_buf, status_buf_len, progress_ctx, "Waiting for DFU device...");
    if (!wait_for_stm32_dfu_device(ctx, &device, 10000, status_buf, status_buf_len, progress_ctx)) {
        goto cleanup;
    }

    set_status(status_buf, status_buf_len, progress_ctx, "Preparing DFU state...");
    if (!dfu_prepare_idle(&device, status_buf, status_buf_len, progress_ctx)) {
        goto cleanup;
    }

    if (device.layout_valid) {
        device.mass_erased = false;
        device.last_erased_page = 0xFFFFFFFFu;
    } else {
        set_status(status_buf, status_buf_len, progress_ctx, "Performing mass erase...");
        report_progress(progress_cb, progress_ctx, 3);
        if (!dfu_mass_erase(&device, status_buf, status_buf_len, progress_ctx)) {
            goto cleanup;
        }
        device.mass_erased = true;
        if (!dfu_prepare_idle(&device, status_buf, status_buf_len, progress_ctx)) {
            goto cleanup;
        }
        device.last_erased_page = 0xFFFFFFFFu;
    }

    set_status(status_buf, status_buf_len, progress_ctx,
               "Setting write address 0x%08X", STM32_BASE_ADDRESS);
    if (!dfu_set_address_pointer(&device, STM32_BASE_ADDRESS, status_buf, status_buf_len, progress_ctx)) {
        goto cleanup;
    }

    report_progress(progress_cb, progress_ctx, device.layout_valid ? 6 : 5);
    set_status(status_buf, status_buf_len, progress_ctx, "Writing firmware...");

    chunk_buffer = (uint8_t *)malloc(device.transfer_size + 1);
    if (!chunk_buffer) {
        set_status(status_buf, status_buf_len, progress_ctx,
                   "Error: insufficient memory for transfer buffer");
        goto cleanup;
    }

    const uint16_t data_block_number = 2;
    size_t written = 0;
    while (written < firmware_len) {
        size_t remaining = firmware_len - written;
        size_t chunk_size = (remaining > device.transfer_size)
                               ? device.transfer_size
                               : remaining;

        uint32_t chunk_address = STM32_BASE_ADDRESS + (uint32_t)written;
        const stm32_dfu_segment_t *segment = NULL;
        if (device.layout_valid) {
            segment = find_segment(&device.layout, chunk_address);
            if (!segment) {
                set_status(status_buf, status_buf_len, progress_ctx,
                           "Error: address 0x%08X outside DFU segment",
                           chunk_address);
                goto cleanup;
            }
            uint32_t seg_remaining = segment->end - chunk_address;
            if (seg_remaining < chunk_size) {
                chunk_size = seg_remaining;
            }
        }

        if (!device.mass_erased && segment) {
            if (!dfu_erase_range(&device,
                                 chunk_address,
                                 (uint32_t)chunk_size,
                                 status_buf,
                                 status_buf_len,
                                 progress_ctx)) {
                goto cleanup;
            }
        }

        if (!dfu_set_address_pointer(&device, chunk_address, status_buf, status_buf_len, progress_ctx)) {
            goto cleanup;
        }

        size_t data_len = chunk_size;
        memcpy(chunk_buffer, firmware_data + written, data_len);
        size_t aligned_len = (data_len % 2) ? (data_len + 1) : data_len;
        if (aligned_len > data_len) {
            chunk_buffer[data_len] = 0xFF;
        }

        if (!dfu_download_block(&device,
                                data_block_number,
                                chunk_buffer,
                                (uint16_t)aligned_len,
                                status_buf,
                                status_buf_len,
                                progress_ctx,
                                false)) {
            char detail[128];
            _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                        "Error: write failed at 0x%08X (len=%u)",
                        chunk_address,
                        (unsigned)aligned_len);
            set_status(status_buf, status_buf_len, progress_ctx, "%s", detail);
            goto cleanup;
        }

        written += data_len;

        int percent = (device.layout_valid ? 6 : 5) + (int)((written * 94ULL) / firmware_len);
        report_progress(progress_cb, progress_ctx, percent);
    }

    set_status(status_buf, status_buf_len, progress_ctx, "Finalizing firmware...");
    report_progress(progress_cb, progress_ctx, 100);

    if (!dfu_download_block(&device, data_block_number, NULL, 0,
                            status_buf, status_buf_len, progress_ctx, true)) {
        goto cleanup;
    }

    set_status(status_buf, status_buf_len, progress_ctx, "Firmware written, device restarting");
    success = true;

cleanup:
    if (chunk_buffer) {
        free(chunk_buffer);
    }

    release_device(&device);

    if (ctx) {
        libusb_exit(ctx);
    }

    if (firmware_data) {
        free(firmware_data);
    }

    if (!success && status_buf && status_buf_len && status_buf[0] == '\0') {
        set_status(status_buf, status_buf_len, progress_ctx, "Unknown DFU failure");
    }

    return success;
}
