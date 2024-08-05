/*
 * mac80211 - channel management
 */

#include <linux/nl80211.h>
#include "ieee80211_i.h"
#ifdef CONFIG_ATBM_SUPPORT_CSA
#include "driver-ops.h"
struct ieee80211_csa_status
{
	u8 n_suces;
	u8 n_fails;
};
#endif

static enum ieee80211_chan_mode
__ieee80211_get_channel_mode(struct ieee80211_local *local,
			     struct ieee80211_sub_if_data *ignore)
{
	struct ieee80211_sub_if_data *sdata;

	lockdep_assert_held(&local->iflist_mtx);

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata == ignore)
			continue;

		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_MONITOR){
			if(local->monitors == 0)
				continue;
			return CHAN_MODE_HOPPING;
		}
		if (sdata->vif.type == NL80211_IFTYPE_STATION &&
		    !sdata->u.mgd.associated)
			continue;
#ifdef CONFIG_ATBM_SUPPORT_IBSS
		if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
			if (!sdata->u.ibss.ssid_len)
				continue;
			if (!sdata->u.ibss.fixed_channel)
				return CHAN_MODE_HOPPING;
		}
#endif
#ifdef CONFIG_ATBM_SUPPORT_CSA
		if(sdata->csa_state != IEEE80211_CSA_MLME_STATE_IDLE){
			return CHAN_MODE_HOPPING;
		}
#endif
		if (sdata->vif.type == NL80211_IFTYPE_AP &&
		    !sdata->u.ap.beacon)
			continue;

		return CHAN_MODE_FIXED;
	}

	return CHAN_MODE_UNDEFINED;
}

enum ieee80211_chan_mode
ieee80211_get_channel_mode(struct ieee80211_local *local,
			   struct ieee80211_sub_if_data *ignore)
{
	enum ieee80211_chan_mode mode;

	mutex_lock(&local->iflist_mtx);
	mode = __ieee80211_get_channel_mode(local, ignore);
	mutex_unlock(&local->iflist_mtx);

	return mode;
}

bool ieee80211_set_channel_type(struct ieee80211_local *local,
				struct ieee80211_sub_if_data *sdata,
				enum nl80211_channel_type chantype)
{
	struct ieee80211_channel_state *chan_state;
	struct ieee80211_sub_if_data *tmp;
	enum nl80211_channel_type superchan = NL80211_CHAN_NO_HT;
	bool result;

	mutex_lock(&local->iflist_mtx);
#ifdef CONFIG_ATBM_SUPPORT_MULTI_CHANNEL
	if (local->hw.flags & IEEE80211_HW_SUPPORTS_MULTI_CHANNEL) {
		/* XXX: COMBO: TBD - is this ok? */
		BUG_ON(!sdata);
		sdata->chan_state._oper_channel_type = chantype;
		sdata->vif.bss_conf.channel_type = chantype;
		result = true;
		goto out;
	}
#endif
	chan_state = ieee80211_get_channel_state(local, sdata);

	list_for_each_entry(tmp, &local->interfaces, list) {
		if (tmp == sdata)
			continue;

		if (!ieee80211_sdata_running(tmp))
			continue;

		switch (tmp->vif.bss_conf.channel_type) {
		case NL80211_CHAN_NO_HT:
		case NL80211_CHAN_HT20:
			if (superchan > tmp->vif.bss_conf.channel_type)
				break;

			superchan = tmp->vif.bss_conf.channel_type;
			break;
		case NL80211_CHAN_HT40PLUS:
			WARN_ON(superchan == NL80211_CHAN_HT40MINUS);
			superchan = NL80211_CHAN_HT40PLUS;
			break;
		case NL80211_CHAN_HT40MINUS:
			WARN_ON(superchan == NL80211_CHAN_HT40PLUS);
			superchan = NL80211_CHAN_HT40MINUS;
			break;
		}
	}

	switch (superchan) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		/*
		 * allow any change that doesn't go to no-HT
		 * (if it already is no-HT no change is needed)
		 */
		if (chantype == NL80211_CHAN_NO_HT)
			break;
		superchan = chantype;
		break;
	case NL80211_CHAN_HT40PLUS:
	case NL80211_CHAN_HT40MINUS:
		/* allow smaller bandwidth and same */
		if (chantype == NL80211_CHAN_NO_HT)
			break;
		if (chantype == NL80211_CHAN_HT20)
			break;
		if (superchan == chantype)
			break;
		result = false;
		goto out;
	}

	chan_state->_oper_channel_type = superchan;
	if (sdata)
	{
		sdata->vif.bss_conf.channel_type = chantype;
	}
	result = true;
 out:
	mutex_unlock(&local->iflist_mtx);

	return result;
}

