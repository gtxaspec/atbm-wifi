/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2008-2010	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/export.h>
#include <net/atbm_mac80211.h>
#include "ieee80211_i.h"
#include "rate.h"
#include "mesh.h"
#include "wme.h"

#if 0
void ieee80211_tx_status_irqsafe(struct ieee80211_hw *hw,
				 struct sk_buff *skb)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int tmp;

	skb->pkt_type = IEEE80211_TX_STATUS_MSG;
	atbm_skb_queue_tail(info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS ?
		       &local->skb_queue : &local->skb_queue_unreliable, skb);
	tmp = atbm_skb_queue_len(&local->skb_queue) +
		atbm_skb_queue_len(&local->skb_queue_unreliable);
	while (tmp > IEEE80211_IRQSAFE_QUEUE_LIMIT &&
	       (skb = atbm_skb_dequeue(&local->skb_queue_unreliable))) {
		atbm_dev_kfree_skb_irq(skb);
		tmp--;
		I802_DEBUG_INC(local->tx_status_drop);
	}
	tasklet_schedule(&local->tasklet);
}
#endif
//EXPORT_SYMBOL(ieee80211_tx_status_irqsafe);
static void ieee80211_frame_acked(struct sta_info *sta, struct sk_buff *skb)
{
#if defined (CONFIG_ATBM_SMPS)
	struct atbm_ieee80211_mgmt *mgmt = (void *) skb->data;
	struct ieee80211_local *local = sta->local;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	
	if (ieee80211_is_action(mgmt->frame_control) &&
	    sdata->vif.type == NL80211_IFTYPE_STATION &&
	    mgmt->u.action.category == ATBM_WLAN_CATEGORY_HT &&
	    mgmt->u.action.u.ht_smps.action == WLAN_HT_ACTION_SMPS) {
		/*
		 * This update looks racy, but isn't -- if we come
		 * here we've definitely got a station that we're
		 * talking to, and on a managed interface that can
		 * only be the AP. And the only other place updating
		 * this variable is before we're associated.
		 */
		switch (mgmt->u.action.u.ht_smps.smps_control) {
		case WLAN_HT_SMPS_CONTROL_DYNAMIC:
			sta->sdata->u.mgd.ap_smps = IEEE80211_SMPS_DYNAMIC;
			break;
		case WLAN_HT_SMPS_CONTROL_STATIC:
			sta->sdata->u.mgd.ap_smps = IEEE80211_SMPS_STATIC;
			break;
		case WLAN_HT_SMPS_CONTROL_DISABLED:
		default: /* shouldn't happen since we don't send that */
			sta->sdata->u.mgd.ap_smps = IEEE80211_SMPS_OFF;
			break;
		}

		ieee80211_queue_work(&local->hw, &local->recalc_smps);
	}
#endif
}



static int ieee80211_tx_radiotap_len(struct ieee80211_tx_info *info)
{
	int len = sizeof(struct ieee80211_radiotap_header);
	/* IEEE80211_RADIOTAP_TX_FLAGS */
	len += 2;

	/* IEEE80211_RADIOTAP_DATA_RETRIES */
	len += 1;
	return len;
}

static void ieee80211_add_tx_radiotap_header(struct ieee80211_supported_band
					     *sband, struct sk_buff *skb,
					     int retry_count, int rtap_len)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_radiotap_header *rthdr;
	unsigned char *pos;
	u16 txflags;

	rthdr = (struct ieee80211_radiotap_header *) atbm_skb_push(skb, rtap_len);

	memset(rthdr, 0, rtap_len);
	rthdr->it_len = cpu_to_le16(rtap_len);
	rthdr->it_present =
		cpu_to_le32((1 << IEEE80211_RADIOTAP_TX_FLAGS) |
			    (1 << IEEE80211_RADIOTAP_DATA_RETRIES));
	pos = (unsigned char *)(rthdr + 1);

	/*
	 * XXX: Once radiotap gets the bitmap reset thing the vendor
	 *	extensions proposal contains, we can actually report
	 *	the whole set of tries we did.
	 */
	/* IEEE80211_RADIOTAP_TX_FLAGS */
	txflags = 0;
	if (!(info->flags & IEEE80211_TX_STAT_ACK) &&
	    !is_multicast_ether_addr(hdr->addr1))
		txflags |= IEEE80211_RADIOTAP_F_TX_FAIL;

	put_unaligned_le16(txflags, pos);
	pos += 2;

	/* IEEE80211_RADIOTAP_DATA_RETRIES */
	/* for now report the total retry_count */
	*pos = retry_count;
	pos++;
}

/*
 * Use a static threshold for now, best value to be determined
 * by testing ...
 * Should it depend on:
 *  - on # of retransmissions
 *  - current throughput (higher value for higher tpt)?
 */
