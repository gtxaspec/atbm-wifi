/*
 * mac80211 work implementation
 *
 * Copyright 2003-2008, Jouni Malinen <j@w1.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2009, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <net/atbm_mac80211.h>
#include <asm/unaligned.h>

#include "ieee80211_i.h"
#include "rate.h"
#include "driver-ops.h"
#include "cfg.h"
static void ieee80211_work_empty_start_pendding(struct ieee80211_local *local);


/* utils */
static inline void ASSERT_WORK_MTX(struct ieee80211_local *local)
{
	lockdep_assert_held(&local->mtx);
}

/*
 * We can have multiple work items (and connection probing)
 * scheduling this timer, but we need to take care to only
 * reschedule it when it should fire _earlier_ than it was
 * asked for before, or if it's not pending right now. This
 * function ensures that. Note that it then is required to
 * run this function for all timeouts after the first one
 * has happened -- the work that runs from this timer will
 * do that.
 */
static void run_again(struct ieee80211_local *local,
		      unsigned long timeout)
{
	ASSERT_WORK_MTX(local);

	if (!atbm_timer_pending(&local->work_timer) ||
	    time_before(timeout, local->work_timer.expires))
		atbm_mod_timer(&local->work_timer, timeout);
}
#if ((LINUX_VERSION_CODE < KERNEL_VERSION(2,6,40)) || (defined (ATBM_ALLOC_MEM_DEBUG)))
static void work_free_rcu(struct rcu_head *head)
{
	struct ieee80211_work *wk =
		container_of(head, struct ieee80211_work, rcu_head);

	atbm_kfree(wk);
}

void free_work(struct ieee80211_work *wk)
{
	call_rcu(&wk->rcu_head, work_free_rcu);
}
#else
void free_work(struct ieee80211_work *wk)
{
	kfree_rcu(wk, rcu_head);
}

#endif
void ieee80211_work_enter_pending(struct ieee80211_local *local)
{	
	/*
	*suspend worker
	*/
	local->work_suspend = true;
	smp_mb();
}
void ieee80211_work_exit_pending(struct ieee80211_local *local)
{
	/*
	*resume worker
	*/
	local->work_suspend = false;
	smp_mb();

	ieee80211_queue_work(&local->hw, &local->work_work);
}
static void __ieee80211_work_cook_lists(struct ieee80211_local *local)
{	
	list_splice_tail_init(&local->work_pending_list, &local->work_list);
}
static void ieee80211_work_cook_lists(struct ieee80211_local *local)
{
	spin_lock_bh(&local->work_pending_lock);
	list_splice_tail_init(&local->work_pending_list, &local->work_list);
	spin_unlock_bh(&local->work_pending_lock);
}
int ieee80211_work_busy(struct ieee80211_local *local)
{
	return (local->worker_working == true) || !list_empty(&local->work_list);
}
static int ieee80211_work_aborted(struct ieee80211_work *wk)
{
	return (wk->type == IEEE80211_WORK_ABORT) || (wk->start == NULL);
}
static int ieee80211_work_start(struct ieee80211_work *wk)
{
	enum work_action rma;

	rma = wk->start(wk);

	if(wk->type == IEEE80211_WORK_ABANDON){
		rma = WORK_ACT_TIMEOUT;
	}

	return rma;
}
static int ieee80211_work_fc_mismatch(struct ieee80211_work *wk,u16 fc)
{
	return (wk->filter_fc !=  0xFF) && (wk->filter_fc != (fc & IEEE80211_FCTL_STYPE));
}
static int ieee80211_work_skip_rx(struct ieee80211_work *wk)
{
	return  wk->rx == NULL;
}
static int ieee80211_work_skip_tx(struct ieee80211_work *wk)
{
	return  wk->tx == NULL;
}
static int ieee80211_work_skip_filter(struct ieee80211_work *wk,struct sk_buff *skb)
{
	return  wk->filter == NULL ||  wk->filter(wk,skb) == false;
}