bool ieee80211_chan_ct_coexists(enum nl80211_channel_type wk_ct,
				       enum nl80211_channel_type oper_ct)
{
	switch (wk_ct) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		return true;
	case NL80211_CHAN_HT40MINUS:
	case NL80211_CHAN_HT40PLUS:
		if((oper_ct == NL80211_CHAN_NO_HT) || (oper_ct == NL80211_CHAN_HT20))
			return true;
		return (wk_ct == oper_ct);
	}
	WARN_ON(1); /* shouldn't get here */
	return false;
}

#ifdef CONFIG_ATBM_SUPPORT_CSA
static int  ieee80211_csa_running(struct ieee80211_local *local)
{
	return !list_empty(&local->csa_pending) || !list_empty(&local->csa_req);
}
static void ieee80211_chan_csa_timer(unsigned long arg)
{
	struct ieee80211_sub_if_data *sdata = (struct ieee80211_sub_if_data *)arg;

	atbm_printk_csa("[%s]:csa timeout[%d]\n",sdata->name,sdata->csa_state);
	
	if(sdata->csa_state != IEEE80211_CSA_MLME_STATE_START)
		return;

	sdata->csa_state = IEEE80211_CSA_MLME_STATE_TIMEOUT;
	
	ieee80211_queue_work(&sdata->local->hw, &sdata->local->csa_work);
} 
static int ieee80211_chan_csa_start(struct ieee80211_local *local,
				struct ieee80211_csa_work *csa_req)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(csa_req->vif);
	

	if(!ieee80211_sdata_running(sdata)){
		goto fail;
	}
	
	if(ieee80211_get_channel_mode(local,sdata) == CHAN_MODE_FIXED){
		goto fail;
	}
	
	if(csa_req->block)
		ieee80211_stop_queues_by_reason(&sdata->local->hw,IEEE80211_QUEUE_STOP_REASON_CSA);
	
	switch(sdata->vif.type){
	case NL80211_IFTYPE_STATION:
		{
			unsigned long timeout = csa_req->mactime + 
							msecs_to_jiffies(csa_req->count * sdata->vif.bss_conf.beacon_int);
			
			if(csa_req->count <= 1){
				sdata->csa_state = IEEE80211_CSA_MLME_STATE_FINISHED;
				break;
			}
			
			if(!time_is_after_jiffies(timeout)){
				sdata->csa_state = IEEE80211_CSA_MLME_STATE_FINISHED;
				break;
			}

			sdata->csa_state = IEEE80211_CSA_MLME_STATE_START;
			smp_mb();
			atbm_mod_timer(&sdata->csa_timer,timeout);
			break;
		}
	case NL80211_IFTYPE_AP:
		{
			struct ieee80211_csa_request request;

			
			request.chan     = csa_req->chan;
			request.chantype = csa_req->chan_type;
			request.start    = true;
			request.cfg80211 = csa_req->cfg80211;
			request.ie.count = csa_req->count;
			request.ie.mode  = csa_req->block;
			request.ie.new_ch_num = channel_hw_value(csa_req->chan);

			atbm_printk_csa("[%s] ap csa[%d][%d][%d][%d]\n",sdata->name,request.chantype,
							request.ie.new_ch_num,request.ie.count,request.ie.mode);

			csa_req->mactime = jiffies;
			sdata->csa_state = IEEE80211_CSA_MLME_STATE_START;
			smp_mb();
			atbm_mod_timer(&sdata->csa_timer,csa_req->mactime + msecs_to_jiffies((csa_req->count + 5) * (sdata->vif.bss_conf.beacon_int)));
			
			if(drv_do_csa(sdata->local,sdata,&request)){
				goto fail;
			}

			break;
		}
	default:
		atbm_printk_err("%s csa type err(%d)\n",sdata->name,sdata->vif.type);
		BUG_ON(1);
		break;
	}
	return 0;
