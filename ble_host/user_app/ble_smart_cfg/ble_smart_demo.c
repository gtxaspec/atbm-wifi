#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>
#include <asm/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "ble_smart_cfg.h"


typedef signed char s8;

typedef unsigned char u8;

typedef signed short s16;

typedef unsigned short u16;

typedef signed int s32;

typedef unsigned int u32;

typedef signed long long s64;

typedef unsigned long long u64;


#define MAX_SYNC_EVENT_BUFFER_LEN 	512

#define ATBM_SMT_STATUS_STOP		1
#define ATBM_SMT_STATUS_MSG			2


int cfg_done_connected;


extern char connect_ap[64];
struct status_async
{
	u8 type; /* 0: connect msg, 1: driver msg, 2:scan complete, 3:wakeup host reason, 4:disconnect reason, 5:connect fail reason, 6:customer event*/
	u8 driver_mode; /* TYPE1 0: rmmod, 1: insmod; TYPE3\4\5 reason */
	u8 list_empty;
	u8 event_buffer[MAX_SYNC_EVENT_BUFFER_LEN];
};


struct at_cmd_direct_info
{
	u32 len;
	u8 cmd[1500];
};

static struct wsm_ble_smt_ind ble_smt;
static char gCmdStr[512];

#if 0
int console_scanf(char* s, int len)
{
	int i = 0, err = 0;
	if (NULL == s)
		return -1;
	char ch = '\0';
	while ((ch = getchar()) != '\n') {
		if (i < (len - 1)) {
			s[i] = ch;
			i++;
		}
		else {
			err = 1; //不跳出循环，读取完所有该次输入的内容
		}
	}
	s[i] = '\0'; //确保是以'\0'结尾
	return err;  //获取成功：0，实参为空指针：-1，输入的长度超出指定的长度：1.
}
#endif


#if 0
void* get_console_func(void* arg)
{
	int ret = -1;

	while (1) {
		memset(gCmdStr, 0, 512);
		ret = console_scanf(gCmdStr, 512);
		if (ret == 0) {
			if (strlen(gCmdStr) > 0) {
				atbm_smt_status = 1;
				sem_post(&sem_sock_sync);
				if (!strcmp(gCmdStr, "ble_smart_stop")) {
					break;
				}
			}
		}
		usleep(100000);
	}
}
#endif

#if 0
void connect_wifi_ap(u8* ssid, u8 ssidLen, u8* pwd, u8 pwdLen)
{
	u8 ssidStr[33] = { 0 };
	u8 pwdStr[65] = { 0 };
	int sret, i;

	if (ssidLen > 32 || pwdLen > 64) {
		fprintf(stdout, "ssidLen(%d) pwdLen(%d) error!\n", ssidLen, pwdLen);
		return;
	}

	memcpy(ssidStr, ssid, ssidLen);
	memcpy(pwdStr, pwd, pwdLen);

	sprintf(gCmdStr, "ifconfig wlan0 down");
	sret = system(gCmdStr);
	usleep(100000);

	sprintf(gCmdStr, "killall wpa_supplicant");
	sret = system(gCmdStr);
	usleep(100000);

	sprintf(gCmdStr, "wpa_supplicant -D nl80211 -i wlan0 -c /usr/wpa_cfg/wpa_supplicant.conf -B");
	sret = system(gCmdStr);
	usleep(100000);

	sprintf(gCmdStr, "ifconfig wlan0 up");
	sret = system(gCmdStr);
	usleep(1000000);

	sprintf(gCmdStr, "wpa_cli -i wlan0 add_network");
	sret = system(gCmdStr);

	sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 scan_ssid 1", ssidStr);
	sret = system(gCmdStr);

	sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 ssid '\"%s\"'", ssidStr);
	sret = system(gCmdStr);
	fprintf(stdout, "%s\n", gCmdStr);

	sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 mode 0");
	sret = system(gCmdStr);

	if (pwdLen > 0) {
		sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 key_mgmt WPA-PSK");
		sret = system(gCmdStr);
		fprintf(stdout, "%s\n", gCmdStr);
		sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 psk '\"%s\"'", pwdStr);
		sret = system(gCmdStr);
		fprintf(stdout, "%s\n", gCmdStr);
	}
	else {
		sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 key_mgmt NONE");
		sret = system(gCmdStr);
		fprintf(stdout, "%s\n", gCmdStr);
		for (i = 0; i < 4; i++) {
			sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 wep_key%i \"\"", i);
			sret = system(gCmdStr);
		}
	}

	sprintf(gCmdStr, "wpa_cli -i wlan0 enable_network 0");
	sret = system(gCmdStr);
}
#endif

