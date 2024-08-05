
 

#include "wifi.h"
#include "lwip/sockets.h"

#include "../wifi_backup_common.h"

#include "drv_api.h"
#include "platform_devices.h"
#include "dev_drv.h"
#include "drv_gpio.h"
#include "atbm_wifi_driver_api.h"

#if 0

#define WIFI_WAKEUP_PATTERN_RECEIVED    0x2
#define WIFI_WAKEUP_BEACON_LOST         0x10
#define WIFI_WAKEUP_KEEPALIVE_TIMEOUT   0x20000

#define ATBM_NULL	((atbm_void *)0)

net_wakeup_cfg_t wifi_power_save ={
	.wakeup_data_offset = 42, //42 - UDP paket , 54 - TCP packet
	.wakeup_data        = "0x983B16F8F39C",
	.wakeup_data_len	= 6
};

static uint32_t s_wifi_tcpka = 0;
static uint32_t s_wifi_wakeup_status = 0;
extern int atbmwifi_get_mac(unsigned char *mac_addr);
int wifi_raw_packet_recv(uint8_t *data, uint32_t *pkt_type)
{
	return 0;
}
int wifi_raw_packet_send(const uint8_t *packet, uint32_t length)
{
	return 0;
}
int moal_wifi_set_mfgbridge_mode(int enable){

}

char wifi_init_check()
{
    return 0;
}

int wifi_wps_connect(struct bss_descriptor *bss_connected)
{
	return 0;
}

char *wifi_get_version(char *ver)
{
	char version[200] = { 0 };
		
	return ver;
}

/**
 * @brief  读取mac地址，
 * @param  mac_addr : mac地址 6BYTE
 */
 //TODO:
atbm_void atbm_wifi_get_mac_address(unsigned char *addr);
int wifi_get_mac(unsigned char *mac_addr)
{
	atbm_wifi_get_mac_address(mac_addr);
	return 0;
}


/**
 * @brief  配置省电模式,在保持连接的情况下可达到很可观省电效果
 *		省电模式下的耗电量由数据收发量决定
 *		此外用户可以配置更加细节的电源管理参数,比如DTIM间隔
 *		一般情况下使用此函数已经够用
 *
 * @param  power_save_level : 取值 0,1,2; 
 *	0 PM0 No power save
 *	1 PM1
 *  2 PM2 with sleep delay
 * @param  mac : 指向需要获取的station的地址
 */
int wifi_power_cfg(uint8_t power_save_level)
{
	return 0;
}

int wifi_ps_cfg(uint8_t ps_mode, uint8_t dtim, uint8_t *wakeup_data)
{
	return 0;
}

/**
 * @brief  wifi_get_mode, get wifi mode
 * @param  : 
 * @retval AK_WIFI_MODE
 */
extern int atbm_akwifi_get_mode(void);

int wifi_get_mode()
{
	return atbm_wifi_get_current_mode();
}

/**
 * @brief  wifi_set_mode, set wifi mode.
 * ap_wifi_dev and sta_wifi_dev are two different device, set mode just set the global variable wifi_dev
 * @param  : 
 * @retval AK_WIFI_MODE
 */
extern atbm_void* atbm_wifi_on( ATBM_WIFI_MODE AP_sta_mode);

int wifi_set_mode(int type)
{
	if(1 ==type)
	{
		atbm_wifi_on(ATBM_WIFI_STA_MODE);
	}
	else 
	{
		atbm_wifi_on(ATBM_WIFI_AP_MODE);
	}
	return 0;
}

/*
*
*wifi_scan
*it will do scan in all channel
*/
extern int atbm_wifi_scan_network(char* scan_buf, atbm_uint32 buf_size);

