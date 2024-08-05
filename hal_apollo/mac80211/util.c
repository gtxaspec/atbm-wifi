/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * utilities for mac80211
 */

#include <net/atbm_mac80211.h>
#include <linux/netdevice.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/bitmap.h>
#include <linux/crc32.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include <net/tcp.h>
#include <linux/ratelimit.h>
#include <linux/highmem.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,19,0))
#include <linux/highmem-internal.h>
#endif

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "mesh.h"
#include "wme.h"
#include "wep.h"
#include "atbm_common.h"
#include "twt.h"
#include "../apollo.h"

/* privid for wiphys to determine whether they belong to us or not */
void *mac80211_wiphy_privid = &mac80211_wiphy_privid;
int tx_rate = 0xFF;
extern int tx_rate_down;
extern int tx_rate_static;
module_param(tx_rate,int,0644);
DEFINE_RATELIMIT_STATE(atbm_ratelimit_state,30*HZ,3);

int ieee80211_ratelimit(void)
{
	return __ratelimit(&atbm_ratelimit_state);
}
u8 *ieee80211_get_bssid(struct ieee80211_hdr *hdr, size_t len,
			enum nl80211_iftype type)
{
	__le16 fc = hdr->frame_control;

	 /* drop ACK/CTS frames and incorrect hdr len (ctrl) */
	if (len < 16)
		return NULL;

	if (ieee80211_is_data(fc)) {
		if (len < 24) /* drop incorrect hdr len (data) */
			return NULL;

		if (ieee80211_has_a4(fc))
			return NULL;
		if (ieee80211_has_tods(fc))
			return hdr->addr1;
		if (ieee80211_has_fromds(fc))
			return hdr->addr2;

		return hdr->addr3;
	}

	if (ieee80211_is_mgmt(fc)) {
		if (len < 24) /* drop incorrect hdr len (mgmt) */
			return NULL;
		return hdr->addr3;
	}

	if (ieee80211_is_ctl(fc)) {
		if(ieee80211_is_pspoll(fc))
			return hdr->addr1;

		if (ieee80211_is_back_req(fc)) {
			switch (type) {
			case NL80211_IFTYPE_STATION:
				return hdr->addr2;
			case NL80211_IFTYPE_AP:
			case NL80211_IFTYPE_AP_VLAN:
				return hdr->addr1;
			default:
				break; /* fall through to the return */
			}
		}
	}

	return NULL;
}

void ieee80211_tx_set_protected(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb = tx->skb;
	struct ieee80211_hdr *hdr;

	do {
		hdr = (struct ieee80211_hdr *) skb->data;
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	} while ((skb = skb->next));
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
static bool ieee80211_all_queues_started(struct ieee80211_hw *hw)
{
	unsigned int queue;

	for (queue = 0; queue < hw->queues; queue++)
		if (ieee80211_queue_stopped(hw, queue))
			return false;
	return true;
}
#endif

void ieee80211_propagate_queue_wake(struct ieee80211_local *local, int queue)
{
	struct ieee80211_sub_if_data *sdata;

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		int ac;

#ifdef CONFIG_MAC80211_ATBM_ROAMING_CHANGES
		if (test_bit(SDATA_STATE_OFFCHANNEL, &sdata->state)
			    || sdata->queues_locked)
#else
		if (test_bit(SDATA_STATE_OFFCHANNEL, &sdata->state))
#endif
			continue;

		if (sdata->vif.cab_queue != IEEE80211_INVAL_HW_QUEUE &&
		    local->queue_stop_reasons[sdata->vif.cab_queue] != 0)
			continue;

		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
			int ac_queue = sdata->vif.hw_queue[ac];

			if (ac_queue == queue ||
			    (sdata->vif.cab_queue == queue &&
			     local->queue_stop_reasons[ac_queue] == 0 &&
			     atbm_skb_queue_empty(&local->pending[ac_queue]))){
				netif_wake_subqueue(sdata->dev, ac);
			}
		}
	}
}

static void __ieee80211_wake_queue(struct ieee80211_hw *hw, int queue,
				   enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_wake_queue(local, queue, reason);

	if (WARN_ON(queue >= hw->queues))
		return;

	if (!test_bit(reason, &local->queue_stop_reasons[queue]))
		return;

	__clear_bit(reason, &local->queue_stop_reasons[queue]);

	if (local->queue_stop_reasons[queue] != 0)
		/* someone still has this queue stopped */
		return;

	if (atbm_skb_queue_empty(&local->pending[queue])) {
		rcu_read_lock();
		ieee80211_propagate_queue_wake(local, queue);
		rcu_read_unlock();
	} else
		tasklet_schedule(&local->tx_pending_tasklet);
}

void ieee80211_wake_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__ieee80211_wake_queue(hw, queue, reason);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_wake_queue(struct ieee80211_hw *hw, int queue)
{
	ieee80211_wake_queue_by_reason(hw, queue,
				       IEEE80211_QUEUE_STOP_REASON_DRIVER);
}
//EXPORT_SYMBOL(ieee80211_wake_queue);

static void __ieee80211_stop_queue(struct ieee80211_hw *hw, int queue,
				   enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;

	trace_stop_queue(local, queue, reason);

	if (WARN_ON(queue >= hw->queues))
		return;

	if (test_bit(reason, &local->queue_stop_reasons[queue]))
		return;

	__set_bit(reason, &local->queue_stop_reasons[queue]);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		int ac;

		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
			if (sdata->vif.hw_queue[ac] == queue ||
			    sdata->vif.cab_queue == queue)
				netif_stop_subqueue(sdata->dev, ac);
		}
	}
	rcu_read_unlock();
}

void ieee80211_stop_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__ieee80211_stop_queue(hw, queue, reason);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
	ieee80211_stop_queue_by_reason(hw, queue,
				       IEEE80211_QUEUE_STOP_REASON_DRIVER);
}
//EXPORT_SYMBOL(ieee80211_stop_queue);

void ieee80211_add_pending_skb(struct ieee80211_local *local,
			       struct sk_buff *skb)
{
	struct ieee80211_hw *hw = &local->hw;
	unsigned long flags;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int queue = info->hw_queue;

	if (WARN_ON(!info->control.vif)) {
		atbm_kfree_skb(skb);
		return;
	}

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__ieee80211_stop_queue(hw, queue, IEEE80211_QUEUE_STOP_REASON_SKB_ADD);
	__atbm_skb_queue_tail(&local->pending[queue], skb);
	__ieee80211_wake_queue(hw, queue, IEEE80211_QUEUE_STOP_REASON_SKB_ADD);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_add_pending_skbs_fn(struct ieee80211_local *local,
				   struct sk_buff_head *skbs,
				   void (*fn)(void *data), void *data)
{
	struct ieee80211_hw *hw = &local->hw;
	struct sk_buff *skb;
	unsigned long flags;
	int queue, i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	while ((skb = atbm_skb_dequeue(skbs))) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (WARN_ON(!info->control.vif)) {
			atbm_kfree_skb(skb);
			continue;
		}

		queue = info->hw_queue;

		__ieee80211_stop_queue(hw, queue,
				IEEE80211_QUEUE_STOP_REASON_SKB_ADD);

		__atbm_skb_queue_tail(&local->pending[queue], skb);
	}

	if (fn)
		fn(data);

	for (i = 0; i < hw->queues; i++)
		__ieee80211_wake_queue(hw, i,
			IEEE80211_QUEUE_STOP_REASON_SKB_ADD);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_add_pending_skbs(struct ieee80211_local *local,
				struct sk_buff_head *skbs)
{
	ieee80211_add_pending_skbs_fn(local, skbs, NULL, NULL);
}

void ieee80211_stop_queues_by_reason(struct ieee80211_hw *hw,
				    enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);

	for (i = 0; i < hw->queues; i++)
		__ieee80211_stop_queue(hw, i, reason);

	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_stop_queues(struct ieee80211_hw *hw)
{
	ieee80211_stop_queues_by_reason(hw,
					IEEE80211_QUEUE_STOP_REASON_DRIVER);
}
//EXPORT_SYMBOL(ieee80211_stop_queues);

int ieee80211_queue_stopped(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int ret;

	if (WARN_ON(queue >= hw->queues))
		return true;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	ret = !!local->queue_stop_reasons[queue];
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
	return ret;
}
//EXPORT_SYMBOL(ieee80211_queue_stopped);

void ieee80211_wake_queues_by_reason(struct ieee80211_hw *hw,
				     enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);

	for (i = 0; i < hw->queues; i++)
		__ieee80211_wake_queue(hw, i, reason);

	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void ieee80211_wake_queues(struct ieee80211_hw *hw)
{
	ieee80211_wake_queues_by_reason(hw, IEEE80211_QUEUE_STOP_REASON_DRIVER);
}
//EXPORT_SYMBOL(ieee80211_wake_queues);

void ieee80211_iterate_active_interfaces_atomic(
	struct ieee80211_hw *hw,
	void (*iterator)(void *data, u8 *mac,
			 struct ieee80211_vif *vif),
	void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;

	if((local->quiescing == true) || (local->suspended == true))
		return;
	rcu_read_lock();

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		switch (sdata->vif.type) {
		case NL80211_IFTYPE_MONITOR:
		case NL80211_IFTYPE_AP_VLAN:
			continue;
		default:
			break;
		}
		if (ieee80211_sdata_running(sdata))
			iterator(data, sdata->vif.addr,
				 &sdata->vif);
	}

	rcu_read_unlock();
}
//EXPORT_SYMBOL_GPL(ieee80211_iterate_active_interfaces_atomic);

/*
 * Nothing should have been stuffed into the workqueue during
 * the suspend->resume cycle. If this WARN is seen then there
 * is a bug with either the driver suspend or something in
 * mac80211 stuffing into the workqueue which we haven't yet
 * cleared during mac80211's suspend cycle.
 */
static bool ieee80211_can_queue_work(struct ieee80211_local *local)
{
	if ((local->quiescing == true)||(local->suspended && !local->resuming)){
		pr_warn("queueing ieee80211 work while going to suspend\n");
		return false;
	}
	return true;
}

void ieee80211_queue_work(struct ieee80211_hw *hw, struct atbm_work_struct *work)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (!ieee80211_can_queue_work(local))
		return;

	atbm_queue_work(local->workqueue, work);
}
//EXPORT_SYMBOL(ieee80211_queue_work);

void ieee80211_queue_delayed_work(struct ieee80211_hw *hw,
				  struct atbm_delayed_work *dwork,
				  unsigned long delay)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (!ieee80211_can_queue_work(local))
		return;

	atbm_queue_delayed_work(local->workqueue, dwork, delay);
}
//EXPORT_SYMBOL(ieee80211_queue_delayed_work);

