/*
 * mac80211 STA and AP API for mac80211 altobeam APOLLO drivers
 *
 *
 * Copyright (c) 2016, altobeam
 * Author: 
 *
 * Based on 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "apollo.h"
#include "sta.h"
#include "ap.h"
#include "bh.h"
#include "net/atbm_mac80211.h"
#include "mac80211/ieee80211_i.h"


#if defined(CONFIG_ATBM_APOLLO_STA_DEBUG)
#define ap_printk(...) atbm_printk_always(__VA_ARGS__)
#else
#define ap_printk(...)
#endif
extern int start_choff;
#define HT_INFO_OFFSET 4
#define HT_INFO_MASK 0x0011
#define HT_INFO_IE_LEN 22
#define ATBM_APOLLO_LINK_ID_GC_TIMEOUT ((unsigned long)(10 * HZ))

#define ATBM_APOLLO_ENABLE_ARP_FILTER_OFFLOAD	3
/*For Samsung, it is defined as 4*/
#define ATBM_APOLLO_KEEP_ALIVE_PERIOD	(4)

#ifndef ERP_INFO_BYTE_OFFSET
#define ERP_INFO_BYTE_OFFSET 2
#endif

#ifdef IPV6_FILTERING
#define ATBM_APOLLO_ENABLE_NDP_FILTER_OFFLOAD	3
#endif /*IPV6_FILTERING*/

static int atbm_upload_beacon(struct atbm_vif *priv);
#ifdef ATBM_PROBE_RESP_EXTRA_IE
static int atbm_upload_proberesp(struct atbm_vif *priv);
#endif
#ifdef CONFIG_ATBM_BT_COMB
static int atbm_upload_pspoll(struct atbm_vif *priv);
static int atbm_upload_null(struct atbm_vif *priv);
#endif
static int atbm_upload_qosnull(struct atbm_vif *priv);
static int atbm_start_ap(struct atbm_vif *priv);
static int atbm_update_beaconing(struct atbm_vif *priv);
/*
static int atbm_enable_beaconing(struct atbm_vif *priv,
				   bool enable);
*/
static void __atbm_sta_notify(struct atbm_vif *priv,
				enum sta_notify_cmd notify_cmd,
				int link_id);

/* ******************************************************************** */
/* AP API								*/

int atbm_sta_add(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct atbm_sta_priv *sta_priv =
			(struct atbm_sta_priv *)&sta->drv_priv;
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);
	struct atbm_link_entry *entry;
	struct atbm_common *hw_priv = hw->priv;
	struct wsm_map_link map_link = {
		.link_id = 0,
	};
		
	if(atbm_bh_is_term(hw_priv)){
		return -ENOENT;
	}

	if(vif->type == NL80211_IFTYPE_STATION){
		struct cfg80211_bss *bss;
		int ret = -1;
		if((bss = ieee80211_atbm_get_authen_bss(vif, hw_priv->channel,sta->addr, NULL, 0))){
			ret = atbm_do_join(vif,bss);
			ieee80211_atbm_put_authen_bss(vif,bss);
		}else if((bss = ieee80211_atbm_get_bss(hw_priv->hw->wiphy, hw_priv->channel,sta->addr, NULL, 0, 0, 0))){
			ret = atbm_do_join(vif,bss);
			ieee80211_atbm_put_bss(hw_priv->hw->wiphy, bss);
		}
		if(ret == 0){
			rcu_assign_pointer(priv->linked_sta[0], sta);
			rcu_assign_pointer(priv->linked_sta[ATBMWIFI_MAX_STA_IN_AP_MODE+1], sta);
		}
		return ret;
	}
	if (priv->mode != NL80211_IFTYPE_AP)
		return 0;

	if (sta->aid == 0 || sta->aid > hw_priv->wsm_caps.NumOfStations) {
		atbm_printk_err("add sta[%pM]],Aid[%d] err\n", sta->addr, sta->aid);
		return -ENOENT;
	}
	
	atbm_printk_err("sta(%pM)(%d) add\n",sta->addr,sta->aid);
	
	spin_lock_bh(&priv->ps_state_lock);
	
	sta_priv->priv = priv;
	sta_priv->link_id = sta->aid;
	entry = &priv->link_id_db[sta_priv->link_id - 1];

	BUG_ON(entry->status != ATBM_APOLLO_LINK_OFF);
	
	entry->status = ATBM_APOLLO_LINK_HARD;
	entry->timestamp = jiffies;
	priv->sta_asleep_mask &= ~BIT(sta_priv->link_id);
	priv->pspoll_mask &= ~BIT(sta_priv->link_id);
	priv->link_id_map |= BIT(sta_priv->link_id);
	map_link.link_id = sta_priv->link_id;
	memcpy(map_link.mac_addr, sta->addr, ETH_ALEN);
	memcpy(entry->mac,sta->addr,ETH_ALEN);
	hw_priv->connected_sta_cnt++;
	spin_unlock_bh(&priv->ps_state_lock);

	rcu_assign_pointer(priv->linked_sta[sta_priv->link_id], sta);
	if (WARN_ON(wsm_map_link(hw_priv, &map_link, priv->if_id))) {
		atbm_printk_err("Sta[%pM][%d] add errr\n", sta->addr, sta->aid);
		entry->status = ATBM_APOLLO_LINK_OFF;
		rcu_assign_pointer(priv->linked_sta[sta_priv->link_id], NULL);
		synchronize_rcu();
		return -ENOENT;
	}
	return 0;
}

int atbm_sta_remove(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta)
{
	struct atbm_common *hw_priv = hw->priv;
	struct atbm_sta_priv *sta_priv =
			(struct atbm_sta_priv *)&sta->drv_priv;
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);
	struct atbm_link_entry *entry;
	struct wsm_map_link unmap_link = {
		.link_id = 0,
		.unmap = 1,
	};
	if(atbm_bh_is_term(hw_priv)){
		return 0;
	}
	if(vif->type == NL80211_IFTYPE_STATION){
		atbm_do_unjoin(vif,sta->addr);
		rcu_assign_pointer(priv->linked_sta[0], NULL);
		rcu_assign_pointer(priv->linked_sta[ATBMWIFI_MAX_STA_IN_AP_MODE+1], NULL);
		rcu_assign_pointer(priv->linked_sta[sta->aid], NULL);
		synchronize_rcu();
		return 0;
	}
	if (priv->mode != NL80211_IFTYPE_AP || !sta_priv->link_id)
		return 0;
	
	atbm_printk_err("sta(%pM)(%d)(%d) remove\n",sta->addr,sta_priv->link_id,sta->aid);
	WARN_ON(sta_priv->link_id != sta->aid);
	
	entry = &priv->link_id_db[sta_priv->link_id - 1];

	WARN_ON(memcmp(sta->addr,entry->mac,ETH_ALEN));
	
	rcu_assign_pointer(priv->linked_sta[sta_priv->link_id], NULL);
	/*
	*wait tx path and rx path finished .after that the data frame can not
	*send to low path
	*/
	synchronize_rcu();

	spin_lock_bh(&priv->ps_state_lock);
	entry->status = ATBM_APOLLO_LINK_OFF;
	priv->link_id_map &= ~BIT(sta_priv->link_id);
	priv->sta_asleep_mask &= ~BIT(sta_priv->link_id);
	priv->pspoll_mask &= ~BIT(sta_priv->link_id);
	unmap_link.link_id = sta_priv->link_id;
	memcpy(unmap_link.mac_addr, sta->addr, ETH_ALEN);
	hw_priv->connected_sta_cnt--;
	spin_unlock_bh(&priv->ps_state_lock);
	/*
	*trigger send pending package
	*/
	atbm_bh_wakeup(hw_priv);

	if(atbm_wait_event_timeout_stay_awake(hw_priv,
				hw_priv->tx_queue_stats.wait_link_id_empty,
				atbm_queue_stats_is_empty(
					&hw_priv->tx_queue_stats, sta_priv->link_id, priv->if_id),
				7* HZ,true) == 0){
		atbm_printk_err("sta[%pM] clear pending package timeout\n",sta->addr);
	}
	
	WARN_ON(wsm_map_link(hw_priv, &unmap_link, priv->if_id));
	return 0;
}

static void __atbm_sta_notify(struct atbm_vif *priv,
				enum sta_notify_cmd notify_cmd,
				int link_id)
{
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	u32 bit, prev;

	/* Zero link id means "for all link IDs" */
	if (link_id)
		bit = BIT(link_id);
	else if (WARN_ON_ONCE(notify_cmd != STA_NOTIFY_AWAKE))
		bit = 0;
	else
		bit = priv->link_id_map;
	prev = priv->sta_asleep_mask & bit;

	switch (notify_cmd) {
	case STA_NOTIFY_SLEEP:
		if (!prev) {
			if (priv->buffered_multicasts &&
					!priv->sta_asleep_mask)
				atbm_hw_priv_queue_work(hw_priv,
					&priv->multicast_start_work);
			priv->sta_asleep_mask |= bit;
		}
		break;
	case STA_NOTIFY_AWAKE:
		if (prev) {
			priv->sta_asleep_mask &= ~bit;
			priv->pspoll_mask &= ~bit;
			if (priv->tx_multicast && link_id &&
					!priv->sta_asleep_mask)
				atbm_hw_priv_queue_work(hw_priv,
					&priv->multicast_stop_work);
			atbm_bh_wakeup(hw_priv);
		}
		break;
	}
}

void atbm_sta_notify(struct ieee80211_hw *dev,
		       struct ieee80211_vif *vif,
		       enum sta_notify_cmd notify_cmd,
		       struct ieee80211_sta *sta)
{
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);
	struct atbm_sta_priv *sta_priv =
		(struct atbm_sta_priv *)&sta->drv_priv;
	u8 tid  = 0;
	
	if(atomic_read(&priv->enabled)==0){
		return;
	}
	if(atbm_bh_is_term(priv->hw_priv)){
		return;
	}
	
	spin_lock_bh(&priv->ps_state_lock);
	__atbm_sta_notify(priv, notify_cmd, sta_priv->link_id);

	if(notify_cmd == STA_NOTIFY_SLEEP){
		for(tid = 0; tid < ATBM_APOLLO_MAX_TID ; tid++){
			if(priv->link_id_db[sta_priv->link_id - 1].buffered[tid]){
				ieee80211_sta_set_buffered(sta, tid, true);
			}
		}
	}
	spin_unlock_bh(&priv->ps_state_lock);
}

static void atbm_ps_notify(struct atbm_vif *priv,
		      int link_id, bool ps)
{
	if (link_id > ATBMWIFI_MAX_STA_IN_AP_MODE)
		return;

	txrx_printk("%s for LinkId: %d. STAs asleep: %.8X\n",
			ps ? "Stop" : "Start",
			link_id, priv->sta_asleep_mask);

	/* TODO:COMBO: __atbm_sta_notify changed. */
	__atbm_sta_notify(priv,
		ps ? STA_NOTIFY_SLEEP : STA_NOTIFY_AWAKE, link_id);
}

