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
 *
 * Transmit and frame generation functions.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/bitmap.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/tcp.h>
#include <net/net_namespace.h>
#include <net/ieee80211_radiotap.h>
#include <net/cfg80211.h>
#include <net/atbm_mac80211.h>
#include <asm/unaligned.h>
#include <linux/udp.h>
#include <net/ip.h>


#include "ieee80211_i.h"
#include "driver-ops.h"
#include "mesh.h"
#include "wep.h"
#include "wpa.h"
#include "wapi.h"
#include "wme.h"
#include "rate.h"
#include "twt.h"

static int wme_enable = 1;
module_param(wme_enable, int, 0644);
/* misc utils */
extern int Atbm_Test_Success;
extern struct etf_test_config etf_config;
#ifdef CONFIG_ATBM_SUPPORT_TSO
static int ieee80211_tx_gso_checksum(struct ieee80211_sub_if_data* sdata,struct sk_buff* skb)
{	
	if(!skb_is_gso(skb)){
		if(skb->ip_summed == CHECKSUM_PARTIAL){
			/*TSO may be not do check sum,so here do*/
			/*IF hw support tcp/udp check sum,remove the code*/
			if (skb_checksum_help(skb)){
				atbm_printk_err("skb_checksum_help err\n");
				goto err;
			}
		}
	}else {
		struct iphdr *iph;
		int ihl;
		struct tcphdr *th;
		unsigned thlen;

		if(unlikely(skb->protocol != htons(ETH_P_IP))){
			atbm_printk_err("we only support tso,check kernel!\n");
			goto err;
		}
		
		if (unlikely(!atbm_pskb_may_pull(skb, sizeof(*iph))))
			goto err;

		iph = ip_hdr(skb);
		ihl = iph->ihl * 4;
		
		if (unlikely(!atbm_pskb_may_pull(skb, ihl)))
			goto err;
		/*
		*skip ip header
		*/
		if(unlikely((iph->protocol) != IPPROTO_TCP)){
			atbm_printk_err("only support tcp\n");
			goto err;
		}
		
		if (unlikely(!atbm_pskb_may_pull(skb, sizeof(*th))))
			goto err;

		th = tcp_hdr(skb);
		thlen = th->doff * 4;
		
		if (thlen < sizeof(*th))
			goto err;

		if (unlikely(!atbm_pskb_may_pull(skb, thlen)))
			goto err;
	}
	return 0;
err:
	atbm_printk_err("ipv4_tso err\n");
	atbm_dev_kfree_skb(skb);
	return 1;
}

static void ieee80211_tx_ipv4_tso_init(struct sk_buff* skb)
{
	struct ieee80211_tx_info  *info = IEEE80211_SKB_CB(skb);
	
	info->gso = (skb_is_gso(skb) != 0);
	
	if(info->gso == 0){
		return;
	}
	info->control.tso.network_len    = skb_transport_header(skb) - skb_network_header(skb);
	info->control.tso.transport_len  = tcp_hdr(skb)->doff * 4;
	
}
#endif

static struct sk_buff *ieee80211_alloc_xmit_skb(struct sk_buff *skb,struct net_device *dev)
{
#if defined (ATBM_ALLOC_SKB_DEBUG)
	struct sk_buff *xmit_skb;

	xmit_skb = atbm_skb_copy(skb,GFP_ATOMIC);
	dev_kfree_skb(skb);
	
	return xmit_skb;
#else
	/*
	 * If the skb is shared we need to obtain our own copy.
	 */
	if (atbm_skb_shared(skb)) {
		struct sk_buff* tmp_skb;
		
		tmp_skb = atbm_skb_clone(skb, GFP_ATOMIC);
		if(tmp_skb){
			atbm_kfree_skb(skb);
			return tmp_skb;
		}
	}
	return skb;
#endif
}
static ieee80211_tx_result debug_noinline
ieee80211_tx_h_check_assoc(struct ieee80211_tx_data *tx)
{

	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx->skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);
	bool assoc = false;

	if (unlikely(info->flags & IEEE80211_TX_CTL_INJECTED))
		return TX_CONTINUE;

	if (unlikely(test_bit(SCAN_OFF_CHANNEL, &tx->local->scanning)) &&
	    !ieee80211_is_probe_req(hdr->frame_control) &&
	    !ieee80211_is_nullfunc(hdr->frame_control))
		/*
		 * When software scanning only nullfunc frames (to notify
		 * the sleep state to the AP) and probe requests (for the
		 * active scan) are allowed, all other frames should not be
		 * sent and we should not get here, but if we do
		 * nonetheless, drop them to avoid sending them
		 * off-channel. See the link below and
		 * ieee80211_start_scan() for more.
		 *
		 * http://article.gmane.org/gmane.linux.kernel.wireless.general/30089
		 */
		return TX_DROP;
#ifdef CONFIG_ATBM_SUPPORT_WDS
	if (tx->sdata->vif.type == NL80211_IFTYPE_WDS)
		return TX_CONTINUE;
#endif
#ifdef CONFIG_MAC80211_ATBM_MESH
	if (tx->sdata->vif.type == NL80211_IFTYPE_MESH_POINT)
		return TX_CONTINUE;
#endif
	if (tx->flags & IEEE80211_TX_PS_BUFFERED)
		return TX_CONTINUE;

	if (tx->sta)
		assoc = test_sta_flag(tx->sta, WLAN_STA_ASSOC);

	if (likely(tx->flags & IEEE80211_TX_UNICAST)) {
		if (unlikely(!assoc &&
			     tx->sdata->vif.type != NL80211_IFTYPE_ADHOC &&
			     ieee80211_is_data(hdr->frame_control))) {
#ifdef CONFIG_MAC80211_ATBM_VERBOSE_DEBUG
			atbm_printk_debug( "%s: dropped data frame to not "
			       "associated station %pM\n",
			       tx->sdata->name, hdr->addr1);
#endif /* CONFIG_MAC80211_ATBM_VERBOSE_DEBUG */
			I802_DEBUG_INC(tx->local->tx_handlers_drop_not_assoc);
			return TX_DROP;
		}
	} else {
		if (unlikely(ieee80211_is_data(hdr->frame_control) &&
	//		     tx->local->num_sta == 0 &&
				 tx->sdata->vif.type != NL80211_IFTYPE_AP &&
			     tx->sdata->vif.type != NL80211_IFTYPE_ADHOC)) {
			/*
			 * No associated STAs - no need to send multicast
			 * frames.
			 */
			return TX_DROP;
		}
		return TX_CONTINUE;
	}

	return TX_CONTINUE;
}

/* This function is called whenever the AP is about to exceed the maximum limit
 * of buffered frames for power saving STAs. This situation should not really
 * happen often during normal operation, so dropping the oldest buffered packet
 * from each queue should be OK to make some room for new frames. */
static void purge_old_ps_buffers(struct ieee80211_local *local)
{
	int total = 0, purged = 0;
	struct sk_buff *skb;
	struct sta_info *sta;

	/*
	 * virtual interfaces are protected by RCU
	 */
	rcu_read_lock();
	/*
	 * Drop one frame from each station from the lowest-priority
	 * AC that has frames at all.
	 */
	list_for_each_entry_rcu(sta, &local->sta_list, list) {
		int ac;

		for (ac = IEEE80211_AC_BK; ac >= IEEE80211_AC_VO; ac--) {
			skb = atbm_skb_dequeue(&sta->ps_tx_buf[ac]);
			total += atbm_skb_queue_len(&sta->ps_tx_buf[ac]);
			if (skb) {
				purged++;
				atbm_dev_kfree_skb(skb);
				break;
			}
		}
	}

	rcu_read_unlock();

	local->total_ps_buffered = total;
#ifdef CONFIG_MAC80211_ATBM_VERBOSE_PS_DEBUG
	wiphy_debug(local->hw.wiphy, "PS buffers full - purged %d frames\n",
		    purged);
#endif
}
static bool __ieee80211_tx_h_unicast_ps_buf(struct ieee80211_sub_if_data* sdata, struct sta_info* sta, struct sk_buff* skb)
{
	struct ieee80211_local* local = sdata->local;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);

	if (!sta) {
		return false;
	}
	
	if (unlikely((test_sta_flag(sta, WLAN_STA_PS_STA) ||
		test_sta_flag(sta, WLAN_STA_PS_DRIVER)) &&
		!(info->flags & IEEE80211_TX_CTL_POLL_RESPONSE))) {
		int ac = skb_get_queue_mapping(skb);

#ifdef CONFIG_MAC80211_ATBM_VERBOSE_PS_DEBUG
		atbm_printk_debug("STA %pM aid %d: PS buffer for AC %d\n",
			sta->sta.addr, sta->sta.aid, ac);
#endif /* CONFIG_MAC80211_ATBM_VERBOSE_PS_DEBUG */
		if (sdata->local->total_ps_buffered >= TOTAL_MAX_TX_BUFFER)
			purge_old_ps_buffers(local);
		if (atbm_skb_queue_len(&sta->ps_tx_buf[ac]) >= STA_MAX_TX_BUFFER) {
			struct sk_buff* old = atbm_skb_dequeue(&sta->ps_tx_buf[ac]);
#ifdef CONFIG_MAC80211_ATBM_VERBOSE_PS_DEBUG
			if (net_ratelimit())
				atbm_printk_debug("%s: STA %pM TX buffer for "
					"AC %d full - dropping oldest frame\n",
					sdata->name, sta->sta.addr, ac);
#endif
			atbm_dev_kfree_skb(old);
		}
		else
			local->total_ps_buffered++;

		info->control.jiffies = jiffies;
		info->control.vif = &sdata->vif;
		info->flags |= IEEE80211_TX_INTFL_NEED_TXPROCESSING;
		atbm_skb_queue_tail(&sta->ps_tx_buf[ac],skb);

		if (!atbm_timer_pending(&local->sta_cleanup))
			atbm_mod_timer(&local->sta_cleanup,
				round_jiffies(jiffies +
					STA_INFO_CLEANUP_INTERVAL));

		/*
		 * We queued up some frames, so the TIM bit might
		 * need to be set, recalculate it.
		 */
		sta_info_recalc_tim(sta);

		return true;
	}

	return false;
}

