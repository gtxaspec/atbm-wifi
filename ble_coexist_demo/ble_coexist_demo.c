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
#include <time.h>

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

#define ATBM_IOCTL          			(121)

#define ATBM_BLE_COEXIST_START        	_IOW(ATBM_IOCTL,0, unsigned int)
#define ATBM_BLE_COEXIST_STOP        	_IOW(ATBM_IOCTL,1, unsigned int)
#define ATBM_BLE_SET_ADV_DATA        	_IOW(ATBM_IOCTL,2, unsigned int)
#define ATBM_BLE_ADV_RESP_MODE_START    _IOW(ATBM_IOCTL,3, unsigned int)
#define ATBM_BLE_SET_RESP_DATA     		_IOW(ATBM_IOCTL,4, unsigned int)
#define ATBM_BLE_HIF_TXDATA     		_IOW(ATBM_IOCTL,5, unsigned int)


#define BLE_ATBM_GAP_SVR					0xBBB0
#define BLE_ATBM_GAP_SVR1					0xBBB1

#define BLE_ATBM_GAP_SVR_BR					0xBAB0
#define BLE_ATBM_GAP_SVR_BR_SUC				0xBAB1


#define BLE_HS_ADV_TYPE_FLAGS                   0x01
#define BLE_HS_ADV_TYPE_INCOMP_UUIDS16          0x02
#define BLE_HS_ADV_TYPE_COMP_UUIDS16            0x03
#define BLE_HS_ADV_TYPE_INCOMP_UUIDS32          0x04
#define BLE_HS_ADV_TYPE_COMP_UUIDS32            0x05
#define BLE_HS_ADV_TYPE_INCOMP_UUIDS128         0x06
#define BLE_HS_ADV_TYPE_COMP_UUIDS128           0x07
#define BLE_HS_ADV_TYPE_INCOMP_NAME             0x08
#define BLE_HS_ADV_TYPE_COMP_NAME               0x09
#define BLE_HS_ADV_TYPE_TX_PWR_LVL              0x0a
#define BLE_HS_ADV_TYPE_SLAVE_ITVL_RANGE        0x12
#define BLE_HS_ADV_TYPE_SOL_UUIDS16             0x14
#define BLE_HS_ADV_TYPE_SOL_UUIDS128            0x15
#define BLE_HS_ADV_TYPE_SVC_DATA_UUID16         0x16
#define BLE_HS_ADV_TYPE_PUBLIC_TGT_ADDR         0x17
#define BLE_HS_ADV_TYPE_RANDOM_TGT_ADDR         0x18
#define BLE_HS_ADV_TYPE_APPEARANCE              0x19
#define BLE_HS_ADV_TYPE_ADV_ITVL                0x1a
#define BLE_HS_ADV_TYPE_SVC_DATA_UUID32         0x20
#define BLE_HS_ADV_TYPE_SVC_DATA_UUID128        0x21
#define BLE_HS_ADV_TYPE_URI                     0x24
#define BLE_HS_ADV_TYPE_MESH_PROV               0x29
#define BLE_HS_ADV_TYPE_MESH_MESSAGE            0x2a
#define BLE_HS_ADV_TYPE_MESH_BEACON             0x2b
#define BLE_HS_ADV_TYPE_MFG_DATA                0xff

#define BLE_HS_ADV_F_DISC_GEN                   0x02
#define BLE_HS_ADV_F_BREDR_UNSUP                0x04



#define BLE_SMT_CFG_STATUS_SUCESS			0
#define BLE_SMT_CFG_STATUS_START			1
#define BLE_SMT_CFG_STATUS_END				0xFF
#define BLE_SMT_CFG_STATUS_BUSY				0xF0
#define BLE_SMT_CFG_STATUS_TIMEOUT			0xF1
#define BLE_SMT_CFG_STATUS_SSID_LEN_ERR		0xF2
#define BLE_SMT_CFG_STATUS_SSID_ERR			0xF3
#define BLE_SMT_CFG_STATUS_PWD_LEN_ERR		0xF4
#define BLE_SMT_CFG_STATUS_PWD_ERR			0xF5
#define BLE_SMT_CFG_STATUS_STOP		


#define ATBM_BLE_SMART        			_IOW(ATBM_IOCTL,1, unsigned int)


struct ioctl_status_async{
	u8 type; /* 0: connect msg, 1: driver msg, 2:scan complete, 3:wakeup host reason, 4:disconnect reason, 5:connect fail reason, 6:customer event*/
	u8 driver_mode; /* TYPE1 0: rmmod, 1: insmod; TYPE3\4\5 reason */
	u8 list_empty;
	u8 event_buffer[MAX_SYNC_EVENT_BUFFER_LEN];
};

struct ioctl_ble_start{
	u32 ble_interval;				//ble work interval (100 ~ 60000 ms)
	u32 ble_scan_win;				//ble scan win	(0 ~ ble_interval/2 ms)
	u8 ble_adv;						//ble work start adv,  (=1 start =0 stop)
	u8 ble_scan;					//ble work start scan, (=1 start =0 stop)
	u8 ble_adv_chan;				//ble adv use sigle channel, (=0 use 37 38 39, =39 only use 39)
	u8 ble_scan_chan;				//ble scan use sigle channel, (=0 use 37 38 39, =38 only use 38)
};

struct ioctl_ble_adv_data{
	u8 mac[6];						//ble adv use this mac, if all byte is 0, use fix mac
	u8 adv_data_len;				//ble adv data len, (1 ~ 31)
	u8 adv_data[31];				//ble adv data
};

struct ioctl_ble_conn_rpt{
	u8 init_addr[6];
	u8 adv_addr[6];
};