static int atbm_set_tim_impl(struct atbm_vif *priv, bool aid0_bit_set)
{
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	struct sk_buff *skb;
	struct wsm_update_ie update_ie = {
		.what = WSM_UPDATE_IE_BEACON,
		.count = 1,
	};
	u16 tim_offset, tim_length;
	
	skb = ieee80211_beacon_get_tim(priv->hw, priv->vif,
			&tim_offset, &tim_length);
	if (!skb) {
		atbm_printk_err("%s : if_id[%d],if_name[%s] alloc skb err \n",__func__,priv->if_id,vif_to_sdata(priv->vif)->name);
		return -ENOENT;
	}

	if (tim_offset && tim_length >= 6) {
		/* Ignore DTIM count from mac80211:
		 * firmware handles DTIM internally. */
		skb->data[tim_offset + 2] = 0;

		/* Set/reset aid0 bit */
		if (aid0_bit_set)
			skb->data[tim_offset + 4] |= 1;
		else
			skb->data[tim_offset + 4] &= ~1;
	}

	update_ie.ies = &skb->data[tim_offset];
	update_ie.length = tim_length;
	WARN_ON(wsm_update_ie(hw_priv, &update_ie, priv->if_id));

	atbm_dev_kfree_skb(skb);

	return 0;
}

void atbm_set_tim_work(struct atbm_work_struct *work)
{
	struct atbm_vif *priv =
		container_of(work, struct atbm_vif, set_tim_work);
	if(atbm_bh_is_term(priv->hw_priv)){
		return;
	}
	(void)atbm_set_tim_impl(priv, priv->aid0_bit_set);
}

int atbm_set_tim(struct ieee80211_hw *dev, struct ieee80211_sta *sta,bool set)
{
	struct atbm_sta_priv *sta_priv =
		(struct atbm_sta_priv *)&sta->drv_priv;
	struct atbm_vif *priv = sta_priv->priv;

	if(atomic_read(&priv->enabled)==0){
		return 0;
	}
	if(atbm_bh_is_term(priv->hw_priv)){
		return 0;
	}
	WARN_ON(priv->mode != NL80211_IFTYPE_AP);
	atbm_hw_priv_queue_work(priv->hw_priv, &priv->set_tim_work);
	return 0;
}
#if 0
void atbm_set_cts_work(struct atbm_work_struct *work)
{
	struct atbm_vif *priv =
		container_of(work, struct atbm_vif, set_cts_work.work);
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);

	u8 erp_ie[3] = {ATBM_WLAN_EID_ERP_INFO, 0x1, 0};
	struct wsm_update_ie update_ie = {
		.what = WSM_UPDATE_IE_BEACON,
		.count = 1,
		.ies = erp_ie,
		.length = 3,
	};
	u32 erp_info;
	__le32 use_cts_prot;
	if(atbm_bh_is_term(hw_priv)){
		return;
	}
	mutex_lock(&hw_priv->conf_mutex);
	erp_info = priv->erp_info;
	mutex_unlock(&hw_priv->conf_mutex);
	use_cts_prot =
		erp_info & WLAN_ERP_USE_PROTECTION ?
		__cpu_to_le32(1) : 0;

	erp_ie[ERP_INFO_BYTE_OFFSET] = erp_info;

	ap_printk("[STA] ERP information 0x%x\n", erp_info);

	/* TODO:COMBO: If 2 interfaces are on the same channel they share
	the same ERP values */
	WARN_ON(wsm_write_mib(hw_priv, WSM_MIB_ID_NON_ERP_PROTECTION,
				&use_cts_prot, sizeof(use_cts_prot),
				priv->if_id));
#if defined(CONFIG_NL80211_TESTMODE) || defined(CONFIG_ATBM_IOCTRL)
	{
		extern int atbm_tool_use_cts_prot;
		atbm_tool_use_cts_prot = use_cts_prot;
	}
#endif
	/* If STA Mode update_ie is not required */
	if (priv->mode != NL80211_IFTYPE_STATION) {
		WARN_ON(wsm_update_ie(hw_priv, &update_ie, priv->if_id));
	}

	return;
}
#endif
#ifdef CONFIG_ATBM_BT_COMB
static int atbm_set_btcoexinfo(struct atbm_vif *priv)
{
	struct wsm_override_internal_txrate arg;
	int ret = 0;

	if (priv->mode == NL80211_IFTYPE_STATION) {
		/* Plumb PSPOLL and NULL template */
		WARN_ON(atbm_upload_pspoll(priv));
		WARN_ON(atbm_upload_null(priv));
	} else {
		return 0;
	}

	memset(&arg, 0, sizeof(struct wsm_override_internal_txrate));

	if (!priv->vif->p2p) {
		/* STATION mode */
		if (priv->bss_params.operationalRateSet & ~0xF) {
			ap_printk("[STA] STA has ERP rates\n");
			/* G or BG mode */
			arg.internalTxRate = (__ffs(
			priv->bss_params.operationalRateSet & ~0xF));
		} else {
			ap_printk("[STA] STA has non ERP rates\n");
			/* B only mode */
			arg.internalTxRate = (__ffs(
			priv->association_mode.basicRateSet));
		}
		arg.nonErpInternalTxRate = (__ffs(
			priv->association_mode.basicRateSet));
	} else {
		/* P2P mode */
		arg.internalTxRate = (__ffs(
			priv->bss_params.operationalRateSet & ~0xF));
		arg.nonErpInternalTxRate = (__ffs(
			priv->bss_params.operationalRateSet & ~0xF));
	}

	ap_printk( "[STA] BTCOEX_INFO"
		"MODE %d, internalTxRate : %x, nonErpInternalTxRate: %x\n",
		priv->mode,
		arg.internalTxRate,
		arg.nonErpInternalTxRate);

	ret = WARN_ON(wsm_write_mib(ABwifi_vifpriv_to_hwpriv(priv),
		WSM_MIB_ID_OVERRIDE_INTERNAL_TX_RATE,
		&arg, sizeof(arg), priv->if_id));

	return ret;
}
#endif
#ifdef CONFIG_ATBM_SUPPORT_IBSS
void atbm_ibss_join_work(struct atbm_vif *priv)
{
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	const u8 *bssid;
	struct cfg80211_bss *bss;
	struct wsm_protected_mgmt_policy mgmt_policy;
	struct wsm_operational_mode mode = {
		.power_mode = wsm_power_mode_quiescent,
		.disableMoreFlagUsage = true,
	};
	struct wsm_join join = {
                .mode = WSM_JOIN_MODE_IBSS,
                .preambleType = WSM_JOIN_PREAMBLE_SHORT,
                .probeForJoin = 1,
                /* dtimPeriod will be updated after association */
                .dtimPeriod = 1,
//                .beaconInterval = bss->beacon_interval,
    };

	bssid = priv->vif->bss_conf.bssid;
	bss = ieee80211_atbm_get_bss(hw_priv->hw->wiphy, hw_priv->channel,
			bssid, NULL, 0, 0, 0);
	if (!bss) {
		wsm_unlock_tx(hw_priv);
		return;
	}
	join.beaconInterval = bss->beacon_interval;
	if (priv->if_id)
		join.flags |= WSM_FLAG_MAC_INSTANCE_1;
	else
		join.flags &= ~WSM_FLAG_MAC_INSTANCE_1;

	if(priv->tmpframe_probereq_set==0){
		join.probeForJoin = 0;
		//hw_priv->channel->hw_value = 6;
		//join.channelNumber = 0;
		atbm_printk_ap("<atbm_WIFI>because not scan before join, probeForJoin =0 hw_value %d\n",hw_priv->channel->hw_value);
	}
#ifdef CONFIG_ATBM_BT_COMB
	/* BT Coex related changes */
	if (hw_priv->is_BT_Present) {
		if (((hw_priv->conf_listen_interval * 100) %
				bss->beacon_interval) == 0)
			priv->listen_interval =
				((hw_priv->conf_listen_interval * 100) /
				bss->beacon_interval);
		else
			priv->listen_interval =
				((hw_priv->conf_listen_interval * 100) /
				bss->beacon_interval + 1);
	}
#endif

	if (priv->hw->conf.ps_dtim_period) {
		priv->join_dtim_period = priv->hw->conf.ps_dtim_period;
	}
	join.dtimPeriod = priv->join_dtim_period;
	priv->beacon_int = bss->beacon_interval;
	ap_printk( "[STA] Join DTIM: %d, interval: %d\n",
			join.dtimPeriod, priv->beacon_int);

	hw_priv->is_go_thru_go_neg = false;
	join.channelNumber = channel_hw_value(hw_priv->channel);

	/* basicRateSet will be updated after association.
	Currently these values are hardcoded */
	if (hw_priv->channel->band == IEEE80211_BAND_5GHZ) {
		join.band = WSM_PHY_BAND_5G;
		join.basicRateSet = 64; /*6 mbps*/
	}else{
		join.band = WSM_PHY_BAND_2_4G;
		join.basicRateSet = 7; /*1, 2, 5.5 mbps*/
	}
	memcpy(&join.bssid[0], bssid, sizeof(join.bssid));
	memcpy(&priv->join_bssid[0], bssid, sizeof(priv->join_bssid));
#ifdef CONFIG_ATBM_SUPPORT_P2P
	if (priv->vif->p2p) {
		join.flags |= WSM_JOIN_FLAGS_P2P_GO;
		join.basicRateSet =
			atbm_rate_mask_to_wsm(hw_priv, 0xFF0);
	}
#endif
	if(priv->if_id&&(priv->vif->p2p==true)){
		join.channel_type = (u32)(hw_priv->channel_type>NL80211_CHAN_NO_HT ? 
								  NL80211_CHAN_HT20 : NL80211_CHAN_NO_HT);
	} else {
		join.channel_type = (u32)hw_priv->channel_type;
	}
	wsm_flush_tx(hw_priv);

#ifdef CONFIG_PM
	/*Stay Awake for Join Timeout*/
	atbm_pm_stay_awake(&hw_priv->pm_state, 3 * HZ);
#endif
#if define(CONFIG_ATBM_STA_LISTEN) || defined(CONFIG_ATBM_SUPPORT_P2P)
	atbm_disable_listening(priv);
#endif
	WARN_ON(wsm_set_operational_mode(hw_priv, &mode, priv->if_id));
	WARN_ON(wsm_set_block_ack_policy(hw_priv,
		/*hw_priv->ba_tid_mask*/0, hw_priv->ba_tid_rx_mask, priv->if_id));
	mgmt_policy.protectedMgmtEnable = 0;
	mgmt_policy.unprotectedMgmtFramesAllowed = 1;
	mgmt_policy.encryptionForAuthFrame = 1;
	wsm_set_protected_mgmt_policy(hw_priv, &mgmt_policy,
				      priv->if_id);

	if (wsm_join(hw_priv, &join, priv->if_id)) {
		memset(&priv->join_bssid[0],
			0, sizeof(priv->join_bssid));

	} else {
		/* Upload keys */
		priv->join_status = ATBM_APOLLO_JOIN_STATUS_IBSS;
		atbm_upload_keys(priv);

		/* Due to beacon filtering it is possible that the
		 * AP's beacon is not known for the mac80211 stack.
		 * Disable filtering temporary to make sure the stack
		 * receives at least one */
		priv->disable_beacon_filter = true;

	}
	atbm_update_filtering(priv);
	ieee80211_atbm_put_bss(hw_priv->hw->wiphy, bss);
	wsm_unlock_tx(hw_priv);
}
#endif
#ifdef CONFIG_ATBM_HE
static u8 atbm_he_get_ppe_val(u8 *ppe, u8 ppe_pos_bit)
{
	u8 byte_num = ppe_pos_bit / 8;
	u8 bit_num = ppe_pos_bit % 8;
	u8 residue_bits;
	u8 res;

	if (bit_num <= 5)
		return (ppe[byte_num] >> bit_num) &(BIT(ATBM_IEEE80211_PPE_THRES_INFO_PPET_SIZE) - 1);

	/*
	 * If bit_num > 5, we have to combine bits with next byte.
	 * Calculate how many bits we need to take from current byte (called
	 * here "residue_bits"), and add them to bits from next byte.
	 */

	residue_bits = 8 - bit_num;

	res = (ppe[byte_num + 1] &(BIT(ATBM_IEEE80211_PPE_THRES_INFO_PPET_SIZE - residue_bits) - 1)) << residue_bits;
	res += (ppe[byte_num] >> bit_num) & (BIT(residue_bits) - 1);

	return res;
}
#endif //CONFIG_ATBM_HE