#define STA_LOST_PKT_THRESHOLD	50
void ieee80211_tx_8023_status(struct ieee80211_local* local,struct sk_buff* skb)
{
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);
	struct sta_info* sta;

	if(info->control.sta){
		sta = container_of(info->control.sta, struct sta_info, sta);
		if(info->flags & IEEE80211_TX_STATUS_EOSP){
			clear_sta_flag(sta, WLAN_STA_SP);
		}
	}
	
	atbm_dev_kfree_skb(skb);
}

void ieee80211_tx_80211_status(struct ieee80211_local* local,struct sk_buff* skb)
{
	struct sk_buff* skb2;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);
	struct sta_info* sta = NULL;
	struct ieee80211_hdr* hdr = (struct ieee80211_hdr*)skb->data;
	bool acked;
	u16 type;
	bool send_to_cooked;
	struct net_device* prev_dev = NULL;
	int rtap_len;
	struct ieee80211_supported_band* sband;
	struct ieee80211_sub_if_data *sdata;
	
	acked = !!(info->flags & IEEE80211_TX_STAT_ACK);
	
	if(info->control.sta)
		sta = container_of(info->control.sta, struct sta_info, sta);
	
	if (!(info->flags & IEEE80211_TX_CTL_INJECTED) && acked && sta)
		ieee80211_frame_acked(sta, skb);
	
#ifdef ATBM_AP_SME
	if ((info->flags & IEEE80211_TX_AP_HANDLE_STATUS) && sta) {
		if (!ieee80211_ap_sme_tx_mgmt_status(sta->sdata, skb)) {
			rcu_read_unlock();
			return;
		}
	}
#endif
	
	type = le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_FTYPE;
	
	if (info->flags & IEEE80211_TX_INTFL_NL80211_FRAME_TX) {
		u64 cookie = (unsigned long)skb;
		atbm_printk_err("TX Status[%x][%d]\n", hdr->frame_control, !!(info->flags & IEEE80211_TX_STAT_ACK));
#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,6,0))
		cfg80211_mgmt_tx_status(
			skb->dev, cookie, skb->data, skb->len,
			!!(info->flags & IEEE80211_TX_STAT_ACK), GFP_ATOMIC);
#else
		cfg80211_mgmt_tx_status(
			skb->dev->ieee80211_ptr, cookie, skb->data, skb->len,
			!!(info->flags & IEEE80211_TX_STAT_ACK), GFP_ATOMIC);
#endif
	}

	/* this was a transmitted frame, but now we want to reuse it */
	atbm_skb_orphan(skb);

	/* Need to make a copy before skb->cb gets cleared */
	send_to_cooked = !!(info->flags & IEEE80211_TX_CTL_INJECTED) || (type != IEEE80211_FTYPE_DATA);

	/*
	 * This is a bit racy but we can avoid a lot of work
	 * with this test...
	 */
	if (!local->monitors && (!send_to_cooked || !local->cooked_mntrs)) {
		atbm_dev_kfree_skb(skb);
		return;
	}

//	atbm_printk_err("TX Status[%x][%d][%d]\n", hdr->frame_control, !!(info->flags & IEEE80211_TX_STAT_ACK), skb->len);
	/* send frame to monitor interfaces now */
	rtap_len = ieee80211_tx_radiotap_len(info);
	if (WARN_ON_ONCE(atbm_skb_headroom(skb) < rtap_len)) {
		atbm_dev_kfree_skb(skb);
		return;
	}	
	sband = local->hw.wiphy->bands[info->band];
	ieee80211_add_tx_radiotap_header(sband, skb, 0, rtap_len);

	/* XXX: is this sufficient for BPF? */
	atbm_skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));

	
	list_for_each_entry_rcu(sdata, &local->interfaces, list){
		if (sdata->vif.type == NL80211_IFTYPE_MONITOR) {
			if (!ieee80211_sdata_running(sdata))
				continue;

			if ((sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES) &&
				!send_to_cooked)
				continue;

			if (prev_dev) {
				skb2 = atbm_skb_clone(skb, GFP_ATOMIC);
				if (skb2) {
					skb2->dev = prev_dev;
					atbm_netif_rx(skb2);
				}
			}

			prev_dev = sdata->dev;
		}
	}
	if (prev_dev) {
		skb->dev = prev_dev;
		atbm_netif_rx(skb);
		skb = NULL;
	}
	atbm_dev_kfree_skb(skb);
}

void ieee80211_tx_status(struct ieee80211_hw* hw, struct sk_buff* skb)
{
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);
	
	if(info->flags & IEEE80211_TX_CTL_8023){
		ieee80211_tx_8023_status(hw_to_local(hw), skb);
	}else {
		ieee80211_tx_80211_status(hw_to_local(hw), skb);
	}
}

void ieee80211_sta_rate_status(struct ieee80211_sta* pubsta)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);

	rate_control_tx_hmac(sta->local,sta);
}
void ieee80211_report_low_ack(struct ieee80211_sta* pubsta, u32 num_packets)
{
	struct sta_info *sta = container_of(pubsta, struct sta_info, sta);
	cfg80211_cqm_pktloss_notify(sta->sdata->dev, sta->sta.addr,
				    num_packets, GFP_ATOMIC);
}
