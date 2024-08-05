/*
 * Datapath implementation for altobeam APOLLO mac80211 drivers
 * *
 * Copyright (c) 2016, altobeam
 * Author:
 *
 *Based on apollo code
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <net/atbm_mac80211.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <net/ip.h>
#include <linux/bitops.h>
#include "apollo.h"
#include "wsm.h"
#include "bh.h"
#include "ap.h"
#include "debug.h"
#include "sta.h"
#include "sbus.h"
#include "atbm_p2p.h"
#include "mac80211/ieee80211_i.h"


#if defined(CONFIG_ATBM_APOLLO_TX_POLICY_DEBUG)
#define tx_policy_printk(...) atbm_printk_always(__VA_ARGS__)
#else
#define tx_policy_printk(...)
#endif

#define ATBM_APOLLO_INVALID_RATE_ID (0xFF)

#ifdef CONFIG_ATBM_APOLLO_TESTMODE
#include "atbm_testmode.h"
#endif /* CONFIG_ATBM_APOLLO_TESTMODE */

int he_loops = 0;
module_param(he_loops,int,0);
/* ******************************************************************** */
/* TX policy cache implementation					*/
//extern int g_connetting;
extern int tx_longpreamble;
extern int he_dcm ;
extern int he_ltf_gi; 
extern int he_ldpc ;
extern int he_not_40M ;
//extern int he_ER_106tone ;
extern int he_pad8us;
extern int ht_sgi;
extern int ht_ldpc;
extern int he_set_mcs_2x_p16;


/* ******************************************************************** */
/* apollo TX implementation						*/

struct atbm_txinfo {
	struct sk_buff *skb;
	unsigned queue;
	struct ieee80211_tx_info *tx_info;
	struct atbm_sta_priv *sta_priv;
	struct atbm_txpriv txpriv;
};

u32 atbm_rate_mask_to_wsm(struct atbm_common *hw_priv, u32 rates)
{
	u32 ret = 0;
	int i;
	struct ieee80211_rate * bitrates =
		hw_priv->hw->wiphy->bands[hw_priv->channel->band]->bitrates;
	for (i = 0; i < 32; ++i) {
		if (rates & BIT(i))
			ret |= BIT(bitrates[i].hw_value);
	}
	return ret;
}
static int
atbm_tx_h_calc_link_ids(struct atbm_vif *priv,
			  struct atbm_txinfo *t)
{
#ifdef CONFIG_ATBM_SUPPORT_P2P
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	if ((t->tx_info->flags & IEEE80211_TX_CTL_TX_OFFCHAN) ||
			(hw_priv->roc_if_id == priv->if_id))
		t->txpriv.offchannel_if_id = 2;
	else
		t->txpriv.offchannel_if_id = 0;
#endif
#ifdef CONFIG_ATBM_STA_LISTEN
	if(priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA_LISTEN){
		t->txpriv.raw_link_id = 0;
		t->txpriv.link_id = 0;
	}else
#endif
	if(priv->join_status == ATBM_APOLLO_JOIN_STATUS_SIMPLE_MONITOR){
		t->txpriv.raw_link_id = 0;
		t->txpriv.link_id = 0;
	}
	else if (likely(t->tx_info->control.sta && t->sta_priv->link_id))
		t->txpriv.raw_link_id = t->txpriv.link_id = t->sta_priv->link_id;
	else if (priv->mode != NL80211_IFTYPE_AP)
		t->txpriv.raw_link_id = t->txpriv.link_id = 0;
	else if (!(t->tx_info->flags & IEEE80211_TX_UCAST)) {
		if (priv->enable_beacon) {
			t->txpriv.raw_link_id = 0;
			t->txpriv.link_id = atbm_dtim_virtual_linkid();
		} else {
			t->txpriv.raw_link_id = 0;
			t->txpriv.link_id = 0;
		}
	}
	else {
		if (t->tx_info->flags & IEEE80211_TX_CTL_8023) {
			atbm_printk_err("No more link IDs available.\n");
			return -ENOENT;
		}
		if (ieee80211_is_mgmt(((struct atbm_ieee80211_mgmt *)t->skb->data)->frame_control)) {
			t->txpriv.link_id = 0;
		}
		else {
			atbm_printk_err("No more link IDs available.\n");
			return -ENOENT;
		}
		t->txpriv.raw_link_id = t->txpriv.link_id;
	}

	if (t->tx_info->control.sta &&
			(t->tx_info->control.sta->uapsd_queues & BIT(t->queue)))
		t->txpriv.link_id = atbm_uapsd_virtual_linkid();
	return 0;
}
#ifdef CONFIG_ATBM_BT_COMB
/* BT Coex specific handling */
static void
atbm_tx_h_bt(struct atbm_vif *priv,
	       struct atbm_txinfo *t,
	       struct wsm_tx *wsm)
{
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);

	u8 priority = 0;

	if (!hw_priv->is_BT_Present)
		return;

	if (unlikely(ieee80211_is_nullfunc(t->hdr->frame_control)))
		priority = WSM_EPTA_PRIORITY_MGT;
	else if (ieee80211_is_data(t->hdr->frame_control)) {
		/* Skip LLC SNAP header (+6) */
		u8 *payload = &t->skb->data[t->hdrlen];
		u16 *ethertype = (u16 *) &payload[6];
		if (unlikely(*ethertype == __be16_to_cpu(ETH_P_PAE)))
			priority = WSM_EPTA_PRIORITY_EAPOL;
	} else if (unlikely(ieee80211_is_assoc_req(t->hdr->frame_control) ||
		ieee80211_is_reassoc_req(t->hdr->frame_control))) {
		struct atbm_ieee80211_mgmt *mgt_frame =
				(struct atbm_ieee80211_mgmt *)t->hdr;

		if (mgt_frame->u.assoc_req.listen_interval <
						priv->listen_interval) {
			txrx_printk(
				"Modified Listen Interval to %d from %d\n",
				priv->listen_interval,
				mgt_frame->u.assoc_req.listen_interval);
			/* Replace listen interval derieved from
			 * the one read from SDD */
			mgt_frame->u.assoc_req.listen_interval =
				priv->listen_interval;
		}
	}

	if (likely(!priority)) {
		if (ieee80211_is_action(t->hdr->frame_control))
			priority = WSM_EPTA_PRIORITY_ACTION;
		else if (ieee80211_is_mgmt(t->hdr->frame_control))
			priority = WSM_EPTA_PRIORITY_MGT;
		else if ((wsm->queueId == WSM_QUEUE_VOICE))
			priority = WSM_EPTA_PRIORITY_VOICE;
		else if ((wsm->queueId == WSM_QUEUE_VIDEO))
			priority = WSM_EPTA_PRIORITY_VIDEO;
		else
			priority = WSM_EPTA_PRIORITY_DATA;
	}

	txrx_printk( "[TX] EPTA priority %d.\n",
		priority);

	wsm->flags |= priority << 1;
}
#endif
#if defined(CONFIG_NL80211_TESTMODE) || defined(CONFIG_ATBM_IOCTRL)

