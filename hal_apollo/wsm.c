/*
 * WSM host interface (HI) implementation for altobeam APOLLO mac80211 drivers.
 *
 * Copyright (c) 2016, altobeam
 * Author:
 *
 *  Based on 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/random.h>

#include "apollo.h"
#include "wsm.h"
#include "bh.h"
#include "debug.h"
#include "hwio.h"
#include "sbus.h"
#include "mac80211/ieee80211_i.h"
#include "mac80211/rc80211_minstrel.h"
#include "mac80211/rc80211_minstrel_ht.h"

#ifdef ATBM_SUPPORT_SMARTCONFIG
extern int smartconfig_start_rx(struct atbm_common *hw_priv,struct sk_buff *skb,int channel );
#endif

extern void etf_v2_scan_rx(struct atbm_common *hw_priv,struct sk_buff *skb,u8 rssi );
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
#include "sta.h"
#endif /*ROAM_OFFLOAD*/
#endif
//#define CONFIG_ATBM_APOLLO_WSM_DEBUG
#if defined(CONFIG_ATBM_APOLLO_WSM_DEBUG)
#define wsm_printk  printk
#else
#define wsm_printk(...)
#endif
extern int test_cnt_packet;


#define WSM_CMD_TIMEOUT		(60 * HZ) /* With respect to interrupt loss */
#define WSM_CMD_SCAN_TIMEOUT	(15 * HZ) /* With respect to interrupt loss */
#define WSM_CMD_JOIN_TIMEOUT	(18 * HZ) /* Join timeout is 5 sec. in FW   */
#define WSM_CMD_START_TIMEOUT	(17 * HZ)
#define WSM_CMD_RESET_TIMEOUT	(14 * HZ) /* 2 sec. timeout was observed.   */
#define WSM_CMD_DEFAULT_TIMEOUT	(40 * HZ)
#define WSM_CMD_DONOT_CONFIRM_TIMEOUT	(-1)
#define WSM_SKIP(buf, size)						\
	do {								\
		if (unlikely((buf)->data + size > (buf)->end))		\
			goto underflow;					\
		(buf)->data += size;					\
	} while (0)

#define WSM_GET(buf, ptr, size)						\
	do {								\
		if (unlikely((buf)->data + size > (buf)->end))		\
			goto underflow;					\
		memcpy(ptr, (buf)->data, size);				\
		(buf)->data += size;					\
	} while (0)

#define __WSM_GET(buf, type, cvt)					\
	({								\
		type val;						\
		if (unlikely((buf)->data + sizeof(type) > (buf)->end))	\
			goto underflow;					\
		val = cvt(*(type *)(buf)->data);			\
		(buf)->data += sizeof(type);				\
		val;							\
	})
#if 0
#define WSM_SKIP(buf,size)								\
	do {												\
		if(unlikely((buf)->data + size > (buf)->end))	\
			goto underflow;								\
		(buf)->data += size;							\
	}while(0)
#endif
#define WSM_GET8(buf)  __WSM_GET(buf, u8, (u8))
#define WSM_GET16(buf) __WSM_GET(buf, u16, __le16_to_cpu)
#define WSM_GET32(buf) __WSM_GET(buf, u32, __le32_to_cpu)

#define WSM_PUT(buf, ptr, size)						\
	do {								\
		if(buf == NULL)					\
			goto nomem;					\
		if (unlikely((buf)->data + size > (buf)->end))		\
			if (unlikely(wsm_buf_reserve((buf), size)))	\
				goto nomem;				\
		memcpy((buf)->data, ptr, size);				\
		(buf)->data += size;					\
	} while (0)

#define __WSM_PUT(buf, val, type, cvt)					\
	do {								\
		if(buf == NULL)					\
			goto nomem;					\
		if (unlikely((buf)->data + sizeof(type) > (buf)->end))	\
			if (unlikely(wsm_buf_reserve((buf), sizeof(type)))) \
				goto nomem;				\
		*(type *)(buf)->data = cvt(val);			\
		(buf)->data += sizeof(type);				\
	} while (0)
	
#define WSM_PUT8(buf, val)  __WSM_PUT(buf, val, u8, (u8))
#define WSM_PUT16(buf, val) __WSM_PUT(buf, val, u16, __cpu_to_le16)
#define WSM_PUT32(buf, val) __WSM_PUT(buf, val, u32, __cpu_to_le32)
struct wsm_shmem_arg_s {
	void *buf;
	size_t buf_size;
};
static void wsm_buf_reset(struct wsm_buf *buf);
static int wsm_buf_reserve(struct wsm_buf *buf, size_t extra_size);
static int get_interface_id_scanning(struct atbm_common *hw_priv);
int wsm_write_shmem_confirm(struct atbm_common *hw_priv,
				struct wsm_shmem_arg_s *arg,
				struct wsm_buf *buf);
int wsm_read_shmem_confirm(struct atbm_common *hw_priv,
				struct wsm_shmem_arg_s *arg,
				struct wsm_buf *buf);
int wsm_efuse_change_data_confirm(struct atbm_common *hw_priv, struct wsm_buf *buf);

static int wsm_cmd_send(struct atbm_common *hw_priv,
			struct wsm_buf *buf,
			void *arg, u16 cmd, long tmo, int if_id);

static struct atbm_vif
	*wsm_get_interface_for_tx(struct atbm_common *hw_priv);

/**********************/
//1: Exception  0: Normal
int wifi_run_sta = 0;
void atbm_wifi_run_status_set(int status)
{
	wifi_run_sta = status;
	return;
}
int atbm_wifi_run_status_get(void)
{
	return wifi_run_sta;
}
/***********************/
void wsm_alloc_tx_buffer(struct atbm_common *hw_priv)
{
	spin_lock_bh(&hw_priv->tx_com_lock);
	++hw_priv->hw_bufs_used;	
	spin_unlock_bh(&hw_priv->tx_com_lock);
}

int wsm_release_tx_buffer(struct atbm_common *hw_priv, int count)
{
	spin_lock_bh(&hw_priv->tx_com_lock);
	hw_priv->hw_bufs_used -= count;
	if (WARN_ON(hw_priv->hw_bufs_used < 0))
		hw_priv->hw_bufs_used=0;	
	spin_unlock_bh(&hw_priv->tx_com_lock);
	if (!(hw_priv->hw_bufs_used))
		wake_up(&hw_priv->bh_evt_wq);
	
	return 1;
}

void wsm_alloc_tx_and_vif_buffer(struct atbm_common *hw_priv,int if_id)
{
	spin_lock_bh(&hw_priv->tx_com_lock);
	++hw_priv->hw_bufs_used;
	hw_priv->hw_bufs_used_vif[if_id] ++;
	spin_unlock_bh(&hw_priv->tx_com_lock);
}
static void wsm_release_err_data(struct atbm_common	*hw_priv,struct wsm_tx *wsm)
{
	struct atbm_queue *queue;
	u8 queue_id;
	struct sk_buff *skb;
	const struct atbm_txpriv *txpriv;
	
	BUG_ON(wsm == NULL);
	queue_id = atbm_queue_get_queue_id(wsm->packetID);

	BUG_ON(queue_id >= 4);
	queue = &hw_priv->tx_queue[queue_id];
	BUG_ON(queue == NULL);
	
	if(!atbm_queue_get_skb(queue, wsm->packetID, &skb, &txpriv)) {

//		struct ieee80211_tx_info *tx = IEEE80211_SKB_CB(skb);
		//int tx_count = 0;
//		int i;
		wsm_release_vif_tx_buffer(hw_priv,txpriv->if_id,1);
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		atbm_queue_remove(hw_priv, queue, wsm->packetID);
#else
		atbm_queue_remove(queue, wsm->packetID);
#endif
	}else {
		atbm_printk_err("pkg has been free(%d)(%d)(%d)\n",hw_priv->hw_bufs_used,
			hw_priv->hw_bufs_used_vif[0],hw_priv->hw_bufs_used_vif[1]);
		wsm_release_vif_tx_buffer(hw_priv,atbm_queue_get_if_id(wsm->packetID),1);
	}
	wsm_release_tx_buffer(hw_priv, 1);
}

void wsm_force_free_tx(struct atbm_common	*hw_priv,struct wsm_tx *wsm)
{
	int wsm_id;

	if(wsm == NULL)
		return;
	
	wsm_id = __le16_to_cpu(wsm->hdr.id) & 0x3F;

	switch(wsm_id){
	case WSM_FIRMWARE_CHECK_ID:
		break;
	case WSM_TRANSMIT_REQ_MSG_ID:
		atbm_printk_err("release:WSM_TRANSMIT_REQ_MSG_ID\n");
		wsm_release_err_data(hw_priv,wsm);
		break;
	default:
		atbm_printk_err("release:cmd\n");
		wsm_release_tx_buffer(hw_priv, 1);
		spin_lock_bh(&hw_priv->wsm_cmd.lock);
		if(hw_priv->wsm_cmd.cmd != 0XFFFF){
			hw_priv->wsm_cmd.ret = -1;
			hw_priv->wsm_cmd.done = 1;
			hw_priv->wsm_cmd.cmd = 0xFFFF;
			hw_priv->wsm_cmd.ptr = NULL;
			hw_priv->wsm_cmd.arg = NULL;
			wake_up(&hw_priv->wsm_cmd_wq);		
		}	
		spin_unlock_bh(&hw_priv->wsm_cmd.lock);
		break;
	}
}

static inline void wsm_cmd_lock(struct atbm_common *hw_priv)
{
	mutex_lock(&hw_priv->wsm_cmd_mux);
}

static inline void wsm_cmd_unlock(struct atbm_common *hw_priv)
{
	mutex_unlock(&hw_priv->wsm_cmd_mux);
}
static int wsm_oper_lock_flag=0;
static inline void wsm_oper_lock(struct atbm_common *hw_priv)
{
	down(&hw_priv->wsm_oper_lock);
	wsm_oper_lock_flag=1; 
}

void wsm_oper_unlock(struct atbm_common *hw_priv)
{
	wsm_oper_lock_flag=0;
	up(&hw_priv->wsm_oper_lock);
}
struct wsm_arg {
	__le32 req_id;
	void *buf;
	size_t buf_size;
};
/*
*init check cmd,not need confirm
*/
int wsm_firmware_init_check_req(struct atbm_common *hw_priv)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);
	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_FIRMWARE_CHECK_ID, WSM_CMD_DONOT_CONFIRM_TIMEOUT,
			   0);
	wsm_cmd_unlock(hw_priv);
	return ret;
}
static int wsm_generic_req_confirm(struct atbm_common *hw_priv,
				struct wsm_arg *arg,
				struct wsm_buf *buf)
{
	u32 req_id = WSM_GET32(buf);
	u32 status = WSM_GET32(buf);
	
	if(req_id != arg->req_id){
		atbm_printk_err("%s:req_id[%d][%d] err\n",__func__,arg->req_id,req_id);
		return -EINVAL;
	}
	
	if(status != WSM_STATUS_SUCCESS){
		atbm_printk_err("%s:status err[%d]\n",__func__,req_id);
		return -EINVAL;
	}
	
	WSM_GET(buf, arg->buf, arg->buf_size);