void atbm_bss_info_changed(struct ieee80211_hw *dev,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *info,
			     u32 changed)
{
	struct atbm_common *hw_priv = dev->priv;
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);
#ifdef CONFIG_ATBM_SUPPORT_IBSS
	bool do_ibss_join = false;
#endif
	u8 pmMode = WSM_PSM_FAST_PS;

	if(unlikely(info->uapsd)){
		pmMode = WSM_PSM_PS;
	}
	if(atbm_bh_is_term(hw_priv)){
		return;
	}
	if(atomic_read(&priv->enabled) == 0){
		atbm_printk_err("[%s] has been disabled\n",vif_to_sdata(vif)->name);
		return;
	}
	mutex_lock(&hw_priv->conf_mutex);
	if (changed & BSS_CHANGED_STA_RESTART){
		struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
		if(sdata->u.mgd.associated){
			atbm_restart_join_bss(priv,sdata->u.mgd.associated);
		}
	}
	if(changed & BSS_CHANGED_HT_CHANNEL_TYPE){
		/*
		*sta and ap mode change channel or channel type
		*/
		if(info->assoc || (priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP)){
			struct wsm_set_chantype set_channtype;	
			wsm_lock_tx_async(hw_priv);
			wsm_flush_tx(hw_priv);
			BUG_ON(hw_priv->channel == NULL);
			set_channtype.band = (hw_priv->channel->band == IEEE80211_BAND_5GHZ) ?
								 WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G;
			set_channtype.flag = 0;
			set_channtype.channelNumber = channel_hw_value(hw_priv->channel);
			set_channtype.channelType = (u32)info->channel_type;
			atbm_printk_err("%s:chatype(%d),channelNumber(%d)\n",__func__,set_channtype.channelType,set_channtype.channelNumber);
			if(wsm_set_chantype_func(hw_priv,&set_channtype,priv->if_id)){
				/*
				*if we set channel type err to lmc,send 20 M is safe
				*/
			}
			wsm_unlock_tx(hw_priv);
		}
	}
	if (changed & BSS_CHANGED_BSSID) {
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		spin_lock_bh(&hw_priv->tsm_lock);
		if (hw_priv->tsm_info.sta_associated) {
			unsigned now = jiffies;
			hw_priv->tsm_info.sta_roamed = 1;
			if ((now - hw_priv->tsm_info.txconf_timestamp_vo) >
			    (now - hw_priv->tsm_info.rx_timestamp_vo))
				hw_priv->tsm_info.use_rx_roaming = 1;
		} else {
			hw_priv->tsm_info.sta_associated = 1;
		}
		spin_unlock_bh(&hw_priv->tsm_lock);
#endif /*CONFIG_ATBM_APOLLO_TESTMODE*/
		memcpy(priv->bssid, info->bssid, ETH_ALEN);
		atbm_setup_mac_pvif(priv);
#ifdef CONFIG_ATBM_SUPPORT_IBSS
		if (info->ibss_joined)
			do_ibss_join = true;
#endif
	}

	/* TODO: BSS_CHANGED_IBSS */
#ifdef CONFIG_ATBM_LMAC_FILTER_IP_FRAME
	if (changed & BSS_CHANGED_ARP_FILTER) {
		struct wsm_arp_ipv4_filter filter = {0};
		int i;
		ap_printk( "[STA] BSS_CHANGED_ARP_FILTER "
				     "enabled: %d, cnt: %d\n",
				     info->arp_filter_enabled,
				     info->arp_addr_cnt);

		if (info->arp_filter_enabled){
            if (vif->type == NL80211_IFTYPE_STATION)
                    filter.enable = (u32)ATBM_APOLLO_ENABLE_ARP_FILTER_OFFLOAD;
            else if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP)
                    filter.enable = (u32)(1<<1);
            else
                    filter.enable = 0;
        }

		/* Currently only one IP address is supported by firmware.
		 * In case of more IPs arp filtering will be disabled. */
		if (info->arp_addr_cnt > 0 &&
		    info->arp_addr_cnt <= WSM_MAX_ARP_IP_ADDRTABLE_ENTRIES) {
			for (i = 0; i < info->arp_addr_cnt; i++) {
				filter.ipv4Address[i] = info->arp_addr_list[i];
				ap_printk( "[STA] addr[%d]: 0x%X\n",
					  i, filter.ipv4Address[i]);
			}
		} else
			filter.enable = 0;
		ap_printk( "[STA] arp ip filter enable: %d\n",
			  __le32_to_cpu(filter.enable));
		atbm_printk_ap("[STA] arp ip filter enable: %d\n",
			  __le32_to_cpu(filter.enable));
#ifdef CONFIG_ATBM_LMAC_FILTER_IP_FRAME
		if (filter.enable){
            atbm_set_arpreply(dev, vif);
		}
		if (wsm_set_arp_ipv4_filter(hw_priv, &filter, priv->if_id))
			WARN_ON(1);
#endif

		priv->filter4.enable = filter.enable;
		if (priv->filter4.enable &&
			(priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA)) {
			/* Firmware requires that value for this 1-byte field must
			 * be specified in units of 500us. Values above the 128ms
			 * threshold are not supported. */
			LOCAL_SET_CONNECT_STOP(hw_to_local(hw_priv->hw));
			if (info->dynamic_ps_timeout >= 0x80)
				priv->powersave_mode.fastPsmIdlePeriod = 0xFF;
			else
				priv->powersave_mode.fastPsmIdlePeriod =
					info->dynamic_ps_timeout << 1;

			if (priv->setbssparams_done) {
				struct wsm_set_pm pm = priv->powersave_mode;
				int ret = 0;
				
				if(info->ps_enabled)
					priv->powersave_mode.pmMode = pmMode;
				else
					priv->powersave_mode.pmMode = WSM_PSM_ACTIVE;
				atbm_printk_err("%s %d ,pmMode = %d \n",__func__,__LINE__,priv->powersave_mode.pmMode);
				ret = atbm_set_pm (priv, &priv->powersave_mode);
				if(ret)
					priv->powersave_mode = pm;
			} else{
//				priv->powersave_mode.pmMode = WSM_PSM_FAST_PS;
				
			}
			atbm_printk_err("%s %d : pmMode(%d) \n",__func__,__LINE__,priv->powersave_mode.pmMode);
		}

	}
#ifdef IPV6_FILTERING
	if (changed & BSS_CHANGED_NDP_FILTER) {
		struct wsm_ndp_ipv6_filter filter = {0};
		int i;
		u16 *ipv6addr = NULL;

		ap_printk( "[STA] BSS_CHANGED_NDP_FILTER "
				     "enabled: %d, cnt: %d\n",
				     info->ndp_filter_enabled,
				     info->ndp_addr_cnt);

		if (info->ndp_filter_enabled) {
			if (vif->type == NL80211_IFTYPE_STATION)
				filter.enable = (u32)ATBM_APOLLO_ENABLE_NDP_FILTER_OFFLOAD;
			else if ((vif->type == NL80211_IFTYPE_AP))
				filter.enable = (u32)(1<<1);
			else
				filter.enable = 0;
		}

		/* Currently only one IP address is supported by firmware.
		 * In case of more IPs ndp filtering will be disabled. */
		if (info->ndp_addr_cnt > 0 &&
		    info->ndp_addr_cnt <= WSM_MAX_NDP_IP_ADDRTABLE_ENTRIES) {
			for (i = 0; i < info->ndp_addr_cnt; i++) {
				priv->filter6.ipv6Address[i] = filter.ipv6Address[i] = info->ndp_addr_list[i];
				ipv6addr = (u16 *)(&filter.ipv6Address[i]);
				ap_printk( "[STA] ipv6 addr[%d]: %x:%x:%x:%x:%x:%x:%x:%x\n", \
									i, cpu_to_be16(*(ipv6addr + 0)), cpu_to_be16(*(ipv6addr + 1)), \
									cpu_to_be16(*(ipv6addr + 2)), cpu_to_be16(*(ipv6addr + 3)), \
									cpu_to_be16(*(ipv6addr + 4)), cpu_to_be16(*(ipv6addr + 5)), \
									cpu_to_be16(*(ipv6addr + 6)), cpu_to_be16(*(ipv6addr + 7)));
			}
		} else {
			filter.enable = 0;
			for (i = 0; i < info->ndp_addr_cnt; i++) {
				ipv6addr = (u16 *)(&info->ndp_addr_list[i]);
				ap_printk( "[STA] ipv6 addr[%d]: %x:%x:%x:%x:%x:%x:%x:%x\n", \
									i, cpu_to_be16(*(ipv6addr + 0)), cpu_to_be16(*(ipv6addr + 1)), \
									cpu_to_be16(*(ipv6addr + 2)), cpu_to_be16(*(ipv6addr + 3)), \
									cpu_to_be16(*(ipv6addr + 4)), cpu_to_be16(*(ipv6addr + 5)), \
									cpu_to_be16(*(ipv6addr + 6)), cpu_to_be16(*(ipv6addr + 7)));
			}
		}

		ap_printk( "[STA] ndp ip filter enable: %d\n",
			  __le32_to_cpu(filter.enable));

		if (filter.enable)
			atbm_set_na(dev, vif);

		priv->filter6.enable = filter.enable;

		if (wsm_set_ndp_ipv6_filter(hw_priv, &filter, priv->if_id))
			WARN_ON(1);
#if 0 /*Commented out to disable Power Save in IPv6*/
		if (filter.enable && (priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA) && (priv->vif->p2p) &&
				!(priv->firmware_ps_mode.pmMode & WSM_PSM_FAST_PS)) {
			if(priv->setbssparams_done) {
				struct wsm_set_pm pm = priv->powersave_mode;
				int ret = 0;

				priv->powersave_mode.pmMode = WSM_PSM_FAST_PS;
				ret = atbm_set_pm(priv, &priv->powersave_mode);
				if(ret) {
					priv->powersave_mode = pm;
				}
			} else {
				priv->powersave_mode.pmMode = WSM_PSM_FAST_PS;
			}
		}
#endif
	}
#endif /*IPV6_FILTERING*/
#endif
	if (changed & BSS_CHANGED_BEACON) {
		atbm_printk_err( "BSS_CHANGED_BEACON\n");
#ifdef HIDDEN_SSID
		if(priv->join_status != ATBM_APOLLO_JOIN_STATUS_AP)
		{
		
	      priv->hidden_ssid = info->hidden_ssid;
	      priv->ssid_length = info->ssid_len;
	      memcpy(priv->ssid, info->ssid, info->ssid_len);

		}else{
     	 	ap_printk( "priv->join_status=%d\n",priv->join_status);
		}
#endif
		WARN_ON(atbm_upload_beacon(priv));
		WARN_ON(atbm_update_beaconing(priv));
	}
#ifdef CONFIG_ATBM_SUPPORT_IBSS
	if (changed & BSS_CHANGED_IBSS)
	{
		priv->beacon_int = info->beacon_int;
		atbm_upload_beacon(priv);
		WARN_ON(atbm_update_beaconing(priv));
	}
#endif
	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		ap_printk( "BSS_CHANGED_BEACON_ENABLED dummy\n");
		if (priv->enable_beacon != info->enable_beacon) {
			priv->enable_beacon = info->enable_beacon;
		}
