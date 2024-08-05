
#include <assert.h>


#include <linux/fs.h>
//#include <linux/kernel.h>
//#include <linux/module.h>
#include <linux/string.h>




#include "atbm_hal.h"
#include "services/gap/ble_svc_gap.h"
#include "services/dis/ble_svc_dis.h"
#include "services/gatt/ble_svc_gatt.h"

#include "host/ble_gatt.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/ble_hci_trans.h"
#include "ble_adv_cfg.h"
#include "atbm_os_api.h"



/** 200 ms. */
#define BLE_CFG_ADV_INTERVAL		(200 * 1000 / BLE_HCI_ADV_ITVL)
/** 200 ms. */
#define BLE_CFG_SCAN_INTERVAL		(200 * 1000 / BLE_HCI_SCAN_ITVL)
/** 50 ms. */
#define BLE_CFG_SCAN_WINDOW			(50 * 1000 / BLE_HCI_SCAN_ITVL)

#define BROADCAST_SMT_PACKET_SIZE 19  //SSID.PWD DATA


int adv_cfg_start = 0;
pAtbm_thread_t ble_adv_thread = NULL;
static const char *device_name = "AltoBeam";
static const char *device_broadcast_name = "ATBMSET";
static const char *device_broadcast_name_suc = "ATBMSUC";

static int wifi_connected_completed = 0;
static u8 broadcast_ssid_pwd_data[128];
static u8 broadcast_ssid_pwd_received_mask = 0;
static u8 broadcast_ssid_pwd_received_len = 0;
static u8 cfg_done_checked = 0;
static struct ble_adv_cfg_ind ble_adv_ind;
static struct ble_npl_sem adv_cfg_sem;
static char gCmdStr[512];


extern bool atbm_ble_is_quit;
extern char connect_ap[64];
extern sem_t sem_sock_sync;
extern char cmd_line[1600];
extern bool g_is_quit;