fail:
	sdata->csa_state = IEEE80211_CSA_MLME_STATE_FAIL;
	return -1;
}
static void ieee80211_csa_beacon_update_after(struct ieee80211_sub_if_data *sdata)
{
	struct beacon_data *beacon_after = NULL;
	struct proberesp_data *proberesp_after = NULL;
	struct ieee80211_csa_mlme *csa_mlme = &sdata->u.ap.csa_mlme;
	
	beacon_after    = rcu_dereference(csa_mlme->beacon_after);
	proberesp_after = rcu_dereference(csa_mlme->proberesp_after);

	rcu_assign_pointer(csa_mlme->beacon_after,NULL);
	rcu_assign_pointer(csa_mlme->proberesp_after,NULL);
	
	if(beacon_after){
		struct beacon_data *old = rcu_dereference(sdata->u.ap.beacon);

		rcu_assign_pointer(sdata->u.ap.beacon, beacon_after);

		synchronize_rcu();
		if(old)
			atbm_kfree(old);
	}else {
		struct beacon_data *old = rcu_dereference(sdata->u.ap.beacon);
		struct ieee802_atbm_11_elems elems;
		struct atbm_ieee80211_mgmt* mgmt = (struct atbm_ieee80211_mgmt*)old->head;

		ieee802_11_parse_elems(mgmt->u.beacon.variable,old->head_len - (mgmt->u.beacon.variable - old->head) ,false, &elems,NULL,NULL);

		if (elems.ds_params && elems.ds_params_len == 1){
			elems.ds_params[0] = channel_hw_value(csa_mlme->chan);
		}else {
			atbm_printk_csa("beacon_after not find ds\n");
		}
	}

	if(proberesp_after){
#ifdef ATBM_PROBE_RESP_EXTRA_IE
		struct proberesp_data *old = rcu_dereference(sdata->u.ap.proberesp);

		rcu_assign_pointer(sdata->u.ap.proberesp, proberesp_after);
		synchronize_rcu();

		if(old)
			atbm_kfree(old);
#endif
	}else {
		struct proberesp_data *old = rcu_dereference(sdata->u.ap.proberesp);
		struct ieee802_atbm_11_elems elems;
		struct atbm_ieee80211_mgmt* mgmt = (struct atbm_ieee80211_mgmt*)old->head;

		
		ieee802_11_parse_elems(mgmt->u.probe_resp.variable,old->head_len - (mgmt->u.probe_resp.variable - old->head) ,false, &elems,NULL,NULL);

		if (elems.ds_params && elems.ds_params_len == 1){
			elems.ds_params[0] = channel_hw_value(csa_mlme->chan);
		}else {
			atbm_printk_csa("proberesp_after not find ds\n");
		}
	}
	ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_BEACON_ENABLED |
						BSS_CHANGED_BEACON |
						BSS_CHANGED_SSID);
}
static enum work_done_result ieee80211_csa_completed_notify_wk_done(struct ieee80211_work* wk,
	struct sk_buff* skb)
{
#if (LINUX_VERSION_IS_GEQ_OR_CPTCFG(3,12,0))
	struct ieee80211_sub_if_data *sdata = wk->sdata;
	struct wireless_dev* wdev = sdata->dev->ieee80211_ptr;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(sdata->local, sdata);
	struct cfg80211_chan_def chandef;

	
	cfg80211_chandef_create(&chandef,chan_state->oper_channel,sdata->vif.bss_conf.channel_type);
	
	atbm_wdev_lock(wdev);
	ieee80211_ch_switch_notify(sdata,&chandef);
	atbm_wdev_unlock(wdev);
#endif