struct ioctl_ble_adv_resp_start{
	u32 ble_interval;
};

struct ioctl_ble_resp_data{
	u8 resp_data_len;
	u8 resp_data[31];
};

struct at_cmd_direct_info{
	u32 len;
	u8 cmd[1500];
};

struct wsm_ble_smt_ind {
	u32 status;
	u8 ssid[32];
	u8 pwd[64];
	u8 ssid_len;
	u8 pwd_len;
};

u8 broadcast_ssid_pwd_data[128];
u8 broadcast_ssid_pwd_received_mask = 0;
u8 broadcast_ssid_pwd_received_len = 0;
#define BROADCAST_SMT_PACKET_SIZE 19  //SSID.PWD DATA
static unsigned char cfg_done_checked = 0;

typedef struct {
    /** Type of the UUID */
    u8 type;
} ble_uuid_t;

/** 16-bit UUID */
typedef struct {
    ble_uuid_t u;
    u16 value;
} ble_uuid16_t;

struct ble_hs_adv_fields {
    /*** 0x01 - Flags. */
    u8 flags;

    /*** 0x02,0x03 - 16-bit service class UUIDs. */
    ble_uuid16_t *uuids16;
    u8 num_uuids16;
    unsigned uuids16_is_complete:1;

    /*** 0x08,0x09 - Local name. */
    u8 *name;
    u8 name_len;
    unsigned name_is_complete:1;

    /*** 0xff - Manufacturer specific data. */
    u8 *mfg_data;
    u8 mfg_data_len;
};


static sem_t sem_status;
static sem_t sem_sock_sync;
static int atbm_fp = -1;
static struct ioctl_status_async status;
static char gCmdStr[512];
static int atbm_smt_status = 0;
static u8 ioctl_data[1024];
static int mac_filter_en = 0;
static u8 mac_filter[6];
static struct ioctl_ble_adv_data adv_data_recv;
static struct ioctl_ble_conn_rpt conn_rpt;
static u16 test_recv_cnt = 0;
static const char *device_name = "AltoBeam";
static const char *device_broadcast_name = "ATBMSET";
static const char *device_broadcast_name_suc = "ATBMSUC";
static int wifi_connected_completed = 0;

static u8 test_mac[6] = {0x01, 0x02, 0x03, 0x88, 0x88, 0x08};
static struct wsm_ble_smt_ind ble_smt;
static void ble_smt_set_broadcast_adv_data(void);

char * CmdLine_GetToken(char ** pLine)
{
    char *    str;
    char *    line;
    char ch;

    line = *pLine;

    /* escape white space */
    ch = line[0];
    while(ch != 0)
    {
        /* CmdLine_GetLine() has already replaced '\n' '\r' with 0 */
        if ( (ch == ' ') || (ch == ',') || (ch == '\t') ||(ch == ':'))
        {
            line++;
            ch = line[0];
            continue;
        }
        break;
    }

    str = line;
    while(ch != 0)
    {
        if ( (ch == ' ') || (ch == ',') || (ch == '\t')||(ch == ':') )
        {
            line[0] = 0;
            /* CmdLine_GetLine() has replaced '\n' '\r' with 0, so we can do line++ */
            line++;
            break;
        }
        line++;
        ch = line[0];
    }

    *pLine = line;

    return str;
}
int CmdLine_GetHex(char **pLine, unsigned int  *pDword)
{
    char *  str;
    char *  str0;
    int     got_hex;
    unsigned int  d = 0;

    str = CmdLine_GetToken(pLine);
    if (str[0] == 0)
    {
        return 0;
    }

    str0 = str;
    got_hex = 0;
    for (;;)
    {
        char    ch;

        ch = str[0];
        if (ch == 0)
        {
            break;
        }
        if (ch >= '0' && ch <= '9')
        {
            d = (d<<4) | (ch - '0');
        }
        else if (ch >= 'a' && ch <= 'f')
        {
            d = (d<<4) | (ch - 'a' + 10);
        }
        else if (ch >= 'A' && ch <= 'F')
        {
            d = (d<<4) | (ch - 'A' + 10);
        }
        else
        {
            got_hex = 0;
            break;
        }
        got_hex = 1;
        str++;
    }
    if (got_hex)
    {
        *pDword = d;
    }
    else
    {
        printf("Invalid hexdecimal: %s\n", str0);
    }

    return got_hex;
}

int CmdLine_GetInteger(char **pLine, unsigned int *pDword)
{
    char *  str;
    char *  str0;
    int     got_int;
    unsigned int  d = 0;

    str = CmdLine_GetToken(pLine);
    if (str[0] == 0)
    {
        return -1;
    }

    str0 = str;
    got_int = 0;
    for (;;)
    {
        char    ch;

        ch = str[0];
        if (ch == 0)
        {
            break;
        }
        if (ch >= '0' && ch <= '9')
        {
            d = d*10 + (ch - '0');
            got_int = 1;
            str++;
        }
        else
        {
            got_int = 0;
            break;
        }
    }
    if (got_int)
    {
        *pDword = d;
    }
    else
    {
        printf("Invalid unsigned decimal: %s\n", str0);
    }

    return got_int;
}


#define BLE_HS_ADV_MAX_FIELD_SZ     			(31 - 2)
#define BLE_HS_ADV_TYPE_INCOMP_NAME             0x08
#define BLE_HS_ADV_TYPE_COMP_NAME               0x09