static int ble_adv_cfg_set_sucess(void)
{
	u8 mfg_data_br[3] = {0xAB, 0xBA, 0xA0};	
	u8 mfg_data_br_suc[3] = {0xAB, 0xBA, 0xA1};
	u8 len = 0;
	u8 adv_data_len = 0;
	u8 adv_data[31] = {0};
	u8 *data;
	int rc;

	data = &adv_data[0];
	//flag
	len = 1;
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_FLAGS;
	*data ++ = (BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
	adv_data_len += (len + 2);

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
	adv_data_len += (len + 2);

	//mfa data
	len = 3;
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_MFG_DATA;
	if(wifi_connected_completed == 1){
		memcpy(data, mfg_data_br_suc, len);
	}
	else{
		memcpy(data, mfg_data_br, len);
	}
	data += len;
	adv_data_len += (len + 2);

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
	adv_data_len += (len + 2);

	iot_printf("adv_data_len:%d device_name:%s\n", adv_data_len, device_broadcast_name);


    rc = ble_gap_adv_set_data(&adv_data[0], adv_data_len);
    if (rc != 0) {
        return rc;
    }

	return 0;
}


static int ble_parse_adv_cfg_data(u8 *adv_data, u8 adv_data_len)
{
	int i;
	struct ble_hs_adv_fields fields;

	ble_hs_adv_parse_fields(&fields, adv_data, adv_data_len);
	if(fields.name && fields.name_len){
		if(memcmp("btpw", fields.name,4)){
			iot_printf("ble_parse_smt_adv_data, name:");
			for(i=0; i<fields.name_len; i++){
				iot_printf("%c", fields.name[i]);
			}
			iot_printf("\n");
			return -1;
		}
	}
	else{
		return -1;
	}

	if(adv_data_len > 31){
		iot_printf("adv_data_len err\n");
		return -1;
	}

	if(!memcmp("btpwsuc", fields.name, 7)){
		cfg_done_checked = 1;   //after send ATBMSUC, received ack from phone.
		iot_printf("#############Ap stop\n");
	}

	iot_printf("ble_recv_adv_data, name len:%d =", fields.name_len);
	for(i=0; i<8; i++){
		iot_printf("%02X", fields.name[i]);
	}
	iot_printf("\n");

    if(fields.name[4] == 0x30)
    {
		if((broadcast_ssid_pwd_received_mask &0x01) == 0){   //save packet0, set bit0
			broadcast_ssid_pwd_received_mask|=0x01;
			broadcast_ssid_pwd_data[0] = fields.name[5];
			broadcast_ssid_pwd_data[1] = fields.name[6]- 1; //ios string can not be zero, add 1
			memcpy(&broadcast_ssid_pwd_data[2], &fields.name[7], (fields.name_len-7));
			broadcast_ssid_pwd_received_len =  broadcast_ssid_pwd_received_len + fields.name_len-7;
			iot_printf("adv_data_len0%d total:%d pwdlen:%d smt_data_len:%d\n",fields.name_len,broadcast_ssid_pwd_data[0],broadcast_ssid_pwd_data[1], broadcast_ssid_pwd_received_len);
		}
   }
	if(fields.name[4] == 0x31)
	{        
		if((broadcast_ssid_pwd_received_mask &0x02) == 0){   //save packet1
		broadcast_ssid_pwd_received_mask|=0x02;
		memcpy(&broadcast_ssid_pwd_data[BROADCAST_SMT_PACKET_SIZE+2], &fields.name[7], (fields.name_len-7));
		broadcast_ssid_pwd_received_len = broadcast_ssid_pwd_received_len + (fields.name_len-7);
		iot_printf("adv_data_len1:%d total:%d pwdlen:%d smt_data_len:%d\n",fields.name_len,broadcast_ssid_pwd_data[0],broadcast_ssid_pwd_data[1], broadcast_ssid_pwd_received_len);
	}
	}
	if(fields.name[4] == 0x32)
	{        
		if((broadcast_ssid_pwd_received_mask & 0x04) == 0){   //save packet2
		broadcast_ssid_pwd_received_mask|=0x04;
		memcpy(&broadcast_ssid_pwd_data[BROADCAST_SMT_PACKET_SIZE*2 + 2],  &fields.name[7], (fields.name_len-7));
		broadcast_ssid_pwd_received_len = broadcast_ssid_pwd_received_len + (fields.name_len-7);
		iot_printf("adv_data_len2:%d total:%d pwdlen:%d smt_data_len:%d\n",fields.name_len,broadcast_ssid_pwd_data[0],broadcast_ssid_pwd_data[1], broadcast_ssid_pwd_received_len);
	}
	}
	if(fields.name[4] == 0x33)
	{        
		if((broadcast_ssid_pwd_received_mask &0x08 )== 0){   //save packet3
			broadcast_ssid_pwd_received_mask|=0x08;
			memcpy(&broadcast_ssid_pwd_data[BROADCAST_SMT_PACKET_SIZE*3 + 2],  &fields.name[7], (fields.name_len-7));
			broadcast_ssid_pwd_received_len = broadcast_ssid_pwd_received_len + (fields.name_len-7);
			iot_printf("adv_data_len3:%d total:%d pwdlen:%d smt_data_len:%d\n",fields.name_len,broadcast_ssid_pwd_data[0],broadcast_ssid_pwd_data[1], broadcast_ssid_pwd_received_len);
		}
	}
	if(fields.name[4] == 0x34)
	{        
		if((broadcast_ssid_pwd_received_mask &0x10 )== 0){   //save packet4
			broadcast_ssid_pwd_received_mask|=0x10;
			memcpy(&broadcast_ssid_pwd_data[BROADCAST_SMT_PACKET_SIZE*4 + 2],  &fields.name[6], (fields.name_len-6));
			broadcast_ssid_pwd_received_len = broadcast_ssid_pwd_received_len + (fields.name_len-6);
			iot_printf("adv_data_len4:%d total:%d pwdlen:%d smt_data_len:%d\n",fields.name_len,broadcast_ssid_pwd_data[0],broadcast_ssid_pwd_data[1], broadcast_ssid_pwd_received_len);
		}
	}

	iot_printf("total:%d pwdlen:%d rcvlen:%d\n",broadcast_ssid_pwd_data[0], broadcast_ssid_pwd_data[1] , broadcast_ssid_pwd_received_len  );

	if((broadcast_ssid_pwd_data[0] > 0)&&(broadcast_ssid_pwd_received_len == broadcast_ssid_pwd_data[0]))
	{
		ble_adv_ind.ssid_len = broadcast_ssid_pwd_data[0] - broadcast_ssid_pwd_data[1];
		ble_adv_ind.pwd_len = broadcast_ssid_pwd_data[1];
		if(ble_adv_ind.ssid_len >=0)
		{
			memcpy(&ble_adv_ind.ssid[0], &broadcast_ssid_pwd_data[2],  ble_adv_ind.ssid_len);
			memcpy(&ble_adv_ind.pwd[0], &broadcast_ssid_pwd_data[ble_adv_ind.ssid_len+2],  ble_adv_ind.pwd_len);
			if(ble_adv_ind.status < BLE_ADV_CFG_TRANS_END){
				ble_adv_ind.status = BLE_ADV_CFG_TRANS_END;           
				iot_printf("adv cfg done!, total:%d, ssidlen:%d, pwd_len:%d, ssid:%s, pwd:%s\n",	 	
				broadcast_ssid_pwd_data[0],ble_adv_ind.ssid_len, ble_adv_ind.pwd_len, ble_adv_ind.ssid, ble_adv_ind.pwd);
			}
		}
	}

	if(ble_adv_ind.status == BLE_ADV_CFG_TRANS_END)
	{      
		ble_adv_cfg_set_sucess();        //set adv cfg success BLE broadcast data.
		ble_npl_sem_release(&adv_cfg_sem);
	}
	else
	{
		ble_gap_adv_set_data(&adv_data[0], adv_data_len);
  	}
	
	return 0;
}


static int ble_adv_cfg_scan_event(struct ble_gap_event *event, void *arg)
{
	switch (event->type) {
		case BLE_GAP_EVENT_DISC:
			ble_parse_adv_cfg_data(event->disc.data, event->disc.length_data);
			break;

		default:
			break;
	}

	return 0;
}

static int ble_cfg_set_cfg_adv_init(void)
{
	int rc;
	u8 len = 0;
	u8 adv_data_len = 0;
	u8 adv_data[31] = {0};
	u8 *data;
	struct ble_gap_adv_params adv_params;

	data = &adv_data[0];
	//flag
	len = 1;
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_FLAGS;
	*data ++ = (BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
	adv_data_len += (len + 2);

	len = strlen(device_name);
	*data ++ = (len + 1);
	*data ++ = BLE_HS_ADV_TYPE_COMP_NAME;
	memcpy(data, device_name, len);
	adv_data_len += (len + 2);
	data += len;
	
    rc = ble_gap_adv_set_data(&adv_data[0], adv_data_len);
    if (rc != 0) {
        return rc;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_NON;
	adv_params.itvl_min = BLE_CFG_ADV_INTERVAL;
	adv_params.itvl_max = BLE_CFG_ADV_INTERVAL;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           		&adv_params, NULL, NULL);
    if (rc != 0) {
        return rc;
    }

	return 0;
}

static int ble_cfg_set_cfg_scan_init(void)
{
    int rc;
	struct ble_gap_disc_params disc_params;

	memset(&disc_params, 0, sizeof(disc_params));
	disc_params.passive = 1;		//
	disc_params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
	disc_params.itvl = BLE_CFG_SCAN_INTERVAL;
	disc_params.window = BLE_CFG_SCAN_WINDOW;
    rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params,
                      ble_adv_cfg_scan_event, NULL);
	
    if (rc != 0) {
        return rc;
    }

	return 0;

}

static void adv_cfg_connect_wifi_ap(u8* ssid, u8 ssidLen, u8* pwd, u8 pwdLen)
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

	conn_ap_len = strlen(connect_ap);
	if(conn_ap_len){
		if(pwdLen)
			sprintf(gCmdStr, "%s %s %s",connect_ap, ssid, pwd);
		else
			sprintf(gCmdStr, "%s %s",connect_ap, ssid);
	}else{
		if(pwdLen)
			sprintf(gCmdStr, "/tmp/connect_ap.sh %s %s",ssid, pwd);
		else
			sprintf(gCmdStr, "/tmp/connect_ap.sh %s", ssid);
	}
	printf("start CMD:%s \n",gCmdStr);
	system(gCmdStr);
}

