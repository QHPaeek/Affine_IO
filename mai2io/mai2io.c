#include <windows.h>
#include <process.h>
#include <limits.h>
#include <stdint.h>
#include "config.h"
#include "mai2io.h"
#include "serial.h"
#include "dprintf.h"

#include <stdatomic.h>

#define ARRAY_LENGTH 34
#define DEFAULT_VALUE 128

#define SHM_NAME_1   TEXT("mai_io_shm_1")
#define SHM_NAME_2   TEXT("mai_io_shm_2")
#define ARRAY_SIZE 2

//#define DEBUG

extern char comPort1[13]; //串口号
extern char comPort2[13]; //串口号
extern HANDLE hPort1; // 串口句柄
extern HANDLE hPort2; // 串口句柄
volatile uint8_t opts2;
volatile uint16_t p1 = 0;
volatile uint16_t p2 = 0;
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

static uint8_t mai2_opbtn;
static bool mai2_io_coin;

static HANDLE h_exMapFile1;
static HANDLE h_exMapFile2;
static uint8_t* mai_io_btn_1;
static uint8_t* mai_io_btn_2;

uint16_t mai2_io_get_api_version(void)
{
    return 0x0101;
}

HRESULT mai2_io_init(void)
{
    dprintf("Affine IO:mai2_io_init\n");
    mai2_io_config_load(&mai2_io_cfg, L".\\segatools.ini");
    //read_json_to_threshold("curva_config.json", touch_threshold);
    return S_OK;
}

HRESULT mai2_io_poll(void)
{  
    mai2_opbtn = 0;
    
    if (h_exMapFile1 == NULL || mai_io_btn_1 == NULL) {
        if (mai_io_btn_1 != NULL) {
            UnmapViewOfFile(mai_io_btn_1);
            mai_io_btn_1 = NULL;
        }
        if (h_exMapFile1 != NULL) {
            CloseHandle(h_exMapFile1);
            h_exMapFile1 = NULL;
        }
        
        h_exMapFile1 = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME_1);
        if (h_exMapFile1 != NULL) {
            mai_io_btn_1 = (uint8_t*)MapViewOfFile(h_exMapFile1, FILE_MAP_ALL_ACCESS, 0, 0, ARRAY_SIZE);
        }
    }
    
    if (h_exMapFile2 == NULL || mai_io_btn_2 == NULL) {
        if (mai_io_btn_2 != NULL) {
            UnmapViewOfFile(mai_io_btn_2);
            mai_io_btn_2 = NULL;
        }
        if (h_exMapFile2 != NULL) {
            CloseHandle(h_exMapFile2);
            h_exMapFile2 = NULL;
        }
        
        h_exMapFile2 = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, SHM_NAME_2);
        if (h_exMapFile2 != NULL) {
            mai_io_btn_2 = (uint8_t*)MapViewOfFile(h_exMapFile2, FILE_MAP_ALL_ACCESS, 0, 0, ARRAY_SIZE);
        }
    }

    if(mai_io_btn_1 != NULL){
        p1 =  mai_io_btn_1[0];
        p1 |=  ((mai_io_btn_1[1] & 0b10000) << 4);
        mai2_opbtn |=  (mai_io_btn_1[1] & 0b111);
    }
    if(mai_io_btn_2 != NULL){
        p2 =  mai_io_btn_2[0];
        p2 |=  ((mai_io_btn_2[1] & 0b100000) << 3);
        mai2_opbtn |=  (mai_io_btn_2[1] & 0b111);
    }
    return S_OK;
}

void mai2_io_get_opbtns(uint8_t *opbtn){
    if (opbtn != NULL) {
        *opbtn = mai2_opbtn;
    }
}

void mai2_io_get_gamebtns(uint16_t *player1, uint16_t *player2){
    if (player1 != NULL) {
        *player1 = p1;
    }

    if (player2 != NULL) {
        *player2 = p2;
    }
    // #ifdef DEBUG
    // dprintf("Affine IO:player1:%x,player2:%x\n",*player1,*player2);
    // #endif
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
        
        mai2_io_touch_1p_stop_flag = false;
        mai2_io_touch_2p_stop_flag = false;
        
        if (mai2_io_cfg.debug_input_1p) {
            dprintf("Affine IO:enable 1p thread\n");
            mai2_io_touch_1p_thread = (HANDLE)_beginthreadex(NULL, 0, mai2_io_touch_1p_thread_proc, _callback, 0, NULL);
        }
        if (mai2_io_cfg.debug_input_2p) {
            dprintf("Affine IO:enable 2p thread\n");
            mai2_io_touch_2p_thread = (HANDLE)_beginthreadex(NULL, 0, mai2_io_touch_2p_thread_proc, _callback, 0, NULL);
        }
    }
}

