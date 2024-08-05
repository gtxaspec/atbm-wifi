#include <net/atbm_mac80211.h>
#include <linux/nl80211.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/freezer.h>
#include <linux/inetdevice.h>
#include <net/net_namespace.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "twt.h"

static void ieee80211_twt_state_change(struct sta_twt_mlme *twt,enum atbm_ieee80211_twt_setup_cmd cmd);

static void ieee80211_twt_deliver_buffed(struct sta_twt_mlme *twt)
{
	struct sta_info *sta = container_of(twt,struct sta_info,twt);
	struct sk_buff_head pending;
	unsigned long flags;
	int ac;
	set_sta_flag(sta, WLAN_STA_HE_TWT_DELIVER);
	clear_sta_flag(sta,WLAN_STA_HE_TWT_DOZE);

	skb_queue_head_init(&pending);

	
	spin_lock_bh(&twt->lock);

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {

		spin_lock_irqsave(&sta->tx_filtered[ac].lock, flags);
		skb_queue_splice_tail_init(&sta->tx_filtered[ac], &pending);
		spin_unlock_irqrestore(&sta->tx_filtered[ac].lock, flags);
	}

	ieee80211_add_pending_skbs(sta->local, &pending);

	clear_sta_flag(sta, WLAN_STA_HE_TWT_DELIVER);
	spin_unlock_bh(&twt->lock);
}


bool ieee80211_tx_h_twt_buf(struct ieee80211_sub_if_data* sdata, struct sta_info* sta, struct sk_buff* skb)
{
	struct ieee80211_local* local = sdata->local;
	struct ieee80211_tx_info* info = IEEE80211_SKB_CB(skb);

	if (!sta) {
		return false;
	}
	
	if (unlikely(test_sta_flag(sta, WLAN_STA_HE_TWT_DOZE) &&
		!(info->flags & IEEE80211_TX_CTL_POLL_RESPONSE))) {
		int ac = skb_get_queue_mapping(skb);
		struct sta_twt_mlme *twt = &sta->twt;

		spin_lock_bh(&twt->lock);

		if(!test_sta_flag(sta, WLAN_STA_HE_TWT_DOZE) && 
		   !test_sta_flag(sta, WLAN_STA_HE_TWT_DELIVER)){
		   spin_unlock_bh(&twt->lock);
		   return false;
		}
		info->control.jiffies = jiffies;
		info->control.vif = &sdata->vif;
		info->flags |= IEEE80211_TX_INTFL_NEED_TXPROCESSING;
		atbm_skb_queue_tail(&sta->tx_filtered[ac],skb);

		if (!atbm_timer_pending(&local->sta_cleanup))
			atbm_mod_timer(&local->sta_cleanup,
				round_jiffies(jiffies +
					STA_INFO_CLEANUP_INTERVAL));

		spin_unlock_bh(&twt->lock);
		return true;
	}

	return false;
}
static void  ieee80211_twt_state_set(struct sta_twt_mlme *twt,enum ieee80211_sta_twt_state state)
{
	struct sta_info *sta = container_of(twt,struct sta_info,twt);
	
	twt->state = state;

	switch(twt->state){
	case STA_TWT_STATE_IDLE:
	case STA_TWT_STATE_SP_START:
		/*
		*release buff
		*/
		ieee80211_twt_deliver_buffed(twt);
		break;
	case STA_TWT_STATE_SP_INTERVAL:
		set_sta_flag(sta, WLAN_STA_HE_TWT_DOZE);
		break;
	default:
		break;
	}
}
static void ieee80211_twt_set_teardown(struct sta_info *sta)
{
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;

	if(local->ops->do_twt)
		local->ops->do_twt(&sdata->vif,&sta->sta,&sta->twt.twt_request,false);

	ieee80211_twt_state_set(&sta->twt,STA_TWT_STATE_IDLE);
}
static bool _ieee80211_process_rx_twt_action(struct sk_buff *skb)
{
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	atbm_printk_always("%s:action(%d)\n",__func__,mgmt->u.action.u.s1g.action_code);
	switch (mgmt->u.action.u.s1g.action_code) {
	case ATBM_WLAN_S1G_TWT_SETUP: {
		struct atbm_ieee80211_twt_setup *twt;

		if (skb->len < ATBM_IEEE80211_MIN_ACTION_SIZE +
				   1 + /* action code */
				   sizeof(struct atbm_ieee80211_twt_setup) +
				   2 /* TWT req_type agrt */)
		{
			atbm_printk_err("twt_action fail[%d][%d]\n",skb->len,ATBM_IEEE80211_MIN_ACTION_SIZE + sizeof(struct atbm_ieee80211_twt_setup));
			break;
		}

		twt = (void *)mgmt->u.action.u.s1g.variable;
		if (twt->element_id != ATBM_WLAN_EID_S1G_TWT)
		{
			atbm_printk_err("rx twt id [%d][%d]\n",twt->element_id,ATBM_WLAN_EID_S1G_TWT);
			break;
		}

		if (skb->len < ATBM_IEEE80211_MIN_ACTION_SIZE +
				   4 + /* action code + token + tlv */
				   twt->length)
		{
			atbm_printk_err("twt_action fail 2[%d][%d][%d]\n",skb->len,ATBM_IEEE80211_MIN_ACTION_SIZE,twt->length);
			break;
		}

		return true; /* queue the frame */
	}
	case ATBM_WLAN_S1G_TWT_TEARDOWN:
		if (skb->len < ATBM_IEEE80211_MIN_ACTION_SIZE + 2)
			break;

		return true; /* queue the frame */
	default:
		break;
	}

	return false;
}