extern int atbm_tool_shortGi;
#endif
/* ******************************************************************** */
static void atbm_tx_hif_xmit(struct atbm_common *hw_priv)
{
	if(!hw_priv->sbus_ops->sbus_data_write){
		atbm_bh_wakeup(hw_priv);
	}else {
		hw_priv->sbus_ops->sbus_data_write(hw_priv->sbus_priv);
	}
}
void atbm_build_wsm_hdr(struct wsm_tx_encap *wsm_encap)
{
	struct wsm_tx* wsm = (struct wsm_tx*)wsm_encap->data;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(wsm_encap->skb);
	u8 align_size = (info->flags & IEEE80211_TX_2BYTTES_OFF) ? 2 : 0;
#ifdef USB_BUS_BUG
	wsm->hdr.u.usb_hdr.usb_id = __cpu_to_le16(WSM_TRANSMIT_REQ_MSG_ID);
#endif
	wsm->hdr.len = __cpu_to_le16(wsm_encap->len + sizeof(struct wsm_tx));
	wsm->hdr.id = __cpu_to_le16(WSM_TRANSMIT_REQ_MSG_ID);
#ifdef CIPHER_HIF_COMBINED
	wsm->hdr.enc_hdr.EncFlags = __cpu_to_le32((((wsm_encap->priv->if_id) << ENC_INTERFACE_OFFSET) & ENC_INTERFACE_MASK));
	if (info->control.hw_key) {
		wsm->hdr.enc_hdr.EncFlags |= __cpu_to_le32(ENC_CMD_ENCRYPT | ENC_ENABLE_11W);
		if (info->control.hw_key->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
			atbm_printk_debug("11w WLAN_CIPHER_SUITE_AES_CMAC\n");
			wsm->hdr.enc_hdr.EncFlags |= __cpu_to_le32(ENC_FORCE_PROTECT_MGMT);
		}
	}
	wsm->hdr.enc_hdr.EncFlags |= __cpu_to_le32(((((u8*)wsm + sizeof(struct wsm_tx) - (u8*)(&wsm->hdr.enc_hdr.EncFlags))) << MAC_HEADER_OFFSET_OFFSET) & MAC_HEADER_OFFSET_MASK);
#ifdef ATBM_ENC_CHECKSUM_TEST
	wsm->hdr.enc_hdr.EncFlags |= __cpu_to_le32((CHKSUM_ENABLE | CHKSUM_ENABLE_OPT));
#endif
	wsm->hdr.u.common.mark = __cpu_to_le16(0xE569);
	wsm->hdr.u.common.flag |= __cpu_to_le16((wsm_encap->len - align_size)&ENC_DATA_LEN_MASK);
	wsm->hdr.u.common.total_len = wsm->hdr.len;
	if(align_size){
		wsm->hdr.u.common.flag |= __cpu_to_le16(HW_INSERT_DUMMY);
		wsm->flags |= WSM_TX_2BYTES_SHIFT;
	}
#endif
	wsm->queueId = (wsm_encap->txpriv->raw_link_id << 2) | wsm_queue_id_to_wsm(skb_get_queue_mapping(wsm_encap->skb));
	wsm->packetID = wsm_encap->packetID;
	wsm_encap->len = wsm_encap->len + sizeof(struct wsm_tx);
	atbm_printk_debug("[BuildWsm][%x]\n", wsm->hdr.enc_hdr.EncFlags);
}
extern int ampdu;
void atbm_build_rate_policy(struct wsm_tx_encap *wsm_encap)
{	
	struct wsm_tx* wsm = (struct wsm_tx*)wsm_encap->data;
	struct ieee80211_tx_info* tx_info = IEEE80211_SKB_CB(wsm_encap->skb);
	static u32 lasthtTxParameters = 0;
	static u32 lastheTxParameters = 0;
	static u32 lasRateSets = 0;
	static u8  lasMaxRate = 0;
	struct atbm_common *hw_priv = wsm_encap->priv->hw_priv;

	if(hw_priv->chip_version == CRONUS_NO_HT40_LDPC){
		ht_ldpc = 0;
		he_ldpc = 0;
		he_dcm = 0;
	}
	wsm->flags |= 0 << 4;

	wsm->maxTxRate   = tx_info->control.tx_max_rate;	
	wsm->txRateSets  = __cpu_to_le32(tx_info->control.tx_rate_sets);
   		
	wsm->htTxParameters &= ~(__cpu_to_le32(WSM_NEED_TX_CONFIRM));
	if (tx_info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS) {
		wsm->htTxParameters |= __cpu_to_le32(WSM_NEED_TX_CONFIRM);
	}
#ifdef CONFIG_TX_NO_CONFIRM_DEBUG
	wsm->htTxParameters |= __cpu_to_le32(WSM_NEED_TX_CONFIRM);
	tx_info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
#endif
	//no confirm required packet, set as rate co
	if (!(wsm->htTxParameters&__cpu_to_le32(WSM_NEED_TX_CONFIRM))){
		if(tx_info->tx_update_rate){
			wsm->htTxParameters |=__cpu_to_le32(WSM_NEED_TX_CONFIRM);
			wsm->htTxParameters |=__cpu_to_le32(WSM_NEED_TX_RATE_CONFIRM);
			atbm_printk_rc(">>>>hmac_max_rate:%x  wsm->txRateSets :%x  WSM_NEED_TX_RATE_CONFIRM\n", wsm->maxTxRate,  wsm->txRateSets );
		}
	}

  if(((tx_info->control.force_policyid & 0x70) > 0))
  	{
  	    wsm->flags &= 0x8f;  //clear bit[4:6]
		wsm->flags |= (tx_info->control.force_policyid & 0x70) ;  //set bit[4:6]
		//atbm_printk_rc("wsm->flags:%x \n", wsm->flags);
  	}
	
	/*
	*try to set WSM_HT_AGGR_EN and WSM_HT_TX_WIDTH_40M
	*/
	if ((RATE_HT ==  (tx_info->control.tx_rate_sets & RATE_MODE_MASK)) || (RATE_HE ==  (tx_info->control.tx_rate_sets & RATE_MODE_MASK))){
		
		if ((tx_info->flags & IEEE80211_TX_CTL_AMPDU)&& ampdu) {
			wsm->htTxParameters |= __cpu_to_le32(WSM_HT_AGGR_EN);
		}
		if (tx_info->control.sta && ieee80211_sta_support_40w(tx_info->control.sta)) {
			wsm->htTxParameters |= __cpu_to_le32(WSM_HT_TX_WIDTH_40M);
		}
	}
	if (IEEE80211_TX_CTL_ASSIGN_SEQ & tx_info->flags) {
		wsm->htTxParameters |= __cpu_to_le32(WSM_HT_NEED_SEQ);
	}
	
	if(RATE_HT ==  (tx_info->control.tx_rate_sets & RATE_MODE_MASK)) {
		wsm->htTxParameters |=	__cpu_to_le32(WSM_HT_TX_MIXED);
		//ht_sgi:0 long-gi , 1 sgi, 2 auto
		if(ht_sgi == 1){
			wsm->htTxParameters |=	__cpu_to_le32(WSM_HT_TX_SGI);
		}
		else if(ht_sgi == 0){
			wsm->htTxParameters &=	~__cpu_to_le32(WSM_HT_TX_SGI);
		}
		else {
			//auto
		}
		//HT short GI and bandwidth set, MCS7 use short GI, MCS0,1 force to 20M
		if(tx_info->control.tx_rc_flag & IEEE80211_TX_RC_SHORT_GI){	

			if(wsm->htTxParameters &__cpu_to_le32(WSM_HT_TX_WIDTH_40M)){
						if(tx_info->control.tx_rc_flag&IEEE80211_TX_RC_SHORT_GI_40M)
						{
							wsm->htTxParameters |=	__cpu_to_le32(WSM_HT_TX_SGI);
							atbm_printk_rc("HT SHORT_GI_40M\n");
						}
			}else{  //20M
						if(tx_info->control.tx_rc_flag&IEEE80211_TX_RC_SHORT_GI_20M)
						{
							wsm->htTxParameters |=	__cpu_to_le32(WSM_HT_TX_SGI);
							atbm_printk_rc("HT SHORT_GI_20M\n");
						}				
			}
		}
		//ht mcs0 mcs1 may force 20M
		if(tx_info->control.tx_rc_flag & IEEE80211_TX_RC_40M_TO_20M){  
            if(tx_info->control.tx_rc_flag&IEEE80211_TX_RC_SHORT_GI_20M)
            {
                wsm->htTxParameters |= __cpu_to_le32(WSM_HT_TX_SGI);
            }
            else
            {
                  wsm->htTxParameters &=~ __cpu_to_le32(WSM_HT_TX_SGI);
            }
			wsm->htTxParameters  &=	~__cpu_to_le32(WSM_HT_TX_WIDTH_40M);
		}			
		
        // ht_ldpc = 1;
		//0 bcc , 1 ldpc, 2 auto
		if((ht_ldpc==1)||(tx_info->control.tx_rc_flag & IEEE80211_TX_RC_HT_LDPC)){
			wsm->htTxParameters |=	__cpu_to_le32(WSM_TX_HT_LDPC);
		}
		else if(ht_ldpc==0)	{
			wsm->htTxParameters &=	~__cpu_to_le32(WSM_TX_HT_LDPC);
		}
		else {
			//auto
		}
	}else {
	
		if (tx_longpreamble){
			wsm->htTxParameters |=	__cpu_to_le32(WSM_HT_TX_LONG_PREAMBLE);
		}	
	}
	if(RATE_HE <=  (tx_info->control.tx_rate_sets & RATE_MODE_MASK)){
		wsm->heTxParameters  = WSM_TX_HE_HTC;

        //if(he_ltf_gi == 0){
			//he_ltf_gi = ATBM_HE_4X32;
        //}		
		
        

		
		//he_ldpc = 1;

		
		//wsm->heTxParameters  = WSM_TX_HE_HTC
		if(he_ltf_gi==ATBM_HE_4X32){
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_LTF_4X);			
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_GI_32);
		}
		else if(he_ltf_gi==ATBM_HE_4X08){
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_LTF_4X);			
			//wsm->heTxParameters |= WSM_TX_HE_GI_08;
		}
		else if(he_ltf_gi==ATBM_HE_2X16){
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_LTF_2X);			
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_GI_16);
		}
		else if(he_ltf_gi==ATBM_HE_2X08){
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_LTF_2X);			
			//wsm->heTxParameters |= WSM_TX_HE_GI_08;
		}
		else  //not manual forced
		{
			if(tx_info->control.tx_rc_flag & IEEE80211_TX_RC_SHORT_GI){
           		 wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_LTF_2X);		
      	 	 }else{	
			//if set we not support mode 1X, set LTF to default
				if((wsm->heTxParameters &(WSM_TX_HE_LTF_2X|WSM_TX_HE_LTF_4X))==0){
				if(he_set_mcs_2x_p16)
				{
					wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_LTF_2X); 		
					wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_GI_16);				
				}
			    else{
					wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_LTF_4X);			
					wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_GI_32);		
			    }				
					//wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_LDPC);
				}
        	}
		}

        //DCM info from rate control
        if((tx_info->control.tx_rc_flag & IEEE80211_TX_RC_HE_SUPPORT_DCM)){
                wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_SUPPORT_DCM); 
        }        
        if((tx_info->control.tx_rc_flag & IEEE80211_TX_RC_HE_DCM)){
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_DCM_USED);
		}		

        //manual set value:default DCM not set,  0xff
		if(he_dcm == 1)  //manual set DCM on
		{
		   wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_SUPPORT_DCM);
		   wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_DCM_USED);
		}
		if(he_dcm == 0)  //manual set no DCM 
		{
		   wsm->heTxParameters &= ~ __cpu_to_le32(WSM_TX_HE_SUPPORT_DCM);
		   wsm->heTxParameters &= ~ __cpu_to_le32(WSM_TX_HE_DCM_USED);
		}

		
		if((tx_info->control.tx_rc_flag & IEEE80211_TX_RC_HE_LDPC)){
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_LDPC);
		}		
		if((he_ldpc == 0) )  //default he_ldpc == 1, if manual set0, not use LDPC.
	    { 
          
	      wsm->heTxParameters &= ~__cpu_to_le32(WSM_TX_HE_LDPC);
	    	atbm_printk_rc(" heTxParerameters %x\n",wsm->heTxParameters);
	    }

		if((RATE_HE_ER == (wsm->txRateSets & RATE_MODE_MASK))){
			//atbm_printk_rc("heTxParameters not 40M set %x\n",wsm->heTxParameters);
			wsm->htTxParameters &= ~__cpu_to_le32(WSM_HT_TX_WIDTH_40M);
		}

		if(tx_info->control.tx_rc_flag & IEEE80211_TX_RC_40M_TO_20M){		  
			wsm->htTxParameters  &=	~__cpu_to_le32(WSM_HT_TX_WIDTH_40M);
		}			
	/*
		if(he_ER_106tone){	
			atbm_printk_rc("er  heTxParerameters %x\n",wsm->heTxParameters);
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_ER_106);
			wsm->htTxParameters &= ~__cpu_to_le32(WSM_HT_TX_WIDTH_40M);
		}
		*/
		if(he_pad8us)		{
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_NORM_PAD8);
		}
		else  {
			wsm->heTxParameters |= __cpu_to_le32(WSM_TX_HE_NORM_PAD16);
		}

	}
	else {

	}

	  //manual force bandwidth
		if(he_not_40M == 1)
		{
			wsm->htTxParameters |= __cpu_to_le32(WSM_HT_TX_WIDTH_40M);
		}

		if(he_not_40M == 0)
		{
			wsm->htTxParameters &= ~__cpu_to_le32(WSM_HT_TX_WIDTH_40M);
		}



	
	 if(tx_info->tx_update_rate){  //less print
			atbm_printk_rc("htTxParameters: 0x%x heTxParameters 0x%x\n",wsm->htTxParameters, wsm->heTxParameters);
	}

    if((wsm->htTxParameters != lasthtTxParameters)||(lastheTxParameters != wsm->heTxParameters)||(lasMaxRate!= wsm->maxTxRate)||(wsm->txRateSets != lasRateSets))
   	{
		wsm->htTxParameters |= __cpu_to_le32(WSM_TX_RATE_CHANGED);
   	}
	lasthtTxParameters = wsm->htTxParameters;
	lastheTxParameters = wsm->heTxParameters;
	lasRateSets = wsm->txRateSets ; 
	lasMaxRate = tx_info->control.tx_max_rate;

		

}
static void
atbm_tx_h_pm(struct atbm_vif* priv,
	struct atbm_txinfo* t)
{
	if (unlikely(ieee80211_is_auth(((struct atbm_ieee80211_mgmt *)t->skb->data)->frame_control))) {
		u32 mask = ~BIT(t->txpriv.raw_link_id);
		spin_lock_bh(&priv->ps_state_lock);
		priv->sta_asleep_mask &= mask;
		priv->pspoll_mask &= mask;
		spin_unlock_bh(&priv->ps_state_lock);
	}
}
static bool
atbm_tx_h_pm_state(struct atbm_vif *priv,
		     struct atbm_txinfo *t)
{
	int was_buffered = 1;

