#include <windows.h>
#include <limits.h>
#include <stdint.h>
#include "config.h"
#include "mai2io.h"
#include "serial.h"

extern HANDLE hPort1; // 串口句柄
extern HANDLE hPort2; // 串口句柄
uint8_t opts = 0;
uint8_t p1 = 0,p2 = 0;

uint16_t mai2_io_get_api_version(void)
{
    return 0x0100;
}

HRESULT mai2_io_init(void)
{
    open_port(&hPort1,"\\\\.\\COM11");
    open_port(&hPort2,"\\\\.\\COM12");
    p1 = 0;
    p2 = 0;
    return S_OK;
}

HRESULT mai2_io_poll(void)
{
    uint8_t c;
    package_init(&request1);	
    switch (serial_read_cmd(&hPort1,&request1)) {
        case SERIAL_CMD_AUTO_SCAN:
            p1 = (request1.key_status & 0b1111111) | (request1.io_status & 0b10000000);
            opts = request1.io_status & 0b1111; 
            break;
        case 0xff:
            close_port(&hPort1);
            open_port(&hPort1,"\\\\.\\COM11");
            p1 = 0;
            opts = 0;
            break;
        default:
            break;
    }

    package_init(&request2);	
    switch (serial_read_cmd(&hPort2,&request2)) {
        case SERIAL_CMD_AUTO_SCAN:
            p2 = (request2.key_status & 0b1111111) |  (request2.io_status & 0b10000000);
            opts = opts | (request2.io_status & 0b111); 
            opts = opts | ((request2.io_status & 0b1000) << 1); 
            break;
        case 0xff:
            close_port(&hPort2);
            open_port(&hPort2,"\\\\.\\COM12");
            p2 = 0;
            break;
        default:
            break;
    }
    serial_heart_beat(&hPort1,&rsponse1);
    serial_heart_beat(&hPort2,&rsponse2);
    return S_OK;
}

void mai2_io_get_opbtns(uint8_t *opbtn){
    *opbtn = opts;
}

void mai2_io_get_gamebtns(uint16_t *player1, uint16_t *player2){
    *player1 = p1;
    *player2 = p2;
}
