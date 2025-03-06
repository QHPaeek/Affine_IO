#include <windows.h>
#include <stdio.h>
#include "serialslider.h"

extern char comPort[6];
char* vid = "VID_AFF1";
char* pid = "PID_52A4";

#define WIDTH 16
#define HEIGHT 2
#define THRESHOLD 128

void DisplayTable(HANDLE hConsole, int data[HEIGHT][WIDTH]) {
    CHAR_INFO buffer[HEIGHT + 2][WIDTH * 6 + 1]; 
    COORD bufferSize = { WIDTH * 6 + 1, HEIGHT + 2 };
    COORD bufferCoord = { 0, 0 };
    SMALL_RECT writeRegion = { 0, 0, WIDTH * 6, HEIGHT + 1 };    

    for (int y = 0; y < HEIGHT + 2; y++) {
        for (int x = 0; x < WIDTH * 6 + 1; x++) {
            buffer[y][x].Char.UnicodeChar = ' ';
            buffer[y][x].Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
        }
    }

    for (int x = 0; x < WIDTH * 6 + 1; x++) {
        buffer[0][x].Char.UnicodeChar = L'-';
        buffer[HEIGHT + 1][x].Char.UnicodeChar = L'-';
    }
    for (int y = 0; y < HEIGHT + 2; y++) {
        buffer[y][0].Char.UnicodeChar = L'|';
        buffer[y][WIDTH * 6].Char.UnicodeChar = L'|';
    }

    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            int indexX = x * 6 + 1;
            int indexY = y + 1;
            char cell[6];
            snprintf(cell, sizeof(cell), "%3d |", data[y][x]);
            for (int i = 0; i < 5; i++) {
                buffer[indexY][indexX + i].Char.UnicodeChar = cell[i];
                if (data[y][x] > THRESHOLD) {
                    buffer[indexY][indexX + i].Attributes = FOREGROUND_RED | FOREGROUND_INTENSITY; 
                } else {
                    buffer[indexY][indexX + i].Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
                }
            }
        }
    }

    WriteConsoleOutput(hConsole, (CHAR_INFO*)buffer, bufferSize, bufferCoord, &writeRegion);
}

int main() {
    slider_packet_t reponse;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) {
        printf("Error getting console handle\n");
        return 1;
    }
    memcpy(comPort,GetSerialPortByVidPid(vid,pid),6);

    if(*comPort == 0x48){
        char* default_comPort = "COM1";
        memcpy(comPort,default_comPort,5);
    }
    open_port();
    slider_start_air_scan();
    slider_start_scan();

    int data[HEIGHT][WIDTH] = {
        { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0},
        { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}
    };
    uint8_t rgb[93] = {
        255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,
        255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,
    };
    slider_send_leds(rgb);
    package_init(&reponse);	
    while (1) {
        for(uint8_t i=0;i<100;i++){
            switch (serial_read_cmd(&reponse)) {
                case SLIDER_CMD_AUTO_SCAN:
                    for(uint8_t i=0;i<16;i++){
                        data[0][15-i] = reponse.pressure[2*i];
                        data[1][15-i] = reponse.pressure[2*i+1];
                    }
                    package_init(&reponse);
                    DisplayTable(hConsole, data);
                    break;
                case SLIDER_CMD_AUTO_AIR:
                    package_init(&reponse);
                    break;
                case 0xff:
                    memset(data,0, 32);
                    DisplayTable(hConsole, data);
                    close_port();
                    SetConsoleCursorPosition(hConsole, (COORD){0, HEIGHT + 3});
                    printf("Connect failed!");
                    while(!open_port()){
                        close_port();
                        memcpy(comPort,GetSerialPortByVidPid(vid,pid),6);
                        if(*comPort == 0x48){
                            char* default_comPort = "COM1";
                            memcpy(comPort,default_comPort,5);
                        }
                        //memset(pressure,0, 32);
                        Sleep(1);
                    }
                    SetConsoleCursorPosition(hConsole, (COORD){0, HEIGHT + 3});
                    printf("Connect success!");
                    Sleep(1);
                    slider_start_air_scan();
                    slider_start_scan();
                    break;
                default:
                    break;
            }
        }
        slider_send_leds(rgb);
    }
}
