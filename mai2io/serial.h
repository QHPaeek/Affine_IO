#ifndef SERIALSLIDER_H
#define SERIALSLIDER_H
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <winioctl.h>
#include <conio.h>
#include <stdbool.h>
#include <ctype.h>

#define BUFSIZE 128
#define CMD_TIMEOUT 3000

typedef enum serial_cmd {
	SERIAL_CMD_AUTO_SCAN = 0x02,
	SERIAL_CMD_SCAN_START = 0x03,
	SERIAL_CMD_SCAN_STOP = 0x04,
	SERIAL_CMD_CHANGE_TOUCH_THRESHOLD = 0x05,
	SERIAL_CMD_RESET = 0x10,
	SERIAL_CMD_HEART_BEAT = 0x11,
	SERIAL_CMD_GET_BOARD_INFO = 0xF0
} serial_cmd_t;

typedef union serial_packet {
	struct {
		uint8_t syn;
		uint8_t cmd;
		uint8_t size;
		union{
			struct {				
				uint8_t key_status;
				uint8_t io_status;
				uint8_t touch[7];
			};
			struct{
				uint8_t button_led[24];
				uint8_t fet_led[3];
			};
			uint8_t threshold[34];
			uint8_t raw_value[34];
		};
	};
	uint8_t data[BUFSIZE];
} serial_packet_t;

extern char comPort1[13]; //串口号
extern char comPort2[13]; //串口号
extern HANDLE hPort1; // 串口句柄
extern HANDLE hPort2; // 串口句柄
extern DCB dcb; // 串口参数结构体
extern COMMTIMEOUTS timeouts; // 串口超时结构体
extern OVERLAPPED ovWrite;
extern bool fWaitingOnRead;
extern bool fWaitingOnWrite;
extern serial_packet_t request1;
extern serial_packet_t response1;
extern serial_packet_t request2;
extern serial_packet_t response2;
extern bool Serial_Status;//串口状态（是否成功打开）

BOOL open_port(HANDLE *hPortx,char* comPortx);
void close_port(HANDLE *hPortx);
void package_init(serial_packet_t *rsponse);
uint8_t serial_read_cmd(HANDLE hPortx,serial_packet_t *request);
void serial_heart_beat(HANDLE hPortx,serial_packet_t *rsponse);
void serial_change_touch_threshold(HANDLE hPortx,serial_packet_t *rsponse,uint8_t *touch_threshold);
void serial_scan_start(HANDLE hPortx,serial_packet_t *rsponse);
void serial_scan_stop(HANDLE hPortx,serial_packet_t *rsponse);
char* GetSerialPortByVidPid(const char* vid, const char* pid);

#endif