static int ieee802_11_parse_vendor_specific(u8 *pos, size_t elen,
					    struct ieee802_atbm_11_elems *elems,
					    int show_errors)
{
	unsigned int oui;
	//unsigned char i = 0,offsert = 4;
	//int encry_type_count = 0;

	/* first 3 bytes in vendor specific information element are the IEEE
	 * OUI of the vendor. The following byte is used a vendor specific
	 * sub-type. */
	if (elen < 4) {
		if (show_errors) {
			atbm_printk_err("short vendor specific "
				   "information element ignored (len=%lu)",
				   (unsigned long) elen);
		}
		return -1;
	}

	oui = ATBM_WPA_GET_BE24(pos);
	switch (oui) {
	case ATBM_OUI_MICROSOFT:
		/* Microsoft/Wi-Fi information elements are further typed and
		 * subtyped */
		switch (pos[3]) {
		case 1:
			/* Microsoft OUI (00:50:F2) with OUI Type 1:
			 * real WPA information element */
			elems->wpa = pos;
			elems->wpa_len = elen;
			break;
		case ATBM_WMM_OUI_TYPE:
			/* WMM information element */
			if (elen < 5) {
				atbm_printk_debug( "short WMM "
					   "information element ignored "
					   "(len=%lu)",
					   (unsigned long) elen);
				return -1;
			}
			switch (pos[4]) {
			case ATBM_WMM_OUI_SUBTYPE_INFORMATION_ELEMENT:
				elems->wmm_info = pos;
				elems->wmm_info_len = elen;
				break;
			case ATBM_WMM_OUI_SUBTYPE_PARAMETER_ELEMENT:
				elems->wmm_param = pos;
				elems->wmm_param_len = elen;
				break;
			default:
				atbm_printk_debug( "unknown WMM "
					   "information element ignored "
					   "(subtype=%d len=%lu)",
					   pos[4], (unsigned long) elen);
				return -1;
			}
			break;
		case 4:
			/* Wi-Fi Protected Setup (WPS) IE */
			elems->wps_ie = pos;
			elems->wps_ie_len = elen;
			break;
		default:
			atbm_printk_debug( "Unknown Microsoft "
				   "information element ignored "
				   "(type=%d len=%lu)",
				   pos[3], (unsigned long) elen);
			return -1;
		}
		break;

	case ATBM_OUI_WFA:
		switch (pos[3]) {
		case ATBM_P2P_OUI_TYPE:
			/* Wi-Fi Alliance - P2P IE */
			elems->p2p_ie = pos;
			elems->p2p_ie_len = elen;
			break;
		case ATBM_WFD_OUI_TYPE:
			/* Wi-Fi Alliance - WFD IE */
			elems->wfd = pos;
			elems->wfd_len = elen;
			break;
		default:
			atbm_printk_debug("Unknown WFA "
				   "information element ignored "
				   "(type=%d len=%lu)\n",
				   pos[3], (unsigned long) elen);
			return -1;
		}
		break;

	default:
		/*
		printk( "unknown vendor specific "
			   "information element ignored (vendor OUI "
			   "%02x:%02x:%02x len=%lu)",
			   pos[0], pos[1], pos[2], (unsigned long) elen);*/
		return -1;
	}

	return 0;
}
//#ifdef CONFIG_ATBM_HE
static void ieee80211_parse_extension_element(u8 *elem_data, u8 elem_data_len,
					      struct ieee802_atbm_11_elems *elems,u32 * crc)
{
	const void *data = elem_data + 1;
	u8 len = elem_data_len - 1;
	switch (elem_data[0]) {
	case ATBM_WLAN_EID_EXT_HE_MU_EDCA:
		if (len >= sizeof(*elems->mu_edca_param_set)) {
			elems->mu_edca_param_set = data;
			if (crc)
				*crc = crc32_be(*crc, (void *)data,len);
		}
		break;
	case ATBM_WLAN_EID_EXT_HE_CAPABILITY:
		elems->he_cap = data;
		elems->he_cap_len = len;
		break;
	case ATBM_WLAN_EID_EXT_HE_OPERATION:
		if (len >= sizeof(*elems->he_operation) &&
		    len >= atbm_ieee80211_he_oper_size(data) - 1) {
			//if (crc)
			//	*crc = crc32_be(*crc, (void *)elem,
			//			elem->datalen + 2);
			elems->he_operation = data;
		}
		break;
	case ATBM_WLAN_EID_EXT_UORA:
		if (len >= 1)
			elems->uora_element = data;
		break;
	case ATBM_WLAN_EID_EXT_MAX_CHANNEL_SWITCH_TIME:
		if (len == 3)
			elems->max_channel_switch_time = data;
		break;
	case ATBM_WLAN_EID_EXT_MULTIPLE_BSSID_CONFIGURATION:
		if (len >= sizeof(*elems->mbssid_config_ie))
			elems->mbssid_config_ie = data;
		break;
	case ATBM_WLAN_EID_EXT_HE_SPR:
		if (len >= sizeof(*elems->he_spr) &&
		    len >= atbm_ieee80211_he_spr_size(data))
			elems->he_spr = data;
		break;
	case ATBM_WLAN_EID_EXT_HE_6GHZ_CAPA:
		//if (len >= sizeof(*elems->he_6ghz_capa))
		//	elems->he_6ghz_capa = data;
		break;
	}
}
//#endif  //#ifdef CONFIG_ATBM_HE
u32 _ieee802_11_parse_elems_crc(const u8 *start, size_t len, bool action,
			       struct ieee802_atbm_11_elems *elems,
			       u64 filter, u32 crc/*,  struct element *check_inherit*/)
{
	size_t left = len;
	u8 *pos = (u8 *)start;
	bool calc_crc = filter != 0;
	//unsigned char i = 0,offsert = 4;
	//int encry_type_count = 0;
	memset(elems, 0, sizeof(*elems));
	elems->ie_start = (u8 *)start;
	elems->total_len = len;
	elems->encry_info = 0;
	while (left >= 2) {
		u8 id, elen;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left)
			break;

		if (calc_crc && id < 64 && (filter & (1ULL << id)))
			crc = crc32_be(crc, pos - 2, elen + 2);

		switch (id) {
		case ATBM_WLAN_EID_SSID:
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case ATBM_WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case ATBM_WLAN_EID_FH_PARAMS:
			elems->fh_params = pos;
			elems->fh_params_len = elen;
			break;
		case ATBM_WLAN_EID_DS_PARAMS:
			elems->ds_params = pos;
			elems->ds_params_len = elen;
			break;
		case ATBM_WLAN_EID_CF_PARAMS:
			elems->cf_params = pos;
			elems->cf_params_len = elen;
			break;
		case ATBM_WLAN_EID_TIM:
			if (elen >= sizeof(struct ieee80211_tim_ie)) {
				elems->tim = (void *)pos;
				elems->tim_len = elen;
			}
			break;
		case ATBM_WLAN_EID_IBSS_PARAMS:
			elems->ibss_params = pos;
			elems->ibss_params_len = elen;
			break;
		case ATBM_WLAN_EID_CHALLENGE:
			elems->challenge = pos;
			elems->challenge_len = elen;
			break;
		case ATBM_WLAN_EID_VENDOR_SPECIFIC:
			#if 0
			if (elen >= 4 && pos[0] == 0x00 && pos[1] == 0x50 &&
			    pos[2] == 0xf2) {
				/* Microsoft OUI (00:50:F2) */

				if (calc_crc)
					crc = crc32_be(crc, pos - 2, elen + 2);

				if (pos[3] == 1) {
					/* OUI Type 1 - WPA IE */
					elems->wpa = pos;
					elems->wpa_len = elen;
				} else if (elen >= 5 && pos[3] == 2) {
					/* OUI Type 2 - WMM IE */
					if (pos[4] == 0) {
						elems->wmm_info = pos;
						elems->wmm_info_len = elen;
					} else if (pos[4] == 1) {
						elems->wmm_param = pos;
						elems->wmm_param_len = elen;
					}
				}
			}
			/*
			*process p2p ie
			*p2p outui {0x50,0x6F,0x9A,0x09};
			*/
			if(elen >= 4 && pos[0] == 0x50 && pos[1] == 0x6F &&
			    pos[2] == 0x9A && pos[3]== 0x09)
			{
				if (calc_crc)
					crc = crc32_be(crc, pos - 2, elen + 2);
				elems->p2p_ie = pos;
				elems->p2p_ie_len = elen;
			}
			#endif
			ieee802_11_parse_vendor_specific(pos,elen,elems,1);
			break;
		case ATBM_WLAN_EID_RSN:
			elems->rsn = pos;
			elems->rsn_len = elen;

			break;
		case ATBM_WLAN_EID_ERP_INFO:
			elems->erp_info = pos;
			elems->erp_info_len = elen;
			break;
		case ATBM_WLAN_EID_EXT_SUPP_RATES:
			elems->ext_supp_rates = pos;
			elems->ext_supp_rates_len = elen;
			break;
		case ATBM_WLAN_EID_HT_CAPABILITY:
			if (elen >= sizeof(struct ieee80211_ht_cap))
				elems->ht_cap_elem = (void *)pos;
			break;
		case ATBM_WLAN_EID_HT_INFORMATION:
			if (elen >= sizeof(struct ieee80211_ht_info))
				elems->ht_info_elem = (void *)pos;
			break;
		case ATBM_WLAN_EID_MESH_ID:
			elems->mesh_id = pos;
			elems->mesh_id_len = elen;
			break;
		case ATBM_WLAN_EID_MESH_CONFIG:
			if (elen >= sizeof(struct ieee80211_meshconf_ie))
				elems->mesh_config = (void *)pos;
			break;
		case ATBM_WLAN_EID_PEER_MGMT:
			elems->peering = pos;
			elems->peering_len = elen;
			break;
		case ATBM_WLAN_EID_PREQ:
			elems->preq = pos;
			elems->preq_len = elen;
			break;
		case ATBM_WLAN_EID_PREP:
			elems->prep = pos;
			elems->prep_len = elen;
			break;
		case ATBM_WLAN_EID_PERR:
			elems->perr = pos;
			elems->perr_len = elen;
			break;
		case ATBM_WLAN_EID_RANN:
			if (elen >= sizeof(struct ieee80211_rann_ie))
				elems->rann = (void *)pos;
			break;
		case ATBM_WLAN_EID_CHANNEL_SWITCH:
			elems->ch_switch_elem = pos;
			elems->ch_switch_elem_len = elen;
			break;
		case ATBM_WLAN_EID_QUIET:
			if (!elems->quiet_elem) {
				elems->quiet_elem = pos;
				elems->quiet_elem_len = elen;
			}
			elems->num_of_quiet_elem++;
			break;
		case ATBM_WLAN_EID_COUNTRY:
			elems->country_elem = pos;
			elems->country_elem_len = elen;
			break;
		case ATBM_WLAN_EID_PWR_CONSTRAINT:
			elems->pwr_constr_elem = pos;
			elems->pwr_constr_elem_len = elen;
			break;
		case ATBM_WLAN_EID_TIMEOUT_INTERVAL:
			elems->timeout_int = pos;
			elems->timeout_int_len = elen;
			break;
		case ATBM_WLAN_EID_EXT_CHANSWITCH_ANN:
			elems->extended_ch_switch_elem = pos;
			elems->extended_ch_switch_elem_len = elen;
			break;
		case ATBM_WLAN_EID_SECONDARY_CH_OFFSET:
			elems->secondary_ch_elem=pos;
			elems->secondary_ch_elem_len=elen;
			break;
		case ATBM_WLAN_EID_PRIVATE:
			elems->atbm_special = pos;
			elems->atbm_special_len = elen;
			break;
        /*case ATBM_WLAN_EID_RSNX:
			elems->rsnx = pos;
			elems->rsnx_len = elen;
			break;   */
//#ifdef CONFIG_ATBM_HE
		case ATBM_WLAN_EID_EXTENSION:
			ieee80211_parse_extension_element(pos, elen, elems,&crc);
			break;
		case ATBM_WLAN_EID_ADDBA_EXT:
			if (elen < sizeof(struct atbm_ieee80211_addba_ext_ie)) {
				elems->parse_error = true;
				break;
			}
			elems->addba_ext_ie = (void *)pos;
			break;
		case  ATBM_WLAN_EID_AID_RESPONSE:
			if (elen == sizeof(struct atbm_ieee80211_aid_response_ie))
				elems->aid_resp = (void *)pos;
			else
				elems->parse_error = true;
			break;
//#endif  //		CONFIG_ATBM_HE
		case ATBM_WLAN_EID_BSS_MAX_IDLE_PERIOD:
			if (elen >= sizeof(struct atbm_ieee80211_bss_max_idle_period_ie))
				elems->max_idle_period_ie = (void *)pos;
			else
				elems->parse_error = true;
			
			break;
		case ATBM_WLAN_EID_MULTIPLE_BSSID:
			elems->multibssid = pos;
			elems->multibssid_len = elen;
			//atbm_printk_err("find multibssid\n");
		default:
			break;
		}

		left -= elen;
		pos += elen;
	}

	return crc;
}
#ifdef CONFIG_ATBM_HE
/*u8 ieee80211_ie_len_he_cap(struct ieee80211_sub_if_data *sdata, u8 iftype)
{
	const struct ieee80211_sta_he_cap *he_cap;
	struct ieee80211_supported_band *sband;
	u8 n;

	sband = ieee80211_get_sband(sdata);
	if (!sband)
		return 0;

	he_cap = atbm_ieee80211_get_he_iftype_cap(sband, iftype);
	if (!he_cap)
		return 0;

	n = atbm_ieee80211_he_mcs_nss_size(&he_cap->he_cap_elem);
	return 2 + 1 +
	       sizeof(he_cap->he_cap_elem) + n +
	       ieee80211_he_ppe_size(he_cap->ppe_thres[0],
				     he_cap->he_cap_elem.phy_cap_info);
}*/

u8 *ieee80211_ie_build_he_cap(u8 *pos,const struct atbm_ieee80211_sta_he_cap *he_cap,
			      u8 *end)
{
	u8 n;
	u8 ie_len;
	u8 *orig_pos = pos;

	/* Make sure we have place for the IE */
	/*
	 * TODO: the 1 added is because this temporarily is under the EXTENSION
	 * IE. Get rid of it when it moves.
	 */
	if (!he_cap)
		return orig_pos;

	n = atbm_ieee80211_he_mcs_nss_size(&he_cap->he_cap_elem);
	ie_len = 2 + 1 +
		 sizeof(he_cap->he_cap_elem) + n +
		 atbm_ieee80211_he_ppe_size(he_cap->ppe_thres[0],
				       he_cap->he_cap_elem.phy_cap_info);

	if ((end - pos) < ie_len)
		return orig_pos;

	*pos++ = ATBM_WLAN_EID_EXTENSION;
	pos++; /* We'll set the size later below */
	*pos++ = ATBM_WLAN_EID_EXT_HE_CAPABILITY;

	/* Fixed data */
	memcpy(pos, &he_cap->he_cap_elem, sizeof(he_cap->he_cap_elem));
	pos += sizeof(he_cap->he_cap_elem);

	memcpy(pos, &he_cap->he_mcs_nss_supp, n);
	pos += n;

	/* Check if PPE Threshold should be present */
	if ((he_cap->he_cap_elem.phy_cap_info[6] &
	     ATBM_IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT) == 0)
		goto end;

	/*
	 * Calculate how many PPET16/PPET8 pairs are to come. Algorithm:
	 * (NSS_M1 + 1) x (num of 1 bits in RU_INDEX_BITMASK)
	 */
	n = hweight8(he_cap->ppe_thres[0] &
		     ATBM_IEEE80211_PPE_THRES_RU_INDEX_BITMASK_MASK);
	n *= (1 + ((he_cap->ppe_thres[0] & ATBM_IEEE80211_PPE_THRES_NSS_MASK) >>
		   ATBM_IEEE80211_PPE_THRES_NSS_POS));

	/*
	 * Each pair is 6 bits, and we need to add the 7 "header" bits to the
	 * total size.
	 */
	n = (n * ATBM_IEEE80211_PPE_THRES_INFO_PPET_SIZE * 2) + 7;
	n = DIV_ROUND_UP(n, 8);

	/* Copy PPE Thresholds */
	memcpy(pos, &he_cap->ppe_thres, n);
	pos += n;

end:
	orig_pos[1] = (pos - orig_pos) - 2;
	return pos;
}

u8 *ieee80211_ie_build_he_oper(u8 *pos,u16 he_mcs_nss_set)
{
	struct atbm_ieee80211_he_operation *he_oper;
	//struct atbm_ieee80211_he_6ghz_oper *he_6ghz_op;
	u32 he_oper_params;
	u8 ie_len = 1 + sizeof(struct atbm_ieee80211_he_operation);

	//if (chandef->chan->band == NL80211_BAND_6GHZ)
	//	ie_len += sizeof(struct atbm_ieee80211_he_6ghz_oper);

	*pos++ = ATBM_WLAN_EID_EXTENSION;
	*pos++ = ie_len;
	*pos++ = ATBM_WLAN_EID_EXT_HE_OPERATION;

	he_oper_params = 0;
	//he_oper_params |= u32_encode_bits(1023, /* disabled */IEEE80211_HE_OPERATION_RTS_THRESHOLD_MASK);
	he_oper_params |= 1023<<ATBM_IEEE80211_HE_OPERATION_RTS_THRESHOLD_OFFSET;

	he_oper_params |= ATBM_IEEE80211_HE_OPERATION_ER_SU_DISABLE;
		//u32_encode_bits(1,IEEE80211_HE_OPERATION_ER_SU_DISABLE);
	he_oper_params |= ATBM_IEEE80211_HE_OPERATION_BSS_COLOR_DISABLED;
	//u32_encode_bits(1	IEEE80211_HE_OPERATION_BSS_COLOR_DISABLED);
	#if 0
	if (chandef->chan->band == NL80211_BAND_6GHZ)
		he_oper_params |= IEEE80211_HE_OPERATION_6GHZ_OP_INFO;//u32_encode_bits(1,IEEE80211_HE_OPERATION_6GHZ_OP_INFO);
	#endif
	he_oper = (struct atbm_ieee80211_he_operation *)pos;
	he_oper->he_oper_params = cpu_to_le32(he_oper_params);

	/* don't require special HE peer rates */
	he_oper->he_mcs_nss_set = cpu_to_le16(he_mcs_nss_set);
	pos += sizeof(struct atbm_ieee80211_he_operation);
#if 0
	if (chandef->chan->band != NL80211_BAND_6GHZ)
		goto out;

	/* TODO add VHT operational */
	he_6ghz_op = (struct atbm_ieee80211_he_6ghz_oper *)pos;
	he_6ghz_op->minrate = 6; /* 6 Mbps */
	he_6ghz_op->primary =
		ieee80211_frequency_to_channel(chandef->chan->center_freq);
	he_6ghz_op->ccfs0 =
		ieee80211_frequency_to_channel(chandef->center_freq1);
	if (chandef->center_freq2)
		he_6ghz_op->ccfs1 =
			ieee80211_frequency_to_channel(chandef->center_freq2);
	else
		he_6ghz_op->ccfs1 = 0;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_160:
		/* Convert 160 MHz channel width to new style as interop
		 * workaround.
		 */
		he_6ghz_op->control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_160MHZ;
		he_6ghz_op->ccfs1 = he_6ghz_op->ccfs0;
		if (chandef->chan->center_freq < chandef->center_freq1)
			he_6ghz_op->ccfs0 -= 8;
		else
			he_6ghz_op->ccfs0 += 8;
		fallthrough;
	case NL80211_CHAN_WIDTH_80P80:
		he_6ghz_op->control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_160MHZ;
		break;
	case NL80211_CHAN_WIDTH_80:
		he_6ghz_op->control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_80MHZ;
		break;
	case NL80211_CHAN_WIDTH_40:
		he_6ghz_op->control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_40MHZ;
		break;
	default:
		he_6ghz_op->control =
			IEEE80211_HE_6GHZ_OPER_CTRL_CHANWIDTH_20MHZ;
		break;
	}

	pos += sizeof(struct atbm_ieee80211_he_6ghz_oper);
out:
#endif
	return pos;
}
#endif  //CONFIG_ATBM_HE

#ifdef CONFIG_ATBM_HE
#if 0//LINUX_VERSION_IS_LESS_AND_CPTCFG(5,0,0)

static inline void cfg80211_gen_new_bssid(const u8 *bssid, u8 max_bssid,
					  u8 mbssid_index, u8 *new_bssid)
{
	u64 bssid_u64 = ether_addr_to_u64(bssid);
	u64 mask = GENMASK_ULL(max_bssid - 1, 0);
	u64 new_bssid_u64;

	new_bssid_u64 = bssid_u64 & ~mask;

	new_bssid_u64 |= ((bssid_u64 & mask) + mbssid_index) & mask;

	u64_to_ether_addr(new_bssid_u64, new_bssid);
}

const struct element *
cfg80211_find_elem_match(u8 eid, const u8 *ies, unsigned int len,
			 const u8 *match, unsigned int match_len,
			 unsigned int match_offset)
{
	const struct element *elem;

	for_each_element_id(elem, eid, ies, len) {
		if (elem->datalen >= match_offset + match_len &&
		    !memcmp(elem->data + match_offset, match, match_len))
			return elem;
	}

	return NULL;
}

static const struct element *
cfg80211_find_elem(u8 eid, const u8 *ies, int len)
{
	return cfg80211_find_elem_match(eid, ies, len, NULL, 0, 0);
}

static const struct element
*cfg80211_get_profile_continuation(const u8 *ie, size_t ielen,
				   const struct element *mbssid_elem,
				   const struct element *sub_elem)
{
	const u8 *mbssid_end = mbssid_elem->data + mbssid_elem->datalen;
	const struct element *next_mbssid;
	const struct element *next_sub;

	next_mbssid = cfg80211_find_elem(WLAN_EID_MULTIPLE_BSSID,
					 mbssid_end,
					 ielen - (mbssid_end - ie));

	/*
	 * If it is not the last subelement in current MBSSID IE or there isn't
	 * a next MBSSID IE - profile is complete.
	*/
	if ((sub_elem->data + sub_elem->datalen < mbssid_end - 1) ||
	    !next_mbssid)
		return NULL;

	/* For any length error, just return NULL */

	if (next_mbssid->datalen < 4)
		return NULL;

	next_sub = (void *)&next_mbssid->data[1];

	if (next_mbssid->data + next_mbssid->datalen <
	    next_sub->data + next_sub->datalen)
		return NULL;

	if (next_sub->id != 0 || next_sub->datalen < 2)
		return NULL;

	/*
	 * Check if the first element in the next sub element is a start
	 * of a new profile
	 */
	return next_sub->data[0] == WLAN_EID_NON_TX_BSSID_CAP ?
	       NULL : next_mbssid;
}

size_t cfg80211_merge_profile(const u8 *ie, size_t ielen,
			      const struct element *mbssid_elem,
			      const struct element *sub_elem,
			      u8 *merged_ie, size_t max_copy_len)
{
	size_t copied_len = sub_elem->datalen;
	const struct element *next_mbssid;

	if (sub_elem->datalen > max_copy_len)
		return 0;

	memcpy(merged_ie, sub_elem->data, sub_elem->datalen);

	while ((next_mbssid = cfg80211_get_profile_continuation(ie, ielen,
								mbssid_elem,
								sub_elem))) {
		const struct element *next_sub = (void *)&next_mbssid->data[1];

		if (copied_len + next_sub->datalen > max_copy_len)
			break;
		memcpy(merged_ie + copied_len, next_sub->data,
		       next_sub->datalen);
		copied_len += next_sub->datalen;
		mbssid_elem = next_mbssid;
		sub_elem = next_sub;
	}

	return copied_len;
}
static inline const u8 *
cfg80211_find_ie_match(u8 eid, const u8 *ies, unsigned int len,
		       const u8 *match, unsigned int match_len,
		       unsigned int match_offset)
{
	/* match_offset can't be smaller than 2, unless match_len is
	 * zero, in which case match_offset must be zero as well.
	 */
	if (WARN_ON((match_len && match_offset < 2) ||
		    (!match_len && match_offset)))
		return NULL;

	return (void *)cfg80211_find_elem_match(eid, ies, len,
						match, match_len,
						match_offset ?
							match_offset - 2 : 0);
}