	if (t->txpriv.link_id == atbm_dtim_virtual_linkid() &&
			!priv->buffered_multicasts) {
		priv->buffered_multicasts = true;
		if (priv->sta_asleep_mask)
			atbm_hw_priv_queue_work(priv->hw_priv,
				&priv->multicast_start_work);
	}

	if (t->txpriv.raw_link_id && t->txpriv.tid < ATBM_APOLLO_MAX_TID){
		was_buffered = priv->link_id_db[t->txpriv.raw_link_id - 1].buffered[t->txpriv.tid]++;

		if(!(priv->sta_asleep_mask & BIT(t->txpriv.raw_link_id))){
			was_buffered = 1;
		}
	}
	
	return !was_buffered;
}
void atbm_tx(struct ieee80211_hw* dev, struct sk_buff* skb)
{
	struct atbm_common* hw_priv = dev->priv;
	struct atbm_txinfo t = {
		.skb = skb,
		.queue = skb_get_queue_mapping(skb),
		.tx_info = IEEE80211_SKB_CB(skb),
		.txpriv.tid = skb->priority & 0x07,
		.txpriv.rate_id = ATBM_APOLLO_INVALID_RATE_ID,
		.txpriv.raw_link_id = 0,
		.txpriv.link_id = 0,
		.txpriv.if_id = -1,
	};
	struct atbm_vif* priv;
	int ret = 0;
	struct ieee80211_sta *sta;
	bool tid_update = 0;
#ifdef CONFIG_ATBM_SUPPORT_P2P
    struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	struct ieee80211_hdr* frame = (struct ieee80211_hdr*)skb->data;
#endif
	
	priv = ABwifi_get_vif_from_ieee80211(t.tx_info->control.vif);
	if (!priv) {
		atbm_printk_err("[TX]:vif is NULL 2\n");
		goto drop;
	}
	
	if (atomic_read(&priv->enabled) == 0) {
		atbm_printk_err("[TX]:vif not enable\n");
		goto drop;
	}
	t.txpriv.if_id = priv->if_id;
	t.sta_priv = (struct atbm_sta_priv*)&t.tx_info->control.sta->drv_priv;

	if (WARN_ON(t.queue >= 4))
		goto drop;

	ret = atbm_tx_h_calc_link_ids(priv, &t);
	if (ret)
		goto drop;
	
	/*
	*not 8023 frame,that frame come from internal or hostapd.
	*/
	if (!(t.tx_info->flags & IEEE80211_TX_CTL_8023)) {
		struct ieee80211_hdr* frame = (struct ieee80211_hdr*)skb->data;
#ifdef CONFIG_PM
		if (ieee80211_is_auth(frame->frame_control)) {
			atbm_printk_pm("[TX]:authen, delay suspend\n");
			atbm_pm_stay_awake(&hw_priv->pm_state, 5 * HZ);
		}
#endif

		if (ieee80211_is_deauth(frame->frame_control)) {
			atbm_printk_pm("[TX]:[%d]:deatuhen[%pM]\n", priv->if_id, ieee80211_get_DA(frame));	
		}
		atbm_tx_h_pm(priv, &t);
	}
#ifdef CONFIG_ATBM_SUPPORT_P2P
#ifdef ATBM_P2P_CHANGE
	atbm_parase_p2p_action_frame(priv,skb,true);
#endif
#endif


	
	sta = rcu_dereference(t.tx_info->control.sta);
	spin_lock_bh(&priv->ps_state_lock);
	{
		tid_update = atbm_tx_h_pm_state(priv, &t);
		BUG_ON(atbm_queue_put(&hw_priv->tx_queue[t.queue],
				t.skb, &t.txpriv));
	}
	spin_unlock_bh(&priv->ps_state_lock);

	if (tid_update && sta)
		ieee80211_sta_set_buffered(sta,
				t.txpriv.tid, true);
	
	atbm_tx_hif_xmit(hw_priv);

	return;

drop:
	atbm_skb_dtor(hw_priv, skb, &t.txpriv);
	return;
}
static int atbm_handle_pspoll(struct atbm_vif *priv,
				struct sk_buff *skb)
{
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	struct ieee80211_sta *sta;
	struct ieee80211_pspoll *pspoll =
		(struct ieee80211_pspoll *) skb->data;
	int link_id = 0;
	u32 pspoll_mask = 0;
	int drop = 1;
	int i;

