/*
 * mac80211 glue code for mac80211 altobeam APOLLO drivers
 * *
 * Copyright (c) 2016, altobeam
 * Author:
 *
 * Based on apollo code
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * Based on:
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007-2009, Christian Lamparter <chunkeey@web.de>
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * Based on:
 * - the islsm (softmac prism54) driver, which is:
 *   Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 * - stlc45xx driver
 *   Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
/*Linux version 3.4.0 compilation*/
//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
#include<linux/module.h>
//#endif
#include <linux/kthread.h>

#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <net/atbm_mac80211.h>
#include <linux/debugfs.h>
#ifdef CONFIG_ATBM_5G_PRETEND_2G
#include <net/regulatory.h>
#endif

#include "apollo.h"
#include "txrx.h"
#include "sbus.h"
#include "fwio.h"
#include "hwio.h"
#include "bh.h"
#include "sta.h"
#include "ap.h"
#include "scan.h"
#include "debug.h"
#include "pm.h"
#ifdef ATBM_SUPPORT_SMARTCONFIG
#include "smartconfig.h"
#endif
#include "svn_version.h"
#include "internal_cmd.h"


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,10,0))

MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver); 
#endif

MODULE_AUTHOR("wifi_software <wifi_software@altobeam.com>");
MODULE_DESCRIPTION("Softmac altobeam apollo wifi common code");
MODULE_LICENSE("GPL");
MODULE_ALIAS("atbm_core");




static char stbc_debug_on = 0;

const char *atbm_log = ATBM_TAG;


static int adaptive_val = 0;
module_param(adaptive_val, int, 0644);
void atbm_adaptive_val_set(int status)
{
	adaptive_val = status;
	return;
}
static int cfo_state = 0;
module_param(cfo_state, int, 0644);
void atbm_wifi_cfo_set(int status)
{
	cfo_state = status;
}
int atbm_wifi_cfo_get(void)
{
//	wifi_bt_comb = status;
	return cfo_state;
}


static int wifi_bt_comb = 0;
module_param(wifi_bt_comb, int, 0644);
void atbm_wifi_bt_comb_set(int status)
{
	wifi_bt_comb = status;
	return;
}
int atbm_wifi_bt_comb_get(void)
{
//	wifi_bt_comb = status;
	return wifi_bt_comb;
}



static int wifi_status = 0;
module_param(wifi_status, int, 0644);
unsigned int atbm_wifi_status_get(void)
{
	return wifi_status;
}
void atbm_wifi_status_set(unsigned int state)
{
	wifi_status = state;
}



//1: HIF-Disconect  0: Normal
static int hif_sta = 0;
module_param(hif_sta, int, 0644);
void atbm_hif_status_set(int status)
{
	hif_sta = status;
	return;
}



void set_chip_type(const char *chip);
#ifdef SDIO_BUS
static char chip_type_str[32] = "6062-S";

#else
static char chip_type_str[32] = "6062-U";
#endif
module_param_string(chip_name, chip_type_str,32, 0644);
void set_chip_type(const char *chip)
{
	if(chip){
		memset(chip_type_str,0,10);
		memcpy(chip_type_str,chip,strlen(chip));
		atbm_printk_init("current chiptype %s \n",chip_type_str);
	}
}
char * get_chip_type(void)
{
		return chip_type_str;
}

int tx_longpreamble = 1;
module_param(tx_longpreamble,int,0644);

extern int wifi_run_sta;
module_param(wifi_run_sta, int, 0644);

int ampdu = 0xff;
module_param(ampdu, int, 0644);
int ht_sgi = 2;//0 long-gi , 1 sgi, 2 auto
module_param(ht_sgi, int, 0644);

int ht_ldpc = 2;//0 bcc , 1 ldpc, 2 auto
module_param(ht_ldpc, int, 0644);


int tx_rate_down = 0;//0:fixed rate not downgrade , 1. downgrade from start rate
module_param(tx_rate_down, int, 0644);

int tx_rate_static = 1;//0:fixed rate no rate success static , 1. fixed rate have rate success static
module_param(tx_rate_static, int, 0644);

int beamforming_on = 0;//0:fixed rate no rate success static , 1. fixed rate have rate success static
module_param(beamforming_on, int, 0644);

int start_choff = 0xff;
module_param(start_choff, int, 0644);
static int stbc = 1;
module_param(stbc, int, 0644);
static int efuse = 0;
module_param(efuse, int, 0644);
static int driver_ver = DRIVER_VER;
module_param(driver_ver, int, 0644);
static int fw_ver = 0;
module_param(fw_ver, int, 0644);
//he param
/*
DCM just used in HE mcs0/1/3/4
*/
int he_dcm = 0xff;
module_param(he_dcm, int, 0644);
/*
we support 
0> auto
1> 4x3.2
2> 4x0.8
3> 2X1.6
4> 2X0.8
*/
int he_ltf_gi = 0xff;
module_param(he_ltf_gi, int, 0644);
/*
DCM just used in HE LDPC
*/
int he_ldpc = 1;
module_param(he_ldpc, int, 0644);


int he_not_40M = 0xff;
module_param(he_not_40M, int, 0644);

//int he_ER_106tone = 0;
//module_param(he_ER_106tone, int, 0644);



int he_pad8us = 0;
module_param(he_pad8us, int, 0644);

/* Accept MAC address of the form macaddr=0x00,0x80,0xE1,0x30,0x40,0x50 */
static u8 atbm_mac_default[ATBM_WIFI_MAX_VIFS][ETH_ALEN] = {
	{0x00, 0x12, 0x34, 0x00, 0x00, 0x00},
	{0x00, 0x12, 0x34, 0x00, 0x00, 0x01},
};
#if 0
module_param_array_named(macaddr, atbm_mac_default[0], byte, NULL, S_IRUGO);
module_param_array_named(macaddr2, atbm_mac_default[1], byte, NULL, S_IRUGO);
MODULE_PARM_DESC(macaddr, "First MAC address");
MODULE_PARM_DESC(macaddr2, "Second MAC address");
#endif
#define verson_str(_ver) #_ver
#define verson(ver) verson_str(ver)
const char* hmac_ver = "HMAC_VER:"verson(DRIVER_VER);
#ifdef CUSTOM_FEATURE_MAC /* To use macaddr and PS Mode of customers */
#define PATH_WIFI_MACADDR	"/data/.mac.info"
#define PATH_WIFI_PSM_INFO		"/data/.psm.info"
#define PATH_WIFI_EFUSE	    "/data/.efuse.info"
int access_file(char *path, char *buffer, int size, int isRead);
#ifdef CUSTOM_FEATURE_PSM/* To control ps mode */
static int savedpsm = 0;
#endif
#endif
extern u32 atbmwifi_efuse_read_chip_flag(void);

#define DEFAULT_CCA_INTERVAL_MS		(5000)
#define DEFAULT_CCA_INTERVAL_US		(DEFAULT_CCA_INTERVAL_MS*1000)
#define DEFAULT_CCA_UTIL_US		(50) //cca register util : 50us
/* TODO: use rates and channels from the device */
#if 0
#define RATETAB_ENT(_rate, _rateid, _flags)		\
	{						\
		.bitrate	= (_rate),		\
		.hw_value	= (_rateid),		\
		.flags		= (_flags),		\
	}

static struct ieee80211_rate atbm_rates[] = {
	RATETAB_ENT(10,  0,   0),
	RATETAB_ENT(20,  1,   0),
	RATETAB_ENT(55,  2,   0),
	RATETAB_ENT(110, 3,   0),
	RATETAB_ENT(60,  6,  0),
	RATETAB_ENT(90,  7,  0),
	RATETAB_ENT(120, 8,  0),
	RATETAB_ENT(180, 9,  0),
	RATETAB_ENT(240, 10, 0),
	RATETAB_ENT(360, 11, 0),
	RATETAB_ENT(480, 12, 0),
	RATETAB_ENT(540, 13, 0),
};

