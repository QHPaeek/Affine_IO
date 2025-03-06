#include <windows.h>

#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <process.h>

#include "mercuryio.h"
#include "config.h"

#include "serialslider.h"
extern char comPort[13];
char* vid = "VID_AFF1";
char* pid = "PID_52A5";

bool cellPressed[240];
uint8_t cell_raw[30];
slider_packet_t reponse;

#define ARRAY_SIZE 240
#define COLUMNS 10

// 控制台光标定位函数
void setCursorPos(int x, int y) {
    COORD coord = { x, y };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

void main(){

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
    printf("Connect Start!");



    //控制台初始化
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo = {1, FALSE};
    SetConsoleCursorInfo(hConsole, &cursorInfo); // 隐藏光标

    // 绘制静态表格框架
    system("cls");
    printf("Index | Status | Index | Status | Index | Status | Index | Status | Index | Status\n");
    printf("------+--------+-------+--------+-------+--------+-------+--------+-------+\n");
    
    //动态数据区域初始化
    for(int row=0; row<ARRAY_SIZE/COLUMNS; row++) {
        for(int col=0; col<COLUMNS; col++) {
            int index = row*COLUMNS + col;
            setCursorPos(col*12 + 2, row + 3);
            printf("%03d: %s", index, cellPressed[index] ? "TRUE " : "FALSE");
        }
    }
    while(1){
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
            break;
        case 0xff:
            printf("Connect failed!");
            close_port();
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

            break;
        default:
            // for(uint8_t i=0;i<240;i++){
            //     cellPressed[i] = true;
            // }
            break;
        }
        //更新变化的状态显示
        for(int row=0; row<ARRAY_SIZE/COLUMNS; row++) {
            for(int col=0; col<COLUMNS; col++) {
                int index = row*COLUMNS + col;
                setCursorPos(col*12 + 7, row + 3);
                printf("%s", cellPressed[index] ? "TRUE " : "FALSE");
            }
        }
        
        Sleep(5); // 控制刷新频率
    }
    //恢复光标显示
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
    return;
}