#if 0//ifdef CONFIG_ATBM_HE
		if ((priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP) && 
			vif->bss_conf.he_support &&
		    vif->bss_conf.he_oper.params) {
		    
			ret = wsm_vdev_set_param_cmd(ar, priv->if_id,
							    ATBM_HE_PARAM_HEOPS_0_31, vif->bss_conf.he_oper.params);

			if (ret){
				ap_printk( "Failed to set he oper params %x for VDEV %d: %i\n",
					    vif->bss_conf.he_oper.params, priv->if_id, ret);
			}
		}
#endif //CONFIG_ATBM_HE
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		ap_printk( "CHANGED_BEACON_INT\n");
		/* Restart AP only when connected */
		if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP)
		{
			WARN_ON(atbm_update_beaconing(priv));
		}
#ifdef CONFIG_ATBM_SUPPORT_IBSS
		else if (info->ibss_joined)
		{
			do_ibss_join = true;
		}
#endif
	}
#ifdef CONFIG_ATBM_SUPPORT_IBSS
	/* TODO: BSS_CHANGED_IBSS */
	if (do_ibss_join)
	{
		wsm_lock_tx(hw_priv);
		atbm_ibss_join_work(priv);
	}
	if (changed & BSS_CHANGED_IBSS)
	{
		if (!info->assoc) {
			wsm_lock_tx(hw_priv);
			atbm_ibss_join_work(priv);
		}
	}
#endif
	if (changed &
	    (BSS_CHANGED_ASSOC |
	     BSS_CHANGED_BASIC_RATES |
	     BSS_CHANGED_ERP_PREAMBLE |
	     BSS_CHANGED_HT |
	     BSS_CHANGED_ERP_SLOT |
	     BSS_CHANGED_IBSS)) {
		
		atbm_printk_ap("BSS_CHANGED_ASSOC.changed(%x)\n",changed);
		if (info->assoc || info->ibss_joined) { /* TODO: ibss_joined */
			struct ieee80211_sta *sta = NULL;
			int i = 0;
			if (info->dtim_period)
				priv->join_dtim_period = info->dtim_period;
			priv->beacon_int = info->beacon_int;

			rcu_read_lock();
			if (info->bssid && !info->ibss_joined)
				sta = ieee80211_find_sta(vif, info->bssid);
			if (sta) {
				/* TODO:COMBO:Change this once
				* mac80211 changes are available */
				//printk("%s:channel_type(%d),changed(%x)\n",__func__,info->channel_type,changed);
				BUG_ON(!hw_priv->channel);
				hw_priv->ht_info.ht_cap = sta->ht_cap;
#ifdef CONFIG_ATBM_HE
				hw_priv->he_info.he_cap = sta->he_cap;
#endif  //CONFIG_ATBM_HE
				priv->bss_params.operationalRateSet =
					__cpu_to_le32(atbm_rate_mask_to_wsm(hw_priv,sta->supp_rates[hw_priv->channel->band]));
				hw_priv->ht_info.channel_type =	info->channel_type;
				hw_priv->ht_info.operation_mode =	info->ht_operation_mode;
			} else {
				memset(&hw_priv->ht_info, 0,sizeof(hw_priv->ht_info));
#ifdef CONFIG_ATBM_HE
				memset(&hw_priv->he_info, 0,sizeof(hw_priv->he_info));
#endif  //CONFIG_ATBM_HE
				priv->bss_params.operationalRateSet = -1;
			}
			
			priv->htcap = (sta && atbm_is_ht(&hw_priv->ht_info));
			priv->association_mode.greenfieldMode =
				atbm_ht_greenfield(&hw_priv->ht_info);
			priv->association_mode.flags =
				WSM_ASSOCIATION_MODE_SNOOP_ASSOC_FRAMES |
				WSM_ASSOCIATION_MODE_USE_PREAMBLE_TYPE |
				WSM_ASSOCIATION_MODE_USE_HT_MODE |
				WSM_ASSOCIATION_MODE_USE_BASIC_RATE_SET |
				WSM_ASSOCIATION_MODE_USE_MPDU_START_SPACING;
			priv->association_mode.preambleType =
				info->use_short_preamble ?	WSM_JOIN_PREAMBLE_SHORT : WSM_JOIN_PREAMBLE_LONG;
#if defined(CONFIG_NL80211_TESTMODE) || defined(CONFIG_ATBM_IOCTRL)
			{
				extern int atbm_tool_use_short_preamble;
				atbm_tool_use_short_preamble = priv->association_mode.preambleType;
			}
#endif
			priv->association_mode.basicRateSet = __cpu_to_le32(
				atbm_rate_mask_to_wsm(hw_priv,
				info->basic_rates));
			priv->association_mode.mpduStartSpacing =
				atbm_ht_ampdu_density(&hw_priv->ht_info);

			priv->cqm_beacon_loss_count =	info->cqm_beacon_miss_thold;
			priv->cqm_tx_failure_thold = info->cqm_tx_fail_thold;
			priv->cqm_tx_failure_count = 0;
			priv->bss_params.beaconLostCount =
					priv->cqm_beacon_loss_count ?
					priv->cqm_beacon_loss_count :
					priv->cqm_link_loss_count;

			priv->bss_params.aid = info->aid;
#ifdef CONFIG_ATBM_HE		
			if(info->he_support){
				priv->association_mode.flags |= WSM_ASSOCIATION_MODE_USE_HE_MODE;
				priv->bss_params.he_support_flag = F_HE_SUPPORT_ENABLE;
				if(changed & BSS_CHANGED_HE_BSS_COLOR){
					priv->bss_params.color= info->he_bss_color.color;
					priv->bss_params.he_support_flag |= info->he_bss_color.enabled?F_HE_COLOR_ENABLE:0;
					atbm_printk_err("BssidColor[%d]\n",priv->bss_params.color);
				}				
				
				if (info->uora_exists) {
					priv->bss_params.he_support_flag |= F_HE_UORA_ENABLE;				
					priv->bss_params.uora_ocw_range = info->uora_ocw_range;
				}
				
				if (info->nontransmitted) {
					priv->bss_params.he_support_flag |= F_HE_MUL_BSSID_ENABLE;
					memcpy(priv->bss_params.transmitter_bssid,info->transmitter_bssid,6);
					priv->bss_params.max_bssid_indicator = info->bssid_indicator ;
					priv->bss_params.bssid_index 		 = info->bssid_index;
					priv->bss_params.ema_ap 			 = info->ema_ap;
					priv->bss_params.profile_periodicity = info->profile_periodicity;
				}
				
				priv->bss_params.frame_time_rts_th = cpu_to_le16(info->frame_time_rts_th);
				/* HTC flags */
				if(sta){
					if (sta->he_cap.he_cap_elem.mac_cap_info[0] & ATBM_IEEE80211_HE_MAC_CAP0_HTC_HE)
						priv->bss_params.htc_flags |= cpu_to_le32(F_HTC_SUPPORT_HE_ENABLE);				
					if (sta->he_cap.he_cap_elem.mac_cap_info[2] & ATBM_IEEE80211_HE_MAC_CAP2_BSR)
						priv->bss_params.htc_flags |= cpu_to_le32(F_HTC_SUPPORT_BSR_ENABLE);
					if (sta->he_cap.he_cap_elem.mac_cap_info[3] & ATBM_IEEE80211_HE_MAC_CAP3_OMI_CONTROL)
						priv->bss_params.htc_flags |= cpu_to_le32(F_HTC_SUPPORT_OMI_ENABLE);
					if (sta->he_cap.he_cap_elem.mac_cap_info[4] & ATBM_IEEE80211_HE_MAC_CAP4_BQR)
						priv->bss_params.htc_flags |= cpu_to_le32(F_HTC_SUPPORT_BQR_ENABLE);
				}


				/*
				 * Initialize the PPE thresholds to "None" (7), as described in Table
				 * 9-262ac of 80211.ax/D3.0.
				 */
				memset(&priv->bss_params.pkt_ext, 7, sizeof(priv->bss_params.pkt_ext));
				
				if(sta){
					/* If PPE Thresholds exist, parse them  */
					if (sta->he_cap.he_cap_elem.phy_cap_info[6] &
						ATBM_IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) {
						u8 nss = (sta->he_cap.ppe_thres[0] &  ATBM_IEEE80211_PPE_THRES_NSS_MASK) + 1;
						u8 ru_index_bitmap = (sta->he_cap.ppe_thres[0] &
							 ATBM_IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK) >>ATBM_IEEE80211_PPE_THRES_RU_INDEX_BITMASK_POS;
						u8 *ppe = &sta->he_cap.ppe_thres[0];
						u8 ppe_pos_bit = 7; /* Starting after PPE header */
					
						/*
						 * FW currently supports only nss == MAX_HE_SUPP_NSS
						 *
						 * If nss > MAX: we can ignore values we don't support
						 * If nss < MAX: we can set zeros in other streams
						 */
						if (nss > MAX_HE_SUPP_NSS) {
							ap_printk("Got NSS = %d - trimming to %d\n", nss,MAX_HE_SUPP_NSS);
							nss = MAX_HE_SUPP_NSS;
						}
					
						for (i = 0; i < nss; i++) {
							u8 ru_index_tmp = ru_index_bitmap << 1;
							u8 bw;
					
							for (bw = 0; bw < MAX_HE_CHANNEL_BW_INDX; bw++) {
								ru_index_tmp >>= 1;
								if (!(ru_index_tmp & 1))
									continue;
					
								priv->bss_params.pkt_ext.pkt_ext_qam_th[i][bw][1] = atbm_he_get_ppe_val(ppe, ppe_pos_bit);
								ppe_pos_bit += ATBM_IEEE80211_PPE_THRES_INFO_PPET_SIZE;
								priv->bss_params.pkt_ext.pkt_ext_qam_th[i][bw][0] =	atbm_he_get_ppe_val(ppe, ppe_pos_bit);
								ppe_pos_bit += ATBM_IEEE80211_PPE_THRES_INFO_PPET_SIZE;
							}
						}
					
						priv->bss_params.he_support_flag  |= F_HE_MUL_PACKET_EXT;
					} else if ((sta->he_cap.he_cap_elem.phy_cap_info[9] &
							ATBM_IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_MASK) !=
						  ATBM_IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_RESERVED) {
						int low_th = -1;
						int high_th = -1;
					
						/* Take the PPE thresholds from the nominal padding info */
						switch (sta->he_cap.he_cap_elem.phy_cap_info[9] &
							ATBM_IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_MASK) {
						case ATBM_IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_0US:
							low_th = ATBM_HE_PKT_EXT_NONE;
							high_th = ATBM_HE_PKT_EXT_NONE;
							break;
						case ATBM_IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_8US:
							low_th = ATBM_HE_PKT_EXT_BPSK;
							high_th = ATBM_HE_PKT_EXT_NONE;
							break;
						case ATBM_IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US:
							low_th = ATBM_HE_PKT_EXT_NONE;
							high_th = ATBM_HE_PKT_EXT_BPSK;
							break;
						}
					
						/* Set the PPE thresholds accordingly */
						if (low_th >= 0 && high_th >= 0) {
							struct atbm_he_pkt_ext *pkt_ext =(struct atbm_he_pkt_ext *)&priv->bss_params.pkt_ext;
					
							for (i = 0; i < MAX_HE_SUPP_NSS; i++) {
								u8 bw;
					
								for (bw = 0; bw < MAX_HE_CHANNEL_BW_INDX; bw++) {
									pkt_ext->pkt_ext_qam_th[i][bw][0] =	low_th;
									pkt_ext->pkt_ext_qam_th[i][bw][1] =	high_th;
								}
							}
					
							priv->bss_params.he_support_flag |= F_HE_MUL_PACKET_EXT;
						}
					}

					if ((sta->he_cap.he_cap_elem.phy_cap_info[3] & ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK)) {
						//priv->bss_params.he_support_flag |= F_HE_SU_4XLTF_08US;		
						if ((sta->he_cap.he_cap_elem.phy_cap_info[3] & ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK) == ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK) {
							atbm_printk_mgmt("[bss] DCM_support ap tx MCS0\n");
						}
						else if ((sta->he_cap.he_cap_elem.phy_cap_info[3] & ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_MASK) == ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK) {
							atbm_printk_mgmt("[bss] DCM_support ap tx MCS0/1\n");
						}
						else {
							atbm_printk_mgmt("[bss] DCM_support ap tx MCS0/1/3/4\n");
						}

					}
					else {
						atbm_printk_mgmt("bss ap no tx dcm\n");
					}

             
					
					if ((sta->he_cap.he_cap_elem.phy_cap_info[7] & ATBM_IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI)) {
						priv->bss_params.he_support_flag |= F_HE_SU_4XLTF_08US;
					}
					if ((sta->he_cap.he_cap_elem.phy_cap_info[8] & ATBM_IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI)) {
						priv->bss_params.he_support_flag |= F_HE_ER_4XLTF_08US;
					}
				}


				atbm_printk_err("[STA] color %d, he_support_flag: %x\n", priv->bss_params.color, priv->bss_params.he_support_flag);
			}
			else {				
				priv->bss_params.he_support_flag= 0;
			}
			//because some wifi6 AP, build ampdu too big, our chip rx buffer will buffer full 
			// we don't know why ampdu_factor in wifi6 AP not care this ;
			// so set RX BA windows == 32
			// && in wifi4 AP set rx BA win to 32 , rx throughput will not bad ,
			if(info->is_wifi6_ap){
				priv->association_mode.flags |= WSM_ASSOCIATION_MODE_USE_SHORT_BA_WINDOW;
#ifdef CONFIG_ATBM_HE
				if(priv->bss_params.he_support_flag){
					atbm_printk_err("==>connect wifi6 AP open AX, set BA WIN short\n");
				}
				else {				
					atbm_printk_err("==>connect, wifi6 AP but not open AX, set BA WIN short\n");
				}
#endif
			}
			else {
				atbm_printk_err( "[STA] not wifi6 AP\n");
			}
#endif  //CONFIG_ATBM_HE			
			if (priv->join_dtim_period < 1)
				priv->join_dtim_period = 1;
			ap_printk( "[STA] DTIM %d, interval: %d\n",
				priv->join_dtim_period, priv->beacon_int);
			ap_printk("[STA] Preamble: %d, " \
				"Greenfield: %d, Aid: %d, " \
				"Rates: 0x%.8X, Basic: 0x%.8X,mpduStartSpacing %d\n",
				priv->association_mode.preambleType,
				priv->association_mode.greenfieldMode,
				priv->bss_params.aid,
				priv->bss_params.operationalRateSet,
				priv->association_mode.basicRateSet,
				priv->association_mode.mpduStartSpacing);
			rcu_read_unlock();
			
			WARN_ON(wsm_set_association_mode(hw_priv,&priv->association_mode, priv->if_id));
			if (!info->ibss_joined)
			{
				WARN_ON(wsm_keep_alive_period(hw_priv,
					ATBM_APOLLO_KEEP_ALIVE_PERIOD /* sec */,
					priv->if_id));
				WARN_ON(wsm_set_bss_params(hw_priv, &priv->bss_params,priv->if_id));
				priv->setbssparams_done = true;
				WARN_ON(wsm_set_beacon_wakeup_period(hw_priv,
					    priv->beacon_int * priv->join_dtim_period >
					    MAX_BEACON_SKIP_TIME_MS ? 1 : priv->join_dtim_period,
					    0, priv->if_id));
				atbm_printk_ap("priv->htcap(%d)\n",priv->htcap);
				if (priv->htcap) {
					wsm_lock_tx(hw_priv);
					/* Statically enabling block ack for TX/RX */
					WARN_ON(wsm_set_block_ack_policy(hw_priv,hw_priv->ba_tid_tx_mask, hw_priv->ba_tid_rx_mask,priv->if_id));
					wsm_unlock_tx(hw_priv);
				}
				if(info->ps_enabled)
					priv->powersave_mode.pmMode = pmMode;
				else
					priv->powersave_mode.pmMode = WSM_PSM_ACTIVE;
				atbm_set_pm(priv, &priv->powersave_mode);
			}
#ifdef CONFIG_ATBM_SUPPORT_P2P_POWERSAVE
			if (priv->vif->p2p) {
				ap_printk(
					"[STA] Setting p2p powersave "
					"configuration.\n");
				WARN_ON(wsm_set_p2p_ps_modeinfo(hw_priv,&priv->p2p_ps_modeinfo, priv->if_id));
				atbm_notify_noa(priv, ATBM_APOLLO_NOA_NOTIFICATION_DELAY);
			}
#endif //CONFIG_ATBM_SUPPORT_P2P
			if (priv->mode == NL80211_IFTYPE_STATION)
				WARN_ON(atbm_upload_qosnull(priv));
#ifdef CONFIG_ATBM_BT_COMB
			if (hw_priv->is_BT_Present)
				WARN_ON(atbm_set_btcoexinfo(priv));
#endif //CONFIG_ATBM_BT_COMB
		} else {
			memset(&priv->association_mode, 0,
				sizeof(priv->association_mode));
			memset(&priv->bss_params, 0, sizeof(priv->bss_params));
		}
	}
	
	if(BSS_CHANGED_STA_DTIM & changed){
		priv->join_dtim_period = info->dtim_period;
		WARN_ON(wsm_set_beacon_wakeup_period(hw_priv,
				    priv->beacon_int * priv->join_dtim_period >
				    MAX_BEACON_SKIP_TIME_MS ? 1 :
				    priv->join_dtim_period, 0, priv->if_id));
	}
	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_ERP_CTS_PROT)) {
		u32 prev_erp_info = priv->erp_info;

		if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP) {
			if (info->use_cts_prot)
				priv->erp_info |= WLAN_ERP_USE_PROTECTION;
			else if (!(prev_erp_info & WLAN_ERP_NON_ERP_PRESENT))
				priv->erp_info &= ~WLAN_ERP_USE_PROTECTION;

			if (prev_erp_info != priv->erp_info){
				u8 erp_ie[3] = {ATBM_WLAN_EID_ERP_INFO, 0x1, 0};
				struct wsm_update_ie update_ie = {
					.what = WSM_UPDATE_IE_BEACON,
					.count = 1,
					.ies = erp_ie,
					.length = 3,
				};
				u32 erp_info;
				__le32 use_cts_prot;
				
				erp_info = priv->erp_info;
				use_cts_prot =
					erp_info & WLAN_ERP_USE_PROTECTION ?
					__cpu_to_le32(1) : 0;

				erp_ie[ERP_INFO_BYTE_OFFSET] = erp_info;

				ap_printk("[STA] ERP information 0x%x\n", erp_info);

				/* TODO:COMBO: If 2 interfaces are on the same channel they share
				the same ERP values */
				WARN_ON(wsm_write_mib(hw_priv, WSM_MIB_ID_NON_ERP_PROTECTION,
							&use_cts_prot, sizeof(use_cts_prot),priv->if_id));
#if defined(CONFIG_NL80211_TESTMODE) || defined(CONFIG_ATBM_IOCTRL)
				{
					extern int atbm_tool_use_cts_prot;
					atbm_tool_use_cts_prot = use_cts_prot;
				}
#endif
				WARN_ON(wsm_update_ie(hw_priv, &update_ie, priv->if_id));
			}
		}
	}

	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_ERP_SLOT)) {
		__le32 slot_time = info->use_short_slot ?
			__cpu_to_le32(9) : __cpu_to_le32(20);
		ap_printk( "[STA] Slot time :%d us.\n",
			__le32_to_cpu(slot_time));

		//add by wp fix PHICOMM AP4  long slot  bug
		if(priv->htcap){
			 slot_time = 	__cpu_to_le32(9);
		}
		atbm_printk_ap( "[STA] Slot time :%d us.\n",__le32_to_cpu(slot_time));
		WARN_ON(wsm_write_mib(hw_priv, WSM_MIB_ID_DOT11_SLOT_TIME,
			&slot_time, sizeof(slot_time), priv->if_id));
		
#if defined(CONFIG_NL80211_TESTMODE) || defined(CONFIG_ATBM_IOCTRL)
		{
			extern int atbm_tool_use_short_slot;
			atbm_tool_use_short_slot = (slot_time == __cpu_to_le32(9))? 1:0;
		}
#endif
	}
