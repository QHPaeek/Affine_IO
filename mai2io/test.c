#include <windows.h>
#include <stdio.h>
#include <stdbool.h>
#include <conio.h>
#include "serial.h"
#include "dprintf.h"

// 外部变量
extern char comPort1[13];
extern char comPort2[13];
extern HANDLE hPort1;
extern HANDLE hPort2;
extern serial_packet_t response1;
extern serial_packet_t response2;
extern void package_init(serial_packet_t *response);
extern void serial_writeresp(HANDLE hPortx, serial_packet_t *response);

// 调试开关
// #define DEBUG

// 常量定义
#define TOUCH_REGIONS 34        // 触摸区域总数
#define BUTTONS_COUNT 9         // 按钮总数（+1）
#define THRESHOLD_DEFAULT 32768 // 默认阈值（65535的一半，对应显示值50）

// 颜色定义
#define COLOR_RED (FOREGROUND_RED | FOREGROUND_INTENSITY)
#define COLOR_GREEN (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COLOR_DEFAULT (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

// 设备状态枚举
typedef enum
{
    DEVICE_WAIT,
    DEVICE_FAIL,
    DEVICE_OK
} DeviceState;

// 窗口类型枚举
typedef enum
{
    WINDOW_MAIN,
    WINDOW_TOUCHPANEL
} WindowType;

// 全局变量
WindowType currentWindow = WINDOW_MAIN;
DeviceState deviceState1p = DEVICE_WAIT;
DeviceState deviceState2p = DEVICE_WAIT;
bool isFirstConnection1p = TRUE;
bool isFirstConnection2p = TRUE;
bool ledButtonsTest = FALSE;
bool ledControllerTest = FALSE;
bool showRawData = FALSE;
bool usePlayer2 = FALSE; // 控制是否切换到2P模式
HANDLE hConsole;
int consoleWidth = 100;
int consoleHeight = 36;
bool running = true;
char *Vid = "VID_AFF1";
char *Pid_1p = "PID_52A5";
char *Pid_2p = "PID_52A6";

// 玩家输入状态
uint8_t player1Buttons = 0;
uint8_t player2Buttons = 0;
uint8_t opButtons = 0;
uint8_t p1TouchState[7] = {0};
uint8_t p2TouchState[7] = {0};
uint8_t prev_p1TouchState[7] = {0};
uint8_t prev_p2TouchState[7] = {0};
uint8_t prev_player1Buttons = 0;
uint8_t prev_player2Buttons = 0;
uint8_t prev_opButtons = 0;

// 原始值数组
uint8_t p1RawValue[34] = {0};
uint8_t p2RawValue[34] = {0};

// LED状态
uint8_t buttonLEDs[24] = {0};
uint8_t fetLEDs[3] = {0};

// 阈值数据
uint16_t touchThreshold[TOUCH_REGIONS] = {0};

// 缓冲区状态
bool dataChanged = true; 

// 函数声明
void DisplayHeader(DeviceState state1p, DeviceState state2p);
void DisplayMainWindow();
void DisplayTouchPanelWindow();
void SwitchWindow();
void SwitchPlayer();
void HandleKeyInput();
void UpdateDeviceState();
void ReconnectDevices();
void UpdateButtonLEDs();
void UpdateTouchData();
void SetCursorPosition(int x, int y);
void ProcessTouchStateBytes(uint8_t state[7], bool touchMatrix[8][8]);
void DisplayThresholds();
void DisplayButtons();
void ModifyThreshold();
void InitThresholds();
void SendThresholds(HANDLE hPort, serial_packet_t *response);
void DisplayRawData(uint8_t *touchState, uint8_t *rawValue);

bool IsDataChanged();
void ClearLine(int line);

// 阈值转换辅助函数
static inline uint8_t threshold_to_display(uint16_t threshold)
{
    return (uint8_t)((threshold * 100) / 65535);
}

static inline uint16_t display_to_threshold(uint8_t display)
{
    return (uint16_t)((uint32_t)display * 65535 / 100);
}

