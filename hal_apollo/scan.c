/*
 * Scan implementation for altobeam APOLLO mac80211 drivers
 *
 * Copyright (c) 2016, altobeam
 *
 * Based on:
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/sched.h>
#include "apollo.h"
#include "scan.h"
#include "sta.h"
#include "pm.h"
#include "bh.h"
#include "atbm_p2p.h"
#ifdef CONFIG_ATBM_SCAN_SPLIT
#define ATBM_SPLIT_SCAN_MAX_CHANNEL			2
#define ATBM_SPLIT_NEXT_CHANNEL_TIME	(500)
#endif
#ifdef ATBM_SUPPORT_SMARTCONFIG
extern int smartconfig_magic_scan_done(struct atbm_common *hw_priv);
#endif
//#ifdef CONFIG_WIRELESS_EXT
extern int etf_v2_scan_end(struct atbm_common *hw_priv, struct ieee80211_vif *vif );
extern void etf_v2_scan_rx(struct atbm_common *hw_priv,struct sk_buff *skb,u8 rssi );
//#endif
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
static int atbm_advance_scan_start(struct atbm_common *hw_priv)
{
	int tmo = 0;
	tmo += hw_priv->advanceScanElems.duration;
	#ifdef CONFIG_PM
	atbm_pm_stay_awake(&hw_priv->pm_state, tmo * HZ / 1000);
	#endif
	/* Invoke Advance Scan Duration Timeout Handler */
	atbm_hw_priv_queue_delayed_work(hw_priv,
		&hw_priv->advance_scan_timeout, tmo * HZ / 1000);
	return 0;
}
#endif
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
static int atbm_disable_filtering(struct atbm_vif *priv)
{
	int ret = 0;
	bool bssid_filtering = 0;
	struct wsm_rx_filter rx_filter;
	struct wsm_beacon_filter_control bf_control;
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);

	/* RX Filter Disable */
	rx_filter.promiscuous = 0;
	rx_filter.bssid = 0;
	rx_filter.fcs = 0;
	rx_filter.probeResponder = 0;
	rx_filter.keepalive = 0;
	ret = wsm_set_rx_filter(hw_priv, &rx_filter,
			priv->if_id);

	/* Beacon Filter Disable */
	bf_control.enabled = 0;
	bf_control.bcn_count = 1;
	if (!ret)
		ret = wsm_beacon_filter_control(hw_priv, &bf_control,
					priv->if_id);

	/* BSSID Filter Disable */
	if (!ret)
		ret = wsm_set_bssid_filtering(hw_priv, bssid_filtering,
					 priv->if_id);

	return ret;
}

#endif

#ifdef CONFIG_ATBM_SCAN_SPLIT
void atbm_scan_split_work(struct atbm_work_struct *work)
{
	struct atbm_common *hw_priv =
		container_of(work, struct atbm_common, scan.scan_spilt.work);

	wsm_lock_tx_async(hw_priv);
	atbm_scan_work(&hw_priv->scan.work);
}
#endif

static bool atbm_scan_split_running(struct atbm_common *hw_priv)
{
#ifdef CONFIG_ATBM_SCAN_SPLIT
	if(hw_priv->scan.curr == hw_priv->scan.end)
		return false;
	if(hw_priv->scan.split == 0)
		return false;
	
	wsm_unlock_tx(hw_priv);
	atbm_printk_debug("%s:wait next scan\n",__func__);
	atbm_hw_priv_queue_delayed_work(hw_priv, &hw_priv->scan.scan_spilt,msecs_to_jiffies(ATBM_SPLIT_NEXT_CHANNEL_TIME));

	return true;
#else
	BUG_ON(hw_priv == NULL);
	return false;
#endif
}

static int atbm_scan_start(struct atbm_vif *priv, struct wsm_scan *scan)
{
	int ret, i;
	int tmo = 5000;
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);

	for (i = 0; i < scan->numOfChannels; ++i)
		tmo += scan->ch[i].maxChannelTime + 10;

	atomic_set(&hw_priv->scan.in_progress, 1);
	atomic_set(&hw_priv->recent_scan, 1);
#ifndef CONFIG_WAKELOCK
#ifdef CONFIG_PM
	atbm_pm_stay_awake(&hw_priv->pm_state, tmo * HZ / 1000);
#endif
#endif
	atbm_hw_priv_queue_delayed_work(hw_priv, &hw_priv->scan.timeout,
		tmo * HZ / 1000);
	hw_priv->scan.wait_complete = 1;

	ret = wsm_scan(hw_priv, scan, priv->if_id);
	
	if (unlikely(ret)) {
		hw_priv->scan.wait_complete = 0;
		atomic_set(&hw_priv->scan.in_progress, 0);
		atbm_hw_cancel_delayed_work(&hw_priv->scan.timeout,true);
//		atbm_scan_restart_delayed(priv);
	}
	return ret;
}
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
static int atbm_sched_scan_start(struct atbm_vif *priv, struct wsm_scan *scan)
{
	int ret;
	struct atbm_common *hw_priv = ABwifi_vifpriv_to_hwpriv(priv);

	ret = wsm_scan(hw_priv, scan, priv->if_id);
	if (unlikely(ret)) {
		atomic_set(&hw_priv->scan.in_progress, 0);
	}
	return ret;
}
#endif /*ROAM_OFFLOAD*/
#endif
int atbm_hw_scan(struct ieee80211_hw *hw,
		   struct ieee80211_vif *vif,
		   struct ieee80211_scan_req_wrap *req_wrap)
{
	struct atbm_common *hw_priv = hw->priv;
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PROBE_REQUEST,
	};
	int i;
	int ret = 0;
