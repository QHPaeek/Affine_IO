#ifndef MAI2IO_DFU_LOADER_H
#define MAI2IO_DFU_LOADER_H

#include <stddef.h>
#include <stdbool.h>
#include <wchar.h>

typedef void (*dfu_loader_progress_cb)(int percent, void *ctx);
typedef void (*dfu_loader_status_cb)(const char *message, void *ctx);

#define DFU_LOADER_CLIENT_MAGIC 0x44465543u

typedef struct dfu_loader_client {
    unsigned int magic;
    dfu_loader_progress_cb progress_cb;
    dfu_loader_status_cb status_cb;
    void *user_ctx;
} dfu_loader_client_t;

bool dfu_loader_flash(const wchar_t *firmware_path,
                      dfu_loader_progress_cb progress_cb,
                      void *progress_ctx,
                      char *status_buf,
                      size_t status_buf_len);

bool dfu_loader_flash_memory(const unsigned char *firmware_data,
                             size_t firmware_len,
                             dfu_loader_progress_cb progress_cb,
                             void *progress_ctx,
                             char *status_buf,
                             size_t status_buf_len);

#endif /* MAI2IO_DFU_LOADER_H */
