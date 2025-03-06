#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <winioctl.h>
#include <conio.h>
#include <stdbool.h>
#include <ctype.h>
#include "mai2io.h"
#include "serial.h"
#define DEBUG

static char* Vid = "VID_AFF1";
static char* Pid_1p = "PID_52A5";
uint16_t player1,player2;
HANDLE hPort; // 串口句柄
extern HANDLE hPort1; // 串口句柄
extern HANDLE hPort2; // 串口句柄
extern char comPort1[13];

void main(){

    uint8_t c;

    //hPort = CreateFile("\\\\.\\COM11", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    // open_port(&hPort,"\\\\.\\COM11");
    while(hPort1 == NULL || hPort1 == INVALID_HANDLE_VALUE){
        close_port(&hPort1);
        strncpy(comPort1,GetSerialPortByVidPid(Vid,Pid_1p),6);
        if(comPort1[0] == 0){
            int port_num = 11;
            snprintf(comPort1, 10, "\\\\.\\COM%d", port_num);
            #ifdef DEBUG
            printf("Affine IO:1P comPort1:");
            printf(comPort1);
            printf("\n");
            #endif
        }else if(comPort1[4] == 0){

            #ifdef DEBUG
        printf("Affine IO:1P comPort1:");
        printf(comPort1);
        printf("\n");
        #endif
        }else if(comPort1[5] == 0){
            int port_num = (comPort1[3]-48)*10 + (comPort1[4]-48);
            snprintf(comPort1, 10, "\\\\.\\COM%d", port_num);
            #ifdef DEBUG
        printf("Affine IO:1P comPort1:");
        printf(comPort1);
        printf("\n");
        #endif
        }else{
            int port_num = (comPort1[3]-48)*100 + (comPort1[4]-48)*10 + (comPort1[5]-48);
            snprintf(comPort1, 11, "\\\\.\\COM%d", port_num);
            #ifdef DEBUG
        printf("Affine IO:1P comPort1:");
        printf(comPort1);
        printf("\n");
        #endif
        }
        open_port(&hPort1,comPort1);
        Sleep(1000);
    }
    Sleep(10000);
}