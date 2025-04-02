#include <windows.h>
#include <stdio.h>
#include "serialslider.h"

extern char comPort[6];
char *vid = "VID_AFF1";
char *pid = "PID_52A4";

#define WIDTH 16
#define HEIGHT 2
#define THRESHOLD 128

uint8_t airStatus = 0; // Air status variable

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

    // Title
    printf("Linnea Test Tools v0.1\n");

    // State
    printf("Device State: ");
    switch (state)
    {
    case DEVICE_WAIT:
        printf("Wait");
        break;
    case DEVICE_FAIL:
        printf("Fail");
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
    CHAR_INFO buffer[HEIGHT + 2][WIDTH * 6 + 1];
    COORD bufferSize = {WIDTH * 6 + 1, HEIGHT + 2};
    COORD bufferCoord = {0, 0};
    SMALL_RECT writeRegion = {0, 4, WIDTH * 6, HEIGHT + 5};

    for (int y = 0; y < HEIGHT + 2; y++)
    {
        for (int x = 0; x < WIDTH * 6 + 1; x++)
        {
            buffer[y][x].Char.UnicodeChar = ' ';
            buffer[y][x].Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
        }
    }

    for (int x = 0; x < WIDTH * 6 + 1; x++)
    {
        buffer[0][x].Char.UnicodeChar = L'-';
        buffer[HEIGHT + 1][x].Char.UnicodeChar = L'-';
    }
    for (int y = 0; y < HEIGHT + 2; y++)
    {
        buffer[y][0].Char.UnicodeChar = L'|';
        buffer[y][WIDTH * 6].Char.UnicodeChar = L'|';
    }

    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            int indexX = x * 6 + 1;
            int indexY = y + 1;
            char cell[6];
            snprintf(cell, sizeof(cell), "%3d |", data[y][x]);
            for (int i = 0; i < 5; i++)
            {
                buffer[indexY][indexX + i].Char.UnicodeChar = cell[i];
                if (data[y][x] > THRESHOLD)
                {
                    buffer[indexY][indexX + i].Attributes = FOREGROUND_RED | FOREGROUND_INTENSITY;
                }
                else
                {
                    buffer[indexY][indexX + i].Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
                }
            }
        }
    }

    WriteConsoleOutput(hConsole, (CHAR_INFO *)buffer, bufferSize, bufferCoord, &writeRegion);
}

void DisplayAirStatus(HANDLE hConsole, uint8_t status)
{
    CHAR_INFO buffer[8][20]; // 6 for sensors + 2 for border
    COORD bufferSize = {20, 8};
    COORD bufferCoord = {0, 0};

    SMALL_RECT writeRegion = {WIDTH * 6 + 2, 4, WIDTH * 6 + 21, 11};

    // Initialize display area
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 20; x++)
        {
            buffer[y][x].Char.UnicodeChar = ' ';
            buffer[y][x].Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
        }
    }

    // Draw borders
    for (int x = 0; x < 20; x++)
    {
        buffer[0][x].Char.UnicodeChar = L'-';
        buffer[7][x].Char.UnicodeChar = L'-';
    }
    for (int y = 0; y < 8; y++)
    {
        buffer[y][0].Char.UnicodeChar = L'|';
        buffer[y][19].Char.UnicodeChar = L'|';
    }

    // Display every air sensor status
    for (int i = 0; i < 6; i++)
    {
        int sensorNum = 6 - i; // 6,5,4,3,2,1
        int y = i + 1;         // row

        // Label
        char label[5];
        snprintf(label, sizeof(label), "IR_%d", sensorNum);
        for (int j = 0; j < strlen(label); j++)
        {
            buffer[y][j + 2].Char.UnicodeChar = label[j];
        }

        // Status
        bool isActive = (status & (1 << (sensorNum - 1))) != 0;
        const char *stateText = isActive ? "ON " : "OFF";
        for (int j = 0; j < 3; j++)
        {
            buffer[y][j + 12].Char.UnicodeChar = stateText[j];
            buffer[y][j + 12].Attributes = isActive ? (FOREGROUND_RED | FOREGROUND_INTENSITY) : (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
        }
    }

    WriteConsoleOutput(hConsole, (CHAR_INFO *)buffer, bufferSize, bufferCoord, &writeRegion);
}

int main()
{
    slider_packet_t reponse;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DeviceState deviceState = DEVICE_WAIT;
    BOOL isFirstConnection = TRUE; // Tag

    if (hConsole == INVALID_HANDLE_VALUE)
    {
        printf("Error getting console handle\n");
        return 1;
    }

    // Initialize display
    DisplayHeader(hConsole, deviceState);

    int consoleWidth = WIDTH * 6 + 22;
    DisplaySectionTitle(hConsole, "Touch Values", 3, 0, consoleWidth - 20);

    DisplaySectionTitle(hConsole, "IR States", 3, WIDTH * 6 + 2, 20);

    memcpy(comPort, GetSerialPortByVidPid(vid, pid), 6);

    if (*comPort == 0x48)
    {
        char *default_comPort = "COM1";
        memcpy(comPort, default_comPort, 5);
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
                DisplaySectionTitle(hConsole, "Touch Values", 3, 0, consoleWidth - 20);
                DisplaySectionTitle(hConsole, "IR States", 3, WIDTH * 6 + 2, 20);
                DisplayGroundSlider(hConsole, data);
                DisplayAirStatus(hConsole, airStatus); 

                close_port();
                COORD statusPos = {0, HEIGHT + 7};
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
                    if (*comPort == 0x48)
                    {
                        char *default_comPort = "COM1";
                        memcpy(comPort, default_comPort, 5);
                    }
                    Sleep(1);
                }

                deviceState = DEVICE_OK;
                DisplayHeader(hConsole, deviceState);

                DisplaySectionTitle(hConsole, "Touch Values", 3, 0, consoleWidth - 20);
                DisplaySectionTitle(hConsole, "IR States", 3, WIDTH * 6 + 2, 20);

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