static ieee80211_tx_result debug_noinline
ieee80211_tx_h_check_control_port_protocol(struct ieee80211_tx_data *tx)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx->skb);

	if (unlikely(tx->sdata->control_port_protocol == tx->skb->protocol &&
		     tx->sdata->control_port_no_encrypt))
		info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;

	return TX_CONTINUE;
}
static ieee80211_tx_result debug_noinline
ieee80211_tx_h_stats(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb = tx->skb;

	if (!tx->sta)
		return TX_CONTINUE;

	tx->sta->tx_packets++;
	do {
		tx->sta->tx_fragments++;
		tx->sta->tx_bytes += skb->len;
	} while ((skb = skb->next));

	return TX_CONTINUE;
}
static ieee80211_tx_result debug_noinline
ieee80211_tx_h_work(struct ieee80211_tx_data *tx)
{
	return ieee80211_work_tx_mgmt(tx->sdata,tx->skb);
}
/* actual transmit path */
static bool __ieee80211_tx_prep_agg(struct sta_info* sta,struct sk_buff* skb,
	struct ieee80211_tx_info* info,
	struct tid_ampdu_tx* tid_tx,
	int tid)
{
	bool queued = false;
	
	if (test_bit(HT_AGG_STATE_OPERATIONAL, &tid_tx->state)) {
		info->flags |= IEEE80211_TX_CTL_AMPDU;
	}
#if 0
	else if (test_bit(HT_AGG_STATE_WANT_START, &tid_tx->state)) {
		/*
		 * nothing -- this aggregation session is being started
		 * but that might still fail with the driver
		 */
	}
#endif
	else {
		spin_lock(&sta->lock);
		/*
		 * Need to re-check now, because we may get here
		 *
		 *  1) in the window during which the setup is actually
		 *     already done, but not marked yet because not all
		 *     packets are spliced over to the driver pending
		 *     queue yet -- if this happened we acquire the lock
		 *     either before or after the splice happens, but
		 *     need to recheck which of these cases happened.
		 *
		 *  2) during session teardown, if the OPERATIONAL bit
		 *     was cleared due to the teardown but the pointer
		 *     hasn't been assigned NULL yet (or we loaded it
		 *     before it was assigned) -- in this case it may
		 *     now be NULL which means we should just let the
		 *     packet pass through because splicing the frames
		 *     back is already done.
		 */
		tid_tx = rcu_dereference_protected_tid_tx(sta, tid);

		if (!tid_tx) {
			/* do nothing, let packet pass through */
		}
		else if (test_bit(HT_AGG_STATE_OPERATIONAL, &tid_tx->state)) {
			info->flags |= IEEE80211_TX_CTL_AMPDU;
		}
		else {
			queued = true;
			info->control.vif = &sta->sdata->vif;
			info->flags |= IEEE80211_TX_INTFL_NEED_TXPROCESSING;
			__atbm_skb_queue_tail(&tid_tx->pending, skb);
		}
		spin_unlock(&sta->lock);
	}

	return queued;
}
/*
 * initialises @tx
 */
static ieee80211_tx_result
ieee80211_tx_prepare(struct ieee80211_sub_if_data *sdata,
		     struct ieee80211_tx_data *tx,
		     struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
#ifdef CONFIG_WAPI_SUPPORT
	int hdrlen;
#endif

	memset(tx, 0, sizeof(*tx));
	tx->skb = skb;
	tx->local = local;
	tx->sdata = sdata;
	tx->channel = chan_state->conf.channel;

	/*
	 * If this flag is set to true anywhere, and we get here,
	 * we are doing the needed processing, so remove the flag
	 * now.
	 */
	info->flags &= ~IEEE80211_TX_INTFL_NEED_TXPROCESSING;

	hdr = (struct ieee80211_hdr *) skb->data;

	if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN) {
		tx->sta = rcu_dereference(sdata->u.vlan.sta);
		if (!tx->sta && sdata->dev->ieee80211_ptr->use_4addr)
			return TX_DROP;
	} else if (info->flags & IEEE80211_TX_CTL_INJECTED) {
		tx->sta = sta_info_get_bss(sdata, hdr->addr1);
	}
	if (!tx->sta)
		tx->sta = sta_info_get(sdata, hdr->addr1);
	if (is_multicast_ether_addr(hdr->addr1)) {
		tx->flags &= ~IEEE80211_TX_UNICAST;
		info->flags |= IEEE80211_TX_CTL_NO_ACK;
	} else {
		tx->flags |= IEEE80211_TX_UNICAST;
		info->flags |= IEEE80211_TX_UCAST;
		/*
		 * Flags are initialized to 0. Hence, no need to
		 * explicitly unset IEEE80211_TX_CTL_NO_ACK since
		 * it might already be set for injected frames.
		 */
	}

	info->flags |= IEEE80211_TX_CTL_DONTFRAG;

	if (!tx->sta)
		info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT;
	else if (test_and_clear_sta_flag(tx->sta, WLAN_STA_CLEAR_PS_FILT))
		info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT;
#ifdef CONFIG_WAPI_SUPPORT
	hdrlen = ieee80211_hdrlen(hdr->frame_control);
	if (skb->len > hdrlen + sizeof(rfc1042_header) + 2) {
		u8 *pos = &skb->data[hdrlen + sizeof(rfc1042_header)];
		tx->ethertype = (pos[0] << 8) | pos[1];
	}
#endif
	return TX_CONTINUE;
}
/*
 * Invoke TX handlers, return 0 on success and non-zero if the
 * frame was dropped or queued.
 */
static int invoke_tx_handlers(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb = tx->skb;
	ieee80211_tx_result res = TX_DROP;

#define CALL_TXH(txh) \
	do {				\
		res = txh(tx);		\
		if (res != TX_CONTINUE)	\
			goto txh_done;	\
	} while (0)
	CALL_TXH(ieee80211_tx_h_check_assoc);
	CALL_TXH(ieee80211_tx_h_check_control_port_protocol);
	/* handlers after fragment must be aware of tx info fragmentation! */
	CALL_TXH(ieee80211_tx_h_stats);
	CALL_TXH(ieee80211_tx_h_work);
#undef CALL_TXH

 txh_done:
	if (unlikely(res == TX_DROP)) {
		I802_DEBUG_INC(tx->local->tx_handlers_drop);
		while (skb) {
			struct sk_buff *next;

			next = skb->next;
			atbm_dev_kfree_skb(skb);
			skb = next;
		}
		return -1;
	} else if (unlikely(res == TX_QUEUED)) {
		I802_DEBUG_INC(tx->local->tx_handlers_queued);
		return -1;
	}

	return 0;
}
static bool __ieee80211_tx(struct ieee80211_sub_if_data* sdata,
		struct sk_buff* skb, struct sta_info* sta, bool txpending)
{
	struct ieee80211_local* local = sdata->local;
	int q;
	unsigned long flags;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);
	q = info->hw_queue;
	
	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	if (local->queue_stop_reasons[q] ||
		(!txpending && !atbm_skb_queue_empty(&local->pending[q]))) {
		if(info->ignore_reason && ((local->queue_stop_reasons[q] & ~BIT(info->ignore_reason)) == 0)){
			atbm_printk_err("ignore pending[%d][%lx]\n",info->ignore_reason,local->queue_stop_reasons[q]);
		}else {
			if (unlikely(txpending))
				__atbm_skb_queue_head(&local->pending[q],skb);
			else
				__atbm_skb_queue_tail(&local->pending[q],skb);

			spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
			atbm_printk_err("add to pending(%d)(%lx)\n", q, local->queue_stop_reasons[q]);
			return false;
		}
	}
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
	
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_MONITOR:
		if (local->monitor_sdata != sdata)
			info->control.vif = NULL;
		else
			atbm_printk_debug("%s:monitor[%s] send\n", __func__, sdata->name);
		break;
	case NL80211_IFTYPE_AP_VLAN:
		info->control.vif = &container_of(sdata->bss,
			struct ieee80211_sub_if_data, u.ap)->vif;
		break;
	default:
		/* keep */
		break;
	}
	if (sta && sta->uploaded)
		info->control.sta = &sta->sta;
	else
		info->control.sta = NULL;
	/*
	*info->control.jiffies is not used from here
	*/
	info->control.jiffies = 0;
#ifdef CONFIG_ATBM_SUPPORT_TSO
	ieee80211_tx_ipv4_tso_init(skb);