bool ieee80211_process_rx_twt_action(struct ieee80211_rx_data *rx)
{
	if (!rx->sta)
		return false;

	return _ieee80211_process_rx_twt_action(rx->skb);
}
/*
static void
ieee80211_twt_send_setup(struct ieee80211_sub_if_data *sdata, const u8 *da,
			     const u8 *bssid, struct atbm_ieee80211_twt_setup *twt)
{
	int len = ATBM_IEEE80211_MIN_ACTION_SIZE + 4 + twt->length;
	struct ieee80211_local *local = sdata->local;
	struct atbm_ieee80211_mgmt *mgmt;
	struct sk_buff *skb;

	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom + len);
	if (!skb)
		return;

	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = skb_put(skb, len);
	memset(mgmt,0,len);
	
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);

	mgmt->u.action.category = ATBM_WLAN_CATEGORY_S1G;
	mgmt->u.action.u.s1g.action_code = ATBM_WLAN_S1G_TWT_SETUP;
	memcpy(mgmt->u.action.u.s1g.variable, twt, 3 + twt->length);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
					IEEE80211_TX_CTL_REQ_TX_STATUS;
	ieee80211_tx_skb(sdata, skb);
}
*/
static int ieee80211_twt_rx_setup(struct ieee80211_sub_if_data *sdata,
			   struct sta_info *sta, struct sk_buff *skb)
{
	struct ieee80211_local *local = sdata->local;
	struct atbm_ieee80211_mgmt *mgmt = (void *)skb->data;
	struct atbm_ieee80211_twt_setup *twt = (void *)mgmt->u.action.u.s1g.variable;
	struct atbm_ieee80211_twt_params *twt_agrt = (void *)twt->params;
	enum atbm_ieee80211_twt_setup_cmd setup_cmd;
	u16 req_type = le16_to_cpu(twt_agrt->req_type);
	struct sta_twt_mlme *statwt = &sta->twt;
	int ret = 0;
	
	/* broadcast TWT not supported yet */
	if (twt->control) {
		ret = -1;
		goto out;
	}
	
	if (statwt->state >= STA_TWT_STATE_ACCEPT){
		ret = -1;
		goto out;
	}
	
	setup_cmd = FIELD_GET(ATBM_IEEE80211_TWT_REQTYPE_SETUP_CMD, req_type);

	ieee80211_twt_state_change(statwt,setup_cmd);

	if(statwt->state == STA_TWT_STATE_IDLE){
		ret = -1;
		goto out;
	}