#ifdef CONFIG_ATBM_SUPPORT_RCPI_RSSI
	if (changed & (BSS_CHANGED_ASSOC | BSS_CHANGED_CQM)) {
		struct wsm_rcpi_rssi_threshold threshold = {
			.rollingAverageCount = 8,
		};

		ap_printk( "[CQM] RSSI threshold "
			"subscribe: %d +- %d\n",
			info->cqm_rssi_thold, info->cqm_rssi_hyst);
		ap_printk( "[CQM] Beacon loss subscribe: %d\n",
			info->cqm_beacon_miss_thold);
		ap_printk( "[CQM] TX failure subscribe: %d\n",
			info->cqm_tx_fail_thold);
		priv->cqm_rssi_thold = info->cqm_rssi_thold;
		priv->cqm_rssi_hyst = info->cqm_rssi_hyst;
		if (info->cqm_rssi_thold || info->cqm_rssi_hyst) {
			/* RSSI subscription enabled */
			/* TODO: It's not a correct way of setting threshold.
			 * Upper and lower must be set equal here and adjusted
			 * in callback. However current implementation is much
			 * more relaible and stable. */
			if (priv->cqm_use_rssi) {
				threshold.upperThreshold =	info->cqm_rssi_thold +	info->cqm_rssi_hyst;
				threshold.lowerThreshold =	info->cqm_rssi_thold;
			} else {
				/* convert RSSI to RCPI
				 * RCPI = (RSSI + 110) * 2 */
				threshold.upperThreshold =(info->cqm_rssi_thold + info->cqm_rssi_hyst + 110) * 2;
				threshold.lowerThreshold =(info->cqm_rssi_thold + 110) * 2;
			}
			threshold.rssiRcpiMode |= WSM_RCPI_RSSI_THRESHOLD_ENABLE;
		} else {
			/* There is a bug in FW, see sta.c. We have to enable
			 * dummy subscription to get correct RSSI values. */
			threshold.rssiRcpiMode |=
				WSM_RCPI_RSSI_THRESHOLD_ENABLE |
				WSM_RCPI_RSSI_DONT_USE_UPPER |
				WSM_RCPI_RSSI_DONT_USE_LOWER;
		}
		WARN_ON(wsm_set_rcpi_rssi_threshold(hw_priv, &threshold,priv->if_id));

		priv->cqm_tx_failure_thold = info->cqm_tx_fail_thold;
		priv->cqm_tx_failure_count = 0;

		if (priv->cqm_beacon_loss_count !=	info->cqm_beacon_miss_thold) {
			priv->cqm_beacon_loss_count =	info->cqm_beacon_miss_thold;
			priv->bss_params.beaconLostCount =	priv->cqm_beacon_loss_count ?
				priv->cqm_beacon_loss_count :
				priv->cqm_link_loss_count;
			/* Make sure we are associated before sending
			 * set_bss_params to firmware */
			if (priv->bss_params.aid) {
				WARN_ON(wsm_set_bss_params(hw_priv, &priv->bss_params, priv->if_id));
				priv->setbssparams_done = true;
			}
		}
	}
