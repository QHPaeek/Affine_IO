#include "serial.h"
#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <SetupAPI.h>
#include "dprintf.h"
#include <tchar.h>

#pragma comment(lib, "setupapi.lib")

//#define DEBUG
#define READ_BUF_SIZE 256
#define READ_TIMEOUT 500
char comPort1[13]; //串口号
char comPort2[13]; //串口号
HANDLE hPort1; // 串口句柄
HANDLE hPort2; // 串口句柄
DCB dcb; // 串口参数结构体
COMMTIMEOUTS timeouts; // 串口超时结构体
OVERLAPPED ovWrite;
bool fWaitingOnRead;
bool fWaitingOnWrite;
serial_packet_t request1;
serial_packet_t response1;
serial_packet_t request2;
serial_packet_t response2;
bool Serial_Status;//串口状态（是否成功打开）

char* GetSerialPortByVidPid(const char* vid, const char* pid) {
    HDEVINFO deviceInfoSet;
    SP_DEVINFO_DATA deviceInfoData;
    DWORD i;
    char hardwareId[1024];
    static char portName[256];
	static char zero[10] = {0};

    // Get the device information set for all present devices
    deviceInfoSet = SetupDiGetClassDevs(NULL, "USB", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        return zero;
    }

    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    // Enumerate through all devices in the set
    for (i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); i++) {
        // Get the hardware ID
        if (SetupDiGetDeviceRegistryProperty(deviceInfoSet, &deviceInfoData, SPDRP_HARDWAREID, NULL, (PBYTE)hardwareId, sizeof(hardwareId), NULL)) {
            // Check if the hardware ID contains the VID and PID
            if (strstr(hardwareId, vid) && strstr(hardwareId, pid)) {
                // Get the port name
                HKEY hDeviceKey = SetupDiOpenDevRegKey(deviceInfoSet, &deviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
                if (hDeviceKey != INVALID_HANDLE_VALUE) {
                    DWORD portNameSize = sizeof(portName);
                    if (RegQueryValueEx(hDeviceKey, "PortName", NULL, NULL, (LPBYTE)portName, &portNameSize) == ERROR_SUCCESS) {
                        RegCloseKey(hDeviceKey);
                        SetupDiDestroyDeviceInfoList(deviceInfoSet);
                        return portName;
                    }
                    RegCloseKey(hDeviceKey);
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return zero;
}


BOOL open_port(HANDLE *hPortx ,char* comPortx) {
    // hPort1 = CreateFileA(comPort1, GENERIC_READ | GENERIC_WRITE, 0, NULL,
    //                      OPEN_EXISTING, 0, NULL);
	if (*hPortx != INVALID_HANDLE_VALUE) {
		CloseHandle(*hPortx);
	}
	*hPortx = CreateFile(comPortx, GENERIC_READ | GENERIC_WRITE, 0, NULL , OPEN_EXISTING, 0, NULL);
	if (*hPortx == INVALID_HANDLE_VALUE) {
		#ifdef DEBUG
		DWORD err = GetLastError();
		dprintf("Affine IO:CreateFile failed (Error %d)\n", err); // 输出具体错误码
		printf("Affine IO:CreateFile failed (Error %d)\n", err); 
		#endif
		CloseHandle(*hPortx);
		return FALSE;
	}
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(*hPortx, &dcb)) {
        CloseHandle(*hPortx);
        return FALSE;
    }

    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_ENABLE; // 启用DTR

    if (!SetCommState(*hPortx, &dcb)) {
        CloseHandle(*hPortx);
        return FALSE;
    }

    COMMTIMEOUTS timeouts = { 0 };
    if (!GetCommTimeouts(*hPortx, &timeouts)) {
        CloseHandle(*hPortx);
        return FALSE;
    }

	timeouts.ReadIntervalTimeout = 1; // 设置读取间隔超时为1毫秒
    timeouts.ReadTotalTimeoutConstant = 5; // 设置读取总超时常量为5毫秒
    timeouts.ReadTotalTimeoutMultiplier = 1; // 设置读取总超时乘数为1毫秒
    timeouts.WriteTotalTimeoutConstant = 100; // 设置写入总超时常量为100毫秒
    timeouts.WriteTotalTimeoutMultiplier = 10; // 设置写入总超时乘数为10毫秒

    if (!SetCommTimeouts(*hPortx, &timeouts)) {
        CloseHandle(*hPortx);
        return FALSE;
    }
	#ifdef DEBUG
	dprintf("Affine IO:Open port success\n");
	printf("Affine IO:Open port success\n");
	#endif
    return TRUE;
}
void close_port(HANDLE *hPortx){
	CloseHandle(*hPortx);
}

void package_init(serial_packet_t *rsponse){
	for(int i = 0;i< BUFSIZE;i++){
		rsponse->data[i] = 0;
	}
}

BOOL send_data(HANDLE hPortx,int length,uint8_t *send_buffer)
{
    DWORD bytes_written; // 写入的字节数
	// while (fWaitingOnWrite) {
    //     // Already waiting for a write to complete.
    // }
    // // Write data to the port asynchronously.
    // fWaitingOnWrite = TRUE;
    // 写入数据
    if (!WriteFile(hPortx, send_buffer, length, &bytes_written, NULL))
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

void serial_writeresp(HANDLE hPortx,serial_packet_t *rsponse) {
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

BOOL serial_read1(HANDLE hPortx,uint8_t *result){
	DWORD recv_len;
	if (ReadFile(hPortx,  result, 1, &recv_len, NULL) && (recv_len != 0)){
		return TRUE;
	}
	else{
		return FALSE;
	}
	
}

uint8_t serial_read_cmd(HANDLE hPortx,serial_packet_t *reponse){
	uint8_t checksum = 0;
	uint8_t rep_size = 0;
	BOOL ESC = FALSE;
	uint8_t c;
	COMSTAT comStat;
	DWORD   dwErrors = 0;
	while(serial_read1(hPortx,&c)){
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
	if (!GetCommState(hPortx, &dcb)) {
    // 串口已断开
	return 0xff;
	}
	return 0xfe;
}

void serial_heart_beat(HANDLE hPortx,serial_packet_t *rsponse){
	package_init(rsponse);
	rsponse->syn = 0xff;
	rsponse->cmd = SERIAL_CMD_HEART_BEAT;
	rsponse->size = 0;
	serial_writeresp(hPortx,rsponse);
	//Sleep(3);
}

// void serial_change_touch_threshold(HANDLE hPortx,serial_packet_t *rsponse,uint8_t *touch_threshold){
// 	package_init(rsponse);
// 	rsponse->syn = 0xff;
// 	rsponse->cmd = SERIAL_CMD_CHANGE_TOUCH_THRESHOLD;
// 	rsponse->size = 34;
// 	memcpy(rsponse->threshold,touch_threshold,34);
// 	serial_writeresp(hPortx,rsponse);
// 	//Sleep(3);
// }

void serial_scan_start(HANDLE hPortx,serial_packet_t *rsponse){
	package_init(rsponse);
	rsponse->syn = 0xff;
	rsponse->cmd = SERIAL_CMD_SCAN_START;
	rsponse->size = 0;
	serial_writeresp(hPortx,rsponse);
	//Sleep(3);
}

void serial_scan_stop(HANDLE hPortx,serial_packet_t *rsponse){
	package_init(rsponse);
	rsponse->syn = 0xff;
	rsponse->cmd = SERIAL_CMD_SCAN_STOP;
	rsponse->size = 0;
	serial_writeresp(hPortx,rsponse);
	//Sleep(3);
}