/**
 * cfg80211_find_ie - find information element in data
 *
 * @eid: element ID
 * @ies: data consisting of IEs
 * @len: length of data
 *
 * Return: %NULL if the element ID could not be found or if
 * the element is invalid (claims to be longer than the given
 * data), or a pointer to the first byte of the requested
 * element, that is the byte containing the element ID.
 *
 * Note: There are no checks on the element length other than
 * having to fit into the given data.
 */
const u8 *cfg80211_find_ie(u8 eid, const u8 *ies, int len)
{
	return cfg80211_find_ie_match(eid, ies, len, NULL, 0, 0);
}

static inline const struct element *
cfg80211_find_ext_elem(u8 ext_eid, const u8 *ies, int len)
{
	return cfg80211_find_elem_match(ATBM_WLAN_EID_EXTENSION, ies, len,
					&ext_eid, 1, 0);
}
#endif //#if LINUX_VERSION_IS_LESS_OR_CPTCFG(5,0,0)
#ifdef CONFIG_CFG880211_SUPPORT_HE
static size_t ieee802_11_find_bssid_profile(const u8 *start, size_t len,
					    struct ieee802_atbm_11_elems *elems,
					    u8 *transmitter_bssid,
					    u8 *bss_bssid,
					    u8 *nontransmitted_profile)
{
	const struct element *elem, *sub;
	size_t profile_len = 0;
	bool found = false;

	if (!bss_bssid || !transmitter_bssid)
		return profile_len;

	for_each_element_id(elem, ATBM_WLAN_EID_MULTIPLE_BSSID, start, len) {
		if (elem->datalen < 2)
			continue;

		for_each_element(sub, elem->data + 1, elem->datalen - 1) {
			u8 new_bssid[ETH_ALEN];
			const u8 *index;

			if (sub->id != 0 || sub->datalen < 4) {
				/* not a valid BSS profile */
				continue;
			}

			if (sub->data[0] != ATBM_WLAN_EID_NON_TX_BSSID_CAP ||
			    sub->data[1] != 2) {
				/* The first element of the
				 * Nontransmitted BSSID Profile is not
				 * the Nontransmitted BSSID Capability
				 * element.
				 */
				continue;
			}

			memset(nontransmitted_profile, 0, len);
			profile_len = cfg80211_merge_profile(start, len,
							     elem,
							     sub,
							     nontransmitted_profile,
							     len);

			/* found a Nontransmitted BSSID Profile */
			index = cfg80211_find_ie(ATBM_WLAN_EID_MULTI_BSSID_IDX,
						 nontransmitted_profile,
						 profile_len);
			if (!index || index[1] < 1 || index[2] == 0) {
				/* Invalid MBSSID Index element */
				continue;
			}

			cfg80211_gen_new_bssid(transmitter_bssid,
					       elem->data[0],
					       index[2],
					       new_bssid);
			if (ether_addr_equal(new_bssid, bss_bssid)) {
				found = true;
				elems->bssid_index_len = index[1];
				elems->bssid_index = (void *)&index[2];
				break;
			}
		}
	}

	return found ? profile_len : 0;
}
#endif
#endif  //CONFIG_ATBM_HE

u32 ieee802_11_parse_elems_crc(const u8 *start, size_t len, bool action,
			       struct ieee802_atbm_11_elems *elems,
			       u64 filter, u32 crc, u8 *transmitter_bssid,
			       u8 *bss_bssid)
{
#if defined(CONFIG_ATBM_HE) && defined(CONFIG_CFG880211_SUPPORT_HE)
	struct element *non_inherit = NULL;
#endif
	u8 *nontransmitted_profile=NULL;
	int nontransmitted_profile_len = 0;

	memset(elems, 0, sizeof(*elems));
	elems->ie_start = (u8 *)start;
	elems->total_len = len;
#if defined(CONFIG_ATBM_HE) && defined(CONFIG_CFG880211_SUPPORT_HE)
	nontransmitted_profile = kmalloc(len, GFP_ATOMIC);
	if (nontransmitted_profile) {
		nontransmitted_profile_len =
			ieee802_11_find_bssid_profile(start, len, elems,
						      transmitter_bssid,
						      bss_bssid,
						      nontransmitted_profile);
		non_inherit = (struct element *)cfg80211_find_ext_elem(WLAN_EID_EXT_NON_INHERITANCE,
					       nontransmitted_profile,
					       nontransmitted_profile_len);
	}
#endif  //CONFIG_ATBM_HE
	crc = _ieee802_11_parse_elems_crc(start, len, action, elems, filter,
					  crc/*, non_inherit*/);

	/* Override with nontransmitted profile, if found */
	if (nontransmitted_profile_len)
		_ieee802_11_parse_elems_crc(nontransmitted_profile,
					    nontransmitted_profile_len,
					    action, elems, 0, 0/*, NULL*/);

	if (elems->tim && !elems->parse_error) {
		const struct ieee80211_tim_ie *tim_ie = elems->tim;

		elems->dtim_period = tim_ie->dtim_period;
		elems->dtim_count = tim_ie->dtim_count;
	}
	
#ifdef CONFIG_ATBM_HE
	/* Override DTIM period and count if needed */
	if (elems->bssid_index &&
	    elems->bssid_index_len >=
	    offsetofend(struct atbm_ieee80211_bssid_index, dtim_period))
		elems->dtim_period = elems->bssid_index->dtim_period;

	if (elems->bssid_index &&
	    elems->bssid_index_len >=
	    offsetofend(struct atbm_ieee80211_bssid_index, dtim_count))
		elems->dtim_count = elems->bssid_index->dtim_count;
	
	if(nontransmitted_profile)
	kfree(nontransmitted_profile);
#endif  //CONFIG_ATBM_HE

	return crc;
}

void ieee802_11_parse_elems(const u8 *start, size_t len,
					  bool action,
					  struct ieee802_atbm_11_elems *elems,
					  u8 *transmitter_bssid,
					  u8 *bss_bssid)
{
	ieee802_11_parse_elems_crc(start, len, action, elems, 0, 0,
				   transmitter_bssid, bss_bssid);
}

void ieee80211_set_wmm_default(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct atbm_ieee80211_tx_queue_params qparam;
	int ac;
	bool use_11b;
	int aCWmin, aCWmax;

	if (!local->ops->conf_tx)
		return;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return;

	memset(&qparam, 0, sizeof(qparam));

	use_11b = (chan_state->conf.channel->band == IEEE80211_BAND_2GHZ) &&
		 !(sdata->flags & IEEE80211_SDATA_OPERATING_GMODE);

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		/* Set defaults according to 802.11-2007 Table 7-37 */
		aCWmax = 1023;
		if (use_11b)
			aCWmin = 31;
		else
			aCWmin = 15;

		switch (ac) {
		case 3: /* AC_BK */
			qparam.cw_max = aCWmax;
			qparam.cw_min = aCWmin;
			qparam.txop = 0;
			qparam.aifs = 7;
			break;
		default: /* never happens but let's not leave undefined */
		case 2: /* AC_BE */
			qparam.cw_max = aCWmax;
			qparam.cw_min = aCWmin;
			qparam.txop = 0;
			qparam.aifs = 3;
			break;
		case 1: /* AC_VI */
			qparam.cw_max = aCWmin;
			qparam.cw_min = (aCWmin + 1) / 2 - 1;
			if (use_11b)
				qparam.txop = 6016/32;
			else
				qparam.txop = 3008/32;
			qparam.aifs = 2;
			break;
		case 0: /* AC_VO */
			qparam.cw_max = (aCWmin + 1) / 2 - 1;
			qparam.cw_min = (aCWmin + 1) / 4 - 1;
			if (use_11b)
				qparam.txop = 3264/32;
			else
				qparam.txop = 1504/32;
			qparam.aifs = 2;
			break;
		}

		qparam.uapsd = false;

		sdata->tx_conf[ac] = qparam;
		drv_conf_tx(local, sdata, ac, &qparam);
	}

	/* after reinitialize QoS TX queues setting to default,
	 * disable QoS at all */

	if (sdata->vif.type != NL80211_IFTYPE_MONITOR) {
		sdata->vif.bss_conf.qos =
			sdata->vif.type != NL80211_IFTYPE_STATION;
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_QOS);
	}
}

void ieee80211_sta_def_wmm_params(struct ieee80211_sub_if_data *sdata,
				  const size_t supp_rates_len,
				  const u8 *supp_rates)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	int i, have_higher_than_11mbit = 0;

	/* cf. IEEE 802.11 9.2.12 */
	for (i = 0; i < supp_rates_len; i++)
		if ((supp_rates[i] & 0x7f) * 5 > 110)
			have_higher_than_11mbit = 1;

	if (chan_state->conf.channel->band == IEEE80211_BAND_2GHZ &&
	    have_higher_than_11mbit)
		sdata->flags |= IEEE80211_SDATA_OPERATING_GMODE;
	else
		sdata->flags &= ~IEEE80211_SDATA_OPERATING_GMODE;

	ieee80211_set_wmm_default(sdata);
}

u32 ieee80211_atbm_mandatory_rates(struct ieee80211_local *local,
			      enum ieee80211_band band)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *bitrates;
	u32 mandatory_rates;
	enum ieee80211_rate_flags mandatory_flag;
	int i;

	sband = local->hw.wiphy->bands[band];
	if (WARN_ON(!sband))
		return 1;

	if (band == IEEE80211_BAND_2GHZ)
		mandatory_flag = IEEE80211_RATE_MANDATORY_B;
	else
		mandatory_flag = IEEE80211_RATE_MANDATORY_A;

	bitrates = sband->bitrates;
	mandatory_rates = 0;
	for (i = 0; i < sband->n_bitrates; i++)
		if (bitrates[i].flags & mandatory_flag)
			mandatory_rates |= BIT(i);
	return mandatory_rates;
}

void ieee80211_send_auth(struct ieee80211_sub_if_data *sdata,
			 u16 transaction, u16 auth_alg, u16 status,
			 u8 *extra, size_t extra_len, const u8 *bssid,
			 const u8 *key, u8 key_len, u8 key_idx)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct atbm_ieee80211_mgmt *mgmt;
#ifdef CONFIG_ATBM_USE_SW_ENC
	int err;
#endif

	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom +
			    sizeof(*mgmt) + 6 + extra_len);
	if (!skb)
		return;

	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);

	mgmt = (struct atbm_ieee80211_mgmt *) atbm_skb_put(skb, 24 + 6);
	memset(mgmt, 0, 24 + 6);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_AUTH);
	memcpy(mgmt->da, bssid, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);
	mgmt->u.auth.auth_alg = cpu_to_le16(auth_alg);
	mgmt->u.auth.auth_transaction = cpu_to_le16(transaction);
	mgmt->u.auth.status_code = cpu_to_le16(status);
	if (extra)
		memcpy(atbm_skb_put(skb, extra_len), extra, extra_len);
	if (auth_alg == WLAN_AUTH_SHARED_KEY && transaction == 3) {
#ifdef CONFIG_ATBM_USE_SW_ENC
		mgmt->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		err = ieee80211_wep_encrypt(local, skb, key, key_len, key_idx);
		WARN_ON(err);
#else
		mgmt->u.auth.status_code = cpu_to_le16(1);
 		atbm_printk_always("%s:not support shared key\n",__func__);
#endif
	}
 	atbm_printk_mgmt( "%s %d transaction =%d,key_idx %d,key <%s> \n",__func__,__LINE__,transaction,key_idx,key);
	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	ieee80211_tx_skb(sdata, skb);
}

int ieee80211_build_preq_ies(struct ieee80211_sub_if_data *sdata,u8 *buffer,
			     const u8 *ie, size_t ie_len,
			     enum ieee80211_band band, u32 rate_mask,
			     u8 channel)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_supported_band *sband;
	u8 *pos;
	size_t offset = 0, noffset;
	int supp_rates_len, i;
	u8 rates[32];
	int num_rates;
	int ext_rates_len;
	const struct atbm_ieee80211_sta_he_cap *he_cap;	
	u8 *end = buffer + local->hw_scan_ies_bufsize;

	sband = local->hw.wiphy->bands[band];

	pos = buffer;
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
	if (!sband)
		goto out;
#endif /*ROAM_OFFLOAD*/
#endif
	num_rates = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if ((BIT(i) & rate_mask) == 0)
			continue; /* skip rate */
		if(num_rates<4){
			rates[num_rates++] = (u8) ((sband->bitrates[i].bitrate / 5)|0x80);
		}else{
			rates[num_rates++] = (u8) (sband->bitrates[i].bitrate / 5);

		}
	}

	supp_rates_len = min_t(int, num_rates, 8);

	*pos++ = ATBM_WLAN_EID_SUPP_RATES;
	*pos++ = supp_rates_len;
	memcpy(pos, rates, supp_rates_len);
	pos += supp_rates_len;

	/* insert "request information" if in custom IEs */
	if (ie && ie_len) {
		static const u8 before_extrates[] = {
			ATBM_WLAN_EID_SSID,
			ATBM_WLAN_EID_SUPP_RATES,
			ATBM_WLAN_EID_REQUEST,
		};
		noffset = atbm_ieee80211_ie_split(ie, ie_len,
					     before_extrates,
					     ARRAY_SIZE(before_extrates),
					     offset);
		memcpy(pos, ie + offset, noffset - offset);
		pos += noffset - offset;
		offset = noffset;
	}

	ext_rates_len = num_rates - supp_rates_len;
	if (ext_rates_len > 0) {
		*pos++ = ATBM_WLAN_EID_EXT_SUPP_RATES;
		*pos++ = ext_rates_len;
		memcpy(pos, rates + supp_rates_len, ext_rates_len);
		pos += ext_rates_len;
	}

	if (channel && sband->band == IEEE80211_BAND_2GHZ) {
		*pos++ = ATBM_WLAN_EID_DS_PARAMS;
		*pos++ = 1;
		*pos++ = channel;
	}

	/* insert custom IEs that go before HT */
	if (ie && ie_len) {
		static const u8 before_ht[] = {
			ATBM_WLAN_EID_SSID,
			ATBM_WLAN_EID_SUPP_RATES,
			ATBM_WLAN_EID_REQUEST,
			ATBM_WLAN_EID_EXT_SUPP_RATES,
			ATBM_WLAN_EID_DS_PARAMS,
			ATBM_WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
		};
		noffset = atbm_ieee80211_ie_split(ie, ie_len,
					     before_ht, ARRAY_SIZE(before_ht),
					     offset);
		memcpy(pos, ie + offset, noffset - offset);
		pos += noffset - offset;
		offset = noffset;
	}

	if (sband->ht_cap.ht_supported) {
		u16 cap = sband->ht_cap.cap;
		__le16 tmp;

		if(sdata->vif.p2p == true){
			cap &= ~(IEEE80211_HT_CAP_SUP_WIDTH_20_40 | 
		           IEEE80211_HT_CAP_DSSSCCK40 |
		           IEEE80211_HT_CAP_SGI_40);
		}
		*pos++ = ATBM_WLAN_EID_HT_CAPABILITY;
		*pos++ = sizeof(struct ieee80211_ht_cap);
		memset(pos, 0, sizeof(struct ieee80211_ht_cap));
		tmp = cpu_to_le16(cap);
		memcpy(pos, &tmp, sizeof(u16));
		pos += sizeof(u16);
		*pos++ = sband->ht_cap.ampdu_factor |
			 (sband->ht_cap.ampdu_density <<
				IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT);
		memcpy(pos, &sband->ht_cap.mcs, sizeof(sband->ht_cap.mcs));
		pos += sizeof(sband->ht_cap.mcs);
		pos += 2 + 4 + 1; /* ext info, BF cap, antsel */
	}
#ifdef CONFIG_ATBM_HE	
	if(local->scan_sdata){
		he_cap = atbm_ieee80211_get_he_iftype_cap(&local->hw,sband,
						     ieee80211_vif_type_p2p(&local->scan_sdata->vif));
		if (he_cap) {
			pos = ieee80211_ie_build_he_cap(pos, he_cap, end);
			if (!pos)
				goto out;
		}
	}
#endif  //CONFIG_ATBM_HE

	/*
	 * If adding more here, adjust code in main.c
	 * that calculates local->scan_ies_len.
	 */

	/* add any remaining custom IEs */
	if (ie && ie_len) {
		noffset = ie_len;
		memcpy(pos, ie + offset, noffset - offset);
		pos += noffset - offset;
	}
//#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
//#ifdef ROAM_OFFLOAD
out:
//#endif /*ROAM_OFFLOAD*/
//#endif
	return pos - buffer;
}