#ifdef CONFIG_ATBM_SUPPORT_P2P
	int roc_if_id = 0;
#endif
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
	u16 advance_scan_req_channel;
#endif

	if(atbm_bh_is_term(hw_priv)){
		return -EOPNOTSUPP;
	}

	if(atomic_read(&priv->enabled) == 0){
		atbm_printk_err("%s:disabled\n",__func__);
		return -EOPNOTSUPP;
	}
	/* Scan when P2P_GO corrupt firmware MiniAP mode */
	if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP)
	{
		return -EOPNOTSUPP;
	}
#ifdef CONFIG_ATBM_SUPPORT_P2P
	mutex_lock(&hw_priv->conf_mutex);
	roc_if_id = hw_priv->roc_if_id;
	mutex_unlock(&hw_priv->conf_mutex);

	if (roc_if_id != -1) {
		atbm_printk_err( "[SCAN] Offchannel work pending,ignoring scan work %d\n",hw_priv->roc_if_id);
		return -EBUSY;
	}
#endif

	if (req_wrap->req->n_ssids == 1 && !req_wrap->req->ssids[0].ssid_len)
		req_wrap->req->n_ssids = 0;

	atbm_printk_debug("[SCAN] Scan request for %d SSIDs.\n",req_wrap->req->n_ssids);

	if (req_wrap->req->n_ssids > hw->wiphy->max_scan_ssids)
	{
		return -EINVAL;
	}

	frame.skb = ieee80211_probereq_get(hw, vif, NULL, 0,
		req_wrap->req->ie, req_wrap->req->ie_len,
		req_wrap->flags & IEEE80211_SCAN_REQ_NEED_BSSID ? req_wrap->bssid:NULL);
	if (!frame.skb)
		return -ENOMEM;
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
	if (priv->join_status != ATBM_APOLLO_JOIN_STATUS_STA) {
		if (req_wrap->req->channels[0]->band == NL80211_BAND_2GHZ)
			hw_priv->num_scanchannels = 0;
		else
			hw_priv->num_scanchannels = hw_priv->num_2g_channels;
		
		for (i=0; i < req_wrap->req->n_channels; i++) {
			hw_priv->scan_channels[hw_priv->num_scanchannels + i].number = \
				channel_hw_value(req_wrap->req->channels[i]);
			#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,11,0))
			if (req_wrap->req->channels[i]->flags & IEEE80211_CHAN_PASSIVE_SCAN) 
			#else
			if (req_wrap->req->channels[i]->flags &IEEE80211_CHAN_NO_IR)
			#endif
			{
				hw_priv->scan_channels[hw_priv->num_scanchannels + i].minChannelTime = 50;
				hw_priv->scan_channels[hw_priv->num_scanchannels + i].maxChannelTime = 110;
			}
			else {
				hw_priv->scan_channels[hw_priv->num_scanchannels + i].minChannelTime = 10;
				hw_priv->scan_channels[hw_priv->num_scanchannels + i].maxChannelTime = 40;
				hw_priv->scan_channels[hw_priv->num_scanchannels + i].number |= \
					ATBM_APOLLO_SCAN_TYPE_ACTIVE;
			}
			hw_priv->scan_channels[hw_priv->num_scanchannels + i].txPowerLevel = \
				req_wrap->req->channels[i]->max_power;
			if (req_wrap->req->channels[0]->band == NL80211_BAND_5GHZ)
				hw_priv->scan_channels[hw_priv->num_scanchannels + i].number |= \
					ATBM_APOLLO_SCAN_BAND_5G;
		}
		if (req_wrap->req->channels[0]->band == NL80211_BAND_2GHZ)
			hw_priv->num_2g_channels = req_wrap->req->n_channels;
		else
			hw_priv->num_5g_channels = req_wrap->req->n_channels;
	}
	hw_priv->num_scanchannels = hw_priv->num_2g_channels + hw_priv->num_5g_channels;
