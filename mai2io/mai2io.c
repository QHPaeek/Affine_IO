#include <windows.h>
#include <process.h>
#include <limits.h>
#include <stdint.h>
#include "config.h"
#include "mai2io.h"
#include "serial.h"
#include "dprintf.h"

#define ARRAY_LENGTH 34
#define DEFAULT_VALUE 128

#define DEBUG

extern char comPort1[13]; //串口号
extern char comPort2[13]; //串口号
extern HANDLE hPort1; // 串口句柄
extern HANDLE hPort2; // 串口句柄
static uint8_t opts1 = 0;
static uint8_t opts2 = 0;
static uint8_t p1 = 0,p2 = 0;
static uint8_t touch_threshold[34] = {0};
static uint8_t FET_led[3];
static uint8_t serial_stop_flag_1 = 1;
static uint8_t serial_stop_flag_2 = 1;
static char* Vid = "VID_AFF1";
static char* Pid_1p = "PID_52A5";
static char* Pid_2p = "PID_52A6";

static struct mai2_io_config mai2_io_cfg;
mai2_io_touch_callback_t _callback;
static HANDLE mai2_io_touch_1p_thread;
static bool mai2_io_touch_1p_stop_flag;
static HANDLE mai2_io_touch_2p_thread;
static bool mai2_io_touch_2p_stop_flag;

static uint8_t thread_flag = 0;

uint16_t mai2_io_get_api_version(void)
{
    return 0x0101;
}

HRESULT mai2_io_init(void)
{
    #ifdef DEBUG
    dprintf("Affine IO:mai2_io_init\n");
    #endif
    mai2_io_config_load(&mai2_io_cfg, L".\\segatools.ini");
    //read_json_to_threshold("curva_config.json", touch_threshold);
    return S_OK;
}

HRESULT mai2_io_poll(void)
{  
    #ifdef DEBUG
    dprintf("Affine IO:mai2_io_poll");
    #endif
    return S_OK;
}

void mai2_io_get_opbtns(uint8_t *opbtn){
    if (opbtn != NULL) {
        *opbtn = opts1;
    }
    #ifdef DEBUG
    dprintf("Affine IO:opbtn:%x\n",*opbtn);
    #endif
}

void mai2_io_get_gamebtns(uint16_t *player1, uint16_t *player2){
    if (player1 != NULL) {
        *player1 = p1;
    }

    if (player2 != NULL) {
        *player2 = p2;
    }
    #ifdef DEBUG
    dprintf("Affine IO:player1:%x,player2:%x\n",*player1,*player2);
    #endif
}

HRESULT mai2_io_touch_init(mai2_io_touch_callback_t callback){
    _callback = callback;
    return S_OK;
}

void mai2_io_touch_set_sens(uint8_t *bytes){

}

void mai2_io_touch_update(bool player1, bool player2) {
    if(!thread_flag){
        thread_flag = 1;
        if (mai2_io_cfg.debug_input_1p) {
            #ifdef DEBUG
            dprintf("Affine IO:enable 1p thread\n");
            #endif
            mai2_io_touch_1p_thread = (HANDLE)_beginthreadex(NULL, 0, mai2_io_touch_1p_thread_proc, _callback, 0, NULL);
        }
        if (mai2_io_cfg.debug_input_2p) {
            #ifdef DEBUG
            dprintf("Affine IO:enable 2p thread\n");
            #endif
            mai2_io_touch_2p_thread = (HANDLE)_beginthreadex(NULL, 0, mai2_io_touch_2p_thread_proc, _callback, 0, NULL);
        }
    }
}