static struct ieee80211_rate atbm_mcs_rates[] = {
	RATETAB_ENT(65,  14, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(130, 15, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(195, 16, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(260, 17, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(390, 18, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(520, 19, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(585, 20, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(650, 21, IEEE80211_TX_RC_MCS),
	RATETAB_ENT(60 , 22, IEEE80211_TX_RC_MCS),
};

static struct ieee80211_rate atbm_he_mcs_rates[] = { 
    RATETAB_ENT(75,  23, IEEE80211_TX_RC_HE_MCS), //ER-MCS0   
    RATETAB_ENT(146,  24, IEEE80211_TX_RC_HE_MCS),//ER-MCS1 
    RATETAB_ENT(219,  25, IEEE80211_TX_RC_HE_MCS),//ER-MCS2 
    RATETAB_ENT(75,  26, IEEE80211_TX_RC_HE_MCS),//MCS0
    RATETAB_ENT(146, 27, IEEE80211_TX_RC_HE_MCS),//MCS1
    RATETAB_ENT(219, 28, IEEE80211_TX_RC_HE_MCS),//MCS2
    RATETAB_ENT(293, 29, IEEE80211_TX_RC_HE_MCS),//MCS3
    RATETAB_ENT(439, 30, IEEE80211_TX_RC_HE_MCS),
    RATETAB_ENT(585, 31, IEEE80211_TX_RC_HE_MCS),
    RATETAB_ENT(658, 32, IEEE80211_TX_RC_HE_MCS),
    RATETAB_ENT(731, 33, IEEE80211_TX_RC_HE_MCS),
    RATETAB_ENT(878, 34, IEEE80211_TX_RC_HE_MCS),
    RATETAB_ENT(975, 35, IEEE80211_TX_RC_HE_MCS),
    RATETAB_ENT(1097, 36, IEEE80211_TX_RC_HE_MCS),
    RATETAB_ENT(1219, 37, IEEE80211_TX_RC_HE_MCS), //MCS11    
};
#else
#define RATETAB_ENT(_rate, _rateid, _flags,_hw_flags)	\
	{						\
		.bitrate	= (_rate),		\
		.hw_value	= (_rateid),		\
		.flags		= (_flags),		\
		.hw_value_short = (_hw_flags)	\
	}

static struct ieee80211_rate atbm_rates[] = {
	RATETAB_ENT(10,  0,   0,IEEE80211_RATE_HW_FLAGS_UNMAP),
	RATETAB_ENT(20,  1,   0,IEEE80211_RATE_HW_FLAGS_UNMAP),
	RATETAB_ENT(55,  2,   0,IEEE80211_RATE_HW_FLAGS_UNMAP),
	RATETAB_ENT(110, 3,   0,0),
	RATETAB_ENT(60,  6,  0,0),
	RATETAB_ENT(90,  7,  0,0),
	RATETAB_ENT(120, 8,  0,0),
	RATETAB_ENT(180, 9,  0,0),
	RATETAB_ENT(240, 10, 0,0),
	RATETAB_ENT(360, 11, 0,0),
	RATETAB_ENT(480, 12, 0,0),
	RATETAB_ENT(540, 13, 0,0),
};

static struct ieee80211_rate atbm_mcs_rates[] = {
	RATETAB_ENT(65,  14, IEEE80211_TX_RC_MCS,0),
	RATETAB_ENT(130, 15, IEEE80211_TX_RC_MCS,0),
	RATETAB_ENT(195, 16, IEEE80211_TX_RC_MCS,0),
	RATETAB_ENT(260, 17, IEEE80211_TX_RC_MCS,0),
	RATETAB_ENT(390, 18, IEEE80211_TX_RC_MCS,0),
	RATETAB_ENT(520, 19, IEEE80211_TX_RC_MCS,0),
	RATETAB_ENT(585, 20, IEEE80211_TX_RC_MCS,0),
	RATETAB_ENT(650, 21, IEEE80211_TX_RC_MCS,0),
	RATETAB_ENT(60 , 22, IEEE80211_TX_RC_MCS,0),
};

static struct ieee80211_rate atbm_he_mcs_rates[] = { 
    RATETAB_ENT(75,  23, IEEE80211_TX_RC_HE_MCS,0), //ER-MCS0   
    RATETAB_ENT(146,  24, IEEE80211_TX_RC_HE_MCS,0),//ER-MCS1 
    RATETAB_ENT(219,  25, IEEE80211_TX_RC_HE_MCS,0),//ER-MCS2 
    RATETAB_ENT(75,  26, IEEE80211_TX_RC_HE_MCS,0),//MCS0
    RATETAB_ENT(146, 27, IEEE80211_TX_RC_HE_MCS,0),//MCS1
    RATETAB_ENT(219, 28, IEEE80211_TX_RC_HE_MCS,0),//MCS2
    RATETAB_ENT(293, 29, IEEE80211_TX_RC_HE_MCS,0),//MCS3
    RATETAB_ENT(439, 30, IEEE80211_TX_RC_HE_MCS,0),
    RATETAB_ENT(585, 31, IEEE80211_TX_RC_HE_MCS,0),
    RATETAB_ENT(658, 32, IEEE80211_TX_RC_HE_MCS,0),
    RATETAB_ENT(731, 33, IEEE80211_TX_RC_HE_MCS,0),
    RATETAB_ENT(878, 34, IEEE80211_TX_RC_HE_MCS,0),
    RATETAB_ENT(975, 35, IEEE80211_TX_RC_HE_MCS,0),
    RATETAB_ENT(1097, 36, IEEE80211_TX_RC_HE_MCS,0),
    RATETAB_ENT(1219, 37, IEEE80211_TX_RC_HE_MCS,0), //MCS11    
};


#endif
#define atbm_a_rates		(atbm_rates + 4)
#define atbm_a_rates_size	(ARRAY_SIZE(atbm_rates) - 4)
#define atbm_g_rates		(atbm_rates + 0)
#define atbm_g_rates_size	(ARRAY_SIZE(atbm_rates))
#define atbm_n_rates		(atbm_mcs_rates)
#define atbm_n_rates_size	(ARRAY_SIZE(atbm_mcs_rates))
#define atbm_he_rates		(atbm_he_mcs_rates)
#define atbm_he_rates_size	(ARRAY_SIZE(atbm_he_mcs_rates))


#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq	= 5000 + (5 * (_channel)),		\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}
#ifdef CONFIG_ATBM_5G_PRETEND_2G
#define CHAN5G_2G(_channel,hw_channel ,_flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq	= 5000 + (5 * (_channel)),		\
	.hw_value		= (hw_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}
#endif
static struct ieee80211_channel atbm_2ghz_chantable[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

#ifdef CONFIG_ATBM_APOLLO_5GHZ_SUPPORT
static struct ieee80211_channel atbm_5ghz_chantable[] = {
	CHAN5G(34, 0),		CHAN5G(36, 0),
	CHAN5G(38, 0),		CHAN5G(40, 0),
	CHAN5G(42, 0),		CHAN5G(44, 0),
	CHAN5G(46, 0),		CHAN5G(48, 0),
	CHAN5G(52, 0),		CHAN5G(56, 0),
	CHAN5G(60, 0),		CHAN5G(64, 0),
	CHAN5G(100, 0),		CHAN5G(104, 0),
	CHAN5G(108, 0),		CHAN5G(112, 0),
	CHAN5G(116, 0),		CHAN5G(120, 0),
	CHAN5G(124, 0),		CHAN5G(128, 0),
	CHAN5G(132, 0),		CHAN5G(136, 0),
	CHAN5G(140, 0),		CHAN5G(149, 0),
	CHAN5G(153, 0),		CHAN5G(157, 0),
	CHAN5G(161, 0),		CHAN5G(165, 0),
	CHAN5G(184, 0),		CHAN5G(188, 0),
	CHAN5G(192, 0),		CHAN5G(196, 0),
	CHAN5G(200, 0),		CHAN5G(204, 0),
	CHAN5G(208, 0),		CHAN5G(212, 0),
	CHAN5G(216, 0),
};
#elif defined (CONFIG_ATBM_5G_PRETEND_2G) /* CONFIG_ATBM_APOLLO_5GHZ_SUPPORT */
#pragma message("ATBM60XX:support 5G channel,but actualy at 2G") 
static struct ieee80211_channel atbm_5ghz_chantable[] = {
	CHAN5G_2G(34,36,0),//IEEE80211_CHAN_RADAR
	CHAN5G_2G(36,36,0),//IEEE80211_CHAN_RADAR
	CHAN5G_2G(38,38,0),
	CHAN5G_2G(40,40,0),
	CHAN5G_2G(42,42,0),
	CHAN5G_2G(44,42,0),
};
#endif
/* atbm channel define */
static struct ieee80211_regdomain atbm_request_regdom = {
	.n_reg_rules = 4,
	.alpha2 =  "00",
	.reg_rules = {
		/* IEEE 802.11b/g, channels 1..11 */
		REG_RULE(2412-10, 2462+10, 40, 6, 20, 0),
		/* IEEE 802.11b/g, channels 12..13. */
		REG_RULE(2467-10, 2472+10, 40, 6, 20,
			0/*NL80211_RRF_NO_IR*/),
		/* IEEE 802.11 channel 14 - Only JP enables
		 * this and for 802.11b only */
		REG_RULE(2484-10, 2484+10, 20, 6, 20,
			0/*NL80211_RRF_NO_IR*/),
		/* IEEE 802.11a, channel 36..48 */
		REG_RULE(5170-10, 5240+10, 80, 6, 20,
                        0/*NL80211_RRF_NO_IR*/),
	}
};
//#else
///* atbm channel define */
//static struct ieee80211_regdomain atbm_request_regdom = {
//	.n_reg_rules = 4,
//	.alpha2 =  "00",
//	.reg_rules = {
//		/* IEEE 802.11b/g, channels 1..11 */
//		REG_RULE(2412-10, 2462+10, 40, 6, 20, 0),
//		/* IEEE 802.11b/g, channels 12..13. */
//		REG_RULE(2467-10, 2472+10, 40, 6, 20,
//			0/*NL80211_RRF_NO_IR*/),
//		/* IEEE 802.11 channel 14 - Only JP enables
//		 * this and for 802.11b only */
//		REG_RULE(2484-10, 2484+10, 20, 6, 20,
//			0/*NL80211_RRF_NO_IR*/),
//		/* IEEE 802.11a, channel 36..48 */
//		REG_RULE(5170-10, 5240+10, 80, 6, 20,
//                        0/*NL80211_RRF_NO_IR*/),
//	}
//};



#ifdef CONFIG_ATBM_HE
#pragma message("ATBM606x: support HE mode") 
#define HE_MAC_CAP0  ATBM_IEEE80211_HE_MAC_CAP0_HTC_HE
#define HE_MAC_CAP1  ATBM_IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_0US        /*Trigger Frame MAC Padding Duration set 0*/

#define HE_MAC_CAP2  ATBM_IEEE80211_HE_MAC_CAP2_BSR       \
                    /* |IEEE80211_HE_MAC_CAP2_BCAST_TWT*/\
                     |ATBM_IEEE80211_HE_MAC_CAP2_MU_CASCADING
                 
#define HE_MAC_CAP3  ATBM_IEEE80211_HE_MAC_CAP3_OMI_CONTROL\
                     /*|IEEE80211_HE_MAC_CAP3_OFDMA_RA*/\
                     |ATBM_IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3\
                     /*|IEEE80211_HE_MAC_CAP3_FLEX_TWT_SCHED*/\
                     |ATBM_IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS

#define HE_MAC_CAP4  ATBM_IEEE80211_HE_MAC_CAP4_BSRP_BQRP_A_MPDU_AGG\
                    /* |IEEE80211_HE_MAC_CAP4_OPS   */ \
                     |ATBM_IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU
                    
#define HE_MAC_CAP5  0




/*HE PHY capacity*/
#define HE_PHY_CAP0  ATBM_IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G\
                     |ATBM_IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G

#define HE_PHY_CAP1  ATBM_IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A\
                     |ATBM_IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD
                     // |IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US
#define HE_PHY_CAP2 0
#define HE_PHY_CAP3 0
#define HE_PHY_CAP4 0
#define HE_PHY_CAP5 0
#define HE_PHY_CAP6 0
#define HE_PHY_CAP7 ATBM_IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI
#define HE_PHY_CAP8 0
#define HE_PHY_CAP9 ATBM_IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_8US


#ifdef CONFIG_ATBM_HE_BEAMFORM 			
#define ATBM_IEEE80211_HE_PHY_CAP6_BEAMFORM_ENABLE ATBM_IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |\
				ATBM_IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB|\
				ATBM_IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB
				
#define ATBM_IEEE80211_HE_PHY_CAP4_BEAMFORM_ENABLE ATBM_IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |\
					ATBM_IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4
#else
#define ATBM_IEEE80211_HE_PHY_CAP6_BEAMFORM_ENABLE 0
#define ATBM_IEEE80211_HE_PHY_CAP4_BEAMFORM_ENABLE 0
#endif  //CONFIG_ATBM_HE_BEAMFORM


struct atbm_ieee80211_sta_he_cap atbm_support_he_cap = {
	.has_he = true,
	.he_cap_elem = {
		.mac_cap_info[0] =
			ATBM_IEEE80211_HE_MAC_CAP0_HTC_HE | ATBM_IEEE80211_HE_MAC_CAP0_TWT_REQ,
		.mac_cap_info[1] =
			ATBM_IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
			ATBM_IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_1,
		.mac_cap_info[2] =
			ATBM_IEEE80211_HE_MAC_CAP2_BSR |
			ATBM_IEEE80211_HE_MAC_CAP2_MU_CASCADING ,/*|
			IEEE80211_HE_MAC_CAP2_ACK_EN,*/
		.mac_cap_info[3] =
			ATBM_IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_0,
		.mac_cap_info[4] = ATBM_IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
		.mac_cap_info[5] = ATBM_IEEE80211_HE_MAC_CAP5_OM_CTRL_UL_MU_DATA_DIS_RX,
		.phy_cap_info[0] = 
			ATBM_IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G|
            ATBM_IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G,
		.phy_cap_info[1] =
			ATBM_IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
			ATBM_IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD,
		.phy_cap_info[2] =
			ATBM_IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
			ATBM_IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ,
			//IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
			//IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO,
		.phy_cap_info[3] = ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM|
						ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM,
		.phy_cap_info[4] = ATBM_IEEE80211_HE_PHY_CAP4_BEAMFORM_ENABLE /*IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
				IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4*/,
		.phy_cap_info[6] = ATBM_IEEE80211_HE_PHY_CAP6_BEAMFORM_ENABLE|
		//	ATBM_IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE|
			ATBM_IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO,
		.phy_cap_info[7] =ATBM_IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI,
		.phy_cap_info[8] =ATBM_IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
			ATBM_IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G|
			ATBM_IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484,
		.phy_cap_info[9] =	ATBM_IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
		ATBM_IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU|
			ATBM_IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU|
			//IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB|
			ATBM_IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB|
			ATBM_IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_8US,
		

		/* Leave all the other PHY capability bytes
		 * unset, as DCM, beam forming, RU and PPE
		 * threshold information are not supported
		 */
	},
	.he_mcs_nss_supp = {
		.rx_mcs_80 = cpu_to_le16(0xfffe),
		.tx_mcs_80 = cpu_to_le16(0xfffe),
		.rx_mcs_160 = cpu_to_le16(0xffff),
		.tx_mcs_160 = cpu_to_le16(0xffff),
		.rx_mcs_80p80 = cpu_to_le16(0xffff),
		.tx_mcs_80p80 = cpu_to_le16(0xffff),
	},
	.ppe_thres[0] = 0,
};


#if defined (CONFIG_ATBM_APOLLO_5GHZ_SUPPORT) || defined (CONFIG_ATBM_5G_PRETEND_2G)

struct atbm_ieee80211_sta_he_cap atbm_support_5g_he_cap = {
	.has_he = true,
	.he_cap_elem = {
		.mac_cap_info[0] =
			ATBM_IEEE80211_HE_MAC_CAP0_HTC_HE,
		.mac_cap_info[1] =
			ATBM_IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
			ATBM_IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_1,
		.mac_cap_info[2] =
			ATBM_IEEE80211_HE_MAC_CAP2_BSR |
			ATBM_IEEE80211_HE_MAC_CAP2_MU_CASCADING ,/*|
			IEEE80211_HE_MAC_CAP2_ACK_EN,*/
		.mac_cap_info[3] =
			ATBM_IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_0,
		.mac_cap_info[4] = ATBM_IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
		.phy_cap_info[0] = 
			ATBM_IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G|
            ATBM_IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G,
		.phy_cap_info[1] =
			ATBM_IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
			ATBM_IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD,
		.phy_cap_info[2] =
			ATBM_IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
			ATBM_IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ,
			//IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
			//IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO,
		.phy_cap_info[3] = ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM|
						ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM,
		.phy_cap_info[4] = ATBM_IEEE80211_HE_PHY_CAP4_BEAMFORM_ENABLE /*IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
				IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4*/,
		.phy_cap_info[6] = ATBM_IEEE80211_HE_PHY_CAP6_BEAMFORM_ENABLE|
		//	ATBM_IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE|
			ATBM_IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO,
		.phy_cap_info[7] =ATBM_IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI,
		.phy_cap_info[8] =ATBM_IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
			ATBM_IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU|
			ATBM_IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484,
		.phy_cap_info[9] =	ATBM_IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
		ATBM_IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU|
			ATBM_IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU|
			//IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB|
			ATBM_IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB|
			ATBM_IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_8US,
		

		/* Leave all the other PHY capability bytes
		 * unset, as DCM, beam forming, RU and PPE
		 * threshold information are not supported
		 */
	},
	.he_mcs_nss_supp = {
		.rx_mcs_80 = cpu_to_le16(0xfffe),
		.tx_mcs_80 = cpu_to_le16(0xfffe),
		.rx_mcs_160 = cpu_to_le16(0xffff),
		.tx_mcs_160 = cpu_to_le16(0xffff),
		.rx_mcs_80p80 = cpu_to_le16(0xffff),
		.tx_mcs_80p80 = cpu_to_le16(0xffff),
	},
	.ppe_thres[0] = 0,
};
#endif
#endif  //CONFIG_ATBM_HE

#ifdef ATBM_NOT_SUPPORT_40M_CHW
#pragma message("ATBM601x:not support 40M chw") 
#else
#pragma message("ATBM602x/ATBM603x:support 40M chw")
#endif
static struct ieee80211_supported_band atbm_band_2ghz = {
	.channels = atbm_2ghz_chantable,
	.n_channels = ARRAY_SIZE(atbm_2ghz_chantable),
	.bitrates = atbm_g_rates,
	.n_bitrates = atbm_g_rates_size,
	.ht_cap = {
		.cap = /*IEEE80211_HT_CAP_GRN_FLD		|*/
#ifndef ATBM_NOT_SUPPORT_40M_CHW
			IEEE80211_HT_CAP_SUP_WIDTH_20_40		|
			IEEE80211_HT_CAP_DSSSCCK40				|
#endif
#ifdef CONFIG_ATBM_APOLLO_SUPPORT_SGI
			IEEE80211_HT_CAP_SGI_20 |
#ifndef ATBM_NOT_SUPPORT_40M_CHW
			IEEE80211_HT_CAP_SGI_40	|
#endif
#endif /* ATBM_APOLLO_SUPPORT_SGI */
			(1 << IEEE80211_HT_CAP_RX_STBC_SHIFT)	,
		.ht_supported = 1,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_32K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs = {
			.rx_mask[0] = 0xFF,
			.rx_highest = __cpu_to_le16(0),
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
   /* ._iftype_data = 2,  */
    /*.iftype_data = &atbm_sband_if_data,   */  
};

#if defined (CONFIG_ATBM_APOLLO_5GHZ_SUPPORT) || defined (CONFIG_ATBM_5G_PRETEND_2G)
static struct ieee80211_supported_band atbm_band_5ghz = {
	.channels = atbm_5ghz_chantable,
	.n_channels = ARRAY_SIZE(atbm_5ghz_chantable),
	.bitrates = atbm_a_rates,
	.n_bitrates = atbm_a_rates_size,
	.ht_cap = {
		.cap = IEEE80211_HT_CAP_GRN_FLD |
#ifndef ATBM_NOT_SUPPORT_40M_CHW
			IEEE80211_HT_CAP_SUP_WIDTH_20_40|
			IEEE80211_HT_CAP_DSSSCCK40|
#endif
#ifdef CONFIG_ATBM_APOLLO_SUPPORT_SGI
			IEEE80211_HT_CAP_SGI_20 |
#ifndef ATBM_NOT_SUPPORT_40M_CHW
			IEEE80211_HT_CAP_SGI_40|
#endif
#endif /* ATBM_APOLLO_SUPPORT_SGI */
			(1 << IEEE80211_HT_CAP_RX_STBC_SHIFT),
		.ht_supported = 1,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs = {
			.rx_mask[0] = 0xFF,
			.rx_highest = __cpu_to_le16(0),
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
};
#endif /* CONFIG_ATBM_APOLLO_5GHZ_SUPPORT */



static const unsigned long atbm_ttl[] = {
	1 * HZ,	/* VO */
	2 * HZ,	/* VI */
	5 * HZ, /* BE */
	10 * HZ	/* BK */
};
void atbm_set_fw_ver(struct atbm_common *hw_priv)
{
	fw_ver = hw_priv->wsm_caps.firmwareVersion;
	
}
void atbm_get_drv_version(short *p)
{
	*p = driver_ver;
}

static void atbm_set_p2p_vif_addr(struct atbm_common *hw_priv)
{
	memcpy(hw_priv->addresses[1].addr,hw_priv->addresses[0].addr,ETH_ALEN);
#ifdef ATBM_P2P_ADDR_USE_LOCAL_BIT
	hw_priv->addresses[1].addr[0] ^= BIT(1);
#else
	hw_priv->addresses[1].addr[5] += 1;
#endif
}
int atbm_set_mac_addr2efuse(struct ieee80211_hw *hw, u8 *macAddr)
{
	struct atbm_common *hw_priv = hw->priv;
	u8 EfusemacAddr[6] = {0};


	if (wsm_get_mac_address(hw_priv, &EfusemacAddr[0]) == 0)
	{
		
		if (EfusemacAddr[0]| EfusemacAddr[1]|EfusemacAddr[2]|EfusemacAddr[3]|EfusemacAddr[4]|EfusemacAddr[5])
		{
			atbm_printk_err("MAC addr Writed\n");
			return -1;
		}
	}else
	{
		atbm_printk_err("MAC addr disabled\n");
		return -1;
	}

	if (wsm_set_efuse_mac_address(hw_priv, &macAddr[0]) == 0)
	{
		if (macAddr[0]| macAddr[1]|macAddr[2]|macAddr[3]|macAddr[4]|macAddr[5])
		{
			memcpy(hw_priv->addresses[0].addr,macAddr,ETH_ALEN);		
			atbm_set_p2p_vif_addr(hw_priv);
		}
	}else
	{
		atbm_printk_err("MAC addr disabled\n");
		return -1;
	}

	
	if (hw_priv->addresses[0].addr[3] == 0 &&
		hw_priv->addresses[0].addr[4] == 0 &&
		hw_priv->addresses[0].addr[5] == 0) {
		get_random_bytes(&hw_priv->addresses[0].addr[3], 3);
		atbm_set_p2p_vif_addr(hw_priv);
	}

	SET_IEEE80211_PERM_ADDR(hw_priv->hw, hw_priv->addresses[0].addr);

	return 0;
}
#ifdef CONFIG_IEEE80211_SPECIAL_FILTER
static int atbm_set_frame_filter(struct ieee80211_hw *hw,struct ieee80211_vif *vif,u32 n_filters,
		   struct ieee80211_special_filter *filter_table,bool enable)
{
	struct atbm_common *hw_priv = hw->priv;
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);
	int ret;
	struct wsm_beacon_filter_table table;
	int index = 0;
	struct wsm_beacon_filter_control enabled = {
		.enabled = 0,
		.bcn_count = 1,
	};
	if(priv == NULL){
		return -EOPNOTSUPP;
	}
	if(atbm_bh_is_term(hw_priv)){
		return -EOPNOTSUPP;
	}
	if(atomic_read(&priv->enabled) == 0){
		atbm_printk_err("%s:priv is not enable\n",__func__);
		return -EOPNOTSUPP;
	}

	if(n_filters > 10){
		atbm_printk_err("%s:n_filters (%d)\n",__func__,n_filters);
		return -EOPNOTSUPP;
	}
	atbm_printk_debug("%s:n(%d),enable(%d)\n",__func__,n_filters,enable);
	if(n_filters && (enable == true)){
		memset(&table,0,sizeof(struct wsm_beacon_filter_table));

		for(index = 0;index < n_filters;index ++){
			table.entry[index].ieId = filter_table[index].filter_action;
			memcpy(table.entry[index].oui,filter_table[index].oui,3);

			if(filter_table[index].flags & SPECIAL_F_FLAGS_FRAME_TYPE){
				table.entry[index].actionFlags = WSM_BEACON_FILTER_FRAME_TYPE;
				atbm_printk_debug("%s:frame [%d]\n",__func__,table.entry[index].ieId);
			}
			else if(filter_table[index].flags & SPECIAL_F_FLAGS_FRAME_OUI){
				table.entry[index].actionFlags = WSM_BEACON_FILTER_ACTION_ENABLE | WSM_BEACON_FILTER_OUI;
				atbm_printk_debug("%s:ie_oui ie[%d],oui[%d:%d:%d]\n",__func__,
				table.entry[index].ieId,table.entry[index].oui[0],table.entry[index].oui[1],table.entry[index].oui[2]);
			}
			else if(filter_table[index].flags & SPECIAL_F_FLAGS_FRAME_IE){
				table.entry[index].actionFlags = WSM_BEACON_FILTER_ACTION_ENABLE;
				atbm_printk_debug("%s:ie [%d]\n",__func__,table.entry[index].ieId);
			}
			else
				WARN_ON(1);
		}

		table.numOfIEs = __cpu_to_le32(n_filters);
		
		ret = wsm_set_beacon_filter_table(hw_priv,&table,priv->if_id);

		if(ret != 0)
			return ret;
	}
	enabled.enabled = enable == true;
	ret = wsm_beacon_filter_control(hw_priv,
					&enabled, priv->if_id);

	return ret;
}
#endif
static const struct ieee80211_ops atbm_ops = {
	.start			= atbm_start,
	.stop			= atbm_stop,
	.add_interface		= atbm_add_interface,
	.remove_interface	= atbm_remove_interface,
	.change_interface	= atbm_change_interface,
	.tx			= atbm_tx,
	.hw_scan		= atbm_hw_scan,
	.cancel_hw_scan = atbm_cancel_hw_scan,
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
	.sched_scan_start	= atbm_hw_sched_scan_start,
	.sched_scan_stop	= atbm_hw_sched_scan_stop,
#endif /*ROAM_OFFLOAD*/
#endif
	.set_tim		= atbm_set_tim,
	.sta_notify		= atbm_sta_notify,
	.sta_add		= atbm_sta_add,
	.sta_remove		= atbm_sta_remove,
	.set_key		= atbm_set_key,
	.set_rts_threshold	= atbm_set_rts_threshold,
	.config			= atbm_config,
	.bss_info_changed	= atbm_bss_info_changed,
#ifdef ATBM_SUPPORT_WOW
	.prepare_multicast	= atbm_prepare_multicast,
#endif
	.configure_filter	= atbm_configure_filter,
	.conf_tx		= atbm_conf_tx,
	.get_stats		= atbm_get_stats,
	.ampdu_action		= atbm_ampdu_action,
	.flush			= atbm_flush,
#ifdef CONFIG_PM
	.suspend		= atbm_wow_suspend,
	.resume			= atbm_wow_resume,
#endif /* CONFIG_PM */
	/* Intentionally not offloaded:					*/
	/*.channel_switch	= atbm_channel_switch,		*/
#ifdef CONFIG_ATBM_SUPPORT_P2P
	.remain_on_channel	= atbm_remain_on_channel,
	.cancel_remain_on_channel = atbm_cancel_remain_on_channel,
#endif
#ifdef CONFIG_ATBM_LMAC_FILTER_IP_FRAME
#ifdef IPV6_FILTERING
	.set_data_filter        = atbm_set_data_filter,
#endif /*IPV6_FILTERING*/
#endif
#if defined (CONFIG_NL80211_TESTMODE) && defined(CONFIG_ATBM_TEST_TOOL)
	.testmode_cmd		= atbm_altmtest_cmd,
#endif
	.set_mac_addr2efuse = atbm_set_mac_addr2efuse,
#ifdef CONFIG_ATBM_STA_LISTEN
	.sta_triger_listen = atbm_sta_triger_listen,
	.sta_stop_listen=atbm_sta_stop_listen,
#endif
#ifdef CONFIG_IEEE80211_SPECIAL_FILTER
	.set_frame_filter=atbm_set_frame_filter,
#endif
#ifdef CONFIG_ATBM_SUPPORT_REKEY
	.set_rekey_data = atbm_set_rekey_data
#endif
	.do_set_defualt_wep_key = atbm_do_set_wep_key,
#ifdef CONFIG_ATBM_BLE
	 .do_ble_xmit = atbm_ble_do_ble_xmit,
#endif
	.do_twt = atbm_do_twt,
#ifdef CONFIG_ATBM_SUPPORT_CSA
	.do_csa = atbm_do_csa,
	.do_csa_concurrent = atbm_do_csa_concurrent,
#endif
};
#ifdef CONFIG_PM
static const struct wiphy_wowlan_support atbm_wowlan_support = {
	/* Support only for limited wowlan functionalities */
	.flags = WIPHY_WOWLAN_ANY | WIPHY_WOWLAN_DISCONNECT,
};
#endif
void atbm_get_mac_address(struct atbm_common *hw_priv)
{
	int i;
	u8 macAddr[6] = {0};
#ifdef CUSTOM_FEATURE_MAC/*TO usr macaddr of customers*/
	char readmac[17+1]={0};
#endif
	for (i = 0; i < ATBM_WIFI_MAX_VIFS; i++) {
		memcpy(hw_priv->addresses[i].addr,
			   atbm_mac_default[i],
			   ETH_ALEN);
	}
	if (wsm_get_mac_address(hw_priv, &macAddr[0]) == 0)
	{
		if (macAddr[0]| macAddr[1]|macAddr[2]|macAddr[3]|macAddr[4]|macAddr[5])
		{
			memcpy(hw_priv->addresses[0].addr,macAddr,ETH_ALEN);
			atbm_set_p2p_vif_addr(hw_priv);
		}
	}
#ifdef CUSTOM_FEATURE_MAC/* To use macaddr of customers */
	if(access_file(PATH_WIFI_MACADDR,readmac,17,1) > 0) {
		sscanf(readmac,"%2X:%2X:%2X:%2X:%2X:%2X",
								(int *)&hw_priv->addresses[0].addr[0],
								(int *)&hw_priv->addresses[0].addr[1],
								(int *)&hw_priv->addresses[0].addr[2],
								(int *)&hw_priv->addresses[0].addr[3],
								(int *)&hw_priv->addresses[0].addr[4],
								(int *)&hw_priv->addresses[0].addr[5]);
		atbm_set_p2p_vif_addr(hw_priv);
	}
#endif
	
	if (hw_priv->addresses[0].addr[3] == 0 &&
		hw_priv->addresses[0].addr[4] == 0 &&
		hw_priv->addresses[0].addr[5] == 0) {
		get_random_bytes(&hw_priv->addresses[0].addr[3], 3);
		atbm_set_p2p_vif_addr(hw_priv);
	}

	SET_IEEE80211_PERM_ADDR(hw_priv->hw, hw_priv->addresses[0].addr);

}
#if 0
int ieee80211_send_probe_resp_packet(struct ieee80211_sub_if_data *sdata,struct atbm_common *hw_priv)
{
	struct sk_buff *skb;
	struct atbm_ieee80211_mgmt *mgmt = NULL;
	u8 dest[6]={0xff,0xff,0xff,0xff,0xff,0xff};
	u8 *special = NULL;
	
#ifdef ATBM_PROBE_RESP_EXTRA_IE
	skb = ieee80211_proberesp_get(&sdata->local->hw,&sdata->vif);
#endif
	if(skb == NULL){
		atbm_printk_err("atbm_ieee80211_send_probe_resp  skb err! \n");
		return -1;
	}

	/*
	*add special ie
	*/
	if(atbm_pskb_expand_head(skb,0,sizeof(struct atbm_vendor_cfg_ie),GFP_ATOMIC)){
		//atbm_printk_err("atbm_pskb_expand_head   err! sizeof(struct atbm_vendor_cfg_ie) = %d\n",sizeof(struct atbm_vendor_cfg_ie));
		atbm_dev_kfree_skb(skb);
		return 0;
	}
	special = skb->data + skb->len;
	//atbm_printk_err("atbm_ieee80211_send_probe_resp  special=%x \n",special);
	memcpy(special,&hw_priv->private_ie,sizeof(struct atbm_vendor_cfg_ie));

	atbm_skb_put(skb,sizeof(struct atbm_vendor_cfg_ie));
	

	mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	memcpy(mgmt->da,dest,6);

	IEEE80211_SKB_CB(skb)->flags |=
			IEEE80211_TX_CTL_NO_CCK_RATE;

	ieee80211_tx_skb(sdata, skb);
	return 0;

}

static int atbm_ieee80211_send_probe_resp(void *arg)
{
	
	struct atbm_common *hw_priv = arg;
	struct ieee80211_hw *hw = hw_priv->hw;
	struct ieee80211_local *local = 
		container_of(hw, struct ieee80211_local, hw);
	
	struct ieee80211_sub_if_data *sdata = NULL;
	struct ieee80211_sub_if_data *sdata_update;    
	
		//sdata->local;
	
//	struct atbm_common *hw_priv = (struct atbm_common *) hw->priv;
	hw_priv->stop_prbresp_thread = false;
	hw_priv->start_send_prbresp = false;
	while(1){
		atbm_printk_err(" wait_event_interruptible from  send_prbresp_wq\n");
		wait_event_interruptible(hw_priv->send_prbresp_wq, hw_priv->start_send_prbresp);

		if(hw_priv->stop_prbresp_thread)
			break;
		list_for_each_entry(sdata_update, &local->interfaces, list){
			if(sdata_update->vif.type == NL80211_IFTYPE_AP){
				sdata = sdata_update;
				break;
			}	
		}
		if(sdata == NULL){
			atbm_printk_err("atbm_ieee80211_send_probe_resp not found ap mode! \n");
			msleep(1000);
			continue ;
		}
		
		if (!ieee80211_sdata_running(sdata)){
			atbm_printk_err("atbm_ieee80211_send_probe_resp net interface not runing! \n");
			msleep(1000);
			continue ;
		}
		if(sdata->vif.type != NL80211_IFTYPE_AP){
			atbm_printk_err("atbm_ieee80211_send_probe_resp , not ap mode! \n");
			msleep(1000);
			continue ;
		}
		atbm_printk_err(" start_send_prbresp +++++++++++++\n");
		while(hw_priv->start_send_prbresp){
			msleep(2);
			if(ieee80211_send_probe_resp_packet(sdata,hw_priv)){
				atbm_printk_err("ieee80211_send_probe_resp_packet err! \n");
				break;
			}
		}
		
	}
	while(1){
		if(kthread_should_stop())
			break;
		schedule_timeout_uninterruptible(msecs_to_jiffies(100));
	}
	atbm_printk_err("atbm_ieee80211_send_probe_resp drv_flush! \n");
	return 0;
}


int atbm_send_probe_resp_init(struct atbm_common *hw_priv)
{

	if(!hw_priv){
		atbm_printk_err("atbm_send_probe_resp_init : hw_priv is NULL\n");
		return -1;
	}
	hw_priv->send_prbresp_work = NULL;
	//hw_priv->bh_wq
	init_waitqueue_head(&hw_priv->send_prbresp_wq);
	hw_priv->stop_prbresp_thread = false;
	hw_priv->start_send_prbresp = false;
	hw_priv->send_prbresp_work = 
	kthread_create(&atbm_ieee80211_send_probe_resp, hw_priv, ieee80211_alloc_name(hw_priv->hw,"atbm_prbrp"));
	
	if (!hw_priv->send_prbresp_work){
		atbm_printk_err("kthread_create :error!! \n");
		hw_priv->send_prbresp_work = NULL;
		return -1;
	}else{
		wake_up_process(hw_priv->send_prbresp_work);
	}
	//ATBM_INIT_WORK(&hw_priv->send_prbresp_work, atbm_ieee80211_send_probe_resp);
	//mutex_init(&hw_priv->stop_send_prbresp_lock);
	return 0;
}
int atbm_send_probe_resp_uninit(struct atbm_common *hw_priv) 
{
	struct task_struct *thread = hw_priv->send_prbresp_work;
	if (WARN_ON(!thread))
		return -1;
	hw_priv->bh_thread = NULL;
	hw_priv->stop_prbresp_thread = true;
	hw_priv->start_send_prbresp = true;
	wake_up(&hw_priv->send_prbresp_wq);
	kthread_stop(thread);
	return 0;
}
#endif
#ifdef CONFIG_IEEE80211_SEND_SPECIAL_MGMT

extern int atbm_send_private_mgmt_init(struct atbm_common *hw_priv);
extern int atbm_send_private_mgmt_uninit(struct atbm_common *hw_priv) ;
#endif
struct ieee80211_hw *atbm_init_common(size_t hw_priv_data_len)
{
	int i;
	struct ieee80211_hw *hw;
	struct atbm_common *hw_priv;
	struct ieee80211_supported_band *sband;
	int band;

	hw = ieee80211_alloc_hw(hw_priv_data_len, &atbm_ops);
	if (!hw)
		return NULL;

	hw_priv = hw->priv;
	/* TODO:COMBO this debug message can be removed */
	atbm_printk_init("Allocated hw_priv @ %p\n", hw_priv);
	hw_priv->if_id_slot = 0;
#ifdef CONFIG_ATBM_SUPPORT_P2P
	hw_priv->roc_if_id = -1;
#endif
	hw_priv->bStartTx = 0;
	hw_priv->bStartTxWantCancel = 0;
	atomic_set(&hw_priv->num_vifs, 0);

	hw_priv->hw = hw;
	hw_priv->rates = atbm_rates; /* TODO: fetch from FW */
	hw_priv->mcs_rates = atbm_n_rates;
	hw_priv->he_mcs_rates = atbm_he_rates;
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
	hw_priv->auto_scanning = 0;
	hw_priv->frame_rcvd = 0;
	hw_priv->num_scanchannels = 0;
	hw_priv->num_2g_channels = 0;
	hw_priv->num_5g_channels = 0;
#endif /*ROAM_OFFLOAD*/
#endif

	/* Enable block ACK for 4 TID (BE,VI,VI,VO). */
	/*due to HW limitations*/
	//hw_priv->ba_tid_mask = 0xB1;
	//hw_priv->ba_tid_rx_mask = 0xB1;
	//hw_priv->ba_tid_tx_mask = 0xB1;
	hw_priv->ba_tid_rx_mask = ampdu;
	hw_priv->ba_tid_tx_mask = ampdu;

	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_SUPPORTS_PS |
		    IEEE80211_HW_SUPPORTS_DYNAMIC_PS |
		    IEEE80211_HW_REPORTS_TX_ACK_STATUS |
		    IEEE80211_HW_SUPPORTS_UAPSD |
		    IEEE80211_HW_CONNECTION_MONITOR |
		    IEEE80211_HW_SUPPORTS_CQM_RSSI |
		    /* Aggregation is fully controlled by firmware.
		     * Do not need any support from the mac80211 stack */
		    /* IEEE80211_HW_AMPDU_AGGREGATION | */
		    IEEE80211_HW_SUPPORTS_P2P_PS |
		    IEEE80211_HW_SUPPORTS_CQM_BEACON_MISS |
		    IEEE80211_HW_SUPPORTS_CQM_TX_FAIL |
		    IEEE80211_HW_BEACON_FILTER |
		    IEEE80211_HW_MFP_CAPABLE/*11W need this*/;
#ifdef CONFIG_ATBM_MONITOR_HDR_PRISM
	hw->flags |= IEEE80211_HW_MONITOR_NEED_PRISM_HEADER;
#endif
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
#ifdef CONFIG_ATBM_SUPPORT_IBSS
					  BIT(NL80211_IFTYPE_ADHOC) |
#endif
#ifdef CONFIG_MAC80211_ATBM_MESH
					  BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
#ifdef CONFIG_ATBM_SUPPORT_P2P
					  BIT(NL80211_IFTYPE_P2P_CLIENT) |
					  BIT(NL80211_IFTYPE_P2P_GO) |
#endif
					  BIT(NL80211_IFTYPE_AP);
					  
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0))
	hw->wiphy->software_iftypes |= BIT(NL80211_IFTYPE_MONITOR);
#endif
#ifdef CONFIG_ATBM_SUPPORT_P2P
	hw->wiphy->software_iftypes |= BIT(NL80211_IFTYPE_P2P_CLIENT) |
					  BIT(NL80211_IFTYPE_P2P_GO); 
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
#ifdef CONFIG_ATBM_SUPPORT_P2P
	hw->wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	hw->wiphy->flags |= WIPHY_FLAG_OFFCHAN_TX;
#endif
#ifdef ATBM_AP_SME
	hw->wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME;
#endif
#endif

#ifdef CONFIG_PM
#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3, 11, 0))
	/* Support only for limited wowlan functionalities */
	hw->wiphy->wowlan.flags = WIPHY_WOWLAN_ANY |
					  WIPHY_WOWLAN_DISCONNECT;
	hw->wiphy->wowlan.n_patterns = 0;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
	hw->wiphy->wowlan.max_pkt_offset = 0,
#endif
#else
	hw->wiphy->wowlan = &atbm_wowlan_support;
#endif
#endif
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 0, 8))
	hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
#endif
#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3, 11, 0))
	hw->wiphy->flags |= WIPHY_FLAG_DISABLE_BEACON_HINTS;
#endif
	hw->wiphy->n_addresses = ATBM_WIFI_MAX_VIFS;
	hw->wiphy->addresses = hw_priv->addresses;
	hw->wiphy->max_remain_on_channel_duration = 1000;
	hw->channel_change_time = 500;	/* TODO: find actual value */
#ifdef	CONFIG_ATBM_HE
	hw->he_cap_2g4 = &atbm_support_he_cap;
#if defined (CONFIG_ATBM_APOLLO_5GHZ_SUPPORT) || defined (CONFIG_ATBM_5G_PRETEND_2G)
	hw->he_cap_5g  = &atbm_support_5g_he_cap;
#endif
#ifdef CONFIG_CFG880211_SUPPORT_HE
	hw->wiphy->support_mbssid = 1;
#endif
#endif
#ifdef ATBM_WIFI_QUEUE_LOCK_BUG
	hw->queues = 8;
#else
	hw->queues = 4;
#endif
	hw_priv->noise = -94;

	hw->max_rates = 8;
	hw->max_rate_tries = 15;
	hw->extra_tx_headroom = WSM_TX_EXTRA_HEADROOM + 64 +
		8  /* TKIP IV */ +
		12 /* TKIP ICV and MIC */;
	hw->extra_tx_headroom += IEEE80211_ATBM_SKB_HEAD_SIZE;
	hw->sta_data_size = sizeof(struct atbm_sta_priv);
	hw->vif_data_size = sizeof(struct atbm_vif);
    if(ht_ldpc)
    {
    	atbm_band_2ghz.ht_cap.cap |=  IEEE80211_HT_CAP_LDPC_CODING;
    }
	
	if (ht_sgi)
	{
		atbm_band_2ghz.ht_cap.cap |=  IEEE80211_HT_CAP_SGI_20;
		if (atbm_band_2ghz.ht_cap.cap &  IEEE80211_HT_CAP_SUP_WIDTH_20_40)
			atbm_band_2ghz.ht_cap.cap |=  IEEE80211_HT_CAP_SGI_40;
		if (atbm_band_2ghz.ht_cap.cap &  IEEE80211_HT_CAP_SUP_WIDTH_20_40)
			atbm_band_2ghz.ht_cap.cap |=  IEEE80211_HT_CAP_SGI_40;
	}else
	{		
		atbm_band_2ghz.ht_cap.cap &=  ~(IEEE80211_HT_CAP_SGI_20|IEEE80211_HT_CAP_SGI_40);
	}
	if (stbc)
	{
		//disable MIMO RXSTBC
		atbm_band_2ghz.ht_cap.cap |= 0;//IEEE80211_HT_CAP_RX_STBC;
	}else
	{
		atbm_band_2ghz.ht_cap.cap &= ~(IEEE80211_HT_CAP_TX_STBC|IEEE80211_HT_CAP_RX_STBC);
	}

 if(stbc_debug_on == 1)
 	{
		atbm_band_2ghz.ht_cap.cap |= (IEEE80211_HT_CAP_TX_STBC|0x0100);	 // one stream stbc
		atbm_support_he_cap.he_cap_elem.phy_cap_info[2] |=	ATBM_IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ;
		atbm_support_he_cap.he_cap_elem.phy_cap_info[7] |=	ATBM_IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ;
		atbm_support_he_cap.he_cap_elem.phy_cap_info[7] |=	ATBM_IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ;
 	}
	
	hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &atbm_band_2ghz;
#if defined (CONFIG_ATBM_APOLLO_5GHZ_SUPPORT) || defined (CONFIG_ATBM_5G_PRETEND_2G)
	hw->wiphy->bands[IEEE80211_BAND_5GHZ] = &atbm_band_5ghz;
#endif /* CONFIG_ATBM_APOLLO_5GHZ_SUPPORT */

//#ifdef  CONFIG_ATBM_5G_PRETEND_2G
#if (LINUX_VERSION_IS_GEQ_OR_CPTCFG(3,14,0))
	hw->wiphy->regulatory_flags = REGULATORY_CUSTOM_REG |REGULATORY_DISABLE_BEACON_HINTS|REGULATORY_COUNTRY_IE_IGNORE;
#else
	hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;//REGULATORY_CUSTOM_REG ;//WIPHY_FLAG_CUSTOM_REGULATORY;
#endif
	wiphy_apply_custom_regulatory(hw->wiphy,&atbm_request_regdom);
//#endif
	/* Channel params have to be cleared before registering wiphy again */
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		sband = hw->wiphy->bands[band];
		if (!sband)
			continue;
		for (i = 0; i < sband->n_channels; i++) {
			sband->channels[i].max_antenna_gain = 0;
			sband->channels[i].max_power = 30;
		}
	}

	hw->wiphy->max_scan_ssids = WSM_SCAN_MAX_NUM_OF_SSIDS;
	hw->wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;

	SET_IEEE80211_PERM_ADDR(hw, hw_priv->addresses[0].addr);
	spin_lock_init(&hw_priv->tx_com_lock);
	spin_lock_init(&hw_priv->wakelock_spinlock);
	hw_priv->wakelock_hw_counter = 0;
	hw_priv->wakelock_bh_counter = 0;
#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_init(&hw_priv->hw_wake, WAKE_LOCK_SUSPEND, ieee80211_alloc_name(hw,"wlan_hw_wake"));
	wake_lock_init(&hw_priv->bh_wake, WAKE_LOCK_SUSPEND, ieee80211_alloc_name(hw,"wlan_bh_wake"));
#endif /* CONFIG_HAS_WAKELOCK */


	spin_lock_init(&hw_priv->vif_list_lock);
	mutex_init(&hw_priv->wsm_cmd_mux);
	mutex_init(&hw_priv->conf_mutex);
	sema_init(&hw_priv->wsm_oper_lock, 1);
	atbm_init_timer(&hw_priv->wsm_pm_timer);
	hw_priv->wsm_pm_timer.data = (unsigned long)hw_priv;
	hw_priv->wsm_pm_timer.function = atbm_pm_timer;
	spin_lock_init(&hw_priv->wsm_pm_spin_lock);
	atomic_set(&hw_priv->wsm_pm_running, 0);
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
	spin_lock_init(&hw_priv->tsm_lock);
#endif /*CONFIG_ATBM_APOLLO_TESTMODE*/
	hw_priv->workqueue = atbm_create_singlethread_workqueue(ieee80211_alloc_name(hw,"atbm_wq"));
	sema_init(&hw_priv->scan.lock, 1);
	ATBM_INIT_WORK(&hw_priv->scan.work, atbm_scan_work);
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
	ATBM_INIT_WORK(&hw_priv->scan.swork, atbm_sched_scan_work);
#endif /*ROAM_OFFLOAD*/
#endif
	ATBM_INIT_DELAYED_WORK(&hw_priv->scan.timeout, atbm_scan_timeout);
#ifdef CONFIG_ATBM_SCAN_SPLIT
	ATBM_INIT_DELAYED_WORK(&hw_priv->scan.scan_spilt, atbm_scan_split_work);
#endif

#ifdef CONFIG_IEEE80211_SEND_SPECIAL_MGMT
	
		atbm_send_private_mgmt_init(hw_priv);
#endif


//#ifdef CONFIG_WIRELESS_EXT
	ATBM_INIT_WORK(&hw_priv->etf_tx_end_work, etf_scan_end_work);
	//init_timer(&hw_priv->etf_expire_timer);	
	//hw_priv->etf_expire_timer.expires = jiffies+1000*HZ;
	//hw_priv->etf_expire_timer.data = (unsigned long)hw_priv;
	//hw_priv->etf_expire_timer.function = atbm_etf_test_expire_timer;
//#endif //#ifdef CONFIG_WIRELESS_EXT
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
	ATBM_INIT_DELAYED_WORK(&hw_priv->advance_scan_timeout,
		 atbm_advance_scan_timeout);
#endif
#ifdef CONFIG_ATBM_SUPPORT_P2P
	ATBM_INIT_DELAYED_WORK(&hw_priv->rem_chan_timeout, atbm_rem_chan_timeout);
#endif
	atbm_skb_queue_head_init(&hw_priv->rx_frame_queue);
	atbm_skb_queue_head_init(&hw_priv->rx_frame_free);
#ifdef ATBM_SUPPORT_SMARTCONFIG
	ATBM_INIT_WORK(&hw_priv->scan.smartwork, atbm_smart_scan_work);
	ATBM_INIT_WORK(&hw_priv->scan.smartsetChanwork, atbm_smart_setchan_work);
	ATBM_INIT_WORK(&hw_priv->scan.smartstopwork, atbm_smart_stop_work);
	atbm_init_timer(&hw_priv->smartconfig_expire_timer);	
	hw_priv->smartconfig_expire_timer.data = (unsigned long)hw_priv;
	hw_priv->smartconfig_expire_timer.function = atbm_smartconfig_expire_timer;
#endif
	if (unlikely(atbm_queue_stats_init(&hw_priv->tx_queue_stats,
			WLAN_LINK_ID_MAX,
			atbm_skb_dtor,
			hw_priv))) {
		ieee80211_free_hw(hw);
		return NULL;
	}

	for (i = 0; i < 4; ++i) {
		if (unlikely(atbm_queue_init(&hw_priv->tx_queue[i],
				&hw_priv->tx_queue_stats, i, ATBM_WIFI_MAX_QUEUE_SZ,
				atbm_ttl[i]))) {
			for (; i > 0; i--)
				atbm_queue_deinit(&hw_priv->tx_queue[i - 1]);
			atbm_queue_stats_deinit(&hw_priv->tx_queue_stats);
			ieee80211_free_hw(hw);
			return NULL;
		}
	}
	hw_priv->channel_type = NL80211_CHAN_NO_HT;
	hw_priv->monitor_if_id = -1;
#ifdef CONFIG_ATBM_STA_LISTEN
	atbm_sta_listen_int(hw_priv);
#endif
	init_waitqueue_head(&hw_priv->wsm_cmd_wq);
	init_waitqueue_head(&hw_priv->wsm_startup_done);
	wsm_buf_init(&hw_priv->wsm_cmd_buf);
	spin_lock_init(&hw_priv->wsm_cmd.lock);
#if defined(CONFIG_ATBM_APOLLO_WSM_DUMPS_SHORT)
	hw_priv->wsm_dump_max_size = 20;
#endif /* CONFIG_ATBM_APOLLO_WSM_DUMPS_SHORT */
	for (i = 0; i < ATBM_WIFI_MAX_VIFS; i++)
		hw_priv->hw_bufs_used_vif[i] = 0;

#ifdef MCAST_FWDING
       for (i = 0; i < WSM_MAX_BUF; i++)
               wsm_init_release_buffer_request(hw_priv, i);
       hw_priv->buf_released = 0;
#endif
	return hw;
}
//EXPORT_SYMBOL_GPL(atbm_init_common);

int atbm_register_common(struct ieee80211_hw *dev)
{
	int err;
	err = ieee80211_register_hw(dev);
	if (err) {
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Cannot register device (%d).\n",
				err);
		return err;
	}
	atbm_printk_always( "is registered as '%s'\n",
			wiphy_name(dev->wiphy));
	return 0;
}
//EXPORT_SYMBOL_GPL(atbm_register_common);

void atbm_free_common(struct ieee80211_hw *dev)
{
	 struct atbm_common *hw_priv = dev->priv; 
	/* struct atbm_common *hw_priv = dev->priv; */
	/* unsigned int i; */
#ifdef CONFIG_IEEE80211_SEND_SPECIAL_MGMT
	
		atbm_printk_err("atbm_free_common : stop send_prbresp_work ++++++++++\n");
	
		atbm_send_private_mgmt_uninit(hw_priv);
#endif	

	ieee80211_free_hw(dev);
}
//EXPORT_SYMBOL_GPL(atbm_free_common);

void atbm_unregister_common(struct ieee80211_hw *dev)
{
	struct atbm_common *hw_priv = dev->priv;
	int i;
	atbm_printk_exit("atbm_unregister_common.++\n");
	ieee80211_unregister_hw(dev);
	
	hw_priv->sbus_ops->irq_unsubscribe(hw_priv->sbus_priv);

	if(hw_priv->sbus_ops->sbus_xmit_func_deinit)
		hw_priv->sbus_ops->sbus_xmit_func_deinit(hw_priv->sbus_priv);
	if(hw_priv->sbus_ops->sbus_rev_func_deinit)	
		hw_priv->sbus_ops->sbus_rev_func_deinit(hw_priv->sbus_priv);
	
	hw_priv->init_done = 0;
	atbm_unregister_bh(hw_priv);
	atbm_rx_bh_flush(hw_priv);
	atomic_set(&hw_priv->atbm_pluged,0);
	atbm_debug_release_common(hw_priv);
#ifdef OPER_CLOCK_USE_SEM
	atbm_del_timer_sync(&hw_priv->wsm_pm_timer);
	spin_lock_bh(&hw_priv->wsm_pm_spin_lock);
	if(atomic_read(&hw_priv->wsm_pm_running) == 1){
		atomic_set(&hw_priv->wsm_pm_running, 0);
		wsm_oper_unlock(hw_priv);
		atbm_release_suspend(hw_priv);
	}
	spin_unlock_bh(&hw_priv->wsm_pm_spin_lock);
#endif	

	mutex_destroy(&hw_priv->conf_mutex);
#ifdef MCAST_FWDING
	for (i = 0; i < WSM_MAX_BUF; i++)
		wsm_buf_deinit(&hw_priv->wsm_release_buf[i]);
#endif
	atbm_flush_workqueue(hw_priv->workqueue);
	atbm_destroy_workqueue(hw_priv->workqueue);
	hw_priv->workqueue = NULL;

#ifdef CONFIG_HAS_WAKELOCK
	hw_priv->wakelock_hw_counter = 0;
	hw_priv->wakelock_bh_counter = 0;
	wake_lock_destroy(&hw_priv->hw_wake);
	wake_lock_destroy(&hw_priv->bh_wake);
#endif /* CONFIG_HAS_WAKELOCK */

	wsm_buf_deinit(&hw_priv->wsm_cmd_buf);
	if (hw_priv->skb_cache) {
		atbm_dev_kfree_skb(hw_priv->skb_cache);
		hw_priv->skb_cache = NULL;
	}
#ifdef CONFIG_SUPPORT_SDD
	if (hw_priv->sdd) {
		release_firmware(hw_priv->sdd);
		hw_priv->sdd = NULL;
	}
#endif
	for (i = 0; i < 4; ++i)
		atbm_queue_deinit(&hw_priv->tx_queue[i]);
	atbm_queue_stats_deinit(&hw_priv->tx_queue_stats);
#ifdef CONFIG_PM
	atbm_pm_deinit(&hw_priv->pm_state);
#endif
	if(hw_priv->sbus_ops->sbus_reset_chip)
		hw_priv->sbus_ops->sbus_reset_chip(hw_priv->sbus_priv);
	atbm_printk_exit("atbm_unregister_common.--\n");
}
//EXPORT_SYMBOL_GPL(atbm_unregister_common);

static void ABwifi_set_ifce_comb(struct atbm_common *hw_priv,
				 struct ieee80211_hw *hw)
{
	hw_priv->if_limits1[0].max = 1;

	hw_priv->if_limits1[0].types = BIT(NL80211_IFTYPE_STATION);
#ifdef CONFIG_ATBM_SUPPORT_MULTIAP
	hw_priv->if_limits1[1].max = 2;
#else
	hw_priv->if_limits1[1].max = 1;
#endif
	hw_priv->if_limits1[1].types = BIT(NL80211_IFTYPE_AP);

	hw_priv->if_limits2[0].max = 2;
	hw_priv->if_limits2[0].types = BIT(NL80211_IFTYPE_STATION);


	hw_priv->if_limits3[0].max = 1;

	hw_priv->if_limits3[0].types = BIT(NL80211_IFTYPE_STATION);
	hw_priv->if_limits3[1].max = 1;

	hw_priv->if_limits3[1].types = BIT(NL80211_IFTYPE_AP);

	/* TODO:COMBO: mac80211 doesn't yet support more than 1
	 * different channel */
	hw_priv->if_combs[0].num_different_channels = 2;

	hw_priv->if_combs[0].max_interfaces = 2;
	
	hw_priv->if_combs[0].limits = hw_priv->if_limits1;
	hw_priv->if_combs[0].n_limits = 2;

	hw_priv->if_combs[1].num_different_channels = 2;

	hw_priv->if_combs[1].max_interfaces = 2;

	hw_priv->if_combs[1].limits = hw_priv->if_limits2;
	hw_priv->if_combs[1].n_limits = 1;

	hw_priv->if_combs[2].num_different_channels = 2;

	hw_priv->if_combs[2].max_interfaces = 2;

	hw_priv->if_combs[2].limits = hw_priv->if_limits3;
	hw_priv->if_combs[2].n_limits = 2;

	hw->wiphy->iface_combinations = &hw_priv->if_combs[0];
	hw->wiphy->n_iface_combinations = 3;

#ifdef	CONFIG_ATBM_5G_PRETEND_2G
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 9, 0))
	hw_priv->if_combs[0].radar_detect_widths =	BIT(NL80211_CHAN_NO_HT) |
					BIT(NL80211_CHAN_HT20) |
					BIT(NL80211_CHAN_HT40MINUS) |
					BIT(NL80211_CHAN_HT40PLUS);
	hw_priv->if_combs[1].radar_detect_widths =	BIT(NL80211_CHAN_NO_HT) |
					BIT(NL80211_CHAN_HT20) |
					BIT(NL80211_CHAN_HT40MINUS) |
					BIT(NL80211_CHAN_HT40PLUS);
	hw_priv->if_combs[2].radar_detect_widths =	BIT(NL80211_CHAN_NO_HT) |
					BIT(NL80211_CHAN_HT20) |
					BIT(NL80211_CHAN_HT40MINUS) |
					BIT(NL80211_CHAN_HT40PLUS);
#endif
#endif

}
bool atbm_support_tid_aggr(struct atbm_common *hw_priv,u8 tid)
{
	hw_priv->ba_tid_tx_mask = ampdu;
	return (BIT(tid) & hw_priv->ba_tid_tx_mask) ? true:false;
}
void atbm_monitor_pc(struct atbm_common *hw_priv);
//#ifdef RESET_CHANGE
struct atbm_common *g_hw_priv=0;
//#endif
#ifdef CONFIG_TXPOWER_DCXO_VALUE
//txpower and dcxo config file
char *strfilename = CONFIG_TXPOWER_DCXO_VALUE;
#else
char *strfilename = "/tmp/atbm_txpwer_dcxo_cfg.txt";
#endif

struct atbm_common * atbm_wifi_comb_priv_get(void)
{
	return g_hw_priv;
}
EXPORT_SYMBOL(atbm_wifi_comb_priv_get);
struct sbus_priv * atbm_wifi_comb_buspriv_get(void)
{
	if(g_hw_priv)
		return g_hw_priv->sbus_priv;
	else
		return NULL;
}
EXPORT_SYMBOL(atbm_wifi_comb_buspriv_get);

int get_rate_delta_gain(int *dst)
{
	int i=0,count = 0,readnum=0,val = 0;
	char *file = NULL;
	//char readbuf[1024] = {0};
	char *readbuf;
	char *pdata[64] = {NULL};
	char *pstr = NULL;
#ifdef CONFIG_RATE_TXPOWER
	file = CONFIG_RATE_TXPOWER;
#else
	file = "/tmp/set_rate_power.txt";
#endif
	readbuf = atbm_kmalloc(1024, GFP_KERNEL);	
	if(dst == NULL)
	{
		atbm_printk_err("Invalid parameter,dst == NULL\n");
		return -1;
	}
	if(file == NULL)
	{
		atbm_printk_err("Invalid parameter,file == NULL\n");
		return -1;
	}
	
	if((readnum = access_file(file,readbuf,1024,1)) > 0)
	{
		pdata[count] = readbuf;
		count++;
		
		for(i = 0;i < readnum;i++)
		{
			if((readbuf[i]=='\n') && (count<=45))
			{	
				if(readnum != (i+1)){
					pdata[count] = readbuf+i+1;
					count++;
				}
				readbuf[i] = '\0';
			}
		}
		for(i=0;i<45;i++)
		{
			if(pdata[i])
			{
				atbm_printk_err("%d,%s\n",i,pdata[i]);
				pstr=strchr(pdata[i],'=');
				if(pstr)
				{
				
					sscanf(pstr,"=%d",&val);
					dst[i] = val;
					//atbm_printk_err("%d,%s\n",i,pstr);
					pstr = NULL;
				}
			}
				
		}
	}
	atbm_kfree(readbuf);	
	return 0;
}
void atbm_get_delta_gain(char *srcData,int *allgain,int *bgain,int *gngain)
{
	int i=0;
	char *databuf = srcData;
	if((!databuf)||(!allgain)||(!bgain)||(!gngain))
	{
		atbm_printk_err("atbm_get_delta_gain fail,invalid parameters\n");
		return;
	}
	for(i=0;i<strlen(databuf);i++)
	{
		if(databuf[i] == '\n')
			databuf[i] = ' ';
	}
	sscanf(databuf, "delta_gain1:%d delta_gain2:%d delta_gain3:%d dcxo:%d",
		&allgain[0], &allgain[1], &allgain[2], &allgain[3]);
	/*
	sscanf(databuf, "delta_gain1:%d delta_gain2:%d delta_gain3:%d dcxo:%d \
		b_delta_gain1:%d b_delta_gain2:%d b_delta_gain3:%d \
		gn_delta_gain1:%d gn_delta_gain2:%d gn_delta_gain3:%d ",
		&allgain[0], &allgain[1], &allgain[2], &allgain[3],
		&bgain[0], &bgain[1], &bgain[2],&gngain[0], &gngain[1], &gngain[2]);
	*/
	
}

extern int init_cfg_rate_power(void);

int atbm_core_probe(const struct sbus_ops *sbus_ops,
		      struct sbus_priv *sbus,
		      struct device *pdev,
		      struct atbm_common **pself)
{
	int err = -ENOMEM;
	struct ieee80211_hw *dev;
	struct atbm_common *hw_priv=NULL;
	struct wsm_operational_mode mode = {
		.power_mode = wsm_power_mode_quiescent,
		.disableMoreFlagUsage = true,
	};
	int i = 0;
	int if_id;
	char readbuf[256] = "";
	int deltagain[4]={0};
	int bgain[3]={0};
	int gngain[3]={0};
	//int dcxo = 0;
	int gpio4 = 1;
	u32 chip_flag = 0;
#ifdef CUSTOM_FEATURE_PSM/* To control ps mode */
	char buffer[2];
    savedpsm = mode.power_mode;
	FUNC_ENTER();
	if(access_file(PATH_WIFI_PSM_INFO,buffer,2,1) > 0) {
		if(buffer[0] == 0x30) {
			mode.power_mode = wsm_power_mode_active;
		}
		else
		{
			if(savedpsm)
				mode.power_mode = savedpsm;
			else /* Set default */
				mode.power_mode = wsm_power_mode_quiescent;
		}
		atbm_printk_init("apollo wifi : PSM changed to %d\n",mode.power_mode);
	}
	else {
		atbm_printk_init("apollo wifi : Using default PSM %d\n",mode.power_mode);
	}
#endif

	dev = atbm_init_common(sizeof(struct atbm_common));
	if (!dev)
		goto err;

	hw_priv = dev->priv;
	atbm_hw_priv_assign_pointer(hw_priv);
	hw_priv->init_done = 0;
	atomic_set(&hw_priv->atbm_pluged,0);
	hw_priv->sbus_ops = sbus_ops;
	hw_priv->sbus_priv = sbus;
	hw_priv->pdev = pdev;
	SET_IEEE80211_DEV(hw_priv->hw, pdev);
	*pself = dev->priv;
	/* WSM callbacks. */
	hw_priv->wsm_cbc.scan_complete = atbm_scan_complete_cb;
	hw_priv->wsm_cbc.tx_confirm = atbm_tx_confirm_cb;
	hw_priv->wsm_cbc.rx = atbm_rx_cb;
	hw_priv->wsm_cbc.suspend_resume = atbm_suspend_resume;

	if(atbm_hif_trace_init(hw_priv) == false){
		goto err1;
	}
	/* hw_priv->wsm_cbc.set_pm_complete = atbm_set_pm_complete_cb; */
#ifdef CONFIG_PM
	err = atbm_pm_init(&hw_priv->pm_state, hw_priv);
	if (err) {
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Cannot init PM. (%d).\n",
				err);
		goto err1;
	}
#endif
	err = atbm_register_bh(hw_priv);
	if (err)
		goto err1;
	/*
	*init bus tx/rx
	*/
	if(hw_priv->sbus_ops->sbus_xmit_func_init)
		err = hw_priv->sbus_ops->sbus_xmit_func_init(hw_priv->sbus_priv);
	if(hw_priv->sbus_ops->sbus_rev_func_init)
		err |= hw_priv->sbus_ops->sbus_rev_func_init(hw_priv->sbus_priv);

	if(err){
		atbm_printk_err("rev and xmit init err\n");
		goto err1;
	}
	atomic_set(&hw_priv->atbm_pluged,1);
reload_fw:
	err = atbm_load_firmware(hw_priv);
	if (err){
		atbm_printk_err("atbm_load_firmware ERROR!\n");
		goto err2;
	}

	ABwifi_set_ifce_comb(hw_priv, dev);
	hw_priv->sbus_ops->lock(hw_priv->sbus_priv);
	WARN_ON(hw_priv->sbus_ops->set_block_size(hw_priv->sbus_priv,
		SDIO_BLOCK_SIZE));
	atbm_printk_init("set_block_size=%d\n", SDIO_BLOCK_SIZE);
	hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);

	hw_priv->init_done = 1;
//fix mstar CVT suspend bug,in CVT mstar suspend wait_event_interruptible_timeout sometime will not delay
	{
		int loop =0;
		atbm_printk_init("mdelay wait wsm_startup_done  !!\n");
		while(hw_priv->wsm_caps.firmwareReady !=1){
			/*
			*if the system is not smp, mdelay may not triger schedule.
			*So , after mdelay ,we call schedule_timeout_interruptible to release cpu
			*/
			mdelay(10);
			schedule_timeout_interruptible(msecs_to_jiffies(20));
			if(loop++>100){
				atbm_printk_init(" wsm_startup_done timeout ERROR !!\n");		
				atbm_monitor_pc(hw_priv);
				if(hw_priv->sbus_ops->sbus_reset_chip){
						atbm_printk_err("reload fw\n");
						if(hw_priv->sbus_ops->irq_unsubscribe)
							hw_priv->sbus_ops->irq_unsubscribe(hw_priv->sbus_priv);
						hw_priv->sbus_ops->sbus_reset_chip(hw_priv->sbus_priv);
						goto reload_fw;
				}else {
					err = -ETIMEDOUT;
					goto err3;
				}
			}
		}
	}

#if 1
		chip_flag = atbmwifi_efuse_read_chip_flag();	
	
	   /*check lmac capability based on efuse setting
		  firmwareCap is not chip flag, processed in lmac */
	   if((chip_flag & BIT(0)))//((hw_priv->wsm_caps.firmwareCap &CAPABILITIES_CHIP_FLAG))
	   {
			if((hw_priv->wsm_caps.firmwareCap &CAPABILITIES_BLE) == 0)	//not support BLE
			{
		#ifdef CONFIG_WIFI_BT_COMB
				atbm_printk_init("LMAC NOT SUPPORT CAPABILITIES_BLE <ERROR>\n");
				WARN_ON(1);
				goto err3;
	    #endif
			}	
			//error support 40m, but no LDPC
			if((hw_priv->wsm_caps.firmwareCap &CAPABILITIES_40M)&&((hw_priv->wsm_caps.firmwareCap &CAPABILITIES_LDPC) == 0))
			{
				atbm_printk_init("LMAC SUPPORT 40M BUT NOT SUPPORT LDPC  <ERROR>\n");
				WARN_ON(1);
				goto err3;
			}
			
			if((hw_priv->wsm_caps.firmwareCap &CAPABILITIES_40M) == 0)	//not support 40M
			{
				atbm_band_2ghz.ht_cap.cap &=  ~(IEEE80211_HT_CAP_SUP_WIDTH_20_40|IEEE80211_HT_CAP_SGI_40);
				atbm_printk_init("LMAC NOT SUPPORT 40M !!!\n");
			}
			
			if((hw_priv->wsm_caps.firmwareCap &CAPABILITIES_LDPC) == 0)  //not support LDPC
			{
				if((atbm_band_2ghz.ht_cap.cap &  IEEE80211_HT_CAP_SUP_WIDTH_20_40) == 0){	//not support 40M
					atbm_band_2ghz.ht_cap.cap &=  ~IEEE80211_HT_CAP_LDPC_CODING;			
					//atbm_band_2ghz.iftype_data->he_cap.he_cap_elem.phy_cap_info[1] &= ~ATBM_IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
					atbm_support_he_cap.he_cap_elem.phy_cap_info[1] &= ~ATBM_IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
					atbm_support_he_cap.he_mcs_nss_supp.rx_mcs_80 = cpu_to_le16(0xfffd);  //bit0-bit1 set 1, mcs0-9
					atbm_support_he_cap.he_mcs_nss_supp.tx_mcs_80 = cpu_to_le16(0xfffd);					
					he_ldpc = 0; 
					atbm_printk_init("LMAC NOT SUPPORT LDPC !!!\n");
				}		
#if defined (CONFIG_ATBM_APOLLO_5GHZ_SUPPORT) || defined (CONFIG_ATBM_5G_PRETEND_2G)
				if((atbm_band_5ghz.ht_cap.cap &  IEEE80211_HT_CAP_SUP_WIDTH_20_40) == 0){	//not support 40M
					atbm_band_5ghz.ht_cap.cap &=  ~IEEE80211_HT_CAP_LDPC_CODING;			
					//atbm_band_2ghz.iftype_data->he_cap.he_cap_elem.phy_cap_info[1] &= ~ATBM_IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
					atbm_support_5g_he_cap.he_cap_elem.phy_cap_info[1] &= ~ATBM_IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
					atbm_support_5g_he_cap.he_mcs_nss_supp.rx_mcs_80 = cpu_to_le16(0xfffd);  //bit0-bit1 set 1, mcs0-9
					atbm_support_5g_he_cap.he_mcs_nss_supp.tx_mcs_80 = cpu_to_le16(0xfffd); 				
					he_ldpc = 0; 
					atbm_printk_init("LMAC NOT SUPPORT 5G LDPC !!!\n");
				}		
#endif
			}		
	   }
		/*end lmac capability check*/
#endif



	atbm_firmware_init_check(hw_priv);
	for (if_id = 0; if_id < ABwifi_get_nr_hw_ifaces(hw_priv); if_id++) {
		/* Set low-power mode. */
		err = wsm_set_operational_mode(hw_priv, &mode, if_id);
		if (err) {
			WARN_ON(1);
			goto err3;
		}
		/* Enable multi-TX confirmation */
		err = wsm_use_multi_tx_conf(hw_priv, true, if_id);
		if (err) {
		}
	}
	atbm_set_fw_ver(hw_priv);
	atbm_get_mac_address(hw_priv);
	if (efuse)
	{
		char buffer[15];
		if(access_file(PATH_WIFI_EFUSE,buffer,15,1) > 0) {
			err = wsm_set_efuse_data(hw_priv, buffer, 15);
			if (err) {
				WARN_ON(1);
				goto err3;
			}
			atbm_printk_init("apollo wifi : Set efuse data\n");
		}
	}
	{
		struct efuse_headr efuse_data;
		err = wsm_get_efuse_data(hw_priv, (void *)&efuse_data, sizeof(efuse_data));
		if (err) {
			WARN_ON(1);
			goto err3;
		}
		atbm_printk_init("efuse data is [0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x:0x%x:0x%x:0x%x:0x%x:0x%x]\n",
				efuse_data.version,efuse_data.dcxo_trim,efuse_data.delta_gain1,efuse_data.delta_gain2,efuse_data.delta_gain3,
				efuse_data.Tj_room,efuse_data.topref_ctrl_bias_res_trim,efuse_data.PowerSupplySel,efuse_data.mac[0],efuse_data.mac[1],
				efuse_data.mac[2],efuse_data.mac[3],efuse_data.mac[4],efuse_data.mac[5]);
		memcpy(&hw_priv->efuse,&efuse_data,sizeof(struct efuse_headr));
	}


	
	//use delta_gain and dcxo value in config file,when file is exist
	//if(hw_priv->chip_version >= ARES_B){

	//}
	

	atbm_Default_driver_configuration(hw_priv);

	
	err = atbm_register_common(dev);
	if (err) {
		goto err3;
	}

	
	
#ifdef CONFIG_PM
	atbm_pm_stay_awake(&hw_priv->pm_state, 6 * HZ);
#endif  //#ifdef CONFIG_PM
	*pself = dev->priv;
	
	atbm_wifi_status_set(1);

	return err;

err3:
	//sbus_ops->reset(sbus);
	hw_priv->sbus_ops->irq_unsubscribe(hw_priv->sbus_priv);
err2:
	atomic_set(&hw_priv->atbm_pluged,0);
	atbm_unregister_bh(hw_priv);
err1:
	/*
	*init bus tx/rx
	*/
	if(hw_priv->sbus_ops->sbus_xmit_func_deinit)
		hw_priv->sbus_ops->sbus_xmit_func_deinit(hw_priv->sbus_priv);
	if(hw_priv->sbus_ops->sbus_rev_func_deinit)
		hw_priv->sbus_ops->sbus_rev_func_deinit(hw_priv->sbus_priv);	
	wsm_buf_deinit(&hw_priv->wsm_cmd_buf);
	for (i = 0; i < 4; ++i)
		atbm_queue_deinit(&hw_priv->tx_queue[i]);
	atbm_queue_stats_deinit(&hw_priv->tx_queue_stats);
#ifdef CONFIG_PM
	atbm_pm_deinit(&hw_priv->pm_state);
#endif
	atbm_hif_trace_exit(hw_priv);
	atbm_free_common(dev);
err:
	*pself=NULL;
	if(hw_priv)
		hw_priv->init_done = 0;
	return err;
}
//EXPORT_SYMBOL_GPL(atbm_core_probe);

void atbm_core_release(struct atbm_common *self)
{
	atbm_unregister_common(self->hw);
	atbm_hif_trace_exit(self);
	atbm_free_common(self->hw);
	return;
}
//EXPORT_SYMBOL_GPL(atbm_core_release);
#ifdef CUSTOM_FEATURE_MAC /* To use macaddr and ps mode of customers */
int access_file(char *path, char *buffer, int size, int isRead)
{
	int ret=0;
	struct file *fp;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
	mm_segment_t old_fs = get_fs();
#endif
	if(isRead)
		fp = filp_open(path,O_RDONLY,S_IRUSR);
	else
		fp = filp_open(path,O_CREAT|O_WRONLY,S_IRUSR);

	if (IS_ERR(fp)) {
		atbm_printk_err("apollo wifi : can't open %s\n",path);
		return -1;
	}

	if(isRead)
	{
		{
			fp->f_pos = 0;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
			set_fs(KERNEL_DS);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
			ret = kernel_read(fp,buffer,size,&fp->f_pos);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 84))
			ret = kernel_read(fp,fp->f_pos,buffer,size);

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 14))
			ret = kernel_read(fp,&fp->f_pos,buffer,size);