	return WORK_DONE_DESTROY;
}
static void _ieee80211_csa_completed_notify(struct ieee80211_local *local,struct ieee80211_sub_if_data *sdata)
{		
	switch(sdata->csa_state){
	case IEEE80211_CSA_MLME_STATE_IDLE:
		break;
	case IEEE80211_CSA_MLME_STATE_WK:
		break;
	case IEEE80211_CSA_MLME_STATE_START:
		break;
	case IEEE80211_CSA_MLME_STATE_STOP:
		break;
	case IEEE80211_CSA_MLME_STATE_TIMEOUT:
	case IEEE80211_CSA_MLME_STATE_FAIL:
	case IEEE80211_CSA_MLME_STATE_FINISHED:{
		
		struct ieee80211_work *wk = atbm_kzalloc(sizeof(struct ieee80211_work),GFP_KERNEL);

		if(wk == NULL){
			break;
		}

		wk->sdata = sdata;
		wk->type  = IEEE80211_WORK_CSA_NOTIFY;
		wk->done  = ieee80211_csa_completed_notify_wk_done;
		memcpy(wk->filter_bssid,sdata->vif.bss_conf.bssid,6);
		memcpy(wk->filter_sa,sdata->vif.bss_conf.bssid,6);
		ieee80211_add_work(wk);	
		break;
		}
	}

	sdata->csa_state = IEEE80211_CSA_MLME_STATE_IDLE;
}
static void ieee80211_csa_completed_notify(struct ieee80211_local *local)
{
	struct ieee80211_csa_work *csawork;

	list_for_each_entry(csawork, &local->csa_req, list){
		struct ieee80211_sub_if_data *sdata  = vif_to_sdata(csawork->vif);
		_ieee80211_csa_completed_notify(local,sdata);
	}
}
static bool ieee80211_chan_complete_csa(struct ieee80211_csa_work *csa_req)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(csa_req->vif);
	bool success = false;
	
	switch(sdata->vif.type){
	case NL80211_IFTYPE_STATION:
		if(sdata->csa_state >= IEEE80211_CSA_MLME_STATE_START){
			success = true;
		}
		break;
	case NL80211_IFTYPE_AP:

		switch(sdata->csa_state){
		case IEEE80211_CSA_MLME_STATE_IDLE:
		case IEEE80211_CSA_MLME_STATE_WK:
		case IEEE80211_CSA_MLME_STATE_START:
			success = false;
			atbm_printk_err("ap csa state[%d] is false\n",sdata->csa_state);
		case IEEE80211_CSA_MLME_STATE_TIMEOUT:
			/*timeout can set true*/
			success = true;
		case IEEE80211_CSA_MLME_STATE_FAIL:
		case IEEE80211_CSA_MLME_STATE_STOP:
			{
				struct ieee80211_csa_request request;
				/*
				*cancle channel sw,this is very diffical,because some 
				*stations maybe has channged channel.
				*/
				memset(&request,0,sizeof(struct ieee80211_csa_request));
				request.start = false;
				drv_do_csa(sdata->local,sdata,&request);
			}
			break;
		case IEEE80211_CSA_MLME_STATE_FINISHED:
			success = true;
		}
		break;		
	default:
		atbm_printk_err("%s csa type err(%d)\n",sdata->name,sdata->vif.type);
		BUG_ON(1);
		break;
	}		
	if(csa_req->block)
		ieee80211_wake_queues_by_reason(&sdata->local->hw,IEEE80211_QUEUE_STOP_REASON_CSA);

	atbm_printk_csa("[%s-%s]:%d\n",__func__,sdata->name,success);

	return success;
}
static void ieee80211_chan_pre_complete_csa(struct ieee80211_local *local,struct ieee80211_csa_status *status)
{
	
	struct ieee80211_csa_work *csa_req;

	status->n_fails = 0;
	status->n_suces = 0;
	
	list_for_each_entry(csa_req, &local->csa_req, list){
		
		if(ieee80211_chan_complete_csa(csa_req) == true){
			status->n_suces ++;
		}else {
			status->n_fails ++;
		}
	}
}
static void ieee80211_chan_post_complete_csa(struct ieee80211_local *local,struct ieee80211_csa_status *status)
{
	struct ieee80211_channel_state *chan_state;
	struct ieee80211_csa_work *csa_req;
	struct ieee80211_sub_if_data *sdata;
	
	if(status->n_fails){
		atbm_printk_csa("csa fail,do nothing\n");
		return;
	}

	if(status->n_suces == 0){
		atbm_printk_csa("csa empty,do nothing\n");
		return;
	}
	
	csa_req = list_first_entry(&local->csa_req, struct ieee80211_csa_work,list);
	
	sdata = vif_to_sdata(csa_req->vif);
	/*
	*set channel,only support one channel
	*/
	chan_state =  &local->chan_state;

	chan_state->tmp_channel = csa_req->chan;
	ieee80211_hw_config(local, 0);
	chan_state->oper_channel = csa_req->chan;
	chan_state->tmp_channel  = NULL;
	ieee80211_hw_config(local, 0);

	list_for_each_entry(csa_req, &local->csa_req, list){
		
		sdata = vif_to_sdata(csa_req->vif);

		ieee80211_set_channel_type(local,sdata,csa_req->chan_type);
		
		switch(sdata->vif.type){
		case NL80211_IFTYPE_STATION:
			atbm_printk_csa("sta [%s]:csa done\n",sdata->name);
			break;
		case NL80211_IFTYPE_AP:
			atbm_printk_csa("ap [%s]:csa done\n",sdata->name);
			ieee80211_csa_beacon_update_after(sdata);
			break;		
		default:
			atbm_printk_err("%s csa type err(%d)\n",sdata->name,sdata->vif.type);
			BUG_ON(1);
			break;
		}
	}

	if(status->n_suces == 1){
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_HT_CHANNEL_TYPE);
	}else {
		local->ops->do_csa_concurrent(&local->hw,&local->csa_req);
	}
}
static void __ieee80211_csa_completed(struct ieee80211_local *local)
{
	struct ieee80211_csa_status status;
	
	ieee80211_chan_pre_complete_csa(local,&status);
	ieee80211_chan_post_complete_csa(local,&status);
	ieee80211_csa_completed_notify(local);
	ieee80211_csa_flush(local);

	local->next_csa_state = CSA_IDLE;
	
	WARN_ON(list_empty(&local->csa_req) == 0);
	WARN_ON(list_empty(&local->csa_pending) == 0);
	
#ifdef CONFIG_ATBM_SUPPORT_P2P
	ieee80211_start_next_roc(local);
#endif
	ieee80211_queue_work(&local->hw, &local->work_work);
	
	ieee880211_start_pending_scan(local);
}
static void _ieee80211_csa_process_work(struct ieee80211_local *local)
{
	struct ieee80211_csa_work *csawork, *tmp;
	bool  again = false;
	struct ieee80211_sub_if_data *sdata;
	enum ieee80211_csa_state next_csa_state;

	if(ieee80211_csa_busy(local) == 0){
		local->next_csa_state = CSA_IDLE;
		goto out;
	}
	
	do{
		again = false;
		
		switch (local->next_csa_state) {
		case CSA_WAIT_START:
			list_for_each_entry_safe(csawork,tmp,&local->csa_req, list){

				sdata  = vif_to_sdata(csawork->vif);

				switch(sdata->csa_state){
				case IEEE80211_CSA_MLME_STATE_IDLE:
				case IEEE80211_CSA_MLME_STATE_START:
				case IEEE80211_CSA_MLME_STATE_TIMEOUT:
				case IEEE80211_CSA_MLME_STATE_FAIL:
				case IEEE80211_CSA_MLME_STATE_FINISHED:
					BUG_ON(1);
					break;
				case IEEE80211_CSA_MLME_STATE_WK:
					atbm_printk_csa("%s:csa try start\n",sdata->name);
					ieee80211_chan_csa_start(local,csawork);
					break;
				case IEEE80211_CSA_MLME_STATE_STOP:
					atbm_printk_err("%s:csa start stop\n",sdata->name);
					list_del(&csawork->list);
					_ieee80211_csa_completed_notify(local,sdata);
					atbm_kfree(csawork);
					break;
				}
			}
			local->next_csa_state = CSA_WAIT_COMPLETE;
			again = true;
			break;
		case CSA_WAIT_COMPLETE:

			next_csa_state = CSA_IDLE;
			
			list_for_each_entry_safe(csawork,tmp,&local->csa_req, list){
				
				sdata  = vif_to_sdata(csawork->vif);

				if(!ieee80211_sdata_running(sdata)){
					atbm_printk_csa("[%s]:csa stop running\n",sdata->name);
					atbm_del_timer_sync(&sdata->csa_timer);
					sdata->csa_state = IEEE80211_CSA_MLME_STATE_STOP;
				}
				
				switch(sdata->csa_state){
				case IEEE80211_CSA_MLME_STATE_IDLE:
				case IEEE80211_CSA_MLME_STATE_WK:
					BUG_ON(1);
					break;
				case IEEE80211_CSA_MLME_STATE_START:
					next_csa_state = CSA_WAIT_COMPLETE;
					atbm_printk_csa("[%s]:csa running\n",sdata->name);
					break;
				case IEEE80211_CSA_MLME_STATE_TIMEOUT:
				case IEEE80211_CSA_MLME_STATE_FAIL:
				case IEEE80211_CSA_MLME_STATE_FINISHED:
					atbm_printk_csa("[%s] csa completed[%d]\n",sdata->name,sdata->csa_state);
					break;
				case IEEE80211_CSA_MLME_STATE_STOP:
					list_del(&csawork->list);
					ieee80211_chan_complete_csa(csawork);
					_ieee80211_csa_completed_notify(local,sdata);
					atbm_kfree(csawork);
					atbm_printk_csa("[%s] csa cancle[%d]\n",sdata->name,sdata->csa_state);
					break;
				}
			}
			
			local->next_csa_state = next_csa_state;

			if(local->next_csa_state == CSA_IDLE)
				goto out_complete;
			else
				goto out;	
		default:
			goto out;
		}
	}while(again == true);
		
	goto out;
out_complete:
	__ieee80211_csa_completed(local);
out:
	return;
}
static void ieee80211_csa_process_work(struct atbm_work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, csa_work);
	
	
	mutex_lock(&local->mtx);
	_ieee80211_csa_process_work(local);
	mutex_unlock(&local->mtx);
}
static struct ieee80211_csa_work *ieee880211_chan_csa_work_alloc(
				struct ieee80211_sub_if_data *sdata,struct ieee80211_csa_request *csa)
{
	struct ieee80211_csa_work *work;

