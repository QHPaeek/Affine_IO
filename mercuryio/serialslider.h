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

typedef enum slider_cmd {
	SLIDER_CMD_NOP = 0,
	SLIDER_CMD_AUTO_SCAN = 0x01,
	SLIDER_CMD_SET_LED = 0x02,
	SLIDER_CMD_AUTO_SCAN_START = 0x03,
	SLIDER_CMD_AUTO_SCAN_STOP = 0x04,
	SLIDER_CMD_RESET = 0x10,
	SLIDER_CMD_GET_BOARD_INFO = 0xF0
} slider_cmd_t;

typedef union slider_packet {
	struct {
		uint8_t syn;
		uint8_t cmd;
		uint8_t size;
		union {
			struct{
				uint8_t board_no;
				uint8_t leds[60];
			};
			uint8_t cell[30];
		};
	};
	uint8_t data[BUFSIZE];
} slider_packet_t;


const char* GetSerialPortByVidPid(const char* vid, const char* pid);
BOOL open_port();
void close_port();
BOOL IsSerialPortOpen();
void sliderserial_writeresp(slider_packet_t *request);
DWORD WINAPI sliderserial_read_thread(LPVOID param);
BOOL serial_read1(uint8_t *result);
uint8_t serial_read_cmd(slider_packet_t *reponse);
void package_init(slider_packet_t *request);
void slider_rst();
void slider_start_scan();
void slider_stop_scan();
void slider_send_leds(const uint8_t *rgb);

#endif