int main()
{
    // 设置控制台代码页为UTF-8
    SetConsoleOutputCP(65001);

    // 获取控制台句柄
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE)
    {
        printf("Error getting console handle\n");
        return 1;
    }

    // 设置控制台大小
    COORD bufferSize = {(SHORT)consoleWidth, (SHORT)consoleHeight};
    SMALL_RECT windowSize = {0, 0, (SHORT)(consoleWidth - 1), (SHORT)(consoleHeight - 1)};
    SetConsoleScreenBufferSize(hConsole, bufferSize);
    SetConsoleWindowInfo(hConsole, TRUE, &windowSize);

    // 隐藏光标
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // 清屏一次
    system("cls");

    // 初始化阈值
    InitThresholds();

    // 尝试打开串口连接
    memset(comPort1, 0, 13);
    memset(comPort2, 0, 13);

    // 尝试连接1P设备
    memcpy(comPort1, GetSerialPortByVidPid(Vid, Pid_1p), 6);
    if (comPort1[0] == 0)
    {
        // 如果无法通过VID/PID找到设备，使用默认COM端口
        strcpy(comPort1, "\\\\.\\COM8");
    }
    else if (comPort1[4] == 0)
    {
        // 端口号小于10
        int port_num = (comPort1[3] - '0');
        snprintf(comPort1, 10, "\\\\.\\COM%d", port_num);
    }
    else if (comPort1[5] == 0)
    {
        // 两位数端口号
        int port_num = (comPort1[3] - '0') * 10 + (comPort1[4] - '0');
        snprintf(comPort1, 10, "\\\\.\\COM%d", port_num);
    }
    else
    {
        // 三位数端口号
        int port_num = (comPort1[3] - '0') * 100 + (comPort1[4] - '0') * 10 + (comPort1[5] - '0');
        snprintf(comPort1, 11, "\\\\.\\COM%d", port_num);
    }

    if (open_port(&hPort1, comPort1))
    {
        deviceState1p = DEVICE_OK;
        serial_scan_start(hPort1, &response1);
    }
    else
    {
        deviceState1p = DEVICE_WAIT; // 改为WAIT而不是FAIL
    }

    // 尝试连接2P设备
    memcpy(comPort2, GetSerialPortByVidPid(Vid, Pid_2p), 6);
    if (comPort2[0] == 0)
    {
        // 如果无法通过VID/PID找到设备，使用默认COM端口
        strcpy(comPort2, "\\\\.\\COM9");
    }
    else if (comPort2[4] == 0)
    {
        // 端口号小于10
    }
    else if (comPort2[5] == 0)
    {
        // 两位数端口号
        int port_num = (comPort2[3] - 48) * 10 + (comPort2[4] - 48);
        snprintf(comPort2, 10, "\\\\.\\COM%d", port_num);
    }
    else
    {
        // 三位数端口号
        int port_num = (comPort2[3] - 48) * 100 + (comPort2[4] - 48) * 10 + (comPort2[5] - 48);
        snprintf(comPort2, 11, "\\\\.\\COM%d", port_num);
    }

    // 修改2P设备连接部分
    if (open_port(&hPort2, comPort2))
    {
        deviceState2p = DEVICE_OK;
        serial_scan_start(hPort2, &response2);
    }
    else
    {
        deviceState2p = DEVICE_WAIT; // 改为WAIT而不是FAIL
    }

    // 第一次显示完整界面
    DisplayHeader(deviceState1p, deviceState2p);
    if (currentWindow == WINDOW_MAIN)
    {
        DisplayMainWindow();
    }
    else
    {
        DisplayTouchPanelWindow();
    }

    // 主循环
    while (running)
    {
        // 处理键盘输入
        if (_kbhit())
        {
            HandleKeyInput();
            dataChanged = true;
        }

        // 更新设备状态和数据
        UpdateDeviceState();
        UpdateTouchData();

        // 只有数据变化时才刷新屏幕
        if (dataChanged || IsDataChanged())
        {
            // 只更新变化的部分，而不是整个屏幕
            DisplayHeader(deviceState1p, deviceState2p);

            if (currentWindow == WINDOW_MAIN)
            {
                DisplayMainWindow();
            }
            else
            {
                DisplayTouchPanelWindow();
            }

            dataChanged = false;
        }

        // 设置刷新率
        Sleep(1); 
    }

    // 清理
    if (hPort1 != INVALID_HANDLE_VALUE)
    {
        serial_scan_stop(hPort1, &response1);
        close_port(&hPort1);
    }
    if (hPort2 != INVALID_HANDLE_VALUE)
    {
        serial_scan_stop(hPort2, &response2);
        close_port(&hPort2);
    }

    // 显示光标
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    return 0;
}

// 清除指定行
void ClearLine(int line)
{
    COORD coord = {0, (SHORT)line};
    DWORD written;
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo(hConsole, &csbi);
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X, coord, &written);
    FillConsoleOutputAttribute(hConsole, csbi.wAttributes, csbi.dwSize.X, coord, &written);
    SetConsoleCursorPosition(hConsole, coord);
}

// 检查数据是否发生变化
bool IsDataChanged()
{
    // 检查按钮状态是否变化
    if (prev_player1Buttons != player1Buttons ||
        prev_player2Buttons != player2Buttons ||
        prev_opButtons != opButtons)
    {
        prev_player1Buttons = player1Buttons;
        prev_player2Buttons = player2Buttons;
        prev_opButtons = opButtons;
        return true;
    }

    // 检查触摸状态是否变化
    bool touchChanged = false;
    for (int i = 0; i < 7; i++)
    {
        if (prev_p1TouchState[i] != p1TouchState[i])
        {
            touchChanged = true;
            prev_p1TouchState[i] = p1TouchState[i];
        }
        if (prev_p2TouchState[i] != p2TouchState[i])
        {
            touchChanged = true;
            prev_p2TouchState[i] = p2TouchState[i];
        }
    }

    return touchChanged;
}

