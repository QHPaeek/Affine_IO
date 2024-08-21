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
	SERIAL_CMD_AUTO_SCAN = 0x01,
	SERIAL_CMD_AUTO_SCAN_START = 0x03,
	SERIAL_CMD_AUTO_SCAN_STOP = 0x04,
	SERIAL_CMD_RESET = 0x10,
	SERIAL_CMD_HEART_BEAT = 0x11,
	SERIAL_CMD_GET_BOARD_INFO = 0xF0
} serial_cmd_t;

typedef union serial_packet {
	struct {
		uint8_t syn;
		uint8_t cmd;
		uint8_t size;
		struct {
			uint8_t key_status;
			uint8_t io_status;
		};
	};
	uint8_t data[BUFSIZE];
} serial_packet_t;

char comPort1[5]; //串口号
char comPort2[5]; //串口号
HANDLE hPort1; // 串口句柄
HANDLE hPort2; // 串口句柄
DCB dcb; // 串口参数结构体
COMMTIMEOUTS timeouts; // 串口超时结构体
OVERLAPPED ovWrite;
bool fWaitingOnRead;
bool fWaitingOnWrite;
serial_packet_t request1;
serial_packet_t rsponse1;
serial_packet_t request2;
serial_packet_t rsponse2;
bool Serial_Status;//串口状态（是否成功打开）

BOOL open_port(HANDLE *hPortx,char* comPortx);
void close_port(HANDLE *hPortx);
BOOL IsSerialPortOpen(HANDLE *hPortx);
void package_init(serial_packet_t *rsponse);
BOOL send_data(HANDLE *hPortx,int length,uint8_t *send_buffer);
void serial_writeresp(HANDLE *hPortx,serial_packet_t *rsponse);
BOOL serial_read1(HANDLE *hPortx,uint8_t *result);
uint8_t serial_read_cmd(HANDLE *hPortx,serial_packet_t *request);
void serial_heart_beat(HANDLE *hPortx,serial_packet_t *rsponse);

#endif