#endif
	drv_tx(local, skb);
	
	return true;
}
static bool ieee80211_tx_8023(struct ieee80211_sub_if_data* sdata, struct sk_buff* skb, struct sta_info* sta, bool txpending)
{
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);

	info->flags &= ~IEEE80211_TX_INTFL_NEED_TXPROCESSING;
	
	if (likely(sta)) {
		info->flags |= IEEE80211_TX_UCAST;
		info->control.sta = &sta->sta;
		/*
		*set aggr flags
		*/
		if((info->flags & IEEE80211_TX_CTL_QOS) && sta_support_aggr(sta)){
			struct tid_ampdu_tx* tid_tx;
			int tid;
			tid = skb->priority & 0x07;

			tid_tx = rcu_dereference(sta->ampdu_mlme.tid_tx[tid]);
			if (tid_tx) {
				bool queued;

				queued = __ieee80211_tx_prep_agg(sta, skb, info,
					tid_tx, tid);

				if (unlikely(queued)) {
					return true;
				}
			}
			
			if((info->flags & IEEE80211_TX_CTL_AMPDU) == 0){
				if (ieee80211_start_tx_ba_session(info->control.sta,tid, 0)) {
					atbm_printk_debug("start tx ba session err\n");
				}
			}
		}
		/*
		*cache skb for ps sta
		*/
		if(__ieee80211_tx_h_unicast_ps_buf(sdata, sta, skb) == true){
			/*
			*ucast ps buff
			*/
			atbm_printk_tx("STA[%pM] in ps\n", sta->sta.addr);
			return true;
		}
		/*
		*cache skb for twt sta
		*/
		if(ieee80211_tx_h_twt_buf(sdata, sta, skb) == true){
			atbm_printk_err("STA[%pM] in twt\n", sta->sta.addr);
			return true;
		}
	}
	return __ieee80211_tx(sdata,skb,sta,txpending);
}
/*
 * Returns false if the frame couldn't be transmitted but was queued instead.
 */
static bool ieee80211_tx(struct ieee80211_sub_if_data *sdata,
			 struct sk_buff *skb, bool txpending)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct ieee80211_tx_data tx;
	ieee80211_tx_result res_prepare;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	bool result = true;

	if (unlikely(skb->len < 10)) {
		atbm_dev_kfree_skb(skb);
		return true;
	}

	rcu_read_lock();

	/* initialises tx */
	res_prepare = ieee80211_tx_prepare(sdata, &tx, skb);

	if (unlikely(res_prepare == TX_DROP)) {
		atbm_dev_kfree_skb(skb);
		goto out;
	} else if (unlikely(res_prepare == TX_QUEUED)) {
		goto out;
	}

	tx.channel = chan_state->conf.channel;
	info->band = tx.channel->band;

	/* set up hw_queue value early */
	if (!(info->flags & IEEE80211_TX_CTL_TX_OFFCHAN) ||
	    !(local->hw.flags & IEEE80211_HW_QUEUE_CONTROL))
		info->hw_queue =
			sdata->vif.hw_queue[skb_get_queue_mapping(skb)];

	if (!invoke_tx_handlers(&tx))
		result = __ieee80211_tx(sdata, tx.skb, tx.sta, txpending);
 out:
	rcu_read_unlock();
	return result;
}
void ieee80211_xmit_8023(struct ieee80211_sub_if_data* sdata, struct sta_info* sta,struct sk_buff* skb)
{
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);
	struct ieee80211_channel_state* chan_state = ieee80211_get_channel_state(sdata->local, sdata);

    info->band = chan_state->conf.channel->band;

    info->hw_queue = sdata->vif.hw_queue[skb_get_queue_mapping(skb)];

	info->control.vif = &sdata->vif;
	
	if (unlikely(sdata->control_port_protocol == skb->protocol &&
		sdata->control_port_no_encrypt)) {
		info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	}
	ieee80211_tx_8023(sdata, skb, sta,false);
}

/*
*only mgmt and EAP(ap mode) with the full mac80211 header call this function to send 
*/
void ieee80211_xmit(struct ieee80211_sub_if_data* sdata, struct sk_buff* skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
#ifdef CONFIG_MAC80211_ATBM_MESH
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
#endif

	rcu_read_lock();
	info->control.vif = &sdata->vif;
#ifdef CONFIG_MAC80211_ATBM_MESH
	hdr = (struct ieee80211_hdr *) skb->data;
	if (ieee80211_vif_is_mesh(&sdata->vif) &&
	    ieee80211_is_data(hdr->frame_control) &&
		!is_multicast_ether_addr(hdr->addr1))
			if (mesh_nexthop_lookup(skb, sdata)) {
				/* skb queued: don't free */
				rcu_read_unlock();
				return;
			}
#endif
	info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS | IEEE80211_TX_CTL_USE_MINRATE;
	ieee80211_tx(sdata, skb, false);
	rcu_read_unlock();
}

static bool ieee80211_parse_tx_radiotap(struct sk_buff *skb)
{
	struct ieee80211_radiotap_iterator iterator;
	struct ieee80211_radiotap_header *rthdr =
		(struct ieee80211_radiotap_header *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int ret = ieee80211_radiotap_iterator_init(&iterator, rthdr, skb->len,
						   NULL);
	u16 txflags;

	info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
		       IEEE80211_TX_CTL_DONTFRAG;

	/*
	 * for every radiotap entry that is present
	 * (ieee80211_radiotap_iterator_next returns -ENOENT when no more
	 * entries present, or -EINVAL on error)
	 */

	while (!ret) {
		ret = ieee80211_radiotap_iterator_next(&iterator);

		if (ret)
			continue;

		/* see if this argument is something we can use */
		switch (iterator.this_arg_index) {
		/*
		 * You must take care when dereferencing iterator.this_arg
		 * for multibyte types... the pointer is not aligned.  Use
		 * get_unaligned((type *)iterator.this_arg) to dereference
		 * iterator.this_arg for type "type" safely on all arches.
		*/
		case IEEE80211_RADIOTAP_FLAGS:
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_FCS) {
				/*
				 * this indicates that the skb we have been
				 * handed has the 32-bit FCS CRC at the end...
				 * we should react to that by snipping it off
				 * because it will be recomputed and added
				 * on transmission
				 */
				if (skb->len < (iterator._max_length + FCS_LEN))
					return false;

				atbm_skb_trim(skb, skb->len - FCS_LEN);
			}
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_WEP)
				info->flags &= ~IEEE80211_TX_INTFL_DONT_ENCRYPT;
			if (*iterator.this_arg & IEEE80211_RADIOTAP_F_FRAG)
				info->flags &= ~IEEE80211_TX_CTL_DONTFRAG;
			break;

		case IEEE80211_RADIOTAP_TX_FLAGS:
			txflags = get_unaligned_le16(iterator.this_arg);
			if (txflags & IEEE80211_RADIOTAP_F_TX_NOACK)
				info->flags |= IEEE80211_TX_CTL_NO_ACK;
			break;

		/*
		 * Please update the file
		 * Documentation/networking/mac80211-injection.txt
		 * when parsing new fields here.
		 */

		default:
			break;
		}
	}

	if (ret != -ENOENT) /* ie, if we didn't simply run out of fields */
		return false;

	/*
	 * remove the radiotap header
	 * iterator->_max_length was sanity-checked against
	 * skb->len by iterator init
	 */
	atbm_skb_pull(skb, iterator._max_length);

	return true;
}

static bool ieee80211_ap_can_xmit(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct ieee80211_channel *chan = chan_state->conf.channel;
	bool can_xmit = true;
	
#ifndef CONFIG_ATBM_5G_PRETEND_2G
	if ((chan->flags & (
		#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,11,0))
		IEEE80211_CHAN_NO_IBSS |
		#endif
		IEEE80211_CHAN_RADAR |
	     #if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,11,0))
	     IEEE80211_CHAN_PASSIVE_SCAN
	     #else
	     IEEE80211_CHAN_NO_IR
	     #endif
	     )))
	    can_xmit = false;
		
#else
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
	struct cfg80211_chan_def chandef;
	cfg80211_chandef_create(&chandef,chan,vif_chw(&sdata->vif));
	can_xmit = cfg80211_reg_can_beacon(local->hw.wiphy,&chandef
			   #if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
		       ,sdata->vif.type
		       #endif
		       );
#else
	BUG_ON(chan == NULL);
	can_xmit = true;
#endif
#endif
	return can_xmit;
}
netdev_tx_t ieee80211_monitor_start_xmit(struct sk_buff *skb,
					 struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *tmp_sdata, *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_radiotap_header *prthdr =
		(struct ieee80211_radiotap_header *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr;
	u16 len_rthdr;
	int hdrlen;
	bool monitor_relate = false;

	skb = ieee80211_alloc_xmit_skb(skb,dev);

	if(skb == NULL){
		return NETDEV_TX_OK;
	}
	/*
	 * Frame injection is not allowed if beaconing is not allowed
	 * or if we need radar detection. Beaconing is usually not allowed when
	 * the mode or operation (Adhoc, AP, Mesh) does not support DFS.
	 * Passive scan is also used in world regulatory domains where
	 * your country is not known and as such it should be treated as
	 * NO TX unless the channel is explicitly allowed in which case
	 * your current regulatory domain would not have the passive scan
	 * flag.
	 *
	 * Since AP mode uses monitor interfaces to inject/TX management
	 * frames we can make AP mode the exception to this rule once it
	 * supports radar detection as its implementation can deal with
	 * radar detection by itself. We can do that later by adding a
	 * monitor flag interfaces used for AP support.
	 */
	if(ieee80211_ap_can_xmit(dev)==false)
		goto fail;
	/* check for not even having the fixed radiotap header part */
	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header)))
		goto fail; /* too short to be possibly valid */

	/* is it a header version we can trust to find length from? */
	if (unlikely(prthdr->it_version))
		goto fail; /* only version 0 is supported */

	/* then there must be a radiotap header with a length we can use */
	len_rthdr = ieee80211_get_radiotap_len(skb->data);

	/* does the skb contain enough to deliver on the alleged length? */
	if (unlikely(skb->len < len_rthdr))
		goto fail; /* skb too short for claimed rt header extent */

	/*
	 * fix up the pointers accounting for the radiotap
	 * header still being in there.  We are being given
	 * a precooked IEEE80211 header so no need for
	 * normal processing
	 */
	atbm_skb_set_mac_header(skb, len_rthdr);
	/*
	 * these are just fixed to the end of the rt area since we
	 * don't have any better information and at this point, nobody cares
	 */
	atbm_skb_set_network_header(skb, len_rthdr);
	atbm_skb_set_transport_header(skb, len_rthdr);

	if (skb->len < len_rthdr + 2)
		goto fail;

	hdr = (struct ieee80211_hdr *)(skb->data + len_rthdr);
	hdrlen = ieee80211_hdrlen(hdr->frame_control);

	if (skb->len < len_rthdr + hdrlen)
		goto fail;

	/*
	 * Initialize skb->protocol if the injected frame is a data frame
	 * carrying a rfc1042 header
	 */
	if (ieee80211_is_data(hdr->frame_control) &&
	    skb->len >= len_rthdr + hdrlen + sizeof(rfc1042_header) + 2) {
		u8 *payload = (u8 *)hdr + hdrlen;

		if (atbm_compare_ether_addr(payload, rfc1042_header) == 0)
			skb->protocol = cpu_to_be16((payload[6] << 8) |
						    payload[7]);
	}

	memset(info, 0, sizeof(*info));

	info->flags = IEEE80211_TX_CTL_REQ_TX_STATUS |
		      IEEE80211_TX_CTL_INJECTED;

	/* process and remove the injection radiotap header */
	if (!ieee80211_parse_tx_radiotap(skb))
		goto fail;