#endif /*ROAM_OFFLOAD*/
#endif
	/*
	*supplicant requres scan,we must stay awake.
	*/
	atbm_hold_suspend(hw_priv);
	/* will be unlocked in atbm_scan_work() */
	down(&hw_priv->scan.lock);
	mutex_lock(&hw_priv->conf_mutex);
	atbm_printk_scan("%s:if_id(%d)\n",__func__,priv->if_id);
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
	/* Active Scan - Serving Channel Request Handling */
	advance_scan_req_channel = channel_hw_value(req_wrap->req->channels[0]);
	if (hw_priv->enable_advance_scan &&
		(hw_priv->advanceScanElems.scanMode ==
			ATBM_APOLLO_SCAN_MEASUREMENT_ACTIVE) &&
		(priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA) &&
		(channel_hw_value(hw_priv->channel) == advance_scan_req_channel)) {
		BUG_ON(hw_priv->scan.req);
		/* wsm_lock_tx(hw_priv); */
		wsm_vif_lock_tx(priv);
		hw_priv->scan.if_id = priv->if_id;
		/* Disable Power Save */
		if (priv->powersave_mode.pmMode & WSM_PSM_PS) {
			struct wsm_set_pm pm = priv->powersave_mode;
			pm.pmMode = WSM_PSM_ACTIVE;
			wsm_set_pm(hw_priv, &pm, priv->if_id);
		}
		/* Disable Rx Beacon and Bssid filter */
		ret = atbm_disable_filtering(priv);
		if (ret)
			atbm_printk_warn("%s: Disable BSSID or Beacon filtering failed: %d.\n",
			__func__, ret);
		wsm_unlock_tx(hw_priv);
		mutex_unlock(&hw_priv->conf_mutex);
		/* Transmit Probe Request with Broadcast SSID */
		atbm_tx(hw, frame.skb);
		/* Start Advance Scan Timer */
		atbm_advance_scan_start(hw_priv);
	} else {
#endif
		if (frame.skb) {
			ret = wsm_set_template_frame(hw_priv, &frame,
					priv->if_id);
			if (ret) {
				mutex_unlock(&hw_priv->conf_mutex);
				up(&hw_priv->scan.lock);
				atbm_dev_kfree_skb(frame.skb);
				atbm_release_suspend(hw_priv);
				return ret;
			}
			priv->tmpframe_probereq_set = 1;
		}

//		wsm_vif_lock_tx(priv);
		/*
		*must make sure that there are no pkgs in the lmc.
		*/
		wsm_lock_tx_async(hw_priv);
		wsm_flush_tx(hw_priv);
		
		BUG_ON(hw_priv->scan.req);
		hw_priv->scan.req = req_wrap->req;
		hw_priv->scan.req_wrap = req_wrap;
		hw_priv->scan.n_ssids = 0;
		hw_priv->scan.status = 0;
		hw_priv->scan.begin = &req_wrap->req->channels[0];
		hw_priv->scan.curr = hw_priv->scan.begin;
		hw_priv->scan.end = &req_wrap->req->channels[req_wrap->req->n_channels];
		hw_priv->scan.output_power = hw_priv->output_power;
		hw_priv->scan.if_id = priv->if_id;
		hw_priv->scan.passive = !!(req_wrap->flags & IEEE80211_SCAN_REQ_PASSIVE_SCAN);
		hw_priv->scan.cca = !!(req_wrap->flags & IEEE80211_SCAN_REQ_CCA);
#ifdef CONFIG_ATBM_SCAN_SPLIT
		hw_priv->scan.split = !!(req_wrap->flags & IEEE80211_SCAN_REQ_SPILT);
#endif
		/* TODO:COMBO: Populate BIT4 in scanflags to decide on which MAC
		 * address the SCAN request will be sent */

		for (i = 0; i < req_wrap->req->n_ssids; ++i) {
			struct wsm_ssid *dst =
				&hw_priv->scan.ssids[hw_priv->scan.n_ssids];
			BUG_ON(req_wrap->req->ssids[i].ssid_len > sizeof(dst->ssid));
			memcpy(&dst->ssid[0], req_wrap->req->ssids[i].ssid,
				sizeof(dst->ssid));
			dst->length = req_wrap->req->ssids[i].ssid_len;
			++hw_priv->scan.n_ssids;
		}

		mutex_unlock(&hw_priv->conf_mutex);

		if (frame.skb)
			atbm_dev_kfree_skb(frame.skb);
		atbm_printk_scan("%s:scan, delay suspend\n",__func__);
		atbm_hw_priv_queue_work(hw_priv, &hw_priv->scan.work);

#ifdef CONFIG_ATBM_APOLLO_TESTMODE
	}