static int ieee80211_work_bssid_mismatch(struct ieee80211_work *wk,const u8* addr)
{
	return atbm_compare_ether_addr(wk->filter_bssid, addr);
}

static int ieee80211_work_sa_mismatch(struct ieee80211_work *wk,const u8* addr)
{
	return atbm_compare_ether_addr(wk->filter_sa, addr);
}

static void ieee80211_work_queued_mgmt(struct ieee80211_local *local,
					  struct sk_buff *skb)
{
	struct atbm_ieee80211_mgmt *mgmt;
	struct ieee80211_work *wk;
	enum work_action rma = WORK_ACT_NONE;

	mgmt = (struct atbm_ieee80211_mgmt *) skb->data;

	mutex_lock(&local->mtx);

	list_for_each_entry(wk, &local->work_list, list) {

		/*
		*work has been aborted
		*/
		if(ieee80211_work_aborted(wk))
			continue;
		
		if(ieee80211_work_skip_rx(wk) && ieee80211_work_skip_tx(wk))
			continue;
		
		if(ieee80211_work_skip_filter(wk,skb))
			continue;
		
		if(wk->rx)
			rma = wk->rx(wk,skb);
		else 
			rma = wk->tx(wk,skb);
		/*
		 * We've either received an unexpected frame, or we have
		 * multiple work items and need to match the frame to the
		 * right one.
		 */
		if (rma == WORK_ACT_MISMATCH)
			continue;

		/*
		 * We've processed this frame for that work, so it can't
		 * belong to another work struct.
		 * NB: this is also required for correctness for 'rma'!
		 */
		break;
	}

	switch (rma) {
	case WORK_ACT_MISMATCH:
		/* ignore this unmatched frame */
		break;
	case WORK_ACT_NONE:
		break;
	case WORK_ACT_DONE:
		spin_lock_bh(&local->work_pending_lock);
		list_del_rcu(&wk->list);
		spin_unlock_bh(&local->work_pending_lock);
		break;
	default:
		WARN(1, "unexpected: %d", rma);
	}
	
	mutex_unlock(&local->mtx);

	if (rma != WORK_ACT_DONE)
		goto out;

	switch (wk->done(wk, skb)) {
	case WORK_DONE_DESTROY:
		free_work(wk);
		break;
	case WORK_DONE_REQUEUE:
		//synchronize_rcu();
		wk->started = false; /* restart */
		spin_lock_bh(&local->work_pending_lock);
		list_add_tail(&wk->list, &local->work_pending_list);
		spin_unlock_bh(&local->work_pending_lock);
	}

 out:
	atbm_kfree_skb(skb);
}

static bool ieee80211_work_ct_coexists(enum nl80211_channel_type wk_ct,
				       enum nl80211_channel_type oper_ct)
{
	switch (wk_ct) {
	case NL80211_CHAN_NO_HT:
		return true;
	case NL80211_CHAN_HT20:
		if (oper_ct != NL80211_CHAN_NO_HT)
			return true;
		return false;
	case NL80211_CHAN_HT40MINUS:
	case NL80211_CHAN_HT40PLUS:
		return (wk_ct == oper_ct);
	}
	WARN_ON(1); /* shouldn't get here */
	return false;
}