#endif
	if (changed & BSS_CHANGED_PS) {
		
		if (info->ps_enabled == false){
			priv->powersave_mode.pmMode = WSM_PSM_ACTIVE;
		}else {
			priv->powersave_mode.pmMode = pmMode;
		}

		ap_printk( "[STA] Aid: %d, Joined: %s, Powersave: %s\n",
			priv->bss_params.aid,
			priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA ? "yes" : "no",
			priv->powersave_mode.pmMode == WSM_PSM_ACTIVE ? "WSM_PSM_ACTIVE" :
			priv->powersave_mode.pmMode == WSM_PSM_PS ? "WSM_PSM_PS" :
			priv->powersave_mode.pmMode == WSM_PSM_FAST_PS ? "WSM_PSM_FAST_PS" :
			"UNKNOWN");

		/* Firmware requires that value for this 1-byte field must
		 * be specified in units of 500us. Values above the 128ms
		 * threshold are not supported. */
		if (info->dynamic_ps_timeout >= 0x80)
			priv->powersave_mode.fastPsmIdlePeriod = 0xFF;
		else
			priv->powersave_mode.fastPsmIdlePeriod =
					info->dynamic_ps_timeout << 1;
		
		if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA &&
				priv->bss_params.aid &&
				priv->setbssparams_done &&
#ifdef CONFIG_ATBM_LMAC_FILTER_IP_FRAME
				priv->filter4.enable
#else
				1
#endif
				){
			atbm_printk_err("%s %d:pmMode = %d \n",__func__,__LINE__,priv->powersave_mode.pmMode);
			atbm_set_pm(priv, &priv->powersave_mode);
		}
		
	}
#ifdef CONFIG_ATBM_SUPPORT_P2P_POWERSAVE
	if (changed & BSS_CHANGED_P2P_PS) {
		struct wsm_p2p_ps_modeinfo *modeinfo;
		modeinfo = &priv->p2p_ps_modeinfo;
		ap_printk( "[AP] BSS_CHANGED_P2P_PS\n");
		ap_printk( "[AP] Legacy PS: %d for AID %d "
			"in %d mode.\n", info->p2p_ps.legacy_ps,
			priv->bss_params.aid, priv->join_status);

		if (info->p2p_ps.legacy_ps >= 0) {
			if (info->p2p_ps.legacy_ps > 0)
				priv->powersave_mode.pmMode = WSM_PSM_PS;
			else
				priv->powersave_mode.pmMode = WSM_PSM_ACTIVE;

			if(info->p2p_ps.ctwindow && info->p2p_ps.opp_ps)
				priv->powersave_mode.pmMode = WSM_PSM_PS;
			if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA){
				atbm_printk_err("%s %d:pmMode = %d \n",__func__,__LINE__,priv->powersave_mode.pmMode);
				atbm_set_pm(priv, &priv->powersave_mode);
			}				
		}

		atbm_printk_ap("[AP] CTWindow: %d\n",info->p2p_ps.ctwindow);
		if (info->p2p_ps.ctwindow >= 128)
			modeinfo->oppPsCTWindow = 127;
		else if (info->p2p_ps.ctwindow >= 0)
			modeinfo->oppPsCTWindow = info->p2p_ps.ctwindow;

		atbm_printk_ap("[AP] Opportunistic: %d\n",
			info->p2p_ps.opp_ps);
		switch (info->p2p_ps.opp_ps) {
		case 0:
			modeinfo->oppPsCTWindow &= ~(BIT(7));
			break;
		case 1:
			modeinfo->oppPsCTWindow |= BIT(7);
			break;
		default:
			break;
		}

		atbm_printk_ap("[AP] NOA: %d, %d, %d, %d\n",
			info->p2p_ps.count,
			info->p2p_ps.start,
			info->p2p_ps.duration,
			info->p2p_ps.interval);
		/* Notice of Absence */
		modeinfo->count = info->p2p_ps.count;

		if (info->p2p_ps.count) {
			/* In case P2P_GO we need some extra time to be sure
			 * we will update beacon/probe_resp IEs correctly */
#define NOA_DELAY_START_MS	300
			if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP)
				modeinfo->startTime =
					__cpu_to_le32(info->p2p_ps.start +
						      NOA_DELAY_START_MS);
			else
				modeinfo->startTime =
					__cpu_to_le32(info->p2p_ps.start);
			modeinfo->duration =
				__cpu_to_le32(info->p2p_ps.duration);
			modeinfo->interval =
				 __cpu_to_le32(info->p2p_ps.interval);
			modeinfo->dtimCount = 1;
			modeinfo->reserved = 0;
		} else {
			modeinfo->dtimCount = 0;
			modeinfo->startTime = 0;
			modeinfo->reserved = 0;
			modeinfo->duration = 0;
			modeinfo->interval = 0;
		}

		if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA ||
		    priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP) {
			WARN_ON(wsm_set_p2p_ps_modeinfo(hw_priv, modeinfo,
				priv->if_id));
		}

		/* Temporary solution while firmware don't support NOA change
		 * notification yet */
		atbm_notify_noa(priv, 10);
	}
#endif


#ifdef CONFIG_ATBM_HE
	if (changed & BSS_CHANGED_TWT) {
		/*if (info->twt_requester || info->twt_responder)
			ath11k_wmi_send_twt_enable_cmd(ar, ar->pdev->pdev_id);
		else
			ath11k_wmi_send_twt_disable_cmd(ar, ar->pdev->pdev_id);*/
	}

	if (changed & BSS_CHANGED_HE_OBSS_PD){
		//ath11k_mac_config_obss_pd(ar, &info->he_obss_pd);
	}
	if ((changed & (BSS_CHANGED_ASSOC |BSS_CHANGED_HE_BSS_COLOR))== BSS_CHANGED_HE_BSS_COLOR){
		//if (vif->type == NL80211_IFTYPE_AP) {
			int ret = wsm_send_obss_color_collision_cfg_cmd(
				hw_priv, priv->if_id, info->he_bss_color.color,
				info->he_bss_color.enabled);	
		   atbm_printk_ap("set bss color if[%d]:color %d enable %d ret %d\n", priv->if_id,info->he_bss_color.color,info->he_bss_color.enabled,ret);
		
			if (ret) {
				atbm_printk_ap("failed to set bss color collision on vdev %i: %d\n", priv->if_id,ret);
			}
		//}
		//} else if (vif->type == NL80211_IFTYPE_STATION) {
		//	ret = wsm_send_obss_color_collision_cfg_cmd(hw_priv, priv->if_id,  0, 1);
		//	if (ret) {
		//		atbm_printk_ap("failed to set bss color collision on vdev %i: %d\n", priv->if_id,  ret);
		//	}
		//}
	}
#endif  //CONFIG_ATBM_HE
	mutex_unlock(&hw_priv->conf_mutex);

}

void atbm_multicast_start_work(struct atbm_work_struct *work)
{
	struct atbm_vif *priv =
		container_of(work, struct atbm_vif, multicast_start_work);
	long tmo = priv->join_dtim_period *
			(priv->beacon_int + 20) * HZ / 1024;

	atbm_hw_cancel_queue_work(&priv->multicast_stop_work,true);
	if(atbm_bh_is_term(priv->hw_priv)){
		return;
	}
	if (!priv->aid0_bit_set) {
		wsm_lock_tx(priv->hw_priv);
		atbm_set_tim_impl(priv, true);
		priv->aid0_bit_set = true;
		atbm_mod_timer(&priv->mcast_timeout, jiffies + tmo);
		wsm_unlock_tx(priv->hw_priv);
	}
}

void atbm_multicast_stop_work(struct atbm_work_struct *work)
{
	struct atbm_vif *priv =
		container_of(work, struct atbm_vif, multicast_stop_work);
	if(atbm_bh_is_term(priv->hw_priv)){
		return;
	}
	if (priv->aid0_bit_set) {
		atbm_del_timer_sync(&priv->mcast_timeout);
		wsm_lock_tx(priv->hw_priv);
		priv->aid0_bit_set = false;
		atbm_set_tim_impl(priv, false);
		wsm_unlock_tx(priv->hw_priv);
	}
}

void atbm_mcast_timeout(unsigned long arg)
{
	struct atbm_vif *priv =
		(struct atbm_vif *)arg;

	atbm_printk_warn("Multicast delivery timeout.\n");
	spin_lock_bh(&priv->ps_state_lock);
	priv->tx_multicast = priv->aid0_bit_set &&
			priv->buffered_multicasts;
	if (priv->tx_multicast)
		atbm_bh_wakeup(ABwifi_vifpriv_to_hwpriv(priv));
	else if(priv->aid0_bit_set == true){
		/*
		*Maybe all Sta have been in wake state,and bh has been send the Multicast
		*to all stations.so here need to clear the TIM
		*/
		atbm_printk_err("clear TIM\n");
		atbm_hw_priv_queue_work(priv->hw_priv,&priv->multicast_stop_work);
	}
	spin_unlock_bh(&priv->ps_state_lock);
}

int atbm_ampdu_action(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif,
			enum ieee80211_ampdu_mlme_action action,
			struct ieee80211_sta *sta, u16 tid, u16 *ssn,
			u8 buf_size,u8 hw_token)
{
	/* Aggregation is implemented fully in firmware,
	 * including block ack negotiation.
	 * In case of AMPDU aggregation in RX direction
	 * re-ordering of packets takes place on host. mac80211
	 * needs the ADDBA Request to setup reodering.mac80211 also
	 * sends ADDBA Response which is discarded in the driver as
	 * FW generates the ADDBA Response on its own.*/
	int ret;

	atbm_printk_err("AMPDU[%s]:action[%d],tid[%d],ssn[%d],buff_size[%d],token[%d],ta[%pM]\n",
			vif_to_sdata(vif)->name,action,tid,ssn ? *ssn:0,buf_size,hw_token,sta->addr);
	switch (action) {
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
	{
		struct wsm_rx_ba_session ba_session;

		ba_session.mode = action == IEEE80211_AMPDU_RX_START ? WSM_RX_BA_SESSION_MODE__ENABLE : WSM_RX_BA_SESSION_MODE__DISABLE;
		ba_session.win_size = buf_size;
		ba_session.tid = tid;
		ba_session.resv = 0;
		memcpy(ba_session.TA,sta->addr,ETH_ALEN);
		ba_session.ssn = ssn ? *ssn:0;
		ba_session.timeout = 0;
		ba_session.hw_token = hw_token;
		ba_session.resv2 = 0;
		/* Just return OK to mac80211 */
		ret = wsm_req_rx_ba_session((struct atbm_common *)hw->priv,&ba_session,ABwifi_get_vif_from_ieee80211(vif)->if_id);
		break;
	}
	case IEEE80211_AMPDU_TX_START:
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ret = 0;
		break;
	case IEEE80211_AMPDU_TX_STOP:
		ret = 0;
		break;
	default:
		ret = -ENOTSUPP;
	}
	return ret;
}