	work = atbm_kzalloc(sizeof(struct ieee80211_csa_work),GFP_KERNEL);

	if(work){
		work->vif   = &sdata->vif;
		work->mactime = csa->mactime;
		work->block   = csa->ie.mode;
		work->count   = csa->ie.count;
		work->chan    = csa->chan;
		work->chan_type  = csa->chantype;
		work->cfg80211   = csa->cfg80211;
		

		sdata->csa_state = IEEE80211_CSA_MLME_STATE_IDLE;
	}

	return work;
}
static void ieee80211_chan_csa_concurrent_work_alloc(struct ieee80211_sub_if_data *sdata,
			struct ieee80211_csa_request *csa,struct list_head *works)
{
	/*
	*only support ap + sta/ ap + ap csa , so find ap mode
	*/
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sub_if_data *ap = NULL;
	struct ieee80211_sub_if_data *concurrent;
	struct ieee80211_csa_work *work;
	struct ieee80211_csa_work *work_depend;
	struct ieee80211_csa_request ap_csa;
	
	
	mutex_lock(&local->iflist_mtx);
	
	list_for_each_entry(concurrent, &local->interfaces, list) {
		if (concurrent == sdata)
			continue;

		if (!ieee80211_sdata_running(concurrent))
			continue;

		if (concurrent->vif.type != NL80211_IFTYPE_AP){
			continue;
		}

		if (concurrent->vif.type == NL80211_IFTYPE_AP &&
		    !concurrent->u.ap.beacon)
			continue;

		if(ieee80211_chan_ct_coexists(csa->chantype,concurrent->vif.bss_conf.channel_type) == false){
			atbm_printk_err("chantype[%d][%d] err\n",csa->chantype,concurrent->vif.bss_conf.channel_type);
			break;
		}

		ap = concurrent;
		break;
	}
	
