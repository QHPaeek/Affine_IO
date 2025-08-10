// Todo:
// 1. Raw读取
// 2. 2P测试逻辑修正

#define WIN32_LEAN_AND_MEAN
#define INITGUID
#include <windows.h>
#include <stdio.h>
#include <stdbool.h>
#include <conio.h>
#include <wchar.h>
#include <setupapi.h>
#include <devguid.h>

/* ---------- 项目头文件 ---------- */
#include "serial.h"
#include "dprintf.h"

/* ---------- 调试设置 ---------- */
// #define DEBUG

/* ---------- 常量定义 ---------- */
// 版本号
const char *VERSION = "v0.8";

// 触摸和按键相关常量
#define TOUCH_REGIONS 34        // 触摸区域总数
#define BUTTONS_COUNT 8         // 按钮总数
#define THRESHOLD_DEFAULT 16384 // 默认阈值
#define THRESHOLD_READ_FAILED 65535 // 标记读取失败的特殊值

// 连接和重连常量
#define INIT_CONNECT_ATTEMPTS 5
#define RECONNECT_INTERVAL 500

// 控制台颜色定义
#define COLOR_RED (FOREGROUND_RED | FOREGROUND_INTENSITY)
#define COLOR_GREEN (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COLOR_BLUE (FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define COLOR_DEFAULT (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

// 固件更新相关常量
#define VID_CH340 0x1A86
#define PID_CH340 0x7523
#define BAUDRATE_BOOT 115200
#define PAGE_SZ 256
#define ERASE_RETRY 3
#define SYNC_TIMEOUT_MS 2000

/* ---------- 类型定义 ---------- */
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
    WINDOW_TOUCHPANEL,
    WINDOW_FIRMWARE_UPDATE
} WindowType;

// 连接参数结构体
typedef struct
{
    bool isPlayer1;
} DeviceConnectParams;

/* ---------- 外部变量声明 ---------- */
extern char comPort1[13];
extern char comPort2[13];
extern HANDLE hPort1;
extern HANDLE hPort2;
extern serial_packet_t response1;
extern serial_packet_t response2;
extern void package_init(serial_packet_t *response);
extern void serial_writeresp(HANDLE hPortx, serial_packet_t *response);

/* ---------- 全局变量 ---------- */
// 窗口和UI相关
WindowType currentWindow = WINDOW_MAIN;
HANDLE hConsole;
int consoleWidth = 120;
int consoleHeight = 36;
bool running = true;
bool firmwareWindowDrawn = false;

// 设备状态变量
DeviceState deviceState1p = DEVICE_WAIT;
DeviceState deviceState2p = DEVICE_WAIT;
DeviceState deviceStateKobato = DEVICE_WAIT;
bool isFirstConnection1p = TRUE;
bool isFirstConnection2p = TRUE;
bool usePlayer2 = FALSE; // 控制是否切换到2P模式

// 阈值读取状态变量
bool thresholdReading1p = FALSE;
bool thresholdReading2p = FALSE;
int thresholdProgress1p = 0;  // 0-34，当前读取进度
int thresholdProgress2p = 0;  // 0-34，当前读取进度
bool thresholdComplete1p = FALSE;
bool thresholdComplete2p = FALSE;

// 设备连接配置
char *Vid = "VID_AFF1";
char *Pid_1p = "PID_52A5";
char *Pid_2p = "PID_52A6";
char *Vid_Kobato = "VID_0483";
char *Pid_Kobato = "PID_5740";
HANDLE hPortKobato = INVALID_HANDLE_VALUE;
char comPortKobato[13] = {0};

// Kobato设备特性配置
bool kobatoHighBaud = FALSE;
bool kobatoLedEnabled = FALSE;
uint8_t kobatoLedBrightness = 0;
bool kobatoExtendEnabled = FALSE;
bool kobatoReflectEnabled = FALSE;

// 测试和功能标志
bool ledButtonsTest = FALSE;
bool ledControllerTest = FALSE;
// bool showRawData = FALSE;
bool remapTouchSheet = FALSE;

// 多按键检测相关变量
DWORD buttonPressStartTime = 0; // 按键同时按下的起始时间
bool buttonFailMode = false;    // 标记按键是否进入FAIL
int previousButtonCount = 0;    // 之前按下的按键数量

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

// 延迟设置相关
uint8_t touchLatency = 0;
uint8_t buttonLatency = 0;
bool latencyRead = false;

// 触摸和阈值数据
// 原始值现在没用
uint8_t p1RawValue[34] = {0};
uint8_t p2RawValue[34] = {0};
// 触摸映射表: touchSheet[区域索引] = 物理通道
// 例如: touchSheet[0] = 5 表示A1区域映射到物理通道5
uint8_t touchSheet[TOUCH_REGIONS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33};
uint16_t touchThreshold[TOUCH_REGIONS] = {0};
bool thresholdReadFailed[TOUCH_REGIONS] = {false}; // 标记哪些区域读取失败

// LED状态
uint8_t buttonLEDs[24] = {0};
uint8_t fetLEDs[3] = {0};

// 缓冲区状态
bool dataChanged = true;

// 心跳控制变量
bool heartbeatEnabled = true;         // 心跳是否启用
DWORD heartbeatPauseStartTime = 0;    // 心跳暂停开始时间

// 固件更新相关变量
bool firmware_update_ready = false;          // 是否找到可更新的固件
bool firmware_updating = false;              // 是否正在更新
int firmware_update_progress = 0;            // 更新进度 (0-100)
wchar_t firmware_version[32] = L"v1.000000"; // 固件版本
wchar_t firmware_path[MAX_PATH] = {0};       // 固件路径
char *firmware_status_message = NULL;        // 状态信息
static HANDLE hBootPort = INVALID_HANDLE_VALUE;
static unsigned char *firmware_data = NULL;
static unsigned int firmware_data_len = 0;

// 自动映射相关变量
bool autoRemapActive = false;            // 自动映射是否激活
int autoRemapStage = 0;                  // 当前收集阶段（0-3）
int autoRemapCollected = 0;              // 当前阶段已收集的区块数
int autoRemapCompletedStages = 0;        // 已完成的阶段数（0-4）
DWORD autoRemapLastTime = 0;             // 上次收集时间
uint8_t autoRemapRegions[TOUCH_REGIONS]; // 记录按触发顺序收集的区块
char autoRemapStatus[32] = {0};          // 状态消息
bool autoRemapCompletedRegions[TOUCH_REGIONS] = {false}; // 标记已完成的区块
uint8_t prevTouchMatrix[8][8] = {0};     // 上一帧的触摸状态矩阵

/* ---------- 函数声明 ---------- */
// 阈值读取辅助函数
uint16_t ReadThreshold(HANDLE hPort, serial_packet_t *response, int index);

// 显示和UI相关函数
void DisplayHeader(DeviceState state1p, DeviceState state2p);
void DisplayMainWindow();
void DisplayTouchPanelWindow();
void DisplayFirmwareUpdateWindow();
void SetCursorPosition(int x, int y);
void ClearLine(int line);
void ProcessTouchStateBytes(uint8_t state[7], bool touchMatrix[8][8]);
void DisplayThresholds();
void DisplayInputTest();
void DisplayButtons();
void UpdateFirmwareProgressOnly();

// 操作和控制函数
void SwitchWindow();
void SwitchPlayer();
void HandleKeyInput();
void UpdateDeviceState();
void TryConnectDevice(bool isPlayer1);
void ReconnectDevices();
void UpdateButtonLEDs();
void UpdateTouchData();
void UpdateMultiButtonState();
bool IsDataChanged();

// 心跳控制函数
void PauseHeartbeat();
void ResumeHeartbeat();
void CheckAndStartThresholdReading(); // 检查并开始阈值读取

// 阈值和触摸配置相关函数
void ModifyThreshold();
void InitThresholds();
bool SendThreshold(HANDLE hPort, serial_packet_t *response, int index);
void ReadAllThresholds(HANDLE hPort, serial_packet_t *response);
bool ReadTouchSheet(HANDLE hPort, serial_packet_t *response);
bool WriteTouchSheet(HANDLE hPort, serial_packet_t *response);
void RemapTouchSheet();
void ReadLatencySettings(HANDLE hPort, serial_packet_t *response);
bool WriteLatencySettings(HANDLE hPort, serial_packet_t *response, uint8_t type, uint8_t value);
void ModifyLatency();
void CheckAndStartThresholdReading(); // 检查并开始阈值读取

// 配置文件操作
void SaveSettings();
void LoadSettings();

// 自动映射相关函数
void StartAutoRemap();            // 开始自动映射
void StopAutoRemap(bool success); // 停止自动映射
void UpdateAutoRemap();           // 更新自动映射状态
void ProcessAutoRemapTouch();     // 处理自动映射时的触摸
void CompleteAutoRemap();         // 完成自动映射并应用

// Kobato设备函数
void ConnectKobato();
bool ReadKobatoStatus();
void ReconnectKobato();

// 固件更新相关函数
void StartFirmwareUpdate(void);
bool FindFirmwareFile(void);
void PrepareForFirmwareUpdate();
void SetFirmwareStatusMessage(const char *msg);

// 阈值转换辅助函数
static inline uint16_t threshold_to_display(uint16_t threshold)
{
    // 使用四舍五入避免精度损失
    return (uint16_t)((threshold * 999 + 8192) / 16384);
}

// 格式化阈值显示，读取失败时显示UNK
static inline void format_threshold_display(char *buffer, int index)
{
    if (thresholdReadFailed[index])
    {
        strcpy(buffer, "UNK/999");
    }
    else
    {
        sprintf(buffer, "%3d/999", threshold_to_display(touchThreshold[index]));
    }
}

static inline uint16_t display_to_threshold(uint16_t display)
{
    // 使用四舍五入避免精度损失
    return (uint16_t)((uint32_t)display * 16384 + 499) / 999;
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

    system("cls");

    // 初始化阈值为默认值（仅作为备份）
    InitThresholds();

    // 尝试打开串口连接
    memset(comPort1, 0, 13);
    memset(comPort2, 0, 13);

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

    // 尝试连接设备
    TryConnectDevice(true);  // 连接1P设备
    TryConnectDevice(false); // 连接2P设备

    ConnectKobato(); // 初始连接Kobato设备

    // 主循环
    while (running)
    {
        // 处理键盘输入
        if (_kbhit())
        {
            HandleKeyInput();
            dataChanged = true;
        }

        // 检查是否需要开始阈值读取
        CheckAndStartThresholdReading();

        // 更新设备状态和数据
        UpdateDeviceState();
        UpdateTouchData();

        if (autoRemapActive)
        {
            UpdateAutoRemap();
            ProcessAutoRemapTouch();
        }

        // 只有数据变化时才刷新屏幕
        if (dataChanged || IsDataChanged())
        {
            DisplayHeader(deviceState1p, deviceState2p);

            if (currentWindow == WINDOW_MAIN)
            {
                DisplayMainWindow();
                firmwareWindowDrawn = false;
            }
            else if (currentWindow == WINDOW_TOUCHPANEL)
            {
                DisplayTouchPanelWindow();
                firmwareWindowDrawn = false;
            }
            else if (currentWindow == WINDOW_FIRMWARE_UPDATE)
            {
                if (!firmwareWindowDrawn)
                {
                    DisplayFirmwareUpdateWindow();
                    firmwareWindowDrawn = true;
                }
                else
                {
                    UpdateFirmwareProgressOnly();
                }
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
    if (hPortKobato != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hPortKobato);
    }

    if (firmware_status_message)
    {
        free(firmware_status_message);
        firmware_status_message = NULL;
    }

    // 显示光标
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    return 0;
}

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

bool IsDataChanged()
{
    if (prev_player1Buttons != player1Buttons ||
        prev_player2Buttons != player2Buttons ||
        prev_opButtons != opButtons)
    {
        prev_player1Buttons = player1Buttons;
        prev_player2Buttons = player2Buttons;
        prev_opButtons = opButtons;
        return true;
    }

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
    printf("Curva Test Tools %s", VERSION);

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
        printf("Press [TAB] to switch to Touch Panel View    Press [L] / [S] to LOAD / SAVE curva.ini");
        if (deviceState2p == DEVICE_OK)
        {
            printf(" | Press [N] to switch to %s if connected", usePlayer2 ? "1P" : "2P");
        }
    }
    else if (currentWindow == WINDOW_TOUCHPANEL)
    {
        printf("Press [TAB] to switch to Main View");
        if (deviceState2p == DEVICE_OK)
        {
            printf(" | Press [N] to switch to %s if connected", usePlayer2 ? "1P" : "2P");
        }
    }
    else if (currentWindow == WINDOW_FIRMWARE_UPDATE)
    {
        printf("Press [B] to return to Main View");
    }
}

void DisplayMainWindow()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    SetCursorPosition(0, 4);
    printf("┌──────────────────────────── Trigger Threshold ───────────────────────────┐   ┌───── Input Test ─────┐");

    // 显示阈值数据
    DisplayThresholds();
    
    // 显示Input Test（总是显示）
    DisplayInputTest();

    SetCursorPosition(0, 13);
    printf("└──────────────────────────────────────────────────────────────────────────┘   └──────────────────────┘");

    SetCursorPosition(0, 14);
    printf("┌────────── Board Utilities ─────────┐    ┌────── Touch Panel Modify ──────┐   ┌──── Side Buttons ────┐");

    // LED测试状态
    SetCursorPosition(0, 15);
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
    printf(" │    │  [F5] Modify Region Threshold  │   │ Select #1      ");

    // 显示Select 按钮状态
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
    SetCursorPosition(0, 16);
    printf("│  [F2] FET # LED Test       ");
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
    printf(" │    │  [F6] Remap Touch Sheet        │   │ Reserve        ");

    // 显示Reserve按钮状态
    if (opButtons & (1 << 3))
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

    // 添加F3和F7功能
    SetCursorPosition(0, 17);
    printf("│  [F3] Update Curva Firmware        │    │  [F7] Modify Latency           │   │ Coin           ");
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

    SetCursorPosition(0, 18);
    printf("└────────────────────────────────────┘    └────────────────────────────────┘   │ Service        ");
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

    SetCursorPosition(0, 19);
    printf("┌─── Kobato Stats ────────────────────────────────────────────────────┐        │ Test           ");
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

    SetCursorPosition(0, 20);
    printf("│ State: ");

    switch (deviceStateKobato)
    {
    case DEVICE_WAIT:
        printf("Wait");
        break;
    case DEVICE_FAIL:
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("Fail");
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        break;
    case DEVICE_OK:
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("OK  ");
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        break;
    }

    printf(" | Baud: ");
    if (deviceStateKobato == DEVICE_OK)
    {
        if (kobatoHighBaud)
        {
            SetConsoleTextAttribute(hConsole, COLOR_BLUE);
            printf("HIGH");
        }
        else
        {
            SetConsoleTextAttribute(hConsole, COLOR_GREEN);
            printf("LOW ");
        }
        SetConsoleTextAttribute(hConsole, defaultAttrs);
    }
    else
    {
        printf("N/A ");
    }

    printf(" | LED: ");
    if (deviceStateKobato == DEVICE_OK)
    {
        if (kobatoLedEnabled)
        {
            SetConsoleTextAttribute(hConsole, COLOR_GREEN);
            printf("ON");
            SetConsoleTextAttribute(hConsole, defaultAttrs);
            printf("/%-3d", kobatoLedBrightness);
        }
        else
        {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
            printf("OFF");
            SetConsoleTextAttribute(hConsole, defaultAttrs);
            printf("   ");
        }
    }
    else
    {
        printf("N/A   ");
    }

    printf(" | Extend: ");
    if (deviceStateKobato == DEVICE_OK)
    {
        if (kobatoExtendEnabled)
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
    }
    else
    {
        printf("N/A");
    }

    printf(" | Reflect: ");
    if (deviceStateKobato == DEVICE_OK)
    {
        if (kobatoReflectEnabled)
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
    }
    else
    {
        printf("N/A");
    }

    printf(" │        │   ONLY SEL#1 WORK    │");

    SetCursorPosition(0, 21);
    printf("└─────────────────────────────────────────────────────────────────────┘        └──────────────────────┘");

    for (int i = 23; i < 33; i++)
    {
        ClearLine(i);
    }
}