static void ieee80211_work_timer(unsigned long data)
{
	struct ieee80211_local *local = (void *) data;

	if (local->quiescing)
		return;

	ieee80211_queue_work(&local->hw, &local->work_work);
}
static void ieee80211_work_empty_start_pendding(struct ieee80211_local *local)
{
	lockdep_assert_held(&local->mtx);

	
#ifdef CONFIG_ATBM_SUPPORT_CSA
	if (!list_empty(&local->csa_pending) && list_empty(&local->work_list)){
		ieee80211_chan_start_pending_csa(local);
		return;
	}

	if(ieee80211_csa_busy(local)){
		return;
	}
#endif
#ifdef CONFIG_ATBM_SUPPORT_P2P	
	if(local->roc_pendding&&local->roc_pendding_sdata){
		struct ieee80211_roc_work * roc = local->roc_pendding;
		int ret;
		u32 init_duration = 0;
		u32 roc_duration = 0;
		u32 roc_duration_left = 0;
		u8 roc_timeout = 0;
		ret = -1;
		init_duration = roc->duration;

		if(init_duration == 0)
			init_duration = 100;
		roc_duration_left = init_duration;
		if(init_duration>70)
			roc_duration_left = init_duration -70;
			
		atbm_printk_mgmt("%s:roc_pendding,roc_duration_left(%d),init_duration(%d)\n",__func__,
			roc_duration_left,init_duration);
		roc_timeout = !time_is_after_jiffies(roc->pending_start_time+(roc_duration_left*HZ)/1000);

		if(roc_timeout || list_empty(&local->work_list)){
			local->roc_pendding = NULL;
			local->roc_pendding_sdata = NULL;
			if(roc_timeout){
				ret = -1;
			}
			else if (list_empty(&local->work_list)){
				roc_duration = ((roc->pending_start_time+(init_duration*HZ)/1000 - jiffies)*1000)/HZ;

				if(roc_duration>70)
					ret = ieee80211_start_pending_roc_work(local,roc->hw_roc_dev,
					roc->sdata,roc->chan,roc->chan_type,roc_duration,&roc->cookie,roc->frame);
				else{
					ret = -1;
				}
			}else{
				BUG_ON(1);
			}

			if(ret)
			{
			#if 0
					mutex_unlock(&local->mtx);
					printk(KERN_ERR "%s:pennding roc err \n",__func__);
					ieee80211_roc_notify_destroy(roc);
					mutex_lock(&local->mtx);
			#else
				atbm_printk_mgmt("%s:pennding roc err roc_timeout(%d),roc_duration(%d),cookie(%llx)\n",
					__func__,roc_timeout,roc_duration,roc->mgmt_tx_cookie ? roc->mgmt_tx_cookie: roc->cookie);
				BUG_ON(local->scanning);
				BUG_ON(!list_empty(&local->roc_list));
				roc->started = true;
				list_add_tail(&roc->list, &local->roc_list);
				/*
				*first ,send remain_on_channel_ready event to wpa_supplicatn
				*second, send cancle_remain_on_channel event to wpa_supplicant
				*/
	//				ieee80211_ready_on_channel(&local->hw);
				ieee80211_remain_on_channel_expired(&local->hw,
						(roc->mgmt_tx_cookie ? roc->mgmt_tx_cookie: roc->cookie));
			#endif
			}
			else
			{
				atbm_kfree(roc);
			}
		}

	}
#endif
	if(local->scan_req && !local->scanning && list_empty(&local->work_list)){

		atbm_printk_scan("%s(%s):start delayed scan opration\n",__func__,local->scan_sdata->name);
		ieee880211_start_pending_scan(local);
	}
}
static void ieee80211_work_work(struct atbm_work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, work_work);
	struct ieee80211_channel_state *chan_state = &local->chan_state;
	struct sk_buff *skb;
	struct ieee80211_work *wk, *tmp;
	LIST_HEAD(free_work);
	enum work_action rma;
	bool remain_off_channel = false;
	u8 delay = 0;
	
	if (local->scanning)
		return;
	
	if(local->work_suspend){
		atbm_printk_err("work suspend\n");
		return;
	}
	/*
	 * ieee80211_queue_work() should have picked up most cases,
	 * here we'll pick the rest.
	 */
	if (WARN(local->suspended, "work during suspend\n"))
		return;

	local->worker_working = true;
	/* first process frames to avoid timing out while a frame is pending */
	while ((skb = atbm_skb_dequeue(&local->work_skb_queue)))
		ieee80211_work_queued_mgmt(local, skb);

	mutex_lock(&local->mtx);

