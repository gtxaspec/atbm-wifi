/*
 * Firmware API for mac80211 altobeam APOLLO drivers
 * *
 * Copyright (c) 2016, altobeam
 * Author:
 *
 * Based on apollo code
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * Based on:
 * ST-Ericsson UMAC CW1200 driver which is
 * Copyright (c) 2010, ST-Ericsson
 * Author: Ajitpal Singh <ajitpal.singh@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FWIO_H_INCLUDED
#define FWIO_H_INCLUDED

#ifndef CONFIG_FW_NAME
#if ((ATBM_WIFI_PLATFORM == 13/*PLATFORM_AMLOGIC_S805*/) || (ATBM_WIFI_PLATFORM == 8))
#define FIRMWARE_DEFAULT_PATH	"../wifi/atbm/fw.bin"
#else // PLATFORM_AMLOGIC_S805
#define FIRMWARE_DEFAULT_PATH CONFIG_FW_NAME_WIFI6
#endif //PLATFORM_AMLOGIC_S805
#else 
#define FIRMWARE_DEFAULT_PATH CONFIG_FW_NAME
#define FIRMWARE_BLE_PATH CONFIG_FW_BLE_NAME 
#endif
#define SDD_FILE_DEFAULT_PATH	("sdd.bin")

#ifdef CONFIG_ATBM_BLE_CODE_SRAM
#ifdef CONFIG_WIFI_BT_COMB
#define DOWNLOAD_BLE_SRAM_ADDR		(0x09018000)
#define BLE_SRAM_CODE_SIZE			0x10000
#else//#ifdef CONFIG_WIFI_BT_COMB
#define DOWNLOAD_BLE_SRAM_ADDR		(0x0900A000)
#define BLE_SRAM_CODE_SIZE			0x16000
#endif  //#ifdef CONFIG_WIFI_BT_COMB
#endif//CONFIG_ATBM_BLE_CODE_SRAM

#define ATBM_APOLLO_REV_1601	(1601)
#define ALTOBEAM_WIFI_HDR_FLAG_V2  (0x34353678)

struct firmware_headr {
	u32 flags; /*0x34353677*/
	u32 version;
	u32 iccm_len;
	u32 dccm_len;
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
	u32 sram_len;
	u32 sram_addr;
	u32 reserve[1];
#else
	u32 reserve[3];
#endif
	u16 reserve2;
	u16 checksum;
};

struct firmware_altobeam {
	struct firmware_headr hdr;
	u8 *fw_iccm;
	u8 *fw_dccm;
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
	u8 *fw_sram;
#endif
};

struct atbm_common;


int atbm_get_hw_type(u32 config_reg_val, int *major_revision);

int atbm_load_firmware(struct atbm_common *hw_priv);
void  atbm_get_chiptype(struct atbm_common *hw_priv);
void atbm_release_firmware(void);
int atbm_init_firmware(void);


int load_usb_wifi_bt_comb_firmware(struct firmware_altobeam *fw_altobeam);
int load_usb_wifi_firmware(struct firmware_altobeam *fw_altobeam);


#ifdef CONFIG_PM_SLEEP
int atbm_cache_fw_before_suspend(struct device	 *pdev);
#endif
#endif