//	atbm_printk_err("TX monitor[%x][%d][%d]\n",hdr->frame_control,skb->len);
	rcu_read_lock();

	/*
	 * We process outgoing injected frames that have a local address
	 * we handle as though they are non-injected frames.
	 * This code here isn't entirely correct, the local MAC address
	 * isn't always enough to find the interface to use; for proper
	 * VLAN/WDS support we will need a different mechanism (which
	 * likely isn't going to be monitor interfaces).
	 */
	list_for_each_entry_rcu(tmp_sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(tmp_sdata))
			continue;
		if(tmp_sdata == local->monitor_sdata){
			continue;
		}
		if (tmp_sdata->vif.type == NL80211_IFTYPE_MONITOR ||
		    tmp_sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
		    tmp_sdata->vif.type == NL80211_IFTYPE_WDS)
			continue;
		if (atbm_compare_ether_addr(tmp_sdata->vif.addr, hdr->addr2) == 0) {
			sdata = tmp_sdata;
			monitor_relate = true;
			break;
		}
	}
	if((monitor_relate == true) || 
	   (sdata && !(sdata->u.mntr_flags & MONITOR_FLAG_COOK_FRAMES))){
		ieee80211_xmit(sdata, skb);
	}else {
		atbm_dev_kfree_skb(skb);
		atbm_printk_err( "%s:cannot find ralated sdata\n",sdata->name);
	}
	rcu_read_unlock();

	return NETDEV_TX_OK;

fail:
	atbm_dev_kfree_skb(skb);
	return NETDEV_TX_OK; /* meaning, we dealt with the skb */
}

/**
 * ieee80211_subif_start_xmit - netif start_xmit function for Ethernet-type
 * subinterfaces (wlan#, WDS, and VLAN interfaces)
 * @skb: packet to be sent
 * @dev: incoming interface
 *
 * Returns: 0 on success (and frees skb in this case) or 1 on failure (skb will
 * not be freed, and caller is responsible for either retrying later or freeing
 * skb).
 *
 * This function takes in an Ethernet header and encapsulates it with suitable
 * IEEE 802.11 header based on which interface the packet is coming in. The
 * encapsulated packet will then be passed to master interface, wlan#.11, for
 * transmission (through low-level driver).
 */
int ieee80211_lookup_sta(struct ieee80211_sub_if_data* sdata, struct sk_buff* skb, struct sta_info **psta)
{
	u8* ra = skb->data;
	
	*psta = NULL;
	
	switch (sdata->vif.type) {
	case NL80211_IFTYPE_AP_VLAN:
		
		*psta = rcu_dereference(sdata->u.vlan.sta);
		if (*psta) {
			return 0;
		}
		atbm_fallthrough;	
	case NL80211_IFTYPE_AP:
		ra = skb->data;
		break;
#ifdef CONFIG_ATBM_SUPPORT_WDS
	case NL80211_IFTYPE_WDS:
		ra = sdata->u.wds.remote_addr;
		break;
#endif
	case NL80211_IFTYPE_STATION:
		ra = sdata->u.mgd.bssid;
		break;
#ifdef CONFIG_ATBM_SUPPORT_IBSS
	case NL80211_IFTYPE_ADHOC:
		/* DA SA BSSID */
		ra = skb->data;
		break;
#endif
	default:
		atbm_printk_err("vif type(%d) is not support\n", sdata->vif.type);
		return -1;
	}

	if (!is_multicast_ether_addr(ra)) {
		*psta = sta_info_get(sdata, ra);
		if (*psta == NULL) {
			atbm_printk_limit("not assoc \n");
			return -1;
		}
		if (!test_sta_flag(*psta, WLAN_STA_ASSOC)) {
			/*not assoced with us,so return fail*/
			return -1;
		}
	}

	return 0;
}
static netdev_tx_t _ieee80211_subif_start_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_tx_info *info;
	int ret = NETDEV_TX_BUSY;
	u16 ethertype;
	struct sta_info *sta = NULL;
	bool wme_sta = false, authorized = false, htc = false;
	
	rcu_read_lock();
	
	if (unlikely(skb->len < ETH_HLEN)) {
		ret = NETDEV_TX_OK;
		goto fail;
	}
	
#ifdef CONFIG_MAC80211_BRIDGE
	{
		struct ieee80211_sub_if_data *tmp_sta = ieee80211_brigde_sdata_check(sdata->local,&skb,sdata);

		if(tmp_sta != sdata){
			sdata = tmp_sta;
			dev = sdata->dev;
		}
	}
	if ((sdata->vif.type == NL80211_IFTYPE_STATION) && ieee80211_brigde_change_txhdr(sdata, &skb) == -1) {
		ret = NETDEV_TX_OK;
		goto fail;
	}
#endif
	if (ieee80211_lookup_sta(sdata, skb, &sta)) {
		ret = NETDEV_TX_OK;
		goto fail;
	}
	
	/* convert Ethernet header to proper 802.11 header (based on
	 * operation mode) */
	ethertype = (skb->data[12] << 8) | skb->data[13];

	/*
	 * There's no need to try to look up the destination
	 * if it is a multicast address (which can only happen
	 * in AP mode)
	 */
	if (sta) {
		authorized = test_sta_flag(sta, WLAN_STA_AUTHORIZED);
		wme_sta = test_sta_flag(sta, WLAN_STA_WME);
		htc = test_sta_flag(sta, WLAN_STA_HE);
	}
	/*
	 * Drop unicast frames to unauthorised stations unless they are
	 * EAPOL frames from the local station.
	 */
	if (unlikely(!ieee80211_vif_is_mesh(&sdata->vif) &&
				sta && !authorized &&
		     	(cpu_to_be16(ethertype) != sdata->control_port_protocol ||
		      atbm_compare_ether_addr(sdata->vif.addr, skb->data + ETH_ALEN)))) {
#ifdef CONFIG_MAC80211_ATBM_VERBOSE_DEBUG
		if (net_ratelimit())
			atbm_printk_debug("%s: dropped frame to %pM"
			       " (unauthorized port)\n", dev->name,
			       hdr.addr1);
#endif

		I802_DEBUG_INC(local->tx_handlers_drop_unauth_port);
		atbm_printk_err("drop unauthorized port[%x][%x][%pM][%pM]\n", cpu_to_be16(ethertype),
			sdata->control_port_protocol, sdata->vif.addr, skb->data + ETH_ALEN);
		ret = NETDEV_TX_OK;
		goto fail;
	}
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;
	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0))
	dev->trans_start = jiffies;
#endif
	info = IEEE80211_SKB_CB(skb);
	/*clear cb for later use*/
	memset(info, 0, sizeof(struct ieee80211_tx_info));
	
	info->flags = IEEE80211_TX_CTL_8023;
	
	if (ethertype == ETH_P_PAE){
		info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS | IEEE80211_TX_CTL_USE_MINRATE;
		wme_sta = false;
	}
	if (wme_sta == true) {
		wme_sta = wme_enable ? true:false;
	}
	if (wme_sta == true) {
		info->flags |= IEEE80211_TX_CTL_QOS;
	}

	ieee80211_xmit_8023(sdata, sta, skb);
	
	rcu_read_unlock();
	
	return NETDEV_TX_OK;