#ifdef CONFIG_ATBM_SUPPORT_P2P	
	delay |= !list_empty(&local->roc_list);
#endif
#ifdef CONFIG_ATBM_SUPPORT_CSA
	delay |= ieee80211_csa_busy(local);
#endif
	ieee80211_work_cook_lists(local);

	ieee80211_recalc_idle(local);

	list_for_each_entry_safe(wk, tmp, &local->work_list, list) {
		bool started = wk->started;
		
		if(delay && wk->chan){
			if(!started){
				atbm_printk_mgmt("%s:in_listenning delay work\n",__func__);
				continue;
			}
		}
		
		/* mark work as started if it's on the current off-channel */
		if (!started && ((chan_state->tmp_channel &&
		    wk->chan == chan_state->tmp_channel &&
		    ieee80211_work_ct_coexists(wk->chan_type , chan_state->tmp_channel_type)) || (!wk->chan))) {		   
			started = true;
			wk->timeout = jiffies;
		}
		if (!started && !chan_state->tmp_channel) {
			/*
			 * TODO: could optimize this by leaving the
			 *	 station vifs in awake mode if they
			 *	 happen to be on the same channel as
			 *	 the requested channel
			 */
			do{
				switch(ieee80211_get_channel_mode(local,wk->sdata)){
				case CHAN_MODE_UNDEFINED:
					break;
				case CHAN_MODE_HOPPING:
					goto abort;
				case CHAN_MODE_FIXED:
					if(wk->chan != chan_state->oper_channel){
						atbm_printk_always("[%s] channel fixed[%d][%d]\n",wk->sdata->name,
											channel_hw_value(wk->chan),channel_hw_value(chan_state->oper_channel));
						goto abort;
					}

					if(ieee80211_chan_ct_coexists(wk->chan_type,chan_state->_oper_channel_type) == false){
						atbm_printk_always("[%s] channel type mismatch[%d][%d]\n",wk->sdata->name,
											wk->chan_type,chan_state->_oper_channel_type);
						goto abort;
					}
					break;
				}
				chan_state->tmp_channel = wk->chan;
				chan_state->tmp_channel_type = wk->chan_type;
				ieee80211_hw_config(local, 0);
				break;
abort:
				wk->type = IEEE80211_WORK_ABORT;
				break;
			}while(0);
			
			atbm_printk_mgmt("%s:start work ch(%d)(%d)\n",__func__,channel_hw_value(wk->chan),wk->type);

			started = true;
			wk->timeout = jiffies;
		}
		/* don't try to work with items that aren't started */
		if (!started){			
			atbm_printk_mgmt("%s:not start work ch(%d)\n",__func__,wk->type);
			continue;
		}
		if (time_is_after_jiffies(wk->timeout)) {
			/*
			 * This work item isn't supposed to be worked on
			 * right now, but take care to adjust the timer
			 * properly.
			 */
			run_again(local, wk->timeout);
			continue;
		}

		rma = ieee80211_work_aborted(wk) ? WORK_ACT_TIMEOUT : ieee80211_work_start(wk);
		
		wk->started = started;

		switch (rma) {
		case WORK_ACT_NONE:
			/* might have changed the timeout */
			run_again(local, wk->timeout);
			break;
		case WORK_ACT_TIMEOUT:
			spin_lock_bh(&local->work_pending_lock);
			list_del_rcu(&wk->list);
			list_add(&wk->list, &free_work);
			spin_unlock_bh(&local->work_pending_lock);
			break;
		default:
			WARN(1, "unexpected: %d", rma);
		}		
	}

	list_for_each_entry(wk, &local->work_list, list) {
		if (!wk->started)
			continue;
		if (wk->chan == NULL)
			continue;
		if (wk->chan != chan_state->tmp_channel)
			continue;
		if (!ieee80211_work_ct_coexists(wk->chan_type,
						chan_state->tmp_channel_type))
			continue;
		remain_off_channel = true;
	}

	if (!remain_off_channel && chan_state->tmp_channel) {
		chan_state->tmp_channel = NULL;
		/* If tmp_channel wasn't operating channel, then
		 * we need to go back on-channel.
		 * NOTE:  If we can ever be here while scannning,
		 * or if the hw_config() channel config logic changes,
		 * then we may need to do a more thorough check to see if
		 * we still need to do a hardware config.  Currently,
		 * we cannot be here while scanning, however.
		 */
		if(ieee80211_get_channel_mode(local,NULL) != CHAN_MODE_UNDEFINED){
			atbm_printk_mgmt("%s:reset work ch(%d)\n",__func__,channel_hw_value(chan_state->oper_channel));
			ieee80211_hw_config(local, 0);
		}

		/* At the least, we need to disable offchannel_ps,
		 * so just go ahead and run the entire offchannel
		 * return logic here.  We *could* skip enabling
		 * beaconing if we were already on-oper-channel
		 * as a future optimization.
		 */

		/* give connection some time to breathe */
		run_again(local, jiffies + HZ/10);
	}

	mutex_unlock(&local->mtx);

	list_for_each_entry_safe(wk, tmp, &free_work, list) {
		wk->done(wk, NULL);
		list_del(&wk->list);
		atbm_kfree(wk);
	}

	local->worker_working = false;
	
	mutex_lock(&local->mtx);
	
	ieee80211_work_empty_start_pendding(local);

	ieee80211_recalc_idle(local);

	mutex_unlock(&local->mtx);
}