	if (priv->join_status != ATBM_APOLLO_JOIN_STATUS_AP)
		goto done;
	if (memcmp(priv->vif->addr, pspoll->bssid, ETH_ALEN))
		goto done;

	rcu_read_lock();
	sta = ieee80211_find_sta(priv->vif, pspoll->ta);
	if (sta) {
		struct atbm_sta_priv *sta_priv;
		sta_priv = (struct atbm_sta_priv *)&sta->drv_priv;
		link_id = sta_priv->link_id;
		pspoll_mask = BIT(sta_priv->link_id);
	}
	rcu_read_unlock();
	if (!link_id)
		goto done;

	priv->pspoll_mask |= pspoll_mask;
	drop = 0;

	/* Do not report pspols if data for given link id is
	 * queued already. */
	for (i = 0; i < 4; ++i) {
		if (atbm_queue_get_num_queued(priv,
				&hw_priv->tx_queue[i],
				pspoll_mask)) {
			atbm_bh_wakeup(hw_priv);
			drop = 1;
			break;
		}
	}
	txrx_printk( "[RX] PSPOLL: %s\n", drop ? "local" : "fwd");
done:
	return drop;
}

/* ******************************************************************** */
static void atbm_tx_rate_status(struct atbm_vif *priv,struct wsm_tx_confirm *arg)
{
	struct ieee80211_sta *sta;
	size_t i = 0;
	u16 *rate = (u16*)arg->RateTable;
	
	sta = rcu_dereference(priv->linked_sta[arg->link_id]);

	if(unlikely(sta == NULL)){
		return;
	}
	if(unlikely(sta->txs_retrys == NULL)){
		return;
	}
	if(unlikely(arg->NumOfTxRate != sta->n_rates)){
		return;
	}
	
	for(i = 0;i < (arg->NumOfTxRate * 2 + 4); i++){
		sta->txs_retrys[i] = __le16_to_cpu(rate[i]);
	}
        
    
    
      
	ieee80211_sta_rate_status(sta);
}
void atbm_tx_confirm_cb(struct atbm_common *hw_priv,
			  struct wsm_tx_confirm *arg)
{
	u8 queue_id = atbm_queue_get_queue_id(arg->packetID);
	struct atbm_queue *queue = &hw_priv->tx_queue[queue_id];
	struct sk_buff *skb;
	const struct atbm_txpriv *txpriv;
	struct atbm_vif *priv;
	int tx_count;
	u8 ht_flags;

