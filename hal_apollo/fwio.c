/*
 * Firmware I/O code for mac80211 altobeam APOLLO drivers
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

#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/debugfs.h>

#include "apollo.h"
#include "fwio.h"
#include "hwio.h"
#include "sbus.h"
#include "debug.h"
#include "bh.h"
#include "dcxo_dpll.h"

#ifdef ATBM_USE_SAVED_FW
#pragma message("Suspend Save Firmware")
#endif
#ifdef CONFIG_USE_FW_H
#pragma message("Use Firmware.h")

#else

//static char *fw = NULL;//FIRMWARE_DEFAULT_PATH;
static char *fw = FIRMWARE_DEFAULT_PATH;

#if 0
char firmware_bin[][32]={
#ifdef USB_BUS
		{"ATBM606x_fw_usb.bin"},
		{"ATBM606x_blecomb_usb.bin"}
#else
		{"ATBM606x_fw_sdio.bin"},
		{"ATBM606x_blecomb_sdio.bin"}
#endif
};
#endif

#pragma message(FIRMWARE_DEFAULT_PATH)
#endif




static struct firmware_altobeam atbm_fw;

extern int atbm_wifi_bt_comb_get(void);
//extern void atbm_wifi_bt_comb_set(int status);

void atbm_release_firmware(void)
{
	if(atbm_fw.fw_dccm)
	{
		vfree(atbm_fw.fw_dccm);
		atbm_fw.fw_dccm = NULL;
	}
	if(atbm_fw.fw_iccm)
	{
		vfree(atbm_fw.fw_iccm);
		atbm_fw.fw_iccm = NULL;
	}
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
#ifdef CONFIG_USE_FW_H

	if(atbm_wifi_bt_comb_get() == 1){
#endif
		if(atbm_fw.fw_sram)
		{
			vfree(atbm_fw.fw_sram);
			atbm_fw.fw_sram = NULL;
		}
#ifdef CONFIG_USE_FW_H

	}
#endif
#endif
}
int atbm_init_firmware(void)
{
	memset(&atbm_fw,0,sizeof(struct firmware_altobeam));
	return 0;
}

int atbm_set_firmare(struct firmware_altobeam *fw)
{
#ifdef ATBM_USE_SAVED_FW
	if(!fw || (!fw->fw_dccm&&!fw->fw_iccm))
	{
		atbm_printk_err(KERN_ERR "fw is err\n");
		return -1;
	}

	if(atbm_fw.fw_dccm || atbm_fw.fw_iccm)
	{
		atbm_printk_err("atbm_fw has been set\n");
		return -1;
	}
	memcpy(&atbm_fw.hdr,&fw->hdr,sizeof(struct firmware_headr));
	
	if(atbm_fw.hdr.iccm_len)
	{
		atbm_fw.fw_iccm = vmalloc(atbm_fw.hdr.iccm_len);
		atbm_printk_err("%s:fw_iccm(%p)\n",__func__,atbm_fw.fw_iccm);
		if(!atbm_fw.fw_iccm)
		{
			atbm_printk_err("alloc atbm_fw.fw_iccm err\n");
			goto err;
		}
		memcpy(atbm_fw.fw_iccm,fw->fw_iccm,atbm_fw.hdr.iccm_len);
	}

	if(atbm_fw.hdr.dccm_len)
	{
		atbm_fw.fw_dccm= vmalloc(atbm_fw.hdr.dccm_len);
		
		atbm_printk_err("%s:fw_dccm(%p)\n",__func__,atbm_fw.fw_dccm);
		if(!atbm_fw.fw_dccm)
		{
			atbm_printk_err("alloc atbm_fw.fw_dccm err\n");
			goto err;
		}
		memcpy(atbm_fw.fw_dccm,fw->fw_dccm,atbm_fw.hdr.dccm_len);
	}
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
	if(atbm_wifi_bt_comb_get() == 1){

		if(atbm_fw.hdr.sram_len){
			atbm_fw.fw_sram = vmalloc(atbm_fw.hdr.sram_len);
			
			atbm_printk_err("%s:fw_sram(%p)\n",__func__,atbm_fw.fw_sram);
			if(!atbm_fw.fw_sram)
			{
				atbm_printk_err("alloc atbm_fw.fw_sram err\n");
				goto err;
			}
			memcpy(atbm_fw.fw_sram,fw->fw_sram,atbm_fw.hdr.sram_len);
		}
	}
#endif

	return 0;
err:
	if(atbm_fw.fw_iccm)
	{
		vfree(atbm_fw.fw_iccm);
		atbm_fw.fw_iccm = NULL;
	}

	if(atbm_fw.fw_dccm)
	{
		vfree(atbm_fw.fw_dccm);
		atbm_fw.fw_dccm = NULL;
	}
	
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
	if(atbm_wifi_bt_comb_get() == 1){

		if(atbm_fw.fw_sram)
		{
			vfree(atbm_fw.fw_sram);
			atbm_fw.fw_sram = NULL;
		}
	}
#endif

#endif //#ifndef USB_BUS
	return -1;
}

#define FW_IS_READY	((atbm_fw.fw_dccm != NULL) || (atbm_fw.fw_iccm != NULL))
int atbm_get_fw(struct firmware_altobeam *fw)
{
	if(!FW_IS_READY)
	{
		return -1;
	}

	memcpy(&fw->hdr,&atbm_fw.hdr,sizeof(struct firmware_headr));
	fw->fw_iccm = atbm_fw.fw_iccm;
	fw->fw_dccm = atbm_fw.fw_dccm;
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
	if(atbm_wifi_bt_comb_get() == 1)
		fw->fw_sram = atbm_fw.fw_sram;
#endif
	return 0;
}


int atbm_get_hw_type(u32 config_reg_val, int *major_revision)
{
#if 0
	int hw_type = -1;
	u32 config_value = config_reg_val;
	//u32 silicon_type = (config_reg_val >> 24) & 0x3;
	u32 silicon_vers = (config_reg_val >> 31) & 0x1;

	/* Check if we have CW1200 or STLC9000 */

	hw_type = HIF_1601_CHIP;