	statwt->twt_request.flow_id  = FIELD_GET(ATBM_IEEE80211_TWT_REQTYPE_FLOWID, req_type);
	statwt->twt_request.implicit = !!(req_type & ATBM_IEEE80211_TWT_REQTYPE_IMPLICIT);
	statwt->twt_request.trigger  = !!(req_type & ATBM_IEEE80211_TWT_REQTYPE_TRIGGER);
	statwt->twt_request.flow_type  =  !!(req_type & ATBM_IEEE80211_TWT_REQTYPE_FLOWTYPE);
	statwt->twt_request.exp      = FIELD_GET(ATBM_IEEE80211_TWT_REQTYPE_WAKE_INT_EXP, req_type);
	statwt->twt_request.twt      = le64_to_cpu(twt_agrt->twt);
	statwt->twt_request.duration = twt_agrt->min_twt_dur;
	statwt->twt_request.mantissa = le16_to_cpu(twt_agrt->mantissa);
	atbm_printk_err("[%pM] twt info[%d][%d][%d][%d][%d][%d][%d][%d]\n",sta->sta.addr,statwt->twt_request.flow_id,
					statwt->twt_request.implicit,
					statwt->twt_request.trigger,
					statwt->twt_request.flow_type,
					statwt->twt_request.exp,
					(u32)statwt->twt_request.twt,
					statwt->twt_request.duration,statwt->twt_request.mantissa);
	if(local->ops->do_twt)
		ret = local->ops->do_twt(&sdata->vif,&sta->sta,&statwt->twt_request,true);
out:
	if(ret){
		/*
		*send teardown
		*/
		ieee80211_twt_sta_teardown(sta);
	}
	return ret;
}
static int ieee80211_twt_rx_teardown(struct ieee80211_sub_if_data *sdata,
			   struct sta_info *sta, struct sk_buff *skb)
{
	
	struct atbm_ieee80211_mgmt *mgmt = (void *)skb->data;
//	struct atbm_ieee80211_twt_setup *twt = (void *)mgmt->u.action.u.s1g.variable;
//	struct atbm_ieee80211_twt_params *twt_agrt = (void *)twt->params;
	int ret = 0;
	
	struct sta_twt_mlme *statwt = &sta->twt;
	
	if(statwt->state == STA_TWT_STATE_IDLE){
		ret = -1;
		goto out;
	}

	if(statwt->twt_request.flow_id != mgmt->u.action.u.s1g.variable[0]){
		ret = -1;
		goto out;
	}
	
	ieee80211_twt_set_teardown(sta);
out:
	return ret;
}

void ieee80211_rx_twt_action(struct ieee80211_sub_if_data *sdata,struct sta_info *sta,
			 struct sk_buff *skb)
{
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
//	struct ieee80211_local *local = sdata->local;


	if (!sta)
		goto out;

