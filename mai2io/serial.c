#include "serial.h"

#define READ_BUF_SIZE 256
#define READ_TIMEOUT 500
extern HANDLE hPort1; // 串口句柄
extern HANDLE hPort2; // 串口句柄
// Windows Serial helpers
BOOL open_port(HANDLE *hPortx,char* comPortx)
{
    // 打开串口
    *hPortx = CreateFile(comPortx, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (*hPortx == INVALID_HANDLE_VALUE)
    {
        //printf("can't open %s!\n", comPort);
        return FALSE;
    }

    //获取串口参数
    GetCommState(*hPortx, &dcb);
    // 设置串口参数
    dcb.BaudRate = 115200; // 设置波特率
    dcb.ByteSize = 8; // 设置数据位为8
    dcb.Parity = NOPARITY; // 设置无奇偶校验
    dcb.StopBits = ONESTOPBIT; // 设置停止位为1
    SetCommState(*hPortx, &dcb);

    // 获取串口超时
    GetCommTimeouts(*hPortx, &timeouts);

    // 设置串口超时
    timeouts.ReadIntervalTimeout = 1; // 设置读取间隔超时为1毫秒
    timeouts.ReadTotalTimeoutConstant = 5; // 设置读取总超时常量为5毫秒
    timeouts.ReadTotalTimeoutMultiplier = 1; // 设置读取总超时乘数为1毫秒
    timeouts.WriteTotalTimeoutConstant = 100; // 设置写入总超时常量为100毫秒
    timeouts.WriteTotalTimeoutMultiplier = 10; // 设置写入总超时乘数为10毫秒
    SetCommTimeouts(*hPortx, &timeouts);
	EscapeCommFunction(*hPortx,5); //发送DTR信号
	//EscapeCommFunction(hPort,3); //发送RTS信号
    // 返回成功

    return TRUE;
}

void close_port(HANDLE *hPortx){
	CloseHandle(hPortx);
	hPortx = INVALID_HANDLE_VALUE;
}

// 检查串口是否打开
BOOL IsSerialPortOpen(HANDLE *hPortx) {
    return GetCommState(*hPortx, &dcb);
}

void package_init(serial_packet_t *rsponse){
	for(uint8_t i = 0;i< BUFSIZE;i++){
		rsponse->data[i] = 0;
	}
}

BOOL send_data(HANDLE *hPortx,int length,uint8_t *send_buffer)
{
    DWORD bytes_written; // 写入的字节数
	// while (fWaitingOnWrite) {
    //     // Already waiting for a write to complete.
    // }
    // // Write data to the port asynchronously.
    // fWaitingOnWrite = TRUE;
    // 写入数据
    if (!WriteFile(*hPortx, send_buffer, length, &bytes_written, NULL))
    {
        // printf("can't write data to");
		// printf(comPort);
		// printf("/n");
        return FALSE;
    }
    // 返回成功
	//WriteFile(hPort, send_buffer, length, &bytes_written, &ovWrite);
    return TRUE;
}

void serial_writeresp(HANDLE *hPortx,serial_packet_t *rsponse) {
	uint8_t checksum = 0 - rsponse->syn - rsponse->cmd - rsponse->size; 
	uint8_t length = rsponse->size + 4;
	for (uint8_t i = 0;i<rsponse->size;i++){
		checksum -= rsponse->data[i+3];
		if((rsponse->data[i+3] == 0xff) && (rsponse->data[i+3] == 0xfd)){
			rsponse->data[i+3] = 0xfe;
		}
	}
	rsponse->data[rsponse->size+3] = checksum;
	send_data(hPortx,length,rsponse->data);
}

BOOL serial_read1(HANDLE *hPortx,uint8_t *result){
	long unsigned int recv_len;
	if (ReadFile(*hPortx,  result, 1, &recv_len, NULL) && (recv_len != 0)){
		return TRUE;
	}
	else{
		return false;
	}
	
}

uint8_t serial_read_cmd(HANDLE *hPortx,serial_packet_t *request){
	uint8_t checksum = 0;
	uint8_t rep_size = 0;
	BOOL ESC = FALSE;
	uint8_t c;
	while(serial_read1(hPortx,&c)){
		if(c == 0xff){
			package_init(request);
			rep_size = 0;
			request->syn = c;
			ESC = FALSE;
			checksum += 0xff;
			continue;
			}
		if(request->syn != 0xff){
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
		if(request->cmd == 0){
			request->cmd = c;
			checksum += request->cmd;
			continue;
		}
		if(rep_size == 0){
			request->size = c;
			rep_size = 3;
			checksum += request->size;
			continue;
		}
		request->data[rep_size] = c;
		checksum += c;
		if ((rep_size == request->size + 3) || (rep_size >128) ){
			return request->cmd;
		}
		rep_size++;
	}
	// if (!GetCommState(hPortx, &dcb)) {
    // // 串口已断开
	// return 0xff;
	// }
	return 0xfe;
}

void serial_heart_beat(HANDLE *hPortx,serial_packet_t *rsponse){
	package_init(rsponse);
	rsponse->syn = 0xff;
	rsponse->cmd = SERIAL_CMD_HEART_BEAT;
	rsponse->size = 0;
	serial_writeresp(hPortx,rsponse);
	//Sleep(3);
}