#endif
	return 0;
}
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
int atbm_hw_sched_scan_start(struct ieee80211_hw *hw,
		   struct ieee80211_vif *vif,
		   struct cfg80211_sched_scan_request *req,
		   struct ieee80211_sched_scan_ies *ies)
{
	struct atbm_common *hw_priv = hw->priv;
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);
	struct wsm_template_frame frame = {
		.frame_type = WSM_FRAME_TYPE_PROBE_REQUEST,
	};
	int i;

	atbm_printk_warn("[SCAN] Scheduled scan request-->.\n");

	if (!priv->vif)
		return -EINVAL;

	/* Scan when P2P_GO corrupt firmware MiniAP mode */
	if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP)
		return -EOPNOTSUPP;

	atbm_printk_warn("[SCAN] Scheduled scan: n_ssids %d, ssid[0].len = %d\n", req->n_ssids, req->ssids[0].ssid_len);
	if (req->n_ssids == 1 && !req->ssids[0].ssid_len)
		req->n_ssids = 0;

	atbm_printk_debug("[SCAN] Scan request for %d SSIDs.\n",req->n_ssids);

	if (req->n_ssids > hw->wiphy->max_scan_ssids)
		return -EINVAL;

	frame.skb = ieee80211_probereq_get(hw, priv->vif, NULL, 0,
		ies->ie[0], ies->len[0],NULL);
	if (!frame.skb)
		return -ENOMEM;

	/* will be unlocked in atbm_scan_work() */
	down(&hw_priv->scan.lock);
	mutex_lock(&hw_priv->conf_mutex);
	if (frame.skb) {
		int ret;
#ifdef CONFIG_ATBM_SUPPORT_P2P
		if (priv->if_id == 0)
			atbm_remove_wps_p2p_ie(&frame);
#endif
		ret = wsm_set_template_frame(hw_priv, &frame, priv->if_id);
		if (0 == ret) {
			priv->tmpframe_probereq_set = 1;
			/* Host want to be the probe responder. */
			ret = wsm_set_probe_responder(priv, true);
		}
		if (ret) {
			mutex_unlock(&hw_priv->conf_mutex);
			up(&hw_priv->scan.lock);
			atbm_dev_kfree_skb(frame.skb);
			return ret;
		}
	}

	wsm_lock_tx(hw_priv);

	BUG_ON(hw_priv->scan.req);
	hw_priv->scan.sched_req = req;
	hw_priv->scan.n_ssids = 0;
	hw_priv->scan.status = 0;
	hw_priv->scan.begin = &req->channels[0];
	hw_priv->scan.curr = hw_priv->scan.begin;
	hw_priv->scan.end = &req->channels[req->n_channels];
	hw_priv->scan.output_power = hw_priv->output_power;

	for (i = 0; i < req->n_ssids; ++i) {
		struct wsm_ssid *dst =
			&hw_priv->scan.ssids[hw_priv->scan.n_ssids];
		BUG_ON(req->ssids[i].ssid_len > sizeof(dst->ssid));
		memcpy(&dst->ssid[0], req->ssids[i].ssid,
			sizeof(dst->ssid));
		dst->length = req->ssids[i].ssid_len;
		++hw_priv->scan.n_ssids;
		{
			u8 j;
			atbm_printk_warn("[SCAN] SSID %d\n",i);
			for(j=0; j<req->ssids[i].ssid_len; j++)
				atbm_printk_warn("[SCAN] 0x%x\n", req->ssids[i].ssid[j]);
		}
	}

	mutex_unlock(&hw_priv->conf_mutex);

	if (frame.skb)
		atbm_dev_kfree_skb(frame.skb);
	atbm_hw_priv_queue_work(hw_priv, &hw_priv->scan.swork);
	atbm_printk_warn("<--[SCAN] Scheduled scan request.\n");
	return 0;
}
#endif /*ROAM_OFFLOAD*/
#endif
void atbm_scan_work(struct atbm_work_struct *work)
{
	struct atbm_common *hw_priv = container_of(work,
						struct atbm_common,
						scan.work);
	struct atbm_vif *priv;
	struct ieee80211_channel **it;
	struct wsm_scan scan = {
		.scanType = WSM_SCAN_TYPE_FOREGROUND,
		.scanFlags = 0, /* TODO:COMBO */
		//.scanFlags = WSM_SCAN_FLAG_SPLIT_METHOD, /* TODO:COMBO */
	};
	bool first_run;
	int i;
	u32 ProbeRequestTime  = 10;
	u32 ChannelRemainTime = 20;
	u32 maxChannelTime;
	int scan_status = 0;
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
	int ret = 0;
	u16 advance_scan_req_channel = channel_hw_value(hw_priv->scan.begin[0]);
#endif

	
	priv = __ABwifi_hwpriv_to_vifpriv(hw_priv, hw_priv->scan.if_id);

	/*TODO: COMBO: introduce locking so vif is not removed in meanwhile */

    if (!priv) {
        goto vif_err;
    }

	if (priv->if_id)
		scan.scanFlags |= WSM_FLAG_MAC_INSTANCE_1;
	else
		scan.scanFlags &= ~WSM_FLAG_MAC_INSTANCE_1;

	first_run = hw_priv->scan.begin == hw_priv->scan.curr &&
			hw_priv->scan.begin != hw_priv->scan.end;

	mutex_lock(&hw_priv->conf_mutex);

	if (first_run) {
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		/* Passive Scan - Serving Channel Request Handling */
		if (hw_priv->enable_advance_scan &&
			(hw_priv->advanceScanElems.scanMode ==
				ATBM_APOLLO_SCAN_MEASUREMENT_PASSIVE) &&
			(priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA) &&
			(channel_hw_value(hw_priv->channel) ==
				advance_scan_req_channel)) {
			/* If Advance Scan Request is for Serving Channel Device
			 * should be Active and Filtering Should be Disable */
			if (priv->powersave_mode.pmMode & WSM_PSM_PS) {
				struct wsm_set_pm pm = priv->powersave_mode;
				pm.pmMode = WSM_PSM_ACTIVE;
				wsm_set_pm(hw_priv, &pm, priv->if_id);
			}
			/* Disable Rx Beacon and Bssid filter */
			ret = atbm_disable_filtering(priv);
			if (ret)
				atbm_printk_warn("%s: Disable BSSID or Beacon filtering failed: %d.\n",
				__func__, ret);
		} else if (hw_priv->enable_advance_scan &&
			(hw_priv->advanceScanElems.scanMode ==
				ATBM_APOLLO_SCAN_MEASUREMENT_PASSIVE) &&
			(priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA)) {
				if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA &&
					!(priv->powersave_mode.pmMode & WSM_PSM_PS)) {
					struct wsm_set_pm pm = priv->powersave_mode;
					pm.pmMode = WSM_PSM_PS;
					atbm_set_pm(priv, &pm);
				}
		} else {
#endif
			if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_MONITOR) {
				/* FW bug: driver has to restart p2p-dev mode
				 * after scan */
#if defined(CONFIG_ATBM_STA_LISTEN) || defined(CONFIG_ATBM_SUPPORT_P2P)
				atbm_disable_listening(priv);
#endif
			}
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		}
#endif
	}

	if (!hw_priv->scan.req || (hw_priv->scan.curr == hw_priv->scan.end)) {
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		if (hw_priv->enable_advance_scan &&
			(hw_priv->advanceScanElems.scanMode ==
				ATBM_APOLLO_SCAN_MEASUREMENT_PASSIVE) &&
			(priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA) &&
			(channel_hw_value(hw_priv->channel) ==
				advance_scan_req_channel)) {
			/* WSM Lock should be held here for WSM APIs */
			wsm_vif_lock_tx(priv);
			/* wsm_lock_tx(priv); */
			/* Once Duration is Over, enable filtering
			 * and Revert Back Power Save */
			if (priv->powersave_mode.pmMode & WSM_PSM_PS)
				wsm_set_pm(hw_priv, &priv->powersave_mode,
					priv->if_id);
			atbm_update_filtering(priv);
		}
#endif
		if (hw_priv->scan.status < 0)
			atbm_printk_debug("[SCAN] Scan failed (%d).\n",hw_priv->scan.status);
		else if (hw_priv->scan.req)
			atbm_printk_debug("[SCAN] Scan completed.\n");
		else
			atbm_printk_debug("[SCAN] Scan canceled.\n");

		hw_priv->scan.req = NULL;
		hw_priv->scan.cca = 0;
		hw_priv->scan.req_wrap = NULL;
		atbm_printk_scan("%s:end(%d)\n",__func__,hw_priv->scan.if_id);
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		hw_priv->enable_advance_scan = false;
#endif /* CONFIG_ATBM_APOLLO_TESTMODE */
		wsm_unlock_tx(hw_priv);
		mutex_unlock(&hw_priv->conf_mutex);
		ieee80211_scan_completed(hw_priv->hw,
					 hw_priv->scan.status ? 1 : 0);
		up(&hw_priv->scan.lock);
		atbm_release_suspend(hw_priv);
		return;
	} else {
		struct ieee80211_channel *first = *hw_priv->scan.curr;
		for (it = hw_priv->scan.curr + 1, i = 1;
		     it != hw_priv->scan.end &&
				i < WSM_SCAN_MAX_NUM_OF_CHANNELS;
		     ++it, ++i) {
			 	
			
			if ((*it)->band != first->band)
				break;
			
#ifdef CONFIG_ATBM_SCAN_SPLIT
			if((hw_priv->scan.split == 1) && (i >= ATBM_SPLIT_SCAN_MAX_CHANNEL))
				break;
#endif
#ifdef WIFI_ALLIANCE_CERTIF
			if (((*it)->flags ^ first->flags) &  
			#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,11,0))
				    IEEE80211_CHAN_PASSIVE_SCAN
		       #else
				    IEEE80211_CHAN_NO_IR
		       #endif
			   )
				break;  

			if (!(first->flags & 
			#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,11,0))
				IEEE80211_CHAN_PASSIVE_SCAN
			#else
				IEEE80211_CHAN_NO_IR
			#endif
				) &&
			    (*it)->max_power != first->max_power)
				break;
#endif //WIFI_ALLIANCE_CERTIF
		}
		scan.band = first->band;
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
		if (hw_priv->scan.req->no_cck)
			scan.maxTransmitRate = WSM_TRANSMIT_RATE_6;
		else
		#endif
			scan.maxTransmitRate = WSM_TRANSMIT_RATE_1;
		
		if (priv->if_id&&priv->vif->p2p){
			scan.maxTransmitRate = WSM_TRANSMIT_RATE_6;
		}

		
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		if (hw_priv->enable_advance_scan) {
			if (hw_priv->advanceScanElems.scanMode ==
				ATBM_APOLLO_SCAN_MEASUREMENT_PASSIVE)
				scan.numOfProbeRequests = 0;
			else
				scan.numOfProbeRequests = 1;
		} else {
#endif
			/* TODO: Is it optimal? */
#ifndef CONFIG_ATBM_5G_PRETEND_2G
			scan.numOfProbeRequests =
				(first->flags &
				#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,11,0))
				IEEE80211_CHAN_PASSIVE_SCAN
				#else
				IEEE80211_CHAN_NO_IR
				#endif
				)
				? 0 : 3;
#else
		scan.numOfProbeRequests = 3;
#endif
			if(hw_priv->scan.cca == 1)
				scan.numOfProbeRequests = 1;
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		}
#endif /* CONFIG_ATBM_APOLLO_TESTMODE */
		/*
		*passive scan
		*/
		if(hw_priv->scan.passive)
			scan.numOfProbeRequests = 0;
		scan.numOfSSIDs = hw_priv->scan.n_ssids;
		scan.ssids = &hw_priv->scan.ssids[0];
		scan.numOfChannels = it - hw_priv->scan.curr;
		/* TODO: Is it optimal? */
		if(scan.numOfChannels == 3)
		{
			ProbeRequestTime = 10;
			ChannelRemainTime = 10;
		}
		scan.probeDelay = ProbeRequestTime;
		/* It is not stated in WSM specification, however
		 * FW team says that driver may not use FG scan
		 * when joined. */
		if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA) {
			scan.scanType = WSM_SCAN_TYPE_BACKGROUND;
			scan.scanFlags = WSM_SCAN_FLAG_FORCE_BACKGROUND;
		}		
		if(hw_priv->scan.cca == 1){
			scan.scanFlags |= WSM_FLAG_BEST_CHANNEL_START;
		}
		scan.ch = atbm_kzalloc(
			sizeof(struct wsm_scan_ch[14]),
			GFP_KERNEL);
		if (!scan.ch) {
			hw_priv->scan.status = -ENOMEM;
			goto fail;
		}
		maxChannelTime = (scan.numOfSSIDs * scan.numOfProbeRequests *
			ProbeRequestTime) + ChannelRemainTime;
		maxChannelTime = (maxChannelTime < 35) ? 35 : maxChannelTime;