int wifi_scan(wifi_ap_list_t *ap_list)
{
	char i = 0;
	char result_buf[2732];
	memset(result_buf, 0, sizeof(result_buf));
	WLAN_SCAN_RESULT *result = (WLAN_SCAN_RESULT *)result_buf;
	WLAN_BSS_INFO *info = result->bss_info;
	atbm_wifi_scan_network(result_buf,2732);
	
	ap_list->ap_count = result->count;
	for(i =0;i < result->count; i++)
	{
		info = info +i;
		ap_list->ap_info[i].channel = info->chanspec;
		ap_list->ap_info[i].security = 0;  //闇�瑕佽ˉ鍏?
		ap_list->ap_info[i].rssi = info->RSSI;

		memcpy(ap_list->ap_info[i].ssid, info->SSID,  info->SSID_len);
		ap_list->ap_info[i].ssid[info->SSID_len] ='\0';
		
		memcpy(ap_list->ap_info[i].bssid, info->BSSID, 6);
	}
	return 0;
}
int _wifi_start_log(int num)
{
	atbmwifi_enable_lmaclog(num);
	return 0;
}

void atbm_sdio_dump()
{
		
}


/**
 * @brief  wifi_isconnected, return wifi connect status
 * @param  : 
 * @retval 1 - connect , 0 - not connected 
 */
//TODO:
extern int atbm_wifi_isconnected(int if_id);

int wifi_isconnected()
{
	return atbm_wifi_isconnected(0);
}

/**
 * @brief  wifi_connect, wifi connect to ap 
 * @param  : ssid, key
 * @retval 0 - send connect msg ok , -1 - some error happen 
 */
extern atbm_int32 atbm_wifi_sta_join_ap(char *ssid, char *bssid, WLAN_AUTH_MODE authMode, WLAN_ENCRYPTION encryption, const char *key);
extern void atbm_wifi_set_fast_connect_mode(atbm_uint8 enable, atbm_uint8 channel, atbm_uint8 *pmk);

int wifi_connect(char *essid, char *key)
{
	atbm_wifi_set_fast_connect_mode(0, 0, NULL);
	return atbm_wifi_sta_join_ap(essid,NULL,0,0,key);
}


/**
 * @brief  wifi_quick_connect, wifi connect to ap in quick mode
 * @param  : [in]ssid, 
 * @param  : [in]key, 
 * @param  : [in/out]bss_saved, if essid and key is equal to value in bss_saved, 
			 driver will connect with saved bss. If not equal driver will connect 
			 with essid and save the bss info in bss_saved
 * @return:  0 - connected success, -1 failed 
 */
int wifi_quick_connect( char *essid, char *key, struct bss_descriptor *bss_saved)
{
	FAST_LINK_INFO *finfo = (FAST_LINK_INFO *)bss_saved;

	if(strcmp(essid, (char *)(&finfo->config[finfo->ssid_offset])) || strcmp(key, (char *)(&finfo->config[finfo->psk_offset]))){
		memset(finfo, 0, sizeof(FAST_LINK_INFO));
		strcpy(finfo->config, essid);
		strcpy(finfo->bss, key);
		memset((u8_t *)bss_saved - sizeof(ip_info_t), 0, sizeof(ip_info_t));
	}

	return atbm_wifi_fast_link_noscan(finfo);
}

extern atbm_void atbm_wifi_get_linkinfo_noscan(FAST_LINK_INFO * finfo);

int wifi_get_fast_connect_info(struct bss_descriptor *bss_saved)
{
	atbm_wifi_get_linkinfo_noscan((FAST_LINK_INFO *)bss_saved);
}
/**
 * @brief  wifi_disconnect, wifi disconnect from ap 
 * @param  : ssid, key
 * @retval 0 - send connect msg ok , -1 - some error happen 
 */
extern int atbm_wifi_sta_disjoin_ap(void);

int wifi_disconnect()
{
	return atbm_wifi_sta_disjoin_ap();
}

/**
 * @brief  wifi_create_ap, destroy
 * @param  : 
 * @retval AK_WIFI_MODE
 */
 
extern atbm_int32 atbm_wifi_ap_create(char* ssid, int authMode, int encryption, 
	const char *key, int channel, ATBM_BOOL ssidBcst );
