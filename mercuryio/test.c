// Still in progress!!

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
extern char* vid; 
extern char* pid; 

bool cellPressed[240];
uint8_t cell_raw[30];
slider_packet_t reponse;

#define ARRAY_SIZE 240
#define COLUMNS 16
#define ROWS 15

const char *VERSION = "v0.0";

typedef enum
{
    DEVICE_WAIT,
    DEVICE_FAIL,
    DEVICE_OK
} DeviceState;

// 控制台光标定位函数
void setCursorPos(int x, int y) {
    COORD coord = { x, y };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

void DisplayHeader(HANDLE hConsole, DeviceState state)
{
    setCursorPos(0, 0);

    printf("Mercury Test Tools %s\n", VERSION);

    // State
    printf("Device State: ");
    switch (state)
    {
    case DEVICE_WAIT:
        printf("WAIT");
        break;
    case DEVICE_FAIL:
        printf("FAIL");
        break;
    case DEVICE_OK:
        printf("OK  ");
        break;
    }
    printf("\n\n");
}

void DisplayTouchGrid(HANDLE hConsole, bool cellPressed[240])
{
    setCursorPos(0, 3);
    printf("┌────────────────────────────── Touch Sensor Status ─────────────────────────────┐");
    
    for(int row = 0; row < ROWS; row++) {
        setCursorPos(0, row + 4);
        printf("│");
        
        for(int col = 0; col < COLUMNS; col++) {
            int index = row * COLUMNS + col;
            
            // 高亮显示激活的单元格
            if (cellPressed[index]) {
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                printf(" %03d ", index);
            } else {
                SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
                printf(" %03d ", index);
            }
            
            SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
        }
        printf("│");
    }
    
    setCursorPos(0, ROWS + 4);
    printf("└────────────────────────────────────────────────────────────────────────────────┘");
}

void DisplayStatusMessage(HANDLE hConsole, const char* message, bool isError)
{
    setCursorPos(0, ROWS + 6);
    if (isError) {
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    } else {
        SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    }
    printf("%-70s", message);
    SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
}

void main(){
    // 设置控制台为UTF-8模式
    SetConsoleOutputCP(65001);
    
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DeviceState deviceState = DEVICE_WAIT;
    BOOL isFirstConnection = TRUE;
    
    if (hConsole == INVALID_HANDLE_VALUE) {
        printf("Error getting console handle\n");
        return;
    }
    
    // 设置控制台大小
    COORD bufferSize = {100, 30};
    SMALL_RECT windowSize = {0, 0, 99, 29};
    SetConsoleScreenBufferSize(hConsole, bufferSize);
    SetConsoleWindowInfo(hConsole, TRUE, &windowSize);
    
    // 隐藏光标
    CONSOLE_CURSOR_INFO cursorInfo = {1, FALSE};
    SetConsoleCursorInfo(hConsole, &cursorInfo);
    
    // 初始化显示
    DisplayHeader(hConsole, deviceState);
    
    // 初始化串口连接
    memcpy(comPort, GetSerialPortByVidPid(vid, pid), 6);
    if(comPort[5] != 0){
        int port_num = (comPort[3]-'0')*100 + (comPort[4]-'0')*10 + (comPort[5]-'0');
        snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
    }else if(comPort[4] != 0){
        int port_num = (comPort[3]-'0')*10 + (comPort[4]-'0');
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }else if(comPort[0] != 0){
        int port_num = (comPort[3]-'0');
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }else{
        char* default_comPort = "COM1";
        memcpy(comPort, default_comPort, 5);
    }
    
    if (!open_port()) {
        deviceState = DEVICE_FAIL;
        DisplayHeader(hConsole, deviceState);
        DisplayStatusMessage(hConsole, "Failed to connect to device. Retrying...", TRUE);
    } else {
        deviceState = DEVICE_OK;
        DisplayHeader(hConsole, deviceState);
        slider_start_scan();
        isFirstConnection = FALSE;
        DisplayStatusMessage(hConsole, "Connected to device successfully!", FALSE);
    }
    
    // 初始化显示网格
    DisplayTouchGrid(hConsole, cellPressed);
    
    while(1){
        switch (serial_read_cmd(&reponse)) {
        case SLIDER_CMD_AUTO_SCAN:
            memcpy(cell_raw, reponse.cell, 30);
            package_init(&reponse);
            for(uint8_t i = 0; i < 30; i++){
                for(uint8_t y = 0; y < 8; y++){
                    if(cell_raw[i] & (1 << y)){
                        cellPressed[i*8+y] = true;
                    }else{
                        cellPressed[i*8+y] = false;
                    }
                }
            }
            DisplayTouchGrid(hConsole, cellPressed);
            break;
            
        case 0xff:
            deviceState = DEVICE_FAIL;
            DisplayHeader(hConsole, deviceState);
            DisplayStatusMessage(hConsole, "Connection lost! Attempting to reconnect...", TRUE);
            
            close_port();
            memset(cellPressed, 0, sizeof(bool) * 240);
            DisplayTouchGrid(hConsole, cellPressed);
            
            deviceState = DEVICE_WAIT;
            DisplayHeader(hConsole, deviceState);
            
            while(!open_port()){
                close_port();
                memcpy(comPort, GetSerialPortByVidPid(vid, pid), 6);
                if(comPort[5] != 0){
                    int port_num = (comPort[3]-'0')*100 + (comPort[4]-'0')*10 + (comPort[5]-'0');
                    snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
                }else if(comPort[4] != 0){
                    int port_num = (comPort[3]-'0')*10 + (comPort[4]-'0');
                    snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
                }else if(comPort[0] != 0){
                    int port_num = (comPort[3]-'0');
                    snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
                }else{
                    char* default_comPort = "COM1";
                    memcpy(comPort, default_comPort, 5);
                }
                Sleep(1);
            }
            
            deviceState = DEVICE_OK;
            DisplayHeader(hConsole, deviceState);
            slider_start_scan();
            DisplayStatusMessage(hConsole, "Connection restored successfully!", FALSE);
            break;
            
        default:
            break;
        }
        
        Sleep(5); // 控制刷新频率
    }
    
    // 恢复光标显示
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
    return;
}