fail:
	rcu_read_unlock();
	if (ret == NETDEV_TX_OK)
		atbm_dev_kfree_skb(skb);

	return ret;
}
/* config hooks */
#define IEEE80211_DHCP_TIMEOUT		(HZ/5)
static enum work_done_result ieee80211_dhcp_work_done(struct ieee80211_work *wk,
						  struct sk_buff *skb)
{
	atbm_printk_err("dhcp done");
	atbm_dev_kfree_skb(wk->dhcp.frame);
	return WORK_DONE_DESTROY;
}
static enum work_action __must_check
ieee80211_wk_dhcp_work(struct ieee80211_work *wk)
{
	struct sk_buff *skb;
	
	wk->dhcp.tries ++;
	
	if(wk->dhcp.tries >= wk->dhcp.retry_max){
		
		return WORK_ACT_TIMEOUT;
	}

	if(atomic_read(&wk->sdata->connectting) == 0){
		if(wk->dhcp.tries > 1){
			return WORK_ACT_TIMEOUT;
		}
	}
	
	skb = atbm_skb_copy(wk->dhcp.frame, GFP_KERNEL);
	if(skb){		
		wk->timeout = jiffies + IEEE80211_DHCP_TIMEOUT;
		ieee80211_subif_internal8023_start_xmit(wk->sdata,skb);
	}else {
		/*
		*skb alloc err,so wait a time retry
		*/
		wk->timeout = jiffies;
		wk->dhcp.tries--;
	}
	atbm_printk_always("dhcp work(%d)\n",wk->dhcp.tries);
	return WORK_ACT_NONE;
}
static int ieee80211_wk_start_dhcp_work(struct ieee80211_sub_if_data *sdata,struct sk_buff *skb)
{
	struct ieee80211_work *wk;

	wk = atbm_kzalloc(sizeof(struct ieee80211_work), GFP_ATOMIC);

	if(wk){
		
		wk->type = IEEE80211_WORK_DHCP;
		wk->sdata     = sdata;
		wk->done      = ieee80211_dhcp_work_done;
		wk->start     = ieee80211_wk_dhcp_work;
		wk->rx        = NULL;
		wk->filter_fc = 0;
		wk->dhcp.frame = skb;
		wk->dhcp.retry_max = 5;
		memcpy(wk->filter_bssid,sdata->vif.addr,6);	
		memcpy(wk->filter_sa,sdata->vif.addr,6);	
		ieee80211_add_work(wk);
		return 1;
	} 
	return 0;
}
static int ieee80211_subif_dhcp_cache(struct ieee80211_sub_if_data* sdata,struct sk_buff *dhcp_skb)
{
	#define IS_BOOTP_PORT(src_port,des_port) ((((src_port) == 67)&&((des_port) == 68)) || \
											   (((src_port) == 68)&&((des_port) == 67)))
	struct iphdr* iph;
	struct udphdr* udph;
	
	if(ieee80211_dhcp_running(sdata) == false){
		return 0;
	}
	
	if (dhcp_skb->protocol != htons(ETH_P_IP)) {
		return 0;
	}

	iph = ip_hdr(dhcp_skb);
	if (iph->protocol != IPPROTO_UDP)
		return 0; 

	udph = (struct udphdr*)((u8*)iph + (iph->ihl) * 4);
	if (!IS_BOOTP_PORT(ntohs(udph->source), ntohs(udph->dest)))
		return 0;
	
	
	return ieee80211_wk_start_dhcp_work(sdata,dhcp_skb);
	#undef IS_BOOTP_PORT
}
static int ieee80211_subif_cache_special(struct sk_buff *skb,struct net_device *dev)
{
	struct ieee80211_sub_if_data* sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int res;
	#define CALL_TXSPECAL(txh)			\
	do {				\
		res = txh(sdata,skb);		\
		if (res == 1)	\
			goto handle;  \
	} while (0)
#ifdef CONFIG_ATBM_SUPPORT_TSO
	CALL_TXSPECAL(ieee80211_tx_gso_checksum);
#endif
	CALL_TXSPECAL(ieee80211_subif_dhcp_cache);
	return 0;
handle:
	return 1;
}
netdev_tx_t ieee80211_subif_start_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	skb = ieee80211_alloc_xmit_skb(skb,dev);
	
	if(likely(skb) && ieee80211_subif_cache_special(skb,dev) == 0){
		return _ieee80211_subif_start_xmit(skb,dev);
	}

	return NETDEV_TX_OK;
}

void ieee80211_subif_internal8023_start_xmit(struct ieee80211_sub_if_data* sdata,struct sk_buff *skb)
{
	local_bh_disable();
	if(_ieee80211_subif_start_xmit(skb,sdata->dev) == NETDEV_TX_BUSY)
		atbm_dev_kfree_skb(skb);
	local_bh_enable();
}
/*
 * ieee80211_clear_tx_pending may not be called in a context where
 * it is possible that it packets could come in again.
 */
void ieee80211_clear_tx_pending(struct ieee80211_local *local)
{
	int i;

	for (i = 0; i < local->hw.queues; i++)
		atbm_skb_queue_purge(&local->pending[i]);
}

/*
 * Returns false if the frame couldn't be transmitted but was queued instead,
 * which in this case means re-queued -- take as an indication to stop sending
 * more pending frames.
 */
static bool ieee80211_tx_pending_skb(struct ieee80211_local *local,
				     struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta = NULL;
	struct ieee80211_hdr *hdr;
	bool result;

	sdata = vif_to_sdata(info->control.vif);

	if (info->flags & IEEE80211_TX_CTL_8023) {
		if (ieee80211_lookup_sta(sdata, skb, &sta)) {
			atbm_dev_kfree_skb(skb);
			return true;
		}
		if (sta && (sta->uploaded == false)) {
			atbm_dev_kfree_skb(skb);
			return true;
		}
		if(info->flags & IEEE80211_TX_INTFL_NEED_TXPROCESSING){
			result = ieee80211_tx_8023(sdata, skb, sta,true);;
		}else {
			result = __ieee80211_tx(sdata, skb, sta,true);
		}
	}else if (info->flags & IEEE80211_TX_INTFL_NEED_TXPROCESSING) {
		result = ieee80211_tx(sdata, skb, true);
	} else {
		hdr = (struct ieee80211_hdr *)skb->data;
		sta = sta_info_get(sdata, hdr->addr1);

		result = __ieee80211_tx(sdata, skb, sta, true);
	}

	return result;
}

/*
 * Transmit all pending packets. Called from tasklet.
 */
void ieee80211_tx_pending(unsigned long data)
{
	struct ieee80211_local *local = (struct ieee80211_local *)data;
	unsigned long flags;
	int i;
	bool txok;

	rcu_read_lock();

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	for (i = 0; i < local->hw.queues; i++) {
		/*
		 * If queue is stopped by something other than due to pending
		 * frames, or we have no pending frames, proceed to next queue.
		 */
		if (local->queue_stop_reasons[i] ||
		    atbm_skb_queue_empty(&local->pending[i]))
			continue;

		while (!atbm_skb_queue_empty(&local->pending[i])) {
			struct sk_buff *skb = __atbm_skb_dequeue(&local->pending[i]);
			struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

			if (WARN_ON(!info->control.vif)) {
				atbm_kfree_skb(skb);
				continue;
			}

			spin_unlock_irqrestore(&local->queue_stop_reason_lock,
						flags);
			txok = ieee80211_tx_pending_skb(local, skb);
			spin_lock_irqsave(&local->queue_stop_reason_lock,
					  flags);
			if (!txok)
				break;
		}

		if (atbm_skb_queue_empty(&local->pending[i]))
			ieee80211_propagate_queue_wake(local, i);
	}
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);

	rcu_read_unlock();
}

/* functions for drivers to get certain frames */

static void ieee80211_beacon_add_tim(struct ieee80211_if_ap *bss,
				     struct sk_buff *skb,
				     struct beacon_data *beacon)
{
	u8 *pos, *tim;
	int aid0 = 0;
	int /*i, have_bits = 0,*/ n1, n2;

#if 0
	/* Generate bitmap for TIM only if there are any STAs in power save
	 * mode. */
	if (atomic_read(&bss->num_sta_ps) > 0)
		/* in the hope that this is faster than
		 * checking byte-for-byte */
		have_bits = !bitmap_empty((unsigned long*)bss->tim,
					  IEEE80211_MAX_AID+1);
#endif
	if (bss->dtim_count == 0)
		bss->dtim_count = beacon->dtim_period - 1;
	else
		bss->dtim_count--;

	tim = pos = (u8 *) atbm_skb_put(skb, 6);
	*pos++ = ATBM_WLAN_EID_TIM;
	*pos++ = 4;
	*pos++ = bss->dtim_count;
	*pos++ = beacon->dtim_period;

	if (bss->dtim_count == 0 && !atbm_skb_queue_empty(&bss->ps_bc_buf))
		aid0 = 1;

	bss->dtim_bc_mc = aid0 == 1;

	if (1/*have_bits*/) {
		/* Find largest even number N1 so that bits numbered 1 through
		 * (N1 x 8) - 1 in the bitmap are 0 and number N2 so that bits
		 * (N2 + 1) x 8 through 2007 are 0. */
#if 0
		n1 = 0;
		for (i = 0; i < IEEE80211_MAX_TIM_LEN; i++) {
			if (bss->tim[i]) {
				n1 = i & 0xfe;
				break;
			}
		}
		n2 = n1;
		for (i = IEEE80211_MAX_TIM_LEN - 1; i >= n1; i--) {
			if (bss->tim[i]) {
				n2 = i;
				break;
			}
		}
#else
		n1 = 0; n2 = 2;
#endif
		/* Bitmap control */
		*pos++ = n1 | aid0;
		/* Part Virt Bitmap */
		memcpy(pos, bss->tim + n1, n2 - n1 + 1);

		tim[1] = n2 - n1 + 4;
		atbm_skb_put(skb, n2 - n1);
	} else {
		*pos++ = aid0; /* Bitmap control */
		*pos++ = 0; /* Part Virt Bitmap */
	}
}

