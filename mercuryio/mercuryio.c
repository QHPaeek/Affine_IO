#include <windows.h>

#include <limits.h>
#include <stdint.h>
#include <process.h>

#include "mercuryio.h"
#include "config.h"

#include "serialslider.h"
extern char comPort[13];
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
    if(comPort[5] != 0){
        int port_num = (comPort[3]-48)*100 + (comPort[4]-48)*10 + (comPort[5]-48);
        snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
    }else if(comPort[4] != 0){
        int port_num = (comPort[3]-48)*10 + (comPort[4]-48);
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }else{
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
    uint8_t cell_raw[30];
    size_t i;

    callback = ctx;
    slider_packet_t reponse;
	BOOL ESC = FALSE;
    package_init(&reponse);	
    while (1) {
        switch (serial_read_cmd(&reponse)) {
		    case SLIDER_CMD_AUTO_SCAN:
			    memcpy(cell_raw, reponse.cell, 30);
                package_init(&reponse);
                for(uint8_t i = 0;i<30;i++){
                    for(uint8_t y=0;y<8;y++){
                        if(cell_raw[i] & (1 << y)){
                            cellPressed[i*8+y] = true;
                        }else{
                            cellPressed[i*8+y] = false;
                        }
                    }
                }
                callback(cellPressed);
			    break;
            case 0xff:
                for(uint8_t i=0;i<240;i++){
                    cellPressed[i] = false;
                }
                callback(cellPressed);
                close_port();
                while(!open_port()){
                    close_port();
                    	// Open ports
                    memcpy(comPort,GetSerialPortByVidPid(vid,pid),6);
                    if(comPort[5] != 0){
                        int port_num = (comPort[3]-48)*100 + (comPort[4]-48)*10 + (comPort[5]-48);
                        snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
                    }else if(comPort[4] != 0){
                        int port_num = (comPort[3]-48)*10 + (comPort[4]-48);
                        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
                    }else{
                        char* default_comPort = "COM1";
                        memcpy(comPort,default_comPort,5);
                    }
                    open_port();
                    //memset(pressure,0, 32);
                    callback(cellPressed);
                    Sleep(1);
                }
                Sleep(1);
                slider_start_scan();
                callback(cellPressed);
                break;
            case 0xfe:
                //printf("fe\n");
                break;
            default:
                // for(uint8_t i=0;i<240;i++){
                //     cellPressed[i] = false;
                // }
                // callback(cellPressed);
                break;
        }
    }
    return 0;
}
