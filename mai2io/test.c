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

uint16_t player1,player2;
HANDLE hPort; // 串口句柄
extern HANDLE hPort1; // 串口句柄
extern HANDLE hPort2; // 串口句柄

void main(){
    mai2_io_init();

        uint8_t c;

        //hPort = CreateFile("\\\\.\\COM11", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        // open_port(&hPort,"\\\\.\\COM11");
        while(1){
        // if (hPort == INVALID_HANDLE_VALUE){
        //     printf("open failed\n");
        //         //If not success full display an Error
        //     DWORD dwError = GetLastError();
        //     switch (dwError) {
        //         case ERROR_FILE_NOT_FOUND:
        //             printf("ERROR: Handle was not attached. Reason: COM12 not available.\n");
        //             break;
        //         case ERROR_ACCESS_DENIED:
        //             printf("ERROR: Access denied. Another program might be using the port.\n");
        //             break;
        //         case ERROR_GEN_FAILURE:
        //             printf("ERROR: General failure. There might be a hardware issue.\n");
        //             break;
        //         default:
        //             printf("ERROR: Unknown error occurred. Error code: %lu\n", dwError);
        //             break;
        //     }
        // }else{
        //     serial_read1(&hPort,&c);
        //     printf("%x",c);
        // }
        mai2_io_poll();
        mai2_io_get_gamebtns(&player1, &player2);
        printf("%d\n",player1);
        Sleep(1);
    }
}