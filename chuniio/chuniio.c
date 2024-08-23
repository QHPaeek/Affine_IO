#include <windows.h>

#include <process.h>
#include <stdbool.h>
#include <stdint.h>

#include "chuniio.h"
#include "config.h"

#include "serialslider.h"

uint8_t Air_key_Status;
uint8_t Serial_CMD_Flag;
extern char comPort[6];
char* vid = "VID_AFF1";
char* pid = "PID_52A4";

static unsigned int __stdcall chuni_io_slider_thread_proc(void *ctx);

static bool chuni_io_coin;
static uint16_t chuni_io_coins;
static uint8_t chuni_io_hand_pos;
static HANDLE chuni_io_slider_thread;
static bool chuni_io_slider_stop_flag;
static struct chuni_io_config chuni_io_cfg;

typedef struct {
    chuni_io_slider_callback_t callback;
    Queue* queue;
} callback_context;

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
    memcpy(comPort,GetSerialPortByVidPid(vid,pid),6);
    if(*comPort == 0x48){ //找不到对应设备会返回“0”
        char* default_comPort = "COM1";
        memcpy(comPort,default_comPort,5);
    }
    open_port();
    return S_OK;
}

void chuni_io_slider_start(chuni_io_slider_callback_t callback)
{
    Sleep(1);
    slider_start_air_scan();
    slider_start_scan();
    if (chuni_io_slider_thread != NULL) {
        return;
    }

    InitializeCriticalSection(&cs);
    Queue* queue = createQueue(100);
    callback_context* ctx = (callback_context*)malloc(sizeof(callback_context));
    ctx->callback = callback;
    ctx->queue = queue;
    // CreateThread(NULL, 0, sliderserial_read_thread, queue, 0, NULL);
    chuni_io_slider_thread = (HANDLE) _beginthreadex(NULL,0,chuni_io_slider_thread_proc,ctx,0,NULL);
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
    // uint8_t air_rgb[9];
    // if(board == 0){
    //     for(uint8_t i=0;i<9;i++){
    //         air_rgb[i] = rgb_raw[150+i];
    //     }
    // }
    // else if(board == 1){
    //     for(uint8_t i=0;i<9;i++){
    //         air_rgb[i] = rgb_raw[180+i];
    //     }
    // }
}

HRESULT chuni_io_led_init(void)
{
    return S_OK;
}

static unsigned int __stdcall chuni_io_slider_thread_proc(void* param)
{
    callback_context* ctx = (callback_context*)param;
    chuni_io_slider_callback_t callback = ctx->callback;
    //Queue* queue = ctx->queue;
    slider_packet_t reponse;
    uint8_t pressure[32];
	BOOL ESC = FALSE;
	// uint8_t result = read_serial_port(buffer, recv_len) ;
	// while(result == 0 && result == 2){
	// 	result = read_serial_port(buffer, recv_len);
	// }
    package_init(&reponse);	
    while (!chuni_io_slider_stop_flag) {
        switch (serial_read_cmd(&reponse)) {
		    case SLIDER_CMD_AUTO_SCAN:
			    memcpy(pressure, reponse.pressure, 32);
                if(reponse.size == 33){
                    //32个触摸按键后跟随一位天键
                    Air_key_Status = reponse.air_status;
                }
                package_init(&reponse);
                callback(pressure);
			    break;
            case SLIDER_CMD_AUTO_AIR:
                Air_key_Status = reponse._air_status;
                package_init(&reponse);
                break;
            case 0xff:
                memset(pressure,0, 32);
                callback(pressure);
                close_port();
                while(!open_port()){
                    close_port();
                    memcpy(comPort,GetSerialPortByVidPid(vid,pid),6);
                    if(*comPort == 0x48){ //找不到对应设备会返回“0”
                        char* default_comPort = "COM1";
                        memcpy(comPort,default_comPort,5);
                    }
                    //memset(pressure,0, 32);
                    callback(pressure);
                    Sleep(1);
                }
                Sleep(1);
                slider_start_air_scan();
                slider_start_scan();
                callback(pressure);
                break;
            default:
                break;
        }
        // if (!IsSerialPortOpen()) {
        //     close_port();
        //     while(!open_port()){
        //         close_port();
        //         memset(pressure,0, 32);
        //         callback(pressure);
        //         Sleep(1);
        //     }
        //     slider_start_air_scan();
        //     slider_start_scan();
        // }
    }
    return 0;
}