typedef enum
{
	KEY_NONE_CUS = 0, KEY_WEP_CUS, KEY_WPA_CUS, KEY_WPA2_CUS, KEY_MAX_VALUE_CUS
} SECURITY_TYPE_CUS;


#define SEC_AUTH_PSK(x) \
	do{\
		if((x)==KEY_NONE_CUS){ \
			x=WLAN_WPA_AUTH_DISABLED; \
		}else if((x)==KEY_WEP_CUS){ \
			x=	WLAN_WPA_AUTH_NONE; \
		}else if((x)==KEY_WPA_CUS){ \
			x=	WLAN_WPA_AUTH_PSK; \
		}else if((x)==KEY_WPA2_CUS){ \
			x=WLAN_WPA2_AUTH_PSK; \
		}\
	}while(0)
int wifi_create_ap(struct _apcfg *ap_cfg)
{
	SEC_AUTH_PSK(ap_cfg->enc_protocol);
	return atbm_wifi_ap_create(ap_cfg->ssid, ap_cfg->enc_protocol, ap_cfg->enc_protocol, ap_cfg->key, ap_cfg->channel, 0);
}
/**
 * @brief  wifi_destroy_ap, destroy
 * @param  : 
 * @retval AK_WIFI_MODE
 */
extern atbm_void  atbm_wifi_off(atbm_void);

int wifi_destroy_ap(void)
{
	 atbm_wifi_off();
	 return 0;
}


int wifi_get_tx_pending()
{
	return 0;
}

int wifi_set_max_tx_pending(unsigned char max_tx_pending)
{
	return 0;
}
int wifi_get_max_tx_pending(void)
{
	return 128;
}


/**
 * @brief initializing wifi 
 * @author
 * @date 
 * @param [in] pParam a pointer to T_WIFI_INITPARAM type
 * @return int
 * @retval   0  initializing sucessful
 * @retval  -1 initializing fail
 */
extern atbm_int32  atbm_wifi_hw_init(atbm_void);
extern int atbm_akwifi_setup_sdio(void);
int wifi_init(int init_param)
{
	int ret = 0;
	ret = atbm_akwifi_setup_sdio();
	if(ret <0 ){
		return -1;
	}
	atbm_wifi_hw_init();
	 
	tcpip_init(ATBM_NULL,ATBM_NULL);
	return 0;
}

int wifi_module_resume(void)
{
	return 0;
}


int wifi_sleep(void)
{
	return 0;
	
}


int wifi_wakeup()
{
	return 0;
}

int wifi_keepalive_set(keep_alive_t *keep_alive)
{
    return 0;
}

//0: normal wakeup, -1: wakeup abnormal, 
int wifi_check_wakeup_status(void)
{

    return 0;
}

int wifi_list_station(wifi_sta_list_t *sta_list)
{
	return 0;
}

extern int atbm_wifi_etf_start_tx(int channel,int rate_value,int is_40M, int greedfiled);
extern int atbm_wifi_etf_stop_tx();
extern int atbm_wifi_etf_start_rx(int channel ,int is_40M);
extern int atbm_wifi_etf_stop_rx();
extern atbm_int32 atbm_wifi_mfg_CarrierTone(WLAN_CHANNEL channel);

int wifi_etf_start_tx(int channel,int rate_value,int is_40M, int greedfiled)
{
	return atbm_wifi_etf_start_tx(channel, rate_value, is_40M, greedfiled);
}
int wifi_etf_stop_tx()
{
	return atbm_wifi_etf_stop_tx();
}
int wifi_etf_start_rx(int channel ,int is_40M)
{
	return atbm_wifi_etf_start_rx(channel, is_40M);
}
int wifi_etf_stop_rx()
{
	return atbm_wifi_etf_stop_rx();
}
int wifi_mfg_CarrierTone(WLAN_CHANNEL channel)
{
	return atbm_wifi_mfg_CarrierTone(channel);
}

int wifi_smartconfig()
{
	atbm_smartconfig_start();
}

#endif