static int adv_cfg_wpa_status()
{
	int wpa_success = 0;
	char buff[32];
	
	cmd_system("iwpriv wlan0 common get_conn_state > /tmp/wpa_status.txt",NULL,0);
	cmd_system("cat /tmp/wpa_status.txt | grep wifi",buff,32); 
	sscanf(buff,"wifi_status=%d",&wpa_success);
	printf("wpa_success %d\n", wpa_success);
	return wpa_success;
}


static int ble_adv_cfg_task(void* param)
{
	int rc;
	int i;


	adv_cfg_start = 1;
	memset(&ble_adv_ind, 0, sizeof(struct ble_adv_cfg_ind));
	ble_cfg_set_cfg_adv_init();
	ble_cfg_set_cfg_scan_init();
	ble_npl_sem_init(&adv_cfg_sem, 0);

	while (!atbm_ble_is_quit){
		rc = ble_npl_sem_pend(&adv_cfg_sem, ble_npl_time_ms_to_ticks32(1000));
		if(rc == 0){
			if(ble_adv_ind.status == BLE_ADV_CFG_TRANS_END){
				adv_cfg_connect_wifi_ap(ble_adv_ind.ssid, ble_adv_ind.ssid_len, ble_adv_ind.pwd, ble_adv_ind.pwd_len);
				ble_adv_ind.status = BLE_ADV_CFG_CONN_WAIT;
				break;
			}
		}
	}

	if(ble_adv_ind.status == BLE_ADV_CFG_CONN_WAIT){
		i = 0;
		while(i++ < 10){
			if(adv_cfg_wpa_status()){
				ble_adv_ind.status = BLE_ADV_CFG_CONN_SUCC;
				break;
			}
			ble_npl_time_delay(ble_npl_time_ms_to_ticks32(1000));
		}
	}

	if(ble_adv_ind.status == BLE_ADV_CFG_CONN_SUCC){
		wifi_connected_completed = 1;
		ble_adv_cfg_set_sucess();
		ble_gap_disc_cancel();	//close scan
	}

	while (!atbm_ble_is_quit) {
		ble_npl_time_delay(ble_npl_time_ms_to_ticks32(1000));
	}

	ble_gap_adv_stop();		//close adv
	ble_gap_disc_cancel();	//close scan
	
	strcpy(cmd_line,"quit");
	g_is_quit=1;	
	sem_post(&sem_sock_sync);
	
	adv_cfg_start = 0;

	return 0;
}

void ble_adv_cfg_startup(void)
{
	if(adv_cfg_start){
		iot_printf("ble adv cfg busy!\n");
		return;
	}
	iot_printf("ble_adv_cfg_startup!!!\n");
	ble_adv_thread = atbm_createThread(ble_adv_cfg_task, (atbm_void*)ATBM_NULL, BLE_APP_PRIO);
}