	if(ap == NULL){
		goto work_err;
	}

	memset(&ap_csa,sizeof(ap_csa),0);

	ap_csa.chan     = csa->chan;
	ap_csa.chantype = ap->vif.bss_conf.channel_type;
	ap_csa.mactime  = csa->mactime;
	ap_csa.ie.mode  = csa->ie.mode;
	ap_csa.ie.count = csa->ie.count * sdata->vif.bss_conf.beacon_int / ap->vif.bss_conf.beacon_int;
	ap_csa.ie.new_ch_num = csa->ie.new_ch_num;

	if(ap_csa.ie.count <= 5){
		ap_csa.ie.count = 5;
	}

	work = ieee880211_chan_csa_ap_requst(ap,&ap_csa);

	if(work == NULL){
		goto work_err;
	}

	work_depend = ieee880211_chan_csa_work_alloc(sdata,csa);

	if(work_depend == NULL){
		goto work_depend_err;
	}

	list_add_tail(&work->list,works);
	list_add_tail(&work_depend->list,works);
	
	mutex_unlock(&local->iflist_mtx);
	
	return;
work_depend_err:
	ieee80211_ap_flush_csa(ap);
	atbm_kfree(work);
work_err:
	mutex_unlock(&local->iflist_mtx);
	return;
}
static void ieee80211_chan_prepare_csa_work(struct ieee80211_sub_if_data *sdata,struct ieee80211_csa_request *csa,struct list_head *works)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state;
	
	chan_state = ieee80211_get_channel_state(local, sdata);

	switch(ieee80211_get_channel_mode(local,sdata)){
	case CHAN_MODE_HOPPING:
		atbm_printk_err("channel hopping,exit csa\n");
		break;
	case CHAN_MODE_UNDEFINED:{
			struct ieee80211_csa_work *work = NULL;
			work = ieee880211_chan_csa_work_alloc(sdata,csa);
			if(work){
				atbm_printk_err("[%s] start csa\n",sdata->name);
				list_add_tail(&work->list,works);
			}else {
				atbm_printk_err("[%s] alloc work err",sdata->name);
			}
			break;
		}
	case CHAN_MODE_FIXED:
		/*we support sta+ap/ap+ap, so find ap interface*/
		ieee80211_chan_csa_concurrent_work_alloc(sdata,csa,works);
		break;
	}
}
static void __ieee80211_start_csa_work(struct ieee80211_local *local)
{
	struct ieee80211_csa_work *csawork;
	
	list_for_each_entry(csawork, &local->csa_req, list){
		struct ieee80211_sub_if_data *sdata = vif_to_sdata(csawork->vif);

		sdata->csa_state = IEEE80211_CSA_MLME_STATE_WK;
	}

	local->next_csa_state = CSA_WAIT_START;
	ieee80211_queue_work(&local->hw, &local->csa_work);
}
int ieee80211_start_csa_work(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_csa_request *csa)
{
	struct ieee80211_local *local = sdata->local;
	
	int ret = 0;
	struct list_head works;

	INIT_LIST_HEAD(&works);
	
	if(ieee80211_csa_running(local)){
		atbm_printk_err("csa_req hold \n");
		goto exit;
	}
	
	ieee80211_chan_prepare_csa_work(sdata,csa,&works);
exit:
	if(list_empty(&works)){
		ret = -1;
	}else {
		/*
		*work busy,so wait work complete,because work maybe change operation channel.
		*/		
		if(ieee80211_work_busy(local) == false){
			list_splice_tail_init(&works,&local->csa_req);
			/*
			*cancle scan,because scaning will cost long time
			*/
			__ieee80211_scan_cancel(local);
#ifdef CONFIG_ATBM_SUPPORT_P2P
			ieee80211_roc_purge_current(local);
#endif
			ieee80211_recalc_idle(local);
			
			__ieee80211_start_csa_work(local);
		}else {
			/*
			*pending csa work
			*/
			list_splice_tail_init(&works,&local->csa_pending);
		}
	}
	
	return ret;
}
int ieee80211_start_special_csa_work(struct ieee80211_sub_if_data *sdata,
				    struct ieee80211_csa_request *csa)
{
	struct sk_buff *skb = NULL;
	struct ieee80211_special_work_common *work_common;
	