static unsigned int __stdcall mai2_io_touch_1p_thread_proc(void *ctx){
    dprintf("Affine IO:1p thread start\n");
    mai2_io_touch_callback_t callback = ctx;
    uint8_t state[7] = {0, 0, 0, 0, 0, 0, 0};
    package_init(&response1);	
    char comPort[13];
    uint8_t* mai_io_btn; 

    HANDLE hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,ARRAY_SIZE,SHM_NAME_1);
    if (hMapFile != NULL) {
        mai_io_btn = (uint8_t*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, ARRAY_SIZE);
    }

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
    dprintf("Affine IO:1P comPort:");
    dprintf(comPort);
    dprintf("\n");

    open_port(&hPort1,comPort);
    while (!mai2_io_touch_1p_stop_flag) {
        package_init(&response1);
        uint8_t cmd = serial_read_cmd(hPort1,&response1);
        switch (cmd) {
		    case SERIAL_CMD_AUTO_SCAN:{
			    memcpy(state, response1.touch, 7);
                if (mai_io_btn != NULL) {
                    mai_io_btn[0] = response1.key_status[0] | response1.key_status[1];
                    mai_io_btn[1] = response1.io_status;
                }
                #ifdef DEBUG
                dprintf("Affine IO:Auto Scan:%x %x\n",mai_io_btn[0],mai_io_btn[1]);
                #endif
                callback(1,state);
			    break;
            }
            case 0xff:{
                dprintf("Affine IO:1p port error\n");
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
                
                dprintf("Affine IO:1P comPort reconnected\n");
                
                break;
            }
            default:
                #ifdef DEBUG
                dprintf("Affine IO:other command:%x\n",cmd);
                #endif
                break;
        }
        serial_heart_beat(hPort1,&request1);
    }
    CloseHandle(hPort1);
    if (mai_io_btn != NULL) {
        mai_io_btn[0] = 0;
        mai_io_btn[1] = 0;
    }
    UnmapViewOfFile(mai_io_btn);
    CloseHandle(hMapFile);
}

static unsigned int __stdcall mai2_io_touch_2p_thread_proc(void *ctx){
    dprintf("Affine IO:2p thread start\n");
    mai2_io_touch_callback_t callback = ctx;
    uint8_t state[7] = {0, 0, 0, 0, 0, 0, 0};
    package_init(&response2);	
    char comPort[13];
    uint8_t* mai_io_btn; 

    HANDLE hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,ARRAY_SIZE,SHM_NAME_2);
    if (hMapFile != NULL) {
        mai_io_btn = (uint8_t*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, ARRAY_SIZE);
    }
    memcpy(comPort,GetSerialPortByVidPid(Vid,Pid_2p),6);
    if(comPort[0] == 0){
        int port_num = 12;
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }else if(comPort[4] == 0){
    }else if(comPort[5] == 0){
        int port_num = (comPort[3]-48)*10 + (comPort[4]-48);
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }else{
        int port_num = (comPort[3]-48)*100 + (comPort[4]-48)*10 + (comPort[5]-48);
        snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
        
    }
    dprintf("Affine IO:2P comPort:");
    dprintf(comPort);
    dprintf("\n");

    open_port(&hPort2,comPort);
    while (!mai2_io_touch_2p_stop_flag) {
        switch (serial_read_cmd(hPort2,&response2)) {
		    case SERIAL_CMD_AUTO_SCAN:
			    memcpy(state, response2.touch, 7);
                if (mai_io_btn != NULL) {
                    mai_io_btn[0] = response2.key_status[0] & response2.key_status[1];
                    mai_io_btn[1] = response2.io_status;
                }
                package_init(&response2);
                callback(2,state);
			    break;
                case 0xff:{
                    dprintf("Affine IO:2p port error\n");
                    //dprintf("3\n");
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
                    dprintf("Affine IO:2P comPort reconnected\n");
                    break;
                }
            default:
                break;
        }
        serial_heart_beat(hPort2,&request2);
    }
    CloseHandle(hPort2);
    if (mai_io_btn != NULL) {
        mai_io_btn[0] = 0;
        mai_io_btn[1] = 0;
    }
    UnmapViewOfFile(mai_io_btn);
    CloseHandle(hMapFile);
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