void DisplayThresholds()
{
    // 保存当前文本属性
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    // 检查设备状态
    bool device1pConnected = (deviceState1p == DEVICE_OK);
    bool device2pConnected = (deviceState2p == DEVICE_OK && usePlayer2);
    bool currentDeviceConnected = usePlayer2 ? device2pConnected : device1pConnected;
    
    // 如果设备未连接，显示 "Device Not Connected"
    if (!currentDeviceConnected)
    {
        for (int i = 0; i < 8; i++)
        {
            SetCursorPosition(0, 5 + i);
            if (i == 3) // 在中间显示消息
            {
                printf("│                           DEVICE NOT CONNECTED                           │");
            }
            else if (i == 4) // 在下一行也显示消息
            {
                printf("│                           DEVICE NOT CONNECTED                           │");
            }
            else
            {
                printf("│                                                                          │");
            }
        }
        return;
    }
    
    // 如果正在读取阈值，显示进度
    bool isReading = usePlayer2 ? thresholdReading2p : thresholdReading1p;
    bool isComplete = usePlayer2 ? thresholdComplete2p : thresholdComplete1p;
    int progress = usePlayer2 ? thresholdProgress2p : thresholdProgress1p;
    
    // 如果设备已连接但阈值还没有读取完成，显示等待或进度
    if (currentDeviceConnected && (!isComplete || isReading))
    {
        for (int i = 0; i < 8; i++)
        {
            SetCursorPosition(0, 5 + i);
            if (isReading)
            {
                // 正在读取，显示进度
                if (i == 3)
                {
                    printf("│                        Threshold Reading (%d/34)                         │", progress);
                }
                else if (i == 4)
                {
                    int barWidth = 60;
                    int filled = (progress * barWidth) / 34;
                    printf("│       [");
                    for (int j = 0; j < barWidth; j++)
                    {
                        if (j < filled)
                            printf("=");
                        else
                            printf(" ");
                    }
                    printf("]     │");
                }
                else
                {
                    printf("│                                                                          │");
                }
            }
            else
            {
                // 设备已连接但还没开始读取，显示准备中
                if (i == 3)
                {
                    printf("│                     Preparing to read thresholds...                      │");
                }
                else
                {
                    printf("│                                                                          │");
                }
            }
        }
        return;
    }

    // 正常显示阈值（设备已连接且阈值读取完成）
    // 处理触摸状态到矩阵
    bool touchMatrix[8][8] = {false};
    ProcessTouchStateBytes(usePlayer2 ? p2TouchState : p1TouchState, touchMatrix);

    // A1~A8: 0-7
    // B1~B8: 8-15
    // C1: 16, C2: 17
    // D1~D8: 18-25
    // E1~E8: 26-33
    const char *labels[] = {"D", "A", "E", "B", "C"};

    for (int i = 0; i < 8; i++)
    {
        SetCursorPosition(0, 5 + i);
        printf("│ ");

        // D区域
        if (touchMatrix[i][0])
        {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
        }
        printf("%s%d", labels[0], i + 1);
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        char threshold_str[16];
        format_threshold_display(threshold_str, 18 + i);
        printf(" %s  ", threshold_str);

        // A区域
        if (touchMatrix[i][1])
        {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
        }
        printf("%s%d", labels[1], i + 1);
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        format_threshold_display(threshold_str, i);
        printf(" %s │ ", threshold_str);

        // E区域
        if (touchMatrix[i][2])
        {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
        }
        printf("%s%d", labels[2], i + 1);
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        format_threshold_display(threshold_str, 26 + i);
        printf(" %s  ", threshold_str);

        // B区域
        if (touchMatrix[i][3])
        {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
        }
        printf("%s%d", labels[3], i + 1);
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        format_threshold_display(threshold_str, 8 + i);
        printf(" %s │", threshold_str);

        // C区域
        if (i < 2) // C1 (Index 16), C2 (Index 17)
        {
            printf(" ");
            if (touchMatrix[i][4])
            {
                SetConsoleTextAttribute(hConsole, COLOR_RED);
            }
            printf("%s%d", labels[4], i + 1);
            SetConsoleTextAttribute(hConsole, defaultAttrs);
            format_threshold_display(threshold_str, 16 + i);
            printf(" %s             │", threshold_str);
        }
        else if (i == 6)
        {
            printf("  OUT -> MID -> INNER   │");
        }
        else if (i == 7)
        {
            printf("     Trigger / MAX      │");
        }
        else
        {
            printf("                        │");
        }
    }
}