void ieee80211_add_work(struct ieee80211_work *wk)
{
	struct ieee80211_local *local;

	if (WARN_ON(!wk->sdata))
		goto err;

	if (WARN_ON(!wk->done))
		goto err;

	if (WARN_ON(!ieee80211_sdata_running(wk->sdata)))
		goto err;

	wk->started = false;

	local = wk->sdata->local;

	spin_lock_bh(&local->work_pending_lock);
	list_add_tail(&wk->list, &local->work_pending_list);
	spin_unlock_bh(&local->work_pending_lock);

	if(local->work_suspend == false)
		ieee80211_queue_work(&local->hw, &local->work_work);

	return;
err:
	wk->done(wk,NULL);
	atbm_kfree(wk);

	return;
}

void ieee80211_work_init(struct ieee80211_local *local)
{
	atomic_set(&local->connectting,0);
	INIT_LIST_HEAD(&local->work_list);
	INIT_LIST_HEAD(&local->work_pending_list);
	spin_lock_init(&local->work_pending_lock);
	atbm_setup_timer(&local->work_timer, ieee80211_work_timer,
		    (unsigned long)local);
	ATBM_INIT_WORK(&local->work_work, ieee80211_work_work);
	atbm_skb_queue_head_init(&local->work_skb_queue);
}
bool ieee80211_work_type_switch(struct ieee80211_sub_if_data *sdata,const u8 *bssid,enum ieee80211_work_type cancle_type,
		enum ieee80211_work_type new)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_work *wk;
	bool cleanup = false;
	
	mutex_lock(&local->mtx);
	/*
	*here cook the list
	*/
	spin_lock_bh(&local->work_pending_lock);
	__ieee80211_work_cook_lists(local);
	
	list_for_each_entry(wk, &local->work_list, list) {
		if (wk->sdata != sdata){
			continue;
		}
		if(bssid && ieee80211_work_sa_mismatch(wk,bssid)){
			continue;
		}
		if((cancle_type != IEEE80211_WORK_MAX) && (wk->type != cancle_type)){
			continue;
		}
		cleanup = true;
		wk->type = new;
		wk->started = true;
		wk->timeout = jiffies;
	}
	spin_unlock_bh(&local->work_pending_lock);
	mutex_unlock(&local->mtx);

	return cleanup;
}
bool ieee80211_work_abort(struct ieee80211_sub_if_data *sdata,const u8 *bssid,enum ieee80211_work_type cancle_type)
{
	return ieee80211_work_type_switch(sdata,bssid,cancle_type,IEEE80211_WORK_ABORT);
}
void ieee80211_work_purge(struct ieee80211_sub_if_data *sdata,const u8 *bssid,enum ieee80211_work_type cancle_type,bool sync)
{
	struct ieee80211_local *local = sdata->local;
	bool cleanup = false;

	cleanup  = ieee80211_work_type_switch(sdata,bssid,cancle_type,IEEE80211_WORK_ABORT);
	/* run cleanups etc. */
	if (cleanup && sync)
		ieee80211_work_work(&local->work_work);
	else if(cleanup)
		ieee80211_queue_work(&local->hw, &local->work_work);
}
bool ieee80211_work_abandon(struct ieee80211_sub_if_data *sdata,const u8 *bssid,enum ieee80211_work_type cancle_type)
{
	struct ieee80211_local *local = sdata->local;
	bool cleanup = false;
	cleanup  = ieee80211_work_type_switch(sdata,bssid,cancle_type,IEEE80211_WORK_ABANDON);

	if(cleanup)
		ieee80211_queue_work(&local->hw, &local->work_work);

	return cleanup;
}
ieee80211_rx_result ieee80211_work_rx_mgmt(struct ieee80211_sub_if_data *sdata,
					   struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct atbm_ieee80211_mgmt *mgmt;
	struct ieee80211_work *wk;
	u16 fc;
	ieee80211_rx_result handle = RX_CONTINUE;
	if (skb->len < 24)
	{
		atbm_printk_mgmt("%s:skb->len < 24\n",__func__);
		return RX_DROP_MONITOR;
	}

	mgmt = (struct atbm_ieee80211_mgmt *) skb->data;
	fc = le16_to_cpu(mgmt->frame_control);

	spin_lock_bh(&local->work_pending_lock);
	__ieee80211_work_cook_lists(local);
	list_for_each_entry_rcu(wk, &local->work_list, list) {
		if (sdata != wk->sdata)
			continue;
		if (ieee80211_work_skip_rx(wk))
			continue;
		if (ieee80211_work_skip_filter(wk,skb))
			continue;
		
		atbm_skb_queue_tail(&local->work_skb_queue, skb);
		ieee80211_queue_work(&local->hw, &local->work_work);
		handle = RX_QUEUED;
		break;	
	}
	spin_unlock_bh(&local->work_pending_lock);
	
	return handle;
}