char* cmd_system(const char* command,char * buf,int buff_len)
{
    char* result = NULL;
    FILE *fpRead;
    fpRead = popen(command, "r");
	if(buf && (buff_len > 1)){
  //  char buf[1024];
	    memset(buf,'\0',sizeof(buf));
	    while(fgets(buf,buff_len-1,fpRead)!=NULL)
	    { 
			result=buf;
	    }
	}
    if(fpRead != NULL)
        pclose(fpRead);

    return result;

}


int smt_check_wpa_status()
{
#if 0
	int result = 0;
	
	int wpa_success = 0;
	char tmpstring[50];
	char* wpa_state = "wpa_state=COMPLETED";

	result = system("wpa_cli status | tee /tmp/wpa_status.txt");
	
		
	fprintf(stdout, "check_wap_status result:%d\n", result);

	FILE* status_file = fopen("/tmp/wpa_status.txt", "r");
	if (status_file == NULL) {
		printf("Error no status file\n");
		exit(-1);
	}

	printf("check wpa for string %s\n", wpa_state);

	while (fscanf(status_file, "%s", tmpstring) == 1) {
		// printf("tmpstring %s\n", tmpstring);
		if (strstr(tmpstring, wpa_state) != 0) {//if match found
			{
				wpa_success = 1;
				//wifi_connected_completed = 1;
				break;
			}
		}
	}

	fclose(status_file);
#else
	int wpa_success = 0;
	char buff[32];
	cmd_system("iwpriv wlan0 common get_conn_state > /tmp/wpa_status.txt",NULL,0);
	cmd_system("cat /tmp/wpa_status.txt | grep wifi",buff,32); 
	sscanf(buff,"wifi_status=%d",&wpa_success);
#endif

	printf("wpa_success %d\n", wpa_success);
	return wpa_success;

}

void connect_wifi_ap(u8* ssid, u8 ssidLen, u8* pwd, u8 pwdLen)
{
	u8 ssidStr[33] = { 0 };
	u8 pwdStr[65] = { 0 };
	int sret, i;
	int conn_ap_len;
	if (ssidLen > 32 || pwdLen > 64) {
		fprintf(stdout, "ssidLen(%d) pwdLen(%d) error!\n", ssidLen, pwdLen);
		return;
	}

	memcpy(ssidStr, ssid, ssidLen);
	memcpy(pwdStr, pwd, pwdLen);

#if 1

	conn_ap_len = strlen(connect_ap);
	if(conn_ap_len){
		if(pwdLen)
			sprintf(gCmdStr, "%s %s %s",connect_ap,ssid,pwd);
		else
			sprintf(gCmdStr, "%s %s",connect_ap,ssid);
	}else{
		if(pwdLen)
			sprintf(gCmdStr, "/tmp/connect_ap.sh %s %s",ssid,pwd);
		else
			sprintf(gCmdStr, "/tmp/connect_ap.sh %s",ssid);
	}
	printf("start CMD:%s \n",gCmdStr);
	system(gCmdStr);
//	cmd_system(gCmdStr,NULL,0);
	
#else

	sprintf(gCmdStr, "ifconfig wlan0 down");
	sret = system(gCmdStr);
	usleep(100000);

	sprintf(gCmdStr, "killall wpa_supplicant");
	sret = system(gCmdStr);
	usleep(100000);

	sprintf(gCmdStr, "wpa_supplicant -D nl80211 -i wlan0 -c /usr/wpa_cfg/wpa_supplicant.conf -B");
	sret = system(gCmdStr);
	usleep(100000);

	sprintf(gCmdStr, "ifconfig wlan0 up");
	sret = system(gCmdStr);
	usleep(100000);

	sprintf(gCmdStr, "wpa_cli -i wlan0 remove_network 0");
	sret = system(gCmdStr);
	usleep(100000);

	sprintf(gCmdStr, "wpa_cli -i wlan0 ap_scan 1");
	sret = system(gCmdStr);
	usleep(100000);

	sprintf(gCmdStr, "wpa_cli -i wlan0 add_network");
	sret = system(gCmdStr);
	usleep(1000000);

	sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 ssid '\"%s\"'", ssidStr);
	sret = system(gCmdStr);
	fprintf(stdout, "%s\n", gCmdStr);

	if (pwdLen > 0) {
		sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 psk '\"%s\"'", pwdStr);
		sret = system(gCmdStr);
		fprintf(stdout, "%s\n", gCmdStr);
	}
	else {
		sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 key_mgmt NONE");
		sret = system(gCmdStr);
		fprintf(stdout, "%s\n", gCmdStr);
		for (i = 0; i < 4; i++) {
			sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 wep_key%i \"\"", i);
			sret = system(gCmdStr);
		}
	}

	sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 scan_ssid 1", ssidStr);
	sret = system(gCmdStr);

	sprintf(gCmdStr, "wpa_cli -i wlan0 select_network 0");
	sret = system(gCmdStr);
