/**************************************************************************************************************
 * altobeam RTOS wifi hmac source code 
 *
 * Copyright (c) 2018, altobeam.inc   All rights reserved.
 *
 *  The source code contains proprietary information of AltoBeam, and shall not be distributed, 
 *  copied, reproduced, or disclosed in whole or in part without prior written permission of AltoBeam.
*****************************************************************************************************************/
#ifndef ATBM_CFG_H_
#define ATBM_CFG_H_
/*5G support*/
#undef CONFIG_ATBMWIFI__5GHZ_SUPPORT
#undef CONFIG_5G_SUPPORT
#undef CONFIG_MONITOR
#define CONFIG_HT_MCS_STREAM_MAX_STREAMS	1
#define ATBM_ARRAY_SIZE(_array) (sizeof(_array)/sizeof(_array[0]))

#ifdef LINUX_OS
#define PLATFORM_XUNWEI				(1)
#define PLATFORM_SUN6I  			(2)
#define PLATFORM_FRIENDLY			(3)
#define PLATFORM_SUN6I_64			(4)	
#define PLATFORM_CDLINUX			(12)
#define PLATFORM_AMLOGIC_S805		(13)	
#define PLATFORM_AMLOGIC_905		(8)	
#define PLATFORM_SIGMASATR		(18)	

#ifndef  ATBM_WIFI_PLATFORM
#define ATBM_WIFI_PLATFORM			PLATFORM_SIGMASATR
#endif
#endif

#define RATE_INDEX_B_1M           0
#define RATE_INDEX_B_2M           1
#define RATE_INDEX_B_5_5M         2
#define RATE_INDEX_B_11M          3
#define RATE_INDEX_PBCC_22M       4     // not supported/unused
#define RATE_INDEX_PBCC_33M       5     // not supported/unused
#define RATE_INDEX_A_6M           6
#define RATE_INDEX_A_9M           7
#define RATE_INDEX_A_12M          8
#define RATE_INDEX_A_18M          9
#define RATE_INDEX_A_24M          10
#define RATE_INDEX_A_36M          11
#define RATE_INDEX_A_48M          12
#define RATE_INDEX_A_54M          13
#define RATE_INDEX_N_6_5M         14
#define RATE_INDEX_N_13M          15
#define RATE_INDEX_N_19_5M        16
#define RATE_INDEX_N_26M          17
#define RATE_INDEX_N_39M          18
#define RATE_INDEX_N_52M          19
#define RATE_INDEX_N_58_5M        20
#define RATE_INDEX_N_65M          21
#define RATE_INDEX_N_MCS32_6M       22
#define RATE_INDEX_MAX         		23

/*max num is 0xf*/
#define TEST_BASIC_RATE		(BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(6)|BIT(8)|BIT(10)|BIT(11))
#define TEST_LONG_RETRY_NUM		4
#define TEST_SHORT_RETRY_NUM	7
/*short 1:long 0*/
#define TEST_LONG_PREAMBLE		0
#define TEST_SHORT_PREAMBLE		1

#define TEST_BEACON_INTV    	100
#define TEST_DTIM_INTV    		3
#define DEFAULT_BEACON_LOSS_CNT      40
#define KEEP_ALIVE_PERIOD	(4)
#define ATBM_PKG_REORDER 1
#define BW_40M_SUPPORT  1
#define USE_MAIL_BOX 0
#define ATBM_RX_TASK_QUEUE 1
#define QUEUE_LIST  0
#define ATBM_RX_TASK 1
#define NEW_SUPPORT_PS 1
#define CONFIG_IEEE80211N 1
#define ATBM_P2P_ADDR_USE_LOCAL_BIT 1
/*WPS*/
#define CONFIG_WPS    1
#define CONFIG_WPS2    1
/*p2p*/
#define CONFIG_P2P 0
#if CONFIG_P2P
#undef CONFIG_WPS
#define CONFIG_WPS 1
#undef CONFIG_WPS2
#define CONFIG_WPS2 1
#endif
//can not support,maybe support later
#define CONFIG_WPS_UPNP 0
#define CONFIG_IEEE80211W 0
#define CONFIG_IEEE80211R 0
#define CONFIG_PEERKEY   0

#define RATE_CONTROL_MODE 1//1:pid ,2 minstrel

#define FAST_CONNECT_MODE 0
#define FAST_CONNECT_NO_SCAN 0
#define ATBM_SUPPORT_SMARTCONFIG 0

#define HI_RX_MUTIL_FRAME

#define ATBM_WSM_SDIO_TX_MULT 0

#ifdef USB_BUS_USE
#define ATBM_USB_BUS 1
#define ATBM_SDIO_BUS 0
#elif defined (USB_SDIO_USE)
#define ATBM_USB_BUS 0
#define ATBM_SDIO_BUS 1
#else
#define ATBM_USB_BUS 0
#define ATBM_SDIO_BUS 0
#endif


#if (ATBM_SDIO_BUS)
#define ATBM_TX_SKB_NO_TXCONFIRM 1
#define ATBM_TXRX_IN_ONE_THREAD 0
#define ATBM_WSM_SDIO_TX_MULT 0
#endif

#if (ATBM_USB_BUS)
#define ATBM_TX_SKB_NO_TXCONFIRM 1
#define CONFIG_USB_AGGR_URB_TX 0
#endif
#define TEST_DCXO_DPLL_CONFIG 0

#define ATHENA_LITE 	0
#define ATHENA_B		1
#define ARES_A  	 	2
#define ARES_B  	 	3
#define CRONUS  	 	4


#define PROJ_TYPE  		CRONUS


/*CUSTOM SELECT */
#define JIANRONG_RTOS_3298 0
#define JIANRONG_RTOS_3268 1
#define ALI_RTOS 2
#define AK_RTOS_200 3
#define AK_RTOS_300 4
#define AK_RTOS_37D 5
#define STM32_UCOS 6
#define SUN6I_LINUX 7

#define PLATFORM SUN6I_LINUX

#define CONFIG_WPA2_REINSTALL_CERTIFICATION  0

#define ATBM_DRIVER_ADDBA 1
#define ATBM_DRIVER_PHY_REG_INIT 1

#endif /*ATBM_CFG_H_*/