#ifndef ATBM_USE_FASTLINK
		for (i = 0; i < scan.numOfChannels; ++i) {
			scan.ch[i].number = channel_hw_value(hw_priv->scan.curr[i]);		
#else
		int chan_num[14]={1,6,11,2,7,3,8,4,9,5,10,12,13,14};
		for (i = 0; i < scan.numOfChannels; ++i) {
			scan.ch[i].number = channel_hw_value(hw_priv->scan.curr[chan_num[i] - 1]);
#endif
		//	scan.ch[i].minChannelTime = 100;
		//	scan.ch[i].maxChannelTime = 120;
		//	atbm_printk_err("scan time ¡ê? %d,%d,chan=%d \n",
		//	scan.ch[i].minChannelTime,scan.ch[i].maxChannelTime,scan.ch[i].number);
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
			if (hw_priv->enable_advance_scan) {
				scan.ch[i].minChannelTime =
					hw_priv->advanceScanElems.duration;
				scan.ch[i].maxChannelTime =
					hw_priv->advanceScanElems.duration;
			} else {
#endif
				#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,11,0))
				if (hw_priv->scan.curr[i]->flags & IEEE80211_CHAN_PASSIVE_SCAN) 
				#else
				if (hw_priv->scan.curr[i]->flags & IEEE80211_CHAN_NO_IR)
				#endif
				{
					scan.ch[i].minChannelTime = 50;
					scan.ch[i].maxChannelTime = 110;
				}
				else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
					if (hw_priv->scan.req->no_cck)						
						scan.ch[i].minChannelTime = 35;
					else
#endif //#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0))
						scan.ch[i].minChannelTime = 15;
					scan.ch[i].maxChannelTime = maxChannelTime;
				}
				if(hw_priv->scan.cca == 1){
					scan.ch[i].maxChannelTime = 500;
				}
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
			}
#endif
		}