	txrx_printk( "[TX] TX confirm: %d, %d.\n",
		arg->status, arg->ackFailures);
	if(queue_id >= 4){
		atbm_printk_tx("atbm_tx_confirm_cb %d\n",queue_id);
	}
	if(WARN_ON(!queue)){
		atbm_printk_err("atbm_tx_confirm_cb queue %p\n",queue);
		return;
	}

	priv = ABwifi_hwpriv_to_vifpriv(hw_priv, arg->if_id);
	if (unlikely(!priv)){
		atbm_printk_err("[confirm]:arg_if_id %d\n",arg->if_id);
		return;
	}
	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED)) {
		/* STA is stopped. */
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
		return;
	}

	if (WARN_ON(queue_id >= 4)) {
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
		return;
	}
	
	if (arg->status)
		txrx_printk( "TX failed: %d.\n",
				arg->status);

#ifdef CONFIG_ATBM_APOLLO_TESTMODE
	spin_lock_bh(&hw_priv->tsm_lock);
	if (arg->status)
	{
		hw_priv->atbm_tsm_stats[queue_id].msdu_failed_count++;
		hw_priv->atbm_tsm_stats[queue_id].tid = queue_id;
	}
	hw_priv->atbm_tsm_stats[queue_id].multi_retry_count += arg->ackFailures;

	if ((arg->status == WSM_STATUS_RETRY_EXCEEDED) ||
	    (arg->status == WSM_STATUS_TX_LIFETIME_EXCEEDED)) {
		hw_priv->tsm_stats.msdu_discarded_count++;
		hw_priv->atbm_tsm_stats[queue_id].msdu_discarded_count++;

	} else if ((hw_priv->start_stop_tsm.start) &&
		(arg->status == WSM_STATUS_SUCCESS)) {
		if (queue_id == hw_priv->tsm_info.ac) {
			struct timeval tmval;
			u16 pkt_delay;
			do_gettimeofday(&tmval);
			pkt_delay =
				hw_priv->start_stop_tsm.packetization_delay;
			if (hw_priv->tsm_info.sta_roamed &&
			    !hw_priv->tsm_info.use_rx_roaming) {
				hw_priv->tsm_info.roam_delay = tmval.tv_usec -
				hw_priv->tsm_info.txconf_timestamp_vo;
				if (hw_priv->tsm_info.roam_delay > pkt_delay)
					hw_priv->tsm_info.roam_delay -= pkt_delay;
				txrx_printk( "[TX] txConf"
				"Roaming: roam_delay = %u\n",
				hw_priv->tsm_info.roam_delay);
				hw_priv->tsm_info.sta_roamed = 0;
			}
			hw_priv->tsm_info.txconf_timestamp_vo = tmval.tv_usec;
		}
	}
	spin_unlock_bh(&hw_priv->tsm_lock);