int ble_hs_adv_parse_one_field(struct ble_hs_adv_fields *adv_fields,
                           u8 *total_len, u8 *src, u8 src_len)
{
    u8 data_len;
    u8 type;
    u8 *data;
    int rc;

    if (src_len < 1) {
        return -1;
    }
    *total_len = src[0] + 1;

    if (src_len < *total_len) {
        return -1;
    }

    type = src[1];
    data = src + 2;
    data_len = *total_len - 2;

    if (data_len > BLE_HS_ADV_MAX_FIELD_SZ) {
        return -1;
    }

    switch (type) {
    case BLE_HS_ADV_TYPE_INCOMP_NAME:
        adv_fields->name = data;
        adv_fields->name_len = data_len;
        adv_fields->name_is_complete = 0;
        break;

    case BLE_HS_ADV_TYPE_COMP_NAME:
        adv_fields->name = data;
        adv_fields->name_len = data_len;
        adv_fields->name_is_complete = 1;
        break;

    default:
        break;
    }

    return 0;
}

int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *adv_fields, u8 *src, u8 src_len)
{
    u8 field_len;
    int rc;

    memset(adv_fields, 0, sizeof *adv_fields);

    while (src_len > 0) {
        rc = ble_hs_adv_parse_one_field(adv_fields, &field_len, src, src_len);
        if (rc != 0) {
            return rc;
        }

        src += field_len;
        src_len -= field_len;
    }

    return 0;
}


int open_atbm_ioctl(void)
{
	unsigned long flags = 0;

	atbm_fp = open("/dev/atbm_ioctl", O_RDWR);
	if (atbm_fp < 0){
		printf("open /dev/atbm_ioctl fail.\n");
		return -1;
	}
	fcntl(atbm_fp, F_SETOWN, getpid());
	flags = fcntl(atbm_fp, F_GETFL); 
	fcntl(atbm_fp, F_SETFL, flags | FASYNC);

	return 0;
}