	skb = atbm_dev_alloc_skb(sizeof(struct ieee80211_csa_request));

	if(skb == NULL){
		goto err;
	}

	skb->pkt_type = IEEE80211_SPECIAL_CHANGE_CHANNEL_FREQ;
	work_common = (struct ieee80211_special_work_common*)skb->cb;
	memset(work_common,0,sizeof(struct ieee80211_special_work_common));
	work_common->req_sdata = sdata;
	memcpy(skb->data,csa,sizeof(struct ieee80211_csa_request));;
	atbm_skb_queue_tail(&sdata->local->special_req_list, skb);
	atbm_schedule_work(&sdata->local->special_work);

	return 0;
err:
	return -1;
}

void ieee80211_chan_start_pending_csa(struct ieee80211_local *local)
{
	if(!list_empty(&local->csa_pending)){
		list_splice_tail_init(&local->csa_pending,&local->csa_req);
		atbm_printk_csa("start pending csa\n");
		__ieee80211_start_csa_work(local);
	}
}
struct ieee80211_csa_work *ieee880211_chan_csa_ap_requst(
				struct ieee80211_sub_if_data *sdata,struct ieee80211_csa_request *csa)
{
	struct ieee80211_csa_work *work = NULL;
	struct ieee80211_csa_mlme *csa_mlme;

	if(sdata->vif.type != NL80211_IFTYPE_AP){
		goto err;
	}

	work = ieee880211_chan_csa_work_alloc(sdata,csa);;

	if(work == NULL){
		goto err;
	}

	ieee80211_ap_flush_csa(sdata);
	
	csa_mlme = &sdata->u.ap.csa_mlme;

	csa_mlme->block_tx = csa->ie.mode;
	csa_mlme->count    = csa->ie.count;
	csa_mlme->chan     = csa->chan;

	
	sdata->csa_state = IEEE80211_CSA_MLME_STATE_WK;

	return work;
err:
	return NULL;
}

void ieee80211_ap_flush_csa(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_csa_mlme *csa_mlme = &sdata->u.ap.csa_mlme;
	struct beacon_data *csa_beacon = NULL;
	struct beacon_data *beacon_after = NULL;
	struct proberesp_data *csa_proberesp = NULL;
	struct proberesp_data *proberesp_after = NULL;
	
	csa_beacon      = rcu_dereference(csa_mlme->csa_beacon);
	beacon_after    = rcu_dereference(csa_mlme->beacon_after);
	csa_proberesp   = rcu_dereference(csa_mlme->csa_proberesp);
	proberesp_after = rcu_dereference(csa_mlme->proberesp_after);

	RCU_INIT_POINTER(csa_mlme->csa_beacon,NULL);
	RCU_INIT_POINTER(csa_mlme->beacon_after,NULL);
	RCU_INIT_POINTER(csa_mlme->csa_proberesp,NULL);
	RCU_INIT_POINTER(csa_mlme->proberesp_after,NULL);

	if(csa_beacon)
		atbm_kfree(csa_beacon);
	if(beacon_after)
		atbm_kfree(beacon_after);
	if(csa_proberesp)
		atbm_kfree(csa_proberesp);
	if(proberesp_after)
		atbm_kfree(proberesp_after);
	memset(csa_mlme,0,sizeof(struct ieee80211_csa_mlme));
	
	sdata->csa_state = IEEE80211_CSA_MLME_STATE_IDLE;
}