#endif /*CONFIG_ATBM_APOLLO_TESTMODE*/
#ifdef CONFIG_SUPPORT_LMC_REQUEUE
	if ((arg->status == WSM_REQUEUE) &&
	    (arg->flags & WSM_TX_STATUS_REQUEUE)) {
		/* "Requeue" means "implicit suspend" */
		struct wsm_suspend_resume suspend = {
			.link_id = arg->link_id,
			.stop = 1,
			.multicast = !arg->link_id,
			.if_id = arg->if_id,
		};
		atbm_suspend_resume(priv, &suspend);
		atbm_printk_warn("Requeue for link_id %d (try %d)."
			" STAs asleep: 0x%.8X\n",
			arg->link_id,
			atbm_queue_get_generation(arg->packetID) + 1,
			priv->sta_asleep_mask);
		atbm_printk_debug("<WARNING> %s 111\n",__func__);
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		WARN_ON(atbm_queue_requeue(hw_priv, queue,
				arg->packetID, true));
#else
		WARN_ON(atbm_queue_requeue(queue,
				arg->packetID, true));
#endif
		spin_lock_bh(&priv->ps_state_lock);
		if (!arg->link_id) {
			priv->buffered_multicasts = true;
			if (priv->sta_asleep_mask) {
				atbm_hw_priv_queue_work(hw_priv,
					&priv->multicast_start_work);
			}
		}
		spin_unlock_bh(&priv->ps_state_lock);
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
	} else
#endif
	if (!WARN_ON(atbm_queue_get_skb(
			queue, arg->packetID, &skb, &txpriv))) {
		struct ieee80211_tx_info *tx = IEEE80211_SKB_CB(skb);
		tx_count = arg->ackFailures;
		ht_flags = 0;
		
		//if (priv->association_mode.greenfieldMode)
			//ht_flags |= IEEE80211_TX_RC_GREEN_FIELD;

		if (likely(!arg->status)) {
			tx->flags |= IEEE80211_TX_STAT_ACK;
			priv->cqm_tx_failure_count = 0;
			++tx_count;
			atbm_debug_txed(priv);
			if (arg->flags & WSM_TX_STATUS_AGGREGATION) {
				/* Should report aggregation to mac80211:
				 * it can help  minstrel_ht  calc fit rate*/
				//tx->flags |= IEEE80211_TX_STAT_AMPDU;
				//tx->status.ampdu_len=hw_priv->wsm_txframe_num;
				//tx->status.ampdu_ack_len=(hw_priv->wsm_txframe_num-arg->ackFailures);
				atbm_debug_txed_agg(priv);
			}
		} else {
		}
		/*
		*rate status
		*/
		if(arg->flags & BIT(8)){
			atbm_tx_rate_status(priv,arg);
		}
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		atbm_queue_remove(hw_priv, queue, arg->packetID);
#else
		atbm_queue_remove(queue, arg->packetID);
#endif /*CONFIG_ATBM_APOLLO_TESTMODE*/
	}else{
		atbm_printk_err("[confirm]:<warning>\n");
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
	}
}