static unsigned int __stdcall mai2_io_touch_1p_thread_proc(void *ctx){
    #ifdef DEBUG
    dprintf("Affine IO:1p thread start\n");
    #endif
    mai2_io_touch_callback_t callback = ctx;
    uint8_t state[7] = {0, 0, 0, 0, 0, 0, 0};
    package_init(&response1);	
    char comPort[13];

    memcpy(comPort,GetSerialPortByVidPid(Vid,Pid_1p),6);

    if(comPort[0] == 0){
        int port_num = 11;
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }else if(comPort[4] == 0){
    }else if(comPort[5] == 0){
        int port_num = (comPort[3]-48)*10 + (comPort[4]-48);
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }else{
        int port_num = (comPort[3]-48)*100 + (comPort[4]-48)*10 + (comPort[5]-48);
        snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
        
    }
    #ifdef DEBUG
    dprintf("Affine IO:1P comPort:");
    dprintf(comPort);
    dprintf("\n");
    #endif
    open_port(&hPort1,comPort);
    while (!mai2_io_touch_1p_stop_flag) {
        switch (serial_read_cmd(hPort1,&response1)) {
		    case SERIAL_CMD_AUTO_SCAN:
                // #ifdef DEBUG
                // dprintf("Affine IO:Auto Scan:%02x\n",response1.io_status);
                // #endif
			    memcpy(state, response1.touch, 7);
                p1 = response1.key_status;
                opts1 = response1.io_status; 
                package_init(&response1);
                callback(1,state);
			    break;
            case 0xff:{
                #ifdef DEBUG
                dprintf("Affine IO:1p port error\n");
                //dprintf("3\n");
                #endif
                memset(comPort,0,13);
                while(hPort1 == NULL || hPort1 == INVALID_HANDLE_VALUE){
                    CloseHandle(hPort1);
                    strncpy(comPort,GetSerialPortByVidPid(Vid,Pid_1p),6);
                    if(comPort[0] == 0){
                        int port_num = 11;
                        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
                    }else if(comPort[4] == 0){
                    }else if(comPort[5] == 0){
                        int port_num = (comPort[3]-48)*10 + (comPort[4]-48);
                        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
                    }else{
                        int port_num = (comPort[3]-48)*100 + (comPort[4]-48)*10 + (comPort[5]-48);
                        snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
                        
                    }
                    #ifdef DEBUG
                    dprintf("Affine IO:try 1P comPort:");
                    dprintf(comPort);
                    dprintf("\n");
                    #endif
                    open_port(&hPort1,comPort);
                    Sleep(1000);
                }
                #ifdef DEBUG
                dprintf("Affine IO:1P comPort reconnected\n");
                #endif
                break;
            }
            default:
                break;
        }
    }
    CloseHandle(hPort1);
}

static unsigned int __stdcall mai2_io_touch_2p_thread_proc(void *ctx){
    #ifdef DEBUG
    dprintf("Affine IO:2p thread start\n");
    #endif
    mai2_io_touch_callback_t callback = ctx;
    //serial_heart_beat(&hPort2,&response2);
    uint8_t state[7] = {0, 0, 0, 0, 0, 0, 0};
    package_init(&response2);	
    char comPort[13];
    memcpy(comPort,GetSerialPortByVidPid(Vid,Pid_2p),6);
    if(comPort[0] == 0){
        int port_num = 11;
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }else if(comPort[4] == 0){
    }else if(comPort[5] == 0){
        int port_num = (comPort[3]-48)*10 + (comPort[4]-48);
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }else{
        int port_num = (comPort[3]-48)*100 + (comPort[4]-48)*10 + (comPort[5]-48);
        snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
        
    }
    #ifdef DEBUG
    dprintf("Affine IO:2P comPort:");
    dprintf(comPort);
    dprintf("\n");
    #endif
    open_port(&hPort2,comPort);
    while (!mai2_io_touch_2p_stop_flag) {
        switch (serial_read_cmd(hPort2,&response2)) {
		    case SERIAL_CMD_AUTO_SCAN:
			    memcpy(state, response2.touch, 7);
                p2 = response2.key_status;
                opts2 = response2.io_status;
                package_init(&response2);
                callback(2,state);
			    break;
                case 0xff:{
                    #ifdef DEBUG
                    dprintf("Affine IO:2p port error\n");
                    //dprintf("3\n");
                    #endif
                    memset(comPort,0,13);
                    while(hPort2 == NULL || hPort2 == INVALID_HANDLE_VALUE){
                        CloseHandle(hPort2);
                        strncpy(comPort,GetSerialPortByVidPid(Vid,Pid_1p),6);
                        if(comPort[0] == 0){
                            int port_num = 11;
                            snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
                        }else if(comPort[4] == 0){
                        }else if(comPort[5] == 0){
                            int port_num = (comPort[3]-48)*10 + (comPort[4]-48);
                            snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
                        }else{
                            int port_num = (comPort[3]-48)*100 + (comPort[4]-48)*10 + (comPort[5]-48);
                            snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
                            
                        }
                        #ifdef DEBUG
                        dprintf("Affine IO:try 2P comPort:");
                        dprintf(comPort);
                        dprintf("\n");
                        #endif
                        open_port(&hPort2,comPort);
                        Sleep(1000);
                    }
                    #ifdef DEBUG
                    dprintf("Affine IO:2P comPort reconnected\n");
                    #endif
                    break;
                }
            default:
                break;
        }
    }
    CloseHandle(hPort2);
}

HRESULT mai2_io_led_init(void){
    return S_OK;
}

void mai2_io_led_set_fet_output(uint8_t board, const uint8_t *rgb) {

}

void mai2_io_led_dc_update(uint8_t board, const uint8_t *rgb) {
    return;
}

void mai2_io_led_gs_update(uint8_t board, const uint8_t *rgb) {

}