void DisplayHeader(DeviceState state1p, DeviceState state2p)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    // 固定位置绘制头部信息
    SetCursorPosition(0, 0);
    printf("Curva Test Tools v0.1");

    SetCursorPosition(0, 1);
    printf("Device State 1P: ");
    switch (state1p)
    {
    case DEVICE_WAIT:
        SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
        printf("Wait");
        break;
    case DEVICE_FAIL:
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("Fail");
        break;
    case DEVICE_OK:
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("OK  ");
        break;
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);

    printf(" | Device State 2P: ");
    switch (state2p)
    {
    case DEVICE_WAIT:
        SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
        printf("Wait");
        break;
    case DEVICE_FAIL:
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("Fail");
        break;
    case DEVICE_OK:
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("OK  ");
        break;
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);

    SetCursorPosition(0, 2);
    if (currentWindow == WINDOW_MAIN)
    {
        printf("Press [TAB] to switch to Touch Panel View");
        if (deviceState2p == DEVICE_OK)
        {
            printf(" | Press [N] to switch to %s if connected", usePlayer2 ? "1P" : "2P");
        }
    }
    else
    {
        printf("Press [TAB] to switch to Main View");
        if (deviceState2p == DEVICE_OK)
        {
            printf(" | Press [N] to switch to %s if connected", usePlayer2 ? "1P" : "2P");
        }
    }
}

void DisplayMainWindow()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    // test_layout.txt布局
    SetCursorPosition(0, 4);
    printf("┌──────────────────────────── Trigger Threshold ───────────────────────────┐   ┌───── Input Test ─────┐");

    // 显示阈值数据
    DisplayThresholds();

    SetCursorPosition(0, 13);
    printf("└──────────────────────────────────────────────────────────────────────────┘   └──────────────────────┘");

    SetCursorPosition(0, 15);
    printf("┌────────────── LED Test ────────────┐    ┌─── Trigger Threshold Modify ───┐   ┌──── Side Buttons ────┐");

    // LED测试状态
    SetCursorPosition(0, 16);
    printf("│  [F1] Buttons LED Test     ");
    if (ledButtonsTest)
    {
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("RUNNING");
    }
    else
    {
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("STOP   ");
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);
    printf(" │    │  [F5] Modify Region Threshold  │   │ Select 1       ");

    // 显示Select 1按钮状态
    if (opButtons & (1 << 4))
    {
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("ON ");
    }
    else
    {
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("OFF");
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);
    printf("   │");

    // LED控制器测试
    SetCursorPosition(0, 17);
    printf("│  [F2] Controller LED Test  ");
    if (ledControllerTest)
    {
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("RUNNING");
    }
    else
    {
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("STOP   ");
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);
    printf(" │    │  [F6] Show Raw Touch Data  ");

    // 显示原始数据状态
    if (showRawData)
    {
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("ON ");
    }
    else
    {
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("OFF");
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);
    printf(" │   │ Select 2       ");

    // 显示Select 2按钮状态
    if (opButtons & (1 << 5))
    {
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("ON ");
    }
    else
    {
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("OFF");
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);
    printf("   │");

    SetCursorPosition(0, 18);
    printf("└────────────────────────────────────┘    └────────────────────────────────┘   │ Coin           ");
    // 显示Coin按钮状态
    if (opButtons & (1 << 2))
    {
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("ON ");
    }
    else
    {
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("OFF");
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);
    printf("   │");

    SetCursorPosition(0, 19);
    printf("┌─── Kobato Stats ────────────────────────────────────────────────────┐        │ Service        ");
    // 显示Service按钮状态
    if (opButtons & (1 << 1))
    {
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("ON ");
    }
    else
    {
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("OFF");
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);
    printf("   │");

    SetCursorPosition(0, 20);
    printf("│ State: %-3s | Baud: %-3s | LED: %-7s | Extend: %-3s | Reflect: %-3s │        │ Test           ",
           "Wait",
           "N/A",
           "N/A",
           "N/A",
           "N/A");
    // 显示Test按钮状态
    if (opButtons & (1 << 0))
    {
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("ON ");
    }
    else
    {
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("OFF");
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);
    printf("   │");

    SetCursorPosition(0, 21);
    printf("└─────────────────────────────────────────────────────────────────────┘        └──────────────────────┘");

    // 如果启用显示原始数据，在底部固定区域显示
    if (showRawData)
    {
        DisplayRawData(usePlayer2 ? p2TouchState : p1TouchState, usePlayer2 ? p2RawValue : p1RawValue);
    }
    else
    {
        // 如果不显示原始数据，清除该区域
        for (int i = 23; i < 33; i++) // 增加清除的行数，因为现在显示更多数据
        {
            ClearLine(i);
        }
    }
}