	switch (mgmt->u.action.u.s1g.action_code) {
	case ATBM_WLAN_S1G_TWT_SETUP:
		ieee80211_twt_rx_setup(sdata, sta, skb);
		break;
	case ATBM_WLAN_S1G_TWT_TEARDOWN:
		ieee80211_twt_rx_teardown(sdata,sta,skb);
		break;
	default:
		break;
	}
out:
	return;
}
static void ieee80211_twt_state_change(struct sta_twt_mlme *twt,enum atbm_ieee80211_twt_setup_cmd cmd)  
{
	switch(cmd){
	case ATBM_TWT_SETUP_CMD_REQUEST:
		ieee80211_twt_state_set(twt,STA_TWT_STATE_REQUEST);
		break;
	case ATBM_TWT_SETUP_CMD_SUGGEST:
		ieee80211_twt_state_set(twt,STA_TWT_STATE_SUGGEST);
		break;
	case ATBM_TWT_SETUP_CMD_DEMAND:
		ieee80211_twt_state_set(twt,STA_TWT_STATE_DEMAND);
		break;
	case ATBM_TWT_SETUP_CMD_GROUPING:
		/*not support*/
		ieee80211_twt_state_set(twt,STA_TWT_STATE_REJECT);
		break;
	case ATBM_TWT_SETUP_CMD_ACCEPT:
		ieee80211_twt_state_set(twt,STA_TWT_STATE_ACCEPT);
		break;
	case ATBM_TWT_SETUP_CMD_ALTERNATE:
		/*
		*accept the responding params
		*/
		ieee80211_twt_state_set(twt,STA_TWT_STATE_ACCEPT);
		break;
	case ATBM_TWT_SETUP_CMD_DICTATE:
		/*
		*accept the responding params
		*/
		ieee80211_twt_state_set(twt,STA_TWT_STATE_ACCEPT);
		break;
	case ATBM_TWT_SETUP_CMD_REJECT:
		ieee80211_twt_state_set(twt,STA_TWT_STATE_IDLE);
		break;
	default:
		ieee80211_twt_state_set(twt,STA_TWT_STATE_IDLE);
		break;
	}
}
bool ieee80211_twt_sta_requesting(struct sta_info *sta,struct ieee80211_twt_request_params *twt_request)
{
	struct sta_twt_mlme *twt = &sta->twt;
	struct sk_buff *skb;
	int len = ATBM_IEEE80211_MIN_ACTION_SIZE + 4 + sizeof(struct atbm_ieee80211_twt_setup) + 2 + 8 + 1+ 2 + 1 + 64;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct atbm_ieee80211_mgmt *mgmt;
	
	if(twt_request->control){
		return false;
	}
	
	if(twt->state >= STA_TWT_STATE_ACCEPT){
		atbm_printk_err("twt state err(%d)\n",twt->state);
		goto fail;
	}

	ieee80211_twt_state_change(twt,twt_request->setup_cmd);

	if(twt->state == STA_TWT_STATE_REJECT || twt->state == STA_TWT_STATE_IDLE){
		atbm_printk_err("twt state change err(%d)(%d)\n",twt->state,twt_request->setup_cmd);
		goto fail;
	}
	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom + len);
	if (!skb)
		goto fail;
	
	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);
	
	if(twt->state == STA_TWT_STATE_REQUEST){
		len = offsetof(struct atbm_ieee80211_mgmt,u.action.u.s1g_twt_request.channel) + 1;
	}else {
		len = offsetof(struct atbm_ieee80211_mgmt,u.action.u.s1g_twt_request_target.channel) + 1;
	}
	/*make header*/
	mgmt = skb_put(skb, len);
	memset(mgmt,0,len);

	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	memcpy(mgmt->da, sta->sta.addr, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, sta->sta.addr, ETH_ALEN);
	mgmt->u.action.category = ATBM_WLAN_CATEGORY_S1G;
	mgmt->u.action.u.s1g.action_code = ATBM_WLAN_S1G_TWT_SETUP;

	mgmt->u.action.u.s1g_twt_request.dialog_token = twt_request->token;
	mgmt->u.action.u.s1g_twt_request.twt_id       = ATBM_WLAN_EID_S1G_TWT;
	mgmt->u.action.u.s1g_twt_request.control = 0;
	mgmt->u.action.u.s1g_twt_request.request_type = cpu_to_le16(ATBM_IEEE80211_TWT_REQTYPE_REQUEST);
	mgmt->u.action.u.s1g_twt_request.request_type |= cpu_to_le16(twt_request->setup_cmd << 1);
	mgmt->u.action.u.s1g_twt_request.request_type |= cpu_to_le16(twt_request->exp <<10 );
	mgmt->u.action.u.s1g_twt_request.request_type |= cpu_to_le16(twt_request->flow_id << 7 );
	
	if(twt_request->implicit){
		mgmt->u.action.u.s1g_twt_request.request_type |= cpu_to_le16(ATBM_IEEE80211_TWT_REQTYPE_IMPLICIT);
	}	
	if(twt_request->flow_type){
		mgmt->u.action.u.s1g_twt_request.request_type |= cpu_to_le16(ATBM_IEEE80211_TWT_REQTYPE_FLOWTYPE);
	}
	if(twt_request->trigger){
		mgmt->u.action.u.s1g_twt_request.request_type |= cpu_to_le16(ATBM_IEEE80211_TWT_REQTYPE_TRIGGER);
	}
	
	if(twt->state == STA_TWT_STATE_REQUEST){
		/*without target wake up*/
		mgmt->u.action.u.s1g_twt_request.len = 7;
		mgmt->u.action.u.s1g_twt_request.min_twt_dur = cpu_to_le16(twt_request->duration);
		mgmt->u.action.u.s1g_twt_request.mantissa = cpu_to_le16(twt_request->mantissa);
	}else {
		/*with tartger wake up*/
		mgmt->u.action.u.s1g_twt_request_target.len = 15;
		mgmt->u.action.u.s1g_twt_request_target.twt = cpu_to_le64(twt_request->twt);
		mgmt->u.action.u.s1g_twt_request_target.min_twt_dur = cpu_to_le16(twt_request->duration);
		mgmt->u.action.u.s1g_twt_request_target.mantissa = cpu_to_le16(twt_request->mantissa);
	}
	
	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
					IEEE80211_TX_CTL_REQ_TX_STATUS;
	ieee80211_tx_skb(sdata, skb);
	return true;
fail:
	return false;
}
static enum work_action ieee80211_twt_sta_request_work_start(struct ieee80211_work *wk)
{
	enum work_action action = WORK_ACT_NONE;
	struct sta_info *sta;
	struct sta_twt_mlme *statwt;
	
	mutex_lock(&wk->sdata->local->sta_mtx);
	