#ifdef CONFIG_ATBM_APOLLO_TESTMODE
		if (hw_priv->enable_advance_scan &&
			(hw_priv->advanceScanElems.scanMode ==
				ATBM_APOLLO_SCAN_MEASUREMENT_PASSIVE) &&
			(priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA) &&
			(channel_hw_value(hw_priv->channel)== advance_scan_req_channel)) {
				/* Start Advance Scan Timer */
				hw_priv->scan.status = atbm_advance_scan_start(hw_priv);
				wsm_unlock_tx(hw_priv);
		} else
#endif
		/*
		*sometimes,t920 take a long time to scan,and
		*the usb recive process run too slowly.if that
		*happens,can triger some errors.
		*/
		//hw_priv->scan.status = atbm_scan_start(priv, &scan);
		atbm_printk_scan("scan start band(%d),(%d)\n",scan.band,scan.numOfChannels);
		scan_status = atbm_scan_start(priv, &scan);
		atbm_kfree(scan.ch);
		if (WARN_ON(scan_status)){
			hw_priv->scan.status = scan_status;
			goto fail;
		}
		hw_priv->scan.curr = it;
	}
	mutex_unlock(&hw_priv->conf_mutex);
	return;

fail:	
	hw_priv->scan.curr = hw_priv->scan.end;
	mutex_unlock(&hw_priv->conf_mutex);
	atbm_hw_priv_queue_work(hw_priv, &hw_priv->scan.work);
	return;
vif_err:
	hw_priv->scan.req = NULL;
	hw_priv->scan.cca = 0;
	hw_priv->scan.req_wrap = NULL;
	up(&hw_priv->scan.lock);
	wsm_unlock_tx(hw_priv);
	atbm_printk_err("[SCAN] interface removed\n");
	ieee80211_scan_completed(hw_priv->hw,
				 hw_priv->scan.status ? 1 : 0);
	atbm_release_suspend(hw_priv);
	return;
}
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
void atbm_sched_scan_work(struct atbm_work_struct *work)
{
	struct atbm_common *hw_priv = container_of(work, struct atbm_common,
		scan.swork);
	struct wsm_scan scan;
	struct wsm_ssid scan_ssid;
	int i;
	struct atbm_vif *priv = ABwifi_hwpriv_to_vifpriv(hw_priv,
					hw_priv->scan.if_id);
	if (unlikely(!priv)) {
		WARN_ON(1);
		return;
	}

	atbm_priv_vif_list_read_unlock(&priv->vif_lock);

	
	mutex_lock(&hw_priv->conf_mutex);
	hw_priv->auto_scanning = 1;

	scan.band = 0;

	if (priv->join_status == ATBM_APOLLO_JOIN_STATUS_STA)
		scan.scanType = 3; /* auto background */
	else
		scan.scanType = 2; /* auto foreground */

	scan.scanFlags = 0x01; /* bit 0 set => forced background scan */
	scan.maxTransmitRate = WSM_TRANSMIT_RATE_6;
	scan.autoScanInterval = (0xba << 24)|(30 * 1024); /* 30 seconds, -70 rssi */
	scan.numOfProbeRequests = 1;
	//scan.numOfChannels = 11;
	scan.numOfChannels = hw_priv->num_scanchannels;
	scan.numOfSSIDs = 1;
	scan.probeDelay = 100;
	scan_ssid.length = priv->ssid_length;
	memcpy(scan_ssid.ssid, priv->ssid, priv->ssid_length);
	scan.ssids = &scan_ssid;

	scan.ch = atbm_kzalloc(
		sizeof(struct wsm_scan_ch[scan.numOfChannels]),
		GFP_KERNEL);
	if (!scan.ch) {
		hw_priv->scan.status = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < scan.numOfChannels; i++) {
		scan.ch[i].number = hw_priv->scan_channels[i].number;
		scan.ch[i].minChannelTime = hw_priv->scan_channels[i].minChannelTime;
		scan.ch[i].maxChannelTime = hw_priv->scan_channels[i].maxChannelTime;
		scan.ch[i].txPowerLevel = hw_priv->scan_channels[i].txPowerLevel;
	}

#if 0
	for (i = 1; i <= scan.numOfChannels; i++) {
		scan.ch[i-1].number = i;
		scan.ch[i-1].minChannelTime = 10;
		scan.ch[i-1].maxChannelTime = 40;
	}
#endif

	hw_priv->scan.status = atbm_sched_scan_start(priv, &scan);
	atbm_kfree(scan.ch);
	if (hw_priv->scan.status)
		goto fail;
	mutex_unlock(&hw_priv->conf_mutex);
	return;

fail:
	mutex_unlock(&hw_priv->conf_mutex);
	atbm_hw_priv_queue_work(hw_priv, &hw_priv->scan.swork);
	return;
}