void DisplayThresholds()
{
    // 保存当前文本属性
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    // 处理触摸状态到矩阵
    bool touchMatrix[8][8] = {false};
    ProcessTouchStateBytes(usePlayer2 ? p2TouchState : p1TouchState, touchMatrix);

    // 显示阈值数据
    const char *labels[] = {"D", "A", "E", "B", "C"};

    for (int i = 0; i < 8; i++)
    {
        SetCursorPosition(0, 5 + i);
        printf("│ ");
        
        // D区域
        if (touchMatrix[i][0]) {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
        }
        printf("%s%d", labels[0], i + 1);
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        printf("  %2d/100  ", threshold_to_display(touchThreshold[4 * i]));

        // A区域
        if (touchMatrix[i][1]) {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
        }
        printf("%s%d", labels[1], i + 1);
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        printf("  %2d/100 │ ", threshold_to_display(touchThreshold[4 * i + 1]));

        // E区域
        if (touchMatrix[i][2]) {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
        }
        printf("%s%d", labels[2], i + 1);
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        printf("  %2d/100  ", threshold_to_display(touchThreshold[4 * i + 2]));

        // B区域
        if (touchMatrix[i][3]) {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
        }
        printf("%s%d", labels[3], i + 1);
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        printf("  %2d/100 │", threshold_to_display(touchThreshold[4 * i + 3]));

        // C区域或其他信息
        if (i < 2)
        {
            printf(" ");
            if (touchMatrix[i][4]) {
                SetConsoleTextAttribute(hConsole, COLOR_RED);
            }
            printf("%s%d", labels[4], i + 1);
            SetConsoleTextAttribute(hConsole, defaultAttrs);
            printf("  %2d/100             │", threshold_to_display(touchThreshold[32 + i]));
        }
        else if (i == 6)
        {
            printf("   OUT -> MID -> INNER  │");
        }
        else if (i == 7)
        {
            printf("     Current/Trigger    │");
        }
        else
        {
            printf("                        │");
        }

        // 按键部分
        if (i < BUTTONS_COUNT)
        {
            uint8_t buttons = usePlayer2 ? player2Buttons : player1Buttons;
            printf("   │ Button %d       ", i + 1);

            // 彩色显示按钮状态
            if (buttons & (1 << i))
            {
                SetConsoleTextAttribute(hConsole, COLOR_GREEN);
                printf("ON ");
            }
            else
            {
                SetConsoleTextAttribute(hConsole, COLOR_RED);
                printf("OFF");
            }
            SetConsoleTextAttribute(hConsole, defaultAttrs);
            printf("   │");
        }
        else
        {
            printf("   │                      │");
        }
    }
}

void PrintTouchPanelTrigger(const char *text, char triggeredRegions[][3], int count)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    int len = strlen(text);
    int i = 0;

    while (i < len)
    {
        bool highlighted = false;

        // 查找是否包含任何触发区域的标识符
        for (int j = 0; j < count; j++)
        {
            const char *region = triggeredRegions[j];
            int regionLen = strlen(region);

            if (i + regionLen <= len && strncmp(&text[i], region, regionLen) == 0)
            {
                // 找到触发区域的标识符，高亮显示
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                printf("%s", region);
                SetConsoleTextAttribute(hConsole, defaultAttrs);

                i += regionLen;
                highlighted = true;
                break;
            }
        }

        if (!highlighted)
        {
            printf("%c", text[i++]);
        }
    }
}