void ioctl_msg_func(int sig_num)
{
	int len = 0;
	//printf("ioctl_msg_func,sem_wait ++++++++++++++ \n");
	sem_wait(&sem_status);
	//printf("ioctl_msg_func,sem_wait -------------- \n");
	while (1)
	{
		memset(&status, 0, sizeof(struct ioctl_status_async));
		len = read(atbm_fp, &status, sizeof(struct ioctl_status_async));
		if (len < (int)(sizeof(status))){
			printf("Line:%d read connect stat error.\n", __LINE__);
			break;
		}
		else{
			switch(status.type)
			{
				case 0:
					atbm_smt_status = 1;
					sem_post(&sem_sock_sync);
					break;
				case 1:
					atbm_smt_status = 2;
					sem_post(&sem_sock_sync);
					break;
				default:
					printf("ioctl_msg_func:status.type = %d \n",status.type);
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

void ioctl_ble_msg_func(int sig_num)
{
	int len = 0;

	sem_wait(&sem_status);
	while (1)
	{
		memset(&status, 0, sizeof(struct ioctl_status_async));
		len = read(atbm_fp, &status, sizeof(struct ioctl_status_async));
		if (len < (int)(sizeof(status))){
			printf("Line:%d read connect stat error.\n", __LINE__);
			break;
		}
		else{
			switch(status.type)
			{
				case 0:
					atbm_smt_status = 3;
					sem_post(&sem_sock_sync);
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


int ble_coexist_start(u32 interval, u32 scan_win, u8 adv_en, u8 scan_en, u8 adv_chan, u8 scan_chan)
{
	struct ioctl_ble_start ble_start;

	printf("ble_coexist_start,int:%d, win:%d, adv:%d, scan:%d, adv_chan:%d, scan_chan:%d\n",
			interval, scan_win, adv_en, scan_en, adv_chan, scan_chan);
	ble_start.ble_interval = interval;
	ble_start.ble_scan_win = scan_win;;
	ble_start.ble_adv = adv_en;
	ble_start.ble_scan = scan_en;
	ble_start.ble_adv_chan = adv_chan;
	ble_start.ble_scan_chan = scan_chan;
	memset(ioctl_data, 0, sizeof(ioctl_data));
	memcpy(ioctl_data, &ble_start, sizeof(struct ioctl_ble_start));
	return ioctl(atbm_fp, ATBM_BLE_COEXIST_START, (unsigned long)(&ioctl_data));	
}


int ble_adv_resp_mode_start(u32 interval)
{
	struct ioctl_ble_adv_resp_start ble_start;

	printf("ble_coexist_start,int:%d\n", interval);
	ble_start.ble_interval = interval;
	memset(ioctl_data, 0, sizeof(ioctl_data));
	memcpy(ioctl_data, &ble_start, sizeof(struct ioctl_ble_adv_resp_start));
	return ioctl(atbm_fp, ATBM_BLE_ADV_RESP_MODE_START, (unsigned long)(&ioctl_data));	
}


int ble_coexist_stop(void)
{
	memset(ioctl_data, 0, sizeof(ioctl_data));
	return ioctl(atbm_fp, ATBM_BLE_COEXIST_STOP, (unsigned long)(&ioctl_data));	
}

int ble_set_adv_data(struct ioctl_ble_adv_data *adv_data)
{
	int i;
	struct ble_hs_adv_fields fields;

	if(mac_filter_en){
		if(memcmp(mac_filter, adv_data->mac, 6)){
			return -1;
		}
	}
	else{
		ble_hs_adv_parse_fields(&fields, adv_data->adv_data, adv_data->adv_data_len);
		if(fields.name && fields.name_len){
			if(memcmp("Altobeam", fields.name, fields.name_len)){
				return -1;
			}
		}
		else{
			return -1;
		}
		test_recv_cnt ++;
		sprintf(fields.name, "ATBM%04X\n", test_recv_cnt);
	}

	if(adv_data->adv_data_len > 31){
		printf( "adv_data_len err\n");
		return -1;
	}

	printf( "ble_recv_adv_data,cnt:%d mac:", test_recv_cnt);
	for(i=0; i<6; i++){
		printf( "%02X", adv_data->mac[i]);
	}
	printf( "\n");

	adv_data->mac[0] = 0x01;
	adv_data->mac[1] = 0x02;
	adv_data->mac[2] = 0x03;

#if 0
	printf( "ble_set_adv_data, adv_data:");
	for(i=0; i<adv_data->adv_data_len; i++){
		printf( "%02X", adv_data->adv_data[i]);
	}
	printf( "\n");
#endif
	
	memset(ioctl_data, 0, sizeof(ioctl_data));
	memcpy(ioctl_data, adv_data, sizeof(struct ioctl_ble_adv_data));
	return ioctl(atbm_fp, ATBM_BLE_SET_ADV_DATA, (unsigned long)(&ioctl_data));	
}


int ble_parse_smt_adv_data(struct ioctl_ble_adv_data *adv_data)
{
	int i;
   // int ssid_len = 0;
   // int pwd_len = 0;
	struct ble_hs_adv_fields fields;

	if(mac_filter_en){
		if(memcmp(mac_filter, adv_data->mac, 6)){
			return -1;
		}
	}
	else{
		ble_hs_adv_parse_fields(&fields, adv_data->adv_data, adv_data->adv_data_len);
		if(fields.name && fields.name_len){
			if(memcmp("btpw", fields.name,4)){
				printf( "ble_parse_smt_adv_data, mac");
				for(i=0; i<6; i++){
					printf( ":%02X", adv_data->mac[5-i]);
				}
				printf( " ");
				printf( "name:",
					fields.name_len,fields.name);
				for(i=0; i<fields.name_len; i++){
					printf( "%c", fields.name[i]);
				}
				printf( "\n");
				return -1;
			}
		}
		else{
			/*
				printf( "ble_parse_smt_adv_data, mac");
				for(i=0; i<6; i++){
					printf( ":%02X", adv_data->mac[5-i]);
				}
				printf( "\n");
			*/
			//printf( "ble_parse_smt_adv_data,error : fields.name_len=%d\n",fields.name_len);
			return -1;
		}
		//test_recv_cnt ++;
		//sprintf(fields.name, "ATBM%04X\n", test_recv_cnt);
	}

	if(adv_data->adv_data_len > 31){
		printf( "adv_data_len err\n");
		return -1;
	}

	if(!memcmp("btpwsuc", fields.name,7)){
		cfg_done_checked = 1;   //after send ATBMSUC, received ack from phone.
		printf( "#############Ap stop\n");
	}


	

	printf( "ble_recv_adv_data,cnt:%d mac:", test_recv_cnt);
	for(i=0; i<6; i++){
		printf( "%02X", adv_data->mac[i]);
	}
	printf( "\n");

	printf( "ble_recv_adv_data,cnt:%d name len:%d =", test_recv_cnt, fields.name_len);
	for(i=0; i<8; i++){
		printf( "%02X", fields.name[i]);
	}
	printf( "\n");


	adv_data->mac[0] = 0x01;
	adv_data->mac[1] = 0x02;
	adv_data->mac[2] = 0x03;

    //data format, the ssid_pwd was put in devicename,  wifi open mode, pwd_len will be zero, app add 1.
    //btpw0+total+(pwd_len+1) + data
    //btpw1+total+(pwd_len+1) + data
    //btpw2+total+(pwd_len+1) + data
    //btpw3+total+(pwd_len+1) + data
    //btpw4+total+data   //the last packet put one more valid byte.

    if(fields.name[4] == 0x30)
    {
     if((broadcast_ssid_pwd_received_mask &0x01) == 0){   //save packet0, set bit0
          broadcast_ssid_pwd_received_mask|=0x01;
        broadcast_ssid_pwd_data[0] = fields.name[5];
        broadcast_ssid_pwd_data[1] = fields.name[6]- 1; //ios string can not be zero, add 1
        memcpy(&broadcast_ssid_pwd_data[2], &fields.name[7], (fields.name_len-7));
        broadcast_ssid_pwd_received_len =  broadcast_ssid_pwd_received_len + fields.name_len-7;
        printf("adv_data_len0%d total:%d pwdlen:%d smt_data_len:%d\n",fields.name_len,broadcast_ssid_pwd_data[0],broadcast_ssid_pwd_data[1], broadcast_ssid_pwd_received_len);
        }
   }
    if(fields.name[4] == 0x31)
     {        
       if((broadcast_ssid_pwd_received_mask &0x02) == 0){   //save packet1
          broadcast_ssid_pwd_received_mask|=0x02;
          memcpy(&broadcast_ssid_pwd_data[BROADCAST_SMT_PACKET_SIZE+2], &fields.name[7], (fields.name_len-7));
          broadcast_ssid_pwd_received_len = broadcast_ssid_pwd_received_len + (fields.name_len-7);
          printf("adv_data_len1:%d total:%d pwdlen:%d smt_data_len:%d\n",fields.name_len,broadcast_ssid_pwd_data[0],broadcast_ssid_pwd_data[1], broadcast_ssid_pwd_received_len);
      }
    }
    if(fields.name[4] == 0x32)
    {        
       if((broadcast_ssid_pwd_received_mask & 0x04) == 0){   //save packet2
          broadcast_ssid_pwd_received_mask|=0x04;
          memcpy(&broadcast_ssid_pwd_data[BROADCAST_SMT_PACKET_SIZE*2 + 2],  &fields.name[7], (fields.name_len-7));
          broadcast_ssid_pwd_received_len = broadcast_ssid_pwd_received_len + (fields.name_len-7);
          printf("adv_data_len2:%d total:%d pwdlen:%d smt_data_len:%d\n",fields.name_len,broadcast_ssid_pwd_data[0],broadcast_ssid_pwd_data[1], broadcast_ssid_pwd_received_len);
      }
    }
    if(fields.name[4] == 0x33)
       {        
          if((broadcast_ssid_pwd_received_mask &0x08 )== 0){   //save packet3
             broadcast_ssid_pwd_received_mask|=0x08;
             memcpy(&broadcast_ssid_pwd_data[BROADCAST_SMT_PACKET_SIZE*3 + 2],  &fields.name[7], (fields.name_len-7));
             broadcast_ssid_pwd_received_len = broadcast_ssid_pwd_received_len + (fields.name_len-7);
             printf("adv_data_len3:%d total:%d pwdlen:%d smt_data_len:%d\n",fields.name_len,broadcast_ssid_pwd_data[0],broadcast_ssid_pwd_data[1], broadcast_ssid_pwd_received_len);
         }
       }
	  if(fields.name[4] == 0x34)
       {        
          if((broadcast_ssid_pwd_received_mask &0x10 )== 0){   //save packet4
             broadcast_ssid_pwd_received_mask|=0x10;
             memcpy(&broadcast_ssid_pwd_data[BROADCAST_SMT_PACKET_SIZE*4 + 2],  &fields.name[6], (fields.name_len-6));
             broadcast_ssid_pwd_received_len = broadcast_ssid_pwd_received_len + (fields.name_len-6);
             printf("adv_data_len4:%d total:%d pwdlen:%d smt_data_len:%d\n",fields.name_len,broadcast_ssid_pwd_data[0],broadcast_ssid_pwd_data[1], broadcast_ssid_pwd_received_len);
         }
       }

   printf("total:%d pwdlen:%d rcvlen:%d\n",broadcast_ssid_pwd_data[0], broadcast_ssid_pwd_data[1] , broadcast_ssid_pwd_received_len  );

    if((broadcast_ssid_pwd_data[0] > 0)&&(broadcast_ssid_pwd_received_len == broadcast_ssid_pwd_data[0]))
    {
        ble_smt.ssid_len = broadcast_ssid_pwd_data[0] - broadcast_ssid_pwd_data[1];
        ble_smt.pwd_len = broadcast_ssid_pwd_data[1];
        if(ble_smt.ssid_len >=0)
        {
         memcpy(&ble_smt.ssid[0], &broadcast_ssid_pwd_data[2],  ble_smt.ssid_len);
         memcpy(&ble_smt.pwd[0], &broadcast_ssid_pwd_data[ble_smt.ssid_len+2],  ble_smt.pwd_len);
		 if(ble_smt.status != BLE_SMT_CFG_STATUS_END){
	         ble_smt.status = BLE_SMT_CFG_STATUS_SUCESS;           
	         printf("smartconfig done!, total:%d  ssidlen:%d pwd_len:%d ssid:%s pwd:%s\n",\		 	
	         broadcast_ssid_pwd_data[0],ble_smt.ssid_len ,  ble_smt.pwd_len, ble_smt.ssid, ble_smt.pwd);
		 }
        }    
    }
   
#if 0
	printf( "ble_set_adv_data, adv_data:");
	for(i=0; i<adv_data->adv_data_len; i++){
		printf( "%02X", adv_data->adv_data[i]);
	}
	printf( "\n");
#endif

  if((ble_smt.status == BLE_SMT_CFG_STATUS_SUCESS)||(ble_smt.status == BLE_SMT_CFG_STATUS_END))
  {      
     ble_smt_set_broadcast_adv_data();        //set smart config success BLE broadcast data.
  }
  else
  {
	memset(ioctl_data, 0, sizeof(ioctl_data));
	memcpy(ioctl_data, adv_data, sizeof(struct ioctl_ble_adv_data));
  }
return ioctl(atbm_fp, ATBM_BLE_SET_ADV_DATA, (unsigned long)(&ioctl_data));	
}


void put_le16(void *buf, u16 x)
{
    u8 *u8ptr;

    u8ptr = buf;
    u8ptr[0] = (u8)x;
    u8ptr[1] = (u8)(x >> 8);
}


/*after SSID and Password received, send this broadcast data*/
static void ble_smt_set_broadcast_adv_data(void)
{
	u8 mfg_data_br[3] = {0xAB, 0xBA, 0xA0};	
	u8 mfg_data_br_suc[3] = {0xAB, 0xBA, 0xA1};
	u8 len = 0;
	u8 total_len = 0;
	struct ioctl_ble_adv_data adv_data;
	u8 *data;

	memcpy(&adv_data.mac[0], &test_mac[0], 6);
	data = &adv_data.adv_data[0];

	//flag

	len = 1;
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_FLAGS;
	*data ++ = (BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
	total_len += (len + 2);
//add for ios debug

	//UUID16
	len = 2 + 2; //2*uuid16
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_COMP_UUIDS16;

	 if(wifi_connected_completed == 1)
	 	{
		put_le16(data, (u16)BLE_ATBM_GAP_SVR_BR_SUC);
		data += 2;
		put_le16(data, (u16)BLE_ATBM_GAP_SVR_BR);

	 	}else{	
		put_le16(data, (u16)BLE_ATBM_GAP_SVR_BR);
		data += 2;
		put_le16(data, (u16)BLE_ATBM_GAP_SVR_BR_SUC);
	 		}

	
	data += 2;
	total_len += (len + 2);
	
	//mfa data
	len = 3;
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_MFG_DATA;
	 if(wifi_connected_completed == 1){
		memcpy(data, mfg_data_br_suc, len);
	  }else
	  {
	  	memcpy(data, mfg_data_br, len);
	 }
	data += len;
	total_len += (len + 2);
	adv_data.adv_data_len = total_len;
	

    if(wifi_connected_completed == 1)
    	{
		// device name
		 len = strlen(device_broadcast_name_suc);
		 *data ++ = (len + 1);
		 *data ++ = BLE_HS_ADV_TYPE_COMP_NAME;
		 memcpy(data, device_broadcast_name_suc, len);
		
    	}else{
	    // device name
		len = strlen(device_broadcast_name);
		*data ++ = (len + 1);
		*data ++ = BLE_HS_ADV_TYPE_COMP_NAME;
		memcpy(data, device_broadcast_name, len);
    }
	data += len;
	total_len += (len + 2);
	adv_data.adv_data_len = total_len;
    
	
	printf("adv_data_len:%d device_name:%s\n", total_len, device_broadcast_name);
	memset(ioctl_data, 0, sizeof(ioctl_data));
	
	memcpy(ioctl_data, &adv_data, sizeof(struct ioctl_ble_adv_data));
	
	ioctl(atbm_fp, ATBM_BLE_SET_ADV_DATA, (unsigned long)(&ioctl_data));		
}



static void ble_smt_set_adv_data(void)
{
	u8 mfg_data[3] = {0xAB, 0xAB, 0xA0};
	u8 len = 0;
	u8 total_len = 0;
	struct ioctl_ble_adv_data adv_data;
	u8 *data;

	memcpy(&adv_data.mac[0], &test_mac[0], 6);
	data = &adv_data.adv_data[0];

	//flag
	len = 1;
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_FLAGS;
	*data ++ = (BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
	total_len += (len + 2);
	
	//UUID16
	len = 2 + 2; //2*uuid16
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_COMP_UUIDS16;
	put_le16(data, (u16)BLE_ATBM_GAP_SVR);
	data += 2;
	put_le16(data, (u16)BLE_ATBM_GAP_SVR1);
	data += 2;
	total_len += (len + 2);
	
	//mfa data
	len = 3;
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_MFG_DATA;
	memcpy(data, mfg_data, len);
	data += len;
	total_len += (len + 2);
	adv_data.adv_data_len = total_len;
	
	printf("adv_data_len:%d\n", total_len);
	memset(ioctl_data, 0, sizeof(ioctl_data));
	memcpy(ioctl_data, &adv_data, sizeof(struct ioctl_ble_adv_data));
	ioctl(atbm_fp, ATBM_BLE_SET_ADV_DATA, (unsigned long)(&ioctl_data));		
}

static void ble_smt_set_resp_data(void)
{
	u8 len = 0;
	u8 total_len = 0;
	struct ioctl_ble_resp_data resp_data;
	u8 *data;

	data = &resp_data.resp_data[0];
	//COMP NAME
	len = strlen(device_name);
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_COMP_NAME;
	memcpy(data, device_name, len);
	data += len;
	total_len += (len + 2);
	resp_data.resp_data_len = total_len;

	printf("resp_data_len:%d\n", total_len);
	memset(ioctl_data, 0, sizeof(ioctl_data));
	memcpy(ioctl_data, &resp_data, sizeof(struct ioctl_ble_resp_data));
	ioctl(atbm_fp, ATBM_BLE_SET_RESP_DATA, (unsigned long)(&ioctl_data));			
}


int change_driver_to_ble(char *bleName, char *wifiName)
{
	int flags = 0;
	char *str;
	int len;
	char name[128] = {0};

	if((bleName == NULL) || (wifiName == NULL)){
		printf("driverName is NULL !!!\n");
		return -1;
	}
	
	if(atbm_fp > 0){
		close(atbm_fp);
		atbm_fp = -1;
		usleep(100000);
	}

	sprintf(gCmdStr, "ifconfig wlan0 down");
    system(gCmdStr);	
	usleep(50000);

    sprintf(gCmdStr, "killall wpa_supplicant");
    system(gCmdStr);
	usleep(50000);	

    sprintf(gCmdStr, "killall hostapd");
    system(gCmdStr);
	usleep(50000);	

	str = strstr(wifiName, ".ko");
	if(str){
		len = str - wifiName;
		if(len >= 127){
			printf("driverName is too large\n");
			return -1;
		}
		memcpy(name, wifiName, len);
		sprintf(gCmdStr, "rmmod %s", name);
	}
	else{
	    sprintf(gCmdStr, "rmmod %s", wifiName);
	}
	
    system(gCmdStr);
	usleep(1000000);
	
    sprintf(gCmdStr, "insmod %s", bleName);	
    system(gCmdStr);
	usleep(1000000);

	do{
		atbm_fp = open("/dev/atbm_ioctl", O_RDWR);
	}while(atbm_fp < 0);

	fcntl(atbm_fp, F_SETOWN, getpid());
	flags = fcntl(atbm_fp, F_GETFL); 
	fcntl(atbm_fp, F_SETFL, flags | FASYNC);

	return 0;
}

int change_driver_to_wifi(char *bleName, char *wifiName)
{
	char *str;
	int len;
	char name[128] = {0};

	if((bleName == NULL) || (wifiName == NULL)){
		printf("driverName is NULL !!!\n");
		return -1;
	}

	if(atbm_fp > 0){
		close(atbm_fp);
		atbm_fp = -1;
		usleep(100000);
	}
	
	str = strstr(bleName, ".ko");
	if(str){
		len = str - bleName;
		if(len >= 127){
			printf("driverName is too large\n");
			return -1;
		}
		memcpy(name, bleName, len);
		sprintf(gCmdStr, "rmmod %s", name);
	}
	else{
	    sprintf(gCmdStr, "rmmod %s", bleName);
	}
    system(gCmdStr);
	usleep(1000000);

    sprintf(gCmdStr, "insmod %s", wifiName);	
    system(gCmdStr);
	usleep(2000000);
}

void connect_wifi_ap(u8 *ssid, u8 ssidLen, u8 *pwd, u8 pwdLen)
{
	u8 ssidStr[33] = {0};
	u8 pwdStr[65] = {0};
	int sret, i;

	if(ssidLen > 32 || pwdLen > 64){
		printf("ssidLen(%d) pwdLen(%d) error!\n", ssidLen, pwdLen);
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
	printf("%s\n", gCmdStr);	

	if(pwdLen > 0){
	    sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 psk '\"%s\"'", pwdStr);
	    sret = system(gCmdStr);
		printf("%s\n", gCmdStr);
	}
	else{
	    sprintf(gCmdStr, "wpa_cli -i wlan0 set_network 0 key_mgmt NONE");
	    sret = system(gCmdStr);
		printf("%s\n", gCmdStr);
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
}

int ble_smart_start(void)
{
	struct at_cmd_direct_info at_cmd;

	memset(&at_cmd, 0, sizeof(at_cmd));
	at_cmd.len = strlen("ble_smt_start");
	memcpy(at_cmd.cmd, "ble_smt_start", at_cmd.len);
	return ioctl(atbm_fp, ATBM_BLE_SMART, (unsigned int)(&at_cmd));	
}


int check_wap_status()

{

 int wpa_success = 0;
 int result = 0;
 char tmpstring[50];
 char *wpa_state = "wpa_state=COMPLETED";

  result = system("wpa_cli status | tee /tmp/wpa_status.txt");
  printf( "check_wap_status result:%d\n", result);

   FILE *status_file = fopen("/tmp/wpa_status.txt", "r");
   if (status_file == NULL)
   {
		   printf("Error no status file\n");
		   exit(-1);
   }

   printf("check wpa for string %s\n", wpa_state);

   while ( fscanf(status_file,"%s", tmpstring) == 1)
   {						   
		   // printf("tmpstring %s\n", tmpstring);
		   if(strstr(tmpstring, wpa_state)!=0) {//if match found
		   	{
		   		wpa_success = 1;
			   wifi_connected_completed = 1;
				 break;  
		   	}
		   }
   }			
 
   fclose(status_file);
   
   printf("wpa_success %d\n", wpa_success);
   return wpa_success;

}


int main(int argc, char *argv[])
{
	char *pStr;
	int ret = -1;
	int argc_num;
	u32 getData;
	u32 interval = 0;
	u32 scan_win = 0;
	u8 adv_en = 0;
	u8 scan_en = 0;
	u8 adv_chan = 0;
	u8 scan_chan = 0;
	char *driverName_ble = NULL;
	char *driverName_wifi = NULL;

     time_t broadcast_suc = 0;
     time_t broadcast_suc_end = 0;    
	 wifi_connected_completed = 0;

	if(argc < 3){
		printf("argc is not enough\n", argv[0]);
		return -1;
	}
	
	
	atbm_fp = -1;
	test_recv_cnt = 0;
	sem_init(&sem_status, 0, 1);
	sem_init(&sem_sock_sync, 0, 0);

	signal(SIGIO, ioctl_msg_func);

	open_atbm_ioctl();

	printf("argc:%d\n", argc);
	ble_smt.status = BLE_SMT_CFG_STATUS_START;
	argc_num = 1;
	pStr = strstr(argv[argc_num], "advResp");
	if(pStr){
		argc_num ++;
		pStr = strstr(argv[argc_num], "int=");
		if(pStr){
			pStr = pStr + strlen("int=");
			CmdLine_GetInteger(&pStr, &getData);
			interval = getData;
			ble_smt_set_adv_data();
			ble_smt_set_resp_data();
			ble_adv_resp_mode_start(interval);
		}
		argc_num ++;
		if(argc <= argc_num){
			goto BLE_COEXIST_END;
		}
		driverName_ble = argv[argc_num];
		argc_num ++;
		if(argc <= argc_num){
			goto BLE_COEXIST_END;
		}
		driverName_wifi = argv[argc_num];
		goto BLE_COEXIST_END;
	}
	else{
		pStr = strstr(argv[argc_num], "int=");
		if(pStr){
			pStr = pStr + strlen("int=");
			CmdLine_GetInteger(&pStr, &getData);
			interval = getData;
			argc_num ++;
		}
		
		if(argc <= argc_num){
			goto BLE_COEXIST_START;
		}
		
		pStr = strstr(argv[argc_num], "win=");
		if(pStr){
			pStr = pStr + strlen("win=");
			CmdLine_GetInteger(&pStr, &getData);
			scan_win = getData;
			argc_num ++;
		}
		
		if(argc <= argc_num){
			goto BLE_COEXIST_START;
		}
		
		pStr = strstr(argv[argc_num], "adv_en");
		if(pStr){
			adv_en = 1;
			argc_num ++;
		}
		
		if(argc <= argc_num){
			goto BLE_COEXIST_START;
		}

		pStr = strstr(argv[argc_num], "scan_en");
		if(pStr){
			scan_en = 1;
			argc_num ++;
		}
		
		if(argc <= argc_num){
			goto BLE_COEXIST_START;
		}


		pStr = strstr(argv[argc_num], "adv_ch=");
		if(pStr){
			pStr = pStr + strlen("adv_ch=");
			CmdLine_GetInteger(&pStr, &getData);
			adv_chan = getData;
			argc_num ++;
		}
		
		if(argc <= argc_num){
			goto BLE_COEXIST_START;
		}
		
		pStr = strstr(argv[argc_num], "scan_ch=");
		if(pStr){
			pStr = pStr + strlen("scan_ch=");
			CmdLine_GetInteger(&pStr, &getData);
			scan_chan = getData;
			argc_num ++;
		}
		
		if(argc <= argc_num){
			goto BLE_COEXIST_START;
		}

		pStr = strstr(argv[argc_num], "mac=");
		if(pStr){
			pStr = pStr + strlen("mac=");
			CmdLine_GetHex(&pStr, &getData);
			mac_filter[0] = getData;
			CmdLine_GetHex(&pStr, &getData);
			mac_filter[1] = getData;
			CmdLine_GetHex(&pStr, &getData);
			mac_filter[2] = getData;
			CmdLine_GetHex(&pStr, &getData);
			mac_filter[3] = getData;
			CmdLine_GetHex(&pStr, &getData);
			mac_filter[4] = getData;
			CmdLine_GetHex(&pStr, &getData);
			mac_filter[5] = getData;
			mac_filter_en = 1;
			argc_num ++;
		}
		
		if(argc <= argc_num){
			goto BLE_COEXIST_START;
		}
	}

BLE_COEXIST_START:
	ble_coexist_start(interval, scan_win, adv_en, scan_en, adv_chan, scan_chan);
BLE_COEXIST_END:
	atbm_smt_status = 0;

	for (;;)
	{
		sem_wait(&sem_sock_sync);
		switch(atbm_smt_status){
			case 1:
			     //  printf( "broadcast rcv packet!\n");
                memcpy(&adv_data_recv, status.event_buffer, sizeof(struct ioctl_ble_adv_data));
				if(ble_parse_smt_adv_data(&adv_data_recv) >= 0){
					
				

	               // memcpy(&ble_smt, status.event_buffer, sizeof(struct wsm_ble_smt_ind));
					if(ble_smt.status == BLE_SMT_CFG_STATUS_SUCESS){	
		
						broadcast_suc = time((time_t *)NULL);				
						ble_smt.status = BLE_SMT_CFG_STATUS_END;
						printf( "###connect_wifi_ap broadcast_suc%ld!\n", broadcast_suc);
						connect_wifi_ap(ble_smt.ssid, ble_smt.ssid_len, ble_smt.pwd, ble_smt.pwd_len);
						//ble_smt.status = BLE_SMT_CFG_STATUS_END;						
					}
					if(ble_smt.status == BLE_SMT_CFG_STATUS_END)
					{ 
						broadcast_suc_end = time((time_t *)NULL); 
						//printf( "xconnect_wifi_ap broadcast_suc:%d end:%d\n", broadcast_suc, broadcast_suc_end);					
						if(((broadcast_suc_end - broadcast_suc) > 30)||(cfg_done_checked == 1)){   //30 seconds timeout or recievd ack.
						  printf( "xxconnect_wifi_ap broadcast_suc:%d end:%d, cfg_done_checked:%d\n", broadcast_suc, broadcast_suc_end, cfg_done_checked);
					      ble_coexist_stop();
						  goto config_err;
						}else
							{
							// printf( "########## check_wap_status \n");
							 check_wap_status();
							}
					}         
				}
				break;
			case 2:
				memcpy(&conn_rpt, status.event_buffer, sizeof(struct ioctl_ble_conn_rpt));
				printf("conn_rpt,init_addr:%02X%02X%02X%02X%02X%02X\n", 
								conn_rpt.init_addr[0],conn_rpt.init_addr[1],conn_rpt.init_addr[2],
								conn_rpt.init_addr[3],conn_rpt.init_addr[4],conn_rpt.init_addr[5]);
				printf("conn_rpt,adv_addr:%02X%02X%02X%02X%02X%02X\n", 
								conn_rpt.adv_addr[0],conn_rpt.adv_addr[1],conn_rpt.adv_addr[2],
								conn_rpt.adv_addr[3],conn_rpt.adv_addr[4],conn_rpt.adv_addr[5]);
				if(0 == memcmp(test_mac, conn_rpt.adv_addr, 6)){
					ble_coexist_stop();
					signal(SIGIO, ioctl_ble_msg_func);
					if(0 == change_driver_to_ble(driverName_ble, driverName_wifi)){
						ble_smart_start();
					}
				}
				break;
			case 3:
				memcpy(&ble_smt, status.event_buffer, sizeof(struct wsm_ble_smt_ind));
				if(ble_smt.status == BLE_SMT_CFG_STATUS_SUCESS){
					change_driver_to_wifi(driverName_ble, driverName_wifi);
					connect_wifi_ap(ble_smt.ssid, ble_smt.ssid_len, ble_smt.pwd, ble_smt.pwd_len);
					goto config_err;
				}
				else{
					printf("ble smart config err(0x%X)\n", ble_smt.status);
					change_driver_to_wifi(driverName_ble, driverName_wifi);
					goto config_err;
				}				
				break;
			default:
				break;
		}
		atbm_smt_status  = 0;
	}

config_err:
	sem_destroy(&sem_status);
	sem_destroy(&sem_sock_sync);
	if(atbm_fp > 0){
		close(atbm_fp);
	}
	printf("exit ble_smt process\n");

	return 0;
}