/* ******************************************************************** */
/* WSM callback								*/
void atbm_suspend_resume(struct atbm_vif *priv,
			   struct wsm_suspend_resume *arg)
{
	struct atbm_common *hw_priv =
		ABwifi_vifpriv_to_hwpriv(priv);
#if 0
	ap_printk( "[AP] %s: %s\n",
			arg->stop ? "stop" : "start",
			arg->multicast ? "broadcast" : "unicast");
#endif
	if (arg->multicast) {
		bool cancel_tmo = false;
		spin_lock_bh(&priv->ps_state_lock);
		if (arg->stop) {
			priv->tx_multicast = false;
		} else {
			/* Firmware sends this indication every DTIM if there
			 * is a STA in powersave connected. There is no reason
			 * to suspend, following wakeup will consume much more
			 * power than it could be saved. */
#ifdef CONFIG_PM
			atbm_pm_stay_awake(&hw_priv->pm_state,
					priv->join_dtim_period *
					(priv->beacon_int + 20) * HZ / 1024);
#endif
			priv->tx_multicast = priv->aid0_bit_set &&
					priv->buffered_multicasts;
			if (priv->tx_multicast) {
				cancel_tmo = true;
				atbm_bh_wakeup(hw_priv);
			}
		}
		spin_unlock_bh(&priv->ps_state_lock);
		if (cancel_tmo)
			atbm_del_timer_sync(&priv->mcast_timeout);
	} else {
		spin_lock_bh(&priv->ps_state_lock);
		atbm_ps_notify(priv, arg->link_id, arg->stop);
		spin_unlock_bh(&priv->ps_state_lock);
		if (!arg->stop)
			atbm_bh_wakeup(hw_priv);
	}
	return;
}

/* ******************************************************************** */
/* AP privates								*/

static int atbm_upload_beacon(struct atbm_vif *priv)
{
	int ret = 0;
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_BEACON,
	};
	struct atbm_ieee80211_mgmt *mgmt;
	u8 *erp_inf, *ies, *ht_info;
#ifdef CONFIG_ATBM_AP_CHANNEL_CHANGE_EVENT
	u8 *ds_params;
#endif
	u32 ies_len;
	if (priv->vif->p2p ||
			hw_priv->channel->band == IEEE80211_BAND_5GHZ)
		frame.rate = WSM_TRANSMIT_RATE_6;

	frame.skb = ieee80211_beacon_get(priv->hw, priv->vif);
	if (!frame.skb)
	{
		atbm_printk_err("upload_beacon NULL\n");
		return -ENOMEM;
	}
	mgmt = (void *)frame.skb->data;
	ies = mgmt->u.beacon.variable;
	ies_len = frame.skb->len - (u32)(ies - (u8 *)mgmt);
	atbm_printk_ap("atbm_clear_wpas_p2p_40M_ie\n");
	ht_info = (u8 *)atbm_ieee80211_find_ie( ATBM_WLAN_EID_HT_INFORMATION, ies, ies_len);
        if (ht_info) {
		/* Enable RIFS*/
		ht_info[3] |= 8;
        }
	erp_inf = (u8 *)atbm_ieee80211_find_ie(ATBM_WLAN_EID_ERP_INFO, ies, ies_len);
	if (erp_inf) {
		if (erp_inf[ERP_INFO_BYTE_OFFSET]
				& WLAN_ERP_BARKER_PREAMBLE)
			priv->erp_info |= WLAN_ERP_BARKER_PREAMBLE;
		else
			priv->erp_info &= ~WLAN_ERP_BARKER_PREAMBLE;

		if (erp_inf[ERP_INFO_BYTE_OFFSET]
				& WLAN_ERP_NON_ERP_PRESENT) {
			priv->erp_info |= WLAN_ERP_USE_PROTECTION;
			priv->erp_info |= WLAN_ERP_NON_ERP_PRESENT;
		} else {
			priv->erp_info &= ~WLAN_ERP_USE_PROTECTION;
			priv->erp_info &= ~WLAN_ERP_NON_ERP_PRESENT;
		}
	}
	
#ifdef CONFIG_ATBM_AP_CHANNEL_CHANGE_EVENT
	//change AP dsparam channel when ap & sta not at the same channel
	ds_params = (u8 *)atbm_ieee80211_find_ie( ATBM_WLAN_EID_DS_PARAMS, ies, ies_len);
	if (ds_params) {
		ds_params[2]= channel_hw_value(hw_priv->channel);
		atbm_printk_err("change ds_params channel %d \n",channel_hw_value(hw_priv->channel));
	}
#endif //#ifdef CONFIG_ATBM_AP_CHANNEL_CHANGE_EVENT

#ifdef HIDDEN_SSID
	if (priv->hidden_ssid) {
		u8 *ssid_ie;
		u8 ssid_len;

		atbm_printk_ap("hidden_ssid set\n");
		ssid_ie = (u8 *)atbm_ieee80211_find_ie(ATBM_WLAN_EID_SSID, ies, ies_len);
		WARN_ON(!ssid_ie);
		ssid_len = ssid_ie[1];
		if (ssid_len) {
			atbm_printk_ap("%s: hidden_ssid with zero content "
					"ssid\n", __func__);
			ssid_ie[1] = 0;
			memmove(ssid_ie + 2, ssid_ie + 2 + ssid_len,
					(ies + ies_len -
					 (ssid_ie + 2 + ssid_len)));
			frame.skb->len -= ssid_len;
		} else {
			atbm_printk_ap("%s: hidden ssid with ssid len 0"
					" not supported", __func__);
			atbm_dev_kfree_skb(frame.skb);
			return -1;
		}
	}
#endif
	ret = wsm_set_template_frame(hw_priv, &frame, priv->if_id);
	if (!ret) {
#ifdef ATBM_PROBE_RESP_EXTRA_IE
		if(priv->mode == NL80211_IFTYPE_AP) {
		ret = atbm_upload_proberesp(priv);
			goto __exit_up_beacon;
		}
#else
		/* TODO: Distille probe resp; remove TIM
		 * and other beacon-specific IEs */
		*(__le16 *)frame.skb->data =
			__cpu_to_le16(IEEE80211_FTYPE_MGMT |
				      IEEE80211_STYPE_PROBE_RESP);
		frame.frame_type = WSM_FRAME_TYPE_PROBE_RESPONSE;
		/* TODO: Ideally probe response template should separately
		   configured by supplicant through openmac. This is a
		   temporary work-around known to fail p2p group info
		   attribute related tests
		   */
			ret = wsm_set_template_frame(hw_priv, &frame,
					priv->if_id);
			WARN_ON(wsm_set_probe_responder(priv, false));
#endif
	}
__exit_up_beacon:
	atbm_dev_kfree_skb(frame.skb);

	return ret;
}

#ifdef ATBM_PROBE_RESP_EXTRA_IE
static int atbm_upload_proberesp(struct atbm_vif *priv)
{
	int ret = 0;
//	struct ieee80211_bss_conf *conf = &priv->vif->bss_conf;	
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PROBE_RESPONSE,
	};
#ifdef HIDDEN_SSID
	u8 *ssid_ie;
#endif
	if (priv->vif->p2p || hw_priv->channel->band == IEEE80211_BAND_5GHZ)
		frame.rate = WSM_TRANSMIT_RATE_6;

	frame.skb = ieee80211_proberesp_get(priv->hw, priv->vif);
	if (WARN_ON(!frame.skb))
		return -ENOMEM;
#ifdef HIDDEN_SSID
	if (priv->hidden_ssid) {
		int offset;
		u8 ssid_len;
		/* we are assuming beacon from upper layer will always contain
		   zero filled ssid for hidden ap. The beacon shall never have
		   ssid len = 0.
		  */

		offset = offsetof(struct atbm_ieee80211_mgmt, u.probe_resp.variable);
		ssid_ie = (u8*)atbm_ieee80211_find_ie(ATBM_WLAN_EID_SSID,
				frame.skb->data + offset,
				frame.skb->len - offset);
		ssid_len = ssid_ie[1];
		if (ssid_len && (ssid_len == priv->ssid_length)) {
			memcpy(ssid_ie + 2, priv->ssid, ssid_len);
		} else {
			atbm_printk_err("hidden ssid with mismatched ssid_len %d\n",ssid_len);
			atbm_dev_kfree_skb(frame.skb);
			return -1;
		}
	}
#endif
	ret = wsm_set_template_frame(hw_priv, &frame,  priv->if_id);
	WARN_ON(wsm_set_probe_responder(priv, false));

	atbm_dev_kfree_skb(frame.skb);

	return ret;
}
#endif

int atbm_upload_beacon_private(struct atbm_vif *priv)
{
	return atbm_upload_beacon(priv);
}
int atbm_upload_proberesp_private(struct atbm_vif *priv)
{
#ifdef ATBM_PROBE_RESP_EXTRA_IE
	return atbm_upload_proberesp(priv);
#else
	return 0;
#endif
}


#ifdef CONFIG_ATBM_BT_COMB
static int atbm_upload_pspoll(struct atbm_vif *priv)
{
	int ret = 0;
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PS_POLL,
		.rate = 0xFF,
	};


	frame.skb = ieee80211_pspoll_get(priv->hw, priv->vif);
	if (WARN_ON(!frame.skb))
		return -ENOMEM;

	ret = wsm_set_template_frame(ABwifi_vifpriv_to_hwpriv(priv), &frame,
				priv->if_id);

	atbm_dev_kfree_skb(frame.skb);

	return ret;
}

static int atbm_upload_null(struct atbm_vif *priv)
{
	int ret = 0;
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_NULL,
		.rate = 0xFF,
	};


	frame.skb = ieee80211_nullfunc_get(priv->hw, priv->vif);
	if (WARN_ON(!frame.skb))
		return -ENOMEM;

	ret = wsm_set_template_frame(ABwifi_vifpriv_to_hwpriv(priv),
				&frame, priv->if_id);

	atbm_dev_kfree_skb(frame.skb);

	return ret;
}
#endif



static int atbm_upload_qosnull(struct atbm_vif *priv)
{
	int ret = 0;
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_QOS_NULL,
		.rate = 0xFF,
	};


	frame.skb = ieee80211_qosnullfunc_get(priv->hw, priv->vif);
	if (WARN_ON(!frame.skb))
		return -ENOMEM;

	ret = wsm_set_template_frame(hw_priv, &frame, priv->if_id);

	atbm_dev_kfree_skb(frame.skb);

	return ret;
}