void DisplayTouchPanelWindow()
{
    int baseY = 3;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    // 处理触摸状态
    bool touchMatrix[8][8] = {false};
    ProcessTouchStateBytes(usePlayer2 ? p2TouchState : p1TouchState, touchMatrix);

    // 获取触发的区域标识符列表
    char triggeredRegions[TOUCH_REGIONS][3];
    int count = 0;

    const struct TouchRegion
    {
        char region[3];
        int matrixX;
        int matrixY;
    } regions[] = {
        // 格式: 区域名, 矩阵X, 矩阵Y
        {"D1", 0, 0},
        {"A1", 1, 0},
        {"E1", 2, 0},
        {"B1", 3, 0},
        {"D2", 0, 1},
        {"A2", 1, 1},
        {"E2", 2, 1},
        {"B2", 3, 1},
        {"D3", 0, 2},
        {"A3", 1, 2},
        {"E3", 2, 2},
        {"B3", 3, 2},
        {"D4", 0, 3},
        {"A4", 1, 3},
        {"E4", 2, 3},
        {"B4", 3, 3},
        {"D5", 0, 4},
        {"A5", 1, 4},
        {"E5", 2, 4},
        {"B5", 3, 4},
        {"D6", 0, 5},
        {"A6", 1, 5},
        {"E6", 2, 5},
        {"B6", 3, 5},
        {"D7", 0, 6},
        {"A7", 1, 6},
        {"E7", 2, 6},
        {"B7", 3, 6},
        {"D8", 0, 7},
        {"A8", 1, 7},
        {"E8", 2, 7},
        {"B8", 3, 7},
        {"C1", 4, 0},
        {"C2", 4, 1}};

    int numRegions = sizeof(regions) / sizeof(regions[0]);
    for (int i = 0; i < numRegions; i++)
    {
        const struct TouchRegion *region = &regions[i];

        if (region->matrixX < 8 && region->matrixY < 8 && touchMatrix[region->matrixY][region->matrixX])
        {
            strcpy(triggeredRegions[count], region->region);
            count++;
        }
    }

    // 绘制面板，高亮显示触发的区域
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("┌─────────────────────────── Touch Panel Test ───────────────────────────┐", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│          ────────────────────────────────────────────────────          │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│        ╱                          D1                          ╲        │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│       ╱              A8         D1D1D1         A1              ╲       │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│      ╱             A8A8                        A1A1             ╲      │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│     ╱                           E1E1E1                           ╲     │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│    ╱            D8                E1                D2            ╲    │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│   ╱           D8D8    E8E8                  E2E2    D2D2           ╲   │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  ╱                    E8E8     B8    B1     E2E2                    ╲  │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │          A7                B8B8  B1B1                A2          │  │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │        A7A7         B7      B8    B1      B2         A2A2        │  │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │                    B7B7                  B2B2                    │  │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │      D7     E7      B7       C2  C1       B2      E3     D3      │  │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │    D7     E7  E7           C2C2  C1C1           E3  E3     D3    │  │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │      D7     E7      B6       C2  C1       B3      E3     D3      │  │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │                    B6B6                  B3B3                    │  │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │        A6A6         B6      B5    B5      B3         A3A3        │  │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │          A6                B5B5  B4B4                A3          │  │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  ╲                    E6E6     B5    B4     E4E4                   ╱   │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│   ╲           D6D6    E6E6                  E4E4    D4D4          ╱    │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│    ╲            D6                E5                D4           ╱     │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│     ╲                           E5E5E5                          ╱      │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│      ╲            A5A5                          A4A4           ╱       │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│       ╲             A5          D5D5D5          A4            ╱        │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│        ╲                          D5                         ╱         │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│          ───────────────────────────────────────────────────           │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│            Trigger Threshold CAN BE Modified At Main View              │", triggeredRegions, count);
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("└────────────────────────────────────────────────────────────────────────┘", triggeredRegions, count);
}

void ProcessTouchStateBytes(uint8_t state[7], bool touchMatrix[8][8])
{
    // 从7个字节的触摸状态解析出8x8的触摸矩阵
    int byteIndex = 0;
    int bitIndex = 0;

    memset(touchMatrix, 0, sizeof(bool) * 8 * 8); // 清零矩阵

    for (int y = 0; y < 8 && byteIndex < 7; y++)
    {
        for (int x = 0; x < 8 && byteIndex < 7; x++)
        {
            // 每个字节存储多个触摸点状态
            if (bitIndex >= 8)
            {
                bitIndex = 0;
                byteIndex++;
                if (byteIndex >= 7)
                    break; // 防止越界
            }

            // 检查对应位是否为1
            touchMatrix[y][x] = (state[byteIndex] & (1 << bitIndex)) != 0;
            bitIndex++;
        }
        if (byteIndex >= 7)
            break; // 防止越界
    }
}

void SetCursorPosition(int x, int y)
{
    COORD pos = {(SHORT)x, (SHORT)y};
    SetConsoleCursorPosition(hConsole, pos);
}

void UpdateDeviceState()
{
    DeviceState prevState1p = deviceState1p;
    DeviceState prevState2p = deviceState2p;
    static DWORD lastHeartbeatTime = 0;
    DWORD currentTime = GetTickCount();

    // 每500毫秒发送一次心跳
    if (currentTime - lastHeartbeatTime >= 500)
    {
        lastHeartbeatTime = currentTime;

        // 检查1P设备状态
        if (deviceState1p == DEVICE_OK)
        {
            // 发送心跳并检查响应
            serial_heart_beat(hPort1, &response1);
            Sleep(20); // 给设备一点时间响应

            // 尝试读取响应
            if (serial_read_cmd(hPort1, &response1) == 0xff)
            {
                // 如果读取失败，设置为FAIL状态
                deviceState1p = DEVICE_FAIL;
            }
        }

        // 检查2P设备状态
        if (deviceState2p == DEVICE_OK)
        {
            // 发送心跳并检查响应
            serial_heart_beat(hPort2, &response2);
            Sleep(20); // 给设备一点时间响应

            // 尝试读取响应
            if (serial_read_cmd(hPort2, &response2) == 0xff)
            {
                // 如果读取失败，设置为FAIL状态
                deviceState2p = DEVICE_FAIL;
            }
        }
    }

    // 如果设备处于WAIT或FAIL状态，尝试重连
    if (deviceState1p == DEVICE_WAIT || deviceState1p == DEVICE_FAIL ||
        deviceState2p == DEVICE_WAIT || deviceState2p == DEVICE_FAIL)
    {
        ReconnectDevices();
    }

    // 设备状态变化时标记需要刷新
    if (prevState1p != deviceState1p || prevState2p != deviceState2p)
    {
        dataChanged = true;
    }
}

void ReconnectDevices()
{
    static DWORD lastReconnectTime = 0;
    DWORD currentTime = GetTickCount();

    // 每1秒尝试重连一次
    if (currentTime - lastReconnectTime < 1000)
    {
        return;
    }

    lastReconnectTime = currentTime;

    // 尝试重连1P
    if (deviceState1p == DEVICE_WAIT || deviceState1p == DEVICE_FAIL)
    {
        // 关闭可能已打开的端口
        close_port(&hPort1);

        // 设置为WAIT状态，表明正在等待重连
        deviceState1p = DEVICE_WAIT;
        dataChanged = true;

        memcpy(comPort1, GetSerialPortByVidPid(Vid, Pid_1p), 6);
        if (comPort1[0] == 0)
        {
            strcpy(comPort1, "\\\\.\\COM8");
        }
        else if (comPort1[4] == 0)
        {
            // 端口号小于10
            int port_num = (comPort1[3] - '0');
            snprintf(comPort1, 10, "\\\\.\\COM%d", port_num);
        }
        else if (comPort1[5] == 0)
        {
            // 两位数端口号
            int port_num = (comPort1[3] - '0') * 10 + (comPort1[4] - '0');
            snprintf(comPort1, 10, "\\\\.\\COM%d", port_num);
        }
        else
        {
            // 三位数端口号
            int port_num = (comPort1[3] - '0') * 100 + (comPort1[4] - '0') * 10 + (comPort1[5] - '0');
            snprintf(comPort1, 11, "\\\\.\\COM%d", port_num);
        }

        if (open_port(&hPort1, comPort1))
        {
            deviceState1p = DEVICE_OK;
            serial_scan_start(hPort1, &response1);
            dataChanged = true;
        }
        // 如果重连失败，保持WAIT状态
    }

    // 尝试重连2P
    if (deviceState2p == DEVICE_WAIT || deviceState2p == DEVICE_FAIL)
    {
        close_port(&hPort2);

        // 设置为WAIT状态，表明正在等待重连
        deviceState2p = DEVICE_WAIT;
        dataChanged = true;

        memcpy(comPort2, GetSerialPortByVidPid(Vid, Pid_2p), 6);
        if (comPort2[0] == 0)
        {
            strcpy(comPort2, "\\\\.\\COM9");
        }
        else if (comPort2[4] == 0)
        {
            // 端口号小于10
        }
        else if (comPort2[5] == 0)
        {
            int port_num = (comPort2[3] - 48) * 10 + (comPort2[4] - 48);
            snprintf(comPort2, 10, "\\\\.\\COM%d", port_num);
        }
        else
        {
            int port_num = (comPort2[3] - 48) * 100 + (comPort2[4] - 48) * 10 + (comPort2[5] - 48);
            snprintf(comPort2, 11, "\\\\.\\COM%d", port_num);
        }

        if (open_port(&hPort2, comPort2))
        {
            deviceState2p = DEVICE_OK;
            serial_scan_start(hPort2, &response2);
            dataChanged = true;
        }
        // 如果重连失败，保持WAIT状态
    }
}

void UpdateTouchData()
{
    // 内部循环变量
    const int READ_ITERATIONS = 100;
    bool dataUpdated = false;

    // 读取1P数据
    for (int iteration = 0; iteration < READ_ITERATIONS; iteration++)
    {
        // 读取1P数据
        if (deviceState1p == DEVICE_OK)
        {
            switch (serial_read_cmd(hPort1, &response1))
            {
            case SERIAL_CMD_AUTO_SCAN:
                memcpy(p1TouchState, response1.touch, 7);
                memcpy(p1RawValue, response1.raw_value, 34);
                player1Buttons = response1.key_status;
                opButtons = response1.io_status;
                package_init(&response1);
                dataUpdated = true;
                break;
            case 0xff:
                memset(p1TouchState, 0, sizeof(p1TouchState));
                player1Buttons = 0;
                
                // 关闭当前端口连接
                close_port(&hPort1);
                
                // 更新设备状态为等待状态
                deviceState1p = DEVICE_WAIT;
                dataChanged = true;
                break;
            default:
                break;
            }
        }
    }

    // 读取2P数据 - 同样简化处理
    if (deviceState2p == DEVICE_OK)
    {
        switch (serial_read_cmd(hPort2, &response2))
        {
        case SERIAL_CMD_AUTO_SCAN:
            memcpy(p2TouchState, response2.touch, 7);
            memcpy(p2RawValue, response2.raw_value, 34);
            player2Buttons = response2.key_status;
            opButtons |= response2.io_status;
            package_init(&response2);
            break;
        case 0xff:
            memset(p2TouchState, 0, sizeof(p2TouchState));
            player2Buttons = 0;
            
            // 关闭当前端口连接
            close_port(&hPort2);
            
            // 更新设备状态
            deviceState2p = DEVICE_WAIT;
            dataChanged = true;
            break;
        }
    }

    // 更新LED状态
    UpdateButtonLEDs();
}

void UpdateButtonLEDs()
{
    static int ledCycle = 0;
    static DWORD buttonLastTime = 0;
    static int fetCycle = 0;
    static DWORD fetLastTime = 0;
    DWORD currentTime = GetTickCount();

    // 按钮LED测试逻辑
    if (ledButtonsTest)
    {
        if (currentTime - buttonLastTime > 100)
        {
            buttonLastTime = currentTime;
            ledCycle = (ledCycle + 1) % 24;

            memset(buttonLEDs, 0, 24);
            buttonLEDs[ledCycle] = 255;

            // 发送LED命令
            if (deviceState1p == DEVICE_OK)
            {
                package_init(&response1);
                response1.syn = 0xff;
                response1.cmd = 0x02; // LED命令
                response1.size = 24;
                memcpy(response1.button_led, buttonLEDs, 24);
                serial_writeresp(hPort1, &response1);
            }

            if (deviceState2p == DEVICE_OK && !usePlayer2)
            {
                package_init(&response2);
                response2.syn = 0xff;
                response2.cmd = 0x02; // LED命令
                response2.size = 24;
                memcpy(response2.button_led, buttonLEDs, 24);
                serial_writeresp(hPort2, &response2);
            }
        }
    }

    if (ledControllerTest)
    {
        if (currentTime - fetLastTime > 100)
        {
            fetLastTime = currentTime;
            fetCycle = (fetCycle + 1) % 3;

            memset(fetLEDs, 0, 3);
            fetLEDs[fetCycle] = 255;

            // 发送FET LED命令
            if (deviceState1p == DEVICE_OK)
            {
                package_init(&response1);
                response1.syn = 0xff;
                response1.cmd = 0x09; // FET LED命令
                response1.size = 3;
                memcpy(response1.fet_led, fetLEDs, 3);
                serial_writeresp(hPort1, &response1);
            }

            if (deviceState2p == DEVICE_OK && !usePlayer2)
            {
                package_init(&response2);
                response2.syn = 0xff;
                response2.cmd = 0x09; // FET LED命令
                response2.size = 3;
                memcpy(response2.fet_led, fetLEDs, 3);
                serial_writeresp(hPort2, &response2);
            }
        }
    }
}

void HandleKeyInput()
{
    int key = _getch();

    switch (key)
    {
    case 9: // Tab键
        SwitchWindow();
        dataChanged = true; // 窗口切换，需要刷新
        break;
    case 27: // Esc键
        running = false;
        break;
    case 'n': // 切换玩家
    case 'N':
        if (deviceState2p == DEVICE_OK)
        {
            SwitchPlayer();
            dataChanged = true; // 玩家切换，需要刷新
        }
        break;
    case 0:
    case 224: // 功能键前缀
        key = _getch();
        switch (key)
        {
        case 59: // F1
            ledButtonsTest = !ledButtonsTest;
            dataChanged = true; // LED状态变化，需要刷新
            break;
        case 60: // F2
            ledControllerTest = !ledControllerTest;
            dataChanged = true; // LED状态变化，需要刷新
            break;
        case 63: // F5
            if (currentWindow == WINDOW_MAIN)
            {
                ModifyThreshold();
                dataChanged = true; // 阈值可能变化，需要刷新
            }
            break;
        case 64: // F6
            showRawData = !showRawData;
            dataChanged = true; // 显示设置变化，需要刷新
            break;
        }
        break;
    }
}

void SwitchWindow()
{
    if (currentWindow == WINDOW_MAIN)
    {
        currentWindow = WINDOW_TOUCHPANEL;
        system("cls"); // 窗口切换时需要清屏
    }
    else
    {
        currentWindow = WINDOW_MAIN;
        system("cls"); // 窗口切换时需要清屏
    }
}

void SwitchPlayer()
{
    usePlayer2 = !usePlayer2;
}

void InitThresholds()
{
    // 初始化所有阈值为默认值
    for (int i = 0; i < TOUCH_REGIONS; i++)
    {
        touchThreshold[i] = THRESHOLD_DEFAULT;
    }
}

void ModifyThreshold()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    // 保存当前位置，以便恢复
    int currentY = csbi.dwCursorPosition.Y;

    // 在底部显示提示信息
    int promptY = 23;

    // 清除提示区域
    for (int i = promptY; i < promptY + 4; i++)
    {
        ClearLine(i);
    }

    // 显示提示信息
    SetCursorPosition(0, promptY);
    printf("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\n");
    SetCursorPosition(0, promptY + 1);
    printf("┃     Please enter the Region ID and its Threshold that you want to change (e.g., E1/100)     ┃\n");
    SetCursorPosition(0, promptY + 2);
    printf("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n");
    SetCursorPosition(0, promptY + 3);
    printf("Region ID and its Threshold: ");

    // 显示光标
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // 获取用户输入
    char input[20];
    fgets(input, sizeof(input), stdin);
    
    // 移除可能的换行符
    size_t len = strlen(input);
    if (len > 0 && input[len-1] == '\n')
        input[len-1] = '\0';
    
    // 解析输入
    char regionType;
    int regionNum, display_threshold;
    if (sscanf(input, "%c%d/%d", &regionType, &regionNum, &display_threshold) == 3)
    {
        // 计算索引
        int index = -1;

        if (regionType >= 'A' && regionType <= 'E' && regionNum >= 1 && regionNum <= 8)
        {
            switch (regionType)
            {
            case 'D':
                index = (regionNum - 1) * 4;
                break;
            case 'A':
                index = (regionNum - 1) * 4 + 1;
                break;
            case 'E':
                index = (regionNum - 1) * 4 + 2;
                break;
            case 'B':
                index = (regionNum - 1) * 4 + 3;
                break;
            case 'C':
                if (regionNum <= 2)
                {
                    index = 32 + (regionNum - 1);
                }
                break;
            }

            // 更新阈值并发送到设备 - 将0-100的显示值转换为0-65535的实际值
            if (index >= 0 && index < TOUCH_REGIONS && display_threshold >= 0 && display_threshold <= 100)
            {
                touchThreshold[index] = display_to_threshold(display_threshold);

                // 发送更新的阈值到设备
                if (deviceState1p == DEVICE_OK)
                {
                    SendThresholds(hPort1, &response1);
                }
                if (deviceState2p == DEVICE_OK)
                {
                    SendThresholds(hPort2, &response2);
                }

                // 显示成功消息
                ClearLine(promptY + 3);
                SetCursorPosition(0, promptY + 3);
                SetConsoleTextAttribute(hConsole, COLOR_GREEN);
                printf("Threshold Updated: %c%d = %d/100 (Raw: %d)", regionType, regionNum,
                       display_threshold, touchThreshold[index]);
                SetConsoleTextAttribute(hConsole, defaultAttrs);
                Sleep(1000); // 短暂显示成功消息
            }
            else
            {
                // 显示错误消息
                ClearLine(promptY + 3);
                SetCursorPosition(0, promptY + 3);
                SetConsoleTextAttribute(hConsole, COLOR_RED);
                printf("Invalid Index or Threshold! The Threshold must be between 0-100.");
                SetConsoleTextAttribute(hConsole, defaultAttrs);
                Sleep(1000); // 短暂显示错误消息
            }
        }
        else
        {
            // 显示错误消息
            ClearLine(promptY + 3);
            SetCursorPosition(0, promptY + 3);
            SetConsoleTextAttribute(hConsole, COLOR_RED);
            printf("Invalid Region ID! ");
            SetConsoleTextAttribute(hConsole, defaultAttrs);
            Sleep(1000); // 短暂显示错误消息
        }
    }
    else
    {
        // 显示错误消息
        ClearLine(promptY + 3);
        SetCursorPosition(0, promptY + 3);
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("Invalid Input Format! ");
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        Sleep(1000); // 短暂显示错误消息
    }

    // 隐藏光标
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // 清除提示区域
    for (int i = promptY; i < promptY + 4; i++)
    {
        ClearLine(i);
    }

    // 强制更新显示
    dataChanged = true;
}

