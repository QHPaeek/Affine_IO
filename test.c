#include "serialslider.h"
//#include <synchapi.h>
slider_packet_t reponse;
uint8_t rgb[96] = {0};

void main(){
    open_port();
    slider_start_scan();
    slider_send_leds(rgb);
    while(1){
        uint8_t result = sliderserial_readreq(&reponse);
        if (result == 0x01){
        system("cls");
        for(uint8_t i=0;i<32;i++){
            printf("%x ",reponse.pressure[i]);
        }
        printf("true/n");

        }else if (result == 0xff){
            system("cls");
            printf("not complete/n");
        }else if(result == 0xfd){
            system("cls");
            printf("checksum error/n");
        }
        else if(result == 0){
           system("cls");
           printf("no command/n");
                   for(uint8_t i=0;i<128;i++){
            printf("%x ",reponse.data[i]);
        }
        }
        else{
            system("cls");
            printf("unknow:%x /n",result);
        }
        // for(uint8_t i=0;i<36;i++){
        //     printf("%x ",reponse.data[i]);
        // }
    Sleep(1000);
    }
}