static void atbm_notify_buffered_tx(struct atbm_vif *priv,
			       struct sk_buff *skb, int link_id, int tid)
{
	u8 *buffered;
	u8 still_buffered = 0;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	tx_info->control.sta = priv->linked_sta[link_id];
	
	if (link_id && tid < ATBM_APOLLO_MAX_TID) {
		buffered = priv->link_id_db[link_id - 1].buffered;

		spin_lock_bh(&priv->ps_state_lock);
		if (!WARN_ON(!buffered[tid]))
			still_buffered = --buffered[tid];
		spin_unlock_bh(&priv->ps_state_lock);

		if (!still_buffered && tid < ATBM_APOLLO_MAX_TID) {
			if (tx_info->control.sta)
				ieee80211_sta_set_buffered(tx_info->control.sta, tid, false);
		}
	}
}
void atbm_skb_dtor(struct atbm_common *hw_priv,
		     struct sk_buff *skb,
		     const struct atbm_txpriv *txpriv)
{
	struct atbm_vif *priv =
		__ABwifi_hwpriv_to_vifpriv(hw_priv, txpriv->if_id);
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	
	rcu_read_lock();
	tx_info->control.sta = NULL;

	if(priv)
		atbm_notify_buffered_tx(priv, skb,txpriv->raw_link_id, txpriv->tid);
	atbm_ieee80211_tx_status(hw_priv->hw,skb);
	rcu_read_unlock();
}
void atbm_rx_cb(struct atbm_vif *priv,
		  struct wsm_rx *arg,
		  struct sk_buff **skb_p)
{
#ifdef CONFIG_PM
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
#endif
	struct sk_buff *skb = *skb_p;
	struct ieee80211_rx_status *hdr = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_hdr *frame = (struct ieee80211_hdr *)skb->data;
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
#ifdef CONFIG_PM
	unsigned long grace_period;
#endif	
	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED)) {
		/* STA is stopped. */
		atbm_printk_err("priv->mode == NL80211_IFTYPE_UNSPECIFIED,seq(%x)\n",frame->seq_ctrl);
		goto drop;
	}
	
	atbm_debug_rxed(priv);
#ifdef CONFIG_IEEE80211_SPECIAL_FILTER
	if(unlikely(WSM_RX_STATUS_FILTER_FRAME&arg->flags)){
		*skb_p = ieee80211_special_queue_package(priv->vif,skb);
		return;
	}
