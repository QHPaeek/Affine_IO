#include "serial.h"

#define READ_BUF_SIZE 256
#define READ_TIMEOUT 500

struct SerialPort SerialPort1;
struct SerialPort SerialPort2;

void initializeSerialPort(struct SerialPort *port, const char *comPort) {
    strcpy(port->comPort, comPort);
    port->fWaitingOnRead = FALSE;
    port->fWaitingOnWrite = FALSE;
    // Initialize other members as needed
}

//
// Windows Serial helpers
BOOL open_port(struct SerialPort *port)
{
    // 打开串口
    port->hPort = CreateFile(port->comPort, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (port->hPort == INVALID_HANDLE_VALUE)
    {
        //printf("can't open %s!\n", comPort);
        return FALSE;
    }

    // 获取串口参数
    GetCommState(port->hPort, &port->dcb);
    // 设置串口参数
    port->dcb.BaudRate = 115200; // 设置波特率
    port->dcb.ByteSize = 8; // 设置数据位为8
    port->dcb.Parity = NOPARITY; // 设置无奇偶校验
    port->dcb.StopBits = ONESTOPBIT; // 设置停止位为1
    SetCommState(port->hPort, &port->dcb);

    // 获取串口超时
    GetCommTimeouts(port->hPort, &port->timeouts);

    // 设置串口超时
    port->timeouts.ReadIntervalTimeout = 1; // 设置读取间隔超时为1毫秒
    port->timeouts.ReadTotalTimeoutConstant = 5; // 设置读取总超时常量为5毫秒
    port->timeouts.ReadTotalTimeoutMultiplier = 1; // 设置读取总超时乘数为1毫秒
    port->timeouts.WriteTotalTimeoutConstant = 100; // 设置写入总超时常量为100毫秒
    port->timeouts.WriteTotalTimeoutMultiplier = 10; // 设置写入总超时乘数为10毫秒
    SetCommTimeouts(port->hPort, &port->timeouts);
	EscapeCommFunction(port->hPort,5); //发送DTR信号
	//EscapeCommFunction(hPort,3); //发送RTS信号
    // 返回成功
    return TRUE;
}

void close_port(struct SerialPort *port){
	CloseHandle(port->hPort);
	port->hPort = INVALID_HANDLE_VALUE;
}

// 检查串口是否打开
// BOOL IsSerialPortOpen(struct SerialPort *port) {
//     DWORD errors;
//     COMSTAT status;

//     ClearCommError(port->hPort, &errors, &status);
//     if (errors > 0) {
//         return FALSE;
//     }

//     return TRUE;
// }

void package_init(serial_packet_t *request){
	for(uint8_t i = 0;i< BUFSIZE;i++){
		request->data[i] = 0;
	}
}

BOOL send_data(struct SerialPort *port,int length,uint8_t *send_buffer)
{
    DWORD bytes_written; // 写入的字节数
	// while (fWaitingOnWrite) {
    //     // Already waiting for a write to complete.
    // }
    // // Write data to the port asynchronously.
    // fWaitingOnWrite = TRUE;
    // 写入数据
    if (!WriteFile(port->hPort, send_buffer, length, &bytes_written, NULL))
    {
        return FALSE;
    }
    // 返回成功
    return TRUE;
}

void sliderserial_writeresp(struct SerialPort *port,serial_packet_t *request) {
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
	send_data(port,length,request->data);
}


BOOL serial_read1(struct SerialPort *port,uint8_t *result){
	long unsigned int recv_len;
	if (ReadFile(port->hPort,  result, 1, &recv_len, NULL) && (recv_len != 0)){
		return TRUE;
	}
	else{
		return false;
	}
	
}

uint8_t serial_read_cmd(struct SerialPort *port,serial_packet_t *reponse){
	uint8_t checksum = 0;
	uint8_t rep_size = 0;
	BOOL ESC = FALSE;
	uint8_t c;
	COMSTAT comStat;
	DWORD   dwErrors = 0;
	while(serial_read1(port,&c)){
		if(c == 0xff){
			package_init(reponse);
			rep_size = 0;
			reponse->syn = c;
			ESC = FALSE;
			checksum += 0xff;
			continue;
			}
		if(reponse->syn != 0xff){
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
		if(rep_size == 0){
			reponse->size = c;
			rep_size = 3;
			checksum += reponse->size;
			continue;
		}
		reponse->data[rep_size] = c;
		checksum += c;
		if ((rep_size == reponse->size + 3) || (rep_size >128) ){
			return reponse->cmd;
		}
		rep_size++;
	}
	if (!GetCommState(port->hPort, &port->dcb)) {
    // 串口已断开
	return 0xff;
	}
	return 0xfe;
}

void keypad_rst(struct SerialPort *port){
	package_init(&port->request);
	port->request.syn = 0xff;
	port->request.cmd = SERIAL_CMD_RESET;
	port->request.size = 0;
	sliderserial_writeresp(port,&port->request);
	//Sleep(1);
}

void keypad_start_scan(struct SerialPort *port){
	package_init(&port->request);
	port->request.syn = 0xff;
	port->request.cmd = SERIAL_CMD_AUTO_SCAN_START;
	port->request.size = 0;
	sliderserial_writeresp(port,&port->request);
	//Sleep(1);
}

void keypad_stop_scan(struct SerialPort *port){
	package_init(&port->request);
	port->request.syn = 0xff;
	port->request.cmd = SERIAL_CMD_AUTO_SCAN_STOP;
	port->request.size = 0;
	sliderserial_writeresp(port,&port->request);
	//Sleep(1);
}