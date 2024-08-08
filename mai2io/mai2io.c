#include <windows.h>

#include <limits.h>
#include <stdint.h>

#include "mai2io.h"
#include "config.h"

#include "serial.h"

static struct mai2_io_config mai2_io_cfg;
extern struct SerialPort SerialPort1;
extern struct SerialPort SerialPort2;
uint8_t opts;
uint8_t p1,p2;

uint16_t mai2_io_get_api_version(void)
{
    return 0x0100;
}

HRESULT mai2_io_init(void)
{
    initializeSerialPort(&SerialPort1,comPort1);
    initializeSerialPort(&SerialPort2,comPort2);
    open_port(&SerialPort1);
    open_port(&SerialPort2);
    mai2_io_config_load(&mai2_io_cfg, L".\\segatools.ini");
    return S_OK;
}

HRESULT mai2_io_poll(void)
{

    if (GetAsyncKeyState(mai2_io_cfg.vk_test) & 0x8000) {
        opts |= MAI2_IO_OPBTN_TEST;
    }if (GetAsyncKeyState(mai2_io_cfg.vk_service) & 0x8000) {
        opts |= MAI2_IO_OPBTN_SERVICE;
    }if (GetAsyncKeyState(mai2_io_cfg.vk_coin) & 0x8000) {
        opts |= MAI2_IO_OPBTN_COIN;
    }if (GetAsyncKeyState(mai2_io_cfg.vk_p1_start) & 0x8000) {
        opts |= MAI2_IO_P1_START;
    }if (GetAsyncKeyState(mai2_io_cfg.vk_p2_start) & 0x8000) {
        opts |= MAI2_IO_P2_START;
    }

    package_init(&SerialPort1.rsponse);	
    if(SerialPort1.Serial_Status == FALSE){
        SerialPort1.Serial_Status = open_port(&SerialPort1);
    }else{
        switch (serial_read_cmd(&SerialPort1,&SerialPort1.rsponse)) {
            case SERIAL_CMD_AUTO_SCAN:
                package_init(&SerialPort1.rsponse);
                p1 = SerialPort1.rsponse.key_status;
                break;
            case 0xff:
                close_port(&SerialPort1);
                SerialPort1.Serial_Status = FALSE;
                break;
            default:
                break;
        }
    }


    package_init(&SerialPort2.rsponse);	
    if(SerialPort2.Serial_Status == FALSE){
        SerialPort2.Serial_Status = open_port(&SerialPort2);
    }else{
        switch (serial_read_cmd(&SerialPort2,&SerialPort2.rsponse)) {
            case SERIAL_CMD_AUTO_SCAN:
                package_init(&SerialPort2.rsponse);
                p2 = SerialPort2.rsponse.key_status;
                break;
            case 0xff:
                close_port(&SerialPort2);
                SerialPort1.Serial_Status = FALSE;
                break;
            default:
                break;
        }
    }
    return S_OK;
}

void mai2_io_get_opbtns(uint8_t *opbtn){
    *opbtn = opts;
}

void mai2_io_get_gamebtns(uint16_t *player1, uint16_t *player2){
    *player1 = p1;
    *player2 = p2;
}