void DisplayInputTest()
{
    // 保存当前文本属性
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;
    
    // 显示Input Test的8行
    for (int i = 0; i < 8; i++)
    {
        SetCursorPosition(76, 5 + i); 
        
        if (i < BUTTONS_COUNT)
        {
            uint8_t buttons = usePlayer2 ? player2Buttons : player1Buttons;
            printf("   │ Button %d       ", i + 1);

            // 彩色显示按钮状态
            if (buttons & (1 << i))
            {
                // 检查是否处于FAIL模式
                if (buttonFailMode)
                {
                    SetConsoleTextAttribute(hConsole, COLOR_RED);
                    printf("FAIL");
                }
                else
                {
                    SetConsoleTextAttribute(hConsole, COLOR_GREEN);
                    printf("ON  ");
                }
            }
            else
            {
                SetConsoleTextAttribute(hConsole, COLOR_RED);
                printf("OFF ");
            }
            SetConsoleTextAttribute(hConsole, defaultAttrs);
            printf("  │");
        }
        else
        {
            printf("│                      │");
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

        // 首先尝试匹配长度为2的区域标识符（如A1,B2等）
        if (i + 2 <= len &&
            ((text[i] == 'A' || text[i] == 'B' || text[i] == 'C' ||
              text[i] == 'D' || text[i] == 'E') &&
             text[i + 1] >= '1' && text[i + 1] <= '8'))
        {
            char region[3] = {text[i], text[i + 1], '\0'};

            // 检查是否是已完成的区域（实时检查）
            bool isCompletedRegion = false;

            if (autoRemapActive) {
                // 将区域名称转换为索引以检查是否已完成
                int regionIndex = -1;
                char type = region[0];
                int num = region[1] - '0';

                // 根据区域类型和编号计算索引
                if (type == 'A') {
                    regionIndex = num - 1; // A1-A8 -> 0-7
                } else if (type == 'B') {
                    regionIndex = num + 7; // B1-B8 -> 8-15
                } else if (type == 'C') {
                    regionIndex = num + 15; // C1-C2 -> 16-17
                } else if (type == 'D') {
                    regionIndex = num + 17; // D1-D8 -> 18-25
                } else if (type == 'E') {
                    regionIndex = num + 25; // E1-E8 -> 26-33
                }

                // 检查该区域是否已完成
                if (regionIndex >= 0 && regionIndex < TOUCH_REGIONS) {
                    isCompletedRegion = autoRemapCompletedRegions[regionIndex];
                }
            }

            // 如果是已完成区域，绿色显示
            if (isCompletedRegion)
            {
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                printf("%c%c", region[0], region[1]);
                SetConsoleTextAttribute(hConsole, defaultAttrs);
                i += 2;
                highlighted = true;
            }
            // 否则检查是否是当前触发的区域
            else
            {
                for (int j = 0; j < count && !highlighted; j++)
                {
                    if (strcmp(region, triggeredRegions[j]) == 0)
                    {
                        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                        printf("%c%c", region[0], region[1]);
                        SetConsoleTextAttribute(hConsole, defaultAttrs);
                        i += 2;
                        highlighted = true;
                    }
                }
            }
        }

        // 如果没有匹配2字符的区域，再尝试匹配3字符区域（如 D1D, A1A 等）
        if (!highlighted && i + 3 <= len)
        {
            char region[4] = {text[i], text[i + 1], text[i + 2], '\0'};

            for (int j = 0; j < count; j++)
            {
                const char *triggerRegion = triggeredRegions[j];
                if (strlen(triggerRegion) == 3 && strcmp(region, triggerRegion) == 0)
                {
                    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                    printf("%s", region);
                    SetConsoleTextAttribute(hConsole, defaultAttrs);
                    i += 3;
                    highlighted = true;
                    break;
                }
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
    printf("   ┌───────── Touch Auto Remap ─────────┐");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│          ────────────────────────────────────────────────────          │", triggeredRegions, count);
    printf("   │   Press [F4] to start Auto Remap   │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│        ╱                          D1                          ╲        │", triggeredRegions, count);
    printf("   │                                    │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│       ╱              A8         D1D1D1         A1              ╲       │", triggeredRegions, count);
    printf("   │   Once Auto Remap begins, trigger  │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│      ╱             A8A8                        A1A1             ╲      │", triggeredRegions, count);
    printf("   │   the regions in clockwise order:  │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│     ╱                           E1E1E1                           ╲     │", triggeredRegions, count);
    printf("   │         OUT -> MID -> INNER        │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│    ╱            D8                E1                D2            ╲    │", triggeredRegions, count);
    printf("   │                                    │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│   ╱           D8D8    E8E8                  E2E2    D2D2           ╲   │", triggeredRegions, count);
    printf("   │   After finishing each ring, wait  │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  ╱                    E8E8     B8    B1     E2E2                    ╲  │", triggeredRegions, count);
    printf("   │   for that ring to turn green      │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │          A7                B8B8  B1B1                A2          │  │", triggeredRegions, count);
    printf("   │   before moving on to the next.    │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │        A7A7         B7      B8    B1      B2         A2A2        │  │", triggeredRegions, count);
    printf("   │                                    │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │                    B7B7                  B2B2                    │  │", triggeredRegions, count);
    printf("   │   Triggering any other ring or     │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │      D7     E7      B7       C2  C1       B2      E3     D3      │  │", triggeredRegions, count);
    printf("   │   waiting 15 seconds will end      │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │    D7     E7  E7           C2C2  C1C1           E3  E3     D3    │  │", triggeredRegions, count);
    printf("   │   the scan.                        │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │      D7     E7      B6       C2  C1       B3      E3     D3      │  │", triggeredRegions, count);
    printf("   │                                    │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │                    B6B6                  B3B3                    │  │", triggeredRegions, count);
    printf("   │      The scan start from D1        │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │        A6A6         B6      B5    B4      B3         A3A3        │  │", triggeredRegions, count);
    printf("   │                                    │");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  │          A6                B5B5  B4B4                A3          │  │", triggeredRegions, count);

    printf("   │   Status: ");

    // 添加状态显示
    if (autoRemapActive || autoRemapStatus[0] != '\0')
    {
        if (autoRemapActive)
        {
            SetConsoleTextAttribute(hConsole, COLOR_BLUE);
        }
        else if (strstr(autoRemapStatus, "SUCCESS") != NULL)
        {
            SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        }
        else if (strstr(autoRemapStatus, "FAILED") != NULL ||
                 strstr(autoRemapStatus, "CANCELED") != NULL)
        {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
        }
        else
        {
            SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
        }
        printf("%-22s", autoRemapStatus);
        SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
    }
    else
    {
        printf("                      ");
    }
    printf("   │");

    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│  ╲                    E6E6     B5    B4     E4E4                   ╱   │", triggeredRegions, count);
    printf("   │                                    │");

    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│   ╲           D6D6    E6E6                  E4E4    D4D4          ╱    │", triggeredRegions, count);
    printf("   └────────────────────────────────────┘");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│    ╲            D6                E5                D4           ╱     │", triggeredRegions, count);
    printf("                                         ");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│     ╲                           E5E5E5                          ╱      │", triggeredRegions, count);
    printf("                                         ");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│      ╲            A5A5                          A4A4           ╱       │", triggeredRegions, count);
    printf("                                         ");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│       ╲             A5          D5D5D5          A4            ╱        │", triggeredRegions, count);
    printf("                                         ");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│        ╲                          D5                         ╱         │", triggeredRegions, count);
    printf("                                         ");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("│          ───────────────────────────────────────────────────           │", triggeredRegions, count);
    printf("                                         ");
    SetCursorPosition(0, baseY++);
    PrintTouchPanelTrigger("└────────────────────────────────────────────────────────────────────────┘", triggeredRegions, count);
    printf("                                         ");
}

void ProcessTouchStateBytes(uint8_t state[7], bool touchMatrix[8][8])
{
    // 清零矩阵
    memset(touchMatrix, 0, sizeof(bool) * 8 * 8);

    // A1-A8区域 (A1-A5在byte0, A6-A8在byte1)
    touchMatrix[0][1] = (state[0] & 0x01) != 0; // A1
    touchMatrix[1][1] = (state[0] & 0x02) != 0; // A2
    touchMatrix[2][1] = (state[0] & 0x04) != 0; // A3
    touchMatrix[3][1] = (state[0] & 0x08) != 0; // A4
    touchMatrix[4][1] = (state[0] & 0x10) != 0; // A5
    touchMatrix[5][1] = (state[1] & 0x01) != 0; // A6
    touchMatrix[6][1] = (state[1] & 0x02) != 0; // A7
    touchMatrix[7][1] = (state[1] & 0x04) != 0; // A8

    // B1-B8区域 (B1-B2在byte1, B3-B7在byte2, B8在byte3)
    touchMatrix[0][3] = (state[1] & 0x08) != 0; // B1
    touchMatrix[1][3] = (state[1] & 0x10) != 0; // B2
    touchMatrix[2][3] = (state[2] & 0x01) != 0; // B3
    touchMatrix[3][3] = (state[2] & 0x02) != 0; // B4
    touchMatrix[4][3] = (state[2] & 0x04) != 0; // B5
    touchMatrix[5][3] = (state[2] & 0x08) != 0; // B6
    touchMatrix[6][3] = (state[2] & 0x10) != 0; // B7
    touchMatrix[7][3] = (state[3] & 0x01) != 0; // B8

    // C1-C2区域
    touchMatrix[0][4] = (state[3] & 0x02) != 0; // C1
    touchMatrix[1][4] = (state[3] & 0x04) != 0; // C2

    // D1-D8区域 (D1-D2在byte3, D3-D7在byte4, D8在byte5)
    touchMatrix[0][0] = (state[3] & 0x08) != 0; // D1
    touchMatrix[1][0] = (state[3] & 0x10) != 0; // D2
    touchMatrix[2][0] = (state[4] & 0x01) != 0; // D3
    touchMatrix[3][0] = (state[4] & 0x02) != 0; // D4
    touchMatrix[4][0] = (state[4] & 0x04) != 0; // D5
    touchMatrix[5][0] = (state[4] & 0x08) != 0; // D6
    touchMatrix[6][0] = (state[4] & 0x10) != 0; // D7
    touchMatrix[7][0] = (state[5] & 0x01) != 0; // D8

    // E1-E8区域 (E1-E4在byte5, E5-E8在byte6)
    touchMatrix[0][2] = (state[5] & 0x02) != 0; // E1
    touchMatrix[1][2] = (state[5] & 0x04) != 0; // E2
    touchMatrix[2][2] = (state[5] & 0x08) != 0; // E3
    touchMatrix[3][2] = (state[5] & 0x10) != 0; // E4
    touchMatrix[4][2] = (state[6] & 0x01) != 0; // E5
    touchMatrix[5][2] = (state[6] & 0x02) != 0; // E6
    touchMatrix[6][2] = (state[6] & 0x04) != 0; // E7
    touchMatrix[7][2] = (state[6] & 0x08) != 0; // E8
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

    // 每100毫秒发送一次心跳
    if (heartbeatEnabled && currentTime - lastHeartbeatTime >= 100)
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

    if (deviceStateKobato == DEVICE_WAIT)
    {
        ReconnectKobato();
    }
    else if (deviceStateKobato == DEVICE_OK)
    {
        static DWORD lastKobatoUpdateTime = 0;
        DWORD currentTime = GetTickCount();

        if (currentTime - lastKobatoUpdateTime >= 3000)
        {
            lastKobatoUpdateTime = currentTime;
            if (!ReadKobatoStatus())
            {
                deviceStateKobato = DEVICE_FAIL;
                dataChanged = true;
            }
        }
    }
    else if (deviceStateKobato == DEVICE_FAIL)
    {
        ReconnectKobato();
    }
}

// 心跳控制函数
void PauseHeartbeat()
{
    heartbeatEnabled = false;
    heartbeatPauseStartTime = GetTickCount();
    Sleep(300); // 等待300ms让心跳包超时
}

void ResumeHeartbeat()
{
    heartbeatEnabled = true;
    heartbeatPauseStartTime = 0;
}

void CheckAndStartThresholdReading()
{
    // 检查1P设备
    if (deviceState1p == DEVICE_OK && !thresholdReading1p && !thresholdComplete1p && isFirstConnection1p)
    {
        isFirstConnection1p = FALSE;
        // 开始阈值读取
        ReadAllThresholds(hPort1, &response1);
        ReadTouchSheet(hPort1, &response1);
    }
    
    // 检查2P设备
    if (deviceState2p == DEVICE_OK && !thresholdReading2p && !thresholdComplete2p && isFirstConnection2p)
    {
        isFirstConnection2p = FALSE;
        // 开始阈值读取
        ReadAllThresholds(hPort2, &response2);
        ReadTouchSheet(hPort2, &response2);
    }
}

void TryConnectDevice(bool isPlayer1)
{
    HANDLE *phPort = isPlayer1 ? &hPort1 : &hPort2;
    char *comPort = isPlayer1 ? comPort1 : comPort2;
    char *vid = Vid;
    char *pid = isPlayer1 ? Pid_1p : Pid_2p;
    DeviceState *pDeviceState = isPlayer1 ? &deviceState1p : &deviceState2p;
    serial_packet_t *pResponse = isPlayer1 ? &response1 : &response2;

    // Sleep(50);

    // 查找设备COM端口
    char* foundPort = GetSerialPortByVidPid(vid, pid);
    
    if (foundPort[0] == 0)
    {
        if (isPlayer1)
        {
            // 对于1P设备，使用默认端口COM11
            // snprintf(comPort, 12, "\\\\.\\COM11");
        }
        else
        {
            // 2P设备不使用默认端口，保持WAIT状态
            *pDeviceState = DEVICE_WAIT;
            return;
        }
    }
    else
    {
        // 直接使用找到的端口名，添加Windows命名空间前缀
        snprintf(comPort, 12, "\\\\.\\%s", foundPort);
    }

    // 多次尝试连接
    bool connected = false;

    for (int attempt = 0; attempt < INIT_CONNECT_ATTEMPTS && !connected; attempt++)
    {
        if (open_port(phPort, comPort))
        {
            *pDeviceState = DEVICE_OK;
            serial_scan_start(*phPort, pResponse);
            
            // 触发界面刷新显示设备已连接
            dataChanged = true;

            connected = true;
        }
        else if (attempt < INIT_CONNECT_ATTEMPTS - 1)
        {
            // Sleep(50);

            // 每次重试前重新搜索设备
            char* foundPortRetry = GetSerialPortByVidPid(vid, pid);
            if (foundPortRetry[0] != 0)
            {
                snprintf(comPort, 12, "\\\\.\\%s", foundPortRetry);
            }
        }
    }

    if (!connected)
    {
        *pDeviceState = DEVICE_WAIT;
    }
}

void ReconnectDevices()
{
    static DWORD lastReconnectTime = 0;
    DWORD currentTime = GetTickCount();

    if (currentTime - lastReconnectTime < RECONNECT_INTERVAL)
    {
        return;
    }

    lastReconnectTime = currentTime;

    // 尝试重连1P
    if (deviceState1p == DEVICE_WAIT || deviceState1p == DEVICE_FAIL)
    {
        // 关闭可能已打开的端口
        close_port(&hPort1);

        deviceState1p = DEVICE_WAIT;
        // 重置阈值读取状态
        thresholdReading1p = FALSE;
        thresholdComplete1p = FALSE;
        thresholdProgress1p = 0;
        isFirstConnection1p = TRUE;
        dataChanged = true;

        // Sleep(100);
        char* foundPort1p = GetSerialPortByVidPid(Vid, Pid_1p);

        // 处理COM端口格式
        if (foundPort1p[0] == 0)
        {
            // 如果无法通过VID/PID找到设备，使用默认端口COM11
            // snprintf(comPort1, 12, "\\\\.\\COM11");
        }
        else
        {
            // 直接使用找到的端口名，添加Windows命名空间前缀
            snprintf(comPort1, 12, "\\\\.\\%s", foundPort1p);
        }

        // 尝试多次连接
        for (int attempt = 0; attempt < 3; attempt++)
        {
            if (open_port(&hPort1, comPort1))
            {
                deviceState1p = DEVICE_OK;
                serial_scan_start(hPort1, &response1);
                
                // 触发界面刷新显示设备已连接，阈值读取将在主循环中处理
                dataChanged = true;
                break;
            }
            Sleep(50); // 短暂等待再次尝试
        }
        // 如果重连失败，保持WAIT状态
    }

    // 尝试重连2P
    if (deviceState2p == DEVICE_WAIT || deviceState2p == DEVICE_FAIL)
    {
        close_port(&hPort2);

        deviceState2p = DEVICE_WAIT;
        // 重置阈值读取状态
        thresholdReading2p = FALSE;
        thresholdComplete2p = FALSE;
        thresholdProgress2p = 0;
        isFirstConnection2p = TRUE;
        dataChanged = true;

        // Sleep(100);
        char* foundPort2p = GetSerialPortByVidPid(Vid, Pid_2p);

        // 处理COM端口格式（使用相同的简化逻辑）
        if (foundPort2p[0] == 0)
        {
            // 不连接到默认端口
            deviceState2p = DEVICE_WAIT;
            return;
        }
        else
        {
            // 直接使用找到的端口名，添加Windows命名空间前缀
            snprintf(comPort2, 12, "\\\\.\\%s", foundPort2p);
        }

        // 尝试多次连接
        for (int attempt = 0; attempt < 3; attempt++)
        {
            if (open_port(&hPort2, comPort2))
            {
                deviceState2p = DEVICE_OK;
                serial_scan_start(hPort2, &response2);
                
                // 触发界面刷新显示设备已连接，阈值读取将在主循环中处理
                dataChanged = true;
                break;
            }
            Sleep(50); // 短暂等待再次尝试
        }
    }
}

void UpdateTouchData()
{
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
                // memcpy(p1RawValue, response1.raw_value, 34);
                player1Buttons = (response1.key_status[0] & 0x0F) | (response1.key_status[1] & 0xF0);
                opButtons = response1.io_status;
                package_init(&response1);
                dataUpdated = true;
                break;
            case 0xff:
                memset(p1TouchState, 0, sizeof(p1TouchState));
                player1Buttons = 0;

                // 关闭当前端口连接
                close_port(&hPort1);

                // 更新设备状态为等待状态并重置阈值读取状态
                deviceState1p = DEVICE_WAIT;
                thresholdReading1p = FALSE;
                thresholdComplete1p = FALSE;
                thresholdProgress1p = 0;
                isFirstConnection1p = TRUE;
                dataChanged = true;
                break;
            default:
                break;
            }
        }
    }

    // 读取2P数据
    if (deviceState2p == DEVICE_OK)
    {
        switch (serial_read_cmd(hPort2, &response2))
        {
        case SERIAL_CMD_AUTO_SCAN:
            memcpy(p2TouchState, response2.touch, 7);
            // memcpy(p2RawValue, response2.raw_value, 34);
            player2Buttons = (response2.key_status[0] & 0x0F) | (response2.key_status[1] & 0xF0);
            opButtons |= response2.io_status;
            package_init(&response2);
            break;
        case 0xff:
            memset(p2TouchState, 0, sizeof(p2TouchState));
            player2Buttons = 0;

            // 关闭当前端口连接
            close_port(&hPort2);

            // 更新设备状态并重置阈值读取状态
            deviceState2p = DEVICE_WAIT;
            thresholdReading2p = FALSE;
            thresholdComplete2p = FALSE;
            thresholdProgress2p = 0;
            isFirstConnection2p = TRUE;
            dataChanged = true;
            break;
        }
    }

    // 更新LED状态
    UpdateButtonLEDs();

    UpdateMultiButtonState();
}

void UpdateButtonLEDs()
{
    static int ledCycle = 0;
    static DWORD buttonLastTime = 0;
    static int fetCycle = 0;
    static DWORD fetLastTime = 0;
    DWORD currentTime = GetTickCount();

    // 检查当前激活的设备状态，如果是WAIT状态则禁用所有测试
    if ((usePlayer2 && deviceState2p != DEVICE_OK) || (!usePlayer2 && deviceState1p != DEVICE_OK))
    {
        // 如果当前设备不可用，自动关闭所有测试
        if (ledButtonsTest || ledControllerTest)
        {
            ledButtonsTest = false;
            ledControllerTest = false;
            dataChanged = true;
        }
        return;
    }

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

// 检测多按键同时按下状态
void UpdateMultiButtonState()
{
    // 获取当前活跃的按键
    uint8_t currentButtons = usePlayer2 ? player2Buttons : player1Buttons;

    // 计算按下的按键数量
    int buttonCount = 0;
    for (int i = 0; i < BUTTONS_COUNT; i++)
    {
        if (currentButtons & (1 << i))
        {
            buttonCount++;
        }
    }

    DWORD currentTime = GetTickCount();

    if (buttonCount >= 3)
    {
        if (previousButtonCount < 3)
        {
            buttonPressStartTime = currentTime;
        }
        else if (currentTime - buttonPressStartTime >= 2500 && !buttonFailMode)
        {
            buttonFailMode = true;
            dataChanged = true; // 强制刷新显示
        }
    }
    else
    {
        // 按键数量少于3，重置状态
        if (buttonFailMode)
        {
            buttonFailMode = false;
            dataChanged = true; // 强制刷新显示
        }
    }

    previousButtonCount = buttonCount;
}

void HandleKeyInput()
{
    int key = _getch();

    switch (key)
    {
    case 9: // Tab键
        if (currentWindow == WINDOW_FIRMWARE_UPDATE)
        {
            // 在固件更新模式下不允许Tab切换
            break;
        }
        SwitchWindow();
        dataChanged = true;
        break;
    case 27: // Esc键
        running = false;
        break;
    case 'b':
    case 'B':
        if (currentWindow == WINDOW_FIRMWARE_UPDATE && !firmware_updating)
        {
            currentWindow = WINDOW_MAIN;
            system("cls");
            firmwareWindowDrawn = false; // 重置固件窗口绘制标志
            dataChanged = true;
        }
        break;
    case 'n': // 切换玩家
    case 'N':
        if (currentWindow != WINDOW_FIRMWARE_UPDATE && deviceState2p == DEVICE_OK)
        {
            SwitchPlayer();
            dataChanged = true;
        }
        break;
    case 'l': // 加载配置
    case 'L':
        if (currentWindow != WINDOW_FIRMWARE_UPDATE)
        {
            LoadSettings();
            dataChanged = true;
        }
        break;
    case 's': // 保存配置
    case 'S':
        if (currentWindow != WINDOW_FIRMWARE_UPDATE)
        {
            SaveSettings();
            dataChanged = true;
        }
        break;
    case '\r': // Enter键 - 开始固件更新
        if (currentWindow == WINDOW_FIRMWARE_UPDATE && !firmware_updating)
        {
            // 如果固件未准备好，重新查找固件文件
            if (!firmware_update_ready || firmware_data == NULL)
            {
                firmware_update_ready = FindFirmwareFile();
                dataChanged = true;
            }
            
            // 如果固件已准备好，开始更新
            if (firmware_update_ready)
            {
                StartFirmwareUpdate();
                dataChanged = true;
            }
        }
        break;
    case 0:
    case 224: // 功能键前缀
        key = _getch();
        switch (key)
        {
        case 59: // F1
            if (currentWindow != WINDOW_FIRMWARE_UPDATE /*&&
                ((usePlayer2 && deviceState2p == DEVICE_OK) || (!usePlayer2 && deviceState1p == DEVICE_OK))*/)
            {
                ledButtonsTest = !ledButtonsTest;
                dataChanged = true;
            }
            break;
        case 60: // F2
            if (currentWindow != WINDOW_FIRMWARE_UPDATE /*&&
                ((usePlayer2 && deviceState2p == DEVICE_OK) || (!usePlayer2 && deviceState1p == DEVICE_OK))*/)
            {
                ledControllerTest = !ledControllerTest;
                dataChanged = true;
            }
            break;
        case 61: // F3 - 更新固件
            if (currentWindow == WINDOW_MAIN)
            {
                firmware_update_ready = FindFirmwareFile();
                currentWindow = WINDOW_FIRMWARE_UPDATE;
                system("cls");
                SetFirmwareStatusMessage(NULL);
                dataChanged = true;
            }
            else if (currentWindow == WINDOW_FIRMWARE_UPDATE && !firmware_updating)
            {
                currentWindow = WINDOW_MAIN;
                system("cls");
                firmwareWindowDrawn = false; // 重置固件窗口绘制标志
                dataChanged = true;
            }
            break;
        case 62: // F4 - 自动映射
            if (currentWindow == WINDOW_TOUCHPANEL /*&&
                ((usePlayer2 && deviceState2p == DEVICE_OK) || (!usePlayer2 && deviceState1p == DEVICE_OK))*/)
            {
                if (!autoRemapActive)
                {
                    StartAutoRemap();
                }
                else
                {
                    StopAutoRemap(false); // 用户手动取消
                }
                dataChanged = true;
            }
            break;
        case 63: // F5
            if (currentWindow == WINDOW_MAIN)
            {
                // 显示准备消息
                SetCursorPosition(0, 23);
                printf("Preparing Threshold Modification...                                        ");
                
                /*
                if ((usePlayer2 && deviceState2p == DEVICE_OK) || (!usePlayer2 && deviceState1p == DEVICE_OK))
                {
                */
                    ModifyThreshold();
                    dataChanged = true;
                    /*
                }
                    */
                    
                // 清除准备消息
                ClearLine(24);
            }
            break;
        case 64: // F6
            if (currentWindow == WINDOW_MAIN)
            {
                // 显示准备消息
                SetCursorPosition(0, 23);
                printf("Preparing Touch Sheet Remapping...                                        ");
                
                /*
                if ((usePlayer2 && deviceState2p == DEVICE_OK) || (!usePlayer2 && deviceState1p == DEVICE_OK))
                {
                */
                    RemapTouchSheet();
                    dataChanged = true;
                    /*
                }
                    */
                    
                // 清除准备消息
                ClearLine(24);
            }
            break;
        case 65: // F7 - Modify Latency
            if (currentWindow == WINDOW_MAIN)
            {
                // 显示准备消息
                SetCursorPosition(0, 23);
                printf("Preparing Latency Modification...                                          ");
                
                /*
                if ((usePlayer2 && deviceState2p == DEVICE_OK) || (!usePlayer2 && deviceState1p == DEVICE_OK))
                {
                */
                    ModifyLatency();
                    dataChanged = true;
                    /*
                }
                    */
                    
                // 清除准备消息
                ClearLine(23);
            }
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
        system("cls");
    }
    else
    {
        currentWindow = WINDOW_MAIN;
        system("cls");
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
        thresholdReadFailed[i] = false; // 初始化失败标记为false
    }
    
    #ifdef DEBUG
    printf("Thresholds initialized to default value: %d (display: %d/999)\n", 
           THRESHOLD_DEFAULT, threshold_to_display(THRESHOLD_DEFAULT));
    #endif
}

void ModifyThreshold()
{
    // 暂停心跳包发送
    PauseHeartbeat();
    
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
    printf("┃     Please enter the Region ID and its Threshold that you want to change (e.g., E1/750)     ┃\n");
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
    if (len > 0 && input[len - 1] == '\n')
        input[len - 1] = '\0';

    // 解析输入
    char regionType;
    int regionNum = 0, display_threshold;
    bool setAllRegion = false;

    // 尝试解析全区域格式 (如 "A/100")
    if (sscanf(input, "%c/%d", &regionType, &display_threshold) == 2)
    {
        setAllRegion = true;
    }
    // 尝试解析单个区域格式 (如 "A1/100")
    else if (sscanf(input, "%c%d/%d", &regionType, &regionNum, &display_threshold) != 3)
    {
        // 显示错误消息
        ClearLine(promptY + 3);
        SetCursorPosition(0, promptY + 3);
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("Invalid Input Format! ");
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        Sleep(1000); // 短暂显示错误消息

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
        return;
    }

    // 验证区域类型和阈值
    bool validRegion = false;
    if (regionType >= 'A' && regionType <= 'E')
    {
        if (setAllRegion)
        {
            validRegion = true;
        }
        else if ((regionType == 'A' || regionType == 'B' || regionType == 'D' || regionType == 'E') &&
                 regionNum >= 1 && regionNum <= 8)
        {
            validRegion = true;
        }
        else if (regionType == 'C' && (regionNum == 1 || regionNum == 2))
        {
            validRegion = true;
        }
    }

    if (validRegion && display_threshold >= 0 && display_threshold <= 999)
    {
        uint16_t threshold_value = display_to_threshold(display_threshold);
        bool success = false;
        int indices[8]; // 最多8个索引
        int count = 0;

        if (setAllRegion)
        {
            switch (regionType)
            {
            case 'A':
                // A1-A8对应索引0-7
                for (int i = 0; i < 8; i++)
                {
                    indices[count++] = i;
                }
                break;
            case 'B':
                // B1-B8对应索引8-15
                for (int i = 0; i < 8; i++)
                {
                    indices[count++] = 8 + i;
                }
                break;
            case 'C':
                // C1-C2对应索引16-17
                indices[count++] = 16;
                indices[count++] = 17;
                break;
            case 'D':
                // D1-D8对应索引18-25
                for (int i = 0; i < 8; i++)
                {
                    indices[count++] = 18 + i;
                }
                break;
            case 'E':
                // E1-E8对应索引26-33
                for (int i = 0; i < 8; i++)
                {
                    indices[count++] = 26 + i;
                }
                break;
            }

            // 为所有索引设置阈值
            for (int i = 0; i < count; i++)
            {
                touchThreshold[indices[i]] = threshold_value;

                if (deviceState1p == DEVICE_OK)
                {
                    success |= SendThreshold(hPort1, &response1, indices[i]);
                }
                if (deviceState2p == DEVICE_OK)
                {
                    success |= SendThreshold(hPort2, &response2, indices[i]);
                }
            }
        }
        else
        {
            // 处理单个区域设置
            int index = -1;

            switch (regionType)
            {
            case 'A':
                index = regionNum - 1; // Indices 0-7
                break;
            case 'B':
                index = 8 + (regionNum - 1); // Indices 8-15
                break;
            case 'C':
                if (regionNum == 1)
                {
                    index = 16; // Index 16
                }
                else
                {               // regionNum == 2
                    index = 17; // Index 17
                }
                break;
            case 'D':
                index = 18 + (regionNum - 1); // Indices 18-25
                break;
            case 'E':
                index = 26 + (regionNum - 1); // Indices 26-33
                break;
            }

            if (index >= 0 && index < TOUCH_REGIONS)
            {
                touchThreshold[index] = threshold_value;

                if (deviceState1p == DEVICE_OK)
                {
                    success |= SendThreshold(hPort1, &response1, index);
                }
                if (deviceState2p == DEVICE_OK)
                {
                    success |= SendThreshold(hPort2, &response2, index);
                }
            }
        }

        // 显示成功消息
        ClearLine(promptY + 3);
        SetCursorPosition(0, promptY + 3);

        if (success)
        {
            SetConsoleTextAttribute(hConsole, COLOR_GREEN);
            if (setAllRegion)
            {
                printf("All %c Region Thresholds have been updated to %d",
                       regionType, display_threshold, threshold_value);
            }
            else
            {
                printf("Threshold Updated: %c%d = %d/999 (Raw: %d)",
                       regionType, regionNum, display_threshold, threshold_value);
            }
            SetConsoleTextAttribute(hConsole, defaultAttrs);
            Sleep(1500); // 短暂显示成功消息
        }
        else
        {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
            printf("Failed to update threshold! Device did not confirm the change.");
            SetConsoleTextAttribute(hConsole, defaultAttrs);
            Sleep(2000); // 短暂显示错误消息
        }
    }
    else
    {
        // 显示错误消息
        ClearLine(promptY + 3);
        SetCursorPosition(0, promptY + 3);
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        if (!validRegion)
        {
            printf("Invalid Region ID! ");
        }
        else
        {
            printf("Invalid Index or Threshold! The Threshold must be between 0-999.");
        }
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
    
    // 恢复心跳包发送
    ResumeHeartbeat();
}

uint16_t ReadThreshold(HANDLE hPort, serial_packet_t *response, int index)
{
    if (index < 0 || index >= TOUCH_REGIONS || hPort == INVALID_HANDLE_VALUE)
    {
        if (index >= 0 && index < TOUCH_REGIONS)
        {
            thresholdReadFailed[index] = true;
        }
        return THRESHOLD_READ_FAILED;
    }

    // 清空串口缓冲区，避免残留数据干扰
    PurgeComm(hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);
    Sleep(10);

    // 重试机制：最多重试3次
    for (int retry = 0; retry < 3; retry++)
    {
        // 发送读取阈值命令
        package_init(response);
        response->syn = 0xff;
        response->cmd = SERIAL_CMD_READ_MONO_THRESHOLD;
        response->size = 1;
        response->channel = (uint8_t)index;
        serial_writeresp(hPort, response);

        const int READ_ITERATIONS = 50;
        DWORD startTime = GetTickCount();
        uint8_t cmd;

        while ((GetTickCount() - startTime) < 500) // 增加超时时间到500ms
        {
            for (int iteration = 0; iteration < READ_ITERATIONS; iteration++)
            {
                package_init(response);
                cmd = serial_read_cmd(hPort, response);
                if (cmd == SERIAL_CMD_READ_MONO_THRESHOLD)
                {
                    if (response->size >= 3 && response->channel == index)
                    {
                        // 阈值为低字节在前，高字节在后
                        uint16_t value = (response->threshold[1] << 8) | response->threshold[0];
                        // 验证读取到的值是否合理（避免异常值）
                        if (value <= 16384)
                        {
                            thresholdReadFailed[index] = false; // 读取成功
                            return value;
                        }
                    }
                }
            }
            Sleep(2);
        }

        // 重试前等待一段时间
        if (retry < 2)
        {
            Sleep(50);
        }
    }

    // 所有重试都失败，标记为读取失败
    thresholdReadFailed[index] = true;
    return THRESHOLD_READ_FAILED;
}

bool SendThreshold(HANDLE hPort, serial_packet_t *response, int index)
{
    if (index < 0 || index >= TOUCH_REGIONS || hPort == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    uint16_t value = touchThreshold[index];

    // 发送写入阈值命令
    package_init(response);
    response->syn = 0xff;
    response->cmd = SERIAL_CMD_WRITE_MONO_THRESHOLD;
    response->size = 3;
    response->channel = (uint8_t)index;
    // 低字节在前，高字节在后
    response->threshold[0] = value & 0xFF;
    response->threshold[1] = (value >> 8) & 0xFF;
    serial_writeresp(hPort, response);

    const int READ_ITERATIONS = 100;
    DWORD startTime = GetTickCount();
    uint8_t cmd;

    while ((GetTickCount() - startTime) < 1000) // 1000ms超时
    {
        for (int iteration = 0; iteration < READ_ITERATIONS; iteration++)
        {
            package_init(response);
            cmd = serial_read_cmd(hPort, response);
            if (cmd == SERIAL_CMD_WRITE_MONO_THRESHOLD)
            {
                // 验证响应是否为OK
                if (response->size >= 1 && response->data[3] == 0x01)
                {
                    return true;
                }
                return false;
            }
        }
        Sleep(1);
    }

    // 超时或未收到正确响应
    return false;
}

void ReadAllThresholds(HANDLE hPort, serial_packet_t *response)
{
    if (hPort == INVALID_HANDLE_VALUE)
    {
        return;
    }
    
    // 确定是哪个设备
    bool isPlayer1Device = (hPort == hPort1);
    
    // 设置读取状态
    if (isPlayer1Device)
    {
        thresholdReading1p = TRUE;
        thresholdProgress1p = 0;
        thresholdComplete1p = FALSE;
    }
    else
    {
        thresholdReading2p = TRUE;
        thresholdProgress2p = 0;
        thresholdComplete2p = FALSE;
    }
    
    // 暂停心跳包发送
    PauseHeartbeat();

    // 读取所有34个区块的阈值，增加适当的延迟避免设备响应不及时
    for (int i = 0; i < TOUCH_REGIONS; i++)
    {
        touchThreshold[i] = ReadThreshold(hPort, response, i);
        
        // 更新进度
        if (isPlayer1Device)
        {
            thresholdProgress1p = i + 1;
        }
        else
        {
            thresholdProgress2p = i + 1;
        }
        
        // 触发界面刷新并立即更新显示
        dataChanged = true;
        
        // 强制刷新阈值显示部分
        if (currentWindow == WINDOW_MAIN)
        {
            DisplayThresholds();
        }
        
        // 适当增加延迟，给设备更多反应时间
        Sleep(5);
        
        // 可选：显示读取进度（调试用）
        #ifdef DEBUG
        printf("Reading threshold for region %d: %d\n", i, touchThreshold[i]);
        #endif
    }

    // 完成阈值读取
    if (isPlayer1Device)
    {
        thresholdReading1p = FALSE;
        thresholdComplete1p = TRUE;
    }
    else
    {
        thresholdReading2p = FALSE;
        thresholdComplete2p = TRUE;
    }
    
    // 触发最终界面刷新
    dataChanged = true;

    // 暂停心跳包发送
    ResumeHeartbeat();
}

bool ReadTouchSheet(HANDLE hPort, serial_packet_t *response)
{
    if (hPort == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    // 暂停心跳包发送
    // PauseHeartbeat();

    // 清空串口缓冲区，避免残留数据干扰
    PurgeComm(hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);
    Sleep(10); // 给设备一点反应时间

    // 发送读取触摸映射表命令
    package_init(response);
    response->syn = 0xff;
    response->cmd = SERIAL_CMD_READ_TOUCH_SHEET;
    response->size = 0x01;
    response->data[3] = 0x00;
    serial_writeresp(hPort, response);

    // 接收数据变量
    uint8_t buffer[TOUCH_REGIONS + 4] = {0}; // +4用于存储头部和校验和
    int bytesReceived = 0;
    bool headerReceived = false;
    uint8_t cmd = 0;

    // 循环读取直到超时或收到完整数据
    DWORD startTime = GetTickCount();
    DWORD currentTime;

    do
    {
        currentTime = GetTickCount();

        // 每次先尝试读取一个命令包
        if (!headerReceived)
        {
            package_init(response);
            cmd = serial_read_cmd(hPort, response);

            // 如果收到正确的命令码和数据长度
            if (cmd == SERIAL_CMD_READ_TOUCH_SHEET && response->size == 0x22)
            {
                memcpy(touchSheet, response->touch_sheet, TOUCH_REGIONS);
                return true;
            }
            
            // 如果收到心跳或其他命令，忽略并继续
            if (cmd == SERIAL_CMD_HEART_BEAT || cmd == SERIAL_CMD_AUTO_SCAN)
            {
                continue; // 跳过这些命令，继续等待正确的响应
            }

            // 尝试逐字节接收
            for (int i = 0; i < 200 && bytesReceived < TOUCH_REGIONS + 4; i++)
            {
                uint8_t byte;
                DWORD bytesRead;

                if (ReadFile(hPort, &byte, 1, &bytesRead, NULL) && bytesRead == 1)
                {
                    // 处理数据帧头部
                    if (bytesReceived == 0 && byte == 0xFF)
                    {
                        buffer[bytesReceived++] = byte;
                    }
                    else if (bytesReceived == 1 && byte == SERIAL_CMD_READ_TOUCH_SHEET)
                    {
                        buffer[bytesReceived++] = byte;
                    }
                    else if (bytesReceived == 2 && byte == 0x22)
                    {
                        buffer[bytesReceived++] = byte;
                        headerReceived = true;
                    }
                    // 如果已经接收到头部，继续接收数据体
                    else if (headerReceived && bytesReceived < TOUCH_REGIONS + 4)
                    {
                        buffer[bytesReceived++] = byte;

                        // 如果已接收到全部数据及校验和
                        if (bytesReceived == TOUCH_REGIONS + 4)
                        {
                            memcpy(touchSheet, &buffer[3], TOUCH_REGIONS);
                            return true;
                        }
                    }
                    // 重置如果接收到的不是期望的字节
                    else if (bytesReceived < 3)
                    {
                        bytesReceived = 0;
                    }
                }
                else
                {
                    break; // 无数据可读或读取错误，跳出内循环
                }
            }
        }

        Sleep(10); // 增加延迟，给设备更多响应时间

    } while (currentTime - startTime < 2500); // 增加超时时间到2.5秒

    if (headerReceived && bytesReceived >= TOUCH_REGIONS + 3)
    {
        memcpy(touchSheet, &buffer[3], TOUCH_REGIONS);
        return true;
            
        // 恢复心跳包发送
        // ResumeHeartbeat();
    }

    // 恢复心跳包发送
    ResumeHeartbeat();
    return false; // 超时或未接收到足够数据
}

bool WriteTouchSheet(HANDLE hPort, serial_packet_t *response)
{
    if (hPort == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    // 清空串口缓冲区
    PurgeComm(hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);
    Sleep(50);

    // 发送写入触摸映射表命令
    package_init(response);
    response->syn = 0xff;
    response->cmd = SERIAL_CMD_WRITE_TOUCH_SHEET;
    response->size = 0x22;
    memcpy(response->touch_sheet, touchSheet, TOUCH_REGIONS);
    serial_writeresp(hPort, response);

    const int READ_ITERATIONS = 50; // 减少单次循环次数
    DWORD startTime = GetTickCount();
    uint8_t cmd;

    while ((GetTickCount() - startTime) < 2000) // 增加超时时间到2秒
    {
        for (int iteration = 0; iteration < READ_ITERATIONS; iteration++)
        {
            package_init(response);
            cmd = serial_read_cmd(hPort, response);
            if (cmd == SERIAL_CMD_WRITE_TOUCH_SHEET)
            {
                // 验证响应是否为OK
                if (response->size >= 1 && response->data[3] == 0x01)
                {
                    Sleep(100); // 写入成功后等待设备保存
                    return true;
                }
                return false;
            }
            // 忽略心跳和扫描命令
            if (cmd == SERIAL_CMD_HEART_BEAT || cmd == SERIAL_CMD_AUTO_SCAN)
            {
                continue;
            }
        }
        Sleep(10); // 增加延迟
    }

    // 超时或未收到正确响应
    return false;
}

void RemapTouchSheet()
{
    // 暂停心跳包发送
    PauseHeartbeat();
    
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    // 首先给设备一点缓冲时间
    Sleep(200);

    // 首先读取当前映射
    bool mappingRead = false;

    if (deviceState1p == DEVICE_OK)
    {
        mappingRead = ReadTouchSheet(hPort1, &response1);
    }
    else if (deviceState2p == DEVICE_OK)
    {
        mappingRead = ReadTouchSheet(hPort2, &response2);
    }

    if (!mappingRead)
    {
        // 显示错误消息
        SetCursorPosition(0, 23);
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("Failed to read current touch mapping! Cannot proceed with remapping.");
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        Sleep(1500);
        ClearLine(23);
        return;
    }

    // 保存当前位置，以便恢复
    int currentY = csbi.dwCursorPosition.Y;

    // 在底部显示提示信息
    int promptY = 23;

    // 清除提示区域
    for (int i = promptY; i < promptY + 6; i++)
    {
        ClearLine(i);
    }

    // 创建索引到区块标识符的映射
    const char *blockLabels[TOUCH_REGIONS] = {
        "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", // 0-7
        "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", // 8-15
        "C1", "C2",                                     // 16-17
        "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", // 18-25
        "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8"  // 26-33
    };

    bool duplicate[TOUCH_REGIONS] = {0};
    // 检查是否有多个区域映射到同一个通道
    for (int channel = 0; channel < TOUCH_REGIONS; channel++)
    {
        int count = 0;
        for (int region = 0; region < TOUCH_REGIONS; region++)
        {
            if (touchSheet[region] == channel)
            {
                count++;
            }
        }
        if (count > 1)
        {
            duplicate[channel] = true;
        }
    }

    // 显示当前映射
    SetCursorPosition(0, promptY);
    printf("┌───────────────────────────────────── Curva Touch Sheet Mapping ─────────────────────────────────────┐");
    SetCursorPosition(0, promptY + 1);
    printf("│0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33│");
    SetCursorPosition(0, promptY + 2);
    printf("│o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o │");
    SetCursorPosition(0, promptY + 3);
    printf("│");
    for (int i = 0; i < TOUCH_REGIONS - 1; i++)
    {
        // 从touchSheet中反向查找哪个区域映射到通道i
        int mappedRegion = -1;
        for (int j = 0; j < TOUCH_REGIONS; j++)
        {
            if (touchSheet[j] == i)
            {
                mappedRegion = j;
                break;
            }
        }
        
        if (mappedRegion >= 0 && mappedRegion < TOUCH_REGIONS)
        {
            if (duplicate[i])
            {
                SetConsoleTextAttribute(hConsole, COLOR_RED);
            }
            printf("%-2s ", blockLabels[mappedRegion]);
            if (duplicate[i])
            {
                SetConsoleTextAttribute(hConsole, defaultAttrs);
            }
        }
        else
        {
            printf("?? "); // 无效映射显示为 "?? "
        }
    }
    // 单独处理最后一个标签，不带空格
    int lastIndex = TOUCH_REGIONS - 1;
    int lastMappedRegion = -1;
    for (int j = 0; j < TOUCH_REGIONS; j++)
    {
        if (touchSheet[j] == lastIndex)
        {
            lastMappedRegion = j;
            break;
        }
    }
    if (lastMappedRegion >= 0 && lastMappedRegion < TOUCH_REGIONS)
    {
        printf("%-2s", blockLabels[lastMappedRegion]); // 打印最后一个标签，不加空格
    }
    else
    {
        printf("??");
    }
    printf("│");
    SetCursorPosition(0, promptY + 4);
    printf("└─────────────────────────────────────────────────────────────────────────────────────────────────────┘");
    SetCursorPosition(0, promptY + 5);
    printf("Please Enter The New Mapping (e.g., A1/5): ");

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
    if (len > 0 && input[len - 1] == '\n')
        input[len - 1] = '\0';

    // 如果输入为空，取消操作
    if (strlen(input) == 0)
    {
        cursorInfo.bVisible = FALSE;
        SetConsoleCursorInfo(hConsole, &cursorInfo);

        for (int i = promptY; i < promptY + 6; i++)
        {
            ClearLine(i);
        }
        return;
    }

    // 解析输入
    char regionType;
    int regionNum, channelValue;
    if (sscanf(input, "%c%d/%d", &regionType, &regionNum, &channelValue) == 3)
    {
        // 计算索引
        int index = -1;

        // 检查输入的区域类型和编号是否有效
        bool validRegion = false;
        if (regionType >= 'A' && regionType <= 'E')
        {
            if ((regionType == 'A' || regionType == 'B' || regionType == 'D' || regionType == 'E') && regionNum >= 1 && regionNum <= 8)
            {
                validRegion = true;
            }
            else if (regionType == 'C' && (regionNum == 1 || regionNum == 2))
            {
                validRegion = true;
            }
        }

        if (validRegion && channelValue >= 0 && channelValue <= 33)
        {
            // 根据触摸分区映射表的排列顺序计算索引
            switch (regionType)
            {
            case 'A':
                index = regionNum - 1; // Indices 0-7
                break;
            case 'B':
                index = 8 + (regionNum - 1); // Indices 8-15
                break;
            case 'C':
                if (regionNum == 1)
                {
                    index = 16; // Index 16
                }
                else
                {               // regionNum == 2
                    index = 17; // Index 17
                }
                break;
            case 'D':
                index = 18 + (regionNum - 1); // Indices 18-25
                break;
            case 'E':
                index = 26 + (regionNum - 1); // Indices 26-33
                break;
            }

            // 更新映射表并发送到设备
            if (index >= 0 && index < TOUCH_REGIONS)
            {
                touchSheet[index] = (uint8_t)channelValue;

                bool success1p = false, success2p = false;

                if (deviceState1p == DEVICE_OK)
                {
                    success1p = WriteTouchSheet(hPort1, &response1);
                }
                if (deviceState2p == DEVICE_OK)
                {
                    success2p = WriteTouchSheet(hPort2, &response2);
                }

                // 给设备足够时间保存映射
                if (success1p || success2p)
                {
                    Sleep(300);
                }

                // 显示成功或失败消息
                ClearLine(promptY + 5);
                SetCursorPosition(0, promptY + 5);

                if (success1p || success2p)
                {
                    SetConsoleTextAttribute(hConsole, COLOR_GREEN);
                    printf("Mapping Updated: %c%d ← Channel %d 1P:%s 2P:%s",
                           regionType, regionNum, channelValue,
                           success1p ? "OK" : "FAIL",
                           (deviceState2p == DEVICE_OK) ? (success2p ? "OK" : "FAIL") : "N/A");
                    SetConsoleTextAttribute(hConsole, defaultAttrs);

                    // Why I need these here?
                    if (success1p && deviceState1p == DEVICE_OK)
                    {
                        serial_heart_beat(hPort1, &response1);
                    }
                    if (success2p && deviceState2p == DEVICE_OK)
                    {
                        serial_heart_beat(hPort2, &response2);
                    }

                    Sleep(1500); // 短暂显示成功消息
                }
                else
                {
                    SetConsoleTextAttribute(hConsole, COLOR_RED);
                    printf("Failed to update mapping! Device did not confirm the change.");
                    SetConsoleTextAttribute(hConsole, defaultAttrs);
                    Sleep(2000); // 短暂显示错误消息
                }
            }
        }
        else
        {
            // 显示错误消息
            ClearLine(promptY + 5);
            SetCursorPosition(0, promptY + 5);
            SetConsoleTextAttribute(hConsole, COLOR_RED);
            printf("Invalid Region ID or Channel Value! Channel must be between 0-33.");
            SetConsoleTextAttribute(hConsole, defaultAttrs);
            Sleep(2000); // 短暂显示错误消息
        }
    }
    else
    {
        // 显示错误消息
        ClearLine(promptY + 5);
        SetCursorPosition(0, promptY + 5);
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("Invalid Input Format! Expected format: [region]/[channel] (e.g., A1/5)");
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        Sleep(2000); // 短暂显示错误消息
    }

    // 隐藏光标
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    for (int i = promptY; i < promptY + 8; i++)
    {
        ClearLine(i);
        // 使用一个超长空白字符串确保行完全清除
        SetCursorPosition(0, i);
        printf("                                                                                                       ");
    }

    // 强制更新显示
    dataChanged = true;
    
    // 恢复心跳包发送
    ResumeHeartbeat();
}

// 连接Kobato设备并初始化
void ConnectKobato()
{
    Sleep(200);

    // 尝试通过VID/PID获取Kobato设备的COM端口
    char* foundPortKobato = GetSerialPortByVidPid(Vid_Kobato, Pid_Kobato);
    if (foundPortKobato[0] == 0)
    {
        // 如果找不到设备，状态保持为WAIT
        deviceStateKobato = DEVICE_WAIT;
        return;
    }

    // 直接使用找到的端口名，添加Windows命名空间前缀
    snprintf(comPortKobato, 12, "\\\\.\\%s", foundPortKobato);

    // 重试连接几次
    HANDLE tempPort = INVALID_HANDLE_VALUE;
    bool connected = false;

    for (int attempt = 0; attempt < 3 && !connected; attempt++)
    {
        // 打开串口
        tempPort = CreateFile(comPortKobato, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (tempPort != INVALID_HANDLE_VALUE)
        {
            connected = true;
            break;
        }
        Sleep(100);
    }

    if (!connected)
    {
        deviceStateKobato = DEVICE_WAIT;
        return;
    }

    hPortKobato = tempPort;

    // 找到了设备并打开了串口，从这一点开始如果通信失败则标记为FAIL而非WAIT

    // 先尝试高波特率
    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(hPortKobato, &dcb))
    {
        CloseHandle(hPortKobato);
        hPortKobato = INVALID_HANDLE_VALUE;
        deviceStateKobato = DEVICE_FAIL; // 修改：通信失败标记为FAIL
        return;
    }

    // 设置超时
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 1000;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    if (!SetCommTimeouts(hPortKobato, &timeouts)) // 添加错误检查
    {
        CloseHandle(hPortKobato);
        hPortKobato = INVALID_HANDLE_VALUE;
        deviceStateKobato = DEVICE_FAIL;
        return;
    }

    // 添加复位序列 - 重要的修改
    uint8_t mode_rst_cmd[30];
    memset(mode_rst_cmd, 0xaf, sizeof(mode_rst_cmd));

    // 先用低波特率发送复位命令
    dcb.BaudRate = 38400;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    if (!SetCommState(hPortKobato, &dcb)) // 添加错误检查
    {
        CloseHandle(hPortKobato);
        hPortKobato = INVALID_HANDLE_VALUE;
        deviceStateKobato = DEVICE_FAIL;
        return;
    }

    DWORD bytesWritten;
    if (!WriteFile(hPortKobato, mode_rst_cmd, sizeof(mode_rst_cmd), &bytesWritten, NULL) ||
        bytesWritten != sizeof(mode_rst_cmd)) // 添加错误检查
    {
        CloseHandle(hPortKobato);
        hPortKobato = INVALID_HANDLE_VALUE;
        deviceStateKobato = DEVICE_FAIL;
        return;
    }
    Sleep(10);

    // 再用高波特率发送复位命令
    dcb.BaudRate = 115200;
    if (!SetCommState(hPortKobato, &dcb)) // 添加错误检查
    {
        CloseHandle(hPortKobato);
        hPortKobato = INVALID_HANDLE_VALUE;
        deviceStateKobato = DEVICE_FAIL;
        return;
    }

    if (!WriteFile(hPortKobato, mode_rst_cmd, sizeof(mode_rst_cmd), &bytesWritten, NULL) ||
        bytesWritten != sizeof(mode_rst_cmd)) // 添加错误检查
    {
        CloseHandle(hPortKobato);
        hPortKobato = INVALID_HANDLE_VALUE;
        deviceStateKobato = DEVICE_FAIL;
        return;
    }
    Sleep(1000); // 更长的等待时间

    // 尝试以高波特率读取设备信息
    if (!ReadKobatoStatus())
    {
        // 尝试低波特率
        dcb.BaudRate = 38400;
        if (!SetCommState(hPortKobato, &dcb))
        {
            CloseHandle(hPortKobato);
            hPortKobato = INVALID_HANDLE_VALUE;
            deviceStateKobato = DEVICE_FAIL; // 修改：通信失败标记为FAIL
            return;
        }

        if (!ReadKobatoStatus())
        {
            CloseHandle(hPortKobato);
            hPortKobato = INVALID_HANDLE_VALUE;
            deviceStateKobato = DEVICE_FAIL; // 修改：通信失败标记为FAIL
            return;
        }
    }

    deviceStateKobato = DEVICE_OK;
}

// 读取Kobato设备状态
bool ReadKobatoStatus()
{
    if (hPortKobato == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    // 清空接收缓冲区
    PurgeComm(hPortKobato, PURGE_RXCLEAR);

    // 发送读取EEPROM命令
    uint8_t send_buffer[] = {0xE0, 0x06, 0x00, 0x00, 0xF6, 0x00, 0x00, 0xFC};
    DWORD bytesWritten;

    if (!WriteFile(hPortKobato, send_buffer, sizeof(send_buffer), &bytesWritten, NULL) || bytesWritten != sizeof(send_buffer))
    {
        return false;
    }

    Sleep(1000); // 给设备足够的响应时间

    // 接收缓冲区扩大以确保能够接收完整数据包
    uint8_t recv_buffer[256] = {0};
    DWORD bytesRead = 0;

    if (!ReadFile(hPortKobato, recv_buffer, sizeof(recv_buffer), &bytesRead, NULL))
    {
        return false;
    }

// 调试输出接收到的字节
#ifdef DEBUG
    for (DWORD i = 0; i < bytesRead; i++)
    {
        printf("%02X ", recv_buffer[i]);
    }
    printf("\n");
#endif

    // 实现更精确的数据包解析
    for (DWORD i = 0; i < bytesRead - 8; i++)
    {
        // 寻找命令包头0xE0
        if (recv_buffer[i] == 0xE0)
        {
            // 检查是否有足够的字节用于解析
            if (i + 10 < bytesRead)
            {
                // 第7个和第8个字节包含EEPROM数据
                uint8_t setting_byte = recv_buffer[i + 7]; // 主设置字节
                uint8_t led_bright = recv_buffer[i + 8];   // LED亮度

                // 正确解析设备设置
                kobatoHighBaud = (setting_byte & 0x02) != 0;       // 位1: 高波特率
                kobatoLedEnabled = (setting_byte & 0x04) != 0;     // 位2: LED启用
                kobatoExtendEnabled = (setting_byte & 0x10) != 0;  // 位4: 扩展功能
                kobatoReflectEnabled = (setting_byte & 0x08) != 0; // 位3: 反射功能
                kobatoLedBrightness = led_bright;                  // LED亮度值

                return true;
            }
        }
    }

    return false;
}

// 尝试重新连接Kobato设备
void ReconnectKobato()
{
    static DWORD lastReconnectTime = 0;
    DWORD currentTime = GetTickCount();

    // 每2秒尝试重连一次
    if (currentTime - lastReconnectTime < 2000)
    {
        return;
    }

    lastReconnectTime = currentTime;

    // 关闭之前的连接
    if (hPortKobato != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hPortKobato);
        hPortKobato = INVALID_HANDLE_VALUE;
    }

    // 设置为WAIT状态，表示正在尝试连接
    deviceStateKobato = DEVICE_WAIT;
    dataChanged = true;

    // 尝试连接
    ConnectKobato();
}

void SaveSettings()
{
    // 暂停心跳包发送
    PauseHeartbeat();
    
    FILE *file = fopen("curva.ini", "wb");
    if (file == NULL)
    {
        // 保存失败，显示错误信息
        int promptY = 23;
        for (int i = promptY; i < promptY + 3; i++)
        {
            ClearLine(i);
        }

        SetCursorPosition(0, promptY);
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("Failed to save settings to curva.ini file!");
        SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
        Sleep(2000);
        return;
    }

    // 创建区块标识符映射表
    const char *blockLabels[TOUCH_REGIONS] = {
        "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", // 0-7
        "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", // 8-15
        "C1", "C2",                                     // 16-17
        "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", // 18-25
        "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8"  // 26-33
    };

    // 写入文件标识和版本
    fprintf(file, "[Curva Settings]\n");
    fprintf(file, "Version=%s\n", VERSION);
    fprintf(file, "Date=%s\n\n", __DATE__);

    // 写入阈值数据
    fprintf(file, "[Thresholds]\n");
    for (int i = 0; i < TOUCH_REGIONS; i++)
    {
        uint16_t displayValue = threshold_to_display(touchThreshold[i]);
        fprintf(file, "%s=%u\n", blockLabels[i], displayValue);
    }

    // 写入触摸映射数据
    fprintf(file, "\n[TouchSheet]\n");
    for (int channel = 0; channel < TOUCH_REGIONS; channel++)
    {
        // 查找映射到此通道的区域
        int mappedRegion = -1;
        for (int region = 0; region < TOUCH_REGIONS; region++)
        {
            if (touchSheet[region] == channel)
            {
                mappedRegion = region;
                break;
            }
        }
        
        if (mappedRegion >= 0 && mappedRegion < TOUCH_REGIONS)
        {
            fprintf(file, "Channel%d=%s\n", channel, blockLabels[mappedRegion]);
        }
        else
        {
            fprintf(file, "Channel%d=INVALID\n", channel);
        }
    }

    // 新增：写入延迟设置数据
    fprintf(file, "\n[Latency]\n");
    fprintf(file, "TouchLatency=%u\n", touchLatency * 2);   // 保存实际毫秒值
    fprintf(file, "ButtonLatency=%u\n", buttonLatency * 2); // 保存实际毫秒值

    fclose(file);

    // 显示成功消息
    int promptY = 23;
    for (int i = promptY; i < promptY + 3; i++)
    {
        ClearLine(i);
    }

    SetCursorPosition(0, promptY);
    SetConsoleTextAttribute(hConsole, COLOR_GREEN);
    printf("Settings successfully saved to curva.ini file!");
    SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
    Sleep(2000);

    // 清除消息
    for (int i = promptY; i < promptY + 3; i++)
    {
        ClearLine(i);
    }
    
    // 恢复心跳包发送
    ResumeHeartbeat();
}

void LoadSettings()
{
    // 暂停心跳包发送
    PauseHeartbeat();
    
    FILE *file = fopen("curva.ini", "rb");
    if (file == NULL)
    {
        // 加载失败，显示错误信息
        int promptY = 23;
        for (int i = promptY; i < promptY + 3; i++)
        {
            ClearLine(i);
        }

        SetCursorPosition(0, promptY);
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("Failed to load settings: curva.ini file not found!");
        SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
        Sleep(2000);
        return;
    }

    // 创建区块标识符映射表
    const char *blockLabels[TOUCH_REGIONS] = {
        "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", // 0-7
        "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", // 8-15
        "C1", "C2",                                     // 16-17
        "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", // 18-25
        "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8"  // 26-33
    };

    char line[256];
    char section[32] = "";
    bool thresholdsChanged = false;
    bool touchSheetChanged = false;
    bool latencyChanged = false;

    // 临时存储读取的延迟值
    unsigned int loadedTouchLatency = 0;
    unsigned int loadedButtonLatency = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        line[strcspn(line, "\r\n")] = 0; // 移除换行符

        // 跳过空行和注释行
        if (strlen(line) <= 1 || line[0] == ';' || line[0] == '#')
            continue;

        // 判断是否为区块标识
        if (line[0] == '[' && line[strlen(line) - 1] == ']')
        {
            strncpy(section, line + 1, strlen(line) - 2);
            section[strlen(line) - 2] = '\0';
            continue;
        }

        // 处理不同区块的数据
        if (strcmp(section, "Thresholds") == 0)
        {
            // A1=500 格式的阈值数据（0-999范围）
            char key[10];
            unsigned int displayValue;
            if (sscanf(line, "%[^=]=%u", key, &displayValue) == 2)
            {
                // 查找区域索引
                for (int i = 0; i < TOUCH_REGIONS; i++)
                {
                    if (strcmp(key, blockLabels[i]) == 0 && displayValue <= 999)
                    {
                        // 将0-999的显示值转换回0-65535的实际值
                        touchThreshold[i] = display_to_threshold((uint16_t)displayValue);
                        thresholdsChanged = true;
                        break;
                    }
                }
            }
        }
        else if (strcmp(section, "TouchSheet") == 0)
        {
            char key[20];
            char blockId[10];
            if (sscanf(line, "%[^=]=%s", key, blockId) == 2)
            {
                int channel;
                if (sscanf(key, "Channel%d", &channel) == 1 && channel >= 0 && channel < TOUCH_REGIONS)
                {
                    // 查找区块标识对应的索引
                    for (int i = 0; i < TOUCH_REGIONS; i++)
                    {
                        if (strcmp(blockId, blockLabels[i]) == 0)
                        {
                            touchSheet[i] = channel;
                            touchSheetChanged = true;
                            break;
                        }
                    }
                }
            }
        }
        // 处理延迟设置部分
        else if (strcmp(section, "Latency") == 0)
        {
            char key[20];
            unsigned int value;
            if (sscanf(line, "%[^=]=%u", key, &value) == 2)
            {
                if (strcmp(key, "TouchLatency") == 0 && value <= 18)
                {
                    loadedTouchLatency = value;
                    latencyChanged = true;
                }
                else if (strcmp(key, "ButtonLatency") == 0 && value <= 18)
                {
                    loadedButtonLatency = value;
                    latencyChanged = true;
                }
            }
        }
    }

    fclose(file);

    // 先应用触摸映射，再应用阈值
    bool thresholdsSuccess = false;
    bool touchSheetSuccess = false;
    bool latencySuccess = false;

    // 如果触摸映射已更改，先应用到设备
    if (touchSheetChanged)
    {
        if (deviceState1p == DEVICE_OK)
        {
            touchSheetSuccess = WriteTouchSheet(hPort1, &response1);
        }

        if (deviceState2p == DEVICE_OK && !touchSheetSuccess)
        {
            touchSheetSuccess = WriteTouchSheet(hPort2, &response2);
        }
    }

    // 如果阈值数据已更改，再应用到设备
    if (thresholdsChanged)
    {
        if (deviceState1p == DEVICE_OK)
        {
            // 应用阈值到1P设备
            for (int i = 0; i < TOUCH_REGIONS; i++)
            {
                SendThreshold(hPort1, &response1, i);
                Sleep(10); // 小延迟以避免通信问题
            }
            thresholdsSuccess = true;
        }

        if (deviceState2p == DEVICE_OK)
        {
            // 应用阈值到2P设备
            for (int i = 0; i < TOUCH_REGIONS; i++)
            {
                SendThreshold(hPort2, &response2, i);
                Sleep(10);
            }
            thresholdsSuccess = true;
        }
    }

    // 如果延迟设置已更改，应用到设备
    if (latencyChanged)
    {
        // 将毫秒值转换回设备内部值
        uint8_t touchLatencyValue = (uint8_t)(loadedTouchLatency / 2);
        uint8_t buttonLatencyValue = (uint8_t)(loadedButtonLatency / 2);

        if (deviceState1p == DEVICE_OK)
        {
            bool touchSuccess = WriteLatencySettings(hPort1, &response1, 0, touchLatencyValue);
            bool buttonSuccess = WriteLatencySettings(hPort1, &response1, 1, buttonLatencyValue);
            latencySuccess = touchSuccess && buttonSuccess;
        }
        else if (deviceState2p == DEVICE_OK)
        {
            bool touchSuccess = WriteLatencySettings(hPort2, &response2, 0, touchLatencyValue);
            bool buttonSuccess = WriteLatencySettings(hPort2, &response2, 1, buttonLatencyValue);
            latencySuccess = touchSuccess && buttonSuccess;
        }
    }

    // 显示结果信息
    int promptY = 23;
    for (int i = promptY; i < promptY + 3; i++)
    {
        ClearLine(i);
    }

    SetCursorPosition(0, promptY);
    if (thresholdsChanged || touchSheetChanged || latencyChanged)
    {
        if ((thresholdsChanged && thresholdsSuccess) ||
            (touchSheetChanged && touchSheetSuccess) ||
            (latencyChanged && latencySuccess))
        {
            SetConsoleTextAttribute(hConsole, COLOR_GREEN);
            printf("Settings loaded from curva.ini successfully!");

            if (deviceState1p == DEVICE_OK)
            {
                serial_heart_beat(hPort1, &response1);
            }
            if (deviceState2p == DEVICE_OK)
            {
                serial_heart_beat(hPort2, &response2);
            }
        }
        else
        {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
            printf("Settings loaded but could not be applied to device (device not connected)");
        }
    }
    else
    {
        SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
        printf("No valid settings found in curva.ini");
    }
    SetConsoleTextAttribute(hConsole, COLOR_DEFAULT);
    Sleep(2000);

    // 清除消息
    for (int i = promptY; i < promptY + 3; i++)
    {
        ClearLine(i);
    }
    
    // 恢复心跳包发送
    ResumeHeartbeat();
}

void ReadLatencySettings(HANDLE hPort, serial_packet_t *response)
{
    if (hPort == INVALID_HANDLE_VALUE)
    {
        return;
    }

    // 读取触摸延迟 - 发送命令: 0xff,0x12,0x01,0x00,0x12
    package_init(response);
    response->syn = 0xff;
    response->cmd = SERIAL_CMD_READ_DELAY_SETTING; // 0x12
    response->size = 0x01;                         // 只有一个字节的payload
    response->data[3] = 0x00;                      // 触摸类型(0)
    serial_writeresp(hPort, response);

    // 等待响应 - 期望: 0xff,0x12,0x02,0x00,0x00,0x13
    const int READ_ITERATIONS = 100;
    DWORD startTime = GetTickCount();
    uint8_t cmd;

    while ((GetTickCount() - startTime) < 500) // 500ms超时，与ReadThreshold一致
    {
        for (int iteration = 0; iteration < READ_ITERATIONS; iteration++)
        {
            package_init(response);
            cmd = serial_read_cmd(hPort, response);
            if (cmd == SERIAL_CMD_READ_DELAY_SETTING && response->size >= 2)
            {
                if (response->data[3] == 0x00) // 确认是触摸延迟
                {
                    touchLatency = response->data[4]; // 直接获取延迟值
                    goto read_button_latency;         // 直接跳到读取按键延迟
                }
            }
        }
        Sleep(1); // 与threshold函数一致，每轮内部循环后休眠1ms
    }

read_button_latency:
    // 读取按键延迟 - 发送命令: 0xff,0x12,0x01,0x01,0x13
    package_init(response);
    response->syn = 0xff;
    response->cmd = SERIAL_CMD_READ_DELAY_SETTING; // 0x12
    response->size = 0x01;                         // 只有一个字节的payload
    response->data[3] = 0x01;                      // 按键类型(1)
    serial_writeresp(hPort, response);

    // 等待响应 - 期望: 0xff,0x12,0x02,0x01,0xXX,0xYY
    startTime = GetTickCount();

    while ((GetTickCount() - startTime) < 500) // 500ms超时，与ReadThreshold一致
    {
        for (int iteration = 0; iteration < READ_ITERATIONS; iteration++)
        {
            package_init(response);
            cmd = serial_read_cmd(hPort, response);
            if (cmd == SERIAL_CMD_READ_DELAY_SETTING && response->size >= 2)
            {
                if (response->data[3] == 0x01) // 确认是按键延迟
                {
                    buttonLatency = response->data[4]; // 直接获取延迟值
                    latencyRead = true;
                    return;
                }
            }
        }
        Sleep(1); // 与threshold函数一致，每轮内部循环后休眠1ms
    }
}

// 写入延迟设置
bool WriteLatencySettings(HANDLE hPort, serial_packet_t *response, uint8_t type, uint8_t value)
{
    if (hPort == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    // 发送写入延迟命令
    package_init(response);
    response->syn = 0xff;
    response->cmd = SERIAL_CMD_WRITE_DELAY_SETTING; // 0x13
    response->size = 0x02;                          // 两个字节的payload
    response->data[3] = type;                       // 0-触摸, 1-按键
    response->data[4] = value;                      // 延迟值 (0-9)
    serial_writeresp(hPort, response);

    const int READ_ITERATIONS = 100;
    DWORD startTime = GetTickCount();
    uint8_t cmd;

    while ((GetTickCount() - startTime) < 1000)
    {
        for (int iteration = 0; iteration < READ_ITERATIONS; iteration++)
        {
            package_init(response);
            cmd = serial_read_cmd(hPort, response);
            if (cmd == SERIAL_CMD_WRITE_DELAY_SETTING)
            {
                // 验证响应是否为OK
                if (response->size >= 1 && response->data[3] == type)
                {
                    // 更新本地存储的延迟值
                    if (type == 0)
                        touchLatency = value;
                    else
                        buttonLatency = value;
                    return true;
                }
                return false;
            }
        }
        Sleep(1); // 与threshold函数一致
    }

    return false; // 超时
}

// 修改延迟设置函数
void ModifyLatency()
{
    // 暂停心跳包发送
    PauseHeartbeat();
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    // 如果尚未读取延迟设置，先读取
    if (!latencyRead)
    {
        if (deviceState1p == DEVICE_OK)
        {
            ReadLatencySettings(hPort1, &response1);
        }
        else if (deviceState2p == DEVICE_OK)
        {
            ReadLatencySettings(hPort2, &response2);
        }
        else
        {
            // 设备未连接，无法读取
            return;
        }
    }

    // 在底部显示提示信息
    int promptY = 23;

    // 清除提示区域
    for (int i = promptY; i < promptY + 5; i++)
    {
        ClearLine(i);
    }

    // 显示提示信息 - 每单位对应2ms，最大值为9（对应18ms）
    SetCursorPosition(0, promptY);
    printf("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓");
    SetCursorPosition(0, promptY + 1);
    printf("┃                        Latency Settings (in milliseconds, MAX 18)                           ┃");
    SetCursorPosition(0, promptY + 2);
    printf("┃  Current settings:   Touch latency: %-2d ms    Button latency: %-2d ms                          ┃",
           touchLatency * 2, buttonLatency * 2);
    SetCursorPosition(0, promptY + 3);
    printf("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛");
    SetCursorPosition(0, promptY + 4);
    printf("Enter type (T:Touch, B:Button) and value (e.g., T/10 for 10ms touch latency): ");

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
    if (len > 0 && input[len - 1] == '\n')
        input[len - 1] = '\0';

    // 解析输入
    char type;
    int value;
    bool success = false;

    if (sscanf(input, "%c/%d", &type, &value) == 2)
    {
        type = toupper(type);
        // 验证输入的范围 - 新的范围为0-18ms (0-9个包)
        if ((type == 'T' || type == 'B') && value >= 0 && value <= 18)
        {
            uint8_t typeCode = (type == 'T') ? 0 : 1;
            // 将用户输入的毫秒值除以2，转换为设备内部值（包数量，范围0-9）
            uint8_t deviceValue = (uint8_t)(value / 2);

            // 写入延迟设置
            if (deviceState1p == DEVICE_OK)
            {
                success = WriteLatencySettings(hPort1, &response1, typeCode, deviceValue);
            }
            else if (deviceState2p == DEVICE_OK)
            {
                success = WriteLatencySettings(hPort2, &response2, typeCode, deviceValue);
            }
        }
    }

    // 清除用户输入行
    ClearLine(promptY + 4);
    SetCursorPosition(0, promptY + 4);

    // 显示结果
    if (success)
    {
        SetConsoleTextAttribute(hConsole, COLOR_GREEN);
        printf("Latency setting updated successfully! Touch: %d ms, Button: %d ms",
               touchLatency * 2, buttonLatency * 2);
    }
    else
    {
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        if ((type == 'T' || type == 'B') && (value < 0 || value > 18))
        {
            printf("Failed to update latency setting: Please enter a value between 0 and 18 ms");
        }
        else if (type != 'T' && type != 'B')
        {
            printf("Failed to update latency setting: Invalid type. Please use T (Touch) or B (Button).");
        }
        else
        {
            printf("Failed to update latency setting: Device did not confirm the change.");
        }
    }
    SetConsoleTextAttribute(hConsole, defaultAttrs);

    // 等待用户查看
    Sleep(2000);

    // 恢复光标和清理屏幕
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    for (int i = promptY; i < promptY + 5; i++)
    {
        ClearLine(i);
    }

    // 强制更新显示
    dataChanged = true;
    
    // 恢复心跳包发送
    ResumeHeartbeat();
}

void StartAutoRemap() 
{
    // 首先显示正在读取当前映射的状态
    strcpy(autoRemapStatus, "READING CURRENT MAP");
    dataChanged = true;
    
    // 读取当前映射表
    bool mappingRead = false;

    if (deviceState1p == DEVICE_OK)
    {
        mappingRead = ReadTouchSheet(hPort1, &response1);
    }
    else if (deviceState2p == DEVICE_OK)
    {
        mappingRead = ReadTouchSheet(hPort2, &response2);
    }

    if (!mappingRead)
    {
        strcpy(autoRemapStatus, "READ MAP FAILED");
        return;
    }
    
    // 映射读取成功，开始自动映射
    autoRemapActive = true;
    autoRemapStage = 0;
    autoRemapCollected = 0;
    autoRemapCompletedStages = 0;
    autoRemapLastTime = GetTickCount();
    memset(autoRemapRegions, 0xFF, TOUCH_REGIONS); // 初始化为无效值
    memset(autoRemapCompletedRegions, false, TOUCH_REGIONS); // 清空已完成标记
    memset(prevTouchMatrix, 0, sizeof(prevTouchMatrix)); // 清空上一帧触摸状态
    
    // 设置收集状态
    strcpy(autoRemapStatus, "COLLECTING");
    dataChanged = true;
}

void StopAutoRemap(bool success)
{
    autoRemapActive = false;

    if (success)
    {
        strcpy(autoRemapStatus, "SUCCESS");
        CompleteAutoRemap();
    }
    else
    {
        strcpy(autoRemapStatus, "CANCELED");
    }

    dataChanged = true;
}

void UpdateAutoRemap() 
{
    if (!autoRemapActive) {
        return;
    }
    
    DWORD currentTime = GetTickCount();
    
    // 检查是否超时（15秒无操作）
    if (currentTime - autoRemapLastTime > 15000) {
        StopAutoRemap(false);
        return;
    }
    
    // 检查是否已经收集了所有区块
    if (autoRemapCollected >= TOUCH_REGIONS) {
        // 等待5秒确认无新触发
        if (currentTime - autoRemapLastTime > 5000) {
            // 所有区块收集完成
            StopAutoRemap(true);
        }
    }
}

void ProcessAutoRemapTouch() 
{
    if (!autoRemapActive) {
        return;
    }

    // 处理当前触摸状态
    bool currentTouchMatrix[8][8] = {false};
    ProcessTouchStateBytes(usePlayer2 ? p2TouchState : p1TouchState, currentTouchMatrix);

    // 区块索引映射
    const struct {
        int matrixX;
        int matrixY;
        int index;
    } regionMapping[] = {
        // D1-D8
        {0, 0, 18}, {0, 1, 19}, {0, 2, 20}, {0, 3, 21}, {0, 4, 22}, {0, 5, 23}, {0, 6, 24}, {0, 7, 25},
        // A1-A8
        {1, 0, 0}, {1, 1, 1}, {1, 2, 2}, {1, 3, 3}, {1, 4, 4}, {1, 5, 5}, {1, 6, 6}, {1, 7, 7},
        // E1-E8
        {2, 0, 26}, {2, 1, 27}, {2, 2, 28}, {2, 3, 29}, {2, 4, 30}, {2, 5, 31}, {2, 6, 32}, {2, 7, 33},
        // B1-B8
        {3, 0, 8}, {3, 1, 9}, {3, 2, 10}, {3, 3, 11}, {3, 4, 12}, {3, 5, 13}, {3, 6, 14}, {3, 7, 15},
        // C1-C2
        {4, 0, 16}, {4, 1, 17}
    };

    // 找到新触发的区块（当前触发但上一帧未触发的区块）
    for (int i = 0; i < sizeof(regionMapping) / sizeof(regionMapping[0]); i++) {
        int x = regionMapping[i].matrixX;
        int y = regionMapping[i].matrixY;
        int regionIndex = regionMapping[i].index;

        // 检查是否是新触发的区块（当前触发且上一帧未触发）
        bool isNewTouch = currentTouchMatrix[y][x] && !prevTouchMatrix[y][x];
        
        if (isNewTouch) {
            // 检查此区块是否已被记录
            bool alreadyRecorded = false;
            for (int j = 0; j < autoRemapCollected; j++) {
                if (autoRemapRegions[j] == regionIndex) {
                    alreadyRecorded = true;
                    break;
                }
            }

            // 如果是新区块，记录它
            if (!alreadyRecorded) {
                // 记录区块到下一个可用位置
                autoRemapRegions[autoRemapCollected] = regionIndex;
                autoRemapCompletedRegions[regionIndex] = true; // 标记为已完成
                autoRemapCollected++;
                autoRemapLastTime = GetTickCount();
                dataChanged = true;
                
                // 实时更新进度状态
                if (autoRemapCollected >= 34) {
                    // 所有区块都已收集完成
                    autoRemapCompletedStages = 4;
                    strcpy(autoRemapStatus, "ALL REGIONS COLLECTED");
                    StopAutoRemap(true);
                } else {
                    // 更新收集进度状态
                    snprintf(autoRemapStatus, sizeof(autoRemapStatus), "COLLECTED: %d/34", autoRemapCollected);
                }
            }
        }
    }

    // 更新上一帧的触摸状态
    memcpy(prevTouchMatrix, currentTouchMatrix, sizeof(prevTouchMatrix));
}

void CompleteAutoRemap()
{
    // 当前映射已在StartAutoRemap中读取，直接进行映射处理
    
    // 创建标准区块索引数组
    const uint8_t standardOrder[TOUCH_REGIONS] = {
        // D1,A1,D2,A2,D3,A3,D4,A4,D5,A5,D6,A6,D7,A7,D8,A8,
        18, 0, 19, 1, 20, 2, 21, 3, 22, 4, 23, 5, 24, 6, 25, 7,
        // E1,E2,E3,E4,E5,E6,E7,E8,
        26, 27, 28, 29, 30, 31, 32, 33,
        // B1,B2,B3,B4,B5,B6,B7,B8,
        8, 9, 10, 11, 12, 13, 14, 15,
        // C1,C2
        16, 17
    };

    // 创建新的映射表
    uint8_t newTouchSheet[TOUCH_REGIONS];
    memset(newTouchSheet, 0xFF, TOUCH_REGIONS); // 初始化为无效值

    // 对于每个收集到的区块，找到它在标准顺序中对应的区块，然后创建映射
    for (int i = 0; i < TOUCH_REGIONS; i++)
    {
        if (i < autoRemapCollected && autoRemapRegions[i] != 0xFF)
        {
            // 获取此位置收集到的物理通道索引
            uint8_t collectedChannel = autoRemapRegions[i];
            
            // 找到标准顺序中的区域索引
            uint8_t standardRegion = (i < TOUCH_REGIONS) ? standardOrder[i] : 0xFF;
            
            if (standardRegion != 0xFF)
            {
                // 创建映射: 区域standardRegion -> 物理通道collectedChannel
                // 这意味着当系统需要standardRegion区域触摸时，应该检查collectedChannel通道
                newTouchSheet[standardRegion] = collectedChannel;
            }
        }
    }

    // 复制新映射表到全局变量
    memcpy(touchSheet, newTouchSheet, TOUCH_REGIONS);

    // 写入设备
    bool success1p = false, success2p = false;

    if (deviceState1p == DEVICE_OK)
    {
        success1p = WriteTouchSheet(hPort1, &response1);
    }
    if (deviceState2p == DEVICE_OK)
    {
        success2p = WriteTouchSheet(hPort2, &response2);
    }

    // 更新状态
    if (success1p || success2p)
    {
        strcpy(autoRemapStatus, "SUCCESS");

        // 发送心跳以确保连接保持
        if (success1p && deviceState1p == DEVICE_OK)
        {
            serial_heart_beat(hPort1, &response1);
        }
        if (success2p && deviceState2p == DEVICE_OK)
        {
            serial_heart_beat(hPort2, &response2);
        }
    }
    else
    {
        strcpy(autoRemapStatus, "WRITE FAILED");
    }

    dataChanged = true;
}

/*
void DisplayRawData(uint8_t *touchState, uint8_t *rawValue)
{
    int rows = 5;        // 5行
    int bytesPerRow = 7; // 每行显示7个字节

    // 显示触摸状态字节
    SetCursorPosition(0, 23);
    printf("Raw Touch Data (all 34 bytes):                                              ");

    for (int row = 0; row < rows; row++)
    {
        int startByte = row * bytesPerRow;
        int endByte = min(startByte + bytesPerRow, 34); // 确保不超过34字节

        // 清除当前行并设置光标位置
        SetCursorPosition(0, 24 + row);
        ClearLine(24 + row);

        // 显示字节索引范围
        printf("Bytes %2d-%2d: ", startByte, endByte - 1);

        // 显示每个字节的十六进制表示
        for (int i = startByte; i < endByte; i++)
        {
            printf("%02X ", rawValue[i]);
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
            printf("%3d ", rawValue[i]);
        }
    }

    // 清除可能多余的行
    for (int row = rows; row < 9; row++)
    {
        SetCursorPosition(0, 24 + row);
        ClearLine(24 + row);
    }
}
*/

/* ----- CH340控制函数 ----- */
static void set_dtr(HANDLE h, bool hi)
{
    if (h == INVALID_HANDLE_VALUE)
        return;
    EscapeCommFunction(h, hi ? SETDTR : CLRDTR);
}

static void set_rts(HANDLE h, bool hi)
{
    if (h == INVALID_HANDLE_VALUE)
        return;
    EscapeCommFunction(h, hi ? SETRTS : CLRRTS);
}

/* ----- 进入/退出启动加载程序 ----- */
static void enter_boot(HANDLE h)
{
    set_dtr(h, false); /* BOOT0 = 1 */
    set_rts(h, true);  /* nRST = 0 */
    Sleep(50);
    set_rts(h, false); /* 释放复位 */
    Sleep(100);
}

static void exit_boot(HANDLE h)
{
    set_dtr(h, true); /* BOOT0 = 0 */
    set_rts(h, true); /* nRST = 0 */
    Sleep(50);
    set_rts(h, false); /* nRST = 1 */
    Sleep(100);
}

/* ----- 串口通信辅助函数 ----- */
static bool tx_byte(HANDLE h, unsigned char b)
{
    DWORD w = 0;
    return WriteFile(h, &b, 1, &w, NULL) && w == 1;
}

static bool rx_byte(HANDLE h, unsigned char *b, DWORD tout_ms)
{
    COMMTIMEOUTS tmo;
    GetCommTimeouts(h, &tmo);
    COMMTIMEOUTS orig = tmo;
    tmo.ReadIntervalTimeout = MAXDWORD;
    tmo.ReadTotalTimeoutConstant = tout_ms;
    tmo.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(h, &tmo);

    DWORD r = 0;
    BOOL ok = ReadFile(h, b, 1, &r, NULL);

    SetCommTimeouts(h, &orig);
    return ok && r == 1;
}

/* ----- Bootloader协议函数 ----- */
static unsigned char xor_sum(const unsigned char *p, size_t n)
{
    unsigned char c = 0;
    while (n--)
        c ^= *p++;
    return c;
}

static bool bl_sync(HANDLE h)
{
    unsigned char ack;
    for (DWORD t = 0; t < (SYNC_TIMEOUT_MS / 50); ++t)
    {
        if (tx_byte(h, 0x7F) && rx_byte(h, &ack, 100) && ack == 0x79)
            return true;
        Sleep(50);
    }
    return false;
}

static bool bl_cmd(HANDLE h, unsigned char cmd)
{
    if (!tx_byte(h, cmd) || !tx_byte(h, cmd ^ 0xFF))
        return false;
    unsigned char ack;
    return rx_byte(h, &ack, 200) && ack == 0x79;
}

static bool bl_mass_erase(HANDLE h)
{
    if (!bl_cmd(h, 0x44))
        return false;
    unsigned char seq[3] = {0xFF, 0xFF, 0x00};
    seq[2] = xor_sum(seq, 2);
    DWORD written;
    if (!WriteFile(h, seq, 3, &written, NULL) || written != 3)
        return false;
    unsigned char ack;
    return rx_byte(h, &ack, 5000) && ack == 0x79;
}

static bool bl_write_block(HANDLE h, uint32_t addr, const unsigned char *buf, size_t len)
{
    if (len == 0 || len > PAGE_SZ)
        return false;
    if (!bl_cmd(h, 0x31))
        return false;

    unsigned char a[5] = {
        (unsigned char)((addr >> 24) & 0xFF),
        (unsigned char)((addr >> 16) & 0xFF),
        (unsigned char)((addr >> 8) & 0xFF),
        (unsigned char)(addr & 0xFF),
        0};
    a[4] = xor_sum(a, 4);
    DWORD written;
    if (!WriteFile(h, a, 5, &written, NULL) || written != 5)
        return false;

    unsigned char ack;
    if (!rx_byte(h, &ack, 200) || ack != 0x79)
        return false;

    unsigned char pkt[PAGE_SZ + 2];
    pkt[0] = (unsigned char)(len - 1);
    memcpy(pkt + 1, buf, len);
    pkt[len + 1] = xor_sum(pkt, len + 1);
    if (!WriteFile(h, pkt, len + 2, &written, NULL) || written != len + 2)
        return false;

    return rx_byte(h, &ack, 500) && ack == 0x79;
}

static bool is_ch340(DWORD vid, DWORD pid)
{
    return vid == VID_CH340 && pid == PID_CH340;
}

/* ----- 查找和打开CH340设备 ----- */
typedef struct
{
    char *port_result;
    size_t result_size;
    bool found;
    bool multiple;
    bool timeout;
    volatile BOOL shouldExit;
} CH340FindData;

// 设备查找线程函数
DWORD WINAPI FindCH340Thread(LPVOID lpParam)
{
    CH340FindData *findData = (CH340FindData *)lpParam;
    findData->found = false;
    findData->multiple = false;
    findData->timeout = false;

    HDEVINFO devs = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
    if (devs == INVALID_HANDLE_VALUE)
        return 1;

    int ch340_count = 0;
    char first_port[32] = {0};

    SP_DEVINFO_DATA info = {.cbSize = sizeof(info)};
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devs, i, &info); ++i)
    {
        char hwid[256] = {0};
        DWORD n = 0;

        if (ch340_count > 1)
        {
            findData->multiple = true;
            SetupDiDestroyDeviceInfoList(devs);
            return 0;
        }

        if (findData->shouldExit)
        {
            SetupDiDestroyDeviceInfoList(devs);
            return 0;
        }
        if (!SetupDiGetDeviceRegistryPropertyA(devs, &info, SPDRP_HARDWAREID, NULL, (BYTE *)hwid, sizeof(hwid), &n))
            continue;

        // 检查是否是CH340设备
        DWORD vid = 0, pid = 0;
        if (sscanf(hwid, "USB\\VID_%4lx&PID_%4lx", &vid, &pid) != 2)
            continue;

        if (vid == VID_CH340 && pid == PID_CH340)
        {
            // 找到一个CH340设备
            ch340_count++;

            // 如果发现多个设备，立即结束搜索
            if (ch340_count > 1)
            {
                findData->multiple = true;
                SetupDiDestroyDeviceInfoList(devs);
                return 0;
            }

            // 获取设备友好名称
            char friendly[256] = {0};
            if (SetupDiGetDeviceRegistryPropertyA(devs, &info, SPDRP_FRIENDLYNAME, NULL, (BYTE *)friendly, sizeof(friendly), &n))
            {
                // 提取COM端口号
                char *p = strrchr(friendly, '(');
                if (p && strstr(p, "COM"))
                {
                    strncpy(first_port, p + 1, sizeof(first_port) - 1);
                    char *end = strrchr(first_port, ')');
                    if (end)
                        *end = 0;

                    // 保存结果
                    strncpy(findData->port_result, first_port, findData->result_size);
                    findData->found = true;
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(devs);
    return 0;
}

static bool find_ch340_port(char *out, size_t cch)
{
    // 清空输出缓冲区
    memset(out, 0, cch);

    // 准备线程参数
    CH340FindData findData;
    findData.shouldExit = FALSE;
    findData.port_result = out;
    findData.result_size = cch;
    findData.found = false;
    findData.multiple = false;
    findData.timeout = false;

    // 创建一个独立线程执行设备枚举
    HANDLE hThread = CreateThread(NULL, 0, FindCH340Thread, &findData, 0, NULL);
    if (hThread == NULL)
    {
        return false;
    }

    const DWORD THREAD_TIMEOUT = 500;
    DWORD waitResult = WaitForSingleObject(hThread, THREAD_TIMEOUT);

    if (waitResult == WAIT_TIMEOUT)
    {
        findData.shouldExit = TRUE;
        Sleep(100);
        TerminateThread(hThread, 1);
        CloseHandle(hThread);
        strncpy(out, "TIMEOUT", cch);
        return false;
    }

    // 线程已完成
    CloseHandle(hThread);

    // 检查多设备情况
    if (findData.multiple)
    {
        strncpy(out, "MULTIPLE", cch);
        return false;
    }

    // 检查是否找到设备
    if (findData.found)
    {
        return true;
    }

    // 没有找到设备
    return false;
}

/* ----- 打开串口进行固件更新 ----- */
static bool open_boot_port(const char *name)
{
    char path[32];
    snprintf(path, 32, "\\\\.\\%s", name);

    hBootPort = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hBootPort == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    SetupComm(hBootPort, 4096, 4096);
    DCB dcb = {.DCBlength = sizeof(dcb)};
    if (!GetCommState(hBootPort, &dcb))
        goto err;

    dcb.BaudRate = BAUDRATE_BOOT;
    dcb.Parity = EVENPARITY;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.fParity = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = dcb.fOutX = dcb.fInX = FALSE;
    if (!SetCommState(hBootPort, &dcb))
        goto err;

    COMMTIMEOUTS tmo = {MAXDWORD, 100, 0, 500, 0};
    SetCommTimeouts(hBootPort, &tmo);
    PurgeComm(hBootPort, PURGE_TXCLEAR | PURGE_RXCLEAR);

    set_dtr(hBootPort, false);
    set_rts(hBootPort, false);
    Sleep(100);
    return true;

err:
    CloseHandle(hBootPort);
    hBootPort = INVALID_HANDLE_VALUE;
    return false;
}

/* ----- 释放固件资源 ----- */
static void cleanup_firmware()
{
    if (firmware_data)
    {
        free(firmware_data);
        firmware_data = NULL;
    }
    firmware_data_len = 0;
    
    if (hBootPort != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hBootPort);
        hBootPort = INVALID_HANDLE_VALUE;
    }
    
    // 重置固件更新相关状态
    firmware_update_ready = false;
    firmware_updating = false;
    firmware_update_progress = 0;
}

/* ----- 查找并加载固件文件 ----- */
bool FindFirmwareFile(void)
{
    // 获取可执行文件目录
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);

    // 提取目录部分
    wchar_t *last_slash = wcsrchr(exe_path, L'\\');
    if (last_slash)
    {
        *(last_slash + 1) = L'\0'; // 截断文件名，只保留路径
    }

    // 构建搜索模式
    wchar_t search_pattern[MAX_PATH];
    wcscpy_s(search_pattern, MAX_PATH, exe_path);
    wcscat_s(search_pattern, MAX_PATH, L"Curva_G431_*.bin");

    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = FindFirstFileW(search_pattern, &find_data);

    if (find_handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    unsigned long long latest_version = 0;
    wchar_t latest_filename[MAX_PATH] = {0};

    do
    {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            // 处理文件名，提取版本号
            const wchar_t *prefix = L"Curva_G431_";
            size_t prefix_len = wcslen(prefix);

            if (wcsncmp(find_data.cFileName, prefix, prefix_len) == 0)
            {
                // 找到前缀，提取版本部分
                const wchar_t *version_start = find_data.cFileName + prefix_len;
                size_t version_len = wcslen(version_start);

                // 移除可能的.bin后缀
                if (version_len > 4 && wcscmp(version_start + version_len - 4, L".bin") == 0)
                {
                    version_len -= 4;
                }

                // 转换为数字进行比较
                wchar_t version_str[32] = {0};
                if (version_len < 32)
                {
                    wcsncpy_s(version_str, 32, version_start, version_len);
                    unsigned long long version = wcstoull(version_str, NULL, 10);

                    if (version > latest_version)
                    {
                        latest_version = version;
                        wcscpy_s(latest_filename, MAX_PATH, find_data.cFileName);
                    }
                }
            }
        }
    } while (FindNextFileW(find_handle, &find_data) != 0);

    FindClose(find_handle);

    if (latest_filename[0] == 0)
    {
        return false;
    }

    // 构建完整文件路径
    wcscpy_s(firmware_path, MAX_PATH, exe_path);
    wcscat_s(firmware_path, MAX_PATH, latest_filename);

    // 提取版本号
    wchar_t version_str[32] = {0};
    const wchar_t *prefix = L"Curva_G431_";
    size_t prefix_len = wcslen(prefix);

    if (wcslen(latest_filename) > prefix_len)
    {
        wcscpy_s(version_str, 32, latest_filename + prefix_len);
        // 移除.bin后缀
        wchar_t *dot = wcsrchr(version_str, L'.');
        if (dot)
            *dot = 0;

        swprintf_s(firmware_version, 32, L"v1.%s", version_str);
    }

    // 加载固件数据
    FILE *f = NULL;
    if (_wfopen_s(&f, firmware_path, L"rb") != 0 || !f)
    {
        return false;
    }

    // 获取文件大小
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0)
    {
        fclose(f);
        return false;
    }

    // 释放之前的固件
    if (firmware_data)
    {
        free(firmware_data);
    }

    // 分配内存
    firmware_data = (unsigned char *)malloc(file_size);
    if (!firmware_data)
    {
        fclose(f);
        return false;
    }

    firmware_data_len = (unsigned int)file_size;

    // 读取文件内容
    if (fread(firmware_data, 1, file_size, f) != (size_t)file_size)
    {
        fclose(f);
        free(firmware_data);
        firmware_data = NULL;
        return false;
    }

    fclose(f);
    return true;
}

// 设置更新状态消息
void SetFirmwareStatusMessage(const char *msg)
{
    if (firmware_status_message)
    {
        free(firmware_status_message);
    }

    if (msg)
    {
        size_t len = strlen(msg) + 1;
        firmware_status_message = (char *)malloc(len);
        if (firmware_status_message)
        {
            strcpy_s(firmware_status_message, len, msg);
        }
    }
    else
    {
        firmware_status_message = NULL;
    }

    dataChanged = true;
}

// 固件更新线程
DWORD WINAPI UpdateFirmwareThread(LPVOID lpParam)
{
    firmware_updating = true;
    firmware_update_progress = 0;
    char com_port[32] = {0};

    SetFirmwareStatusMessage("Searching for devices...");
    Sleep(500);

    // 寻找设备
    if (!find_ch340_port(com_port, sizeof(com_port)))
    {
        // 检查是否是多设备错误
        if (strcmp(com_port, "MULTIPLE") == 0)
        {
            SetFirmwareStatusMessage("Error: Multiple CH340 devices detected.");
        }
        else if (strcmp(com_port, "TIMEOUT") == 0)
        {
            SetFirmwareStatusMessage("Error: Device detection timed out. Please try again.");
        }
        else
        {
            SetFirmwareStatusMessage("Error: CH340 device not found");
        }
        firmware_updating = false;
        return 1;
    }

    SetFirmwareStatusMessage("Device connected, preparing for update...");
    firmware_update_progress = 5;
    Sleep(500);

    // 打开端口
    if (!open_boot_port(com_port))
    {
        SetFirmwareStatusMessage("Error: Cannot open serial port");
        firmware_updating = false;
        return 1;
    }

    SetFirmwareStatusMessage("Device connected, entering update mode...");
    firmware_update_progress = 10;

    // 进入bootloader模式
    enter_boot(hBootPort);

    // 同步设备
    if (!bl_sync(hBootPort))
    {
        SetFirmwareStatusMessage("Error: Device synchronization failed");
        exit_boot(hBootPort);
        CloseHandle(hBootPort);
        hBootPort = INVALID_HANDLE_VALUE;
        firmware_updating = false;
        return 1;
    }

    firmware_update_progress = 20;
    SetFirmwareStatusMessage("Erasing chip...");

    // 擦除芯片
    bool erased = false;
    for (int r = 0; r < ERASE_RETRY; ++r)
    {
        if (bl_mass_erase(hBootPort))
        {
            erased = true;
            break;
        }
    }

    if (!erased)
    {
        SetFirmwareStatusMessage("Error: Chip erase failed");
        exit_boot(hBootPort);
        CloseHandle(hBootPort);
        hBootPort = INVALID_HANDLE_VALUE;
        firmware_updating = false;
        return 1;
    }

    firmware_update_progress = 30;
    SetFirmwareStatusMessage("Writing firmware...");

    // 写入固件
    uint32_t addr = 0x08000000;
    size_t sent = 0;

    while (sent < firmware_data_len)
    {
        size_t chunk = firmware_data_len - sent;
        if (chunk > PAGE_SZ)
            chunk = PAGE_SZ;

        if (!bl_write_block(hBootPort, addr, firmware_data + sent, chunk))
        {
            char err_msg[64];
            snprintf(err_msg, sizeof(err_msg), "Error: Write failed @0x%08X", addr);
            SetFirmwareStatusMessage(err_msg);
            exit_boot(hBootPort);
            CloseHandle(hBootPort);
            hBootPort = INVALID_HANDLE_VALUE;
            firmware_updating = false;
            return 1;
        }

        sent += chunk;
        addr += (uint32_t)chunk;

        // 更新进度
        firmware_update_progress = 30 + (int)(sent * 60 / firmware_data_len);
        dataChanged = true;
        Sleep(10);
    }

    firmware_update_progress = 95;
    SetFirmwareStatusMessage("Verifying firmware...");
    Sleep(500);

    firmware_update_progress = 100;
    SetFirmwareStatusMessage("Update successful, restarting device");

    // 退出bootloader模式，重启设备
    exit_boot(hBootPort);
    CloseHandle(hBootPort);
    hBootPort = INVALID_HANDLE_VALUE;

    // 清理
    cleanup_firmware();
    firmware_updating = false;
    return 0;
}

// 启动固件更新过程
void StartFirmwareUpdate(void)
{
    if (firmware_updating)
    {
        return; // 已经在更新中
    }

    // 创建更新线程
    HANDLE updateThread = CreateThread(
        NULL,                 // 默认安全属性
        0,                    // 默认堆栈大小
        UpdateFirmwareThread, // 线程函数
        NULL,                 // 无参数
        0,                    // 立即运行
        NULL                  // 不需要线程ID
    );

    if (updateThread)
    {
        CloseHandle(updateThread);
    }
    else
    {
        SetFirmwareStatusMessage("Error: Unable to start update thread");
    }
}

void PrepareForFirmwareUpdate()
{
    // 关闭所有可能已打开的端口
    if (hBootPort != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hBootPort);
        hBootPort = INVALID_HANDLE_VALUE;
    }

    // 清理固件数据（这会重置所有相关状态）
    cleanup_firmware();
    
    // 清理状态消息
    SetFirmwareStatusMessage(NULL);
}

void DisplayFirmwareUpdateWindow()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    // 清屏
    for (int i = 3; i < 30; i++)
    {
        ClearLine(i);
    }

    // 显示固件更新界面
    SetCursorPosition(0, 4);
    printf("┌────────────────────────── Curva Firmware AutoUpdater ─────────────────────────┐");

    SetCursorPosition(0, 5);
    printf("│                                                                               │");

    SetCursorPosition(0, 6);
    printf("│  Detected Firmware: %-20S                                      │", firmware_version);

    SetCursorPosition(0, 7);
    printf("│  Firmware Path: %-57.57S     │", firmware_path[0] ? firmware_path : L"No firmware file found");

    SetCursorPosition(0, 8);
    printf("│  To use AutoUpdater, verify that the HW version is at least ");
    SetConsoleTextAttribute(hConsole, COLOR_BLUE);
    printf("v1.250515G");
    SetConsoleTextAttribute(hConsole, defaultAttrs);
    printf(".       │");

    SetCursorPosition(0, 9);
    printf("│                                                                               │");

    SetCursorPosition(0, 10);
    if (firmware_update_ready)
    {
        printf("│  Ready to update. Press Enter to start firmware update                        │");
    }
    else
    {
        printf("│");
        SetConsoleTextAttribute(hConsole, COLOR_RED);
        printf("  No valid firmware file found. Please put firmware files in program directory ");
        SetConsoleTextAttribute(hConsole, defaultAttrs);
        printf("│");
    }

    SetCursorPosition(0, 11);
    printf("│                                                                               │");

    // 显示更新状态和进度条
    if (firmware_updating)
    {
        SetCursorPosition(0, 12);
        printf("│  Updating: [");

        // 绘制进度条 (50个字符宽度)
        const int bar_width = 50;
        int filled = (firmware_update_progress * bar_width) / 100;

        for (int i = 0; i < bar_width; i++)
        {
            if (i < filled)
            {
                printf("#");
            }
            else
            {
                printf(" ");
            }
        }

        printf("] %3d%%          │", firmware_update_progress);

        SetCursorPosition(0, 13);
        printf("│                                                                               │");

        SetCursorPosition(0, 14);
        if (firmware_status_message)
        {
            printf("│  Status: %-69s│", firmware_status_message);
        }
        else
        {
            printf("│  Status: Preparing update...                                                │");
        }
    }
    else if (firmware_status_message)
    {
        SetCursorPosition(0, 12);
        printf("│                                                                               │");

        SetCursorPosition(0, 13);
        printf("│  Status: ");

        if (strstr(firmware_status_message, "Error") != NULL)
        {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
            printf("%-69s", firmware_status_message);
        }
        else if (strstr(firmware_status_message, "successful") != NULL ||
                 strstr(firmware_status_message, "Update success") != NULL)
        {
            SetConsoleTextAttribute(hConsole, COLOR_GREEN);
            printf("%-69s", firmware_status_message);
        }
        else
        {
            printf("%-69s", firmware_status_message);
        }

        SetConsoleTextAttribute(hConsole, defaultAttrs);
        printf("│");

        SetCursorPosition(0, 14);
        printf("│                                                                               │");
    }
    else
    {
        SetCursorPosition(0, 12);
        printf("│                                                                               │");

        SetCursorPosition(0, 13);
        printf("│                                                                               │");

        SetCursorPosition(0, 14);
        printf("│                                                                               │");
    }

    // 底部说明
    SetCursorPosition(0, 15);
    printf("│                                                                               │");

    SetCursorPosition(0, 16);
    printf("│  ※ DO NOT disconnect power or USB connection during update                    │");

    SetCursorPosition(0, 17);
    printf("│  ※ Device will automatically restart after update completes                   │");

    SetCursorPosition(0, 18);
    printf("│                                                                               │");

    SetCursorPosition(0, 19);
    printf("│  Press [F3] to return to Main View                                            │");

    SetCursorPosition(0, 20);
    printf("└───────────────────────────────────────────────────────────────────────────────┘");
}

void UpdateFirmwareProgressOnly()
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD defaultAttrs = csbi.wAttributes;

    // 只更新进度条和状态部分
    if (firmware_updating)
    {
        // 更新进度条
        SetCursorPosition(0, 12);
        printf("│  Updating: [");

        // 绘制进度条 (50个字符宽度)
        const int bar_width = 50;
        int filled = (firmware_update_progress * bar_width) / 100;

        for (int i = 0; i < bar_width; i++)
        {
            if (i < filled)
            {
                printf("#");
            }
            else
            {
                printf(" ");
            }
        }

        printf("] %3d%%          │", firmware_update_progress);
    }

    // 更新状态消息行
    SetCursorPosition(0, 13);
    printf("│                                                                               │");

    SetCursorPosition(0, 14);
    if (firmware_status_message)
    {
        printf("│  Status: ");

        if (strstr(firmware_status_message, "Error") != NULL)
        {
            SetConsoleTextAttribute(hConsole, COLOR_RED);
            printf("%-69s", firmware_status_message);
        }
        else if (strstr(firmware_status_message, "successful") != NULL ||
                 strstr(firmware_status_message, "Update success") != NULL)
        {
            SetConsoleTextAttribute(hConsole, COLOR_GREEN);
            printf("%-69s", firmware_status_message);
        }
        else
        {
            printf("%-69s", firmware_status_message);
        }

        SetConsoleTextAttribute(hConsole, defaultAttrs);
        printf("│");
    }
    else
    {
        printf("│                                                                               │");
    }
}