void atbm_hw_sched_scan_stop(struct atbm_common *hw_priv)
{
	struct atbm_vif *priv = ABwifi_hwpriv_to_vifpriv(hw_priv,
					hw_priv->scan.if_id);
	if (unlikely(!priv))
		return;
	atbm_priv_vif_list_read_unlock(&priv->vif_lock);

	wsm_stop_scan(hw_priv, priv->if_id);

	return;
}
#endif /*ROAM_OFFLOAD*/
#endif
#ifdef CONFIG_ATBM_SUPPORT_P2P
void atbm_scan_listenning_restart_delayed(struct atbm_vif *priv)
{
	
}
#endif
static void atbm_scan_complete(struct atbm_common *hw_priv, int if_id)
{
	atomic_xchg(&hw_priv->recent_scan, 0);

	if(atbm_scan_split_running(hw_priv) == false){
		atbm_scan_work(&hw_priv->scan.work);
	}else {
#ifndef CONFIG_ATBM_SCAN_SPLIT
		BUG_ON(1);	
#endif
	}
}

void atbm_scan_complete_cb(struct atbm_common *hw_priv,
				struct wsm_scan_complete *arg)
{
	struct atbm_vif *priv = ABwifi_hwpriv_to_vifpriv(hw_priv,
					hw_priv->scan.if_id);	

	
	if (unlikely(!priv))
	{
		return;
	}

#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
	if (hw_priv->auto_scanning)
		atbm_hw_priv_queue_delayed_work(hw_priv,
				&hw_priv->scan.timeout, 0);
#endif /*ROAM_OFFLOAD*/
#endif
	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED)) {
		/* STA is stopped. */
		atbm_priv_vif_list_read_unlock(&priv->vif_lock);
		atbm_printk_err("[SCAN] mode err\n");
		return;
	}
	atbm_priv_vif_list_read_unlock(&priv->vif_lock);

	//printk("hw_priv->bStartTx %d\n",hw_priv->bStartTx);
	if(hw_priv->bStartTx)
	{
		atbm_hw_priv_queue_delayed_work(hw_priv,&hw_priv->scan.timeout, HZ/10);
		return;
	}

	
#ifdef ATBM_SUPPORT_SMARTCONFIG
	priv->scan_no_connect=priv->scan_no_connect_back;
	if (hw_priv->scan.scan_smartconfig){
		//printk("**%s %d**\n", __FUNCTION__, hw_priv->scan.scan_smartconfig);
		smartconfig_magic_scan_done(hw_priv);
		return ;
	}
#endif
	atbm_printk_scan("hw_priv->scan.status %d\n",hw_priv->scan.status);

	if(hw_priv->scan.status == -ETIMEDOUT)
		atbm_printk_warn("Scan timeout already occured. Don't cancel work");
	if ((hw_priv->scan.status != -ETIMEDOUT) &&
		(atbm_hw_cancel_delayed_work(&hw_priv->scan.timeout,false/*can't set to true,because this function is call in bh, must not wait in bh */) > 0)) {
		hw_priv->scan.status = 1;
		if(hw_priv->scan.cca){
			struct ieee80211_internal_scan_notity notify;
			notify.cca.val = arg->busy_ratio;
			notify.cca.val_len = sizeof(arg->busy_ratio);
			notify.success = true;
			ieee80211_scan_cca_notify(hw_priv->hw,&notify);
		}
#ifdef SIGMSTAR_SCAN_FEATURE
		else {
				int i;
				struct ieee80211_local *local = hw_to_local(hw_priv->hw);
				for(i=0;(i<CHANNEL_NUM)&&(i<14);i++){
				local->noise_floor[i] = arg->busy_ratio[i];
				}
		}
#endif //#ifdef SIGMSTAR_SCAN_FEATURE
		atbm_hw_priv_queue_delayed_work(hw_priv,
				&hw_priv->scan.timeout, 0);
	}
}
//#ifdef CONFIG_WIRELESS_EXT