int ieee80211_csa_busy(struct ieee80211_local *local)
{
	return !list_empty(&local->csa_req);
}

void ieee80211_csa_flush(struct ieee80211_local *local)
{
	while(!list_empty(&local->csa_req)){
		struct ieee80211_csa_work *csawork =
			list_first_entry(&local->csa_req, struct ieee80211_csa_work,
			list);
		list_del(&csawork->list);
		atbm_kfree(csawork);
	}

	while(!list_empty(&local->csa_pending)){
		struct ieee80211_csa_work *csawork =
			list_first_entry(&local->csa_pending, struct ieee80211_csa_work,
			list);
		list_del(&csawork->list);
		atbm_kfree(csawork);
	}
}

void ieee80211_csa_cancel(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_csa_work *csawork;
	
	mutex_lock(&local->mtx);
	
	atbm_printk_csa("[%s][%s]:[%d][%d]\n",__func__,sdata->name,ieee80211_csa_busy(local),sdata->csa_state);
		
	if (ieee80211_csa_running(local) == 0)
		goto out;

	/*
	*try delete from pending
	*/
	list_for_each_entry(csawork, &local->csa_pending, list){
		struct ieee80211_sub_if_data *sdata = vif_to_sdata(csawork->vif);

		if(vif_to_sdata(csawork->vif) != sdata){
			continue;
		}

		list_del(&csawork->list);
		atbm_kfree(csawork);
		goto out;
	}
	
	if (sdata->csa_state == IEEE80211_CSA_MLME_STATE_IDLE){
		goto out;
	}
	
	atbm_del_timer_sync(&sdata->csa_timer);
	sdata->csa_state = IEEE80211_CSA_MLME_STATE_STOP;

	_ieee80211_csa_process_work(local);
out:
	mutex_unlock(&local->mtx);
}
void ieee80211_chan_csa_sdata_init(struct ieee80211_sub_if_data *sdata)
{
	atbm_init_timer(&sdata->csa_timer);
	sdata->csa_timer.data = (unsigned long )sdata;
	sdata->csa_timer.function = ieee80211_chan_csa_timer;
}
void ieee80211_chan_csa_local_init(struct ieee80211_local *local)
{
	ATBM_INIT_WORK(&local->csa_work, ieee80211_csa_process_work);
	
	INIT_LIST_HEAD(&local->csa_req);
	INIT_LIST_HEAD(&local->csa_pending);
	
	local->next_csa_state = CSA_IDLE;
}
void ieee80211_chan_csa_local_exit(struct ieee80211_local *local)
{
	WARN_ON(!list_empty(&local->csa_pending));
	WARN_ON(!list_empty(&local->csa_req));

	ieee80211_csa_flush(local);
}
void ieee80211_event_csa_done(struct ieee80211_sub_if_data *sdata,u32 status)
{
	struct ieee80211_local *local = sdata->local;
	
	mutex_lock(&local->mtx);

	atbm_del_timer_sync(&sdata->csa_timer);
	
	if(sdata->csa_state != IEEE80211_CSA_MLME_STATE_START){
		atbm_printk_err("[%s] csa idle\n",sdata->name);
		goto exit;
	}
	
	sdata->csa_state = status ? IEEE80211_CSA_MLME_STATE_FINISHED : IEEE80211_CSA_MLME_STATE_FAIL;
	
	atbm_printk_always("[%s]:event_csa_done[%d]\n",sdata->name,sdata->csa_state);

	ieee80211_queue_work(&local->hw, &local->csa_work);
exit:
	mutex_unlock(&local->mtx);
}

#if (LINUX_VERSION_IS_GEQ_OR_CPTCFG(3,12,0))
void ieee80211_ch_switch_notify(struct ieee80211_sub_if_data *sdata,struct cfg80211_chan_def *chandef)
{
#if (CONFIG_CPTCFG_CFG80211==1)
	cfg80211_ch_switch_notify(sdata->dev,chandef);
#elif (LINUX_VERSION_CODE <= KERNEL_VERSION(5,19,1))
	cfg80211_ch_switch_notify(sdata->dev,chandef);
#elif (LINUX_VERSION_CODE < KERNEL_VERSION(6,3,0))
	cfg80211_ch_switch_notify(sdata->dev,chandef,0);
#else
	cfg80211_ch_switch_notify(sdata->dev,chandef,0,0);
#endif
}
#endif

#endif