#endif
	/*
	*process other frame
	*/
	switch(priv->join_status){
#ifdef CONFIG_ATBM_STA_LISTEN
		case ATBM_APOLLO_JOIN_STATUS_STA_LISTEN:
		atbm_printk_debug("listen mode\n");
		/*
		*receive listen
		*/
		hdr->flag |= RX_FLAG_STA_LISTEN;
		atbm_fallthrough;
#endif
	case ATBM_APOLLO_JOIN_STATUS_SIMPLE_MONITOR:
		atbm_printk_debug("monitor mode\n");
		/*
		*receive monitor
		*/
		atbm_ieee80211_rx(priv->hw, skb);
		*skb_p = NULL;
		return;
	case ATBM_APOLLO_JOIN_STATUS_PASSIVE:
//		atbm_printk_err("passive mode ,scan\n");
	case ATBM_APOLLO_JOIN_STATUS_MONITOR:
		atbm_printk_debug("p2p listen\n");
		atbm_fallthrough;		
		/*connnected mode*/
	case ATBM_APOLLO_JOIN_STATUS_AP:
	case ATBM_APOLLO_JOIN_STATUS_IBSS:
		if (unlikely(ieee80211_is_pspoll(frame->frame_control))){
			if (atbm_handle_pspoll(priv, skb))
			{
				goto drop;
			}
		}
		atbm_fallthrough;
	case ATBM_APOLLO_JOIN_STATUS_STA:
		if (unlikely(arg->status)) {
            struct sta_info *sta;

			char *da,*sa,*bssid;
 			//struct ieee80211_sub_if_data * sdata = vif_to_sdata(priv->vif);
 			if(priv->vif->type == NL80211_IFTYPE_AP || priv->vif->type == NL80211_IFTYPE_P2P_GO){
				bssid = frame->addr1;
				sa = frame->addr2;
				da = frame->addr3;
			}else{
				da = frame->addr1;
				bssid = frame->addr2;
				sa = frame->addr3;
			}
		
            sta = (struct sta_info *)sta_info_get_rx(vif_to_sdata(priv->vif), frame->addr2);
            if(sta){
					if(priv->join_status != ATBM_APOLLO_JOIN_STATUS_STA_LISTEN)
                    	atbm_printk_limit("[%s Rx]:da:[%pM] sa:[%pM] bssid:[%pM],PTK[%p],FLAGS[%lx],skb->len[%d]\n",
                            vif_to_sdata(priv->vif)->name,da,sa,bssid,sta->ptk,sta->_flags,skb->len);
            }else {
					if(priv->join_status != ATBM_APOLLO_JOIN_STATUS_STA_LISTEN)
                    	atbm_printk_limit("[%s RX]:[%pM] is not assoctiated with us\n",
                                    vif_to_sdata(priv->vif)->name,frame->addr2);
            }
			/*
			*PMF:
			*drop the frame which decrypted fail
			*/
			if(sta && test_sta_flag(sta, WLAN_STA_MFP)){
				if(atbm_ieee80211_is_robust_mgmt_frame(skb) == true){
					
					 atbm_printk_limit("[RX]:drop PMF\n"); 
					 goto drop;
				}
			}
           
            if (arg->status == WSM_STATUS_MICFAILURE) {
					if(priv->join_status != ATBM_APOLLO_JOIN_STATUS_STA_LISTEN)
                   	 atbm_printk_limit( "[RX] MIC failure. ENCRYPTION [%d],da:[%pM] sa:[%pM] bssid:[%pM]\n",
                            WSM_RX_STATUS_ENCRYPTION(arg->flags),da,sa,bssid);
                    hdr->flag |= RX_FLAG_MMIC_ERROR;
            } else if (arg->status == WSM_STATUS_NO_KEY_FOUND) {
					if(priv->join_status != ATBM_APOLLO_JOIN_STATUS_STA_LISTEN)
                    	atbm_printk_limit( "[RX] No key found.ENCRYPTION [%d]da:[%pM] sa:[%pM] bssid:[%pM]\n",
                                    WSM_RX_STATUS_ENCRYPTION(arg->flags),da,sa,bssid);
                            hdr->flag |= RX_FLAG_UNKOWN_STA_FRAME;
                    if(priv->join_status != ATBM_APOLLO_JOIN_STATUS_AP){
                            goto drop;
                    }
            } else {
            	if(priv->join_status != ATBM_APOLLO_JOIN_STATUS_STA_LISTEN)
                    atbm_printk_limit("[RX] Receive failure: %d.ENCRYPTION [%d]da:[%pM] sa:[%pM] bssid:[%pM],fc(%x)\n",
                            arg->status,WSM_RX_STATUS_ENCRYPTION(arg->flags),da,sa,bssid,mgmt->frame_control);
                    hdr->flag |= RX_FLAG_UNKOWN_STA_FRAME;
                    if(priv->join_status != ATBM_APOLLO_JOIN_STATUS_AP){
                            goto drop;
                    }
            }
	 	}
#ifdef CHKSUM_HW_SUPPORT
		else {
			if (arg->flags & WSM_RX_STATUS_CHKSUM_ERROR){
				hdr->flag &=~RX_FLAG_HW_CHKSUM_ERROR;
				hdr->flag |= RX_FLAG_HW_CHKSUM_ERROR;
			}else{
				hdr->flag &=~RX_FLAG_HW_CHKSUM_ERROR;
			}
		}	
#endif
		if(WSM_RX_STATUS_ENCRYPTION(arg->flags)){
			if(ieee80211_has_protected(frame->frame_control))
				hdr->flag |= RX_FLAG_DECRYPTED;
			else {
				atbm_printk_limit("[RX]: open frame decrypted[%x]?\n",frame->frame_control);
			}
		}else {
			if(ieee80211_has_protected(frame->frame_control)){
				atbm_printk_limit("[RX]:need sw decrypted?\n");
			}
		}

		if (arg->status == WSM_STATUS_MICFAILURE) {
			hdr->flag |= RX_FLAG_IV_STRIPPED;
		}
#ifdef CONFIG_PM

		/* Stay awake for 1sec. after frame is received to give
		 * userspace chance to react and acquire appropriate
		 * wakelock. */
		if (ieee80211_is_auth(frame->frame_control))
			grace_period = 5 * HZ;
		else if (ieee80211_is_deauth(frame->frame_control))
			grace_period = 5 * HZ;
		else 
			grace_period = 0;
		if(grace_period != 0)
			atbm_pm_stay_awake(&hw_priv->pm_state, grace_period);
#endif

#ifdef CONFIG_ATBM_SUPPORT_P2P
#ifdef ATBM_P2P_CHANGE	
			atbm_parase_p2p_action_frame(priv,skb,false);
			atbm_parase_p2p_scan_resp(priv,skb);
#endif
#endif

		atbm_ieee80211_rx(priv->hw, skb);
		*skb_p = NULL;
		return;
	default:
		atbm_printk_err("mode err\n");
		goto drop;
	}
drop:
	/* TODO: update failure counters */
	return;
}

/* ******************************************************************** */
/* Security								*/

int atbm_alloc_key(struct atbm_common *hw_priv)
{
	int idx;

	idx = ffs(~hw_priv->key_map) - 1;
	if (idx < 0 || idx > WSM_KEY_MAX_INDEX)
		return -1;

	hw_priv->key_map |= BIT(idx);
	hw_priv->keys[idx].entryIndex = idx;
	return idx;
}

void atbm_free_key(struct atbm_common *hw_priv, int idx)
{
	if((hw_priv->key_map & BIT(idx))){
		memset(&hw_priv->keys[idx], 0, sizeof(hw_priv->keys[idx]));
		hw_priv->key_map &= ~BIT(idx);
	}
}

void atbm_free_keys(struct atbm_common *hw_priv)
{
	memset(&hw_priv->keys, 0, sizeof(hw_priv->keys));
	hw_priv->key_map = 0;
}
#ifdef CONFIG_ATBM_SUPPORT_IBSS
int atbm_upload_keys(struct atbm_vif *priv)
{
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	int idx, ret = 0;

	for (idx = 0; idx <= WSM_KEY_MAX_IDX; ++idx)
		if (hw_priv->key_map & BIT(idx)) {
			ret = wsm_add_key(hw_priv, &hw_priv->keys[idx], priv->if_id);
			if (ret < 0)
				break;
		}
	return ret;
}
#endif