void etf_scan_end_work(struct atbm_work_struct *work)
{
	struct atbm_common *hw_priv =
		container_of(work, struct atbm_common, etf_tx_end_work);
	
	struct atbm_vif *priv = __ABwifi_hwpriv_to_vifpriv(hw_priv,
					hw_priv->scan.if_id);
	
	etf_v2_scan_end(hw_priv,priv->vif);
}
//#endif  //CONFIG_WIRELESS_EXT
void atbm_scan_timeout(struct atbm_work_struct *work)
{
	struct atbm_common *hw_priv =
		container_of(work, struct atbm_common, scan.timeout.work);
//#ifdef CONFIG_WIRELESS_EXT
	if(hw_priv->bStartTx)
	{
		struct atbm_vif *priv = __ABwifi_hwpriv_to_vifpriv(hw_priv,
						hw_priv->scan.if_id);
		if(hw_priv->bStartTxWantCancel==0){
			
			wsm_start_scan_etf(hw_priv,priv->vif);
		}
		else {
			hw_priv->bStartTx = 0;
			hw_priv->bStartTxWantCancel  = 0;
			if(hw_priv->etf_test_v2){
				atbm_hw_priv_queue_work(hw_priv, &hw_priv->etf_tx_end_work);
			}
			//stop etf test
			//up(&hw_priv->scan.lock);
			//printk("atbm_scan_timeout bStartTx %d\n",hw_priv->bStartTx);
		}
		return;
	}
//#endif  //CONFIG_WIRELESS_EXT
	
	if (likely(atomic_xchg(&hw_priv->scan.in_progress, 0))) {
		if (hw_priv->scan.status > 0)
			hw_priv->scan.status = 0;
		else if (!hw_priv->scan.status) {
			atbm_printk_warn("Timeout waiting for scan "
				"complete notification.\n");
			hw_priv->scan.status = -ETIMEDOUT;
			hw_priv->scan.curr = hw_priv->scan.end;
			if(WARN_ON(wsm_stop_scan(hw_priv,
						hw_priv->scan.if_id ? 1 : 0)))
			{
				hw_priv->scan.wait_complete= 0;
				//#ifndef OPER_CLOCK_USE_SEM
				//mutex_unlock(&hw_priv->wsm_oper_lock);
				//#else
				//up(&hw_priv->wsm_oper_lock);
				//#endif
				wsm_oper_unlock(hw_priv);
			}
		}
		atbm_scan_complete(hw_priv, hw_priv->scan.if_id);
#ifdef CONFIG_ATBM_SUPPORT_SCHED_SCAN
#ifdef ROAM_OFFLOAD
	} else if (hw_priv->auto_scanning) {
		hw_priv->auto_scanning = 0;
		ieee80211_sched_scan_results(hw_priv->hw);
#endif /*ROAM_OFFLOAD*/
#endif
	}

}

#ifdef CONFIG_ATBM_APOLLO_TESTMODE
void atbm_advance_scan_timeout(struct atbm_work_struct *work)
{
	struct atbm_common *hw_priv =
		container_of(work, struct atbm_common, advance_scan_timeout.work);

	struct atbm_vif *priv = ABwifi_hwpriv_to_vifpriv(hw_priv,
					hw_priv->scan.if_id);
	if (WARN_ON(!priv))
		return;
	atbm_priv_vif_list_read_unlock(&priv->vif_lock);

	hw_priv->scan.status = 0;
	if (hw_priv->advanceScanElems.scanMode ==
		ATBM_APOLLO_SCAN_MEASUREMENT_PASSIVE) {
		/* Passive Scan on Serving Channel
		 * Timer Expire */
		atbm_scan_complete(hw_priv, hw_priv->scan.if_id);
	} else {
		/* Active Scan on Serving Channel
		 * Timer Expire */
		mutex_lock(&hw_priv->conf_mutex);
		//wsm_lock_tx(priv);
		wsm_vif_lock_tx(priv);
		/* Once Duration is Over, enable filtering
		 * and Revert Back Power Save */
		if ((priv->powersave_mode.pmMode & WSM_PSM_PS))
			wsm_set_pm(hw_priv, &priv->powersave_mode,
				priv->if_id);
		hw_priv->scan.req = NULL;
		atbm_update_filtering(priv);
		hw_priv->enable_advance_scan = false;
		wsm_unlock_tx(hw_priv);
		mutex_unlock(&hw_priv->conf_mutex);
		ieee80211_scan_completed(hw_priv->hw,
			hw_priv->scan.status ? true : false);
		up(&hw_priv->scan.lock);
	}
}
#endif
void atbm_wait_scan_complete_sync(struct atbm_common *hw_priv)
{
	down(&hw_priv->scan.lock);
	mutex_lock(&hw_priv->conf_mutex);
	/*
	*here wait scan completed
	*/
	mutex_unlock(&hw_priv->conf_mutex);
	up(&hw_priv->scan.lock);
	atbm_printk_scan( "%s\n",__func__);
}
void atbm_cancel_hw_scan(struct ieee80211_hw *hw,struct ieee80211_vif *vif)
{
	struct atbm_common *hw_priv = hw->priv;
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);

	atbm_printk_scan( "%s:[%d]\n",__func__,priv->if_id);
#ifdef ATBM_USE_FASTLINK
	if(atbm_hw_cancel_delayed_work(&hw_priv->scan.timeout,true) == true){
		atbm_scan_timeout(&hw_priv->scan.timeout.work);
	}
#endif
	atbm_wait_scan_complete_sync(hw_priv);
}