ieee80211_tx_result ieee80211_work_tx_mgmt(struct ieee80211_sub_if_data *sdata,
					   struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct atbm_ieee80211_mgmt *mgmt;
	struct ieee80211_work *wk;
	u16 fc;
	ieee80211_tx_result handle = TX_CONTINUE;

	mgmt = (struct atbm_ieee80211_mgmt *) skb->data;
	
	if (!ieee80211_is_mgmt(mgmt->frame_control))
		return TX_CONTINUE;
	
	if (skb->len < 24){
		atbm_printk_mgmt("%s:skb->len < 24\n",__func__);
		return TX_DROP;
	}
	fc = le16_to_cpu(mgmt->frame_control);

	spin_lock_bh(&local->work_pending_lock);
	__ieee80211_work_cook_lists(local);
	list_for_each_entry_rcu(wk, &local->work_list, list) {
		if (sdata != wk->sdata)
			continue;
		if (ieee80211_work_skip_tx(wk))
			continue;
		if (ieee80211_work_skip_filter(wk,skb))
			continue;
		atbm_skb_queue_tail(&local->work_skb_queue, skb);
		ieee80211_queue_work(&local->hw, &local->work_work);
		handle = TX_QUEUED;
		break;	
	}
	spin_unlock_bh(&local->work_pending_lock);
	
	return handle;
}

