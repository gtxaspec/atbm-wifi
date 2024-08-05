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
#include "atbm_tool.h"


static sem_t sem;
static sem_t sem_status;
static sem_t sem_sock_sync;
static int atbm_fp = -1;
static struct status_async status;
static char gCmdStr[512];
static int atbm_smt_status = 0;
static struct wsm_ble_smt_ind ble_smt;

char cmd_line[CMD_LINE_LEN];

int change_driver_to_ble(char *driverName)
{
	int flags = 0;
	char *str;
	int len;
	char name[128] = {0};
	if (driverName && (strcmp(driverName, "NULL"))) {
	str = strstr(driverName, ".ko");
		if (str) {
		len = str - driverName;
			if (len >= 127) {
			fprintf(stdout, "driverName is too large\n");
			return -1;
		}
		memcpy(name, driverName, len);
		sprintf(gCmdStr, "rmmod %s", name);
	}
		else {
	    sprintf(gCmdStr, "rmmod %s", driverName);
	}
    system(gCmdStr);
	usleep(2000000);	
	
    sprintf(gCmdStr, "insmod %s", driverName);	
    system(gCmdStr);
	usleep(3000000);
	usleep(3000000);
	}
	atbm_fp = open("/dev/atbm_ioctl", O_RDWR);
	if (atbm_fp < 0){
		fprintf(stdout,"open /dev/atbm_ioctl fail.\n");
		return -1;
	}
	fcntl(atbm_fp, F_SETOWN, getpid());
	flags = fcntl(atbm_fp, F_GETFL); 
	fcntl(atbm_fp, F_SETFL, flags | FASYNC);
	
	return 0;
}


int rmmod_driver(char *driverName)
{
	char *str;
	int len;
	char name[128] = {0};

	str = strstr(driverName, ".ko");
	if(str){
		len = str - driverName;
		if(len >= 127){
			fprintf(stdout, "driverName is too large\n");
			return -1;
		}
		memcpy(name, driverName, len);
		sprintf(gCmdStr, "rmmod %s", name);
	}
	else{
	    sprintf(gCmdStr, "rmmod %s", driverName);
	}
    system(gCmdStr);
	usleep(2000000);

	return 0;
}


int change_driver_to_wifi(char *driverName)
{
	char *str;
	int len;
	char name[128] = {0};

	if(atbm_fp > 0){
		close(atbm_fp);
		atbm_fp = -1;
		usleep(1000000);
	}
	
	str = strstr(driverName, ".ko");
	if(str){
		len = str - driverName;
		if(len >= 127){
			fprintf(stdout,"driverName is too large\n");
			return -1;
		}
		memcpy(name, driverName, len);
		sprintf(gCmdStr, "rmmod %s", name);
	}
	else{
	    sprintf(gCmdStr, "rmmod %s", driverName);
	}
    system(gCmdStr);
	usleep(2000000);

    sprintf(gCmdStr, "insmod %s", driverName);	
    system(gCmdStr);
	usleep(3000000);

	return 0;
}

void ioctl_msg_func(int sig_num)
{
	int len = 0;

	sem_wait(&sem_status);
	while (1)
	{
		memset(&status, 0, sizeof(struct status_async));
		len = read(atbm_fp, &status, sizeof(struct status_async));
		if (len < (int)(sizeof(status))){
			fprintf(stdout,"Line:%d read connect stat error.\n", __LINE__);
			break;
		}
		else{
			switch(status.type)
			{
				case 0:
					memcpy(&ble_smt, status.event_buffer, sizeof(struct wsm_ble_smt_ind));
					fprintf(stdout,"smartcfg ssid:%s, pwd:%s\n", ble_smt.ssid, ble_smt.pwd);
					break;
				default:
					break;
			}
		}
		
		if (status.list_empty)
		{
			break;
		}
	}
	sem_post(&sem_status);
}

