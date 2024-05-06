#include "serialslider.h"

#define comPort "COM1"
// Global state
HANDLE hPort; // 串口句柄
DCB dcb; // 串口参数结构体
COMMTIMEOUTS timeouts; // 串口超时结构体
OVERLAPPED ovWrite;
BOOL fWaitingOnRead = FALSE, fWaitingOnWrite = FALSE;

#define READ_BUF_SIZE 256
#define READ_TIMEOUT 500

//char comPort[10];

// Windows Serial helpers
BOOL open_port()
{
    // 打开串口
    hPort = CreateFile(comPort, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hPort == INVALID_HANDLE_VALUE)
    {
        printf("can't open %s!\n", comPort);
        return FALSE;
    }

    // 获取串口参数
    GetCommState(hPort, &dcb);
    // 设置串口参数
    dcb.BaudRate = 115200; // 设置波特率
    dcb.ByteSize = 8; // 设置数据位为8
    dcb.Parity = NOPARITY; // 设置无奇偶校验
    dcb.StopBits = ONESTOPBIT; // 设置停止位为1
    SetCommState(hPort, &dcb);

    // 获取串口超时
    GetCommTimeouts(hPort, &timeouts);

    // 设置串口超时
    timeouts.ReadIntervalTimeout = 1; // 设置读取间隔超时为1毫秒
    timeouts.ReadTotalTimeoutConstant = 100; // 设置读取总超时常量为10毫秒
    timeouts.ReadTotalTimeoutMultiplier = 10; // 设置读取总超时乘数为10毫秒
    timeouts.WriteTotalTimeoutConstant = 1000; // 设置写入总超时常量为1000毫秒
    timeouts.WriteTotalTimeoutMultiplier = 10; // 设置写入总超时乘数为10毫秒
    SetCommTimeouts(hPort, &timeouts);
	EscapeCommFunction(hPort,5); //发送DTR信号
	EscapeCommFunction(hPort,3); //发送RTS信号
    // 返回成功
    return TRUE;
}

void package_init(slider_packet_t *request){
	for(uint8_t i = 0;i< BUFSIZE;i++){
		request->data[i] = 0;
	}
}

BOOL send_data(int length,uint8_t *send_buffer)
{
    DWORD bytes_written; // 写入的字节数
	// while (fWaitingOnWrite) {
    //     // Already waiting for a write to complete.
    // }
    // // Write data to the port asynchronously.
    // fWaitingOnWrite = TRUE;
    // 写入数据
    if (!WriteFile(hPort, send_buffer, length, &bytes_written, NULL))
    {
        printf("can't write data to");
		printf(comPort);
		printf("/n");
        return FALSE;
    }
    // 返回成功
	//WriteFile(hPort, send_buffer, length, &bytes_written, &ovWrite);
    return TRUE;
}

static uint32_t millis() {
	return GetTickCount();
}

void sliderserial_writeresp(slider_packet_t *request) {
	uint8_t checksum = 0 - request->syn - request->cmd - request->size; 
	uint8_t length = request->size + 4;
	for (uint8_t i = 0;i<request->size;i++){
		checksum -= request->data[i+3];
		if((request->data[i+3] == 0xff) && (request->data[i+3] == 0xfd)){
			request->data[i+3] = 0xfe;
		}
	}
	request->data[request->size+3] = checksum;
	// if((request.checksum[0] == 0xff) && (request.checksum[0] == 0xfd)){
	// 	request.checksum[0] = 0xfd;
	// 	request.checksum[1] = 0xfe;
	// 	uint8_t length = request.size + 4;
	// }
	send_data(length,request->data);
}

// int read_serial_port(LPVOID lpBuf, DWORD dwRead) {
//     BOOL fWaitingOnRead = FALSE;
//     OVERLAPPED osReader = {0};

//     // 创建用于读取的重叠结构体的事件对象
//     osReader.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

//     if (osReader.hEvent == NULL){
//         return 0; // 错误处理: 创建事件失败
// 	}
//     if (!fWaitingOnRead) {
//         // 发出读取请求
//         if (!ReadFile(hPort, lpBuf, sizeof(lpBuf)-1, &dwRead, &osReader)) {
//             if (GetLastError() != ERROR_IO_PENDING)     // 读取没有挂起
//                 return 0; // 错误处理: 读取失败
//             else
//                 fWaitingOnRead = TRUE;
//         }
//         else {    
//             // 读取完成
//             return 1;
//         }
//     }

