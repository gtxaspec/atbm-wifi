#ifndef ATBM_TOOL_H
#define ATBM_TOOL_H

#define __packed __attribute__((packed))

#define BIT(nr)			(1UL << (nr))

#define FILTER_F_ARP 	BIT(0)
#define FILTER_F_ICMP 	BIT(1)
#define FILTER_F_DNS 	BIT(2)


typedef signed char s8;

typedef unsigned char u8;

typedef signed short s16;

typedef unsigned short u16;

typedef signed int s32;

typedef unsigned int u32;

typedef signed long long s64;

typedef unsigned long long u64;


#define CMD_LINE_LEN             	1600

#define ATBM_IOCTL          		(121)
#define ATBM_AT_CMD_DIRECT        	_IOW(ATBM_IOCTL,0, unsigned int)

#define MAX_SYNC_EVENT_BUFFER_LEN 	512

#define ATBM_SMT_STATUS_STOP		1
#define ATBM_SMT_STATUS_MSG			2

#define SER_SOCKET_PATH          	"/tmp/server_socket"


#define BLE_SMART_CFG_STATUS_SUCESS			0
#define BLE_SMART_CFG_STATUS_END				0xFF
#define BLE_SMART_CFG_STATUS_BUSY				0xF0
#define BLE_SMART_CFG_STATUS_TIMEOUT			0xF1
#define BLE_SMART_CFG_STATUS_SSID_LEN_ERR		0xF2
#define BLE_SMART_CFG_STATUS_SSID_ERR			0xF3
#define BLE_SMART_CFG_STATUS_PWD_LEN_ERR		0xF4
#define BLE_SMART_CFG_STATUS_PWD_ERR			0xF5
#define BLE_SMART_CFG_STATUS_STOP				0xF6


struct status_async{
	u8 type; /* 0: connect msg, 1: driver msg, 2:scan complete, 3:wakeup host reason, 4:disconnect reason, 5:connect fail reason, 6:customer event*/
	u8 driver_mode; /* TYPE1 0: rmmod, 1: insmod; TYPE3\4\5 reason */
	u8 list_empty;
	u8 event_buffer[MAX_SYNC_EVENT_BUFFER_LEN];
};

struct wsm_ble_smt_ind {
	u32 status;
	u8 ssid[32];
	u8 pwd[64];
	u8 ssid_len;
	u8 pwd_len;
};


#endif /* ATBM_TOOL_H */