	sta = sta_info_get_bss(wk->sdata, wk->filter_bssid);

	if(sta == NULL){
		action = WORK_ACT_TIMEOUT;
		goto exit;
	}
	
	statwt = &sta->twt;
	
	wk->twt_request.tries ++ ;
	atbm_printk_err("[%pM] twt work start(%d)\n",sta->sta.addr,wk->twt_request.tries);
	if(wk->twt_request.tries >= wk->twt_request.retry_max){
		ieee80211_twt_state_set(statwt,STA_TWT_STATE_IDLE);
		action = WORK_ACT_TIMEOUT;
		goto exit;
	}

	if(ieee80211_twt_sta_requesting(sta,&wk->twt_request.twt) == false){
		action = WORK_ACT_TIMEOUT;
	}else {
		wk->timeout = jiffies + HZ/2;
	}
exit:
	mutex_unlock(&wk->sdata->local->sta_mtx);

	return action;
}
static enum work_action ieee80211_twt_sta_request_work_rx(struct ieee80211_work *wk,struct sk_buff *skb)
{
	struct sta_info *sta;
	
	atbm_printk_err("[%s]: rx twt action\n",wk->sdata->name);
	mutex_lock(&wk->sdata->local->sta_mtx);


	sta = sta_info_get_bss(wk->sdata, wk->filter_bssid);

	if(sta == NULL){
		goto exit;
	}
	if(ieee80211_twt_rx_setup(wk->sdata,sta,skb)){
		goto exit;
	}
	
exit:
	mutex_unlock(&wk->sdata->local->sta_mtx);

	return WORK_ACT_DONE;
}
static enum work_done_result ieee80211_twt_sta_request_work_done(struct ieee80211_work *wk,
				      struct sk_buff *skb)
{
	struct sta_info *sta;
	struct sta_twt_mlme *statwt;

	atbm_printk_err("%s\n",__func__);
	
	mutex_lock(&wk->sdata->local->sta_mtx);
	
	sta = sta_info_get_bss(wk->sdata, wk->filter_bssid);

	if(sta == NULL){
		goto exit;
	}
	
	statwt = &sta->twt;

	if(skb == NULL){
		ieee80211_twt_state_set(statwt,STA_TWT_STATE_IDLE);
		goto exit;
	}
exit:
	mutex_unlock(&wk->sdata->local->sta_mtx);

	return WORK_DONE_DESTROY;
}
static bool ieee80211_twt_sta_request_work_filter(struct ieee80211_work *wk,struct sk_buff *skb)
{
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	
	if (!ieee80211_is_action(mgmt->frame_control))
		return false;

	/* drop too small frames */
	if (skb->len < ATBM_IEEE80211_MIN_ACTION_SIZE)
		return false;

	return _ieee80211_process_rx_twt_action(skb);
}

bool ieee80211_twt_sta_request_work(struct sta_info *sta,struct ieee80211_twt_request_params *twt_request)
{
	struct ieee80211_work *wk = NULL;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(sta->local, sta->sdata);
	wk = atbm_kzalloc(sizeof(struct ieee80211_work), GFP_KERNEL);

	if(wk == NULL){
		goto err;
	}

	wk->type  	= IEEE80211_WORK_TWT;
	wk->sdata 	= sta->sdata;
	wk->start 	= ieee80211_twt_sta_request_work_start;
	wk->rx      = ieee80211_twt_sta_request_work_rx;
	wk->done    = ieee80211_twt_sta_request_work_done;
	wk->filter  = ieee80211_twt_sta_request_work_filter;
	wk->twt_request.retry_max = 5;
	wk->chan    = chan_state->oper_channel;
	wk->chan_type = chan_state->_oper_channel_type;
	wk->chan_mode = CHAN_MODE_FIXED;
	memcpy(&wk->twt_request.twt,twt_request,sizeof(struct ieee80211_twt_request_params));
	memcpy(wk->filter_bssid,sta->sta.addr,6);
	memcpy(wk->filter_sa,   sta->sta.addr,6);
	ieee80211_add_work(wk);
	return true;
err:
	if(wk)
		atbm_kfree(wk);
	 return false;
}
bool ieee80211_twt_sta_teardown(struct sta_info *sta)
{
	struct sta_twt_mlme *twt = &sta->twt;
	struct sk_buff *skb;
	struct ieee80211_sub_if_data *sdata = sta->sdata;
	struct ieee80211_local *local = sdata->local;
	struct atbm_ieee80211_mgmt *mgmt;
	u8 *id;

	skb = atbm_dev_alloc_skb(local->hw.extra_tx_headroom +
			    IEEE80211_MIN_ACTION_SIZE + 2);
	if (!skb)
		goto err;

	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = skb_put_zero(skb, IEEE80211_MIN_ACTION_SIZE + 2);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	memcpy(mgmt->da, sta->sta.addr, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, sta->sta.addr, ETH_ALEN);

	mgmt->u.action.category = ATBM_WLAN_CATEGORY_S1G;
	mgmt->u.action.u.s1g.action_code = ATBM_WLAN_S1G_TWT_TEARDOWN;
	id = (u8 *)mgmt->u.action.u.s1g.variable;
	*id = twt->twt_request.flow_id;

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
					IEEE80211_TX_CTL_REQ_TX_STATUS;
	ieee80211_tx_skb(sdata, skb);

	ieee80211_twt_set_teardown(sta);
	
	return true;
err:
	return false;
}
static enum work_action ieee80211_twt_sta_suspend_work_start(struct ieee80211_work *wk)
{
	struct sta_info *sta;
	enum ieee80211_sta_twt_suspend_state state = wk->twt_suspend.state;
	