static int atbm_start_ap(struct atbm_vif *priv)
{
	int ret;
#ifndef HIDDEN_SSID
	const u8 *ssidie;
	struct sk_buff *skb;
	int offset;
#endif
	struct ieee80211_bss_conf *conf = &priv->vif->bss_conf;
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	struct wsm_start start = {
		.mode = priv->vif->p2p ?
				WSM_START_MODE_P2P_GO : WSM_START_MODE_AP,
		/* TODO:COMBO:Change once mac80211 support is available */
		.band = (hw_priv->channel->band == IEEE80211_BAND_5GHZ) ?
				WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G,
		.channelNumber = channel_hw_value(hw_priv->channel),
		.beaconInterval = conf->beacon_int,
		.DTIMPeriod = conf->dtim_period,
		.preambleType = conf->use_short_preamble ?
				WSM_JOIN_PREAMBLE_SHORT :
				WSM_JOIN_PREAMBLE_LONG,
		.probeDelay = 100,
		.basicRateSet = atbm_rate_mask_to_wsm(hw_priv,
				conf->basic_rates),
		.CTWindow = 0,
	};

	struct wsm_inactivity inactivity = {
		.min_inactivity = 40,//50,
		.max_inactivity = 10,//10,
	};

	struct wsm_operational_mode mode = {
		.power_mode = wsm_power_mode_quiescent,
		.disableMoreFlagUsage = true,
	};

	if (priv->if_id)
		start.mode |= WSM_FLAG_MAC_INSTANCE_1;
	else
		start.mode &= ~WSM_FLAG_MAC_INSTANCE_1;
#ifdef CONFIG_ATBM_SUPPORT_P2P	
	if(priv->vif->p2p){
		inactivity.min_inactivity = 15;
		inactivity.max_inactivity = 5;
	}
#endif
	hw_priv->connected_sta_cnt = 0;

#ifndef HIDDEN_SSID
	/* Get SSID */
	skb = ieee80211_beacon_get(priv->hw, priv->vif);
	if (WARN_ON(!skb))
		return -ENOMEM;

	offset = offsetof(struct atbm_ieee80211_mgmt, u.beacon.variable);
	ssidie = atbm_ieee80211_find_ie(ATBM_WLAN_EID_SSID, skb->data + offset,
				  skb->len - offset);

	memset(priv->ssid, 0, sizeof(priv->ssid));
	if (ssidie) {
		priv->ssid_length = ssidie[1];
		if (WARN_ON(priv->ssid_length > sizeof(priv->ssid)))
			priv->ssid_length = sizeof(priv->ssid);
		memcpy(priv->ssid, &ssidie[2], priv->ssid_length);
	} else {
		priv->ssid_length = 0;
	}
	atbm_dev_kfree_skb(skb);
#endif

	priv->beacon_int = conf->beacon_int;
	priv->join_dtim_period = conf->dtim_period;

	start.ssidLength = priv->ssid_length;
	memcpy(&start.ssid[0], priv->ssid, start.ssidLength);

	memset(&priv->link_id_db, 0, sizeof(priv->link_id_db));
	
	start.channel_type = (u32)(vif_chw(priv->vif));
	if(start_choff<=NL80211_CHAN_HT40PLUS){
		atbm_printk_ap("%s:fix chatype(%d)\n",__func__,start_choff);
		start.channel_type = start_choff;
	}
	
	atbm_printk_ap("%s:start.channel_type,(%d),channelNumber(%d)\n",__func__,start.channel_type,start.channelNumber);

	atbm_printk_ap("[AP] ch: %d(%d), bcn: %d(%d), "
		"brt: 0x%.8X, ssid: %.*s.\n",
		start.channelNumber, start.band,
		start.beaconInterval, start.DTIMPeriod,
		start.basicRateSet,
		start.ssidLength, start.ssid);
	ret = WARN_ON(wsm_start(hw_priv, &start, priv->if_id));
#ifdef CONFIG_ATBM_SUPPORT_P2P_POWERSAVE
	if (!ret && priv->vif->p2p) {
		ap_printk(
			"[AP] Setting p2p powersave "
			"configuration.\n");
		WARN_ON(wsm_set_p2p_ps_modeinfo(hw_priv,
			&priv->p2p_ps_modeinfo, priv->if_id));
		atbm_notify_noa(priv, ATBM_APOLLO_NOA_NOTIFICATION_DELAY);
	}
#endif
	/*Set Inactivity time*/
	 if(!(strstr(&start.ssid[0], "6.1.12"))) {
		wsm_set_inactivity(hw_priv, &inactivity, priv->if_id);
	    }
	if (!ret) {
		WARN_ON(wsm_set_block_ack_policy(hw_priv,
			hw_priv->ba_tid_tx_mask,
			hw_priv->ba_tid_rx_mask,
			priv->if_id));

		priv->join_status = ATBM_APOLLO_JOIN_STATUS_AP;
		/* atbm_update_filtering(priv); */
	}
	WARN_ON(wsm_set_operational_mode(hw_priv, &mode, priv->if_id));
	//use when wep share key,need firmware not enc auth frame
	if((priv->cipherType == WLAN_CIPHER_SUITE_WEP40)
		|| 	(priv->cipherType == WLAN_CIPHER_SUITE_WEP104)){
		struct wsm_protected_mgmt_policy mgmt_policy;
		mgmt_policy.protectedMgmtEnable = 0;
		mgmt_policy.unprotectedMgmtFramesAllowed = 1;
		mgmt_policy.encryptionForAuthFrame = 1;//not enc for hw
		wsm_set_protected_mgmt_policy(hw_priv, &mgmt_policy,
						  priv->if_id);
	}
#ifdef	ATBM_WIFI_QUEUE_LOCK_BUG
	atbm_set_priv_queue_cap(priv);
#endif
	atbm_printk_ap("AP/GO mode ,cipherType %x\n",priv->cipherType);

#if 0//def CONFIG_IEEE80211_SPECIAL_FILTER
	
	 atbm_printk_err("\n%s set special filter action 0x40\n",__func__);
	 struct ieee80211_sub_if_data *sdata = vif_to_sdata(priv->vif);
	 struct ieee80211_special_filter filter;
	 filter.filter_action = 0x40;
	 filter.flags = SPECIAL_F_FLAGS_FRAME_TYPE;
	 ieee80211_special_filter_clear(sdata);
	 if(ieee80211_special_filter_register(sdata,&filter) == false)
			 atbm_printk_err("%s() ieee80211_special_filter_register() ## filter probe 	 req set fail! \n",__func__);
#endif	

	return ret;
}

static int atbm_update_beaconing(struct atbm_vif *priv)
{
	struct ieee80211_bss_conf *conf = &priv->vif->bss_conf;
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
	struct wsm_reset reset = {
		.link_id = 0,
		.reset_statistics = true,
	};

	if (priv->mode == NL80211_IFTYPE_AP) {
		/* TODO: check if changed channel, band */
		if (priv->join_status != ATBM_APOLLO_JOIN_STATUS_AP ||
		    priv->beacon_int != conf->beacon_int) {
			ap_printk( "ap restarting\n");
			wsm_lock_tx(hw_priv);
			if (priv->join_status != ATBM_APOLLO_JOIN_STATUS_PASSIVE)
				WARN_ON(wsm_reset(hw_priv, &reset,
						  priv->if_id));
			priv->join_status = ATBM_APOLLO_JOIN_STATUS_PASSIVE;
			WARN_ON(atbm_start_ap(priv));
			wsm_unlock_tx(hw_priv);
		} else
			ap_printk( "ap started join_status: %d\n",
				  priv->join_status);
	}
	return 0;
}
#ifdef CONFIG_ATBM_SUPPORT_P2P
void atbm_notify_noa(struct atbm_vif *priv, int delay)
{
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);
//	struct cfg80211_p2p_ps p2p_ps = {0};
	struct wsm_p2p_ps_modeinfo *modeinfo;
	modeinfo = &priv->p2p_ps_modeinfo;

	ap_printk( "[AP]: %s called\n", __func__);
	
	if (priv->join_status != ATBM_APOLLO_JOIN_STATUS_AP)
		return;

	if (delay)
		msleep(delay);

	if (!WARN_ON(wsm_get_p2p_ps_modeinfo(hw_priv, modeinfo,priv->if_id))) {
#if defined(CONFIG_ATBM_APOLLO_STA_DEBUG)
		print_hex_dump_bytes("[AP] p2p_get_ps_modeinfo: ",
				     DUMP_PREFIX_NONE,
				     (u8 *)modeinfo,
				     sizeof(*modeinfo));
#endif /* CONFIG_ATBM_APOLLO_STA_DEBUG */
#if 0
		p2p_ps.opp_ps = !!(modeinfo->oppPsCTWindow & BIT(7));
		p2p_ps.ctwindow = modeinfo->oppPsCTWindow & (~BIT(7));
		p2p_ps.count = modeinfo->count;
		p2p_ps.start = __le32_to_cpu(modeinfo->startTime);
		p2p_ps.duration = __le32_to_cpu(modeinfo->duration);
		p2p_ps.interval = __le32_to_cpu(modeinfo->interval);
		p2p_ps.index = modeinfo->reserved;

		ieee80211_p2p_noa_notify(priv->vif,
					 &p2p_ps,
					 GFP_KERNEL);
#endif
	}

}
#endif
int atbm_start_monitor_mode(struct atbm_vif *priv,
				struct ieee80211_channel *chan)
{
	struct atbm_common *hw_priv = priv->hw_priv;
	int ret = 0;
	struct wsm_start start = {
		.mode = WSM_START_MODE_MONITOR_DEV | (priv->if_id << 4),
		.band = (chan->band == IEEE80211_BAND_5GHZ) ?
				WSM_PHY_BAND_5G : WSM_PHY_BAND_2_4G,
		.channelNumber = channel_hw_value(chan),
		.beaconInterval = 100,
		.DTIMPeriod = 1,
		.probeDelay = 0,
		.basicRateSet = 0x0F,
	};
	if(priv->join_status != ATBM_APOLLO_JOIN_STATUS_SIMPLE_MONITOR){
		return -1;
	}
	/*
	*when one interface in monitor mode ,othe interface can not process scan
	*or remain on channel;
	*/
	lockdep_assert_held(&hw_priv->conf_mutex);
	hw_priv->monitor_if_id = priv->if_id;
	/*
	*must make sure monitor_if_id has been set to priv->if_id,before send cmd to lmc;
	*/
	smp_mb();
	/*
	* clear the pkg 
	*/
	ret = WARN_ON(__atbm_flush(hw_priv, false,priv->if_id));
	if(!ret){
		wsm_unlock_tx(hw_priv);
	}
#ifndef ATBM_NOT_SUPPORT_40M_CHW
	if(hw_priv->chip_version == CRONUS_NO_HT40 || hw_priv->chip_version == CRONUS_NO_HT40_LDPC){
		start.channel_type = (u32)(NL80211_CHAN_HT20);
	}else{

		if((channel_hw_value(chan)>=9)&&(channel_hw_value(chan)<=14))
			start.channel_type = (u32)(NL80211_CHAN_HT40MINUS);
		else if((channel_hw_value(chan)>0)&&(channel_hw_value(chan)<9))
			start.channel_type = (u32)(NL80211_CHAN_HT40PLUS);
		else if(channel_hw_value(chan) == 36)
			start.channel_type = (u32)(NL80211_CHAN_HT40PLUS);
		else if(channel_hw_value(chan) == 38)
			start.channel_type = (u32)(NL80211_CHAN_HT40MINUS);
		else {
			return -1;
		}
	}
#else
	start.channel_type = (u32)(NL80211_CHAN_HT20);
#endif
	atbm_printk_ap("%s:if_id(%d),channel(%d)\n",__func__,priv->if_id,channel_hw_value(chan));
	wsm_start(hw_priv, &start, ATBM_WIFI_GENERIC_IF_ID);

	return ret;
}

int atbm_stop_monitor_mode(struct atbm_vif *priv)
{
	struct atbm_common *hw_priv = priv->hw_priv;
	int ret = 0;
	struct wsm_reset reset = {
		.reset_statistics = true,
	};
	lockdep_assert_held(&hw_priv->conf_mutex);
	ret = WARN_ON(__atbm_flush(hw_priv, false, priv->if_id));
	if (!ret) {
		wsm_unlock_tx(hw_priv);
	}
	
	ret = wsm_reset(priv->hw_priv, &reset, ATBM_WIFI_GENERIC_IF_ID);
	smp_mb();
	hw_priv->monitor_if_id = -1;	
	atbm_printk_ap("%s:if_id(%d)\n",__func__,priv->if_id);
	return ret;
}

#ifdef CONFIG_ATBM_SUPPORT_CSA
int atbm_do_csa(struct ieee80211_vif *vif,struct ieee80211_csa_request *request)
{
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);
	struct wsm_csa csa;
	
	if(atomic_read(&priv->enabled) == 0){
		atbm_printk_err("[%s] has been disabled\n",vif_to_sdata(vif)->name);
		return -1;
	}

	if(atbm_bh_is_term(priv->hw_priv)){
		return -1;
	}

	csa.flags       = request->start == true ? WSM_CSA_FLAGS_START : 0;
	csa.csa_mode    = request->ie.mode;
	csa.csa_channel = request->ie.new_ch_num;
	csa.csa_count   = request->ie.count;
	
	return wsm_csa_req(priv->hw_priv,&csa,priv->if_id);
}
#endif