struct sk_buff *ieee80211_beacon_get_tim(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 u16 *tim_offset, u16 *tim_length)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_channel_state *chan_state;
	struct sk_buff *skb = NULL;
	struct ieee80211_tx_info *info;
	struct ieee80211_sub_if_data *sdata = NULL;
	struct ieee80211_if_ap *ap = NULL;
	struct beacon_data *beacon;
	enum ieee80211_band band;
	rcu_read_lock();

	sdata = vif_to_sdata(vif);
	chan_state = ieee80211_get_channel_state(local, sdata);
	band = chan_state->conf.channel->band;
	if (!ieee80211_sdata_running(sdata))
		goto out;

	if (tim_offset)
		*tim_offset = 0;
	if (tim_length)
		*tim_length = 0;

	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		struct beacon_extra *extra;
#ifdef CONFIG_ATBM_HE_AP
		struct mlme_data *mlme;
#endif
		ap = &sdata->u.ap;
		extra = rcu_dereference(ap->beacon_extra);
		beacon = rcu_dereference(ap->beacon);
#ifdef CONFIG_ATBM_HE_AP
		mlme  = rcu_dereference(ap->mlme);
#endif
		if (beacon) {
			int beacon_size;

			beacon_size = local->tx_headroom+beacon->head_len +
					    beacon->tail_len + 256;
			if(extra)
				beacon_size += extra->beacon_extra_len;
#ifdef CONFIG_ATBM_HE_AP
			if(mlme)
				beacon_size += mlme->he_cap_len + mlme->he_op_len;
#endif
			/*
			 * headroom, head length,
			 * tail length and maximum TIM length
			 */
			skb = atbm_dev_alloc_skb(beacon_size);
			if (!skb)
				goto out;

			atbm_skb_reserve(skb, local->tx_headroom);
			memcpy(atbm_skb_put(skb, beacon->head_len), beacon->head,
			       beacon->head_len);

			/*
			 * Not very nice, but we want to allow the driver to call
			 * ieee80211_beacon_get() as a response to the set_tim()
			 * callback. That, however, is already invoked under the
			 * sta_lock to guarantee consistent and race-free update
			 * of the tim bitmap in mac80211 and the driver.
			 */
			if (local->tim_in_locked_section) {
				ieee80211_beacon_add_tim(ap, skb, beacon);
			} else {
				unsigned long flags;

				spin_lock_irqsave(&local->sta_lock, flags);
				ieee80211_beacon_add_tim(ap, skb, beacon);
				spin_unlock_irqrestore(&local->sta_lock, flags);
			}

			if (tim_offset)
				*tim_offset = beacon->head_len;
			if (tim_length)
				*tim_length = skb->len - beacon->head_len;

			if (beacon->tail)
				memcpy(atbm_skb_put(skb, beacon->tail_len),
				       beacon->tail, beacon->tail_len);
			if(extra&&extra->beacon_extra_len){
				memcpy(atbm_skb_put(skb, extra->beacon_extra_len),
				       extra->beacon_extra_ie, extra->beacon_extra_len);
			}
#ifdef CONFIG_ATBM_HE_AP
			if(mlme){
				if(mlme->he_cap_len)
					memcpy(atbm_skb_put(skb, mlme->he_cap_len),mlme->he_cap,mlme->he_cap_len);
				if(mlme->he_op_len)
					memcpy(atbm_skb_put(skb, mlme->he_op_len),mlme->he_op,mlme->he_op_len);
			}
#endif
		} else
			goto out;
	} 
#ifdef CONFIG_ATBM_SUPPORT_IBSS
	else if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		struct ieee80211_if_ibss *ifibss = &sdata->u.ibss;
		struct ieee80211_hdr *hdr;
		struct sk_buff *presp = rcu_dereference(ifibss->presp);

		if (!presp)
			goto out;

		skb = atbm_skb_copy(presp, GFP_ATOMIC);
		if (!skb)
			goto out;

		hdr = (struct ieee80211_hdr *) skb->data;
		hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						 IEEE80211_STYPE_BEACON);
	} 
#endif
#ifdef	CONFIG_MAC80211_ATBM_MESH
	else if (ieee80211_vif_is_mesh(&sdata->vif)) {
		struct atbm_ieee80211_mgmt *mgmt;
		u8 *pos;

#ifdef CONFIG_MAC80211_ATBM_MESH
		if (!sdata->u.mesh.mesh_id_len)
			goto out;
#endif

		/* headroom, head length, tail length and maximum TIM length */
		skb = atbm_dev_alloc_skb(local->tx_headroom + 400 +
				sdata->u.mesh.ie_len);
		if (!skb)
			goto out;

		atbm_skb_reserve(skb, local->hw.extra_tx_headroom);
		mgmt = (struct atbm_ieee80211_mgmt *)
			atbm_skb_put(skb, 24 + sizeof(mgmt->u.beacon));
		memset(mgmt, 0, 24 + sizeof(mgmt->u.beacon));
		mgmt->frame_control =
		    cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_BEACON);
		memset(mgmt->da, 0xff, ETH_ALEN);
		memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
		memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);
		mgmt->u.beacon.beacon_int =
			cpu_to_le16(sdata->vif.bss_conf.beacon_int);
		mgmt->u.beacon.capab_info |= cpu_to_le16(
			sdata->u.mesh.security ? WLAN_CAPABILITY_PRIVACY : 0);

		pos = atbm_skb_put(skb, 2);
		*pos++ = ATBM_WLAN_EID_SSID;
		*pos++ = 0x0;

		if (ieee80211_add_srates_ie(&sdata->vif, skb) ||
		    mesh_add_ds_params_ie(skb, sdata) ||
		    ieee80211_add_ext_srates_ie(&sdata->vif, skb) ||
		    mesh_add_rsn_ie(skb, sdata) ||
		    mesh_add_meshid_ie(skb, sdata) ||
		    mesh_add_meshconf_ie(skb, sdata) ||
		    mesh_add_vendor_ies(skb, sdata)) {
			pr_err("o11s: couldn't add ies!\n");
			goto out;
		}
	}
#endif
	else {
		WARN_ON(1);
		goto out;
	}

	info = IEEE80211_SKB_CB(skb);

	info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	info->flags |= IEEE80211_TX_CTL_NO_ACK;
	info->band = band;
	info->control.vif = vif;

	info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT |
			IEEE80211_TX_CTL_ASSIGN_SEQ |
			IEEE80211_TX_CTL_FIRST_FRAGMENT;
 out:
	rcu_read_unlock();
	return skb;
}
//EXPORT_SYMBOL(ieee80211_beacon_get_tim);

#ifdef ATBM_PROBE_RESP_EXTRA_IE
struct sk_buff *ieee80211_proberesp_get(struct ieee80211_hw *hw,
						   struct ieee80211_vif *vif)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct sk_buff *skb = NULL;
	struct ieee80211_channel_state *chan_state;
	struct ieee80211_tx_info *info;
	struct ieee80211_sub_if_data *sdata = NULL;
	struct ieee80211_if_ap *ap = NULL;
	struct proberesp_data *proberesp;
#ifdef CONFIG_ATBM_HE_AP
	struct mlme_data *mlme;
#endif
	enum ieee80211_band band;
	rcu_read_lock();

	sdata = vif_to_sdata(vif);
	chan_state = ieee80211_get_channel_state(local, sdata);
	band = chan_state->conf.channel->band;

	if (!ieee80211_sdata_running(sdata))
		goto out;

	if (sdata->vif.type == NL80211_IFTYPE_AP) {
		struct probe_response_extra *extra;
		ap = &sdata->u.ap;
		extra     = rcu_dereference(ap->probe_response_extra);
		proberesp = rcu_dereference(ap->proberesp);
#ifdef CONFIG_ATBM_HE_AP
		mlme  = rcu_dereference(ap->mlme);
#endif
		if (proberesp) {
			int proberesp_size;

			proberesp_size = local->tx_headroom +
					    proberesp->head_len + proberesp->proberesp_data_ies_len +
					    proberesp->tail_len;
			if(extra)
				proberesp_size += extra->probe_response_extra_len;
#ifdef CONFIG_ATBM_HE_AP
			if(mlme)
				proberesp_size += mlme->he_cap_len + mlme->he_op_len;
#endif

			/*
			 * headroom, head length,
			 * tail length and probe response ie length
			 */
			skb = atbm_dev_alloc_skb(proberesp_size);
			if (!skb)
				goto out;

			atbm_skb_reserve(skb, local->tx_headroom);
			memcpy(atbm_skb_put(skb, proberesp->head_len), proberesp->head,
			       proberesp->head_len);

			if (proberesp->tail)
				memcpy(atbm_skb_put(skb, proberesp->tail_len),
				       proberesp->tail, proberesp->tail_len);

			if (proberesp->proberesp_data_ies)
				memcpy(atbm_skb_put(skb, proberesp->proberesp_data_ies_len),
				       proberesp->proberesp_data_ies, proberesp->proberesp_data_ies_len);
			if(extra&&extra->probe_response_extra_ie)
				memcpy(atbm_skb_put(skb, extra->probe_response_extra_len),
				       extra->probe_response_extra_ie, extra->probe_response_extra_len);
#ifdef CONFIG_ATBM_HE_AP
			if(mlme){
				if(mlme->he_cap_len)
					memcpy(atbm_skb_put(skb, mlme->he_cap_len),mlme->he_cap,mlme->he_cap_len);
				if(mlme->he_op_len)
					memcpy(atbm_skb_put(skb, mlme->he_op_len),mlme->he_op,mlme->he_op_len);
			}
#endif
		} else
			goto out;
	}
	else if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		goto out;
	}
	else {
		
		WARN_ON(1);
		goto out;
	}

	info = IEEE80211_SKB_CB(skb);

	info->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	//info->flags |= IEEE80211_TX_CTL_NO_ACK;
	info->band = band;

	info->control.vif = vif;

	info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT |
			IEEE80211_TX_CTL_ASSIGN_SEQ |
			IEEE80211_TX_CTL_FIRST_FRAGMENT;
 out:
	rcu_read_unlock();
	return skb;
}
//EXPORT_SYMBOL(ieee80211_proberesp_get);
#endif
struct sk_buff *ieee80211_nullfunc_get(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif)
{
	struct ieee80211_hdr_3addr *nullfunc;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_managed *ifmgd;
	struct ieee80211_local *local;
	struct sk_buff *skb;