#endif
	return HIF_1601_CHIP;
}

static int atbm_load_firmware_generic(struct atbm_common *priv, u8 *data,u32 size,u32 addr)
{

	int ret=0;
	u32 put = 0;
	u8 *buf = NULL;


	buf = atbm_kmalloc(DOWNLOAD_BLOCK_SIZE*2, GFP_KERNEL | GFP_DMA);
	if (!buf) {
		atbm_dbg(ATBM_APOLLO_DBG_ERROR,
			"%s: can't allocate bootloader buffer.\n", __func__);
		ret = -ENOMEM;
		goto error;
	}
	
#ifndef HW_DOWN_FW
	if(priv->sbus_ops->bootloader_debug_config)
		priv->sbus_ops->bootloader_debug_config(priv->sbus_priv,0);
#endif //#ifndef HW_DOWN_FW

	/*  downloading loop */
	atbm_printk_init( "%s: addr %x: len %x\n",__func__,addr,size);
	for (put = 0; put < size ;put += DOWNLOAD_BLOCK_SIZE) {
		u32 tx_size;


		/* calculate the block size */
		tx_size  = min((size - put),(u32)DOWNLOAD_BLOCK_SIZE);

		memcpy(buf, &data[put], tx_size);

		/* send the block to sram */
		ret = atbm_fw_write(priv,put+addr,buf, tx_size);
		if (ret < 0) {
			atbm_dbg(ATBM_APOLLO_DBG_ERROR,
				"%s: can't write block at line %d.\n",
				__func__, __LINE__);
			goto error;
		}
	} /* End of bootloader download loop */

error:
	atbm_kfree(buf);
	return ret;


}
void  atbm_efuse_read_byte(struct atbm_common *priv,u32 byteIndex, u32 *value)
{
	//HW_WRITE_REG(0x16b00000, (byteIndex<<8));
	//*value = HW_READ_REG(0x16b00004);	
	if(priv->sbus_ops && priv->sbus_ops->lock)
		priv->sbus_ops->lock(priv->sbus_priv);
	atbm_direct_write_reg_32(priv,0x16b00000, (byteIndex<<8));
	atbm_direct_read_reg_32(priv,0x16b00004,value);
	if(priv->sbus_ops && priv->sbus_ops->unlock)
		priv->sbus_ops->unlock(priv->sbus_priv);
}