struct sk_buff *ieee80211_build_probe_req(struct ieee80211_sub_if_data *sdata,
					  u8 *dst, u32 ratemask,
					  const u8 *ssid, size_t ssid_len,
					  const u8 *ie, size_t ie_len,
					  bool directed)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct sk_buff *skb;
	struct atbm_ieee80211_mgmt *mgmt;
	size_t buf_len;
	u8 *buf;
	u8 chan;

	/* FIXME: come up with a proper value */
	buf = atbm_kmalloc(200 + ie_len, GFP_KERNEL);
	if (!buf)
		return NULL;

	/*
	 * Do not send DS Channel parameter for directed probe requests
	 * in order to maximize the chance that we get a response.  Some
	 * badly-behaved APs don't respond when this parameter is included.
	 */
	if (directed)
		chan = 0;
	else
		chan = ieee80211_frequency_to_channel(
			channel_center_freq(chan_state->conf.channel));

	buf_len = ieee80211_build_preq_ies(sdata, buf, ie, ie_len,
					   chan_state->conf.channel->band,
					   ratemask, chan);

	skb = ieee80211_probereq_get(&local->hw, &sdata->vif,
				     ssid, ssid_len,
				     buf, buf_len,NULL);
	if (!skb)
		goto out;

	if (dst) {
		mgmt = (struct atbm_ieee80211_mgmt *) skb->data;
		memcpy(mgmt->da, dst, ETH_ALEN);
		memcpy(mgmt->bssid, dst, ETH_ALEN);
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;

 out:
	atbm_kfree(buf);

	return skb;
}

void ieee80211_send_probe_req(struct ieee80211_sub_if_data *sdata, u8 *dst,
			      const u8 *ssid, size_t ssid_len,
			      const u8 *ie, size_t ie_len,
			      u32 ratemask, bool directed, bool no_cck)
{
	struct sk_buff *skb;

	skb = ieee80211_build_probe_req(sdata, dst, ratemask, ssid, ssid_len,
					ie, ie_len, directed);
	if (skb) {
		if (no_cck)
			IEEE80211_SKB_CB(skb)->flags |=
				IEEE80211_TX_CTL_NO_CCK_RATE;
		ieee80211_tx_skb(sdata, skb);
	}
}
bool ieee80211_send_special_probe_req(struct ieee80211_sub_if_data *sdata, u8 *dst,
			      const u8 *ssid, size_t ssid_len,
			      const u8 *special_ie, size_t special_ie_len)
{
	u8 *special = NULL;
	
	if((sdata->vif.type != NL80211_IFTYPE_STATION)&&(sdata->vif.type != NL80211_IFTYPE_MONITOR)){
		return false;
	}

	if(special_ie_len > 255){
		return false;
	}

	if(special_ie&&special_ie_len){
		special = atbm_kmalloc(special_ie_len+2,GFP_ATOMIC);

		if(special == NULL){
			return false;
		}

		special[0] = ATBM_WLAN_EID_PRIVATE;
		special[1] = special_ie_len;

		memcpy(&special[2],special_ie,special_ie_len);
	}

	ieee80211_send_probe_req(sdata,dst,ssid,ssid_len,special,special_ie_len?special_ie_len+2:0,-1,true,false);

	if(special)
		atbm_kfree(special);
	return true;
}

bool ieee80211_send_special_probe_response(struct ieee80211_sub_if_data *sdata, u8 *dst,
			      const u8 *special_ie, size_t special_ie_len)
{
	struct sk_buff *skb = NULL;
	struct atbm_ieee80211_mgmt *mgmt = NULL;
	
	if(sdata->vif.type != NL80211_IFTYPE_AP){
		return false;
	}

	if(special_ie_len > 255){
		return false;
	}

#ifdef ATBM_PROBE_RESP_EXTRA_IE
	skb = ieee80211_proberesp_get(&sdata->local->hw,&sdata->vif);
#endif
	if(skb == NULL){
		return false;
	}

	if(special_ie && special_ie_len){
		/*
		*add special ie
		*/
		u8 *special = NULL;
		
		if(atbm_pskb_expand_head(skb,0,special_ie_len+2,GFP_ATOMIC)){
			return false;
		}

		
		special = skb->data + skb->len;

		*special++ = ATBM_WLAN_EID_PRIVATE;
		*special++ = special_ie_len;
		memcpy(special,special_ie,special_ie_len);

		atbm_skb_put(skb,special_ie_len+2);
	}

	mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	memcpy(mgmt->da,dst,6);
	
	ieee80211_tx_skb(sdata, skb);
	
	return true;
}
void ieee80211_ap_rx_queued_mgmt_special(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb)
{
#if 1
	  /*
	  *the follow code is a demo , add other by yourself
	  */
	  struct ieee80211_rx_status *rx_status;
	  struct atbm_ieee80211_mgmt *mgmt;
	  struct ieee802_atbm_11_elems elems;
	  int baselen;
	  rx_status = (struct ieee80211_rx_status *) skb->cb;
	  mgmt = (struct atbm_ieee80211_mgmt *)skb->data;

	  if (skb->len < 24)
		  return;
	  if(ieee80211_is_probe_req(mgmt->frame_control)){
	      u8 *elements = NULL;
		  const u8 *atbm_ie = NULL;
		  const u8 *special = NULL;
		  int special_len = 0;
		  int freq;
		  baselen = offsetof(struct atbm_ieee80211_mgmt, u.probe_req.variable);
		  if (baselen > skb->len){
			  atbm_printk_err("[probe req] error ! \n");
			  return;
		  }
		  elements = mgmt->u.probe_req.variable;
		  ieee802_11_parse_elems(elements, skb->len - baselen, false, &elems, mgmt->bssid, NULL);
		  if (elems.ds_params && elems.ds_params_len == 1)
			  freq = ieee80211_channel_to_frequency(elems.ds_params[0],
								rx_status->band);
		  else
			  freq = rx_status->freq;
	  
		  //elements = mgmt->u.probe_req.variable;
		  atbm_ie = atbm_ieee80211_find_ie(ATBM_WLAN_EID_PRIVATE,elements,
					(int)(skb->len-offsetof(struct atbm_ieee80211_mgmt, u.probe_req.variable)));
  
		  if(atbm_ie){
			  char special_data[255] = {0};
			  special_len = atbm_ie[1];
			  special = &atbm_ie[2];
			  memcpy(special_data,special,special_len);
			  /*
			  *send probe response
			  */
			  atbm_printk_cfg("[probe req] from [%pM] channel[%d] special ie[%d][%d][%s]\n",mgmt->sa,freq,atbm_ie[0],atbm_ie[1],special_data);
			  memcpy(special_data,"RECV_PROBE_REQ",14);
			  special_data[14] = 0;
			  ieee80211_send_special_probe_response(sdata,mgmt->sa,special_data,14);
			  
		  }else{
			  atbm_printk_cfg("[probe req] from [%pM] channel[%d] \n",mgmt->sa,freq);
		  }
	  }else {
		  /*
		  *other frame
		  */	  
	  }
#endif

}
static bool ieee80211_update_special_ie(struct ieee80211_sub_if_data *sdata,enum ieee80211_special_work_type type,
												enum atbm_ieee80211_eid eid, const u8 *special_ie, size_t special_ie_len)
{
	bool res = true;
	struct sk_buff *skb;
	u8 *special = NULL;
	struct ieee80211_update_special *special_update;

	if((type < IEEE80211_SPECIAL_NONE_TYPE) ||(type>IEEE80211_SPECIAL_STA_SPECIAL_PROBR)){
		atbm_printk_err("%s _ %d \n",__func__,__LINE__);
		res = false;
		goto exit;
	}
	if((special_ie) && (special_ie_len > 0)){
		if((!!special_ie) ^ (!!special_ie_len)){
			atbm_printk_err("%s _ %d \n",__func__,__LINE__);
			res = false;
			goto exit;
		}

		skb = atbm_dev_alloc_skb(special_ie_len+2);

		if(skb == NULL){
			atbm_printk_err("%s _ %d \n",__func__,__LINE__);
			res = false;
			goto exit;
		}
		special = skb->data;
		special[0] = eid;
		special[1] = special_ie_len;

		memcpy(&special[2],special_ie,special_ie_len);
		atbm_skb_put(skb,special_ie_len+2);
		skb->pkt_type = type;
		special_update = (struct ieee80211_update_special*)skb->cb;

		special_update->req_sdata = sdata;
		special_update->special_ie = special;
		special_update->special_len = special_ie_len+2;
	}else{
		skb = atbm_dev_alloc_skb(special_ie_len+2);

		if(skb == NULL){
			atbm_printk_err("%s _ %d \n",__func__,__LINE__);
			res = false;
			goto exit;
		}
		special = skb->data;
		special[0] = eid;
		special[1] = 0;

	
		skb->pkt_type = type;
		special_update = (struct ieee80211_update_special*)skb->cb;

		special_update->req_sdata = sdata;
		special_update->special_ie = NULL;
		special_update->special_len = 0;
	}
	
	atbm_skb_queue_tail(&sdata->local->special_req_list, skb);
	atbm_schedule_work(&sdata->local->special_work);
	res = true;
	
exit:
	return res;
}
bool ieee80211_ap_update_special_beacon(struct ieee80211_sub_if_data *sdata,
		const u8 *special_ie, size_t special_ie_len)
{
	bool res = true;
	/*
	*only ap mode can update beacon
	*/
	if(sdata->vif.type != NL80211_IFTYPE_AP){
		res = false;
		goto exit;
	}
	/*
	*make sure that ,ap mode is running now
	*/
	
	if (!ieee80211_sdata_running(sdata)){
		res = false;
		goto exit;
	}

	res = ieee80211_update_special_ie(sdata,IEEE80211_SPECIAL_AP_SPECIAL_BEACON,ATBM_WLAN_EID_PRIVATE,special_ie,special_ie_len);
exit:
	return res;
}

bool ieee80211_ap_update_special_probe_response(struct ieee80211_sub_if_data *sdata,
		const u8 *special_ie, size_t special_ie_len)
{
	bool res = true;
	/*
	*only ap mode can update beacon
	*/
	if(sdata->vif.type != NL80211_IFTYPE_AP){
		res = false;
		goto exit;
	}
	/*
	*make sure that ,ap mode is running now
	*/
	
	if (!ieee80211_sdata_running(sdata)){
		res = false;
		goto exit;
	}

	res = ieee80211_update_special_ie(sdata,IEEE80211_SPECIAL_AP_SPECIAL_PROBRSP,ATBM_WLAN_EID_PRIVATE,special_ie,special_ie_len);
exit:
	return res;
}

bool ieee80211_ap_update_special_probe_request(struct ieee80211_sub_if_data *sdata,
		const u8 *special_ie, size_t special_ie_len)
{
	bool res = true;
	/*
	*only sta mode can update beacon
	*/
	if(sdata->vif.type != NL80211_IFTYPE_STATION){
		res = false;
		goto exit;
	}
	/*
	*make sure that ,sta mode is running now
	*/
	
	if (!ieee80211_sdata_running(sdata)){
		res = false;
		goto exit;
	}

	res = ieee80211_update_special_ie(sdata,IEEE80211_SPECIAL_STA_SPECIAL_PROBR,ATBM_WLAN_EID_PRIVATE,special_ie,special_ie_len);
exit:
	return res;
}

bool ieee80211_ap_update_vendor_probe_request(struct ieee80211_sub_if_data *sdata,
		const u8 *special_ie, size_t special_ie_len)
{
	bool res = true;
	/*
	*only sta mode can update beacon
	*/
	if(sdata->vif.type != NL80211_IFTYPE_STATION){
		res = false;
		goto exit;
	}
	/*
	*make sure that ,sta mode is running now
	*/
	
	if (!ieee80211_sdata_running(sdata)){
		res = false;
		goto exit;
	}

	res = ieee80211_update_special_ie(sdata,IEEE80211_SPECIAL_STA_SPECIAL_PROBR,ATBM_WLAN_EID_VENDOR_SPECIFIC,special_ie,special_ie_len);
exit:
	return res;
}


/*
*ieee80211_sta_triger_passive_scan - triger sta into passive scan mode
*
*@sdata       interface of the sta will be in sta mode
*@channels    channel list. if null ,will scan all of 2.4G channel
*@n_channels  number of channel in the channel list
*/
bool ieee80211_sta_triger_passive_scan(struct ieee80211_sub_if_data *sdata,
													u8 *channels,size_t n_channels)
{
	bool res = true;
	struct sk_buff *skb;
	struct ieee80211_special_work_scan *scan_req;
	size_t i;
	/*
	*make sure that ,sta mode is running now
	*/
	
	if (!ieee80211_sdata_running(sdata)){
		res = false;
		goto exit;
	}

	/*
	*only station mode can triger scan
	*/
	if(sdata->vif.type != NL80211_IFTYPE_STATION){
		res = false;
		goto exit;
	}

	if((!!channels) ^ (!!n_channels)){
		res = false;
		goto exit;
	}
	
	if(n_channels >= IEEE80211_ATBM_MAX_SCAN_CHANNEL_INDEX)
	{
		res = false;
		goto exit;
	}
	
	for(i = 0;i<n_channels;i++){
		if(ieee8011_channel_valid(&sdata->local->hw,channels[i]) == false){
			res = false;
			goto exit;
		}
	}
	
	skb = atbm_dev_alloc_skb(n_channels);

	if(skb == NULL){
		res = false;
		goto exit;
	}

	skb->pkt_type = IEEE80211_SPECIAL_STA_PASSICE_SCAN;
	scan_req = (struct ieee80211_special_work_scan*)skb->cb;
	memset(scan_req,0,sizeof(struct ieee80211_special_work_scan));
	scan_req->scan_sdata = sdata;
	if(n_channels&&channels){
		scan_req->channels = skb->data;
		memcpy(scan_req->channels,channels,n_channels);
		scan_req->n_channels = n_channels;
	}

	atbm_skb_queue_tail(&sdata->local->special_req_list, skb);
	atbm_schedule_work(&sdata->local->special_work);
	res = true;
exit:
	return res;
}
/*
*ieee80211_sta_triger_positive_scan - triger sta into positive scan mode
*
*@sdata       interface of the sta will be in scan mode
*@channels    channel list .if null ,will scan all of 2.4G channel
*@n_channels  number of channel in the channel list
*@ssid        ssid will be scanning
*@ssid_len    length of the ssid
*@ie          special ie will be inclued in probe request
*@ie_len      length of the ie
*/
bool ieee80211_sta_triger_positive_scan(struct ieee80211_sub_if_data *sdata,
													  u8 *channels,size_t n_channels,
													  u8 *ssid,size_t ssid_len,
													  u8 *ie,u16 ie_len,
													  u8 *bssid)
{
	bool res = true;
	struct sk_buff *skb;
	struct ieee80211_special_work_scan *scan_req;
	size_t len = 0;
	size_t i = 0;
	void *pos;
	void *pos_end;
	/*
	*make sure that ,sta mode is running now
	*/
	
	if (!ieee80211_sdata_running(sdata)){
		res = false;
		goto exit;
	}

	/*
	*only station mode can triger scan
	*/
	if(sdata->vif.type != NL80211_IFTYPE_STATION){
		res = false;
		goto exit;
	}

	if((!!channels) ^ (!!n_channels)){
		res = false;
		goto exit;
	}
	
	if(n_channels >= IEEE80211_ATBM_MAX_SCAN_CHANNEL_INDEX)
	{
		res = false;
		goto exit;
	}
	
	for(i = 0;i<n_channels;i++){
		if(ieee8011_channel_valid(&sdata->local->hw,channels[i]) == false){
			res = false;
			goto exit;
		}
	}
	
	if((!!ssid) ^ (!!ssid_len)){
		res = false;
		goto exit;
	}
	
	if(ssid_len > IEEE80211_MAX_SSID_LEN){
		res = false;
		goto exit;
	}
	
	if((!!ie) ^ (!!ie_len)){
		res = false;
		goto exit;
	}
	
	if(ie_len>257){
		res = false;
		goto exit;
	}
	
	len = n_channels+ie_len;

	if(bssid)
		len += 6;
	
	if(ssid && ssid_len)
		len += sizeof(struct cfg80211_ssid);
	
	skb = atbm_dev_alloc_skb(len);

	if(skb == NULL){
		res = false;
		goto exit;
	}

	skb->pkt_type = IEEE80211_SPECIAL_STA_POSITIVE_SCAN;
	scan_req = (struct ieee80211_special_work_scan*)skb->cb;
	memset(scan_req,0,sizeof(struct ieee80211_special_work_scan));
	scan_req->scan_sdata = sdata;

	pos = (void*)skb->data;
	pos_end = (void*)(skb->data+len);
	/*
	*ssid
	*/
	if(ssid){
		scan_req->ssid = pos;
		
		BUG_ON(scan_req->ssid == NULL);
		scan_req->ssid->ssid_len = ssid_len;
		memcpy(scan_req->ssid->ssid,ssid,ssid_len);
		
		pos = (void*)(scan_req->ssid + 1);
	}
	/*
	*channel
	*/
	if(channels){
		
		scan_req->channels = pos;
		scan_req->n_channels = n_channels;
		memcpy(scan_req->channels,channels,n_channels);

		pos = (void*)(scan_req->channels + n_channels);
	}
	/*
	*ie
	*/
	if(ie){
		scan_req->ie = pos;
		scan_req->ie_len = ie_len;
		memcpy(scan_req->ie,ie,ie_len);
		pos = (void*)(scan_req->ie+ie_len);
	}

	if(bssid){
		scan_req->bssid = pos;
		memcpy(scan_req->bssid,bssid,6);
	}
	
	WARN_ON(pos != pos_end);
	atbm_skb_queue_tail(&sdata->local->special_req_list, skb);
	atbm_schedule_work(&sdata->local->special_work);
	res = true;
exit:	
	return res;
}