	if (WARN_ON(vif->type != NL80211_IFTYPE_STATION))
		return NULL;

	sdata = vif_to_sdata(vif);
	ifmgd = &sdata->u.mgd;
	local = sdata->local;

	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*nullfunc));
	if (!skb)
		return NULL;

	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = (struct ieee80211_hdr_3addr *) atbm_skb_put(skb,
							  sizeof(*nullfunc));
	memset(nullfunc, 0, sizeof(*nullfunc));
	nullfunc->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					      IEEE80211_STYPE_NULLFUNC |
					      IEEE80211_FCTL_TODS);
	memcpy(nullfunc->addr1, ifmgd->bssid, ETH_ALEN);
	memcpy(nullfunc->addr2, vif->addr, ETH_ALEN);
	memcpy(nullfunc->addr3, ifmgd->bssid, ETH_ALEN);

	return skb;
}
//EXPORT_SYMBOL(ieee80211_nullfunc_get);

struct sk_buff *ieee80211_qosnullfunc_get(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif)
{
	struct ieee80211_qos_hdr *nullfunc;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_if_managed *ifmgd;
	struct ieee80211_local *local;
	struct sk_buff *skb;

	if (WARN_ON(vif->type != NL80211_IFTYPE_STATION))
		return NULL;

	sdata = vif_to_sdata(vif);
	ifmgd = &sdata->u.mgd;
	local = sdata->local;

	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*nullfunc));
	if (!skb) {
		atbm_printk_err( "%s: failed to allocate buffer for qos "
		       "nullfunc template\n", sdata->name);
		return NULL;
	}
	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = (struct ieee80211_qos_hdr *) atbm_skb_put(skb,
							  sizeof(*nullfunc));
	memset(nullfunc, 0, sizeof(*nullfunc));
	nullfunc->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					      IEEE80211_STYPE_QOS_NULLFUNC |
					      IEEE80211_FCTL_TODS);
	memcpy(nullfunc->addr1, ifmgd->bssid, ETH_ALEN);
	memcpy(nullfunc->addr2, vif->addr, ETH_ALEN);
	memcpy(nullfunc->addr3, ifmgd->bssid, ETH_ALEN);

	return skb;
}
void ieee80211_send_htc_qosnullfunc(struct ieee80211_hw* hw, struct ieee80211_vif* vif, u32 htc)
{
	struct ieee80211_qos_hdr* nullfunc;
	struct ieee80211_sub_if_data* sdata;
	struct ieee80211_if_managed* ifmgd;
	struct ieee80211_local* local;
	struct sk_buff* skb;

	if (WARN_ON(vif->type != NL80211_IFTYPE_STATION))
		return ;

	sdata = vif_to_sdata(vif);
	ifmgd = &sdata->u.mgd;
	local = sdata->local;

	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*nullfunc) + 4);
	if (!skb) {
		atbm_printk_err("%s: failed to allocate buffer for qos "
			"nullfunc template\n", sdata->name);
		return ;
	}
	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);

	nullfunc = (struct ieee80211_qos_hdr*)atbm_skb_put(skb,
		sizeof(*nullfunc) + 4);
	memset(nullfunc, 0, sizeof(*nullfunc));
	nullfunc->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
		IEEE80211_STYPE_QOS_NULLFUNC |
		IEEE80211_FCTL_TODS |
		IEEE80211_FCTL_ORDER);
	memcpy(nullfunc->addr1, ifmgd->bssid, ETH_ALEN);
	memcpy(nullfunc->addr2, vif->addr, ETH_ALEN);
	memcpy(nullfunc->addr3, ifmgd->bssid, ETH_ALEN);

	memcpy(skb->data + sizeof(struct ieee80211_qos_hdr), &htc, 4);
	atbm_printk_err("QosNUll Htc(%x)\n", htc);
	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT | IEEE80211_TX_CTL_USE_MINRATE;
	ieee80211_tx_skb(sdata, skb);
}
//EXPORT_SYMBOL(ieee80211_qosnullfunc_get);

struct sk_buff *ieee80211_probereq_get(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const u8 *ssid, size_t ssid_len,
				       const u8 *ie, size_t ie_len,u8 *bssid)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local;
	struct ieee80211_hdr_3addr *hdr;
	struct sk_buff *skb;
	size_t ie_ssid_len;
	u8 *pos;
	struct probe_request_extra *extra = NULL;
	size_t extra_len = 0;
	u8 bssid_scan[6]={0xff,0xff,0xff,0xff,0xff,0xff};
	
	rcu_read_lock();
	
	sdata = vif_to_sdata(vif);
	local = sdata->local;
	ie_ssid_len = 2 + ssid_len;
	/*
	*if vif is in station mode try to add extra ie;
	*/
	if(ieee80211_sdata_running(sdata) && (vif->type == NL80211_IFTYPE_STATION)){
		extra = rcu_dereference(sdata->u.mgd.probe_request_extra);
	}
	
	if(extra)
		extra_len = extra->probe_request_extra_len;
	
	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*hdr) +
			    ie_ssid_len + ie_len + extra_len);
	if (!skb){
		goto out;
	}

	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);
	if(bssid)
		memcpy(bssid_scan,bssid,6);
	hdr = (struct ieee80211_hdr_3addr *) atbm_skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					 IEEE80211_STYPE_PROBE_REQ);
	memcpy(hdr->addr1, bssid_scan, ETH_ALEN);
	memcpy(hdr->addr2, vif->addr, ETH_ALEN);
	memcpy(hdr->addr3, bssid_scan, ETH_ALEN);
	pos = atbm_skb_put(skb, ie_ssid_len);
	*pos++ = ATBM_WLAN_EID_SSID;
	*pos++ = ssid_len;
	if (ssid)
		memcpy(pos, ssid, ssid_len);
	pos += ssid_len;

	if (ie) {
		pos = atbm_skb_put(skb, ie_len);
		memcpy(pos, ie, ie_len);
	}

	if(extra){
		pos = atbm_skb_put(skb, extra_len);
		memcpy(pos,extra->probe_request_extra_ie,extra_len);
	}
out:
	rcu_read_unlock();
	return skb;
}
//EXPORT_SYMBOL(ieee80211_probereq_get);

//EXPORT_SYMBOL(ieee80211_qosnullfunc_get);


struct sk_buff *ieee80211_probereq_get_etf(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const u8 *ssid, size_t ssid_len,
				       size_t total_len)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local;
	struct ieee80211_hdr_3addr *hdr;
	struct sk_buff *skb;
	size_t ie_ssid_len;
	int data_len;

	u8 *pos;

	data_len = total_len - sizeof(*hdr) -ssid_len;

	sdata = vif_to_sdata(vif);
	local = sdata->local;
	ie_ssid_len = 2 + ssid_len;

	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*hdr) +
			    ie_ssid_len + data_len+8);
	if (!skb)
		return NULL;

	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);

	hdr = (struct ieee80211_hdr_3addr *) atbm_skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					 IEEE80211_STYPE_PROBE_REQ);
	memset(hdr->addr1, 0xff, ETH_ALEN);
	memcpy(hdr->addr2, vif->addr, ETH_ALEN);
	memset(hdr->addr3, 0xff, ETH_ALEN);
	pos = atbm_skb_put(skb, ie_ssid_len);
	*pos++ = ATBM_WLAN_EID_SSID;
	*pos++ = ssid_len;
	if (ssid)
		memcpy(pos, ssid, ssid_len);
	pos += ssid_len;

	while(1)
	{
		pos = atbm_skb_put(skb, 8);
		*pos++ = 221;
		*pos++ = 6;
		pos += 6;
		data_len -=8;
		if(data_len<0)
			break;
	}

	return skb;
}