//     DWORD dwRes;

//     if (fWaitingOnRead) {
//         dwRes = WaitForSingleObject(osReader.hEvent, READ_TIMEOUT);
//         switch(dwRes)
//         {
//             // 读取完成
//             case WAIT_OBJECT_0:
//                 if (!GetOverlappedResult(hPort, &osReader, &dwRead, FALSE))
//                     return 0; // 错误处理: 读取失败
//                 else
//                     return 1; // 读取完成
//                 	fWaitingOnRead = FALSE;
//                 break;

//             case WAIT_TIMEOUT:
//                 return 2; // 操作未完成，读取挂起
//                 break;                       

//             default:
//                 return 0; // 错误处理: WaitForSingleObject失败
//                 break;
//         }
//     }
//     return 0;
// }

uint8_t sliderserial_readreq(slider_packet_t *reponse) {
	package_init(reponse);
	uint8_t checksum = 0;
	uint8_t rep_size = 0;
	long unsigned int recv_len;
	uint8_t buffer[BUFSIZE] = {0};
	BOOL ESC = FALSE;
	uint8_t revice_complete_retry = 2;
	PurgeComm(hPort, PURGE_RXCLEAR );
	// uint8_t result = read_serial_port(buffer, recv_len) ;
	// while(result == 0 && result == 2){
	// 	result = read_serial_port(buffer, recv_len);
	// }
	while(revice_complete_retry){
		uint8_t result = ReadFile(hPort,  buffer, BUFSIZE-1, &recv_len, NULL);
		if (result && recv_len){
			for(uint8_t i = 0;i<recv_len;i++){
				uint8_t c = buffer[i];
				if(c == 0xff){
					package_init(reponse);
					rep_size = 0;
					reponse->syn = c;
					checksum += reponse->syn;
					continue;
				}
				if(reponse->syn == 0){
					continue;
				}
				if(c == 0xfd){
					ESC = TRUE;
					continue;
				}
				if(ESC){
					c ++;
					ESC = FALSE;
				}

				if(reponse->cmd == 0){
					reponse->cmd = c;
					checksum += reponse->cmd;
					continue;
				}
				else if(rep_size == 0){
					reponse->size = c;
					rep_size = 3;
					checksum += reponse->size;
					continue;
				}
				reponse->data[rep_size] = c;
				checksum += c;
				if ((rep_size == reponse->size + 4) && (rep_size >128) ){
					return reponse->cmd;
				}
				rep_size++;
			}
		}
		revice_complete_retry --;
	}
	return reponse->cmd;
}

void slider_rst(){
	package_init(&request);
	request.syn = 0xff;
	request.cmd = SLIDER_CMD_RESET;
	request.size = 0;
	sliderserial_writeresp(&request);
	Sleep(1);
}

void slider_start_scan(){
	package_init(&request);
	request.syn = 0xff;
	request.cmd = SLIDER_CMD_AUTO_SCAN_START;
	request.size = 0;
	sliderserial_writeresp(&request);
	Sleep(1);
}

void slider_stop_scan(){
	package_init(&request);
	request.syn = 0xff;
	request.cmd = SLIDER_CMD_AUTO_SCAN_STOP;
	request.size = 0;
	sliderserial_writeresp(&request);
	Sleep(1);
}

void slider_send_leds(const uint8_t *rgb){
	package_init(&request);
	request.syn = 0xff;
	request.cmd = SLIDER_CMD_SET_LED;
	request.size = 96;
	memcpy(request.leds, rgb, 96);
	sliderserial_writeresp(&request);
	Sleep(3);
}

void slider_send_air_leds_left(const uint8_t *rgb){
	package_init(&request);
	request.syn = 0xff;
	request.cmd = SLIDER_CMD_SET_AIR_LED_LEFT;
	request.size = 9;
	memcpy(request.air_leds, rgb, 9);
	sliderserial_writeresp(&request);
	Sleep(1);
}

void slider_send_air_leds_right(const uint8_t *rgb){
	package_init(&request);
	request.syn = 0xff;
	request.cmd = SLIDER_CMD_SET_AIR_LED_RIGHT;
	request.size = 9;
	memcpy(request.air_leds, rgb, 9);
	sliderserial_writeresp(&request);
	Sleep(1);
}