extern int atbm_internal_recv_6441_vendor_ie(struct atbm_vendor_cfg_ie *recv_ie);
#define ETH_P_CUSTOM 0x88cc
#define ETH_P_EAPOL 0x888E

int ieee80211_send_L2_2_hight_layer(struct ieee80211_sub_if_data *sdata,
											  struct sk_buff *skb,struct net_device *dev);
extern int ieee80211_send_mgmt_to_wpa_supplicant(struct ieee80211_sub_if_data *sdata,
											  struct sk_buff *skb);

int ieee80211_add_8023_header(struct sk_buff *skb, const char *addr,
			   enum nl80211_iftype iftype)
{
  struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
  unsigned short hdrlen;
  struct ethhdr *ehdr;
  unsigned short len;
  unsigned char dst[6];
  unsigned char src[6];
  char *da,*sa,*bssid;

  
  hdrlen = ieee80211_hdrlen(hdr->frame_control);
  
  if(iftype == NL80211_IFTYPE_AP || iftype == NL80211_IFTYPE_P2P_GO){
	  bssid = hdr->addr1;
	  sa = hdr->addr2;
	  da = hdr->addr3;
  }else{
	  da = hdr->addr1;
	  bssid = hdr->addr2;
	  sa = hdr->addr3;
	  
  }
  memcpy(dst, da, 6);
  memcpy(src, sa, 6);
  
  //atbm_skb_pull(skb, hdrlen);
  //len = htons(skb->len);
  len = sizeof(struct ethhdr);
  ehdr = (struct ethhdr *) atbm_skb_push(skb,len);
  memcpy(ehdr->h_dest, addr, 6);
  memcpy(ehdr->h_source, src, 6);
  ehdr->h_proto = htons(ETH_P_CUSTOM);
  return 0;
}

int ieee80211_add_simple_ratp_header(struct ieee80211_sub_if_data *sdata,struct sk_buff *skb)
{
  struct ieee80211_local *local = sdata->local;
  struct ieee80211_rate *rate = NULL;
  struct ieee80211_supported_band *sband;
  struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
//  struct ieee80211_hw *hw = &local->hw;

  sband = local->hw.wiphy->bands[status->band];
  if (WARN_ON(!sband))
	  goto drop;

  rate = &sband->bitrates[status->rate_idx];
  status->rx_flags = 0;
  
drop:
  return -1;
}
int ieee80211_send_L2_2_hight_layer(struct ieee80211_sub_if_data *sdata,
											  struct sk_buff *skb,struct net_device *dev)
{
  int res = -1;
  struct sk_buff *xmit_skb = NULL;
  xmit_skb = atbm_skb_clone(skb, GFP_ATOMIC);
  if (!xmit_skb && net_ratelimit())
	  atbm_printk_err( "ieee80211_send_L2_2_hight_layer: failed to clone "
			 "multicast frame\n");
  res = ieee80211_add_8023_header(xmit_skb, sdata->vif.addr, sdata->vif.type);
  if (res < 0){
	  atbm_printk_err("ieee80211_data_to_8023 faile!! \n");
	  atbm_dev_kfree_skb(xmit_skb);
	  return res;
  } 	 
  if (xmit_skb && dev) {	  
	  xmit_skb->dev = dev;	  
	  eth_type_trans(xmit_skb, dev);
  //  atbm_printk_err("[atbm_netif_receive_skb] protocol ==> %x \n ",eth_type_trans(xmit_skb, dev));
	  xmit_skb->ip_summed = CHECKSUM_UNNECESSARY;
	  xmit_skb->protocol = htons(ETH_P_CUSTOM);
	  //xmit_skb->pkt_type = PACKET_OTHERHOST;
	  memset(xmit_skb->cb, 0, sizeof(xmit_skb->cb));
	  if(xmit_skb->len > 0){
		  res = atbm_netif_receive_skb(xmit_skb);
		  if(res < 0){
			  atbm_printk_err("[error] ieee80211_send_L2_2_hight_layer err! \n ");
			  atbm_dev_kfree_skb(xmit_skb);
		  }
	  }else{
		  atbm_printk_err("skb->len = 0 \n");
	  }
  }else{
	  atbm_printk_err("skb=%p dev=%p \n",xmit_skb,dev);
  }
  return res;
}


#ifdef CONFIG_ATBM_STA_LISTEN

static  const u8 *atbm_ieee80211_find_vendor_cfg_ie(u8 eid, const u8 *ies, int len)
{
  if(len < 2){
	  return NULL;
  }
  while (1) {
	  len -= ies[1] + 2;
	  ies += ies[1] + 2;
	  if(len < 2){
		  break;
	  }
	  if(ies[0] == 221 && ies[1] > 3 && ies[2] == 0x41 && ies[3] == 0x54 && ies[4] == 0x42){
		  break;
	  }
  }
  
  if (len < 2)
	  return NULL;
  if (len < 2 + ies[1])
	  return NULL;
  return ies;
}

void ieee80211_sta_rx_queued_mgmt_special(struct ieee80211_sub_if_data *sdata,
				  struct sk_buff *skb)
{
	#if 1
		struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
		u8 *elements;
		int baselen;
		struct ieee802_atbm_11_elems elems;
		struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
		int freq;
		char ssid[32]={0};
		u8 *ie = NULL;
		struct atbm_vendor_cfg_ie *private_ie;
		u8 OUI[4];
		/*
		*the follow code is a demo , add other by yourself
		*/
		
		if (skb->len < 24){
			atbm_printk_err("ieee80211_sta_rx_queued_mgmt_special:skb->len < 24 \n");
			return;
		}
	
		if (ieee80211_is_probe_resp(mgmt->frame_control)) {
	
			//atbm_printk_err("recv probe resp! \n");
	
			private_ie = (struct atbm_vendor_cfg_ie *)atbm_ieee80211_find_vendor_cfg_ie(221,mgmt->u.probe_resp.variable,
									   skb->len-offsetof(struct atbm_ieee80211_mgmt, u.probe_resp.variable));
			
			if(private_ie){
				
			//	atbm_printk_err("[%s] recv from ssid[%s],psk[%s] sa[%pM],da[%pM],priv_bssid[%pM] \n",
			//			sdata->name,private_ie->ssid,private_ie->password,mgmt->sa,mgmt->da,sdata->vif.addr);
				
				if(memcmp(mgmt->da,sdata->vif.addr,6) != 0){
					memcpy(mgmt->da,sdata->vif.addr,6);
				}
				atbm_internal_recv_6441_vendor_ie(private_ie);
				/* send data to up layer*/
				ieee80211_send_L2_2_hight_layer(sdata,skb,sdata->dev);
				
				
			}
	
	
		} else if(ieee80211_is_beacon(mgmt->frame_control)){
#if 1
				baselen = offsetof(struct atbm_ieee80211_mgmt, u.beacon.variable);
			if (baselen > skb->len){
				atbm_printk_cfg("[beacon] error ! \n");
				return;
			}
			elements = mgmt->u.beacon.variable;
			ieee802_11_parse_elems(elements, skb->len - baselen,false, &elems,NULL,NULL);
			if (elems.ds_params && elems.ds_params_len == 1)
				freq = ieee80211_channel_to_frequency(elems.ds_params[0],
								  rx_status->band);
			else
				freq = rx_status->freq;
	
			freq = (freq - 2412)/5 + 1;
			if(elems.ssid && elems.ssid_len)
				memcpy(ssid,elems.ssid,elems.ssid_len > 32 ? 32 : elems.ssid_len);
			ie = (u8 *)atbm_ieee80211_find_ie(ATBM_WLAN_EID_PRIVATE,mgmt->u.beacon.variable,
									   skb->len-offsetof(struct atbm_ieee80211_mgmt, u.beacon.variable));			
			if(ie){
				u8 special_data[255]={0};
				ie[2+ie[1]] = 0;
				atbm_printk_cfg("==========>>>[beacon] from [%pM] channel[%d] ssid[%s] ie[%d][%d][%s]\n",mgmt->bssid,freq,ssid,ie[0],ie[1],ie+2);
				
				memcpy(special_data,"RECV_BEACON",11);
				//ieee80211_send_special_probe_req(sdata, NULL, NULL,0, special_data, 11);
				
				//atbm_printk_err("recv_beacon ####################### \n");
				/*
				if(!sned_one){
				special_data[0] = ATBM_WLAN_EID_PRIVATE;
				special_data[1] = 11;
				memcpy(special_data + 2,"RECV_BEACON",11);
				ieee80211_sta_triger_positive_scan(sdata,&freq,1,NULL,0,&special_data[0],special_data[1] + 2);
					sned_one=true;
				}
				*/
				
			}
			else{
				atbm_printk_cfg("[beacon] from [%pM] channel[%d] ssid[%s] \n",mgmt->bssid,freq,ssid);
				return ;
			}
#endif
		}else if(ieee80211_is_action(mgmt->frame_control)) {
			//atbm_printk_err("[action] from [%pM]	\n",mgmt->bssid);
		}else if(ieee80211_is_probe_req(mgmt->frame_control)){
		
			
			private_ie = (struct atbm_vendor_cfg_ie *)atbm_ieee80211_find_ie(221,mgmt->u.probe_req.variable,
									   skb->len-offsetof(struct atbm_ieee80211_mgmt, u.probe_req.variable));
			
			if(private_ie){
				OUI[0] = (ATBM_6441_PRIVATE_OUI >> 24) & 0xFF;
				OUI[1] = (ATBM_6441_PRIVATE_OUI >> 16) & 0xFF;
				OUI[2] = (ATBM_6441_PRIVATE_OUI >> 8) & 0xFF;
				OUI[3] = ATBM_6441_PRIVATE_OUI & 0xFF;
				if(memcmp(private_ie->OUI,OUI,4) == 0){
					atbm_printk_err("[%s] recv from ssid[%s],psk[%s] sa[%pM],da[%pM],priv_bssid[%pM] \n",
							sdata->name,private_ie->ssid,private_ie->password,mgmt->sa,mgmt->da,sdata->vif.addr);
					
					if(memcmp(mgmt->da,sdata->vif.addr,6) != 0){
						memcpy(mgmt->da,sdata->vif.addr,6);
					}
					atbm_internal_recv_6441_vendor_ie(private_ie);
					/* send data to up layer*/
					ieee80211_send_L2_2_hight_layer(sdata,skb,sdata->dev);
					
				}
			}
		}else{
			//atbm_printk_err("frame_control[0x%x] from [%pM]  \n",mgmt->frame_control,mgmt->bssid);
		}
	
#endif

}
#endif
u32 ieee80211_sta_get_rates(struct ieee80211_local *local,
			    struct ieee802_atbm_11_elems *elems,
			    enum ieee80211_band band)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *bitrates;
	size_t num_rates;
	u32 supp_rates;
	int i, j;
	sband = local->hw.wiphy->bands[band];

	if (WARN_ON(!sband))
		return 1;

	bitrates = sband->bitrates;
	num_rates = sband->n_bitrates;
	supp_rates = 0;
	for (i = 0; i < elems->supp_rates_len +
		     elems->ext_supp_rates_len; i++) {
		u8 rate = 0;
		int own_rate;
		if (i < elems->supp_rates_len)
			rate = elems->supp_rates[i];
		else if (elems->ext_supp_rates)
			rate = elems->ext_supp_rates
				[i - elems->supp_rates_len];
		own_rate = 5 * (rate & 0x7f);
		for (j = 0; j < num_rates; j++)
			if (bitrates[j].bitrate == own_rate)
				supp_rates |= BIT(j);
	}
	return supp_rates;
}

void ieee80211_stop_device(struct ieee80211_local *local)
{
	atbm_flush_workqueue(local->workqueue);
	drv_stop(local);
}
#if defined (CONFIG_PM)
int ieee80211_reconfig(struct ieee80211_local *local)
{
	struct ieee80211_hw *hw = &local->hw;
#if defined (ATBM_SUSPEND_REMOVE_INTERFACE) || defined (ATBM_SUPPORT_WOW)
	struct sta_info *sta;
	int res;
#endif
#if defined (CONFIG_PM) ||defined (ATBM_SUSPEND_REMOVE_INTERFACE) || defined (ATBM_SUPPORT_WOW)
	struct ieee80211_sub_if_data *sdata;
#endif
#ifdef ATBM_SUSPEND_REMOVE_INTERFACE
	int i;
#endif

#ifdef CONFIG_PM
	if (local->suspended)
		local->resuming = true;
	atbm_printk_pm("%s:wowlan(%d)\n",__func__,local->wowlan);
#ifdef ATBM_SUPPORT_WOW
	if (local->wowlan) {
		bool suspended = local->suspended;
		local->wowlan = false;
		/*
		* When the driver is resumed, first data is dropped
		* by MAC layer. To eliminate it, the local->suspended
		* should be clear.
		*/
		local->suspended = false;
		res = drv_resume(local);
		if (res < 0) {
			local->suspended = suspended;
			local->resuming = false;
			return res;
		}
		if (res == 0)
			goto wake_up;
		WARN_ON(res > 1);
		/*
		 * res is 1, which means the driver requested
		 * to go through a regular reset on wakeup.
		 */
	}
#endif
#endif
#ifdef ATBM_SUSPEND_REMOVE_INTERFACE
	/* setup fragmentation threshold */
	drv_set_frag_threshold(local, hw->wiphy->frag_threshold);

	/* reset coverage class */
	drv_set_coverage_class(local, hw->wiphy->coverage_class);

	/* everything else happens only if HW was up & running */
	if (!local->open_count)
		goto wake_up;

	/*
	 * Upon resume hardware can sometimes be goofy due to
	 * various platform / driver / bus issues, so restarting
	 * the device may at times not work immediately. Propagate
	 * the error.
	 */
	res = drv_start(local);
	if (res) {
		WARN(local->suspended, "Hardware became unavailable "
		     "upon resume. This could be a software issue "
		     "prior to suspend or a hardware issue.\n");
		return res;
	}

	/* add interfaces */
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_MONITOR &&
		    ieee80211_sdata_running(sdata))
			res = drv_add_interface(local, &sdata->vif);
	}
	/* reconfigure tx conf */
	if (hw->queues >= IEEE80211_NUM_ACS) {
		list_for_each_entry(sdata, &local->interfaces, list) {
			if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
			    sdata->vif.type == NL80211_IFTYPE_MONITOR ||
			    !ieee80211_sdata_running(sdata))
				continue;

			for (i = 0; i < IEEE80211_NUM_ACS; i++)
				drv_conf_tx(local, sdata, i,
					    &sdata->tx_conf[i]);
		}
	}

	/* reconfigure hardware */
	ieee80211_hw_config(local, ~0);

	list_for_each_entry(sdata, &local->interfaces, list)
		ieee80211_configure_filter(sdata);

	/* Finally also reconfigure all the BSS information */
	list_for_each_entry(sdata, &local->interfaces, list) {
		u32 changed;

		if (!ieee80211_sdata_running(sdata))
			continue;

		/* common change flags for all interface types */
		changed = BSS_CHANGED_ERP_CTS_PROT |
			  BSS_CHANGED_ERP_PREAMBLE |
			  BSS_CHANGED_ERP_SLOT |
			  BSS_CHANGED_HT |
			  BSS_CHANGED_BASIC_RATES |
			  BSS_CHANGED_BEACON_INT |
			  BSS_CHANGED_BSSID |
			  BSS_CHANGED_CQM |
			  BSS_CHANGED_QOS |
			  BSS_CHANGED_PS  |
			  BSS_CHANGED_HT_CHANNEL_TYPE;

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			changed |= BSS_CHANGED_ASSOC;
			mutex_lock(&sdata->u.mgd.mtx);
			if(sdata->vif.bss_conf.assoc){
				changed |= (BSS_CHANGED_STA_RESTART|BSS_CHANGED_ASSOC);
				if (sdata->vif.bss_conf.arp_filter_enabled) {
					changed |= BSS_CHANGED_ARP_FILTER;
				}
			}
			ieee80211_bss_info_change_notify(sdata, changed);
			mutex_unlock(&sdata->u.mgd.mtx);
			break;
#ifdef CONFIG_ATBM_SUPPORT_IBSS
		case NL80211_IFTYPE_ADHOC:
			changed |= BSS_CHANGED_IBSS;
#endif
			/* fall through */
		case NL80211_IFTYPE_AP:
			changed |= BSS_CHANGED_SSID;
			/* fall through */
#ifdef CONFIG_MAC80211_ATBM_MESH
		case NL80211_IFTYPE_MESH_POINT:
#endif
			changed |= BSS_CHANGED_BEACON |
				   BSS_CHANGED_BEACON_ENABLED;
			ieee80211_bss_info_change_notify(sdata, changed);
			break;
		case NL80211_IFTYPE_WDS:
			break;
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
			/* ignore virtual */
			break;
		case NL80211_IFTYPE_UNSPECIFIED:
		case NUM_NL80211_IFTYPES:
#ifdef CONFIG_ATBM_SUPPORT_P2P
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_P2P_GO:
#endif
			WARN_ON(1);
			break;
		default:
			break;
		}
	}
	/* add STAs back */
	mutex_lock(&local->sta_mtx);
	list_for_each_entry(sta, &local->sta_list, list) {
		if (sta->uploaded) {
			sdata = sta->sdata;
			if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
				sdata = container_of(sdata->bss,
					     struct ieee80211_sub_if_data,
					     u.ap);

			WARN_ON(drv_sta_add(local, sdata, &sta->sta));
		}
	}
	mutex_unlock(&local->sta_mtx);
	/*
	 * Clear the WLAN_STA_BLOCK_BA flag so new aggregation
	 * sessions can be established after a resume.
	 *
	 * Also tear down aggregation sessions since reconfiguring
	 * them in a hardware restart scenario is not easily done
	 * right now, and the hardware will have lost information
	 * about the sessions, but we and the AP still think they
	 * are active. This is really a workaround though.
	 */
	if (hw->flags & IEEE80211_HW_AMPDU_AGGREGATION) {
		mutex_lock(&local->sta_mtx);

		list_for_each_entry(sta, &local->sta_list, list) {
			ieee80211_sta_tear_down_BA_sessions(sta, true);
			clear_sta_flag(sta, WLAN_STA_BLOCK_BA);
		}

		mutex_unlock(&local->sta_mtx);
	}

	/* add back keys */
	list_for_each_entry(sdata, &local->interfaces, list)
		if (ieee80211_sdata_running(sdata))
			ieee80211_enable_keys(sdata);


	/* setup RTS threshold */
	list_for_each_entry(sdata, &local->interfaces, list)
	    drv_set_rts_threshold(local, sdata, sdata->vif.bss_conf.rts_threshold);