void ieee80211_assign_authen_bss(struct ieee80211_sub_if_data *sdata,struct cfg80211_bss *pub)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	atbm_printk_mgmt("%s:assign authen bss[%p]\n",sdata->name,pub);
	lockdep_assert_held(&local->mtx);
	/*
	*hold our bss for later use
	*/
	if(ieee80211_atbm_handle_bss(local->hw.wiphy,pub) == 0){
		/*
		*save pub
		*/
	}else {
		atbm_printk_mgmt("%s:get bss err\n",sdata->name);
		pub = NULL;
	}
	rcu_assign_pointer(ifmgd->authen_bss,pub);
}

void ieee80211_free_authen_bss(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;
	struct cfg80211_bss *free_bss;
	lockdep_assert_held(&local->mtx);
	
	atbm_printk_always("%s:free authen bss ++\n",sdata->name);
	if(sdata->vif.type != NL80211_IFTYPE_STATION){
		atbm_printk_err("%s: is not sta mode\n",sdata->name);
		return;
	}
	free_bss = rcu_dereference_protected(ifmgd->authen_bss,lockdep_is_held(&local->mtx));
	if(free_bss == NULL){
		return;
	}
	rcu_assign_pointer(ifmgd->authen_bss, NULL);
	
	atbm_printk_always("%s:free authen bss --\n",sdata->name);
	atbm_printk_mgmt("%s:free authen bss[%p]\n",sdata->name,free_bss);
	ieee80211_atbm_release_bss(local->hw.wiphy,free_bss);
}

bool ieee80211_work_rx_default_filter(struct ieee80211_work *wk,struct sk_buff *skb)
{
	u16 fc;
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	fc = le16_to_cpu(mgmt->frame_control);
	
	if (ieee80211_work_fc_mismatch(wk,fc))
		return false;
	if (ieee80211_work_sa_mismatch(wk, mgmt->sa))
		return false;
	if (ieee80211_work_bssid_mismatch(wk, mgmt->bssid))
		return false;

	return true;
	
}

bool ieee80211_work_tx_default_filter(struct ieee80211_work *wk,struct sk_buff *skb)
{
	u16 fc;
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	
	fc = le16_to_cpu(mgmt->frame_control);
	
	if (ieee80211_work_fc_mismatch(wk,fc))
		return false;
	if (ieee80211_work_sa_mismatch(wk, mgmt->da))
		return false;
	if (ieee80211_work_bssid_mismatch(wk, mgmt->bssid))
		return false;

	return true;
	
}
static enum work_done_result ieee80211_work_cancle_work_done(struct ieee80211_work* wk,
	struct sk_buff* skb)
{
	if(wk->type == IEEE80211_WORK_ABANDON){
		return WORK_DONE_DESTROY;
	}

	ieee80211_work_abandon(wk->sdata,wk->filter_bssid,wk->work_cancle.type);
	return WORK_DONE_DESTROY;
}
void  ieee80211_work_start_cancle_work(struct ieee80211_sub_if_data *sdata,u8 *bssid,enum ieee80211_work_type cancle_type)
{
	struct ieee80211_work *wk;
	
	wk = atbm_kzalloc(sizeof(*wk), GFP_ATOMIC);
	if (WARN_ON(!wk))
		return ;
	
	wk->sdata = sdata;
	wk->done  = ieee80211_work_cancle_work_done;
	wk->start = NULL;
	wk->type = IEEE80211_WORK_CANCLE_WORK;
	wk->chan = NULL;
	wk->rx   = NULL;
	memcpy(wk->filter_bssid , bssid,ETH_ALEN);
	memcpy(wk->filter_sa ,    bssid,ETH_ALEN);
	wk->work_cancle.type = cancle_type;
	
	ieee80211_add_work(wk);
}