	return 0;
underflow:
	WARN_ON(1);
	return -EINVAL;
}
int wsm_generic_req(struct atbm_common *hw_priv,const struct wsm_gen_req *req,void *_buf,size_t buf_size,int if_id)
{
	
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	struct wsm_arg arg;
	WARN_ON(req->req_len%4);
	wsm_cmd_lock(hw_priv);
	arg.req_id = req->req_id;
	arg.buf    = _buf;
	arg.buf_size = buf_size;
	WSM_PUT32(buf, req->req_id);
	WSM_PUT(buf, req->params, req->req_len);

	ret = wsm_cmd_send(hw_priv, buf, &arg, WSM_GENERIC_REQ_ID, WSM_CMD_TIMEOUT, if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;
	
	nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

/* ******************************************************************** */
/* WSM API implementation						*/

static int wsm_stop_scan_confirm(struct atbm_common *hw_priv,
			     void *arg,
			     struct wsm_buf *buf)
{
	u32 status = WSM_GET32(buf);
	
	atbm_printk_scan("wsm_stop_scan_confirm %x wait_complete %d\n",status,hw_priv->scan.wait_complete);
	if (status == WSM_STATUS_NOEFFECT){
		
		if(hw_priv->scan.wait_complete)
		{
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
			if(hw_priv->auto_scanning == 0)
				wsm_oper_unlock(hw_priv);
#else
			wsm_oper_unlock(hw_priv);
#endif /*ROAM_OFFLOAD*/
#endif
		}
		
	}
	else if (status != WSM_STATUS_SUCCESS){
		atbm_printk_scan("%s:status(%d)\n",__func__,status);
		return -EINVAL;
	}
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

/* ******************************************************************** */
/* WSM API implementation						*/

static int wsm_generic_confirm(struct atbm_common *hw_priv,
			     void *arg,
			     struct wsm_buf *buf)
{
	u32 status = WSM_GET32(buf);
	if (status != WSM_STATUS_SUCCESS){
		atbm_printk_err( "%s:status(%d)\n",__func__,status);
		return -EINVAL;
	}
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

int wsm_configuration(struct atbm_common *hw_priv,
		      struct wsm_configuration *arg,
		      int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);
	WSM_PUT32(buf, arg->dot11MaxTransmitMsduLifeTime);

	WSM_PUT32(buf, arg->dot11MaxReceiveLifeTime);
	WSM_PUT32(buf, arg->dot11RtsThreshold);


	/* DPD block. */
	WSM_PUT16(buf, arg->dpdData_size + 12);

	WSM_PUT16(buf, 1); /* DPD version */

	WSM_PUT(buf, arg->dot11StationId, ETH_ALEN);

	WSM_PUT16(buf, 5); /* DPD flags */

	WSM_PUT(buf, arg->dpdData, arg->dpdData_size);



	ret = wsm_cmd_send(hw_priv, buf, arg, WSM_CONFIGURATION_REQ_ID, WSM_CMD_TIMEOUT, if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

static int wsm_configuration_confirm(struct atbm_common *hw_priv,
				     struct wsm_configuration *arg,
				     struct wsm_buf *buf)
{
	int i;
	int status;

	status = WSM_GET32(buf);
	if (WARN_ON(status != WSM_STATUS_SUCCESS))
		return -EINVAL;

	WSM_GET(buf, arg->dot11StationId, ETH_ALEN);
	arg->dot11FrequencyBandsSupported = WSM_GET8(buf);
	WSM_SKIP(buf, 1);
	arg->supportedRateMask = WSM_GET32(buf);
	for (i = 0; i < 2; ++i) {
		arg->txPowerRange[i].min_power_level = WSM_GET32(buf);
		arg->txPowerRange[i].max_power_level = WSM_GET32(buf);
		arg->txPowerRange[i].stepping = WSM_GET32(buf);
	}
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

/* ******************************************************************** */

int wsm_reset(struct atbm_common *hw_priv, const struct wsm_reset *arg,
		int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	u16 cmd = 0x000A | WSM_TX_LINK_ID(arg->link_id);

	wsm_cmd_lock(hw_priv);

	WSM_PUT32(buf, arg->reset_statistics ? 0 : 1);
	ret = wsm_cmd_send(hw_priv, buf, NULL, cmd, WSM_CMD_RESET_TIMEOUT,
				if_id);
	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

/* ******************************************************************** */

struct wsm_mib {
	u16 mibId;
	void *buf;
	size_t buf_size;
};

int wsm_read_mib(struct atbm_common *hw_priv, u16 mibId, void *_buf,
			size_t buf_size,int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	struct wsm_mib mib_buf = {
		.mibId = mibId,
		.buf = _buf,
		.buf_size = buf_size,
	};
	wsm_cmd_lock(hw_priv);

	WSM_PUT16(buf, mibId);
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(hw_priv, buf, &mib_buf, WSM_READ_MIB_REQ_ID, WSM_CMD_TIMEOUT, if_id);
	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

static int wsm_read_mib_confirm(struct atbm_common *hw_priv,
				struct wsm_mib *arg,
				struct wsm_buf *buf)
{
	u16 size;
	if (WARN_ON(WSM_GET32(buf) != WSM_STATUS_SUCCESS))
		return -EINVAL;

	if (WARN_ON(WSM_GET16(buf) != arg->mibId))
		return -EINVAL;

	size = WSM_GET16(buf);
	if (size > arg->buf_size)
		size = arg->buf_size;

	WSM_GET(buf, arg->buf, size);
	arg->buf_size = size;
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

/* ******************************************************************** */

int wsm_write_mib(struct atbm_common *hw_priv, u16 mibId, void *_buf,
			size_t buf_size, int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	struct wsm_mib mib_buf = {
		.mibId = mibId,
		.buf = _buf,
		.buf_size = buf_size,
	};

	wsm_cmd_lock(hw_priv);

	WSM_PUT16(buf, mibId);
	WSM_PUT16(buf, buf_size);
	WSM_PUT(buf, _buf, buf_size);

	ret = wsm_cmd_send(hw_priv, buf, &mib_buf, WSM_WRITE_MIB_REQ_ID, WSM_CMD_TIMEOUT,
			if_id);	
	if(ret == -3){
		goto disconnect;
	}
	else if(ret){
		goto nomem;
	}
	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	atbm_printk_err("<WARNING> wsm_write_mib fail !!! mibId=%d\n",mibId);
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;

disconnect:
	atbm_printk_err("<WARNING> wsm_write_mib fail !!! mibId=%d, HIF disconnect\n",mibId);
	wsm_cmd_unlock(hw_priv);
	return 0;

}

static int wsm_write_mib_confirm(struct atbm_common *hw_priv,
				struct wsm_mib *arg,
				struct wsm_buf *buf,
				int interface_link_id)
{
	int ret;
	struct atbm_vif *priv;

		interface_link_id = 0;

	ret = wsm_generic_confirm(hw_priv, arg, buf);
	if (ret)
		return ret;

	if (arg->mibId == WSM_MIB_ID_OPERATIONAL_POWER_MODE) {
		const char *p = arg->buf;

		/* Power save is enabled before add_interface is called */
		if (!hw_priv->vif_list[interface_link_id])
			return 0;
		/* OperationalMode: update PM status. */
		priv = ABwifi_hwpriv_to_vifpriv(hw_priv,
					interface_link_id);
		if (!priv)
			return 0;
		atbm_enable_powersave(priv,
				(p[0] & 0x0F) ? true : false);
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
	}
	return 0;
}



/* ******************************************************************** */

extern struct sk_buff *ieee80211_probereq_get_etf(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const u8 *ssid, size_t ssid_len,
				       size_t total_len);

extern struct sk_buff *ieee80211_probereq_get_etf_v2(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const u8 *ssid, size_t ssid_len,
				       size_t total_len);
#ifdef CONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
extern struct sk_buff *ieee80211_probereq_get_etf_for_send_result(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const u8 *ssid, size_t ssid_len,
				       size_t total_len);
#endif
int wsm_start_tx_param_set(struct atbm_common *hw_priv, struct ieee80211_vif *vif,bool start)
{
	int ret = 0;
	u32 len = 0;
	struct wsm_set_chantype arg = {
	.band = 0,			//0:2.4G,1:5G
	.flag = start? BIT(WSM_SET_CHANTYPE_FLAGS__ETF_TEST_START):0,			//no use
	.channelNumber = hw_priv->etf_channel , // channel number
	.channelType =  hw_priv->etf_channel_type,	// channel type
	};
	
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PROBE_REQUEST,
	};
	len = hw_priv->etf_len;	

	if(hw_priv->etf_greedfiled == 1){
		arg.flag |= BIT(WSM_SET_CHANTYPE_FLAGS__ETF_GREEDFILED);
	}

	//printk("hw_priv->etf_greedfiled:%d\n", hw_priv->etf_greedfiled);
	
	atbm_printk_always("etf_channel = %d etf_channel_type %d\n", hw_priv->etf_channel,hw_priv->etf_channel_type);
	ret = wsm_set_chantype_func(hw_priv,&arg,0);

	if(start==0)
		return ret;

	if(len>1000)
		len = 1000;

	if(len<200)
		len = 200;

	frame.skb = ieee80211_probereq_get_etf(hw_priv->hw, vif, "tttttttt", 8,len);

	ret = wsm_set_template_frame(hw_priv, &frame, 0);
	if (frame.skb)
		atbm_dev_kfree_skb(frame.skb);
	
	
	return ret;
}


int wsm_start_tx_param_set_v2(struct atbm_common *hw_priv, struct ieee80211_vif *vif,bool start)
{
	int ret = 0;
	struct wsm_set_chantype arg = {
	.flag = start? BIT(WSM_SET_CHANTYPE_PRB_TPC):0,			//probreq use tpc
	};

	
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PROBE_REQUEST,
	};

	ret = wsm_set_chantype_func(hw_priv,&arg,0);
	//printk("wsm_start_tx_param_set_v2\n");
	frame.skb = ieee80211_probereq_get_etf_v2(hw_priv->hw, vif, "tttttttt", 0,1000);
	if(ret == 0)
		ret = wsm_set_template_frame(hw_priv, &frame, 0);
	if (frame.skb)
		atbm_dev_kfree_skb(frame.skb);
	
	
	return ret;


}
#ifdef CONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
int wsm_send_result_param_set(struct atbm_common *hw_priv, struct ieee80211_vif *vif,bool start)
{
	struct wsm_set_chantype arg = {
	.flag = start? BIT(WSM_SET_CHANTYPE_PRB_TPC):0,			//probreq use tpc
	};

	
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PROBE_REQUEST,
	};

	wsm_set_chantype_func(hw_priv,&arg,0);
	frame.skb = ieee80211_probereq_get_etf_for_send_result(hw_priv->hw, vif, "tttttttt", 0,1000);

	 wsm_set_template_frame(hw_priv, &frame, 0);
	if (frame.skb)
		atbm_dev_kfree_skb(frame.skb);
	
	
	return 1;


}
#endif
int wsm_start_scan_etf(struct atbm_common *hw_priv, struct ieee80211_vif *vif )
{
	
	struct wsm_scan scan;
	struct wsm_ssid  ssids; 
	struct wsm_scan_ch	ch[2];	
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);


	u32 channel = hw_priv->etf_channel;
	u32 rate = hw_priv->etf_rate;
	hw_priv->scan.if_id = priv->if_id;
	memset(&scan,0,sizeof(struct wsm_scan));
	

	
	scan.scanFlags = 0; /* bit 0 set => forced background scan */
	scan.maxTransmitRate = rate;
	scan.autoScanInterval = (0xba << 24)|(30 * 1024); /* 30 seconds, -70 rssi */
	scan.numOfProbeRequests = 0xff;
	scan.numOfChannels =2;
	scan.numOfSSIDs = 1;
	scan.probeDelay = 1;	
	scan.scanType =WSM_SCAN_TYPE_FOREGROUND;


	scan.ssids = &ssids;
	scan.ssids->length = 4;
	memcpy(ssids.ssid,"tttt",4);
	scan.ch = &ch[0];
	scan.ch[0].number = channel;
	scan.ch[0].minChannelTime= 10;
	scan.ch[0].maxChannelTime= 11;
	scan.ch[0].txPowerLevel= 3;
	scan.ch[1].number = channel;
	scan.ch[1].minChannelTime= 10;
	scan.ch[1].maxChannelTime= 11;
	scan.ch[1].txPowerLevel= 3;

	return wsm_scan(hw_priv,&scan,0);
}


int wsm_start_scan_etf_v2(struct atbm_common *hw_priv, struct ieee80211_vif *vif )
{
	
	struct wsm_scan scan;
	struct wsm_ssid  ssids; 
	struct wsm_scan_ch	ch[2];	
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);


	u32 channel = hw_priv->etf_channel;
	u32 rate = hw_priv->etf_rate;
	hw_priv->scan.if_id = priv->if_id;
	memset(&scan,0,sizeof(struct wsm_scan));
	

	
	scan.scanFlags = 0; /* bit 0 set => forced background scan */
	scan.maxTransmitRate = rate;
	scan.autoScanInterval = (0xba << 24)|(30 * 1024); /* 30 seconds, -70 rssi */
	scan.numOfProbeRequests = 200;
	scan.numOfChannels =1;
	scan.numOfSSIDs = 1;
	scan.probeDelay = 5;	
	scan.scanType =WSM_SCAN_TYPE_FOREGROUND;


	scan.ssids = &ssids;
	scan.ssids->length = 0;
	memcpy(ssids.ssid,"tttttttt",8);
	scan.ch = &ch[0];
	scan.ch[0].number = channel;
	scan.ch[0].minChannelTime= 10;
	scan.ch[0].maxChannelTime= 11;
	scan.ch[0].txPowerLevel= 3;

	return wsm_scan(hw_priv,&scan,0);
}
#ifdef CONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
int wsm_send_result_start_scan_etf(struct atbm_common *hw_priv, struct ieee80211_vif *vif )
{
	
	struct wsm_scan scan;
	struct wsm_ssid  ssids; 
	struct wsm_scan_ch	ch[2];	
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);


	u32 channel = hw_priv->etf_channel;
	u32 rate = hw_priv->etf_rate;
	hw_priv->scan.if_id = priv->if_id;
	memset(&scan,0,sizeof(struct wsm_scan));
	

	
	scan.scanFlags = 0; /* bit 0 set => forced background scan */
	scan.maxTransmitRate = rate;
	scan.autoScanInterval = (0xba << 24)|(30 * 1024); /* 30 seconds, -70 rssi */
	scan.numOfProbeRequests = 30;
	scan.numOfChannels =1;
	scan.numOfSSIDs = 1;
	scan.probeDelay = 5;	
	scan.scanType =WSM_SCAN_TYPE_FOREGROUND;


	scan.ssids = &ssids;
	scan.ssids->length = 0;
	memcpy(ssids.ssid,"tttttttt",8);
	scan.ch = &ch[0];
	scan.ch[0].number = channel;
	scan.ch[0].minChannelTime= 10;
	scan.ch[0].maxChannelTime= 11;
	scan.ch[0].txPowerLevel= 3;

	return wsm_scan(hw_priv,&scan,0);
}
#endif
//get efuse remain bits
int wsm_get_efuse_status(struct atbm_common *hw_priv, struct ieee80211_vif *vif )
{
	int remainBit = 0;
	//struct efuse_headr efuse_temp;
	//memset(&efuse_temp, 0, sizeof(struct efuse_headr));
	wsm_get_efuse_remain_bit(hw_priv, &remainBit, sizeof(int));
	//printk("remainBit:%d\n", remainBit);

	return remainBit;
}


int wsm_start_tx(struct atbm_common *hw_priv, struct ieee80211_vif *vif )
{
	int ret = 0;
	hw_priv->bStartTx = 1;
	hw_priv->bStartTxWantCancel = 0;
	hw_priv->etf_test_v2 =0;
	ret = wsm_start_tx_param_set(hw_priv,vif,1);
	if(ret == 0){
		ret = wsm_start_scan_etf(hw_priv,vif);	
		if(ret){
			atbm_printk_err("wsm_start_scan_etf error %d\n",ret);
		}	
	}
	return ret;
}


extern u32 chipversion;
extern struct efuse_headr efuse_data_etf;
int wsm_start_tx_v2(struct atbm_common *hw_priv, struct ieee80211_vif *vif )
{
//	u8 CodeValue;
	int ret = 0;
	int efuse_remainbit = 0;
	hw_priv->bStartTx = 1;
	hw_priv->bStartTxWantCancel = 1;
	hw_priv->etf_test_v2 =1;
	
	efuse_remainbit = wsm_get_efuse_status(hw_priv, vif);
	atbm_printk_debug("efuse remain bit:%d\n", efuse_remainbit);

	memset(&efuse_data_etf, 0, sizeof(struct efuse_headr));
	ret = wsm_get_efuse_data(hw_priv,(void *)&efuse_data_etf,sizeof(struct efuse_headr));
	if(chipversion == 0x24)
	{
		if(efuse_remainbit <= 12)
			atbm_printk_always("This board already tested and passed!\n");
	}
	else if(chipversion == 0x49)
	{
		if(efuse_remainbit <= 503)
			atbm_printk_always("This board already tested and passed!\n");
	}

	//init_timer(&hw_priv->etf_expire_timer);	
	//hw_priv->etf_expire_timer.expires = jiffies+10*100;
	//hw_priv->etf_expire_timer.data = (unsigned long)hw_priv;
	//hw_priv->etf_expire_timer.function = atbm_etf_test_expire_timer;
	//add_timer(&hw_priv->etf_expire_timer);
	if(ret == 0)
	{
		ret = wsm_start_tx_param_set_v2(hw_priv,vif,1);
		if(ret == 0)
			ret = wsm_start_scan_etf_v2(hw_priv,vif);
		else
			atbm_printk_err("%s:%d\n", __func__, __LINE__);
	}
	else
	{
		atbm_printk_err("%s:%d\n", __func__, __LINE__);
	}

	return ret;
}
#ifdef CONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
int wsm_send_result(struct atbm_common *hw_priv, struct ieee80211_vif *vif )
{
	//hw_priv->bStartTx = 1;
	/* sigmastar test:send package for control golden`s led, and golden do not send response,
		so bStartTxWantCancel must set 0
	*/
	//hw_priv->bStartTxWantCancel = 1;//bStartTxWantCancel=1:get rxrssi and rxevm from lmc
	//hw_priv->etf_test_v2 =1;

	
	wsm_send_result_param_set(hw_priv,vif,1);
	wsm_send_result_start_scan_etf(hw_priv,vif);

	return 0;
}
#endif
int wsm_stop_tx(struct atbm_common *hw_priv)
{
	int ret = 0;
	struct atbm_vif *priv = __ABwifi_hwpriv_to_vifpriv(hw_priv,
					hw_priv->scan.if_id);
	wsm_start_tx_param_set(hw_priv,priv->vif,0);
	//hw_priv->bStartTx = 0;
	hw_priv->bStartTxWantCancel = 1;
	return ret;
}


/* ******************************************************************** */

int wsm_scan(struct atbm_common *hw_priv, const struct wsm_scan *arg,
		int if_id)
{
	int i;
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	if (unlikely(arg->numOfChannels > 48))
		return -EINVAL;

	if (unlikely(arg->numOfSSIDs > WSM_SCAN_MAX_NUM_OF_SSIDS))
		return -EINVAL;

	if (unlikely(arg->band > 1))
		return -EINVAL;

	wsm_oper_lock(hw_priv);
	wsm_cmd_lock(hw_priv);
	WSM_PUT8(buf, arg->band);
	WSM_PUT8(buf, arg->scanType);
	WSM_PUT8(buf, arg->scanFlags);
	WSM_PUT8(buf, arg->maxTransmitRate);
	WSM_PUT32(buf, arg->autoScanInterval);
	WSM_PUT8(buf, arg->numOfProbeRequests);
	WSM_PUT8(buf, arg->numOfChannels);
	WSM_PUT8(buf, arg->numOfSSIDs);
	WSM_PUT8(buf, arg->probeDelay);

	for (i = 0; i < arg->numOfChannels; ++i) {
		WSM_PUT16(buf, arg->ch[i].number);
		WSM_PUT16(buf, 0);
		WSM_PUT32(buf, arg->ch[i].minChannelTime);
		WSM_PUT32(buf, arg->ch[i].maxChannelTime);
		WSM_PUT32(buf, 0);
	}

	for (i = 0; i < arg->numOfSSIDs; ++i) {
		WSM_PUT32(buf, arg->ssids[i].length);
		WSM_PUT(buf, &arg->ssids[i].ssid[0],
				sizeof(arg->ssids[i].ssid));
	}

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_START_SCAN_REQ_ID, WSM_CMD_SCAN_TIMEOUT,
			   if_id);
	wsm_cmd_unlock(hw_priv);
	if (ret){
		wsm_oper_unlock(hw_priv);
	}
//	printk("wsm_scan2--\n");
	return ret;

nomem:
//	printk("wsm_scan3--\n");
	wsm_cmd_unlock(hw_priv);
	wsm_oper_unlock(hw_priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_stop_scan(struct atbm_common *hw_priv, int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);
	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_STOP_SCAN_REQ_ID, WSM_CMD_TIMEOUT,
			   if_id);
	wsm_cmd_unlock(hw_priv);
	return ret;
}
static int wsm_tx_confirm(struct atbm_common *hw_priv,
			  struct wsm_buf *buf,
			  int interface_link_id)
{
	struct wsm_tx_confirm tx_confirm;
	tx_confirm.packetID = WSM_GET32(buf);
	tx_confirm.status = WSM_GET32(buf);
	tx_confirm.txedRate = WSM_GET8(buf);
	tx_confirm.ackFailures = WSM_GET8(buf);
	tx_confirm.flags = WSM_GET16(buf);
	tx_confirm.mediaDelay = WSM_GET32(buf);
	tx_confirm.txQueueDelay = WSM_GET32(buf);

	/* TODO:COMBO:linkID will be stored in packetID*/
	/* TODO:COMBO: Extract traffic resumption map */
	if(tx_confirm.flags & BIT(8)){
	   tx_confirm.NumOfTxRate = WSM_GET32(buf);
	   tx_confirm.RateTable   = buf->data;
	}
	tx_confirm.if_id = atbm_queue_get_if_id(tx_confirm.packetID);
	tx_confirm.link_id = atbm_queue_get_link_id(
			tx_confirm.packetID);
	spin_lock_bh(&hw_priv->tx_com_lock);

	hw_priv->wsm_txconfirm_num++;
	spin_unlock_bh(&hw_priv->tx_com_lock);

	wsm_release_vif_tx_buffer(hw_priv, tx_confirm.if_id, 1);

	if (hw_priv->wsm_cbc.tx_confirm)
		hw_priv->wsm_cbc.tx_confirm(hw_priv, &tx_confirm);
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}
static int wsm_multi_tx_confirm(struct atbm_common *hw_priv,
				struct wsm_buf *buf, int interface_link_id)
{
	struct atbm_vif *priv;
	int ret;
	int count;
	int i;
	count = WSM_GET32(buf);
	if (WARN_ON(count <= 0))
		return -EINVAL;
	else if (count > 1) {
		ret = wsm_release_tx_buffer(hw_priv,  count - 1);
		if (ret < 0)
			return ret;
		if (ret > 0)
			atbm_bh_wakeup(hw_priv);
	}
	priv = ABwifi_hwpriv_to_vifpriv(hw_priv, interface_link_id);
	if (priv) {
		atbm_debug_txed_multi(priv, count);
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
	}
	for (i = 0; i < count; ++i) {
		ret = wsm_tx_confirm(hw_priv, buf, interface_link_id);
		if (ret)
			return ret;
	}
	return ret;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

/* ******************************************************************** */
				
#ifdef CONFIG_RATE_TXPOWER
extern const s8 MCS_LUT_20M[];
extern const s8 MCS_LUT_40M[];
extern int rate_txpower_cfg[64];
extern int get_rate_delta_gain(int *dst);
extern void set_power_by_mode(int wifi_mode, int ofdm_mode, int bw, int rateIndex, int delfault_power, int power_delta, int powerTarFlag);

/*
	rate_txpower value = 10*dB
	[0,1]:dsss
	[2,10]:gn
	[11,22]:HE-SU MCS0-MCS11
	[23,31]:gn 40M
	[32,43]:HE-SU MCS0-MCS11 40M
*/
int wsm_set_rate_power(struct atbm_common *hw_priv)
{
#ifdef CONFIG_TXPOWER_DCXO_VALUE
	//txpower and dcxo config file
	char *strfilename = CONFIG_TXPOWER_DCXO_VALUE;
#else
	char *strfilename = "/tmp/atbm_txpwer_dcxo_cfg.txt";
#endif

	int rate_txpower[64] = {0};//validfalg,data
	int i;
	int power = 0;
	int powerTarFlag = 0;

 	if((get_rate_delta_gain(&rate_txpower[0]) !=  0)){
			atbm_printk_always("==read %s fail==\n",strfilename);
			return -1;
	}
	

	memcpy(rate_txpower_cfg, rate_txpower, sizeof(rate_txpower_cfg));

	atbm_printk_always("==read success==\n");
	powerTarFlag = rate_txpower[0];//1:absolute power;0:delta power;

	if(powerTarFlag)
		atbm_printk_always("set absolute power\n");
	else
		atbm_printk_always("set delta power\n");

	for(i=1;i<=44;i++)
	{
		
		power = (rate_txpower[i] * 4)/10;
		
		if(i<=2)
			set_power_by_mode(1, 0, 0, i, MCS_LUT_20M[i-1], power, powerTarFlag);//rateIndex 0,1,2,3 --> 1:(0,1);2:(2,3)
		else if(i<=11)
		{
			if(i<=10)
				set_power_by_mode(0, 0, 0, (i-3), MCS_LUT_20M[i-1], power, powerTarFlag);
			else
				set_power_by_mode(0, 1, 0, (i-4), MCS_LUT_20M[i-1], power, powerTarFlag);
		}
		else if(i<=23)
			set_power_by_mode(0, 2, 0, (i-12), MCS_LUT_20M[i-1], power, powerTarFlag);
		else if(i<=32)
		{
			if(i<=31)
				set_power_by_mode(0, 0, 1, (i-24), MCS_LUT_40M[i-22], power, powerTarFlag);
			else
				set_power_by_mode(0, 1, 1, (i-25), MCS_LUT_40M[i-22], power, powerTarFlag);
		}
		else if(i<=44)
			set_power_by_mode(0, 2, 1, (i-33), MCS_LUT_40M[i-22], power, powerTarFlag);
	}
	//atbm_printk_always("rate_txpower[%d]:%d\n",i, rate_txpower[i]);
	
	return 0;
}
#endif

static int wsm_join_confirm(struct atbm_common *hw_priv,
			    struct wsm_join *arg,
			    struct wsm_buf *buf)
{
	if (WSM_GET32(buf) != WSM_STATUS_SUCCESS){
		atbm_printk_err("<ATBM WIFI>: connect FAIL \n");
		return -EINVAL;
	}
	arg->minPowerLevel = WSM_GET32(buf);
	arg->maxPowerLevel = WSM_GET32(buf);

	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

int wsm_join(struct atbm_common *hw_priv, struct wsm_join *arg,
	     int if_id)
/*TODO: combo: make it work per vif.*/
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_oper_lock(hw_priv);
	wsm_cmd_lock(hw_priv);
	
#ifdef ATBM_USE_FASTLINK
		arg->flags |=  WSM_JOIN_FLAGS_FORCE;
		arg->probeForJoin = 0;
#endif

	WSM_PUT8(buf, arg->mode);
	WSM_PUT8(buf, arg->band);
	WSM_PUT16(buf, arg->channelNumber);
	WSM_PUT(buf, &arg->bssid[0], sizeof(arg->bssid));
	WSM_PUT16(buf, arg->atimWindow);
	WSM_PUT8(buf, arg->preambleType);
	WSM_PUT8(buf, arg->probeForJoin);
	WSM_PUT8(buf, arg->dtimPeriod);
	WSM_PUT8(buf, arg->flags);
	WSM_PUT32(buf, arg->ssidLength);
	WSM_PUT(buf, &arg->ssid[0], sizeof(arg->ssid));
	WSM_PUT32(buf, arg->beaconInterval);
	WSM_PUT32(buf, arg->basicRateSet);
	WSM_PUT32(buf, arg->channel_type);
	WSM_PUT(buf,arg->transbssid,sizeof(arg->transbssid));
	WSM_PUT16(buf,arg->bssid_index);
	WSM_PUT8(buf,arg->max_bssid_index);
	WSM_PUT8(buf,arg->wifi4_option);
	WSM_PUT8(buf,arg->not_support_multi_bssid);
	WSM_PUT8(buf,arg->is_trans_bssid);
	
	hw_priv->tx_burst_idx = -1;
	ret = wsm_cmd_send(hw_priv, buf, arg, WSM_JOIN_REQ_ID, WSM_CMD_JOIN_TIMEOUT,
			   if_id);
	wsm_cmd_unlock(hw_priv);
	wsm_oper_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	wsm_oper_unlock(hw_priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_set_bss_params(struct atbm_common *hw_priv,
			const struct wsm_set_bss_params *arg,
			int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);

	//WSM_PUT8(buf, 0);
	WSM_PUT(buf, arg, sizeof(struct wsm_set_bss_params));
	//WSM_PUT8(buf, arg->beaconLostCount);
	//WSM_PUT16(buf, arg->aid);
	//WSM_PUT32(buf, arg->operationalRateSet);
	/*
	*he
	*/
/*	WSM_PUT32(buf,arg->he_support_flag);
	WSM_PUT32(buf,arg->htc_flags);
	WSM_PUT16(buf,arg->frame_time_rts_th);
	WSM_PUT8(buf,arg->color);
	WSM_PUT8(buf,arg->uora_ocw_range);
	WSM_PUT(buf,arg->transmitter_bssid,6);
	WSM_PUT8(buf,arg->max_bssid_indicator);
	WSM_PUT8(buf,arg->bssid_index);
	WSM_PUT8(buf,arg->ema_ap);
	WSM_PUT8(buf,arg->profile_periodicity);
	WSM_PUT(buf,&arg->pkt_ext,1*4*2);*/

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_SET_BSS_PARAMS_REQ_ID, WSM_CMD_TIMEOUT,
			if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_add_key(struct atbm_common *hw_priv, const struct wsm_add_key *arg,
			int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);

	WSM_PUT(buf, arg, sizeof(*arg));

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_ADD_KEY_REQ_ID, WSM_CMD_TIMEOUT,
				if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

/* ******************************************************************** */
#if 0
int wsm_remove_key(struct atbm_common *hw_priv,
		   const struct wsm_remove_key *arg, int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);

	WSM_PUT8(buf, arg->entryIndex);
	WSM_PUT8(buf, 0);
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_REMOVE_KEY_REQ_ID, WSM_CMD_TIMEOUT,
			   if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}
#else
int wsm_remove_key(struct atbm_common *hw_priv, const struct wsm_add_key *arg,
			int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);

	WSM_PUT(buf, arg, sizeof(*arg));

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_REMOVE_KEY_REQ_ID, WSM_CMD_TIMEOUT,
				if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}
#endif
/* ******************************************************************** */

int wsm_set_tx_queue_params(struct atbm_common *hw_priv,
				const struct wsm_set_tx_queue_params *arg,
				u8 id, int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	u8 queue_id_to_wmm_aci[] = {3, 2, 0, 1};

	wsm_cmd_lock(hw_priv);

	WSM_PUT8(buf, queue_id_to_wmm_aci[id]);
	WSM_PUT8(buf, 0);
	WSM_PUT8(buf, arg->ackPolicy);
	WSM_PUT8(buf, 0);
	WSM_PUT32(buf, arg->maxTransmitLifetime);
	WSM_PUT16(buf, arg->allowedMediumTime);
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_QUEUE_PARAMS_REQ_ID, WSM_CMD_TIMEOUT, if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_set_edca_params(struct atbm_common *hw_priv,
				const struct wsm_edca_params *arg,
				int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);

	/* Implemented according to specification. */

	WSM_PUT16(buf, arg->params[3].cwMin);
	WSM_PUT16(buf, arg->params[2].cwMin);
	WSM_PUT16(buf, arg->params[1].cwMin);
	WSM_PUT16(buf, arg->params[0].cwMin);

	WSM_PUT16(buf, arg->params[3].cwMax);
	WSM_PUT16(buf, arg->params[2].cwMax);
	WSM_PUT16(buf, arg->params[1].cwMax);
	WSM_PUT16(buf, arg->params[0].cwMax);

	WSM_PUT8(buf, arg->params[3].aifns);
	WSM_PUT8(buf, arg->params[2].aifns);
	WSM_PUT8(buf, arg->params[1].aifns);
	WSM_PUT8(buf, arg->params[0].aifns);

	WSM_PUT16(buf, arg->params[3].txOpLimit);
	WSM_PUT16(buf, arg->params[2].txOpLimit);
	WSM_PUT16(buf, arg->params[1].txOpLimit);
	WSM_PUT16(buf, arg->params[0].txOpLimit);

	WSM_PUT32(buf, arg->params[3].maxReceiveLifetime);
	WSM_PUT32(buf, arg->params[2].maxReceiveLifetime);
	WSM_PUT32(buf, arg->params[1].maxReceiveLifetime);
	WSM_PUT32(buf, arg->params[0].maxReceiveLifetime);


	
	/*add MU edca*/
	WSM_PUT32(buf, arg->mu_edca);
    	if(arg->mu_edca){    
        	WSM_PUT8(buf, arg->mu_params[3].ecw_min_max);
        	WSM_PUT8(buf, arg->mu_params[2].ecw_min_max);
        	WSM_PUT8(buf, arg->mu_params[1].ecw_min_max);
        	WSM_PUT8(buf, arg->mu_params[0].ecw_min_max);

        	WSM_PUT8(buf, arg->mu_params[3].aifns);
        	WSM_PUT8(buf, arg->mu_params[2].aifns);
        	WSM_PUT8(buf, arg->mu_params[1].aifns);
        	WSM_PUT8(buf, arg->mu_params[0].aifns);
                
        	WSM_PUT8(buf, arg->mu_params[3].mu_edca_timer);
        	WSM_PUT8(buf, arg->mu_params[2].mu_edca_timer);
        	WSM_PUT8(buf, arg->mu_params[1].mu_edca_timer);
        	WSM_PUT8(buf, arg->mu_params[0].mu_edca_timer);
    	}
	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_EDCA_PARAMS_REQ_ID, WSM_CMD_TIMEOUT, if_id);
	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

/* ******************************************************************** */
void atbm_pm_timer(unsigned long arg)
{
	struct atbm_common *hw_priv = (struct atbm_common *)arg;
	atbm_printk_err( "%s\n",__func__);
	spin_lock_bh(&hw_priv->wsm_pm_spin_lock);
	if(atomic_read(&hw_priv->wsm_pm_running) == 1){
		atomic_set(&hw_priv->wsm_pm_running, 0);
		wsm_oper_unlock(hw_priv);
		atbm_release_suspend(hw_priv);
		atbm_printk_err("%s:pm timeout\n",__func__);
	}
	spin_unlock_bh(&hw_priv->wsm_pm_spin_lock);
}
static void atbm_pm_timer_setup(struct atbm_common *hw_priv)
{
	spin_lock_bh(&hw_priv->wsm_pm_spin_lock);
	atomic_set(&hw_priv->wsm_pm_running, 1);
	atbm_mod_timer(&hw_priv->wsm_pm_timer,
		jiffies + 2*HZ);
	atbm_hold_suspend(hw_priv);
	spin_unlock_bh(&hw_priv->wsm_pm_spin_lock);
}

static void atbm_pm_timer_cancle(struct atbm_common *hw_priv)
{
	spin_lock_bh(&hw_priv->wsm_pm_spin_lock);
	atomic_set(&hw_priv->wsm_pm_running, 0);	
	atbm_del_timer(&hw_priv->wsm_pm_timer);	
	atbm_release_suspend(hw_priv);
	spin_unlock_bh(&hw_priv->wsm_pm_spin_lock);
}
/* ******************************************************************** */
void wsm_wait_pm_sync(struct atbm_common *hw_priv)
{
	wsm_oper_lock(hw_priv);
	/*
	*wait pm indication
	*/
	wsm_oper_unlock(hw_priv);
	atbm_printk_err("%s:complete\n",__func__);
}
int wsm_set_pm(struct atbm_common *hw_priv, const struct wsm_set_pm *arg,
		int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_oper_lock(hw_priv);

	wsm_cmd_lock(hw_priv);

	WSM_PUT8(buf, arg->pmMode);
	WSM_PUT8(buf, arg->fastPsmIdlePeriod);
	WSM_PUT8(buf, arg->apPsmChangePeriod);
	WSM_PUT8(buf, arg->minAutoPsPollPeriod);
	atbm_printk_err("%s:pmMode:%d,fastPsmIdlePeriod:%d,apPsmChangePeriod:%d,minAutoPsPollPeriod:%d\n",
				__func__,arg->pmMode,arg->fastPsmIdlePeriod,arg->apPsmChangePeriod,arg->minAutoPsPollPeriod);
	
	atbm_pm_timer_setup(hw_priv);
	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_SET_PM_REQ_ID, WSM_CMD_TIMEOUT, if_id);

	wsm_cmd_unlock(hw_priv);
	if (ret){
		atbm_pm_timer_cancle(hw_priv);
        wsm_oper_unlock(hw_priv);
	}
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	wsm_oper_unlock(hw_priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_start(struct atbm_common *hw_priv, const struct wsm_start *arg,
		int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	
	wsm_oper_lock(hw_priv);
	wsm_cmd_lock(hw_priv);

	WSM_PUT8(buf, arg->mode);
	WSM_PUT8(buf, arg->band);
	WSM_PUT16(buf, arg->channelNumber);
	WSM_PUT32(buf, arg->CTWindow);
	WSM_PUT32(buf, arg->beaconInterval);
	WSM_PUT8(buf, arg->DTIMPeriod);
	WSM_PUT8(buf, arg->preambleType);
	WSM_PUT8(buf, arg->probeDelay);
	WSM_PUT8(buf, arg->ssidLength);
	WSM_PUT(buf, arg->ssid, sizeof(arg->ssid));
	WSM_PUT32(buf, arg->basicRateSet);
	WSM_PUT32(buf, arg->channel_type);
	hw_priv->tx_burst_idx = -1;
	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_START_REQ_ID, WSM_CMD_START_TIMEOUT,
			if_id);

	wsm_cmd_unlock(hw_priv);
	wsm_oper_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	wsm_oper_unlock(hw_priv);
	return -ENOMEM;
}


/* ******************************************************************** */
/* [ Notice ] this msgid used by efuse change data
*
int wsm_start_find(struct atbm_common *hw_priv, int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);
	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_START_FIND_ID, WSM_CMD_TIMEOUT, if_id);
	wsm_cmd_unlock(hw_priv);
	return ret;
}
*/
/* ******************************************************************** */

int wsm_stop_find(struct atbm_common *hw_priv, int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);
	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_STOP_FIND_ID, WSM_CMD_TIMEOUT, if_id);
	wsm_cmd_unlock(hw_priv);
	return ret;
}

/* ******************************************************************** */

int wsm_map_link(struct atbm_common *hw_priv, const struct wsm_map_link *arg,
		int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	u16 cmd = WSM_MAP_LINK_REQ_ID;

	wsm_cmd_lock(hw_priv);

	WSM_PUT(buf, &arg->mac_addr[0], sizeof(arg->mac_addr));

	WSM_PUT8(buf, arg->unmap);
	WSM_PUT8(buf, arg->link_id);
	WSM_PUT8(buf, arg->mfp);


	ret = wsm_cmd_send(hw_priv, buf, NULL, cmd, WSM_CMD_TIMEOUT, if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

/* ******************************************************************** */

int wsm_update_ie(struct atbm_common *hw_priv,
		  const struct wsm_update_ie *arg, int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);

	WSM_PUT16(buf, arg->what);
	WSM_PUT16(buf, arg->count);
	WSM_PUT(buf, arg->ies, arg->length);

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_UPDATE_IE_REQ_ID, WSM_CMD_TIMEOUT, if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;

}
/* ******************************************************************** */
#ifdef MCAST_FWDING
/* 3.66 */
static int wsm_give_buffer_confirm(struct atbm_common *hw_priv,
                            struct wsm_buf *buf)
{
	wsm_printk( "[WSM] HW Buf count %d\n", hw_priv->hw_bufs_used);
	wsm_printk( "[WSM] HW Buf short count %d\n", hw_priv->hw_bufs_used_short);
	if (!(hw_priv->hw_bufs_used))
		wake_up(&hw_priv->bh_evt_wq);
	return 0;
}

/* 3.65 */
int wsm_init_release_buffer_request(struct atbm_common *hw_priv, u8 index)
{
	struct wsm_buf *buf = &hw_priv->wsm_release_buf[index];
	u16 cmd = 0x0022; /* Buffer Request */
	u8 flags;
	size_t buf_len;

	wsm_buf_init(buf);

	flags = index ? 0: 0x1;

	WSM_PUT8(buf, flags);
	WSM_PUT8(buf, 0);
	WSM_PUT16(buf, 0);

	buf_len = buf->data - buf->begin;

	/* Fill HI message header */
	((__le16 *)buf->begin)[0] = __cpu_to_le16(buf_len);
	((__le16 *)buf->begin)[1] = __cpu_to_le16(cmd);

	return 0;
nomem:
	return -ENOMEM;
}

/* 3.68 */
static int wsm_request_buffer_confirm(struct atbm_vif *priv,
                            u8 *arg,
                            struct wsm_buf *buf)
{
	u8 count;
	u32 sta_asleep_mask = 0;
	int i;
	u32 mask = 0;
	u32 change_mask = 0;
	struct atbm_common *hw_priv = priv->hw_priv;

	/* There is no status field in this message */
	sta_asleep_mask = WSM_GET32(buf);
	count = WSM_GET8(buf);
	count -= 1; /* Current workaround for FW issue */

	spin_lock_bh(&priv->ps_state_lock);
	change_mask = (priv->sta_asleep_mask ^ sta_asleep_mask);
	wsm_printk( "CM %x, HM %x, FWM %x\n", change_mask,priv->sta_asleep_mask, sta_asleep_mask);
	spin_unlock_bh(&priv->ps_state_lock);

	if (change_mask) {
		struct ieee80211_sta *sta;
		int ret = 0;


		for (i = 0; i < ATBMWIFI_MAX_STA_IN_AP_MODE ; ++i) {

			if(ATBM_APOLLO_LINK_HARD != priv->link_id_db[i].status)
				continue;

			mask = BIT(i + 1);

			/* If FW state and host state for this link are different then notify OMAC */
			if(change_mask & mask) {

				wsm_printk( "PS State Changed %d for sta %pM\n", (sta_asleep_mask & mask) ? 1:0, priv->link_id_db[i].mac);


				rcu_read_lock();
				sta = ieee80211_find_sta(priv->vif, priv->link_id_db[i].mac);
				if (!sta) {
					wsm_printk( "[WSM] WRBC - could not find sta %pM\n",
							priv->link_id_db[i].mac);
				} else {
					ret = ieee80211_sta_ps_transition_ni(sta, (sta_asleep_mask & mask) ? true: false);
					wsm_printk( "PS State NOTIFIED %d\n", ret);
					WARN_ON(ret);
				}
				rcu_read_unlock();
			}
		}
		/* Replace STA mask with one reported by FW */
		spin_lock_bh(&priv->ps_state_lock);
		priv->sta_asleep_mask = sta_asleep_mask;
		spin_unlock_bh(&priv->ps_state_lock);
	}

	wsm_printk( "[WSM] WRBC - HW Buf count %d SleepMask %d\n",
					hw_priv->hw_bufs_used, sta_asleep_mask);
	hw_priv->buf_released = 0;
	WARN_ON(count != (hw_priv->wsm_caps.numInpChBufs - 1));

    return 0;

underflow:
    WARN_ON(1);
    return -EINVAL;
}

/* 3.67 */
int wsm_request_buffer_request(struct atbm_vif *priv,
				u8 *arg)
{
	int ret;
	struct wsm_buf *buf = &priv->hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(priv->hw_priv);

	WSM_PUT8(buf, (*arg));
	WSM_PUT8(buf, 0);
	WSM_PUT16(buf, 0);

	ret = wsm_cmd_send(priv->hw_priv, buf, arg, WSM_REQUEST_BUFFER_REQ_ID, WSM_CMD_JOIN_TIMEOUT,priv->if_id);

	wsm_cmd_unlock(priv->hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(priv->hw_priv);
	return -ENOMEM;
}

#endif
#ifdef ATBM_SUPPORT_WOW
int wsm_set_keepalive_filter(struct atbm_vif *priv, bool enable)
{
        struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);

        priv->rx_filter.keepalive = enable;
        return wsm_set_rx_filter(hw_priv, &priv->rx_filter, priv->if_id);
}
#endif
int wsm_set_probe_responder(struct atbm_vif *priv, bool enable)
{
        struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);

        priv->rx_filter.probeResponder = enable;
        return wsm_set_rx_filter(hw_priv, &priv->rx_filter, priv->if_id);
}
/* ******************************************************************** */
/* WSM indication events implementation					*/

static int wsm_startup_indication(struct atbm_common *hw_priv,
					struct wsm_buf *buf)
{
	u16 status;
	u16 Resv;
	char fw_label[129];
	static const char * const fw_types[] = {
		"ETF",
		"WFM",
		"WSM",
		"HI test",
		"Platform test"
	};
	u32 Config[4];
	u16 firmwareCap2;
	
	hw_priv->wsm_caps.NumOfHwAggrSupports = 16;
	hw_priv->hw->max_hw_support_rx_aggs = hw_priv->wsm_caps.NumOfHwAggrSupports;
	hw_priv->wsm_caps.numInpChBufs	= WSM_GET16(buf);
	hw_priv->wsm_caps.sizeInpChBuf	= WSM_GET16(buf);
	hw_priv->wsm_caps.hardwareId	= WSM_GET16(buf);
	hw_priv->wsm_caps.hardwareSubId	= WSM_GET16(buf);
	status				= WSM_GET16(buf);
	hw_priv->wsm_caps.firmwareCap	= WSM_GET16(buf);
	atbm_printk_init("firmwareCap %x\n",hw_priv->wsm_caps.firmwareCap);
	hw_priv->wsm_caps.firmwareType	= WSM_GET16(buf);
	hw_priv->wsm_caps.firmwareApiVer	= WSM_GET16(buf);
	hw_priv->wsm_caps.firmwareBuildNumber = WSM_GET16(buf);
	hw_priv->wsm_caps.firmwareVersion	= WSM_GET16(buf);
	WSM_GET(buf, &fw_label[0], sizeof(fw_label) - 1);
	fw_label[sizeof(fw_label) - 1] = 0; /* Do not trust FW too much. */
	Config[0]	= WSM_GET32(buf);
	Config[1]	= WSM_GET32(buf);
	Config[2]	= WSM_GET32(buf);
	Config[3]	= WSM_GET32(buf);
	firmwareCap2 =WSM_GET16(buf);
	hw_priv->wsm_caps.firmeareExCap = 0;
	if( buf->data + 2 <= buf->end ){
		Resv = WSM_GET16(buf);
		atbm_printk_debug("wsm_startup_indication : resv[%d] \n",Resv);
	}
	if( buf->data + 4<= buf->end )
		hw_priv->wsm_caps.firmeareExCap = WSM_GET32(buf);
	
	hw_priv->wsm_caps.NumOfStations = Config[0] & 0x000000FF;
	hw_priv->wsm_caps.NumOfInterfaces = (Config[0] & 0xFFFF0000) >> 16;
	hw_priv->hw->max_hw_support_rx_aggs = (Config[0] & 0x0000FF00) >> 8;
	hw_priv->wsm_caps.NumOfHwAggrSupports = (Config[0] & 0x0000FF00) >> 8;
	atbm_printk_init("firmwareCap2 %x\n",firmwareCap2);
	hw_priv->wsm_caps.firmwareCap	|= (firmwareCap2<<16);
	hw_priv->wsm_caps.NumOfHwXmitedAddr = Config[3];
	hw_priv->hw_bufs_free = hw_priv->wsm_caps.numInpChBufs;
	hw_priv->hw_bufs_free_init = hw_priv->hw_bufs_free;
	if(hw_priv->hw->max_hw_support_rx_aggs  ==0)
		hw_priv->hw->max_hw_support_rx_aggs  = 16;
//	BUG_ON(hw_priv->wsm_caps.NumOfHwXmitedAddr == 0);
//	printk("wsm_caps.firmwareCap %x firmware used %s-rate policy\n",hw_priv->wsm_caps.firmwareCap,hw_priv->wsm_caps.firmwareCap&CAPABILITIES_NEW_RATE_POLICY?"new":"old");
	atbm_printk_init("wsm_caps.firmwareCap %x %d",hw_priv->wsm_caps.firmwareCap,hw_priv->hw->max_hw_support_rx_aggs);
/*
	#if (OLD_RATE_POLICY==0)
	//CAPABILITIES_NEW_RATE_POLICY
	if((hw_priv->wsm_caps.firmwareCap & CAPABILITIES_NEW_RATE_POLICY)==0){
		printk(KERN_ERR "\n\n\n******************************************************\n");
		printk(KERN_ERR "\n !!!!!!! lmac version error,please check!!\n");
		printk(KERN_ERR "\n need used new ratecontrol policy,please check!!\n");
		printk(KERN_ERR "\n******************************************************\n\n\n");
		BUG_ON(1);
	}
#else
	if(!!(hw_priv->wsm_caps.firmwareCap & CAPABILITIES_NEW_RATE_POLICY)){
		printk(KERN_ERR "\n\n\n******************************************************\n");
		printk(KERN_ERR "\n ERROR!!!!!!! lmac version error,please check!!\n");
		printk(KERN_ERR "\n ERROR!!!!!!!need used old ratecontrol policy,please check!!\n");
		printk(KERN_ERR "\n******************************************************\n\n\n");
		BUG_ON(1);
	}
#endif //(OLD_RATE_POLICY==0)
*/
	if (WARN_ON(status))
		return -EINVAL;

	if (WARN_ON(hw_priv->wsm_caps.firmwareType > 4))
		return -EINVAL;

	atbm_printk_init("apollo wifi WSM init done.\n"
		"   Input buffers: %d x %d bytes\n"
		"   Hardware: %d.%d\n"
		"   %s firmware [%s], ver: %d, build: %d,"
		    " api: %d, cap: 0x%.4X Config[%x]  expection %x, ep0 cmd addr %x NumOfStations[0x%x] NumOfInterfaces[%x]\n",
		hw_priv->wsm_caps.numInpChBufs,
		hw_priv->wsm_caps.sizeInpChBuf,
		hw_priv->wsm_caps.hardwareId,
		hw_priv->wsm_caps.hardwareSubId,
		fw_types[hw_priv->wsm_caps.firmwareType],
		&fw_label[0],
		hw_priv->wsm_caps.firmwareVersion,
		hw_priv->wsm_caps.firmwareBuildNumber,
		hw_priv->wsm_caps.firmwareApiVer,
		hw_priv->wsm_caps.firmwareCap,Config[0],Config[1],Config[2],hw_priv->wsm_caps.NumOfStations,hw_priv->wsm_caps.NumOfInterfaces);
	BUG_ON(hw_priv->wsm_caps.NumOfStations == 0);
	BUG_ON(hw_priv->wsm_caps.NumOfStations > ATBMWIFI_MAX_STA_IN_AP_MODE);
	hw_priv->wsm_caps.firmwareReady = 1;
	hw_priv->wsm_caps.exceptionaddr =Config[1];
	hw_priv->wsm_caps.HiHwCnfBufaddr = Config[2];//ep0 addr
#if (PROJ_TYPE>=ARES_B)
	atbm_printk_init("EFUSE(8)				[%d]\n",!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_EFUSE8));
	atbm_printk_init("EFUSE(I)					[%d]\n",!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_EFUSEI));
	atbm_printk_init("EFUSE(B)			[%d]\n",!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_EFUSEB));