#endif
#if defined (ATBM_SUSPEND_REMOVE_INTERFACE) || defined (ATBM_SUPPORT_WOW)
 wake_up:
#endif
	ieee80211_wake_queues_by_reason(hw,
			IEEE80211_QUEUE_STOP_REASON_SUSPEND);

	/*
	 * If this is for hw restart things are still running.
	 * We may want to change that later, however.
	 */
	if (!local->suspended)
		return 0;

#ifdef CONFIG_PM
	/* first set suspended false, then resuming */	
	atbm_printk_pm("%s:resume_timer_start\n",__func__);
	atomic_set(&local->resume_timer_start,1);
	atbm_mod_timer(&local->resume_timer, round_jiffies(jiffies + 2*HZ));
	local->suspended = false;
	mb();
	local->resuming = false;

	list_for_each_entry(sdata, &local->interfaces, list) {
		switch(sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			ieee80211_sta_restart(sdata);
			break;
#ifdef CONFIG_ATBM_SUPPORT_IBSS
		case NL80211_IFTYPE_ADHOC:
			ieee80211_ibss_restart(sdata);
			break;
#endif
#ifdef CONFIG_MAC80211_ATBM_MESH
		case NL80211_IFTYPE_MESH_POINT:
			ieee80211_mesh_restart(sdata);
#endif
			break;
		default:
			break;
		}
	}

	atbm_mod_timer(&local->sta_cleanup, jiffies + 1);
#ifdef CONFIG_MAC80211_ATBM_MESH
	mutex_lock(&local->sta_mtx);
	list_for_each_entry(sta, &local->sta_list, list)
		mesh_plink_restart(sta);
	mutex_unlock(&local->sta_mtx);
#endif
#else
	WARN_ON(1);
#endif
	return 0;
}
#endif
//EXPORT_SYMBOL_GPL(ieee80211_resume_disconnect);
#ifdef CONFIG_ATBM_SMPS
static int check_mgd_smps(struct ieee80211_if_managed *ifmgd,
			  enum ieee80211_smps_mode *smps_mode)
{
	if (ifmgd->associated) {
		*smps_mode = ifmgd->ap_smps;

		if (*smps_mode == IEEE80211_SMPS_AUTOMATIC) {
			if (ifmgd->powersave)
				*smps_mode = IEEE80211_SMPS_DYNAMIC;
			else
				*smps_mode = IEEE80211_SMPS_OFF;
		}

		return 1;
	}

	return 0;
}

/* must hold iflist_mtx */
void ieee80211_recalc_smps(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	enum ieee80211_smps_mode smps_mode = IEEE80211_SMPS_OFF;
	int count = 0;

	lockdep_assert_held(&local->iflist_mtx);

	/*
	 * This function could be improved to handle multiple
	 * interfaces better, but right now it makes any
	 * non-station interfaces force SM PS to be turned
	 * off. If there are multiple station interfaces it
	 * could also use the best possible mode, e.g. if
	 * one is in static and the other in dynamic then
	 * dynamic is ok.
	 */

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			goto set;

		count += check_mgd_smps(&sdata->u.mgd, &smps_mode);

		if (count > 1) {
			smps_mode = IEEE80211_SMPS_OFF;
			break;
		}
	}

	if (smps_mode == local->smps_mode)
		return;

 set:
	local->smps_mode = smps_mode;
	/* changed flag is auto-detected for this */
	ieee80211_hw_config(local, 0);
}
#endif
static bool atbm_ieee80211_id_in_list(const u8 *ids, int n_ids, u8 id)
{
	int i;

	for (i = 0; i < n_ids; i++)
		if (ids[i] == id)
			return true;
	return false;
}

/**
 * atbm_ieee80211_ie_split - split an IE buffer according to ordering
 *
 * @ies: the IE buffer
 * @ielen: the length of the IE buffer
 * @ids: an array with element IDs that are allowed before
 *	the split
 * @n_ids: the size of the element ID array
 * @offset: offset where to start splitting in the buffer
 *
 * This function splits an IE buffer by updating the @offset
 * variable to point to the location where the buffer should be
 * split.
 *
 * It assumes that the given IE buffer is well-formed, this
 * has to be guaranteed by the caller!
 *
 * It also assumes that the IEs in the buffer are ordered
 * correctly, if not the result of using this function will not
 * be ordered correctly either, i.e. it does no reordering.
 *
 * The function returns the offset where the next part of the
 * buffer starts, which may be @ielen if the entire (remainder)
 * of the buffer should be used.
 */
size_t atbm_ieee80211_ie_split(const u8 *ies, size_t ielen,
			  const u8 *ids, int n_ids, size_t offset)
{
	size_t pos = offset;

	while (pos < ielen && atbm_ieee80211_id_in_list(ids, n_ids, ies[pos]))
		pos += 2 + ies[pos + 1];

	return pos;
}

size_t ieee80211_ie_split_vendor(const u8 *ies, size_t ielen, size_t offset)
{
	size_t pos = offset;

	while (pos < ielen && ies[pos] != ATBM_WLAN_EID_VENDOR_SPECIFIC)
		pos += 2 + ies[pos + 1];

	return pos;
}


#if defined(CONFIG_MAC80211_ATBM_MESH) || defined(ATBM_SURPORT_TDLS)

int ieee80211_add_srates_ie(struct ieee80211_vif *vif, struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct ieee80211_supported_band *sband;
	int rate;
	u8 i, rates, *pos;

	sband = local->hw.wiphy->bands[chan_state->conf.channel->band];
	rates = sband->n_bitrates;
	if (rates > 8)
		rates = 8;

	if (atbm_skb_tailroom(skb) < rates + 2)
		return -ENOMEM;

	pos = atbm_skb_put(skb, rates + 2);
	*pos++ = ATBM_WLAN_EID_SUPP_RATES;
	*pos++ = rates;
	for (i = 0; i < rates; i++) {
		rate = sband->bitrates[i].bitrate;
		*pos++ = (u8) (rate / 5);
	}

	return 0;
}

int ieee80211_add_ext_srates_ie(struct ieee80211_vif *vif, struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct ieee80211_supported_band *sband;
	int rate;
	u8 i, exrates, *pos;

	sband = local->hw.wiphy->bands[chan_state->conf.channel->band];
	exrates = sband->n_bitrates;
	if (exrates > 8)
		exrates -= 8;
	else
		exrates = 0;

	if (atbm_skb_tailroom(skb) < exrates + 2)
		return -ENOMEM;

	if (exrates) {
		pos = atbm_skb_put(skb, exrates + 2);
		*pos++ = ATBM_WLAN_EID_EXT_SUPP_RATES;
		*pos++ = exrates;
		for (i = 8; i < sband->n_bitrates; i++) {
			rate = sband->bitrates[i].bitrate;
			*pos++ = (u8) (rate / 5);
		}
	}
	return 0;
}
#endif
struct cfg80211_bss *ieee80211_atbm_get_bss(struct wiphy *wiphy,
				      struct ieee80211_channel *channel,
				      const u8 *bssid,
				      const u8 *ssid, size_t ssid_len,
				      u16 capa_mask, u16 capa_val)
{
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	return  cfg80211_get_bss(wiphy, channel,
			bssid, ssid, ssid_len, capa_mask, capa_val);
	#else
	/*
	enum ieee80211_bss_type {
	IEEE80211_BSS_TYPE_ESS,
	IEEE80211_BSS_TYPE_PBSS,
	IEEE80211_BSS_TYPE_IBSS,
	IEEE80211_BSS_TYPE_MBSS,
	IEEE80211_BSS_TYPE_ANY
	};
	enum ieee80211_privacy {
	IEEE80211_PRIVACY_ON,
	IEEE80211_PRIVACY_OFF,
	IEEE80211_PRIVACY_ANY
	};
	*/
	enum ieee80211_bss_type bss_type = IEEE80211_BSS_TYPE_ANY;
	enum ieee80211_privacy  privacy = IEEE80211_PRIVACY_ANY;

	if(capa_mask&WLAN_CAPABILITY_ESS)
		bss_type = IEEE80211_BSS_TYPE_ESS;
	else if(capa_mask&WLAN_CAPABILITY_IBSS)
		bss_type = IEEE80211_BSS_TYPE_IBSS;
	else
		bss_type = IEEE80211_BSS_TYPE_ANY;

	if(capa_mask&WLAN_CAPABILITY_PRIVACY)
		privacy = IEEE80211_PRIVACY_ON;
	else
		privacy = IEEE80211_PRIVACY_ANY;

	return cfg80211_get_bss(wiphy,channel,bssid,ssid,ssid_len,bss_type,privacy);
	#endif
}

void ieee80211_atbm_put_bss(struct wiphy *wiphy, struct cfg80211_bss *pub)
{
#if LINUX_VERSION_IS_GEQ_OR_CPTCFG(3, 9, 0)
	cfg80211_put_bss(wiphy, pub);
#else
	BUG_ON(wiphy == NULL);
	cfg80211_put_bss(pub);
#endif
}
int ieee80211_atbm_ref_bss(struct wiphy *wiphy, struct cfg80211_bss *pub)
{
#if LINUX_VERSION_IS_GEQ_OR_CPTCFG(3, 9, 0)
	cfg80211_ref_bss(wiphy,pub);
	return 0;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
	cfg80211_ref_bss(pub);
	return 0;
#else
	/*
	*get bss again for ourself.
	*/
	const u8 *ssid = NULL;
	const u8 *bss_ssid = NULL;
	size_t ssid_len = 0;
	struct cfg80211_bss *bss;

	ssid = ieee80211_bss_get_ie(pub, ATBM_WLAN_EID_SSID);

	if(ssid){
		ssid_len = ssid[1];
		bss_ssid = ssid+2;
	}

	bss = ieee80211_atbm_get_bss(wiphy,pub->channel,pub->bssid,
								bss_ssid,ssid_len,pub->capability,
								pub->capability);

	return 	bss != pub;
#endif
}
int ieee80211_atbm_handle_bss(struct wiphy *wiphy, struct cfg80211_bss *pub)
{
	return ieee80211_atbm_ref_bss(wiphy,pub);
}

void ieee80211_atbm_release_bss(struct wiphy *wiphy, struct cfg80211_bss *pub)
{
	ieee80211_atbm_put_bss(wiphy,pub);
}

struct cfg80211_bss *__ieee80211_atbm_get_authen_bss(struct ieee80211_vif *vif,
					  struct ieee80211_channel *channel,
				      const u8 *bssid,
				      const u8 *ssid, size_t ssid_len)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct cfg80211_bss *bss = NULL;
	struct cfg80211_bss *authening_bss = NULL;
	
	rcu_read_lock();
	authening_bss = rcu_dereference(ifmgd->authen_bss);
	while(authening_bss != NULL){
		
		if(channel != authening_bss->channel){
			break;
		}
		
		if(bssid&&(atbm_compare_ether_addr(bssid, authening_bss->bssid)!=0)){
			break;
		}
		
		if(ssid&&ssid_len){
			const u8* bss_ssid;
			
			bss_ssid = ieee80211_bss_get_ie(authening_bss, ATBM_WLAN_EID_SSID);

			if((bss_ssid == NULL) || (bss_ssid[1] != ssid_len)){
				break;
			}

			if(memcmp(ssid,bss_ssid+2,ssid_len) != 0){
				break;
			}
		}
		if(ieee80211_atbm_handle_bss(local->hw.wiphy,authening_bss) == 0){
			bss = authening_bss;
		}else {			
			atbm_printk_err("%s:bss get err\n",__func__);
		}
		break;
	}
	rcu_read_unlock();

	return bss;
}

void __ieee80211_atbm_put_authen_bss(struct ieee80211_vif *vif,struct cfg80211_bss *bss)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	/*
	*ifmgd->authen_bss may have be released
	*/
	ieee80211_atbm_release_bss(local->hw.wiphy,bss);
}
struct cfg80211_bss *ieee80211_atbm_get_authen_bss(struct ieee80211_vif *vif,
					  struct ieee80211_channel *channel,
				      const u8 *bssid,
				      const u8 *ssid, size_t ssid_len){
	struct cfg80211_bss *bss = NULL;
	
	bss = __ieee80211_atbm_get_authen_bss(vif,channel,bssid,ssid,ssid_len);

	return bss;
}
void ieee80211_atbm_put_authen_bss(struct ieee80211_vif *vif,struct cfg80211_bss *bss)
{
	__ieee80211_atbm_put_authen_bss(vif,bss);
}
static char* ieee8211_find_name(struct ieee80211_hw *hw,const char *name)
{
	/*
	*phy0-name;
	*/
	struct ieee80211_local *local = hw_to_local(hw);
	int phy_namelen = strlen(wiphy_name(local->hw.wiphy))+1;
	int new_namelen = phy_namelen+strlen(name)+1;
	struct ieee80211_name_def *def = NULL;
	struct ieee80211_name_def *def_next = NULL;
	struct ieee80211_name_def *new_def = NULL;
	char *find_name = NULL;
	spin_lock_bh(&local->ieee80211_name_lock);
	
	
	if(new_namelen > 128){
		goto exit;
	}

	list_for_each_entry_safe(def,def_next,&local->ieee80211_name_list,list){

		if(def->s_name != name)
			continue;

		list_del(&def->list);

		new_def = atbm_krealloc(def,new_namelen+sizeof(struct ieee80211_name_def),GFP_ATOMIC);

		if(new_def == NULL){
			goto exit;
		}
		find_name = new_def->mem;
		new_def->d_name = find_name;
		new_def->s_name = name;
		new_def->name_size = new_namelen;

		memcpy(find_name,wiphy_name(local->hw.wiphy),strlen(wiphy_name(local->hw.wiphy)));
		find_name[strlen(wiphy_name(local->hw.wiphy))] = '-';
		memcpy(find_name+strlen(wiphy_name(local->hw.wiphy))+1,name,strlen(name)+1);
		break;
	}
	if(find_name){
		atbm_printk_debug("%s:find_name[%s]\n",__func__,find_name);
		list_add_tail(&new_def->list, &local->ieee80211_name_list);
	}
exit:
	spin_unlock_bh(&local->ieee80211_name_lock);
	return find_name;
}
char *ieee80211_alloc_name(struct ieee80211_hw *hw,const char *name)
{
	struct ieee80211_local *local = hw_to_local(hw);
	const char *phy_name = wiphy_name(local->hw.wiphy);
	int alloc_name_len = strlen(name)+strlen(phy_name)+2/*'-' + 0*/;
	char *alloc_name = NULL;
	struct ieee80211_name_def *def;

	alloc_name = ieee8211_find_name(hw,name);

	if(alloc_name)
		return alloc_name;
	
	spin_lock_bh(&local->ieee80211_name_lock);
	/*
	*local->ieee80211_name_len + alloc_name_len may be is too long
	*/
	if(alloc_name_len < 0)
		goto exit;
	
	if(alloc_name_len >= 128){
		atbm_printk_err("%s:name is too long\n",__func__);
		goto exit;
	}
	
	def = atbm_kzalloc(alloc_name_len + sizeof(struct ieee80211_name_def),GFP_ATOMIC);

	if(def == NULL){
		atbm_printk_err("%s def is null\n",__func__);
		goto exit;
	}
	
	alloc_name = def->mem;
	def->s_name = name;
	def->d_name = alloc_name;
	def->name_size = alloc_name_len;
	/*
	*phy_name-name
	*/
	memcpy(alloc_name,phy_name,strlen(phy_name));
	alloc_name[strlen(phy_name)] = '-';
	memcpy(alloc_name+strlen(phy_name)+1,name,strlen(name)+1);
	atbm_printk_debug("[%s],alloc_name_len[%d]\n",alloc_name,alloc_name_len);
	list_add_tail(&def->list, &local->ieee80211_name_list);
exit:
	spin_unlock_bh(&local->ieee80211_name_lock);
	return alloc_name;
}
/**
 * _ieee80211_is_robust_mgmt_frame - check if frame is a robust management frame
 * @hdr: the frame (buffer must include at least the first octet of payload)
 */