#else
			ret = vfs_read(fp,buffer,size,&fp->f_pos);
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
			set_fs(old_fs);
#endif
		}
	}
	else
	{
		if(fp->f_op->write == NULL) {
			atbm_printk_err("apollo wifi : Not allow to write\n");
			return -2;
		}
		else {
			fp->f_pos = 0;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
			set_fs(KERNEL_DS);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
			ret = kernel_write(fp,buffer,size,&fp->f_pos);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 84))
			ret = kernel_write(fp,buffer,size,fp->f_pos);

#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 14))
			ret = kernel_write(fp,&fp->f_pos,buffer,size);
#else
			ret = vfs_write(fp,buffer,size,&fp->f_pos);
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0))
			set_fs(old_fs);
#endif
		}
	}
	filp_close(fp,NULL);

	atbm_printk_debug("apollo wifi : access_file return code(%d)\n",ret);
	return ret;
}
#endif
#if defined(CONFIG_NL80211_TESTMODE) || defined(CONFIG_ATBM_IOCTRL)


void atbm_set_shortGI(u32 shortgi)
{
	atbm_printk_debug("atbm_set_shortGI,%d\n", shortgi);
	if (shortgi)
	{
		atbm_band_2ghz.ht_cap.cap |=  IEEE80211_HT_CAP_SGI_20;
		if (atbm_band_2ghz.ht_cap.cap &  IEEE80211_HT_CAP_SUP_WIDTH_20_40)
			atbm_band_2ghz.ht_cap.cap |=  IEEE80211_HT_CAP_SGI_40;
	}else
	{
		atbm_band_2ghz.ht_cap.cap &=  ~(IEEE80211_HT_CAP_SGI_20|IEEE80211_HT_CAP_SGI_40);
	}

}
#endif