u32 atbm_efuse_read_bit(struct atbm_common *priv,u32 bitIndex)
{
	u32	efuseBitIndex = bitIndex;
	u32 byteIndex;
	u32 value = 0;

	{
		byteIndex = efuseBitIndex / 8;
		atbm_efuse_read_byte(priv,byteIndex, &value);
	}
	value = value >> (efuseBitIndex % 8);
	value &= 0x1;
	return value;
}
int atbm_check_crouns_type(struct atbm_common *hw_priv)
{
//#ifdef USB_BUS
//	hw_priv->loader_ble = 1;
	hw_priv->loader_ble = 0;

	if(atbm_efuse_read_bit(hw_priv,14) == 1){
		if(atbm_wifi_bt_comb_get() == 1)
			hw_priv->loader_ble = 1;
		
			
		if(atbm_efuse_read_bit(hw_priv,15) == 1){
			atbm_printk_always("not support ldpc & ht40!\n");
			hw_priv->chip_version = CRONUS_NO_HT40_LDPC;
		}else{
			atbm_printk_always("not support ht40!\n");
			hw_priv->chip_version = CRONUS_NO_HT40;
		}
	}else if (atbm_efuse_read_bit(hw_priv,13) == 1){
		atbm_printk_always("not support BLE!\n");
		hw_priv->chip_version = CRONUS_NO_BLE;
		hw_priv->loader_ble = 0;
	}else{
		if(atbm_wifi_bt_comb_get() == 1)
			hw_priv->loader_ble = 1;
	}
//#endif 
	return 0;
}

void  atbm_get_chiptype(struct atbm_common *hw_priv)
{
	u32 chipver = 0;

	atbm_direct_read_reg_32(hw_priv,0x0acc017c,&chipver);
    chipver&=0xff;
	
	hw_priv->chip_version = ARES_B;
	switch(chipver)
	{
		case 0x14:	
			hw_priv->chip_version = APOLLO_F;	
			break;
		case 0x24:	
		case 0x25:	
			//strHwChipFw = ("AthenaB.bin");
			hw_priv->chip_version = ATHENA_B;
			break;
		case 0x45:	
		case 0x46:	
		case 0x47:	
			hw_priv->chip_version = ARES_A;
			break;	
		case 0x49:
			hw_priv->chip_version = ARES_B;	
			break;
		case 0x64:
		case 0x65:
			hw_priv->chip_version = HERA;		
			break;
		default:
			hw_priv->chip_version = CRONUS;
			//g_wifi_chip_type = ATHENA_B;
			atbm_check_crouns_type(hw_priv);
		break;
	}

	atbm_printk_always("%s, chipver=0x%x, g_wifi_chip_type[%d]\n",__func__, chipver,hw_priv->chip_version );
}

#ifndef  CONFIG_USE_FW_H
char * atbm_HwGetChipFw(struct atbm_common *priv)
{

	char * strHwChipFw = NULL;
#if 0
	if(atbm_wifi_bt_comb_get() == 1){
		fw = firmware_bin[1];
	}else
		fw=firmware_bin[0];
#endif
	
	if(fw)
	{
		atbm_printk_always("fw [%s]\n", fw );
	 	return fw;
	}

	return strHwChipFw;
}
#endif
//#define TEST_DCXO_CONFIG move to makefile
#ifndef CONFIG_USE_FW_H
#define USED_FW_FILE
#endif
#ifdef USED_FW_FILE
/*check if fw headr ok*/
static int atbm_fw_checksum(struct firmware_headr * hdr)
{
	return 1;
}
#if 0
//#else
#ifdef USB_BUS
#include "firmware_usb.h"
#endif
#ifdef SDIO_BUS
#include "firmware_sdio.h"
#endif
#ifdef SPI_BUS
#include "firmware_spi.h"
#endif

#endif