void SendThresholds(HANDLE hPort, serial_packet_t *response)
{
    package_init(response);
    response->syn = 0xff;
    response->cmd = SERIAL_CMD_CHANGE_TOUCH_THRESHOLD;
    response->size = 34;
    memcpy(response->threshold, touchThreshold, 34);
    serial_writeresp(hPort, response);
}

void DisplayRawData(uint8_t *touchState, uint8_t *rawValue)
{
    int rows = 9;        // 设置显示行数，34字节需要5行显示
    int bytesPerRow = 7; // 每行显示7个字节

    // 显示触摸状态字节
    SetCursorPosition(0, 23);
    printf("Raw Touch Data (all 34 bytes):                                              ");

    for (int row = 0; row < rows && row * bytesPerRow < 34; row++)
    {
        SetCursorPosition(0, 24 + row);
        ClearLine(24 + row); // 先清除行，再显示内容
        
        int startByte = row * bytesPerRow;
        int endByte = min(startByte + bytesPerRow, 34); // 确保不超过34字节

        // 显示字节索引范围
        printf("Bytes %2d-%2d: ", startByte, endByte - 1);

        // 显示每个字节的十六进制表示
        for (int i = startByte; i < endByte; i++)
        {
            if (i < 34)
            {
                printf("%02X ", rawValue[i]);
            }
        }

        // 填充空格使显示整齐
        for (int i = endByte; i < startByte + bytesPerRow; i++)
        {
            printf("   ");
        }

        printf(" | ");

        // 显示每个字节的十进制表示
        for (int i = startByte; i < endByte; i++)
        {
            if (i < 34)
            {
                printf("%3d ", rawValue[i]);
            }
        }

        ClearLine(24 + row); // 清除行尾可能的残留字符
    }
}