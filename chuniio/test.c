#include <windows.h>
#include <stdio.h>
#include "serialslider.h"

extern char comPort[13];
char *vid = "VID_AFF1";
char *pid = "PID_52A4";

#define WIDTH 16
#define HEIGHT 2
#define THRESHOLD 128

uint8_t airStatus = 0; // Air status variable

const char *VERSION = "v0.2";

typedef enum
{
    DEVICE_WAIT,
    DEVICE_FAIL,
    DEVICE_OK
} DeviceState;

void DisplayHeader(HANDLE hConsole, DeviceState state)
{
    COORD pos = {0, 0};
    SetConsoleCursorPosition(hConsole, pos);

    // 修改程序标题
    printf("Linnea Test Tools %s\n", VERSION);

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

void DisplaySectionTitle(HANDLE hConsole, const char *title, int row, int column, int width)
{
    COORD pos = {column, row};
    SetConsoleCursorPosition(hConsole, pos);

    int titleLen = strlen(title);
    int padding = (width - titleLen - 2) / 2;

    for (int i = 0; i < padding; i++)
        printf(" ");
    printf("[%s]", title);
}

void DisplayGroundSlider(HANDLE hConsole, int data[HEIGHT][WIDTH])
{
    COORD startPos = {0, 3};
    
    SetConsoleCursorPosition(hConsole, startPos);
    printf("┌─────────────────────────────────────────── Touch Values ──────────────────────────────────────┐");
    
    SetConsoleCursorPosition(hConsole, (COORD){startPos.X, startPos.Y + 1});
    printf("│");
    for (int x = 0; x < WIDTH; x++)
    {
        printf("     │");
    }
    
    SetConsoleCursorPosition(hConsole, (COORD){startPos.X, startPos.Y + 2});
    printf("│");
    for (int x = 0; x < WIDTH; x++)
    {
        if (data[0][x] > THRESHOLD)
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        
        printf(" %3d ", data[0][x]);  
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
        printf("│");
    }
    
    SetConsoleCursorPosition(hConsole, (COORD){startPos.X, startPos.Y + 3});
    printf("│");
    for (int x = 0; x < WIDTH; x++)
    {
        printf("     │");
    }
    
    SetConsoleCursorPosition(hConsole, (COORD){startPos.X, startPos.Y + 4});
    printf("├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤");
    
    SetConsoleCursorPosition(hConsole, (COORD){startPos.X, startPos.Y + 5});
    printf("│");
    for (int x = 0; x < WIDTH; x++)
    {
        printf("     │");
    }
    
    SetConsoleCursorPosition(hConsole, (COORD){startPos.X, startPos.Y + 6});
    printf("│");
    for (int x = 0; x < WIDTH; x++)
    {
        if (data[1][x] > THRESHOLD)
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
        
        printf(" %3d ", data[1][x]);  
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
        printf("│");
    }
    
    SetConsoleCursorPosition(hConsole, (COORD){startPos.X, startPos.Y + 7});
    printf("│");
    for (int x = 0; x < WIDTH; x++)
    {
        printf("     │");
    }
    
    SetConsoleCursorPosition(hConsole, (COORD){startPos.X, startPos.Y + 8});
    printf("└───────────────────────────────────────────────────────────────────────────────────────────────┘");
}

void DisplayAirStatus(HANDLE hConsole, uint8_t status)
{
    COORD startPos = {WIDTH * 5 + 17, 3}; 
    
    SetConsoleCursorPosition(hConsole, startPos);
    printf("  ┌──── IR States ────┐");
    
    for (int i = 0; i < 6; i++)
    {
        int sensorNum = 6 - i;
        SetConsoleCursorPosition(hConsole, (COORD){startPos.X + 2, startPos.Y + i + 1});
        
        printf("│   IR_%d     ", sensorNum);
        
        bool isActive = (status & (1 << (sensorNum - 1))) != 0;
        if (isActive) {
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
            printf("ON ");
        } else {
            printf("OFF");
        }
        
        SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
        printf("    │");
    }
    
    SetConsoleCursorPosition(hConsole, (COORD){startPos.X + 2, startPos.Y + 7});
    printf("│ WHITE LED WHEN ON │");
    
    SetConsoleCursorPosition(hConsole, (COORD){startPos.X + 2, startPos.Y + 8});
    printf("└───────────────────┘");
}

int main()
{
    // Set console to UTF-8 mode
    SetConsoleOutputCP(65001);

    slider_packet_t reponse;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DeviceState deviceState = DEVICE_WAIT;
    BOOL isFirstConnection = TRUE; // Tag

    if (hConsole == INVALID_HANDLE_VALUE)
    {
        printf("Error getting console handle\n");
        return 1;
    }

    COORD bufferSize = {100, 30};
    SMALL_RECT windowSize = {0, 0, 99, 29};
    SetConsoleScreenBufferSize(hConsole, bufferSize);
    SetConsoleWindowInfo(hConsole, TRUE, &windowSize);
    
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // Initialize display
    DisplayHeader(hConsole, deviceState);

    int consoleWidth = WIDTH * 6 + 22;

    memcpy(comPort, GetSerialPortByVidPid(vid, pid), 6);

    if (comPort[0] == 0)
    {
        char *default_comPort = "COM1";
        memcpy(comPort, default_comPort, 5);
    }
    else if (comPort[4] == 0)
    {
        int port_num = (comPort[3] - '0');
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }
    else if (comPort[5] == 0)
    {
        int port_num = (comPort[3] - '0') * 10 + (comPort[4] - '0');
        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
    }
    else
    {
        int port_num = (comPort[3] - '0') * 100 + (comPort[4] - '0') * 10 + (comPort[5] - '0');
        snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
    }

    if (!open_port())
    {
        deviceState = DEVICE_FAIL;
        DisplayHeader(hConsole, deviceState);
    }
    else
    {
        deviceState = DEVICE_OK;
        DisplayHeader(hConsole, deviceState);
        slider_start_air_scan();
        slider_start_scan();
        isFirstConnection = FALSE; // Set to FALSE after the first connection
    }

    int data[HEIGHT][WIDTH] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
    
    uint8_t rgb[93] = {
        255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,
        255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,255,255,0,
    };
    
    // 初始绘制UI
    DisplayGroundSlider(hConsole, data);
    DisplayAirStatus(hConsole, airStatus);
    
    slider_send_leds(rgb);
    package_init(&reponse);
    
    while (1)
    {
        for (uint8_t i = 0; i < 100; i++)
        {
            switch (serial_read_cmd(&reponse))
            {
            case SLIDER_CMD_AUTO_SCAN:
                for (uint8_t i = 0; i < 16; i++)
                {
                    data[0][15 - i] = reponse.pressure[2 * i];
                    data[1][15 - i] = reponse.pressure[2 * i + 1];
                }
                if (reponse.size == 33)
                {
                    airStatus = reponse.air_status;
                }
                package_init(&reponse);
                DisplayGroundSlider(hConsole, data);
                DisplayAirStatus(hConsole, airStatus); 
                break;
                
            case SLIDER_CMD_AUTO_AIR:
                airStatus = reponse._air_status; 
                package_init(&reponse);
                DisplayAirStatus(hConsole, airStatus); 
                break;

            case 0xff:
                memset(data, 0, sizeof(data));
                airStatus = 0; 
                deviceState = DEVICE_FAIL;
                DisplayHeader(hConsole, deviceState);
                DisplayGroundSlider(hConsole, data);
                DisplayAirStatus(hConsole, airStatus); 

                close_port();
                COORD statusPos = {0, HEIGHT + 12};
                SetConsoleCursorPosition(hConsole, statusPos);

                // Only show the connection failed message if it's not the first connection
                if (!isFirstConnection)
                {
                    printf("Connect failed! Trying to reconnect...                     ");
                }

                deviceState = DEVICE_WAIT;
                DisplayHeader(hConsole, deviceState);

                while (!open_port())
                {
                    close_port();
                    memcpy(comPort, GetSerialPortByVidPid(vid, pid), 6);
                    
                    if (comPort[0] == 0)
                    {
                        char *default_comPort = "COM1";
                        memcpy(comPort, default_comPort, 5);
                    }
                    else if (comPort[4] == 0)
                    {
                        int port_num = (comPort[3] - '0');
                        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
                    }
                    else if (comPort[5] == 0)
                    {
                        int port_num = (comPort[3] - '0') * 10 + (comPort[4] - '0');
                        snprintf(comPort, 10, "\\\\.\\COM%d", port_num);
                    }
                    else
                    {
                        int port_num = (comPort[3] - '0') * 100 + (comPort[4] - '0') * 10 + (comPort[5] - '0');
                        snprintf(comPort, 11, "\\\\.\\COM%d", port_num);
                    }
                    
                    Sleep(1);
                }

                deviceState = DEVICE_OK;
                DisplayHeader(hConsole, deviceState);

                if (!isFirstConnection)
                {
                    SetConsoleCursorPosition(hConsole, statusPos);
                    printf("Connection restored!                                     ");
                }
                else
                {
                    isFirstConnection = FALSE;
                }
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