struct sk_buff *ieee80211_probereq_get_etf_v2(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const u8 *ssid, size_t ssid_len,
				       size_t total_len)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local;
	struct ieee80211_hdr_3addr *hdr;
	struct sk_buff *skb;
	size_t ie_ssid_len;
	int data_len;	
	struct ATBM_TEST_IE  *pAtbm_Ie;
			u8 out[3]=ATBM_OUI;

	u8 *pos;

	data_len =12*sizeof(struct ATBM_TEST_IE) ;

	sdata = vif_to_sdata(vif);
	local = sdata->local;
	ie_ssid_len = 2 + ssid_len;

	skb = atbm_dev_alloc_skb(2*(local->hw.extra_tx_headroom + sizeof(*hdr) +
			    ie_ssid_len + data_len));

	
	if (!skb)
		return NULL;
	
	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);

	hdr = (struct ieee80211_hdr_3addr *) atbm_skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					 IEEE80211_STYPE_PROBE_REQ);
	memset(hdr->addr1, 0xff, ETH_ALEN);
	memcpy(hdr->addr2, vif->addr, ETH_ALEN);
	memset(hdr->addr3, 0xff, ETH_ALEN);
	pos = atbm_skb_put(skb, ie_ssid_len);
	*pos++ = ATBM_WLAN_EID_SSID;
	*pos++ = ssid_len;
	if (ssid)
		memcpy(pos, ssid, ssid_len);
	pos += ssid_len;

	*pos++ = ATBM_WLAN_EID_SUPP_RATES;
	*pos++ = 7;//len
	*pos++ = 2;	
	*pos++ = 4;	
	*pos++ = 11;	
	*pos++ = 22;	
	*pos++ = 12;	
	*pos++ = 24;	
	*pos++ = 48;

	*pos++ = ATBM_WLAN_EID_EXT_SUPP_RATES;
	*pos++ = 5;//len
	*pos++ = 18;	
	*pos++ = 36;	
	*pos++ = 72;	
	*pos++ = 96;	
	*pos++ = 108;	
	

	pos = atbm_skb_put(skb, 7+5+4);


	while(1)
	{		

		pos = atbm_skb_put(skb, sizeof(struct ATBM_TEST_IE));
		//*pos++ = 221;
		//*pos++ = 6;
		pAtbm_Ie=(struct ATBM_TEST_IE  *)pos;
			
		pAtbm_Ie->ie_id = D11_WIFI_ELT_ID;
		pAtbm_Ie->len = sizeof(struct ATBM_TEST_IE)-2;
		memcpy(pAtbm_Ie->oui, out,3);
		pAtbm_Ie->oui_type = WIFI_ATBM_IE_OUI_TYPE;
		pAtbm_Ie->test_type = TXRX_TEST_REQ;
#ifdef ATBM_PRODUCT_TEST_USE_FEATURE_ID
		pAtbm_Ie->featureid = etf_config.featureid;
#endif
		pAtbm_Ie->result[0] = etf_config.rssifilter;
		pAtbm_Ie->result[1] = etf_config.rxevm;
		pos +=sizeof(struct ATBM_TEST_IE);


		
		data_len -=sizeof(struct ATBM_TEST_IE);
		if(data_len<=0)
			break;
	}
	

	//frame_hexdump("etf probe req", hdr,skb->len);
	return skb;
}
#ifdef CONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
struct sk_buff *ieee80211_probereq_get_etf_for_send_result(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       const u8 *ssid, size_t ssid_len,
				       size_t total_len)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local;
	struct ieee80211_hdr_3addr *hdr;
	struct sk_buff *skb;
	size_t ie_ssid_len;
	int data_len;	
	struct ATBM_TEST_IE  *pAtbm_Ie;
			u8 out[3]=ATBM_OUI;

	u8 *pos;

	data_len = 8*sizeof(struct ATBM_TEST_IE) ;

	sdata = vif_to_sdata(vif);
	local = sdata->local;
	ie_ssid_len = 2 + ssid_len;

	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom + sizeof(*hdr) +
			    ie_ssid_len + data_len);

	
	if (!skb)
		return NULL;
	
	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);

	hdr = (struct ieee80211_hdr_3addr *) atbm_skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					 IEEE80211_STYPE_PROBE_REQ);
	memset(hdr->addr1, 0xff, ETH_ALEN);
	memcpy(hdr->addr2, vif->addr, ETH_ALEN);
	memset(hdr->addr3, 0xff, ETH_ALEN);
	pos = atbm_skb_put(skb, ie_ssid_len);
	*pos++ = ATBM_WLAN_EID_SSID;
	*pos++ = ssid_len;
	if (ssid)
		memcpy(pos, ssid, ssid_len);
	pos += ssid_len;

	*pos++ = ATBM_WLAN_EID_SUPP_RATES;
	*pos++ = 7;//len
	*pos++ = 2;	
	*pos++ = 4;	
	*pos++ = 11;	
	*pos++ = 22;	
	*pos++ = 12;	
	*pos++ = 24;	
	*pos++ = 48;

	*pos++ = ATBM_WLAN_EID_EXT_SUPP_RATES;
	*pos++ = 5;//len
	*pos++ = 18;	
	*pos++ = 36;	
	*pos++ = 72;	
	*pos++ = 96;	
	*pos++ = 108;	
	

	pos = atbm_skb_put(skb, 7+5+4);


	while(1)
	{		

		pos = atbm_skb_put(skb, sizeof(struct ATBM_TEST_IE));
		//*pos++ = 221;
		//*pos++ = 6;
		pAtbm_Ie=(struct ATBM_TEST_IE  *)pos;
			
		pAtbm_Ie->ie_id = D11_WIFI_ELT_ID;
		pAtbm_Ie->len = sizeof(struct ATBM_TEST_IE)-2;
		memcpy(pAtbm_Ie->oui, out,3);
		pAtbm_Ie->oui_type = WIFI_ATBM_IE_OUI_TYPE;
		pAtbm_Ie->test_type = TXRX_TEST_RESULT;
#ifdef ATBM_PRODUCT_TEST_USE_FEATURE_ID
		pAtbm_Ie->featureid = etf_config.featureid;
#endif
		if(Atbm_Test_Success == 1){
			pAtbm_Ie->resverd = TXRX_TEST_PASS;
		}
		else if(Atbm_Test_Success == -1)
		{
			pAtbm_Ie->resverd = TXRX_TEST_FAIL;
		}
		
		pos +=sizeof(struct ATBM_TEST_IE);


		
		data_len -=sizeof(struct ATBM_TEST_IE);
		if(data_len<=0)
			break;
	}

	//frame_hexdump("etf probe req", hdr,skb->len);
	return skb;
}
#endif
void ieee80211_tx_skb(struct ieee80211_sub_if_data *sdata, struct sk_buff *skb)
{
	atbm_skb_set_mac_header(skb, 0);
	atbm_skb_set_network_header(skb, 0);
	atbm_skb_set_transport_header(skb, 0);

	/* Send all internal mgmt frames on VO. Accordingly set TID to 7. */
	atbm_skb_set_queue_mapping(skb, IEEE80211_AC_VO);
	skb->priority = 7;

	/*
	 * The other path calling ieee80211_xmit is from the tasklet,
	 * and while we can handle concurrent transmissions locking
	 * requirements are that we do not come into tx with bhs on.
	 */
	local_bh_disable();
	ieee80211_xmit(sdata, skb);
	local_bh_enable();
}

bool ieee80211_tx_multicast_deauthen(struct ieee80211_sub_if_data *sdata)
{
	struct sk_buff *skb;
	struct atbm_ieee80211_mgmt *mgmt;
	struct ieee80211_local *local = sdata->local;
	u8 multicast_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
	
	if(sdata->vif.type != NL80211_IFTYPE_AP){
		return false;
	}

	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom +sizeof(*mgmt) + 6);
	if (!skb){
		return false;
	}

	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);
	atbm_printk_mgmt( "%s:[%pM] send deauthen\n",sdata->name,sdata->vif.addr);
	mgmt = (struct atbm_ieee80211_mgmt *) atbm_skb_put(skb, sizeof(struct atbm_ieee80211_mgmt));
	memset(mgmt, 0, 24 + 6);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_DEAUTH|IEEE80211_FCTL_FROMDS);
	memcpy(mgmt->da, multicast_addr, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);
	mgmt->u.deauth.reason_code = WLAN_REASON_DEAUTH_LEAVING;

	IEEE80211_SKB_CB(skb)->flags = IEEE80211_TX_INTFL_DONT_ENCRYPT | IEEE80211_TX_CTL_USE_MINRATE;

	ieee80211_tx_skb(sdata,skb);

	return true;
}

bool ieee80211_tx_sta_deauthen(struct sta_info *sta)
{
	struct sk_buff *skb;
	struct atbm_ieee80211_mgmt *mgmt;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	
	if(sdata->vif.type != NL80211_IFTYPE_AP){
		return false;
	}

	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom +sizeof(*mgmt) + 6);
	if (!skb){
		return false;
	}

	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);
	atbm_printk_mgmt( "%s:[%pM] send deauthen\n",sdata->name,sta->sta.addr);
	mgmt = (struct atbm_ieee80211_mgmt *) atbm_skb_put(skb, sizeof(struct atbm_ieee80211_mgmt));
	memset(mgmt, 0, 24 + 6);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_DEAUTH|IEEE80211_FCTL_FROMDS);
	memcpy(mgmt->da, sta->sta.addr, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);
	mgmt->u.deauth.reason_code = WLAN_REASON_DEAUTH_LEAVING;

	IEEE80211_SKB_CB(skb)->flags = IEEE80211_TX_INTFL_DONT_ENCRYPT | IEEE80211_TX_CTL_USE_MINRATE;

	ieee80211_tx_skb(sdata,skb);

	return true;
}