#endif
}


int get_wifi_wpa_status()
{
	fprintf(stdout, "get_wifi_wpa_status cfg_done_connected:%d\n" ,cfg_done_connected);
	return  cfg_done_connected;
}

int smt_demo_end_indicate(u8* event_buffer)
{
	int ret = -1;
	int result = 0;
	int i = 0;
	time_t broadcast_suc = 0;
	time_t broadcast_suc_end = 0;
	char buff[32];


	fprintf(stdout, "ble smart config end...\n");
	//atbm_smt_status = 0;
	cfg_done_connected = 0;


	do {
			memcpy(&ble_smt, event_buffer, sizeof(struct wsm_ble_smt_ind));

			if (ble_smt.status == BLE_SMART_CFG_SSID_PWD_TRANS_END) {

				broadcast_suc = time((time_t*)NULL);
				fprintf(stdout, "ble smart config ssid pwd trans Done(0x%X) ssid %s pwd %s\n", ble_smt.status, ble_smt.ssid, ble_smt.pwd);

				//change_driver_to_wifi(driverName_ble, driverName_wifi);
				connect_wifi_ap(ble_smt.ssid, ble_smt.ssid_len, ble_smt.pwd, ble_smt.pwd_len);
				
				cmd_system("iwpriv wlan0 common get_conn_state > /tmp/wpa_status.txt",NULL,0);
				cmd_system("cat /tmp/wpa_status.txt | grep wifi",buff,32); 
				sscanf(buff,"wifi_status=%d",&result);
				
				fprintf(stdout, "check_wap_status result:%d pwd:%s len:%d\n", result, ble_smt.pwd, ble_smt.pwd_len);

				goto config_err;
			}
			else if (ble_smt.status == BLE_SMART_CFG_STATUS_END) {

			//	result = system("wpa_cli status | tee /tmp/wpa_status.txt");
				cmd_system("iwpriv wlan0 common get_conn_state > /tmp/wpa_status.txt",NULL,0);
				cmd_system("cat /tmp/wpa_status.txt | grep wifi",buff,32); 
				sscanf(buff,"wifi_status=%d",&result);
				fprintf(stdout, " wap_status:%d\n", result);
				//fprintf(stdout,"ble smart config Done(0x%X)\n", ble_smt.status);
				//	goto config_err;
			}
			else if (ble_smt.status == BLE_SMART_CFG_STATUS_SUCESS) {
				//change_driver_to_wifi(driverName_ble, driverName_wifi);
				//connect_wifi_ap(ble_smt.ssid, ble_smt.ssid_len, ble_smt.pwd, ble_smt.pwd_len);
				fprintf(stdout, "ble smart config Done(0x%X)\n", ble_smt.status);
				goto config_err;

			}
			else {
				fprintf(stdout, "ble smart config err(0x%X)\n", ble_smt.status);
				//change_driver_to_wifi(driverName_ble, driverName_wifi);
				//goto config_err;
			}
	} while (0);

config_err:

	while (i < 10) {
		broadcast_suc_end = time((time_t*)NULL);
		cfg_done_connected = smt_check_wpa_status();
		if (((broadcast_suc_end - broadcast_suc) > 10) || (cfg_done_connected == 1)) {   //20 seconds timeout or recievd ack.
			fprintf(stdout, "connect_wifi_ap broadcast_suc:%d end:%d, cfg_done_connected:%d\n", broadcast_suc, broadcast_suc_end, cfg_done_connected);
			break;
		}
		sleep(1);
		i++;
	}

	if (cfg_done_connected) {
		fprintf(stdout, "exit ble_smt process wifi connected \n");
	}
	else {
		fprintf(stdout, "exit ble_smt process wifi failed to connect \n");
	}

	return 0;
}