	mutex_lock(&wk->sdata->local->sta_mtx);
	

	sta = sta_info_get_bss(wk->sdata, wk->filter_bssid);

	if(sta == NULL){
		goto exit;
	}

	if(sta->twt.state == STA_TWT_STATE_IDLE){
		goto  exit;
	}
	atbm_printk_always("twt state notify[%d]\n",state);
	switch(state){
	case STA_TWT_SUSPEND_STATE_IDLE:
	case STA_TWT_SUSPEND_STATE_WAIT_SP_START_SETP1:
	case STA_TWT_SUSPEND_STATE_WAIT_SP_START_SETP2:
	case STA_TWT_SUSPEND_STATE_SP_START:
		/*
		*release buffed package
		*/
		ieee80211_twt_state_set(&sta->twt,STA_TWT_STATE_SP_START);
		break;
	case STA_TWT_SUSPEND_STATE_SP_INTERVAL_PREPARE:
	case STA_TWT_SUSPEND_STATE_SP_INTERVAL_CHANGE_PS:
	case STA_TWT_SUSPEND_STATE_SP_INTERVAL:
		/*
		*start  buff package
		*/
		ieee80211_twt_state_set(&sta->twt,STA_TWT_STATE_SP_INTERVAL);
		break;
	default:
		break;
	}
exit:
	mutex_unlock(&wk->sdata->local->sta_mtx);

	return WORK_ACT_TIMEOUT;
}
static enum work_done_result ieee80211_twt_sta_suspend_work_done(struct ieee80211_work *wk,
				      struct sk_buff *skb)
{
	return WORK_DONE_DESTROY;
}
bool ieee80211_twt_sta_suspend_state_work(struct ieee80211_sub_if_data *sdata,enum ieee80211_sta_twt_suspend_state state)
{
	struct ieee80211_work *wk = NULL;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(sdata->local, sdata);
	wk = atbm_kzalloc(sizeof(struct ieee80211_work), GFP_KERNEL);

	if(wk == NULL){
		goto err;
	}

	wk->type  	= IEEE80211_WORK_TWT_SUSPEND;
	wk->sdata 	= sdata;
	wk->start 	= ieee80211_twt_sta_suspend_work_start;
	wk->done    = ieee80211_twt_sta_suspend_work_done;
	wk->chan    = chan_state->oper_channel;
	wk->chan_type = chan_state->_oper_channel_type;
	wk->chan_mode = CHAN_MODE_FIXED;
	wk->twt_suspend.state = state;
	
	memcpy(wk->filter_bssid,sdata->vif.bss_conf.bssid,6);
	memcpy(wk->filter_sa,   sdata->vif.bss_conf.bssid,6);
	
	ieee80211_add_work(wk);
	
	return true;
err:	
	if(wk)
		atbm_kfree(wk);
	return false;
}
void ieee80211_twt_sta_init(struct sta_info *sta)
{
	sta->twt.state = STA_TWT_STATE_IDLE;
	spin_lock_init(&sta->twt.lock);
}

void ieee80211_twt_sta_deinit(struct sta_info *sta)
{
	if(sta->twt.state == STA_TWT_STATE_IDLE){
		return;
	}
	ieee80211_twt_sta_teardown(sta);
}
