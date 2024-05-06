#include <windows.h>

#include <process.h>
#include <stdbool.h>
#include <stdint.h>

#include "chuniio.h"
#include "config.h"

#include "serialslider.h"

uint8_t Air_key_Status;
uint8_t Serial_CMD_Flag;

static unsigned int __stdcall chuni_io_slider_thread_proc(void *ctx);

static bool chuni_io_coin;
static uint16_t chuni_io_coins;
static uint8_t chuni_io_hand_pos;
static HANDLE chuni_io_slider_thread;
static bool chuni_io_slider_stop_flag;
static struct chuni_io_config chuni_io_cfg;


uint16_t chuni_io_get_api_version(void)
{
    return 0x0102;
}

HRESULT chuni_io_jvs_init(void)
{
    chuni_io_config_load(&chuni_io_cfg, L".\\segatools.ini");

    return S_OK;
}

void chuni_io_jvs_read_coin_counter(uint16_t *out)
{
    if (out == NULL) {
        return;
    }

    if (GetAsyncKeyState(chuni_io_cfg.vk_coin)) {
        if (!chuni_io_coin) {
            chuni_io_coin = true;
            chuni_io_coins++;
        }
    } else {
        chuni_io_coin = false;
    }

    *out = chuni_io_coins;
}

void chuni_io_jvs_poll(uint8_t *opbtn, uint8_t *beams)
{
    size_t i;

    if (GetAsyncKeyState(chuni_io_cfg.vk_test)) {
        *opbtn |= 0x01; /* Test */
    }

    if (GetAsyncKeyState(chuni_io_cfg.vk_service)) {
        *opbtn |= 0x02; /* Service */
    }
    *beams = Air_key_Status;
}

HRESULT chuni_io_slider_init(void)
{
	// Open ports
    open_port();
    return S_OK;
}

void chuni_io_slider_start(chuni_io_slider_callback_t callback)
{
    slider_start_scan();
    if (chuni_io_slider_thread != NULL) {
        return;
    }

    chuni_io_slider_thread = (HANDLE) _beginthreadex(
            NULL,
            0,
            chuni_io_slider_thread_proc,
            callback,
            0,
            NULL);
}

void chuni_io_slider_stop(void)
{
    slider_stop_scan();

    if (chuni_io_slider_thread == NULL) {
        return;
    }

    chuni_io_slider_stop_flag = true;

    WaitForSingleObject(chuni_io_slider_thread, INFINITE);
    CloseHandle(chuni_io_slider_thread);
    chuni_io_slider_thread = NULL;
    chuni_io_slider_stop_flag = false;
}

void chuni_io_slider_set_leds(const uint8_t *rgb)
{
    slider_send_leds(rgb);
}

void chuni_io_led_set_colors(uint8_t board,uint8_t *rgb_raw)
{
    uint8_t air_rgb[9];
    if(board == 0){
        for(uint8_t i=0;i<9;i++){
            air_rgb[i] = rgb_raw[150+i];
        }
    }
    else if(board == 1){
        for(uint8_t i=0;i<9;i++){
            air_rgb[i] = rgb_raw[180+i];
        }
    }
}

HRESULT chuni_io_led_init(void)
{
    return S_OK;
}

static unsigned int __stdcall chuni_io_slider_thread_proc(void *ctx)
{
    chuni_io_slider_callback_t callback;
    slider_packet_t reponse;
    uint8_t pressure[32];
    size_t i;

    callback = ctx;

    while (!chuni_io_slider_stop_flag) {
        switch (sliderserial_readreq(&reponse)) {
		    case SLIDER_CMD_AUTO_SCAN:
			    memcpy(pressure, reponse.pressure, 32);
                //memset(pressure,20,32);
			    break;
            case SLIDER_CMD_AUTO_AIR:
                Air_key_Status = reponse.air_status;
                break;
            // case 0:
            //     memset(pressure,10,32);
            //     break;
            // case 0xff:
            //     memset(pressure,30,32);
            //     break;
            // case 0xfd:
            //     memset(pressure,40,32);
            //     break;
            default:
                break;
        }
        callback(pressure);
        Sleep(1);
    }
    return 0;
}