static inline bool _atbm_ieee80211_is_robust_mgmt_frame(struct ieee80211_hdr *hdr)
{
	if (ieee80211_is_disassoc(hdr->frame_control) ||
	    ieee80211_is_deauth(hdr->frame_control))
		return true;

	if (ieee80211_is_action(hdr->frame_control)) {
		u8 category;

		/*
		 * Action frames, excluding Public Action frames, are Robust
		 * Management Frames. However, if we are looking at a Protected
		 * frame, skip the check since the data may be encrypted and
		 * the frame has already been found to be a Robust Management
		 * Frame (by the other end).
		 */
		if (ieee80211_has_protected(hdr->frame_control))
			return true;
		category = *(((u8 *) hdr) + 24) & 0x7f;

		return category != ATBM_WLAN_CATEGORY_PUBLIC &&
			category != ATBM_WLAN_CATEGORY_HT &&
			category != ATBM_WLAN_CATEGORY_WNM_UNPROTECTED &&
			category != ATBM_WLAN_CATEGORY_SELF_PROTECTED &&
			category != ATBM_WLAN_CATEGORY_UNPROT_DMG &&
			category != ATBM_WLAN_CATEGORY_VHT &&
			category != ATBM_WLAN_CATEGORY_VENDOR_SPECIFIC &&
			category != ATBM_WLAN_CATEGORY_S1G;
	}

	return false;
}

/**
 * ieee80211_is_robust_mgmt_frame - check if skb contains a robust mgmt frame
 * @skb: the skb containing the frame, length will be checked
 */
bool atbm_ieee80211_is_robust_mgmt_frame(struct sk_buff *skb)
{
	if (skb->len < ATBM_IEEE80211_MIN_ACTION_SIZE)
		return false;
	return _atbm_ieee80211_is_robust_mgmt_frame((void *)skb->data);
}
#ifndef CONFIG_IEEE80211_SPECIAL_FILTER
struct sk_buff *ieee80211_special_queue_package(struct ieee80211_vif *vif,struct sk_buff *skb)
{
	return skb;
}
#endif

struct ieee80211_vif *ieee80211_get_monitor_vif(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	return local->monitor_sdata ? &local->monitor_sdata->vif : NULL;
}
bool atbm_iee80211_bss_is_nontrans(struct cfg80211_bss *cbss)
{
#ifndef CONFIG_CFG880211_SUPPORT_HE
	struct ieee80211_bss *bss = (void *)cbss->priv;

	return (bss && bss->nontransmit);
#else
	return cbss->transmitted_bss ? true:false;
#endif
}
/* WSM events */
/* Error */
#define WSM_EVENT_ERROR			(0)

/* BSS lost */
#define WSM_EVENT_BSS_LOST		(1)

/* BSS regained */
#define WSM_EVENT_BSS_REGAINED		(2)

/* Radar detected */
#define WSM_EVENT_RADAR_DETECTED	(3)

/* RCPI or RSSI threshold triggered */
#define WSM_EVENT_RCPI_RSSI		(4)

/* BT inactive */
#define WSM_EVENT_BT_INACTIVE		(5)

/* BT active */
#define WSM_EVENT_BT_ACTIVE		(6)

#define WSM_EVENT_PS_MODE_ERROR         (7)

#define WSM_EVENT_INACTIVITY		(9)

#define WSM_EVENT_TWT_STATE			(10)

#ifdef CONFIG_ATBM_SUPPORT_CSA

#define WSM_EVENT_CSA_DONE			(11)

#endif
static void  ieee80211_event_sta_inactivity(struct ieee80211_sub_if_data *sdata,u32 aid_mask)
{
	u8 aid = 0;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	
	if(sdata->vif.type != NL80211_IFTYPE_AP){
		return;
	}
	for(aid = 1;aid_mask != 0;aid++){
		if((aid_mask & BIT(aid)) == 0){
			continue;
		}

		mutex_lock(&local->sta_mtx);
		list_for_each_entry(sta, &local->sta_list, list) {
			if(sta->uploaded == false)
				continue;
			if(sta->sdata != sdata)
				continue;
			if(sta->sta.aid != aid)
				continue;
			atbm_printk_always("sta[%pM] inactivity\n",sta->sta.addr);
			WARN_ON(ieee80211_rx_sta_cook_deauthen(sta)==false);
			WARN_ON(ieee80211_tx_sta_deauthen(sta)==false);
			break;
		}

		mutex_unlock(&local->sta_mtx);

		aid_mask &= ~BIT(aid);
	}
	
}
static enum work_done_result ieee80211_event_work_done(struct ieee80211_work *wk,
				      struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = wk->sdata;
	switch(wk->event.id){
	case WSM_EVENT_ERROR:
		break;
	case WSM_EVENT_BSS_LOST:
		__ieee80211_connection_loss(sdata,NULL);
		break;
	case WSM_EVENT_BSS_REGAINED:
		break;
	case WSM_EVENT_RADAR_DETECTED:
		break;
	case WSM_EVENT_RCPI_RSSI:
		break;
	case WSM_EVENT_BT_INACTIVE:
		break;
	case WSM_EVENT_BT_ACTIVE:
		break;
	case WSM_EVENT_PS_MODE_ERROR:
		sdata->u.mgd.powersave_enable = false;
		mutex_lock(&sdata->local->iflist_mtx);
		ieee80211_recalc_ps(sdata->local, -1);
		mutex_unlock(&sdata->local->iflist_mtx);
		break;
	case WSM_EVENT_TWT_STATE:
		ieee80211_twt_sta_suspend_state_work(sdata,wk->event.data);
		break;
	case WSM_EVENT_INACTIVITY:
		ieee80211_event_sta_inactivity(sdata,wk->event.data);
		break;
#ifdef CONFIG_ATBM_SUPPORT_CSA
	case WSM_EVENT_CSA_DONE:
		ieee80211_event_csa_done(sdata,wk->event.data);
		break;
#endif
	}
	return WORK_DONE_DESTROY;
}
void ieee80211_event_work(struct ieee80211_vif *vif,u32 id,u32 data)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_work *wk;

	wk = atbm_kzalloc(sizeof(struct ieee80211_work), GFP_ATOMIC);

	if(wk == NULL){
		return;
	}

	wk->sdata = sdata;
	wk->type  = IEEE80211_WORK_EVENT;
	wk->event.id = id;
	wk->event.data = data;
	wk->done  = ieee80211_event_work_done;

	ieee80211_add_work(wk);
	
}
u8 *atbm_iee80211_bss_transmit_bssid(struct cfg80211_bss *cbss)
{
#ifndef CONFIG_CFG880211_SUPPORT_HE
	struct ieee80211_bss *bss = (void *)cbss->priv;

	return bss->transbssid;
#else
	return cbss->transmitted_bss->bssid;
#endif
}

u8 atbm_iee80211_max_bssid_indicator(struct cfg80211_bss *cbss)
{
#ifndef CONFIG_CFG880211_SUPPORT_HE
	struct ieee80211_bss *bss = (void *)cbss->priv;

	return bss->max_bssid_indicator;
#else
	return cbss->max_bssid_indicator;
#endif
}

u8 atbm_iee80211_bssid_index(struct cfg80211_bss *cbss)
{
#ifndef CONFIG_CFG880211_SUPPORT_HE
	struct ieee80211_bss *bss = (void *)cbss->priv;

	return bss->bssid_index;
#else
	return cbss->bssid_index;
#endif
}
bool atbm_iee80211_he_used(struct cfg80211_bss *cbss)
{
	struct ieee80211_bss *bss = (void *)cbss->priv;

	return bss->he_used;
}
static int ieee80211_use_mfp(struct ieee80211_hdr* hdr, struct sta_info* sta)
{
	if (!ieee80211_is_mgmt(hdr->frame_control))
		return 0;

	if (sta == NULL || !test_sta_flag(sta, WLAN_STA_MFP))
		return 0;
	if (!_atbm_ieee80211_is_robust_mgmt_frame(hdr))
		return 0;

	return 1;
}

struct ieee80211_tx_encap{
	struct ieee80211_sub_if_data* sdata;
	struct sta_info* sta;
	struct sk_buff* skb;
	u8* xmit_buff;
	int hdrlen;
	int skip_len;
	int remain_len;
#ifdef CONFIG_ATBM_SUPPORT_TSO
	__wsum csum;
	int payload_len;
#endif
};

static int ieee80211_build_encrype(struct ieee80211_tx_encap *encap)
{
	struct ieee80211_key* key = NULL;
	struct ieee80211_key* lookup_key = NULL;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(encap->skb);
	struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)encap->xmit_buff;
	/*
	*find key
	*/
	if (unlikely(info->flags & IEEE80211_TX_INTFL_DONT_ENCRYPT))
		lookup_key = NULL;
	else if (encap->sta && (key = rcu_dereference(encap->sta->ptk)))
		lookup_key = key;
	else if (ieee80211_is_mgmt(hdr->frame_control) &&
		is_multicast_ether_addr(hdr->addr1) &&
		_atbm_ieee80211_is_robust_mgmt_frame(hdr) &&
		(key = rcu_dereference(encap->sdata->default_mgmt_key)))
		lookup_key = key;
	else if (is_multicast_ether_addr(hdr->addr1) &&
		(key = rcu_dereference(encap->sdata->default_multicast_key)))
		lookup_key = key;
	else if (!is_multicast_ether_addr(hdr->addr1) &&
		(key = rcu_dereference(encap->sdata->default_unicast_key)))
		lookup_key = key;
	else
		lookup_key = NULL;

	if (lookup_key) {

		switch (lookup_key->conf.cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			if (ieee80211_is_auth(hdr->frame_control))
				break;				
			atbm_printk_debug("WEP[%d][%d]\n", lookup_key->conf.iv_len, lookup_key->conf.icv_len);
			atbm_fallthrough;
		case WLAN_CIPHER_SUITE_TKIP:
			if (!ieee80211_is_data_present(hdr->frame_control)) {
				lookup_key = NULL;
			}
			else {
				hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
				atbm_printk_debug("TKIP[%d][%d]\n", lookup_key->conf.iv_len, lookup_key->conf.icv_len);
			}
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			if (!ieee80211_is_data_present(hdr->frame_control) &&
				!ieee80211_use_mfp(hdr, encap->sta)) {
				lookup_key = NULL;
			}
			else {
				atbm_printk_debug("CCMP[%d][%d]\n", lookup_key->conf.iv_len, lookup_key->conf.icv_len);
				hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
			}
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:			
			atbm_printk_debug("CMAC[%d][%d]\n", lookup_key->conf.iv_len, lookup_key->conf.icv_len);
			if (!ieee80211_is_mgmt(hdr->frame_control)) {
				lookup_key = NULL;
			}
			else {
			}
			break;
		default:
			atbm_printk_err("cipher type err\n");
			goto fail;
		}
	}

	if (lookup_key) {
		info->control.hw_key = &lookup_key->conf;
		encap->hdrlen     += lookup_key->conf.iv_len;
		encap->remain_len += lookup_key->conf.icv_len;
	}else{
		info->control.hw_key = NULL;
	}
	
	return 0;
fail:
	return -1;
}
static void ieee80211_build_80211_payload_align(struct ieee80211_tx_encap *encap)
{
	u8 s_align = (unsigned long)(encap->skb->data + encap->skip_len) & 3;
	u8 d_align = (unsigned long)(encap->xmit_buff + encap->hdrlen) & 3;
	//struct ieee80211_tx_info* info = IEEE80211_SKB_CB(encap->skb);
	/*
	*1bytes not support;
	*/
	if((s_align & 1) || (d_align & 1)){
		return;
	}

	/*
	*s align is same with the d alogn
	*/

	if(s_align == d_align){
		return;
	}
#ifndef CONFIG_ENABLE_HIF_DUMMY
#define CONFIG_ENABLE_HIF_DUMMY 0
#endif
#if CONFIG_ENABLE_HIF_DUMMY
	info->flags |= IEEE80211_TX_2BYTTES_OFF;
	encap->hdrlen += 2;
	atbm_printk_debug("2bytes off\n");
#endif //CONFIG_ENABLE_HIF_DUMMY
}
#ifdef CONFIG_ATBM_SUPPORT_TSO
static int ieee80211_build_80211_payload_tcp(struct ieee80211_tx_encap *encap)
{
	unsigned int len;
	struct sk_buff* skb = encap->skb;
	unsigned int mss = skb_shinfo(skb)->gso_size;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(encap->skb);
	struct ieee80211_tso_info *tso = &info->control.tso;
	unsigned int offset = encap->skip_len + tso->offset;

	encap->csum = 0;

	if(skb->len < offset){
		goto err;
	}

	len = skb->len - offset;
	
	if (len > mss){
		len = mss;
		info->requeue =  1;
	}else {
		info->requeue =  0;
	}
	encap->payload_len = len;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0))
	encap->csum = skb_copy_and_csum_bits(skb,offset,encap->xmit_buff + encap->hdrlen,len,0);
#else
	encap->csum = skb_copy_and_csum_bits(skb,offset,encap->xmit_buff + encap->hdrlen,len);
#endif
	return 0;
err:
	return  -1;
}
static int ieee80211_build_80211_payload_tso(struct ieee80211_tx_encap *encap)
{
	struct sk_buff* skb = encap->skb;
	u8* xmit_buff = encap->xmit_buff;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(encap->skb);
	struct ieee80211_tso_info *tso = &info->control.tso;
	struct atbm_tcphdr *th;
	u8 *nheader;
	size_t noffset;
	/*
	*copy 2bytes protocol
	*/
	noffset = skb_network_header(skb) - (skb->data + encap->skip_len);
	memcpy(xmit_buff +  encap->hdrlen,skb->data + encap->skip_len,noffset);
	encap->hdrlen += noffset;
	encap->skip_len += noffset;
//	if(encap->hdrlen % 4){
//		atbm_printk_err("offset(%d)\n",encap->hdrlen % 4);
//	}
	/*copy ip hdr*/
	nheader = xmit_buff + encap->hdrlen;
	memcpy(xmit_buff + encap->hdrlen,skb_network_header(skb),tso->network_len);
	encap->hdrlen += tso->network_len;
	encap->skip_len += tso->network_len;
	/*copy tcp hdr*/
	th = (struct atbm_tcphdr *)(xmit_buff + encap->hdrlen);
	memcpy(xmit_buff + encap->hdrlen,skb_transport_header(skb),tso->transport_len);
	encap->hdrlen += tso->transport_len;
	encap->skip_len += tso->transport_len;

	/*IF hw support tcp/udp check sum,remove the checksum code*/
	
	if(ieee80211_build_80211_payload_tcp(encap)){
		goto err;
	}

	/*
	*init tcp hdr
	*/
	do {
		unsigned int oldlen;
		unsigned int seq = ntohl(th->seq);
		__be32 delta;
		
		oldlen = (u16)~(skb->len - (unsigned int)((skb_transport_header(skb) - skb->data)) - tso->transport_len);
		delta  = htonl(oldlen + encap->payload_len);
		
		if(tso->n_mss > 0){
		    th->seq = htonl(seq + tso->offset);
		    th->cwr = 0;
		}
		if(info->requeue){
			th->fin = 0;
			th->psh = 0;
		}
		th->check = ~csum_fold((__force __wsum)((__force u32)th->check + (__force u32)delta));
		th->check = csum_fold(csum_partial((u8*)th,tso->transport_len, encap->csum));
	} while (0);

	/*
	*init ip header
	*/
	do {
		struct atbm_iphdr *iph = (struct atbm_iphdr *)nheader;
		int id = ntohs(iph->id);
		
		iph->id = htons(id + tso->n_mss);
		iph->tot_len = htons(tso->network_len + tso->transport_len + encap->payload_len);
		iph->check = 0;
		iph->check = ip_fast_csum(iph, iph->ihl);
	} while (0);	
	/*
	*update params for next package processing
	*/
	tso->offset += encap->payload_len;
	encap->hdrlen   += encap->payload_len;
	encap->skip_len += tso->offset;
	tso->n_mss 	++;
	//atbm_printk_err("tso:offset(%d),n_mss(%d),hdrlen(%d),skip_len(%d),len(%d)(%d)\n",tso->offset,
	//tso->n_mss,encap->hdrlen,encap->skip_len,skb->len,info->requeue);
	return 0;
err:
	atbm_printk_err("build tso err\n");
	return -1;
}
#endif
static int ieee80211_build_80211_payload(struct ieee80211_tx_encap *encap)
{	
	u8* xmit_buff = encap->xmit_buff + encap->hdrlen;
	struct sk_buff* skb = encap->skb;
	
#ifdef CONFIG_ATBM_SUPPORT_TSO
	if (IEEE80211_SKB_CB(skb)->gso) {
		atbm_printk_once("tso xmit(%d)\n",skb->len);
		return  ieee80211_build_80211_payload_tso(encap);
	}
#endif
	if(!skb_copy_bits(skb,encap->skip_len,xmit_buff,skb->len - encap->skip_len)){
		encap->hdrlen += skb->len - encap->skip_len;
		return 0;
	}
	atbm_printk_err("80211 payload copy err\n");
	return -1;
}
static int ieee80211_build_up_80211_frame_fast(struct ieee80211_tx_encap *encap)
{
	struct ieee80211_sta_hdr *sta_hdr;
	u16 ethertype = (encap->skb->data[12] << 8) | encap->skb->data[13];
	struct ethhdr *eth = (struct ethhdr *)encap->skb->data;
	struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)encap->xmit_buff;
	u8 tid = 16;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(encap->skb);
	
	if (cpu_to_be16(ethertype) == encap->sdata->control_port_protocol)
		goto  fail;
	
	if(unlikely(encap->sta == NULL)){
		goto fail;
	}

	sta_hdr = rcu_dereference(encap->sta->sta_hdr);

	if(unlikely(sta_hdr == NULL)){
		goto fail;
	}

	memcpy(encap->xmit_buff,  sta_hdr->hdr, sta_hdr->hdr_len);
	memcpy(encap->xmit_buff + sta_hdr->da_offs, eth->h_dest, ETH_ALEN);
	memcpy(encap->xmit_buff + sta_hdr->sa_offs, eth->h_source, ETH_ALEN);

	if (hdr->frame_control & cpu_to_le16(IEEE80211_STYPE_QOS_DATA)) {
		tid = encap->skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
		*ieee80211_get_qos_ctl(hdr) = tid;
		info->flags |= IEEE80211_TX_CTL_QOS;
	}else {
		info->flags &= ~IEEE80211_TX_CTL_QOS;
	}
	if(sta_hdr->key)
		info->control.hw_key = &sta_hdr->key->conf;
	
	encap->hdrlen = sta_hdr->hdr_len;
	encap->remain_len = sta_hdr->icv_len;
	
	atbm_printk_once("sta_hdr\n");
	return 0;