#endif
	atbm_printk_init("CAPABILITIES_ATBM_PRIVATE_IE      [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_ATBM_PRIVATE_IE)		); 
	atbm_printk_init("CAPABILITIES_NVR_IPC              [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_NVR_IPC)  );  
	atbm_printk_init("CAPABILITIES_NO_CONFIRM           [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_NO_CONFIRM 		)  );
	atbm_printk_init("CAPABILITIES_WOW                  [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_WOW 		)  );
	atbm_printk_init("CAPABILITIES_NO_BACKOFF           [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_NO_BACKOFF 		)  );
	atbm_printk_init("CAPABILITIES_CFO                  [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_CFO 		)  );  
	atbm_printk_init("CAPABILITIES_CHIP_FLAG            [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_CHIP_FLAG 		)  );  
	atbm_printk_init("CAPABILITIES_TXCAL                [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_TXCAL 		)  );  
	atbm_printk_init("CAPABILITIES_HIF_TRACE       		[%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_HIF_TRACE 		)  );  
	atbm_printk_init("CAPABILITIES_FORCE_PM_ACTIVE      [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_FORCE_PM_ACTIVE 		)  );
	atbm_printk_init("CAPABILITIES_SMARTCONFIG          [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_SMARTCONFIG		)  );
	atbm_printk_init("CAPABILITIES_ETF                  [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_ETF		)  );
	//atbm_printk_init("CAPABILITIES_LMAC_RATECTL         [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_LMAC_RATECTL		)  );  
	//atbm_printk_init("CAPABILITIES_LMAC_TPC             [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_LMAC_TPC		)  );  
	//atbm_printk_init("CAPABILITIES_LMAC_TEMPC           [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_LMAC_TEMPC		)  );  
	atbm_printk_init("CAPABILITIES_BLE		  			[%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_BLE		)  );  
	atbm_printk_init("CAPABILITIES_40M			  		[%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_40M 	)  );  
	atbm_printk_init("CAPABILITIES_LDPC 		  		[%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_LDPC		)  );  
	
	atbm_printk_init("CAPABILITIES_NAV_ENABLE              [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_NAV_ENABLE		)  );
	atbm_printk_init("CAPABILITIES_USB_RECOVERY_BUG     [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_USB_RECOVERY_BUG)	); 
	atbm_printk_init("CAPABILITIES_USE_IPC              [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_USE_IPC)      );
	atbm_printk_init("CAPABILITIES_OUTER_PA             [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_OUTER_PA)      );
	atbm_printk_init("CAPABILITIES_WFA    [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_WFA)      );
	atbm_printk_init("CAPABILITIES_EAP_GROUP_REKEY  [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_EAP_GROUP_REKEY)      );
	atbm_printk_init("CAPABILITIES_RTS_LONG_DURATION    [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_RTS_LONG_DURATION)      );
	atbm_printk_init("CAPABILITIES_TX_CFO_PPM_CORRECTION[%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_TX_CFO_PPM_CORRECTION)      );
	atbm_printk_init("CAPABILITIES_RXCAL       [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_RXCAL)      );
	atbm_printk_init("CAPABILITIES_HW_CHECKSUM          [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_HW_CHECKSUM)      );
	atbm_printk_init("CAPABILITIES_80211_TO_8023 [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_80211_TO_8023)      );
	atbm_printk_init("CAPABILITIES_NEW_CHIPER  [%d]\n" ,!!(hw_priv->wsm_caps.firmwareCap &CAPABILITIES_NEW_CHIPER)		);

/*	atbm_printk_init("EX_CAPABILITIES_TWO_CHIP_ONE_SOC	[%d]",!!(hw_priv->wsm_caps.firmeareExCap & EX_CAPABILITIES_TWO_CHIP_ONE_SOC));
	atbm_printk_init("EX_CAPABILITIES_MANUAL_SET_AC 	[%d]",!!(hw_priv->wsm_caps.firmeareExCap & EX_CAPABILITIES_MANUAL_SET_AC));
	atbm_printk_init("EX_CAPABILITIES_LMAC_BW_CONTROL	[%d]",!!(hw_priv->wsm_caps.firmeareExCap & EX_CAPABILITIES_LMAC_BW_CONTROL));
	atbm_printk_init("EX_CAPABILITIES_SUPPORT_TWO_ANTENNA	[%d]",!!(hw_priv->wsm_caps.firmeareExCap & EX_CAPABILITIES_SUPPORT_TWO_ANTENNA));
	atbm_printk_init("EX_CAPABILITIES_ENABLE_STA_REMAIN_ON_CHANNEL	[%d]",!!(hw_priv->wsm_caps.firmeareExCap & EX_CAPABILITIES_ENABLE_STA_REMAIN_ON_CHANNEL));
	atbm_printk_init("EX_CAPABILITIES_ENABLE_PS 	   [%d]",!!(hw_priv->wsm_caps.firmeareExCap & EX_CAPABILITIES_ENABLE_PS));
	atbm_printk_init("EX_CAPABILITIES_TX_REQUEST_FIFO_LINK 	   [%d]",!!(hw_priv->wsm_caps.firmeareExCap & EX_CAPABILITIES_TX_REQUEST_FIFO_LINK));

	if(1 == ((hw_priv->wsm_caps.firmeareExCap & EX_CAPABILITIES_CHIP_TYPE)>>7))
		atbm_printk_init("EX_CAPABILITIES_CHIP_TYPE		6032IS\n");
	else if(2 == ((hw_priv->wsm_caps.firmeareExCap & EX_CAPABILITIES_CHIP_TYPE)>>7))
		atbm_printk_init("EX_CAPABILITIES_CHIP_TYPE		6032It\n");
	else if(3 == ((hw_priv->wsm_caps.firmeareExCap & EX_CAPABILITIES_CHIP_TYPE)>>7))
		atbm_printk_init("EX_CAPABILITIES_CHIP_TYPE		6012B\n");
*/
#ifdef ATBM_P2P_ADDR_USE_LOCAL_BIT
	if((hw_priv->wsm_caps.firmwareCap &CAPABILITIES_VIFADDR_LOCAL_BIT)==0){
		
		atbm_printk_init("LMAC NOT CAPABILITIES_VIFADDR_LOCAL_BIT <ERROR>\n");
//		BUG_ON(1);
	}
#else
	if((hw_priv->wsm_caps.firmwareCap &CAPABILITIES_VIFADDR_LOCAL_BIT)){
		
		atbm_printk_init("LMAC SET CAPABILITIES_VIFADDR_LOCAL_BIT <ERROR>\n");
//		BUG_ON(1);
	}
#endif

#ifdef ATBM_PRODUCT_TEST_USE_FEATURE_ID
	atbm_printk_init("CONFIG_PRODUCT_TEST_USE_FEATURE_ID [1]\n");
#else
	atbm_printk_init("CONFIG_PRODUCT_TEST_USE_FEATURE_ID [0]\n");
#endif
#ifdef CONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
	atbm_printk_init("CONFIG_PRODUCT_TEST_USE_GOLDEN_LED [1]\n");
#else
	atbm_printk_init("CONFIG_PRODUCT_TEST_USE_GOLDEN_LED [0]\n");
#endif

	BUG_ON(atbm_hif_trace_cap_valid(hw_priv));
	wake_up(&hw_priv->wsm_startup_done);
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}
#define DBG_PRINT_BUF_SIZE_MAX 390
static int wsm_debug_print_indication(struct atbm_common *hw_priv,
					struct wsm_buf *buf)
{
	char fw_debug_print[DBG_PRINT_BUF_SIZE_MAX + 1];
	u16 length;

	length = WSM_GET16(buf);
	if (length > DBG_PRINT_BUF_SIZE_MAX)
		length = DBG_PRINT_BUF_SIZE_MAX;
	WSM_GET(buf, &fw_debug_print[0], length);
	fw_debug_print[length] = '\0';

	atbm_printk_lmac("[lmac]:%s", fw_debug_print);
	return 0;
underflow:
	atbm_printk_err("wsm_debug_print_indication:EINVAL\n");
	return -EINVAL;
}

static int wsm_smartconfig_indication(struct atbm_common *hw_priv,
					int interface_link_id,
					struct wsm_buf *buf,
					struct sk_buff **skb_p)
{
#if (PROJ_TYPE>=ARES_A)
#ifdef ATBM_SUPPORT_SMARTCONFIG

	//struct atbm_vif *priv;
	int channelNum;
	int channelType;
	int packNum;
	int rate;
	int length;
	u32	rx_status0_start;
	size_t hdr_len;
	int length_bak;
	char * data;

	channelType 		= WSM_GET16(buf);
	channelNum  		= WSM_GET16(buf);
	packNum 			= WSM_GET8(buf);
	rate				= WSM_GET8(buf);
	length				= WSM_GET16(buf);
	rx_status0_start	= WSM_GET32(buf);

	hdr_len = buf->data - buf->begin;
	atbm_skb_pull(*skb_p, hdr_len);
	length_bak = (*skb_p)->len;
	(*skb_p)->len = length;
	data  = (*skb_p)->data;
	atbm_printk_smt("chann=%d,channelType=%d,packNum=%d,rate=%d,len=%d,rx_status0_start=%d,mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
		channelNum,channelType,packNum,rate,length,rx_status0_start,
		data[4],data[5],data[6],data[7],data[8],data[9]);
	smartconfig_start_rx(hw_priv,*skb_p,channelNum);
	if (*skb_p)
	{	
		(*skb_p)->len = length_bak;
		atbm_skb_push(*skb_p, hdr_len);
	}
underflow:
#endif 	//#ifdef ATBM_SUPPORT_SMARTCONFIG
#endif //#if (PROJ_TYPE==ARES_A)
	return 0;
}
int wsm_8023_format(struct wsm_hdr *wsm)
{
	struct wsm_buf wsmbuf;
	u32 flags;
	
	wsmbuf.begin = (u8 *)&wsm[0];
	wsmbuf.data = (u8 *)&wsm[1];
	wsmbuf.end = &wsmbuf.begin[__le32_to_cpu(wsm->len)];
	WSM_SKIP(&wsmbuf,8);
	flags = WSM_GET32(&wsmbuf);

	return  !!(WSM_RX_STATUS_8023_FRAME & flags);
underflow:
	return 0;
}
static int wsm_fill_wsm_rx_info(struct wsm_buf *wsmbuf,struct wsm_rx *rx)
{
	rx->status = WSM_GET32(wsmbuf);
	rx->channelNumber = WSM_GET16(wsmbuf);
	rx->rxedRate = WSM_GET8(wsmbuf);
	rx->rcpiRssi = WSM_GET8(wsmbuf);
	rx->flags = WSM_GET32(wsmbuf);
	rx->flags2 = WSM_GET32(wsmbuf);
	/*8023 Frame*/
	if(rx->flags & WSM_RX_STATUS_8023_FRAME){
		rx->frame_ctrl = WSM_GET16(wsmbuf);
		rx->seq_ctrl   = WSM_GET16(wsmbuf);
		rx->qos_ctrl   = WSM_GET16(wsmbuf);
		rx->resv0      = WSM_GET16(wsmbuf);
		rx->iv_len	   = WSM_GET8(wsmbuf);
		rx->icv_len    = WSM_GET8(wsmbuf);
		rx->resv1      = WSM_GET8(wsmbuf);
		rx->offset     = WSM_GET8(wsmbuf);
		BUG_ON(rx->offset == 0 ||rx->offset > (24 + 2 + 4 + 6)  || (rx->offset < 8));
		WSM_GET(wsmbuf,rx->iv,8);
		WSM_SKIP(wsmbuf,rx->offset-8);
	}
	
	rx->link_id = ((rx->flags & (0xf << 25)) >> 25);
	rx->channel_type = rx->flags2 & 0x07;
	
	if(rx->flags2 & ((0x1F) << 3)){
		rx->link_id = (rx->flags2 & ((0x1F) << 3))>>3;
	}	

	if(rx->rcpiRssi > 128)
		rx->rcpiRssi = rx->rcpiRssi -256;
	else
		rx->rcpiRssi = rx->rcpiRssi;
	return  0;
underflow:
	return -1;

}
static void wsm_fill_rx_cb_info(struct ieee80211_rx_status *hdr,struct wsm_rx *rx)
{
	hdr->mactime = jiffies;
	hdr->flag  = 0;
	/*in mac80211,if_id and aid not used*/
	hdr->if_id = rx->if_id;
	hdr->aid   =  rx->link_id;
	/*used by 8023*/
	hdr->frame_ctrl =  rx->frame_ctrl;
	hdr->qos_ctrl   =  rx->qos_ctrl;
	hdr->seq_ctrl	=  rx->seq_ctrl;
	memcpy(hdr->iv,rx->iv,8);
	hdr->band = (rx->channelNumber > 14) ? IEEE80211_BAND_5GHZ : IEEE80211_BAND_2GHZ;
	hdr->freq = ieee80211_channel_to_frequency( rx->channelNumber,hdr->band);

	if(rx->rxedRate >= 26){
		hdr->flag |= RX_FLAG_HE;
		hdr->rate_idx =  rx->rxedRate - 26;
	}else if ( rx->rxedRate >= 23) {
		hdr->flag |= RX_FLAG_HE_ER;
		hdr->rate_idx =  rx->rxedRate - 23;
	}
	else if ( rx->rxedRate >= 14) {
		hdr->flag |= RX_FLAG_HT;
		hdr->rate_idx =  rx->rxedRate - 14;
	} else if (rx->rxedRate >= 4) {
		hdr->flag |= RX_FLAG_11G;
		if (hdr->band == IEEE80211_BAND_5GHZ)
			hdr->rate_idx =  rx->rxedRate - 6;
		else
			hdr->rate_idx =  rx->rxedRate - 2;
	} else {
		hdr->rate_idx =  rx->rxedRate;
	}

	hdr->signal = (s8) rx->rcpiRssi;
	hdr->antenna = 0;
	
	if ( rx->flags & WSM_RX_STATUS_AGGREGATE){
		atbm_printk_once("<WIFI> rx ampdu ++ \n");
		hdr->flag |= RX_FLAG_AMPDU;
	}
	
#ifdef CHKSUM_HW_SUPPORT			
	if (rx->flags & WSM_RX_STATUS_CHKSUM_ERROR){
		hdr->flag &=~RX_FLAG_HW_CHKSUM_ERROR;
		hdr->flag |= RX_FLAG_HW_CHKSUM_ERROR;
	}else{
		hdr->flag &=~RX_FLAG_HW_CHKSUM_ERROR;
	}
#endif
	if(rx->flags & WSM_RX_STATUS_8023_FRAME)
		hdr->flag |= RX_FLAG_8023;
}
struct sk_buff *wsm_rx_alloc_8023_frame(struct atbm_common *hw_priv,struct wsm_hdr *wsm)
{
	size_t len;
	struct wsm_buf wsmbuf;
	struct sk_buff *skb = NULL;
	struct wsm_rx rx;
	int interface_link_id = ((__le32_to_cpu(wsm->id)&0xfff) >> 6) & 0x0F;
	
	wsmbuf.begin = (u8 *)&wsm[0];
	wsmbuf.data = (u8 *)&wsm[1];
	wsmbuf.end = &wsmbuf.begin[__le32_to_cpu(wsm->len)];
	
	skb = atbm_dev_alloc_skb(wsm->len + 64+64);

	if(unlikely(skb == NULL)){
		goto underflow;
	}
	
	if(unlikely(wsm_fill_wsm_rx_info(&wsmbuf,&rx))){
		goto underflow;
	}
	
	rx.if_id = interface_link_id;
	
	atbm_skb_reserve(skb,64+64-14);

	len = wsm->len - (wsmbuf.data - wsmbuf.begin);
	WSM_GET(&wsmbuf,atbm_skb_put(skb,len),len);
	atbm_skb_trim(skb,skb->len - rx.icv_len);
	
	wsm_fill_rx_cb_info(IEEE80211_SKB_RXCB(skb),&rx);
	
	return skb;
underflow:
	if(skb)
		atbm_dev_kfree_skb(skb);
	return NULL;
}
int wsm_receive_indication_8023_frame(struct atbm_common *hw_priv,struct sk_buff* skb)
{
	struct atbm_vif *priv = NULL;
	struct ieee80211_rx_status *hdr = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_sta* sta = NULL;

	priv = ABwifi_hwpriv_to_vifpriv(hw_priv, hdr->if_id);
	
	if (unlikely(!priv)) {
		goto drop;
	}
	
	BUG_ON(hdr->aid > ATBMWIFI_MAX_STA_IN_AP_MODE+1);
	sta = rcu_dereference(priv->linked_sta[hdr->aid]);

	/*
	*sta not exit but fw translate to 8023,that situation
	*only opened when in 4way handshake or disconnneting state.
	*/
	if(unlikely(sta == NULL)){
		atbm_printk_err("[RX]:try to find rx sta\n");
		switch(priv->join_status){
		case ATBM_APOLLO_JOIN_STATUS_STA:
			/*
			*use join_bssid find sta
			*/
			sta = ieee80211_find_sta_by_ifaddr(priv->hw, priv->join_bssid, NULL);
			break;
		case ATBM_APOLLO_JOIN_STATUS_AP:
			/*
			*use SA to find sta
			*/
			sta = ieee80211_find_sta_by_ifaddr(priv->hw,skb->data + 6,NULL);
			break;
		default:
			atbm_printk_err("join status[%d] not support 8023\n",priv->join_status);
			goto drop;
		}
		/*
		*frame not for us
		*/
		if(unlikely(sta == NULL)){
			/*
			*if in ap mode ,need to send deauthen.
			*if in sta mode,firmware can not receied the package
			*/
			atbm_printk_err("STA not found[%pM][%pM]\n",skb->data,skb->data+6);
			goto drop;
		}
	}	
	atbm_printk_debug("[8023]:[%pM][%pM][%x:%x]\n",
					skb->data,skb->data+6,*(skb->data + 12),
					*(skb->data + 13));	
	atbm_ieee80211_rx_8023(priv->hw,sta,skb);
	atbm_priv_vif_list_read_unlock(&priv->vif_lock);
	
	return 0;
drop:
	if(priv)
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
	atbm_dev_kfree_skb(skb);
	return -1;
}
static int wsm_receive_indication(struct atbm_common *hw_priv,
					int interface_link_id,
					struct wsm_buf *buf,
					struct sk_buff **skb_p)
{
	struct atbm_vif *priv;
	if (hw_priv->wsm_cbc.rx) {
		struct wsm_rx rx;
		struct ieee80211_hdr *hdr;
		size_t hdr_len;
		__le16 fctl;

		if(wsm_fill_wsm_rx_info(buf,&rx)){
			goto underflow;
		}
		/*
		*receive 8023 frame
		*/
		if(rx.flags & WSM_RX_STATUS_8023_FRAME){

			rx.if_id = interface_link_id;
			atbm_skb_pull(*skb_p, buf->data - buf->begin);
			wsm_fill_rx_cb_info(IEEE80211_SKB_RXCB(*skb_p),&rx);
			wsm_receive_indication_8023_frame(hw_priv,*skb_p);			
			*skb_p = NULL;
			return 0;
		}			
#ifdef ATBM_SUPPORT_SMARTCONFIG
		if (rx.flags &WSM_RX_STATUS_SMARTCONFIG){
			fctl = *(__le16 *)buf->data;
			hdr_len = buf->data - buf->begin;
			atbm_skb_pull(*skb_p, hdr_len);
			smartconfig_start_rx(hw_priv,*skb_p,rx.channelNumber);
			if (*skb_p)
				atbm_skb_push(*skb_p, hdr_len);
			return 0;
		}
#endif	
//#ifdef CONFIG_WIRELESS_EXT
		if (hw_priv->bStartTx && hw_priv->etf_test_v2){
			fctl = *(__le16 *)buf->data;
			//printk("rx etf data %x\n",fctl);
			if(ieee80211_is_probe_resp(fctl )){
				hdr_len = buf->data - buf->begin;
				atbm_skb_pull(*skb_p, hdr_len);
				etf_v2_scan_rx(hw_priv,*skb_p,rx.rcpiRssi);
				if (*skb_p)
					atbm_skb_push(*skb_p, hdr_len);
			}
			return 0;
		}
//#endif //#ifdef CONFIG_WIRELESS_EXT
	/* TODO:COMBO: Frames received from scanning are received
		* with interface ID == 2 */
		if (interface_link_id == ATBM_WIFI_GENERIC_IF_ID) {
			/* Frames received in response to SCAN
			 * Request */
			interface_link_id = get_interface_id_scanning(hw_priv);
			if (interface_link_id == -1) {
#ifdef CONFIG_ATBM_SUPPORT_P2P
				interface_link_id = hw_priv->roc_if_id;
#endif
//				printk("%s %d if_id=%d\n",__func__,__LINE__,interface_link_id);
				if(interface_link_id == -1)
					interface_link_id = hw_priv->monitor_if_id;
#ifdef CONFIG_ATBM_STA_LISTEN
				if(interface_link_id == -1)
					interface_link_id = hw_priv->sta_listen_if;
#endif
			}
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
			if (hw_priv->auto_scanning) {
				interface_link_id = hw_priv->scan.if_id;
				atbm_printk_scan("%s %d if_id=%d\n",__func__,__LINE__,interface_link_id);
			}
#endif/*ROAM_OFFLOAD*/
#endif
		}
		/* linkid (peer sta id is encoded in bit 25-28 of
		   flags field */
		rx.if_id = interface_link_id;

		/* FW Workaround: Drop probe resp or
		beacon when RSSI is 0 */
		hdr = (struct ieee80211_hdr *) buf->data;

		//printk("wifi rx fc 0x%x if_id=%d,frame len %d\n",hdr->frame_control,rx.if_id,(*skb_p)->len-(buf->data - buf->begin));
		//print_hex_dump_bytes("rxframe<-- ",
		//		DUMP_PREFIX_NONE,
		//		 hdr,32);

		priv = ABwifi_hwpriv_to_vifpriv(hw_priv, rx.if_id);
		if (!priv) {
			//printk(KERN_ERR "wsm_receive_indication: NULL priv drop frame(%d)\n",rx.if_id);
			return 0;
		}

		/* FW Workaround: Drop probe resp or
		beacon when RSSI is 0 */
		if (((s8)(rx.rcpiRssi)>5) &&
		    (ieee80211_is_probe_resp(hdr->frame_control) ||
		    ieee80211_is_beacon(hdr->frame_control))) {
			atbm_priv_vif_list_read_unlock(&priv->vif_lock);
			atbm_printk_err("rcpiRssi %d\n",(s8)(rx.rcpiRssi));
			return 0;
		}
		
		hdr_len = buf->data - buf->begin;
		atbm_skb_pull(*skb_p, hdr_len);
		wsm_fill_rx_cb_info(IEEE80211_SKB_RXCB(*skb_p),&rx);
		hw_priv->wsm_cbc.rx(priv, &rx, skb_p);
		if (*skb_p)
			atbm_skb_push(*skb_p, hdr_len);
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
	}
	return 0;

underflow:
	return -EINVAL;
}

static int wsm_event_indication(struct atbm_common *hw_priv,
				struct wsm_buf *buf,
				int interface_link_id)
{
	struct atbm_vif *priv;
	struct wsm_event event;

	priv = ABwifi_hwpriv_to_vifpriv(hw_priv, interface_link_id);
	
	if (unlikely(!priv)) {
		wsm_printk( "[WSM] Event: %d(%d) for removed "
			   "interface, ignoring\n", event->evt.eventId,
			   event->evt.eventData);
		goto underflow;
	}
	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED)) {
		/* STA is stopped. */
		goto underflow;
	}
	event.eventId   = __le32_to_cpu(WSM_GET32(buf));
	event.eventData = __le32_to_cpu(WSM_GET32(buf));

	ieee80211_event_work(priv->vif,event.eventId,event.eventData);
	
	atbm_priv_vif_list_read_unlock(&priv->vif_lock);
	
	return 0;
underflow:
	if(priv)
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
	return -EINVAL;
}
static int wsm_set_pm_indication(struct atbm_common *hw_priv,
					struct wsm_buf *buf)
{
	atbm_printk_pm("[PM]:rev ind\n");
	spin_lock_bh(&hw_priv->wsm_pm_spin_lock);
	if(atomic_read(&hw_priv->wsm_pm_running) == 1){
		atomic_set(&hw_priv->wsm_pm_running, 0);
		atbm_del_timer(&hw_priv->wsm_pm_timer);
		wsm_oper_unlock(hw_priv);
		atbm_release_suspend(hw_priv);
		atbm_printk_pm("[PM]:up pm lock\n");
	}
	spin_unlock_bh(&hw_priv->wsm_pm_spin_lock);
	return 0;
}
static int wsm_scan_complete_indication(struct atbm_common *hw_priv,
					struct wsm_buf *buf)
{
	hw_priv->scan.wait_complete = 0;

#ifdef ROAM_OFFLOAD
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
	if(hw_priv->auto_scanning == 0)
		wsm_oper_unlock(hw_priv);
#endif
#else

	wsm_oper_unlock(hw_priv);
#endif /*ROAM_OFFLOAD*/
	if (hw_priv->wsm_cbc.scan_complete) {
		struct wsm_scan_complete arg;
		arg.status = WSM_GET32(buf);
		arg.psm = WSM_GET8(buf);
		arg.numChannels = WSM_GET8(buf);
#ifndef SIGMSTAR_SCAN_FEATURE
		if(hw_priv->scan.cca)
			WSM_GET(buf, arg.busy_ratio, sizeof(arg.busy_ratio));
#else //SIGMSTAR_SCAN_FEATURE
		WSM_GET(buf, arg.busy_ratio, sizeof(arg.busy_ratio));		
#endif //#ifdef SIGMSTAR_SCAN_FEATURE
		hw_priv->wsm_cbc.scan_complete(hw_priv, &arg);
	}
	return 0;

underflow:
	return -EINVAL;
}

static int wsm_find_complete_indication(struct atbm_common *hw_priv,
					struct wsm_buf *buf)
{
	/* TODO: Implement me. */
	//STUB();
	return 0;
}

static int wsm_suspend_resume_indication(struct atbm_common *hw_priv,
					 int interface_link_id,
					 struct wsm_buf *buf)
{
	if (hw_priv->wsm_cbc.suspend_resume) {
		u32 flags;
		struct wsm_suspend_resume arg;
		struct atbm_vif *priv;

		int i;
		arg.if_id = interface_link_id;
		/* TODO:COMBO: Extract bitmap from suspend-resume
		* TX indication */
		atbm_for_each_vif(hw_priv, priv, i) {
			if (!priv)
					continue;
				if (priv->join_status ==
					ATBM_APOLLO_JOIN_STATUS_AP) {
				 arg.if_id = priv->if_id;
				 break;
			}
			arg.link_id = 0;
		}


		flags = WSM_GET32(buf);
		arg.stop = !(flags & 1);
		arg.multicast = !!(flags & 8);
		arg.queue = (flags >> 1) & 3;

		priv = ABwifi_hwpriv_to_vifpriv(hw_priv, arg.if_id);
		if (unlikely(!priv)) {
			wsm_printk( "[WSM] suspend-resume indication"
				   " for removed interface!\n");
			return 0;
		}
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
		hw_priv->wsm_cbc.suspend_resume(priv, &arg);
	}
	return 0;

underflow:
	return -EINVAL;
}
#define WSM_DEBUG_IND_EPTA_RT_STATS     0
#define WSM_DEBUG_IND_EPTA_NRT_STATS    1
#define WSM_DEBUG_IND_EPTA_DBG_INFO    	2
#define WSM_DEBUG_IND_PS_DBG_INFO    	3
#define WSM_DEBUG_IND_PAS_DBG_INFO    	4
#define WSM_DEBUG_IND_TEMP   			5
#define WSM_DEBUG_IND_CPU_PROFILING   	6
#define WSM_HI_DEBUG_IND__EPTA_NRT_STATS__RESERVED  6
typedef struct WSM_HI_DEBUG_IND_S {
	u16 MsgLen;
	u16 Msgid;
	union WSM_HI_DEBUG_IND__DEBUG_DATA_U{
		struct WSM_HI_DEBUG_IND__EPTA_RT_STATS_S{
			u32 MsgStartId;
			u32 IsBtRt;
			u32 Timestamp;
			u32 LinkId;
			u32 NumRequests;
			u32 NumGrants;
		}EptaRtStats;
		struct WSM_HI_DEBUG_IND__EPTA_NRT_STATS_S{
		}EptaNRtStats;
		u32 RawData[1];

	}uDbgData;
}WSM_HI_DEBUG_IND;
static int wsm_debug_indication(struct atbm_common *hw_priv,
					 struct wsm_buf *buf)
{


	u32 dbg_id = WSM_GET32(buf);

	switch(dbg_id){
		case WSM_DEBUG_IND_EPTA_RT_STATS:
			break;
		case WSM_DEBUG_IND_EPTA_NRT_STATS:
			break;
		case WSM_DEBUG_IND_EPTA_DBG_INFO:
			break;
		case WSM_DEBUG_IND_PS_DBG_INFO:
			break;
		case WSM_DEBUG_IND_PAS_DBG_INFO:
			break;
		case WSM_DEBUG_IND_TEMP:
			break;
		case WSM_DEBUG_IND_CPU_PROFILING:
			break;
		default:
			break;
	}
	return 0;

underflow:
	return -EINVAL;
}
/* ******************************************************************** */
/* WSM TX								*/
static void wsm_cmd_hif_ximt(struct atbm_common *hw_priv)
{
	if(!hw_priv->sbus_ops->sbus_wsm_write){
		atbm_bh_wakeup(hw_priv);
	}else {
		hw_priv->sbus_ops->sbus_wsm_write(hw_priv->sbus_priv);
	}
}
#ifdef CIPHER_HIF_COMBINED
u8 wsm_checksum_u8(u8 *p, int len)
{
	u16 sum = 0;
	while(len--){
		sum += *p++;
	}
	sum = (sum >> 8) + (sum & 0xff);
	sum += (sum >> 8);
	return (~sum & 0xff);
}
#endif

int wsm_cmd_send(struct atbm_common *hw_priv,
		 struct wsm_buf *buf,
		 void *arg, u16 cmd, long tmo, int if_id)
{
	size_t buf_len = buf->data - buf->begin;
	struct wsm_hdr_tx * wsm_h = (struct wsm_hdr_tx *)buf->begin;
	int ret;	
	if(atbm_bh_is_term(hw_priv)){
		atbm_printk_err("bh_thread %p,bh_error %d pluged %d\n",(hw_priv->bh_thread), (hw_priv->bh_error),(atomic_read(&hw_priv->atbm_pluged)));
		wsm_buf_reset(buf);
		atbm_hif_status_set(1);
		return -3;
	}
	
	if (cmd == 0x0006) /* Write MIB */
		wsm_printk( "[WSM] >>> 0x%.4X [MIB: 0x%.4X] (%ld)\n",
			cmd, __le16_to_cpu(((__le16 *)buf->begin)[sizeof(struct wsm_hdr_tx)/2]),
			buf_len);
	else
		wsm_printk( "[WSM] >>> 0x%.4X (%ld)\n", cmd, buf_len);

	/* Fill HI message header */
	/* BH will add sequence number */

	/* TODO:COMBO: Add if_id from  to the WSM header */
	/* if_id == -1 indicates that command is HW specific,
	 * eg. wsm_configuration which is called during driver initialzation
	 *  (mac80211 .start callback called when first ifce is created. )*/

	/* send hw specific commands on if 0 */
	if (if_id == -1)
		if_id = 0;
	wsm_h = (struct wsm_hdr_tx *)buf->begin;

#ifdef CIPHER_HIF_COMBINED   //old wsm interface.

//new WSM interface. add enc header

	{
		//struct wsm_hdr *hdr = (struct wsm_hdr *)buf->begin;		
		wsm_h->u.common.total_len =__cpu_to_le16(buf_len);
		wsm_h->u.common.checksum = wsm_checksum_u8(buf->begin, 7);
		wsm_h->u.common.flag = 0;
		wsm_h->len = __cpu_to_le16(buf_len);
		wsm_h->id = __cpu_to_le16(cmd |(if_id << 6) );
	}
#endif

//#ifdef USB_BUS
//	((__le16 *)buf->begin)[2] = __cpu_to_le16(buf_len);
//	((__le16 *)buf->begin)[3] = __cpu_to_le16(cmd |(if_id << 6) );
//#else
//
//        ((__le16 *)buf->begin)[0] = __cpu_to_le16(buf_len);
//        ((__le16 *)buf->begin)[1] = __cpu_to_le16(cmd |(if_id << 6) );
//#endif
	spin_lock_bh(&hw_priv->wsm_cmd.lock);
    BUG_ON(hw_priv->wsm_cmd.ptr);
	hw_priv->wsm_cmd.done = 0;
	hw_priv->wsm_cmd.ptr = buf->begin;
	hw_priv->wsm_cmd.len = buf_len;
	hw_priv->wsm_cmd.arg = arg;
	hw_priv->wsm_cmd.cmd = cmd;
	hw_priv->wsm_cmd.cnf = (tmo != WSM_CMD_DONOT_CONFIRM_TIMEOUT);
	tmo = (tmo == WSM_CMD_DONOT_CONFIRM_TIMEOUT ? WSM_CMD_TIMEOUT : tmo);
	spin_unlock_bh(&hw_priv->wsm_cmd.lock);
	hw_priv->sbus_ops->power_mgmt(hw_priv->sbus_priv, false);

#ifdef USB_CMD_UES_EP0
	atbm_ep0_write_cmd(hw_priv,wsm_h);
#else
	wsm_cmd_hif_ximt(hw_priv);
#endif
	if (unlikely(hw_priv->bh_error)||atbm_bh_is_term(hw_priv)) {
		/* Do not wait for timeout if BH is dead. Exit immediately. */
		ret = 0;
	} else {
		long wsm_cmd_starttime = jiffies;
		long wsm_cmd_runtime;
		long wsm_cmd_max_tmo = WSM_CMD_DEFAULT_TIMEOUT;

		/* Give start cmd a little more time */
		if (tmo == WSM_CMD_START_TIMEOUT)
			wsm_cmd_max_tmo = WSM_CMD_START_TIMEOUT;

		if (tmo == WSM_CMD_SCAN_TIMEOUT)
			wsm_cmd_max_tmo = WSM_CMD_SCAN_TIMEOUT;
		
		tmo = wsm_cmd_max_tmo*4+1;
		
		/* Firmware prioritizes data traffic over control confirm.
		 * Loop below checks if data was RXed and increases timeout
		 * accordingly. */
		do {
			/* It's safe to use unprotected access to
			 * wsm_cmd.done here */
			ret = atbm_wait_event_timeout_stay_awake(hw_priv,
					hw_priv->wsm_cmd_wq,
					hw_priv->wsm_cmd.done
					, tmo,true);
			wsm_cmd_runtime = jiffies - wsm_cmd_starttime;
			if(!ret  &&
					wsm_cmd_runtime < wsm_cmd_max_tmo && 
					(atomic_read(&hw_priv->bh_term)!=0)){
				//wakeup again
				wsm_cmd_hif_ximt(hw_priv);
			}
		} while (!ret  &&
					wsm_cmd_runtime < wsm_cmd_max_tmo && 
					(atomic_read(&hw_priv->bh_term)!=0));
	}
	
	if (unlikely(ret == 0)) {

		spin_lock_bh(&hw_priv->wsm_cmd.lock);
		if(hw_priv->wsm_cmd.ptr){
			atbm_printk_err("wsm_cmd_send cmd not send to fw\n");
		}
		atbm_printk_err("wsm_cmd_send,buffused(%d)\n",hw_priv->hw_bufs_used);
		hw_priv->wsm_cmd.arg = NULL;
		hw_priv->wsm_cmd.ptr = NULL;
		hw_priv->wsm_cmd.done = 1;
		ret = -1;
		hw_priv->wsm_cmd.ret = -1;
		hw_priv->wsm_cmd.cmd = 0xFFFF;
		spin_unlock_bh(&hw_priv->wsm_cmd.lock);
		atbm_printk_err("wsm_cmd_send timeout cmd %x tmo %ld\n",cmd,tmo);		
		/* Kill BH thread to report the error to the top layer. */
		//hw_priv->bh_error = 1;
		
		atbm_bh_halt(hw_priv);
		ret = -ETIMEDOUT;
	} else {
		spin_lock_bh(&hw_priv->wsm_cmd.lock);
		BUG_ON(!hw_priv->wsm_cmd.done);
		ret = hw_priv->wsm_cmd.ret;
		spin_unlock_bh(&hw_priv->wsm_cmd.lock);
	}
	wsm_buf_reset(buf);
	hw_priv->sbus_ops->power_mgmt(hw_priv->sbus_priv, true);
	return ret;
}

/* ******************************************************************** */
/* WSM TX port control							*/

void wsm_lock_tx(struct atbm_common *hw_priv)
{
	wsm_cmd_lock(hw_priv);
	if (atomic_add_return(1, &hw_priv->tx_lock) == 1) {
		if (wsm_flush_tx(hw_priv))
			wsm_printk( "[WSM] TX is locked.\n");
	}
	wsm_cmd_unlock(hw_priv);
}

void wsm_vif_lock_tx(struct atbm_vif *priv)
{
	struct atbm_common *hw_priv = priv->hw_priv;
	wsm_cmd_lock(hw_priv);
	if (atomic_add_return(1, &hw_priv->tx_lock) == 1) {
		if (wsm_vif_flush_tx(priv))
			wsm_printk( "[WSM] TX is locked for"
					" if_id %d.\n", priv->if_id);
	}
	wsm_cmd_unlock(hw_priv);
}

void wsm_lock_tx_async(struct atbm_common *hw_priv)
{
	if (atomic_add_return(1, &hw_priv->tx_lock) == 1)
		wsm_printk( "[WSM] TX is locked (async).\n");
}

void wsm_unlock_tx_async(struct atbm_common *hw_priv)
{
	 atomic_sub_return(1, &hw_priv->tx_lock);
}





bool wsm_flush_tx(struct atbm_common *hw_priv)
{
	unsigned long timestamp = jiffies;
	bool pending = false;
	long timeout;
	int i;



	/* Flush must be called with TX lock held. */
	BUG_ON(!atomic_read(&hw_priv->tx_lock));

	/* First check if we really need to do something.
	 * It is safe to use unprotected access, as hw_bufs_used
	 * can only decrements. */
	if (!(hw_priv->hw_bufs_used)){
		return true;
	}

	if(atbm_bh_is_term(hw_priv)){
//		hw_priv->bh_error = 1;
		return false;
	}

	if (hw_priv->bh_error) {
		/* In case of failure do not wait for magic. */
		wsm_printk( "[WSM] Fatal error occured, "
				"will not flush TX.\n");
		return false;
	} else {
		/* Get a timestamp of "oldest" frame */
		for (i = 0; i < 4; ++i)
			pending |= atbm_queue_get_xmit_timestamp(
					&hw_priv->tx_queue[i],
					&timestamp, ATBM_WIFI_ALL_IFS,
					0xffffffff);
		/* It is allowed to lock TX with only a command in the pipe. */
		if (!pending)
			return true;

		timeout=WSM_CMD_LAST_CHANCE_TIMEOUT;
		if (atbm_wait_event_timeout_stay_awake(hw_priv,hw_priv->bh_evt_wq,!(hw_priv->hw_bufs_used),timeout,true) <= 0) {
			atbm_printk_err( "+++++  bh_error=1 have txframe pending hw_bufs_used %d,timeout =%d\n",(u32)hw_priv->hw_bufs_used,(u32)timeout);
			if(!(hw_priv->hw_bufs_used)){
			/* Get a timestamp of "oldest" frame */
                	  for (i = 0; i < 4; ++i)
                        	pending |= atbm_queue_get_xmit_timestamp(
                                        &hw_priv->tx_queue[i],
                                        &timestamp, ATBM_WIFI_ALL_IFS,
                                        0xffffffff);

			   atbm_printk_err("<WARNING hw_bufs_use==0,pending %x,but wait imeout!!!!!!\n",pending);
			   
			   return true;
			}
			atbm_printk_err("+++++  bh_error=1 have txframe pending hw_bufs_used %d,timeout =%d\n",(u32)hw_priv->hw_bufs_used,(u32)timeout);
			if(!(hw_priv->hw_bufs_used)){
			/* Get a timestamp of "oldest" frame */
                	  for (i = 0; i < 4; ++i)
                        	pending |= atbm_queue_get_xmit_timestamp(
                                        &hw_priv->tx_queue[i],
                                        &timestamp, ATBM_WIFI_ALL_IFS,
                                        0xffffffff);

			   atbm_printk_err("<WARNING hw_bufs_use==0,pending %x,but wait imeout!!!!!!\n",pending);
			   
			   return true;
			}
			//				
			atbm_printk_err("bh_error=1 have txframe pending hw_bufs_used %d,hw_noconfirm_tx %d,timeout =%d\n",(u32)hw_priv->hw_bufs_used,hw_priv->hw_noconfirm_tx,(u32)timeout);
			{
				/* Hmmm... Not good. Frame had stuck in firmware. */
//				hw_priv->bh_error = 1;
				atbm_printk_err("bh_error=1 have txframe pending hw_bufs_used %d,timeout =%d\n",(u32)hw_priv->hw_bufs_used,(u32)timeout);
				WARN_ON(1);
//				wake_up(&hw_priv->bh_wq);
				atbm_bh_halt(hw_priv);
				return false;
			}
		}
		/* Ok, everything is flushed. */
		return true;
	}
}

bool wsm_vif_flush_tx(struct atbm_vif *priv)
{
	struct atbm_common *hw_priv = priv->hw_priv;
	unsigned long timestamp = jiffies;
	long timeout;
	int i;
	int if_id = priv->if_id;
	/* Flush must be called with TX lock held. */
	BUG_ON(!atomic_read(&hw_priv->tx_lock));

	/* First check if we really need to do something.
	 * It is safe to use unprotected access, as hw_bufs_used
	 * can only decrements. */
	if (!hw_priv->hw_bufs_used_vif[priv->if_id])
		return true;
	if (hw_priv->bh_error) {
		/* In case of failure do not wait for magic. */
		wsm_printk(KERN_ERR "[WSM] Fatal error occured, "
				"will not flush TX.\n");
		return false;
	} else {
		/* Get a timestamp of "oldest" frame */
		for (i = 0; i < 4; ++i)
			atbm_queue_get_xmit_timestamp(
					&hw_priv->tx_queue[i],
					&timestamp, if_id,
					0xffffffff);
		/* It is allowed to lock TX with only a command in the pipe. */
		if (!hw_priv->hw_bufs_used_vif[if_id])
			return true;

		timeout =WSM_CMD_LAST_CHANCE_TIMEOUT ;
		if (timeout < 0 || atbm_wait_event_timeout_stay_awake(hw_priv,hw_priv->bh_evt_wq,
				!hw_priv->hw_bufs_used_vif[if_id],
				timeout,true) <= 0) {

			atbm_printk_err("%s:++  bh_error=1 hw_bufs_used_vif %d,hw_bufs_used %d,timeout %ld\n", __func__,
						hw_priv->hw_bufs_used_vif[priv->if_id],hw_priv->hw_bufs_used,timeout);
			//			
			{
				/* Hmmm... Not good. Frame had stuck in firmware. */
//				hw_priv->bh_error = 1;
				atbm_printk_err("%s:  bh_error=1 hw_bufs_used_vif %d,hw_bufs_used %d,timeout %ld\n", __func__,
						hw_priv->hw_bufs_used_vif[priv->if_id],hw_priv->hw_bufs_used,timeout);
				WARN_ON(1);
//				wake_up(&hw_priv->bh_wq);
				atbm_bh_halt(hw_priv);
				return false;
			}
		}
		/* Ok, everything is flushed. */
		return true;
	}
}


void wsm_unlock_tx(struct atbm_common *hw_priv)
{
	int tx_lock;
	if (hw_priv->bh_error
		)
		atbm_printk_err("fatal error occured, unlock is unsafe\n");
	else {
		tx_lock = atomic_sub_return(1, &hw_priv->tx_lock);
		if (tx_lock < 0) {
			BUG_ON(1);
		} else if (tx_lock == 0) {
			atbm_bh_wakeup(hw_priv);
			wsm_printk(KERN_DEBUG "[WSM] TX is unlocked.\n");
		}
	}
}

/* ******************************************************************** */
/* WSM RX								*/

int wsm_handle_exception(struct atbm_common *hw_priv, u8 *data, u32 len)
{
	struct atbm_vif *priv = NULL;
	struct wsm_buf buf;
	u32 reason;
	u32 reg[18];
	char fname[32];
	int i;
	int if_id = 0;

#ifdef CONFIG_PM

	/* Send the event upwards on the FW exception */
	atbm_pm_stay_awake(&hw_priv->pm_state, 3*HZ);

#endif

	atbm_hw_vif_read_lock(&hw_priv->vif_list_lock);
	atbm_for_each_vif_safe(hw_priv, priv, if_id) {
		if (!priv)
			continue;
//		ieee80211_driver_hang_notify(priv->vif, GFP_KERNEL);
	}
	atbm_hw_vif_read_unlock(&hw_priv->vif_list_lock);

	buf.begin = buf.data = data;
	buf.end = &buf.begin[len];

	reason = WSM_GET32(&buf);
	atbm_printk_always("wsm_handle_exception :reason[%d] \n ",reason);
	for (i = 0; i < ARRAY_SIZE(reg); ++i)
		reg[i] = WSM_GET32(&buf);
	WSM_GET(&buf, fname, sizeof(fname));


	atbm_printk_always("Firmware assert at %d,%s, line %d\n",
			(int)sizeof(fname), fname, (int)reg[1]);

	for (i = 0; i < 12; i += 4)
		atbm_printk_always("R%d: 0x%.8X, R%d: 0x%.8X, R%d: 0x%.8X, R%d: 0x%.8X,\n",
			i + 0, reg[i + 0], i + 1, reg[i + 1],
			i + 2, reg[i + 2], i + 3, reg[i + 3]);
	atbm_printk_always("R12: 0x%.8X, SP: 0x%.8X, LR: 0x%.8X, PC: 0x%.8X,\n",
		reg[i + 0], reg[i + 1], reg[i + 2], reg[i + 3]);
	i += 4;
	atbm_printk_always("CPSR: 0x%.8X, SPSR: 0x%.8X\n",
		reg[i + 0], reg[i + 1]);

	print_hex_dump_bytes("R1: ", DUMP_PREFIX_NONE,
		fname, sizeof(fname));

	atbm_wifi_run_status_set(1);
	return 0;

underflow:
	atbm_printk_always("Firmware exception.\n");
	print_hex_dump_bytes("Exception: ", DUMP_PREFIX_NONE,
		data, len);
	atbm_wifi_run_status_set(1);
	return -EINVAL;
}
#if 0
static int wsm_test_confirm(struct atbm_common *hw_priv,
				struct wsm_buf *buf)
{
	int ret = 0;
	int count;
	
	count =WSM_GET32(buf);
	ret = wsm_release_tx_buffer(hw_priv, count-1);
	//printk("count:%d,hw_bufs_used:%d\n",count,hw_priv->hw_bufs_used);
	return ret;

underflow:
	WARN_ON(1);
	return -EINVAL;
}
#endif
extern int atbm_rx_cnt;
int wsm_handle_rx(struct atbm_common *hw_priv, int id,
		  struct wsm_hdr *wsm, struct sk_buff **skb_p)
{
	struct wsm_buf wsm_buf;
#ifdef MCAST_FWDING
	struct atbm_vif *priv = NULL;
	int i = 0;
#endif
	int interface_link_id = (id >> 6) & 0x0F;
	int ret = 0;
	/* Strip link id. */
	id &= ~WSM_TX_LINK_ID(WSM_TX_LINK_ID_MAX);

	wsm_buf.begin = (u8 *)&wsm[0];
	wsm_buf.data = (u8 *)&wsm[1];
	wsm_buf.end = &wsm_buf.begin[__le32_to_cpu(wsm->len)];

	wsm_printk("[WSM][vid=%d] <<< 0x%.4X (%d)\n",interface_link_id, id,
			wsm_buf.end - wsm_buf.begin);
//	frame_hexdump(__func__,(u32*)wsm,32);
	if (id == WSM_TX_REQ_ID) {
		ret = wsm_tx_confirm(hw_priv, &wsm_buf, interface_link_id);
#ifdef MCAST_FWDING
	} else if (id == WSM_GIVE_BUFFER_REQ_ID) {
		ret = wsm_give_buffer_confirm(hw_priv, &wsm_buf);
#endif
	} 
	else if (id == WSM_FIRMWARE_CHECK_CONFIRM_ID) {
		//ret = wsm_multi_tx_confirm(hw_priv, &wsm_buf,
		//			   interface_link_id);
		atbm_printk_bus("WSM_FIRMWARE_CHECK_CONFIRM_ID \n");

	}
	else if (id == WSM_LEGACY_MULTI_TX_CNF_ID) {
		ret = wsm_multi_tx_confirm(hw_priv, &wsm_buf,
					   interface_link_id);
	}
	else if (id & WSM_CNF_BASE) {
		void *wsm_arg;
		u16 wsm_cmd;

		/* Do not trust FW too much. Protection against repeated
		 * response and race condition removal (see above). */
		spin_lock_bh(&hw_priv->wsm_cmd.lock);
		wsm_arg = hw_priv->wsm_cmd.arg;
		wsm_cmd = hw_priv->wsm_cmd.cmd &
				~WSM_TX_LINK_ID(WSM_TX_LINK_ID_MAX);
		hw_priv->wsm_cmd.last_send_cmd=hw_priv->wsm_cmd.cmd = 0xFFFF;
		
		spin_unlock_bh(&hw_priv->wsm_cmd.lock);
		
		if (WARN_ON((id & ~WSM_CNF_BASE) != wsm_cmd)) {
			/* Note that any non-zero is a fatal retcode. */
			ret = -EINVAL;
			goto out;
		}
		switch (id) {

		case 0x0400:
			if (likely(wsm_arg))
				ret = wsm_read_shmem_confirm(hw_priv,
									wsm_arg,
									&wsm_buf);
			break;
		case 0x0401:
			if (likely(wsm_arg))
				ret = wsm_write_shmem_confirm(hw_priv,
									wsm_arg,
									&wsm_buf);
			break;
		case WSM_GENERIC_RESP_ID:
			if (likely(wsm_arg))
				ret = wsm_generic_req_confirm(hw_priv, wsm_arg, &wsm_buf);
			break;
		case WSM_CONFIGURATION_RESP_ID:
			/* Note that wsm_arg can be NULL in case of timeout in
			 * wsm_cmd_send(). */
			if (likely(wsm_arg))
				ret = wsm_configuration_confirm(hw_priv,
								wsm_arg,
								&wsm_buf);
			break;
		case WSM_READ_MIB_RESP_ID:
			if (likely(wsm_arg))
				ret = wsm_read_mib_confirm(hw_priv, wsm_arg,
								&wsm_buf);
			break;
		case WSM_WRITE_MIB_RESP_ID:
			if (likely(wsm_arg))
				ret = wsm_write_mib_confirm(hw_priv, wsm_arg,
							    &wsm_buf,
							    interface_link_id);
			break;
		case WSM_JOIN_RESP_ID:
			if (likely(wsm_arg))
				ret = wsm_join_confirm(hw_priv, wsm_arg,
						       &wsm_buf);
			break;
		case WSM_HI_EFUSE_CHANGE_DATA_CNF_ID:
				ret = wsm_efuse_change_data_confirm(hw_priv, &wsm_buf);
			break;
#ifdef MCAST_FWDING
		case WSM_REQUEST_BUFFER_REQ_CNF_ID: /* req buffer cfm*/
			if (likely(wsm_arg)){
				atbm_for_each_vif(hw_priv, priv, i) {
					if (priv && (priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP))
						ret = wsm_request_buffer_confirm(priv,
								wsm_arg, &wsm_buf);
				}
			}
			break;
#endif
		case WSM_STOP_SCAN_RESP_ID: /* stop-scan */
			{
				wsm_stop_scan_confirm(hw_priv, wsm_arg, &wsm_buf);
				break;
			}
		case WSM_START_SCAN_RESP_ID: /* start-scan */
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
			if (hw_priv->auto_scanning) {
				if (atomic_read(&hw_priv->scan.in_progress)) {
					hw_priv->auto_scanning = 0;
				}
				else {
					wsm_oper_unlock(hw_priv);
					up(&hw_priv->scan.lock);
				}
			}
	
#endif /*ROAM_OFFLOAD*/
#endif
			//must be no break here!!!!!!!!!!!!!
#ifdef CONFIG_ATBM_SUPPORT_CSA
		case WSM_CSA_RESP_ID:
		case WSM_CSA_RESP_CONCURRENT_ID:
#endif
		case WSM_SET_CHANTYPE_RESP_ID:
		case WSM_RESET_RESP_ID: /* wsm_reset */
		case WSM_ADD_KEY_RESP_ID: /* add_key */
		case WSM_REMOVE_KEY_RESP_ID: /* remove_key */
		case WSM_SET_PM_RESP_ID: /* wsm_set_pm */
		case WSM_SET_BSS_PARAMS_RESP_ID: /* set_bss_params */
		case WSM_QUEUE_PARAMS_RESP_ID: /* set_tx_queue_params */
		case WSM_EDCA_PARAMS_RESP_ID: /* set_edca_params */
		case WSM_SWITCH_CHANNEL_RESP_ID: /* switch_channel */
		case WSM_START_RESP_ID: /* start */
		case WSM_BEACON_TRANSMIT_RESP_ID: /* beacon_transmit */
		//case WSM_START_FIND_RESP_ID: /* start_find */
		case WSM_STOP_FIND_RESP_ID: /* stop_find */
		case WSM_UPDATE_IE_RESP_ID: /* update_ie */
		case WSM_MAP_LINK_RESP_ID: /* map_link */
		case WSM_RX_BA_SESSION_RESP_ID:
#ifdef CONFIG_ATBM_BLE_ADV_COEXIST
		case WSM_BLE_MSG_RESP_ID:
#endif
			WARN_ON(wsm_arg != NULL);
			ret = wsm_generic_confirm(hw_priv, wsm_arg, &wsm_buf);
			if (ret)
				atbm_printk_warn("wsm_generic_confirm "
					"failed for request 0x%.4X.\n",
					id & ~0x0400);
			break;
		default:
			BUG_ON(1);
		}

		spin_lock_bh(&hw_priv->wsm_cmd.lock);
		hw_priv->wsm_cmd.ret = ret;
		hw_priv->wsm_cmd.done = 1;
		spin_unlock_bh(&hw_priv->wsm_cmd.lock);
		ret = 0; /* Error response from device should ne stop BH. */

		wake_up(&hw_priv->wsm_cmd_wq);
	} else if (id & WSM_IND_BASE) {
		switch (id) {
		case WSM_SMARTCONFIG_INDICATION_ID:
			ret = wsm_smartconfig_indication(hw_priv, interface_link_id,
					&wsm_buf, skb_p);
			break;
		case WSM_DEBUG_PRINT_IND_ID:
			ret = wsm_debug_print_indication(hw_priv, &wsm_buf);
			break;
		case WSM_STARTUP_IND_ID:
			ret = wsm_startup_indication(hw_priv, &wsm_buf);
			break;
		case WSM_RECEIVE_INDICATION_ID:
			ret = wsm_receive_indication(hw_priv, interface_link_id,
					&wsm_buf, skb_p);
			break;
		case WSM_EVENT_INDICATION_ID:
			ret = wsm_event_indication(hw_priv, &wsm_buf,
					interface_link_id);
			break;
		case WSM_SWITCH_CHANNLE_IND_ID:
	//		ret = wsm_channel_switch_indication(hw_priv, &wsm_buf);
			break;
		case WSM_SET_PM_MODE_CMPL_IND_ID:
			ret = wsm_set_pm_indication(hw_priv, &wsm_buf);
			break;
		case WSM_SCAN_COMPLETE_IND_ID:
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
			if(hw_priv->auto_scanning && hw_priv->frame_rcvd) {
				struct atbm_vif *priv;
				hw_priv->frame_rcvd = 0;
				priv = ABwifi_hwpriv_to_vifpriv(hw_priv, hw_priv->scan.if_id);
				if (unlikely(!priv)) {
					WARN_ON(1);
					return 0;
				}
					atbm_priv_vif_list_read_unlock(&priv->vif_lock);
				if (hw_priv->beacon) {
					struct wsm_scan_complete *scan_cmpl = \
						(struct wsm_scan_complete *) \
						((u8 *)wsm + sizeof(struct wsm_hdr));
					struct ieee80211_rx_status *rhdr = \
						IEEE80211_SKB_RXCB(hw_priv->beacon);
					rhdr->signal = (s8)scan_cmpl->reserved;
					if (!priv->cqm_use_rssi) {
						rhdr->signal = rhdr->signal / 2 - 110;
					}
					if (!hw_priv->beacon_bkp)
						hw_priv->beacon_bkp = \
						atbm_skb_copy(hw_priv->beacon, GFP_ATOMIC);
					hw_priv->beacon = hw_priv->beacon_bkp;

					hw_priv->beacon_bkp = NULL;
				}
				wsm_printk( \
				"[WSM] Send Testmode Event.\n");
				atbm_testmode_event(priv->hw->wiphy,
					NL80211_CMD_NEW_SCAN_RESULTS, 0,
					0, GFP_KERNEL);

			}
#endif /*ROAM_OFFLOAD*/
#endif
			ret = wsm_scan_complete_indication(hw_priv, &wsm_buf);
			break;
		case WSM_FIND_CMPL_IND_ID:
			ret = wsm_find_complete_indication(hw_priv, &wsm_buf);
			break;
		case WSM_SUSP_RESUME_TX_IND_ID:
			ret = wsm_suspend_resume_indication(hw_priv,
					interface_link_id, &wsm_buf);
			break;
		case WSM_DEBUG_IND_ID:
			ret = wsm_debug_indication(hw_priv, &wsm_buf);
			break;
#ifdef CONFIG_ATBM_BLE_ADV_COEXIST
		case WSM_BLE_IND_ID:
			ret = wsm_ble_indication(hw_priv, &wsm_buf, interface_link_id);
			break;
#endif
		default:
			//STUB();
			break;
		}
	} else {
		atbm_printk_err("%s:id(%x)\n",__func__,id);
		WARN_ON(1);
		ret = -EINVAL;
	}
out:
	return ret;
}
static void wsm_build_80211_frame(struct wsm_tx_encap *wsm_encap)
{
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(wsm_encap->skb);
	/*
	*set sta for later use
	*/
	info->control.sta = rcu_dereference(wsm_encap->priv->linked_sta[wsm_encap->txpriv->raw_link_id]);
	
	wsm_encap->len = ieee80211_build_80211_frame(wsm_encap->priv->vif, wsm_encap->skb, wsm_encap->data + sizeof(struct wsm_tx));
}
static bool wsm_handle_tx_data(struct wsm_tx_encap *wsm_encap)
{
	struct wsm_tx* wsm = (struct wsm_tx*)wsm_encap->data;
	struct atbm_vif *priv = wsm_encap->priv;
	struct atbm_common* hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	struct atbm_txpriv *txpriv = wsm_encap->txpriv;
	struct ieee80211_tx_info* tx_info = IEEE80211_SKB_CB(wsm_encap->skb);
	bool handled = false;
	enum
	{
		doDrop,
		doTx,
	} action = doTx;

#if __SIZEOF_POINTER__  == 8
	BUG_ON((u64)wsm % 4);
#else
	BUG_ON((u32)wsm % 4);
#endif
	memset(wsm, 0, sizeof(*wsm));
	wsm_build_80211_frame(wsm_encap);
	
	if (unlikely(wsm_encap->len == 0)) {
		atbm_printk_err("%s:build 80211 err\n", __func__);
		action = doDrop;
	}else {
		/*
		*build wsm hdr
		*/
		atbm_build_wsm_hdr(wsm_encap);		
		/*
		*build rate policy
		*/
		atbm_build_rate_policy(wsm_encap);
		
		switch (wsm_encap->priv->mode) {
		case NL80211_IFTYPE_STATION:
#ifdef CONFIG_ATBM_STA_LISTEN
			if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA_LISTEN) {
				txpriv->offchannel_if_id = ATBM_WIFI_GENERIC_IF_ID;
			}
			else
#endif
			if (unlikely(priv->join_status <= ATBM_APOLLO_JOIN_STATUS_MONITOR)) {
#ifdef CONFIG_ATBM_SUPPORT_P2P
				if ((atomic_read(&hw_priv->remain_on_channel) || (hw_priv->roc_if_id != -1)) &&
					(hw_priv->roc_if_id == priv->if_id) &&
					priv->join_status <= ATBM_APOLLO_JOIN_STATUS_MONITOR) {

					txpriv->offchannel_if_id = ATBM_WIFI_GENERIC_IF_ID;
					atbm_printk_mgmt("%s:remain_on_channel tx\n", __func__);
				}
				else
#endif
				{
					/*
					*the current priv is not joined and in listenning mode,so we dont kown
					*the channel.
					*/
					atbm_printk_err("drop\n");
					action = doDrop;
				}

			}
			break;
		case NL80211_IFTYPE_AP:
			if (unlikely(!priv->join_status))
				action = doDrop;
			else if (unlikely(!(BIT(txpriv->raw_link_id) &
				(BIT(0) | priv->link_id_map)))) {
				atbm_printk_warn("A frame with expired link id "
					"is dropped.\n");
				action = doDrop;
			}
			break;
#ifdef CONFIG_ATBM_SUPPORT_IBSS
		case NL80211_IFTYPE_ADHOC:
			if (priv->join_status != ATBM_APOLLO_JOIN_STATUS_IBSS)
				action = doDrop;
			break;
#endif
		case NL80211_IFTYPE_MESH_POINT:
			//STUB();
		case NL80211_IFTYPE_MONITOR:
			if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_SIMPLE_MONITOR) {
				txpriv->offchannel_if_id = ATBM_WIFI_GENERIC_IF_ID;
				action = doTx;
				break;
			}
			atbm_fallthrough;
		default:
			action = doDrop;
			break;
		}

	}
	
	*wsm_encap->source = NULL;
	
	switch (action) {
	case doDrop:
	{
		/* See detailed description of "join" below.
		 * We are dropping everything except AUTH in non-joined mode. */
		atbm_printk_err( "[WSM] Drop frame\n");
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		BUG_ON(atbm_queue_remove(hw_priv, wsm_encap->queue,
			__le32_to_cpu(wsm_encap->packetID)));
#else
		BUG_ON(atbm_queue_remove(wsm_encap->queue,
			__le32_to_cpu(wsm_encap->packetID)));
#endif /*CONFIG_ATBM_APOLLO_TESTMODE*/
		handled = true;
	}
	break;
	case doTx:
	{
		atbm_printk_debug("[WSM] Tx frame\n");
		if (wsm->htTxParameters & __cpu_to_le32(WSM_NEED_TX_CONFIRM)){
			*wsm_encap->source = wsm_encap->data;
			hw_priv->hw_noconfirm_tx++;
			wsm_alloc_tx_and_vif_buffer(hw_priv,priv->if_id);
		}else {
#ifdef CONFIG_ATBM_SUPPORT_TSO
			if(tx_info->requeue == 0){
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
				BUG_ON(atbm_queue_remove(hw_priv, wsm_encap->queue,
					__le32_to_cpu(wsm_encap->packetID)));
#else
				BUG_ON(atbm_queue_remove(wsm_encap->queue,
					__le32_to_cpu(wsm_encap->packetID)));
#endif /*CONFIG_ATBM_APOLLO_TESTMODE*/
			}else {
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
				WARN_ON(atbm_queue_requeue(hw_priv, wsm_encap->queue,
						wsm_encap->packetID, true));
#else
				WARN_ON(atbm_queue_requeue(wsm_encap->queue,
						wsm_encap->packetID, true));
#endif
			}
#else //CONFIG_ATBM_SUPPORT_TSO
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
			WARN_ON(atbm_queue_remove(hw_priv, wsm_encap->queue,
					wsm_encap->packetID));
#else
			WARN_ON(atbm_queue_remove(wsm_encap->queue,
					wsm_encap->packetID));
#endif
									
#endif//CONFIG_ATBM_SUPPORT_TSO
		}
	}
	break;
	}
	return handled;
}

static int atbm_get_prio_queue(struct atbm_vif *priv,
				 u32 link_id_map, int *total)
{
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	static u32 urgent;
	struct wsm_edca_queue_params *edca;
	unsigned score, best = -1;
	int winner = -1;
	int queued;
	int i;
	urgent = BIT(atbm_dtim_virtual_linkid()) | BIT(atbm_uapsd_virtual_linkid());

	/* search for a winner using edca params */
	for (i = 0; i < 4; ++i) {
		queued = atbm_queue_get_num_queued(priv,
				&hw_priv->tx_queue[i],
				link_id_map);
		if (!queued)
			continue;
		*total += queued;
		edca = &priv->edca.params[i];

		score = ((edca->aifns + edca->cwMin) << 16) +
				(edca->cwMax - edca->cwMin) *
				(prandom_u32() & 0xFFFF);

		//score = ((edca->aifns) << 8) +
		//		((1<<edca->cwMin) &random32());
//#define EDCA_QUEUE_FIX
#ifdef EDCA_QUEUE_FIX
		if(((hw_priv->wsm_caps.numInpChBufs -hw_priv->hw_bufs_used) < (hw_priv->wsm_caps.numInpChBufs /2))
			&&	(hw_priv->tx_queue[i].num_pending_vif[priv->if_id] <= (hw_priv->wsm_caps.numInpChBufs /8))){
		//	printk("decl score queue[%d] \n",i);

		//printk("queuePending :[VO]<%d>[Vi]<%d>[BE]<%d>[BK]<%d> \n",
		//	 hw_priv->tx_queue[0].num_pending,
		//	 hw_priv->tx_queue[1].num_pending,
		//	 hw_priv->tx_queue[2].num_pending,
		//   	 hw_priv->tx_queue[3].num_pending);
			score = score>>2;
		}
#endif /*add by wp*/
		if (score < best && (winner < 0 || i != 3)) {
			best = score;
			winner = i;
		}
	}

	/* override winner if bursting */
	if (winner >= 0 && hw_priv->tx_burst_idx >= 0 &&
			winner != hw_priv->tx_burst_idx &&
			!atbm_queue_get_num_queued(priv,
				&hw_priv->tx_queue[winner],
				link_id_map & urgent) &&
			atbm_queue_get_num_queued(priv,
				&hw_priv->tx_queue[hw_priv->tx_burst_idx],
				link_id_map))
		winner = hw_priv->tx_burst_idx;

	return winner;
}

static int wsm_get_tx_queue_and_mask(struct wsm_tx_encap *wsm_encap)
{
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(wsm_encap->priv);
	int idx;
	u32 tx_allowed_mask;
	int total = 0;

	/* Search for a queue with multicast frames buffered */
	if (wsm_encap->priv->tx_multicast) {
		tx_allowed_mask = BIT(atbm_dtim_virtual_linkid());
		idx = atbm_get_prio_queue(wsm_encap->priv,
				tx_allowed_mask, &total);
		if (idx >= 0) {
			wsm_encap->more = total > 1;
			goto found;
		}
	}

	/* Search for unicast traffic */
	tx_allowed_mask = ~wsm_encap->priv->sta_asleep_mask;
	tx_allowed_mask |= BIT(atbm_uapsd_virtual_linkid());
	if (wsm_encap->priv->sta_asleep_mask) {
		tx_allowed_mask |= wsm_encap->priv->pspoll_mask;
		tx_allowed_mask &= ~BIT(atbm_dtim_virtual_linkid());
	} else {
		tx_allowed_mask |= BIT(atbm_dtim_virtual_linkid());
	}
	idx = atbm_get_prio_queue(wsm_encap->priv,
			tx_allowed_mask, &total);
	if (idx < 0)
		return -ENOENT;

found:
	wsm_encap->queue = &hw_priv->tx_queue[idx];
	wsm_encap->tx_allowed_mask = tx_allowed_mask;
	return 0;
}


int wsm_get_tx(struct atbm_common *hw_priv, struct hif_tx_encap *encap)
{
	struct wsm_tx_encap wsm_encap;
	struct wsm_tx* wsm = NULL;
	int queue_num;
	/*
	 * Count was intended as an input for wsm->more flag.
	 * During implementation it was found that wsm->more
	 * is not usable, see details above. It is kept just
	 * in case you would like to try to implement it again.
	 */
	int count = 0;
	int if_pending = 1;
	
	/* More is used only for broadcasts. */
#ifndef USB_CMD_UES_EP0
	if (hw_priv->wsm_cmd.ptr) {
		++count;
		spin_lock_bh(&hw_priv->wsm_cmd.lock);
		BUG_ON(!hw_priv->wsm_cmd.ptr);
		memcpy(encap->data, hw_priv->wsm_cmd.ptr, hw_priv->wsm_cmd.len);
		encap->tx_len = hw_priv->wsm_cmd.len;
		encap->burst = 1;
		hw_priv->wsm_cmd.last_send_cmd = hw_priv->wsm_cmd.cmd;
		if(unlikely(hw_priv->wsm_cmd.cnf == 0)){
			encap->source = NULL;
			hw_priv->wsm_cmd.ret = 0;
			hw_priv->wsm_cmd.done = 1;
			hw_priv->wsm_cmd.cmd = 0xFFFF;
			hw_priv->wsm_cmd.last_send_cmd = 0xFFFF;
			wake_up(&hw_priv->wsm_cmd_wq);
		}else {
			encap->source = hw_priv->wsm_cmd.ptr;
			wsm_alloc_tx_buffer(hw_priv);
		}
		hw_priv->wsm_cmd.ptr = NULL;
		spin_unlock_bh(&hw_priv->wsm_cmd.lock);
		return count;
	} else
#endif //USB_CMD_UES_EP0
	{		
		memset(&wsm_encap,0,sizeof(wsm_encap));
		for (;;) {
			int ret;
			struct atbm_vif *priv;
			if (atomic_add_return(0, &hw_priv->tx_lock))
			{
				break;
			}
			/* Keep one buffer reserved for commands. Note
			   that, hw_bufs_used has already been incremented
			   before reaching here. */
			
			priv = wsm_get_interface_for_tx(hw_priv);
			/* go to next interface ID to select next packet */
			hw_priv->if_id_selected ^= 1;

			/* There might be no interface before add_interface
			 * call */
			if (!priv) {
				if (if_pending) {
					if_pending = 0;
					continue;
				}
				break;
			}
			wsm_encap.priv = priv;
			/* This can be removed probably: atbm_vif will not
			 * be in hw_priv->vif_list (as returned from
			 * wsm_get_interface_for_tx) until it's fully
			 * enabled, so statement above will take case of that*/
			if (
				!atomic_read(&priv->enabled)
				) {
				atbm_priv_vif_list_read_unlock(&priv->vif_lock);
				if (if_pending) {
					if_pending = 0;
					continue;
				}
				break;
			}

			/* TODO:COMBO: Find the next interface for which
			* packet needs to be found */
			spin_lock_bh(&priv->ps_state_lock);
			ret = wsm_get_tx_queue_and_mask(&wsm_encap);
			queue_num = wsm_encap.queue - hw_priv->tx_queue;

			if (priv->buffered_multicasts &&
					(ret || !wsm_encap.more) &&
					(priv->tx_multicast ||
					 !priv->sta_asleep_mask)) {
				priv->buffered_multicasts = false;
				if (priv->tx_multicast) {
					priv->tx_multicast = false;
					atbm_hw_priv_queue_work(hw_priv,
						&priv->multicast_stop_work);
				}
			}

			spin_unlock_bh(&priv->ps_state_lock);

			if (ret) {
				atbm_priv_vif_list_read_unlock(&priv->vif_lock);
				if (if_pending == 1) {
					if_pending = 0;
					continue;
				}
				break;
			}
			
			if (atbm_queue_get(&wsm_encap)) {
				atbm_priv_vif_list_read_unlock(&priv->vif_lock);
				if_pending = 0;
				continue;
			}
			
			wsm_encap.data = encap->data;
			wsm_encap.source = &encap->source;
			
			if (wsm_handle_tx_data(&wsm_encap)) {
				atbm_priv_vif_list_read_unlock(&priv->vif_lock);
				if_pending = 0;
				continue;  /* Handled by WSM */
			}
			
			wsm = (struct wsm_tx*)encap->data;
			
			wsm->hdr.id &= __cpu_to_le16(
					~WSM_TX_IF_ID(WSM_TX_IF_ID_MAX));

			if (wsm_encap.txpriv->offchannel_if_id)
				wsm->hdr.id |= cpu_to_le16(
					WSM_TX_IF_ID(wsm_encap.txpriv->offchannel_if_id));
			else
				wsm->hdr.id |= cpu_to_le16(
					WSM_TX_IF_ID(priv->if_id));

			priv->pspoll_mask &= ~BIT(wsm_encap.txpriv->raw_link_id);

			encap->tx_len = __le16_to_cpu(wsm->hdr.len);

			/* allow bursting if txop is set */
			if (priv->edca.params[queue_num].txOpLimit)
				encap->burst = min(encap->burst,
					(int)atbm_queue_get_num_queued(priv,
						wsm_encap.queue, wsm_encap.tx_allowed_mask) + 1);
			else
				encap->burst = 1;

			/* store index of bursting queue */
			if (encap->burst > 1)
				hw_priv->tx_burst_idx = queue_num;
			else
				hw_priv->tx_burst_idx = -1;

			if (wsm_encap.more) {
				struct ieee80211_hdr *hdr =
					(struct ieee80211_hdr *)
					&((u8*)wsm)[sizeof(struct wsm_tx)];
				/* more buffered multicast/broadcast frames
				*  ==> set MoreData flag in IEEE 802.11 header
				*  to inform PS STAs */
				hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_MOREDATA);
			}
			wsm_printk( "[WSM] >>> 0x%.4X (%d) %p %c\n",
				0x0004, encap->tx_len, encap->data,
				wsm->more ? 'M' : ' ');
			++count;			
			atbm_priv_vif_list_read_unlock(&priv->vif_lock);
			break;
		}
	}

	return count;
}

int wsm_txed(struct atbm_common *hw_priv, u8 *data)
{
	if (data == hw_priv->wsm_cmd.ptr) {
		spin_lock_bh(&hw_priv->wsm_cmd.lock);
		hw_priv->wsm_cmd.last_send_cmd = hw_priv->wsm_cmd.cmd;
		hw_priv->wsm_cmd.ptr = NULL;
		spin_unlock_bh(&hw_priv->wsm_cmd.lock);;
		return  1;
	}
	return 0;
}

/* ******************************************************************** */
/* WSM buffer*/
#ifdef USB_BUS_BUG
#define MAX_WSM_BUF_LEN (1632)//
#else
#define MAX_WSM_BUF_LEN (528)//
#endif
void wsm_buf_init(struct wsm_buf *buf)
{
	BUG_ON(buf->begin);
	buf->begin = atbm_kmalloc(/*SDIO_BLOCK_SIZE*/ MAX_WSM_BUF_LEN, GFP_KERNEL | GFP_DMA);
	buf->end = buf->begin ? &buf->begin[MAX_WSM_BUF_LEN] : buf->begin;
	wsm_buf_reset(buf);
	//printk("%s hw_priv->wsm_cmd_buf begin 0x%x.data 0x%x \n",__func__,buf->begin,buf->data);

}

void wsm_buf_deinit(struct wsm_buf *buf)
{
	if(buf->begin)
		atbm_kfree(buf->begin);
	buf->begin = buf->data = buf->end = NULL;
}

static void wsm_buf_reset(struct wsm_buf *buf)
{
	if (buf->begin) {
//#ifdef USB_BUS
#ifdef CIPHER_HIF_COMBINED
      buf->data = buf->begin + sizeof(struct wsm_hdr_tx);
      memset(buf->begin + offsetof(struct wsm_hdr_tx, enc_hdr), 0, sizeof(struct hw_enc_hdr));
#else
	  buf->data = &buf->begin[sizeof(struct wsm_hdr_tx)];
#endif
//#else
//		buf->data = &buf->begin[4];
//#endif
		*(u32 *)buf->begin = 0;
	}
	else
		buf->data = buf->begin;
}

static int wsm_buf_reserve(struct wsm_buf *buf, size_t extra_size)
{
	size_t pos = buf->data - buf->begin;
	size_t size = pos + extra_size;

	if (size & (SDIO_BLOCK_SIZE - 1)) {
		size &= SDIO_BLOCK_SIZE;
		size += SDIO_BLOCK_SIZE;
	}

	buf->begin = atbm_krealloc(buf->begin, size, GFP_KERNEL | GFP_DMA);
	if (buf->begin) {
		buf->data = &buf->begin[pos];
		buf->end = &buf->begin[size];
		return 0;
	} else {
		buf->end = buf->data = buf->begin;
		return -ENOMEM;
	}
}

static struct atbm_vif
	*wsm_get_interface_for_tx(struct atbm_common *hw_priv)
{
	struct atbm_vif *priv = NULL;
	int i = hw_priv->if_id_selected;
	struct ieee80211_vif  *i_priv = NULL;
	atbm_hw_vif_read_lock(&hw_priv->vif_list_lock);
	i_priv = ATBM_HW_VIF_GET(hw_priv->vif_list[i]);

	if(i_priv)
		priv = ABwifi_get_vif_from_ieee80211(i_priv);

	if(!priv)
		atbm_hw_vif_read_unlock(&hw_priv->vif_list_lock);
	/* TODO:COMBO:
	* Find next interface based on TX bitmap announced by the FW
	* Find next interface based on load balancing */

	return priv;
}

static inline int get_interface_id_scanning(struct atbm_common *hw_priv)
{
	//fix passive scan bug
	if (hw_priv->scan.req)
		return hw_priv->scan.if_id;
	else
		return -1;
}

int wsm_read_shmem(struct atbm_common *hw_priv, u32 address, void *buffer,
			size_t buf_size)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	u16 flags = 0;//0x80|0x40;
	struct wsm_shmem_arg_s wsm_shmem_arg = {
		.buf = buffer,
		.buf_size = buf_size,
	};

	wsm_cmd_lock(hw_priv);

	WSM_PUT32(buf, address);
	WSM_PUT16(buf, buf_size);
	WSM_PUT16(buf, flags);
	ret = wsm_cmd_send(hw_priv, buf, &wsm_shmem_arg, 0x0000, WSM_CMD_TIMEOUT,
				0);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}
#define HI_STATUS_SUCCESS (0)

int wsm_read_shmem_confirm(struct atbm_common *hw_priv,
				struct wsm_shmem_arg_s *arg, struct wsm_buf *buf)
{
	u8 *ret_buf = arg->buf;

	if (WARN_ON(WSM_GET32(buf) != HI_STATUS_SUCCESS))
		return -EINVAL;

	WSM_GET(buf, ret_buf, arg->buf_size);

	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}

int wsm_write_shmem(struct atbm_common *hw_priv, u32 address,size_t size,
						void *buffer)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	u16 flags = 0;//0x80|0x40;
	struct wsm_shmem_arg_s wsm_shmem_arg = {
		.buf = buffer,
		.buf_size = size,
	};

	wsm_cmd_lock(hw_priv);

	WSM_PUT32(buf, address);
	WSM_PUT16(buf, size);
	WSM_PUT16(buf, flags);
	WSM_PUT(buf, buffer, size);

	ret = wsm_cmd_send(hw_priv, buf, &wsm_shmem_arg, 0x0001, WSM_CMD_TIMEOUT,
				0);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}


int wsm_write_shmem_confirm(struct atbm_common *hw_priv,
				struct wsm_shmem_arg_s *arg, struct wsm_buf *buf)
{
	if (WARN_ON(WSM_GET32(buf) != HI_STATUS_SUCCESS))
		return -EINVAL;
	return 0;

underflow:
	WARN_ON(1);
	return -EINVAL;
}
/*
#define WSM_RX_BA_SESSION_REQ_ID 0x0014
#define WSM_RX_BA_SESSION_RESP_ID       0x0414
struct wsm_rx_ba_session{
        u8 mode;
        u8 win_size;
        u8 tid;
        u8 resv;
        u8 TA[6]
        u16 ssn;
        u16 timeout;
        u8 hw_token;
        u8 resv2;
};
*/
int wsm_req_rx_ba_session(struct atbm_common *hw_priv,
                                        struct wsm_rx_ba_session *arg,int if_id)
{
        int ret;
        struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

        wsm_cmd_lock(hw_priv);
        WSM_PUT8(buf,arg->mode);
        WSM_PUT8(buf,arg->win_size);
        WSM_PUT8(buf,arg->tid);
        WSM_PUT8(buf,arg->resv);
        WSM_PUT(buf,arg->TA,6);
        WSM_PUT16(buf,arg->ssn);
        WSM_PUT16(buf,arg->timeout);
        WSM_PUT8(buf,arg->hw_token);
        WSM_PUT8(buf,arg->resv2);

        ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_RX_BA_SESSION_REQ_ID, WSM_CMD_TIMEOUT,
                        if_id);

        wsm_cmd_unlock(hw_priv);
        return ret;

nomem:
        wsm_cmd_unlock(hw_priv);
        return -ENOMEM;
}
int wsm_set_chantype_func(struct atbm_common *hw_priv,
				    struct wsm_set_chantype *arg,int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	if (unlikely(arg->band > 1))
		return -EINVAL;
	wsm_oper_lock(hw_priv);
	wsm_cmd_lock(hw_priv);
	WSM_PUT8(buf, arg->band);
	WSM_PUT8(buf, arg->flag);
	WSM_PUT16(buf, arg->channelNumber);
	WSM_PUT32(buf, arg->channelType);
	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_SET_CHANTYPE_ID, WSM_CMD_TIMEOUT,
			if_id);
	wsm_cmd_unlock(hw_priv);
	wsm_oper_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	wsm_oper_unlock(hw_priv);
	return -ENOMEM;
}
#if 0
int wsm_test_cmd(struct atbm_common *hw_priv,u8 *buffer,int size)
{
	int ret;
	struct wsm_shmem_arg_s wsm_shmem_arg = {
		.buf = buffer,
		.buf_size = size,
	};
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	wsm_cmd_lock(hw_priv);
	WSM_PUT(buf, buffer, size);
	ret = wsm_cmd_send(hw_priv, buf, &wsm_shmem_arg, 0xe, WSM_CMD_TIMEOUT, 0);
	wsm_cmd_unlock(hw_priv);
	return 0;
nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}
#endif
/*
	@name: wsm_efuse_change_data_cmd
	@param: arg		efuse data
	@returns:	0,											success
				LMC_STATUS_CODE__EFUSE_VERSION_CHANGE	failed because efuse version change  
				LMC_STATUS_CODE__EFUSE_FIRST_WRITE,		failed because efuse by first write   
				LMC_STATUS_CODE__EFUSE_PARSE_FAILED,  		failed because efuse data wrong, cannot be parase
				LMC_STATUS_CODE__EFUSE_FULL,				failed because efuse have be writen full
				
	@description: this function proccesses change efuse data to chip
*/
int wsm_efuse_change_data_cmd(struct atbm_common *hw_priv, const struct efuse_headr *arg,
		int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	u16 cmd = WSM_HI_EFUSE_CHANGE_DATA_REQ_ID;

	wsm_cmd_lock(hw_priv);

	WSM_PUT8(buf, arg->specific);
	WSM_PUT8(buf, arg->version);
	WSM_PUT8(buf, arg->dcxo_trim);
	WSM_PUT8(buf, arg->delta_gain1);
	WSM_PUT8(buf, arg->delta_gain2);
	WSM_PUT8(buf, arg->delta_gain3);
	WSM_PUT8(buf, arg->Tj_room);
	WSM_PUT8(buf, arg->topref_ctrl_bias_res_trim);
	WSM_PUT8(buf, arg->PowerSupplySel);
	WSM_PUT(buf, &arg->mac[0], sizeof(arg->mac));

	ret = wsm_cmd_send(hw_priv, buf, NULL, cmd, WSM_CMD_TIMEOUT, if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

int wsm_efuse_change_data_confirm(struct atbm_common *hw_priv, struct wsm_buf *buf)
{
	u32 status = 0;
	status = WSM_GET32(buf);	
	return status;
underflow:	
    WARN_ON(1);
    return -EINVAL;
}


#ifdef CONFIG_ATBM_HE

int wsm_send_obss_color_collision_cfg_cmd(struct atbm_common *hw_priv,int if_id,u8 color,u8 enable)
{
	return 0;
}

#if 0
int wsm_set_muedca_params(struct atbm_common *hw_priv,const struct wsm_edca_params *arg,int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	wsm_cmd_lock(hw_priv);

	/* Implemented according to specification. */

	WSM_PUT(buf, &arg->mu_params[0],sizeof(struct wsm_muedca_queue_params)*4);

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_MUEDCA_PARAMS_REQ_ID, WSM_CMD_TIMEOUT, if_id);
	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}
#endif  //0

#endif //#ifdef CONFIG_ATBM_HE

#ifdef CONFIG_ATBM_BLE_ADV_COEXIST
int wsm_ble_msg_coexist_start(struct atbm_common *hw_priv, const struct wsm_ble_msg_coex_start *arg,
										int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	if (unlikely(arg->ble_id > BLE_MSG_MAX_ID))
		return -EINVAL;

	wsm_cmd_lock(hw_priv);
	
	WSM_PUT32(buf, arg->status);
	WSM_PUT8(buf, arg->ble_id);
	WSM_PUT8(buf, arg->reserved[0]);
	WSM_PUT8(buf, arg->reserved[1]);
	WSM_PUT8(buf, arg->reserved[2]);
	WSM_PUT32(buf, arg->interval);
	WSM_PUT32(buf, arg->coex_flag);
	WSM_PUT32(buf, arg->scan_win);
	WSM_PUT32(buf, arg->chan_flag);

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_BLE_MSG_REQ_ID, WSM_CMD_TIMEOUT, if_id);
	
	wsm_cmd_unlock(hw_priv);
	return ret;
	
nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

int wsm_ble_msg_coexist_stop(struct atbm_common *hw_priv, const struct wsm_ble_msg *arg,
										int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	if (unlikely(arg->ble_id > BLE_MSG_MAX_ID))
		return -EINVAL;

	wsm_cmd_lock(hw_priv);
	
	WSM_PUT32(buf, arg->status);
	WSM_PUT8(buf, arg->ble_id);
	WSM_PUT8(buf, arg->reserved[0]);
	WSM_PUT8(buf, arg->reserved[1]);
	WSM_PUT8(buf, arg->reserved[2]);

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_BLE_MSG_REQ_ID, WSM_CMD_TIMEOUT, if_id);
	
	wsm_cmd_unlock(hw_priv);
	return ret;
	
nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;

}

int wsm_ble_msg_set_adv_data(struct atbm_common *hw_priv, const struct wsm_ble_msg_adv_data *arg,
										int if_id)
{
	int ret, i;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;

	if (unlikely(arg->ble_id > BLE_MSG_MAX_ID))
		return -EINVAL;

	wsm_cmd_lock(hw_priv);
	
	WSM_PUT32(buf, arg->status);
	WSM_PUT8(buf, arg->ble_id);
	WSM_PUT8(buf, arg->reserved[0]);
	WSM_PUT8(buf, arg->reserved[1]);
	WSM_PUT8(buf, arg->reserved[2]);

	for(i=0; i<6; i++){
		WSM_PUT8(buf, arg->mac[i]);
	}
	
	WSM_PUT8(buf, arg->adv_data_len);
	
	for(i=0; i<31; i++){
		WSM_PUT8(buf, arg->adv_data[i]);
	}	

	ret = wsm_cmd_send(hw_priv, buf, NULL, WSM_BLE_MSG_REQ_ID, WSM_CMD_TIMEOUT, if_id);
	
	wsm_cmd_unlock(hw_priv);
	return ret;
	
nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;

}

int wsm_ble_indication(struct atbm_common *hw_priv, struct wsm_buf *buf, int if_id)
{
	u8 bleId;
	u8 reserved;
	struct wsm_ble_rpt *ble_rpt;

	bleId = WSM_GET8(buf);
	reserved = WSM_GET8(buf);
	reserved = WSM_GET8(buf);
	reserved = WSM_GET8(buf);

	switch(bleId){
		case WSM_BLE_IND_ADV_RPT:
			ble_rpt = (struct wsm_ble_rpt *)buf->data;
			atbm_ioctl_ble_adv_rpt_async(hw_priv->hw,(u8 *)ble_rpt, sizeof(struct wsm_ble_adv_rpt));
			break;
		case WSM_BLE_IND_CONN_RPT:
			ble_rpt = (struct wsm_ble_rpt *)buf->data;
			atbm_ioctl_ble_conn_rpt_async(hw_priv->hw,(u8 *)ble_rpt, sizeof(struct wsm_ble_rpt));			
			break;
		default:
			break;
	}

underflow:
	return 0;
}

#endif
#ifdef CONFIG_ATBM_BLE
int wsm_ble_xmit(struct atbm_common *hw_priv,u8 *xmit,size_t len)
{
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	int ret;
	//printk("HI_MSG_ID_BLE_BASE %d\n",len);
	wsm_cmd_lock(hw_priv);
	WSM_PUT(buf,xmit,len);
	ret = wsm_cmd_send(hw_priv,buf,NULL,HI_MSG_ID_BLE_BASE,WSM_CMD_DONOT_CONFIRM_TIMEOUT,3);
	wsm_cmd_unlock(hw_priv);
	return 0;
nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}
#endif

#ifdef CONFIG_ATBM_SUPPORT_CSA

int wsm_csa_req(struct atbm_common *hw_priv, const struct wsm_csa *arg,
		int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	u16 cmd = WSM_CSA_REQ_ID;

	wsm_cmd_lock(hw_priv);

	WSM_PUT8(buf, arg->flags);
	WSM_PUT8(buf, arg->csa_mode);
	WSM_PUT8(buf, arg->csa_channel);
	WSM_PUT8(buf, arg->csa_count);

	ret = wsm_cmd_send(hw_priv, buf, NULL, cmd, WSM_CMD_TIMEOUT, if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

int wsm_csa_concurrent_req(struct atbm_common *hw_priv, const struct wsm_csa_concurrent *arg,
		int if_id)
{
	int ret;
	struct wsm_buf *buf = &hw_priv->wsm_cmd_buf;
	u16 cmd = WSM_CSA_REQ_CONCURRENT_ID;

	wsm_cmd_lock(hw_priv);

	WSM_PUT8(buf,  arg->vifs[0].if_id);
	WSM_PUT8(buf,  arg->vifs[0].chantyep);
	WSM_PUT16(buf, arg->vifs[0].channel);

	
	WSM_PUT8(buf,  arg->vifs[1].if_id);
	WSM_PUT8(buf,  arg->vifs[1].chantyep);
	WSM_PUT16(buf, arg->vifs[1].channel);
	ret = wsm_cmd_send(hw_priv, buf, NULL, cmd, WSM_CMD_TIMEOUT, if_id);

	wsm_cmd_unlock(hw_priv);
	return ret;

nomem:
	wsm_cmd_unlock(hw_priv);
	return -ENOMEM;
}

#endif