void connect_wifi_ap(u8 *ssid, u8 ssidLen, u8 *pwd, u8 pwdLen)
{
	u8 ssidStr[33] = {0};
	u8 pwdStr[65] = {0};
	int sret, i;

	if(ssidLen > 32 || pwdLen > 64){
		fprintf(stdout,"ssidLen(%d) pwdLen(%d) error!\n", ssidLen, pwdLen);
		return;
	}

	memcpy(ssidStr, ssid, ssidLen);
	memcpy(pwdStr, pwd, pwdLen);

	if(pwdLen)
		sprintf(gCmdStr, "/tmp/connect_ap.sh %s %s",ssid,pwd);
	else
		sprintf(gCmdStr, "/tmp/connect_ap.sh %s",ssid);

	printf(

"start CMD:%s \n",gCmdStr);
	system(gCmdStr);
	
/*	
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
	fprintf(stdout,"%s\n", gCmdStr);	

	if(pwdLen > 0){
	    sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 psk '\"%s\"'", pwdStr);
	    sret = system(gCmdStr);
		fprintf(stdout,"%s\n", gCmdStr);
	}
	else{
	    sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 key_mgmt NONE");
	    sret = system(gCmdStr);
		fprintf(stdout,"%s\n", gCmdStr);
		for(i = 0; i < 4; i++)
		{
			sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 wep_key%i \"\"", i);
			sret = system(gCmdStr);
		}
	}

    sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 scan_ssid 1", ssidStr);
    sret = system(gCmdStr);
	
    sprintf(gCmdStr, "wpa_cli -i wlan0 select_network 0");
    sret = system(gCmdStr);
    */
}

struct at_cmd_direct_info{
	u32 len;
	u8 cmd[1500];
};

int at_cmd_direct(int fp, char *arg)
{
	struct at_cmd_direct_info at_cmd;

	memset(&at_cmd, 0, sizeof(at_cmd));
	at_cmd.len = strlen(arg);
	memcpy(at_cmd.cmd, arg, at_cmd.len);
	return ioctl(fp, ATBM_AT_CMD_DIRECT, (unsigned int)(&at_cmd));	
}

void *get_command_func(void *arg)
{
	struct sockaddr_un ser_un;
	int socket_fd = 0;
	int connect_fd = 0;
	int ret = 0;
	char recall[]="OK";

	socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (socket_fd <= 0)
	{
		fprintf(stdout,"open socket err\n");
		return;
	}

	unlink(SER_SOCKET_PATH);

	memset(&ser_un, 0, sizeof(ser_un));  
    ser_un.sun_family = AF_UNIX;  
	strcpy(ser_un.sun_path, SER_SOCKET_PATH);
    ret = bind(socket_fd, (struct sockaddr *)&ser_un, sizeof(struct sockaddr_un));
    if (ret < 0)
	{
		fprintf(stdout,"bind err\n"); 
	   return;
    }  

	ret = listen(socket_fd, 5);
    if (ret < 0) 
	{  
        fprintf(stdout,"listen err\n"); 
	   return;          
    }

	while (1)
	{
		connect_fd = accept(socket_fd, NULL, NULL);
		if (connect_fd < 0)
		{
			continue;
		}

		memset(cmd_line, 0, sizeof(cmd_line));
		while (1)
		{
			ret = read(connect_fd, cmd_line, sizeof(cmd_line));
			if (ret > 0)
			{
				break;
			}
		}
		
		write(connect_fd, recall, strlen(recall)+1);
		close(connect_fd);
		fprintf(stdout,"cmd_line: %s\n", cmd_line);
		sem_post(&sem_sock_sync);

		if (!strcmp(cmd_line,"quit"))
		{
			break;
		}
	}

	close(socket_fd);
}

/*
atbm_tool drivername.ko

*/
int main(int argc, char* argv[])
{
	char *driverName;
	int ret = -1;
	pthread_t sock_tid;

	if(argc < 1){
		fprintf(stdout,"Usage: %s <wifi driver name xxx.ko>\n", argv[0]);
		return -1;
	}
	
	atbm_fp = -1;
	sem_init(&sem, 0, 1);
	sem_init(&sem_status, 0, 1);
	sem_init(&sem_sock_sync, 0, 0);

	signal(SIGIO, ioctl_msg_func);

	driverName = argv[1];
	change_driver_to_ble(driverName);

	atbm_smt_status = 0;

	pthread_create(&sock_tid, NULL, get_command_func, NULL);

	for (;;)
	{
		sem_wait(&sem_sock_sync);
		if (!strcmp(cmd_line, "quit"))
		{
			break;
		}
		else
		{
			sem_wait(&sem);
			at_cmd_direct(atbm_fp, cmd_line);
			sem_post(&sem);
		}
	}

config_err:
	pthread_join(sock_tid, NULL);
	pthread_cancel(sock_tid);
	sem_destroy(&sem_status);
	sem_destroy(&sem_sock_sync);
	if(atbm_fp > 0){
		close(atbm_fp);
	}
	fprintf(stdout,"exit ble_smt process\n");
	if (strcmp(driverName, "NULL")) {
	rmmod_driver(driverName);
	}

	return 0;
}





