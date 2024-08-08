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


#define comPort1 "COM11"
#define comPort2 "COM12"

#define BUFSIZE 128
#define CMD_TIMEOUT 3000

typedef enum serial_cmd {
	SERIAL_CMD_AUTO_SCAN = 0x01,
	SERIAL_CMD_AUTO_SCAN_START = 0x03,
	SERIAL_CMD_AUTO_SCAN_STOP = 0x04,
	SERIAL_CMD_RESET = 0x10,
	SERIAL_CMD_GET_BOARD_INFO = 0xF0
} serial_cmd_t;

typedef union serial_packet {
	struct {
		uint8_t syn;
		uint8_t cmd;
		uint8_t size;
		union {
			uint8_t key_status;
		};
	};
	uint8_t data[BUFSIZE];
} serial_packet_t;

struct SerialPort{
char comPort[5]; //串口号
HANDLE hPort; // 串口句柄
DCB dcb; // 串口参数结构体
COMMTIMEOUTS timeouts; // 串口超时结构体
OVERLAPPED ovWrite;
bool fWaitingOnRead;
bool fWaitingOnWrite;
serial_packet_t request;
serial_packet_t rsponse;
bool Serial_Status;//串口状态（是否成功打开）
};

extern struct SerialPort SerialPort1;
extern struct SerialPort SerialPort2;

BOOL open_port(struct SerialPort *port);
void close_port(struct SerialPort *port);
void initializeSerialPort(struct SerialPort *port, const char *comPort);
BOOL serial_read1(struct SerialPort *port,uint8_t *result);
uint8_t serial_read_cmd(struct SerialPort *port,serial_packet_t *reponse);
void package_init(serial_packet_t *request);
void keypad_rst(struct SerialPort *port);
void keypad_start_scan(struct SerialPort *port);
void keypad_stop_scan(struct SerialPort *port);

#endif