fail:
	return -1;
}
static int ieee80211_build_up_to_80211_frame_slow(struct ieee80211_tx_encap *encap)
{
	//const u8* encaps_data;
	//int encaps_len;
	//u16 ethertype = (encap->skb->data[12] << 8) | encap->skb->data[13];
	struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)encap->xmit_buff;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(encap->skb);
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_DATA);

	
	switch (encap->sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		if (encap->sta) {
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS);
			/* RA TA DA SA */
			memcpy(hdr->addr1, encap->sta->sta.addr, ETH_ALEN);
			memcpy(hdr->addr2, encap->sdata->vif.addr, ETH_ALEN);
			memcpy(hdr->addr3, encap->skb->data, ETH_ALEN);
			memcpy(hdr->addr4, encap->skb->data + ETH_ALEN, ETH_ALEN);
			encap->hdrlen = 30;
			break;
		}
		/* fall through */
	case NL80211_IFTYPE_AP:
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_FROMDS);
		/* DA BSSID SA */
		memcpy(hdr->addr1, encap->skb->data, ETH_ALEN);
		memcpy(hdr->addr2, encap->sdata->vif.addr, ETH_ALEN);
		memcpy(hdr->addr3, encap->skb->data + ETH_ALEN, ETH_ALEN);
		encap->hdrlen = 24;
		break;
#ifdef CONFIG_ATBM_SUPPORT_WDS
	case NL80211_IFTYPE_WDS:
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS);
		/* RA TA DA SA */
		memcpy(hdr->addr1, encap->sdata->u.wds.remote_addr, ETH_ALEN);
		memcpy(hdr->addr2, encap->sdata->vif.addr, ETH_ALEN);
		memcpy(hdr->addr3, encap->skb->data, ETH_ALEN);
		memcpy(hdr->addr4, encap->skb->data + ETH_ALEN, ETH_ALEN);
		encap->hdrlen = 30;
		break;
#endif
	case NL80211_IFTYPE_STATION:
#ifdef CONFIG_ATBM_4ADDR
		if (encap->sdata->u.mgd.use_4addr &&
			cpu_to_be16(ethertype) != encap->sdata->control_port_protocol) {
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_FROMDS |
				IEEE80211_FCTL_TODS);
			/* RA TA DA SA */
			memcpy(hdr->addr1, encap->sdata->u.mgd.bssid, ETH_ALEN);
			memcpy(hdr->addr2, encap->sdata->vif.addr, ETH_ALEN);
			memcpy(hdr->addr3, encap->skb->data, ETH_ALEN);
			memcpy(hdr->addr4, encap->skb->data + ETH_ALEN, ETH_ALEN);
			encap->hdrlen = 30;
		}
		else
#endif
		{
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_TODS);
			/* BSSID SA DA */
			memcpy(hdr->addr1, encap->sdata->u.mgd.bssid, ETH_ALEN);
			memcpy(hdr->addr2, encap->skb->data + ETH_ALEN, ETH_ALEN);
			memcpy(hdr->addr3, encap->skb->data, ETH_ALEN);
			encap->hdrlen = 24;
		}
		break;
#ifdef CONFIG_ATBM_SUPPORT_IBSS
	case NL80211_IFTYPE_ADHOC:
		/* DA SA BSSID */
		memcpy(hdr->addr1, encap->skb->data, ETH_ALEN);
		memcpy(hdr->addr2, encap->skb->data + ETH_ALEN, ETH_ALEN);
		memcpy(hdr->addr3, encap->sdata->u.ibss.bssid, ETH_ALEN);
		encap->hdrlen = 24;
		break;
#endif
	default:
		goto fail;
	}
	/*
	*build qos
	*/
	if (info->flags & IEEE80211_TX_CTL_QOS) {
		u8* p = encap->xmit_buff + encap->hdrlen;
		u8 ack_policy, tid;

		hdr->frame_control |= cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
		tid = encap->skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;

		*p = 0;
		/* preserve EOSP bit */
		ack_policy = *p & IEEE80211_QOS_CTL_EOSP;
		/* qos header is 2 bytes */
		*p++ = ack_policy | tid;
		*p = ieee80211_vif_is_mesh(&encap->sdata->vif) ?
			(IEEE80211_QOS_CTL_MESH_CONTROL_PRESENT >> 8) : 0;

		encap->hdrlen += 2;
	}
	hdr->duration_id = 0;
	hdr->seq_ctrl = 0;
	
	if (ieee80211_build_encrype(encap)) {
		atbm_printk_err("%s:find key err\n", __func__);
		goto fail;
	}

	return 0;
fail:
	return  -1;
}
static int ieee80211_build_up_to_80211_frame(struct ieee80211_tx_encap *encap)
{
	const u8* encaps_data;
	int encaps_len;
	u16 ethertype = (encap->skb->data[12] << 8) | encap->skb->data[13];
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(encap->skb);

	if(unlikely((encap->sta == NULL) && (info->flags & IEEE80211_TX_UCAST))){
		goto fail;
	}
	
	if(ieee80211_build_up_80211_frame_fast(encap) == 0){
		/*build fast sucess*/
	}else if(ieee80211_build_up_to_80211_frame_slow(encap)  == 0){
		/*build slow sucess*/
	}else {
		/*build frame fail*/
		goto fail;
	}
	
	encap->skip_len = ETH_HLEN;
	
	if (ethertype == ETH_P_AARP || ethertype == ETH_P_IPX) {
		encaps_data = bridge_tunnel_header;
		encaps_len = sizeof(bridge_tunnel_header);
		encap->skip_len -= 2;
	}
	else if (ethertype >= 0x600) {
		encaps_data = rfc1042_header;
		encaps_len = sizeof(rfc1042_header);
		encap->skip_len -= 2;
	}
	else {
		encaps_data = NULL;
		encaps_len = 0;
	}
	encap->hdrlen += encaps_len;
	/*
	*make sure soure and  skb->data is at same align;
	*/
	ieee80211_build_80211_payload_align(encap);
	
	if (encaps_data) {
		memcpy(encap->xmit_buff + encap->hdrlen - encaps_len/**/, encaps_data, encaps_len);
	}

	if (ieee80211_build_80211_payload(encap)) {
		atbm_printk_err("%s:copy payload err\n", __func__);
		goto fail;
	}
	
	return 0;
fail:
	encap->hdrlen = 0;
	return -1;
}
static int ieee80211_build_raw_frame(struct ieee80211_tx_encap *encap)
{
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(encap->skb);

	encap->hdrlen = ieee80211_hdrlen(((struct ieee80211_hdr*)encap->skb->data)->frame_control);
	memcpy(encap->xmit_buff, encap->skb->data, encap->hdrlen);
	encap->skip_len = encap->hdrlen;

	if (ieee80211_build_encrype(encap)) {
		atbm_printk_err("%s:find key err\n", __func__);
		goto fail;
	}	
	if (ieee80211_build_80211_payload(encap)) {
		atbm_printk_err("%s:copy payload err\n", __func__);
		goto fail;
	}

	if (info->control.hw_key && (info->control.hw_key->cipher == WLAN_CIPHER_SUITE_AES_CMAC)) {
		struct ieee80211_mmie* mmie = (struct ieee80211_mmie*)(encap->xmit_buff + encap->hdrlen);
		memset(mmie,0,sizeof(struct ieee80211_mmie));
		mmie->element_id = WLAN_EID_MMIE;
		mmie->length = sizeof(*mmie) - 2;
		mmie->key_id = cpu_to_le16(info->control.hw_key->keyidx);
		
	}

	return 0;
fail:
	encap->hdrlen = 0;
	return -1;
}
static void ieee80211_build_rate_policy(struct ieee80211_tx_encap *encap)
{
	struct ieee80211_tx_rate_control txrc;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(encap->skb);
	txrc.sband = encap->sdata->local->hw.wiphy->bands[info->band];
	txrc.info = info;
	txrc.lower = (info->flags&(IEEE80211_TX_CTL_USE_MINRATE | IEEE80211_TX_CTL_NO_ACK)) || (encap->sta == NULL);
	if((tx_rate != 0xFF) && (txrc.lower == 0)){
		txrc.manual_rate  = tx_rate;
        txrc.manual = TX_RATE_FIXED;
        if(tx_rate_down)
        {
            txrc.manual |= TX_RATE_FIXED_AUTO_DOWN;  //manual have two options, 1, aways send on fixed rate. 2, down rate from set rate. 
        }
        if(tx_rate_static)
        {
            txrc.manual |= TX_RATE_FIXED_STATIC;  
        }
       }else {
		txrc.manual = 0;
	}
	if((info->gso && info->sample_pkt_flag) || (info->gso == 0) || (info->control.tso.n_mss == 1)){
		rate_control_get_rate(encap->sdata,encap->sta,&txrc);
		
	}
	info->rate_poll |= info->tx_update_rate;
	info->tx_update_rate = info->rate_poll && (info->requeue == 0);
#if 0
#ifdef CONFIG_ATBM_SUPPORT_TSO	
	if(info->gso == 0 || info->control.tso.n_mss == 1){
		rate_control_get_rate(encap->sdata,encap->sta,&txrc);
	}

	info->rate_poll |= info->tx_update_rate;
	info->tx_update_rate = info->rate_poll && (info->requeue == 0);
#else
	rate_control_get_rate(encap->sdata,encap->sta,&txrc);
#endif
#endif
}
int ieee80211_build_80211_frame(struct ieee80211_vif* vif, struct sk_buff* skb, u8* xmit_buff)
{
	struct ieee80211_sub_if_data* sdata = vif_to_sdata(vif);
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);
	struct sta_info* sta = NULL;
	struct ieee80211_tx_encap encap;
	/*
	*ap vlan change the sdata
	*/
	if(info->control.sta){
		sta = container_of(info->control.sta, struct sta_info, sta);
		sdata = sta->sdata;
		BUG_ON(sdata == NULL);
	}
	
	encap.sdata = sdata;
	encap.sta   = sta;
	encap.skb   = skb;
	encap.xmit_buff = xmit_buff;
	encap.hdrlen = 0;
	encap.remain_len = 0;
	
	if (info->flags & IEEE80211_TX_CTL_8023) {
		if (ieee80211_build_up_to_80211_frame(&encap)) {
			atbm_printk_err("build 80211 hdr errss\n");
			goto fail;
		}
	} else if(ieee80211_build_raw_frame(&encap)){
		atbm_printk_err("build raw frame err\n");
		goto fail;
	}
	/*
	*assign seq
	*/
	if (sta && (info->flags & IEEE80211_TX_CTL_QOS)) {
		u16* seq;
		struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)xmit_buff;
		seq = &sta->tid_seq[skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK];

		hdr->seq_ctrl = cpu_to_le16(*seq);

		/* Increase the sequence number. */
		*seq = (*seq + 0x10) & IEEE80211_SCTL_SEQ;

		if((info->flags & IEEE80211_TX_CTL_EOSP) && (info->requeue == 0)){
			*ieee80211_get_qos_ctl(hdr) |= IEEE80211_QOS_CTL_EOSP;
		}
	}else {
		info->flags |= IEEE80211_TX_CTL_ASSIGN_SEQ;
	}

	if((info->flags & IEEE80211_TX_CTL_MORE) && (info->requeue == 0)){
		struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)xmit_buff;
		hdr->frame_control |= IEEE80211_FCTL_MOREDATA;
	}
	/*
	*remain icv space
	*/
	encap.hdrlen += encap.remain_len;
	
	ieee80211_build_rate_policy(&encap);
	atbm_printk_debug("[Build80211][%d->%d][%x]\n", skb->len, encap.hdrlen, *((u16*)xmit_buff));
	return encap.hdrlen;
fail:
	return 0;
}
void ieee80211_atbm_amsdu_to_8023s(struct sk_buff *skb, struct sk_buff_head *list,
			      const u8 *addr, enum nl80211_iftype iftype,
			      const unsigned int extra_headroom,
			      bool has_80211_header)
{
	struct sk_buff *frame = NULL;
	u16 ethertype;
	u8 *payload;
	const struct ethhdr *eth;
	int remaining, err;
	u8 dst[ETH_ALEN], src[ETH_ALEN];

	if (has_80211_header) {
		err = ieee80211_data_to_8023(skb, addr, iftype);
		if (err)
			goto out;

		/* skip the wrapping header */
		eth = (struct ethhdr *) atbm_skb_pull(skb, sizeof(struct ethhdr));
		if (!eth)
			goto out;
	} else {
		eth = (struct ethhdr *) skb->data;
	}

	while (skb != frame) {
		u8 padding;
		__be16 len = eth->h_proto;
		unsigned int subframe_len = sizeof(struct ethhdr) + ntohs(len);

		remaining = skb->len;
		memcpy(dst, eth->h_dest, ETH_ALEN);
		memcpy(src, eth->h_source, ETH_ALEN);

		padding = (4 - subframe_len) & 0x3;
		/* the last MSDU has no padding */
		if (subframe_len > remaining)
			goto purge;

		atbm_skb_pull(skb, sizeof(struct ethhdr));
		/* reuse skb for the last subframe */
		if (remaining <= subframe_len + padding)
			frame = skb;
		else {
			unsigned int hlen = ALIGN(extra_headroom, 4);
			/*
			 * Allocate and reserve two bytes more for payload
			 * alignment since sizeof(struct ethhdr) is 14.
			 */
			frame = atbm_dev_alloc_skb(hlen + subframe_len + 2);
			if (!frame)
				goto purge;

			atbm_skb_reserve(frame, hlen + sizeof(struct ethhdr) + 2);
			memcpy(atbm_skb_put(frame, ntohs(len)), skb->data,
				ntohs(len));

			eth = (struct ethhdr *)atbm_skb_pull(skb, ntohs(len) +
							padding);
			if (!eth) {
				atbm_dev_kfree_skb(frame);
				goto purge;
			}
		}

		skb_reset_network_header(frame);
		frame->dev = skb->dev;
		frame->priority = skb->priority;

		payload = frame->data;
		ethertype = (payload[6] << 8) | payload[7];

		if (likely((ether_addr_equal(payload, rfc1042_header) &&
			    ethertype != ETH_P_AARP && ethertype != ETH_P_IPX) ||
			   ether_addr_equal(payload, bridge_tunnel_header))) {
			/* remove RFC1042 or Bridge-Tunnel
			 * encapsulation and replace EtherType */
			atbm_skb_pull(frame, 6);
			memcpy(atbm_skb_push(frame, ETH_ALEN), src, ETH_ALEN);
			memcpy(atbm_skb_push(frame, ETH_ALEN), dst, ETH_ALEN);
		} else {
			memcpy(atbm_skb_push(frame, sizeof(__be16)), &len,
				sizeof(__be16));
			memcpy(atbm_skb_push(frame, ETH_ALEN), src, ETH_ALEN);
			memcpy(atbm_skb_push(frame, ETH_ALEN), dst, ETH_ALEN);
		}
		__atbm_skb_queue_tail(list, frame);
	}

	return;

 purge:
	__atbm_skb_queue_purge(list);
 out:
	atbm_dev_kfree_skb(skb);
}

