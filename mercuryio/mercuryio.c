#include <windows.h>

#include <limits.h>
#include <stdint.h>
#include <process.h>

#include "mercuryio.h"
#include "config.h"

#include "serialslider.h"
extern char comPort[6];
char* vid = "VID_AFF1";
char* pid = "PID_52A5";

static unsigned int __stdcall mercury_io_touch_thread_proc(void *ctx);

static uint8_t mercury_opbtn;
static uint8_t mercury_gamebtn;
static struct mercury_io_config mercury_io_cfg;
static bool mercury_io_touch_stop_flag;
static HANDLE mercury_io_touch_thread;

uint16_t mercury_io_get_api_version(void)
{
    return 0x0100;
}

HRESULT mercury_io_init(void)
{
    mercury_io_config_load(&mercury_io_cfg, L".\\segatools.ini");

    return S_OK;
}

HRESULT mercury_io_poll(void)
{
    mercury_opbtn = 0;
    mercury_gamebtn = 0;

    if (GetAsyncKeyState(mercury_io_cfg.vk_test)) {
        mercury_opbtn |= MERCURY_IO_OPBTN_TEST;
    }

    if (GetAsyncKeyState(mercury_io_cfg.vk_service)) {
        mercury_opbtn |= MERCURY_IO_OPBTN_SERVICE;
    }

    if (GetAsyncKeyState(mercury_io_cfg.vk_coin)) {
        mercury_opbtn |= MERCURY_IO_OPBTN_COIN;
    }

    if (GetAsyncKeyState(mercury_io_cfg.vk_vol_up)) {
        mercury_gamebtn |= MERCURY_IO_GAMEBTN_VOL_UP;
    }

    if (GetAsyncKeyState(mercury_io_cfg.vk_vol_down)) {
        mercury_gamebtn |= MERCURY_IO_GAMEBTN_VOL_DOWN;
    }

    return S_OK;
}

void mercury_io_get_opbtns(uint8_t *opbtn)
{
    if (opbtn != NULL) {
        *opbtn = mercury_opbtn;
    }
}

void mercury_io_get_gamebtns(uint8_t *gamebtn)
{
    if (gamebtn != NULL) {
        *gamebtn = mercury_gamebtn;
    }
}

HRESULT mercury_io_touch_init(void)
{
    // Open ports
    memcpy(comPort,GetSerialPortByVidPid(vid,pid),6);
    if(*comPort == 0x48){ //找不到对应设备会返回“0”
        char* default_comPort = "COM1";
        memcpy(comPort,default_comPort,5);
    }
    open_port();
    return S_OK;
}

void mercury_io_touch_start(mercury_io_touch_callback_t callback)
{
    if (mercury_io_touch_thread != NULL) {
        return;
    }

    mercury_io_touch_thread = (HANDLE) _beginthreadex(
        NULL,
        0,
        mercury_io_touch_thread_proc,
        callback,
        0,
        NULL
    );
}

void mercury_io_touch_set_leds(struct led_data data)
{
    //slider_send_leds(rgb);
}

static unsigned int __stdcall mercury_io_touch_thread_proc(void *ctx)
{
    mercury_io_touch_callback_t callback;
    bool cellPressed[240];
    size_t i;

    callback = ctx;
    slider_packet_t reponse;
	BOOL ESC = FALSE;
    package_init(&reponse);	
    while (1) {
        switch (serial_read_cmd(&reponse)) {
		    case SLIDER_CMD_AUTO_SCAN:
			    memcpy(cellPressed, reponse.pressure, 32);
                package_init(&reponse);
                callback(cellPressed);
			    break;
            case 0xff:
                memset(cellPressed,0, 240);
                callback(cellPressed);
                close_port();
                while(!open_port()){
                    close_port();
                    memcpy(comPort,GetSerialPortByVidPid(vid,pid),6);
                    if(*comPort == 0x48){ //找不到对应设备会返回“0”
                        char* default_comPort = "COM1";
                        memcpy(comPort,default_comPort,5);
                    }
                    //memset(pressure,0, 32);
                    callback(cellPressed);
                    Sleep(1);
                }
                Sleep(1);
                slider_start_scan();
                callback(cellPressed);
                break;
            default:
                break;
        }
    }
    return 0;
}