#endif
#ifdef CONFIG_PM_SLEEP
#pragma message("CONFIG_PM_SLEEP")
int atbm_cache_fw_before_suspend(struct device	 *pdev)
{
#if defined (USED_FW_FILE) && defined(ATBM_USE_SAVED_FW)
	int ret = 0;
	const char *fw_path= fw;
	const struct firmware *firmware = NULL;
	struct firmware_altobeam fw_altobeam;

	memset(&fw_altobeam,0,sizeof(struct firmware_altobeam));
	if(fw_path == NULL){
		goto error2;
	}
	if(FW_IS_READY){
		atbm_printk_err("atbm_fw ready\n");
		goto error2;
	}

	ret = request_firmware(&firmware, fw_path, pdev);
	if(ret){
		atbm_printk_err("request_firmware err\n");
		goto error2;
	}
	if(*(int *)firmware->data == ALTOBEAM_WIFI_HDR_FLAG){
		memcpy(&fw_altobeam.hdr,firmware->data,sizeof(struct firmware_headr));
		if(atbm_fw_checksum(&fw_altobeam.hdr)==0){
			ret = -1;
			 atbm_dbg(ATBM_APOLLO_DBG_ERROR,"%s: atbm_fw_checksum fail 11\n", __func__);
			 goto error1;
		}
		fw_altobeam.fw_iccm = (u8 *)firmware->data + sizeof(struct firmware_headr);
		fw_altobeam.fw_dccm = fw_altobeam.fw_iccm + fw_altobeam.hdr.iccm_len;
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
		if(atbm_wifi_bt_comb_get() == 1)
			fw_altobeam.fw_sram = fw_altobeam.fw_dccm + fw_altobeam.hdr.dccm_len;
#endif
		atbm_dbg(ATBM_APOLLO_DBG_ERROR,"%s: have header,lmac version(%d) iccm_len(%d) dccm_len(%d)\n", __func__,
			fw_altobeam.hdr.version,fw_altobeam.hdr.iccm_len,fw_altobeam.hdr.dccm_len);
	}
	else {
		fw_altobeam.hdr.version =  0x001;
		if(firmware->size > DOWNLOAD_ITCM_SIZE){
			fw_altobeam.hdr.iccm_len =  DOWNLOAD_ITCM_SIZE;
			fw_altobeam.hdr.dccm_len =  firmware->size - fw_altobeam.hdr.iccm_len;
			if(fw_altobeam.hdr.dccm_len > DOWNLOAD_DTCM_SIZE) {
				ret = -1;
			 	atbm_dbg(ATBM_APOLLO_DBG_ERROR,"%s: atbm_fw_checksum fail 22\n", __func__);
			 	goto error1;
			}
			fw_altobeam.fw_iccm = (u8 *)firmware->data;
			fw_altobeam.fw_dccm = fw_altobeam.fw_iccm+fw_altobeam.hdr.iccm_len;
		}
		else {
			fw_altobeam.hdr.iccm_len = firmware->size;
			fw_altobeam.hdr.dccm_len = 0;
			fw_altobeam.fw_iccm = (u8 *)firmware->data;
			
		}

	}
	atbm_release_firmware();
	
	memcpy(&atbm_fw.hdr,&fw_altobeam.hdr,sizeof(struct firmware_headr));
	if(atbm_fw.hdr.iccm_len)
	{
		atbm_fw.fw_iccm = vmalloc(atbm_fw.hdr.iccm_len);
		
		if(!atbm_fw.fw_iccm)
		{
			atbm_printk_err( "alloc atbm_fw.fw_iccm err\n");
			goto error1;
		}
		memcpy(atbm_fw.fw_iccm,fw_altobeam.fw_iccm,atbm_fw.hdr.iccm_len);
	}

	if(atbm_fw.hdr.dccm_len)
	{
		atbm_fw.fw_dccm = vmalloc(atbm_fw.hdr.dccm_len);

		if(!atbm_fw.fw_dccm)
		{
			atbm_printk_err("alloc atbm_fw.fw_dccm err\n");
			goto error1;
		}
		memcpy(atbm_fw.fw_dccm,fw_altobeam.fw_dccm,atbm_fw.hdr.dccm_len);
	}
	
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
	if(atbm_wifi_bt_comb_get() == 1){

		if(atbm_fw.hdr.sram_len){
			atbm_fw.fw_sram = vmalloc(atbm_fw.hdr.sram_len);
			if(!atbm_fw.fw_sram)
			{
				atbm_printk_err("alloc atbm_fw.fw_dccm err\n");
				goto error1;
			}
			memcpy(atbm_fw.fw_sram,fw_altobeam.fw_sram,atbm_fw.hdr.sram_len);		
		}
	}
#endif
	
	atbm_printk_always("%s:cached fw\n",__func__);
	release_firmware(firmware);
	return 0;
error1:
	
	atbm_printk_err("%s:error1\n",__func__);
	release_firmware(firmware);
	if(atbm_fw.fw_iccm)
	{
		vfree(atbm_fw.fw_iccm);
		atbm_fw.fw_iccm = NULL;
	}

	if(atbm_fw.fw_dccm)
	{
		vfree(atbm_fw.fw_dccm);
		atbm_fw.fw_dccm = NULL;
	}
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
	if(atbm_wifi_bt_comb_get() == 1){

		if(atbm_fw.fw_sram)
		{
			vfree(atbm_fw.fw_sram);
			atbm_fw.fw_sram = NULL;		
		}
	}
#endif
error2:
	atbm_printk_err("%s:error2\n",__func__);
	return ret;
#else
	return 0;
#endif//
}
#endif
static int atbm_start_load_firmware(struct atbm_common *priv)
{

	int ret;
#ifdef USED_FW_FILE
	const char *fw_path= atbm_HwGetChipFw(priv);
#endif//
	const struct firmware *firmware = NULL;
	struct firmware_altobeam fw_altobeam;
loadfw:
	//u32 testreg_uart;
#ifdef START_DCXO_CONFIG
	u32 val32_1;
	atbm_ahb_write_32(priv,0x18e00014,0x200);
	atbm_ahb_read_32(priv,0x18e00014,&val32_1);
	//atbm_ahb_read_32(priv,0x16400000,&testreg_uart);
	atbm_printk_always("0x18e000e4-->%08x %08x\n",val32_1);
#endif//TEST_DCXO_CONFIG
	if(!FW_IS_READY)
	{
#ifdef USED_FW_FILE
	    atbm_dbg(ATBM_APOLLO_DBG_MSG,"%s:FW FILE = %s\n",__func__,fw_path);
		ret = request_firmware(&firmware, fw_path, priv->pdev);
		if (ret) {
			atbm_dbg(ATBM_APOLLO_DBG_ERROR,
				"%s: can't load firmware file %s.\n",
				__func__, fw_path);
			goto error;
		}
		BUG_ON(!firmware->data);
		if ((*(int*)firmware->data == ALTOBEAM_WIFI_HDR_FLAG) || (*(int*)firmware->data == ALTOBEAM_WIFI_HDR_FLAG_V2))
		{
			memcpy(&fw_altobeam.hdr,firmware->data,sizeof(struct firmware_headr));
			if (atbm_fw_checksum(&fw_altobeam.hdr) == 0) {
				ret = -1;
				 atbm_dbg(ATBM_APOLLO_DBG_ERROR,"%s: atbm_fw_checksum fail 11\n", __func__);
				 goto error;
			}
			fw_altobeam.fw_iccm = (u8 *)firmware->data + sizeof(struct firmware_headr);
			fw_altobeam.fw_dccm = fw_altobeam.fw_iccm + fw_altobeam.hdr.iccm_len;
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
			fw_altobeam.fw_sram = fw_altobeam.fw_dccm + fw_altobeam.hdr.dccm_len;
			if (*(int*)firmware->data == ALTOBEAM_WIFI_HDR_FLAG)
				fw_altobeam.hdr.sram_addr = DOWNLOAD_BLE_SRAM_ADDR;
#endif			
			atbm_dbg(ATBM_APOLLO_DBG_ERROR,"%s: have header,lmac version(%d) iccm_len(%d) dccm_len(%d),fwsize(%zu),hdrsize(%zu)\n", __func__,
				fw_altobeam.hdr.version,fw_altobeam.hdr.iccm_len,fw_altobeam.hdr.dccm_len,firmware->size,sizeof(struct firmware_headr));

			//frame_hexdump("fw_iccm ",fw_altobeam.fw_iccm,64);
			//frame_hexdump("fw_dccm ",fw_altobeam.fw_dccm,64);
		}
		else {
			fw_altobeam.hdr.version =  0x001;
			if(firmware->size > DOWNLOAD_ITCM_SIZE){
				fw_altobeam.hdr.iccm_len =  DOWNLOAD_ITCM_SIZE;
				fw_altobeam.hdr.dccm_len =  firmware->size - fw_altobeam.hdr.iccm_len;
				if(fw_altobeam.hdr.dccm_len > DOWNLOAD_DTCM_SIZE) {
					ret = -1;
				 	atbm_dbg(ATBM_APOLLO_DBG_ERROR,"%s: atbm_fw_checksum fail 22\n", __func__);
				 	goto error;
				}
				fw_altobeam.fw_iccm = (u8 *)firmware->data;
				fw_altobeam.fw_dccm = fw_altobeam.fw_iccm+fw_altobeam.hdr.iccm_len;
			}
			else {
				fw_altobeam.hdr.iccm_len = firmware->size;
				fw_altobeam.hdr.dccm_len = 0;
				fw_altobeam.fw_iccm = (u8 *)firmware->data;
				
			}

		}
#else //USED_FW_FILE
		{
		//atbm_dbg(ATBM_APOLLO_DBG_ERROR,"used firmware.h=\n");
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
		if(priv->loader_ble == 1){
			load_usb_wifi_bt_comb_firmware(&fw_altobeam);
			atbm_printk_err("\n======>>> load WIFI BLE COMB firmware <<<======\n\n");
		
		}else
#endif
		{
			atbm_printk_err("\n======>>> load only WIFI firmware <<<======\n\n");
			load_usb_wifi_firmware(&fw_altobeam);
		}
		//atbm_wifi_bt_comb_set(priv->wifi_ble_comb);
#if 0		
		fw_altobeam.hdr.iccm_len = sizeof(fw_code);
		fw_altobeam.hdr.dccm_len = sizeof(fw_data);
#ifdef CONFIG_ATBM_BLE_CODE_SRAM
		fw_altobeam.hdr.sram_len = sizeof(fw_sram);
		fw_altobeam.fw_sram = &fw_sram[0];
#endif
		fw_altobeam.fw_iccm = &fw_code[0];
		fw_altobeam.fw_dccm = &fw_data[0];
#endif
		}
#endif //USED_FW_FILE
		atbm_set_firmare(&fw_altobeam);
	}
	else
	{
		if((ret = atbm_get_fw(&fw_altobeam))<0)
		{
			goto error;
		}
	}
	atbm_dbg(ATBM_APOLLO_DBG_ERROR,"START DOWNLOAD ICCM=========\n");

	ret = atbm_load_firmware_generic(priv,fw_altobeam.fw_iccm,fw_altobeam.hdr.iccm_len,DOWNLOAD_ITCM_ADDR);
	if(ret<0)
		goto error;

	#ifdef USB_BUS
	//fw_altobeam.hdr.dccm_len = 0xd000;
	if (fw_altobeam.hdr.dccm_len > 0xd000)
	fw_altobeam.hdr.dccm_len = 0xd000;
	#else
	if(fw_altobeam.hdr.dccm_len > 0xd000)
	fw_altobeam.hdr.dccm_len = 0xd000;
	#endif			

	atbm_dbg(ATBM_APOLLO_DBG_ERROR,"START DOWNLOAD DCCM=========\n");
	ret = atbm_load_firmware_generic(priv,fw_altobeam.fw_dccm,fw_altobeam.hdr.dccm_len,DOWNLOAD_DTCM_ADDR);
	if(ret<0)
		goto error;

#ifdef CONFIG_ATBM_BLE_CODE_SRAM
	if(priv->loader_ble == 1){

		if(fw_altobeam.hdr.sram_len){
			atbm_dbg(ATBM_APOLLO_DBG_ERROR,"START DOWNLOAD BLE SRAM=========\n");
			ret = atbm_load_firmware_generic(priv, fw_altobeam.fw_sram, fw_altobeam.hdr.sram_len, fw_altobeam.hdr.sram_addr);
			if(ret<0)
				goto error;
		}
	}
#endif	


	atbm_dbg(ATBM_APOLLO_DBG_MSG, "FIRMWARE DOWNLOAD SUCCESS\n");

error:
	if (ret<0){
		if(atbm_reset_lmc_cpu(priv) == 0)
			goto loadfw;
	}
	if (firmware)
		release_firmware(firmware);
	return ret;


}


int atbm_load_firmware(struct atbm_common *hw_priv)
{
	int ret;
	
	atbm_get_chiptype(hw_priv);
	
	atbm_printk_init("atbm_before_load_firmware++\n");
	ret = atbm_before_load_firmware(hw_priv);
	if(ret <0)
		goto out;
	atbm_printk_init("atbm_start_load_firmware++\n");
	ret = atbm_start_load_firmware(hw_priv);
	if(ret <0)
		goto out;
	atbm_printk_init("atbm_after_load_firmware++\n");
	ret = atbm_after_load_firmware(hw_priv);
	if(ret <0){
		goto out;
	}
	ret =0;
out:
	return ret;

}

