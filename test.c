#include "serialslider.h"
//#include <synchapi.h>
slider_packet_t reponse;
uint8_t rgb[96] = {0};
slider_packet_t reponse;
uint8_t pressure[32];
uint8_t checksum = 0;
uint8_t rep_size = 0;
uint8_t Serial_CMD = 0;
BOOL ESC = FALSE;

void main(){
    open_port();
    slider_start_air_scan();
    slider_start_scan();
    slider_send_leds(rgb);
    uint8_t c;
    while(1){
        switch (serial_read_cmd(&reponse)) {
            case SLIDER_CMD_AUTO_SCAN:
                printf("Ground key:");
                memcpy(pressure, reponse.pressure, 32);
                Serial_CMD = 0;
                for(uint8_t i=0;i<32;i++){
                    printf("%X ",pressure[i]);
                }
                printf("\n");
                package_init(&reponse);
                break;
            case SLIDER_CMD_AUTO_AIR:
                printf("air key:");
                for(uint8_t i=0;i<6;i++){
                    printf("%X ",reponse.air_status&(1<<i));
                }
                printf("\n");
                package_init(&reponse);
                Serial_CMD = 0;
                break;
            // case 0:
            //     memset(pressure,10,32);
            //     break;
            // case 0xff:
            //     memset(pressure,30,32);
            //     break;
            // case 0xfd:
            //     memset(pressure,40,32);
            //     break;
            default:
                //printf("no command \n");
                break;
        }
    }
}