/*
 * Off-channel operation helpers
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/export.h>
#include <net/atbm_mac80211.h>
#include "ieee80211_i.h"
#include "driver-trace.h"
#include "driver-ops.h"
#ifdef CONFIG_ATBM_SUPPORT_P2P
#if 0
/*
 * inform AP that we will go to sleep so that it will buffer the frames
 * while we scan
 */
static void ieee80211_offchannel_ps_enable(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_if_managed *ifmgd = &sdata->u.mgd;

	sdata->offchannel_ps_enabled = false;

	/* FIXME: what to do when local->pspolling is true? */

	del_timer_sync(&sdata->dynamic_ps_timer);
	del_timer_sync(&ifmgd->bcn_mon_timer);
	del_timer_sync(&ifmgd->conn_mon_timer);

	atbm_cancel_work_sync(&sdata->dynamic_ps_enable_work);

	if (sdata->vif.bss_conf.ps_enabled) {
		sdata->offchannel_ps_enabled = true;
		sdata->vif.bss_conf.ps_enabled = false;
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_PS);
	}

	if (!(sdata->offchannel_ps_enabled) ||
	    !(local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK))
		/*
		 * If power save was enabled, no need to send a nullfunc
		 * frame because AP knows that we are sleeping. But if the
		 * hardware is creating the nullfunc frame for power save
		 * status (ie. IEEE80211_HW_PS_NULLFUNC_STACK is not
		 * enabled) and power save was enabled, the firmware just
		 * sent a null frame with power save disabled. So we need
		 * to send a new nullfunc frame to inform the AP that we
		 * are again sleeping.
		 */
		ieee80211_send_nullfunc(local, sdata, 1);
}
/* inform AP that we are awake again, unless power save is enabled */
static void ieee80211_offchannel_ps_disable(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;

	if (!sdata->ps_allowed)
		ieee80211_send_nullfunc(local, sdata, 0);
	else if (sdata->offchannel_ps_enabled) {
		/*
		 * In !IEEE80211_HW_PS_NULLFUNC_STACK case the hardware
		 * will send a nullfunc frame with the powersave bit set
		 * even though the AP already knows that we are sleeping.
		 * This could be avoided by sending a null frame with power
		 * save bit disabled before enabling the power save, but
		 * this doesn't gain anything.
		 *
		 * When IEEE80211_HW_PS_NULLFUNC_STACK is enabled, no need
		 * to send a nullfunc frame because AP already knows that
		 * we are sleeping, let's just enable power save mode in
		 * hardware.
		 */
		sdata->vif.bss_conf.ps_enabled = true;
		ieee80211_bss_info_change_notify(sdata, BSS_CHANGED_PS);
	} else if (sdata->vif.bss_conf.dynamic_ps_timeout > 0) {
		/*
		 * If IEEE80211_CONF_PS was not set and the dynamic_ps_timer
		 * had been running before leaving the operating channel,
		 * restart the timer now and send a nullfunc frame to inform
		 * the AP that we are awake.
		 */
		ieee80211_send_nullfunc(local, sdata, 0);
		mod_timer(&sdata->dynamic_ps_timer, jiffies +
			  msecs_to_jiffies(sdata->vif.bss_conf.dynamic_ps_timeout));
	}

	ieee80211_sta_reset_beacon_monitor(sdata);
	ieee80211_sta_reset_conn_monitor(sdata);
}
#endif

#ifdef ATBM_P2P_CHANGE
static bool  ieee80211_parase_p2p_action_attrs(u8* ie_start,ssize_t ie_len,struct atbm_p2p_message *p2p_msg)
{
	u8* p2p_ie = NULL;
	u8* attr_start = NULL;
	ssize_t attr_len = 0;
	u8  p2p_find = 0;
	const u8 *pos = ie_start, *end = ie_start + ie_len;

	memset(p2p_msg,0,sizeof(struct atbm_p2p_message));
	while(pos<end)
	{
		p2p_ie = atbm_ieee80211_find_p2p_ie(pos,end-pos);
		if((p2p_ie == NULL) || p2p_ie[1] < 6){
			break;
		}
		attr_start = p2p_ie+6;
		attr_len = p2p_ie[1]-4;
		if((attr_start == NULL) || (attr_len<3))
			break;
		if(p2p_msg->capability == NULL)
			p2p_msg->capability = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_CAPABILITY);
		if(p2p_msg->go_intent == NULL)
			p2p_msg->go_intent = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_GROUP_OWNER_INTENT);
		if(p2p_msg->status == NULL)
			p2p_msg->status = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_STATUS);
		if(p2p_msg->operating_channel == NULL)
			p2p_msg->operating_channel = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_OPERATING_CHANNEL);
		if(p2p_msg->channel_list == NULL)
			p2p_msg->channel_list = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_CHANNEL_LIST);
		if(p2p_msg->group_bssid == NULL)
			p2p_msg->group_bssid = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_GROUP_BSSID);
		if(p2p_msg->group_info == NULL)
			p2p_msg->group_info = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_GROUP_INFO);
		if(p2p_msg->group_id == NULL)
			p2p_msg->group_id = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_GROUP_ID);
		if(p2p_msg->device_id == NULL)
			p2p_msg->device_id = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_DEVICE_ID);
		if(p2p_msg->manageability == NULL)
			p2p_msg->manageability = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_MANAGEABILITY);
		if(p2p_msg->intended_addr == NULL)
			p2p_msg->intended_addr = atbm_ieee80211_find_p2p_attr(attr_start,attr_len,ATBM_P2P_ATTR_INTENDED_INTERFACE_ADDR);
		p2p_find = 1;

		pos += 2+p2p_ie[1];
	}
	if(p2p_find == 0)
		return false;

	return true;
	
}
static void ieee80211_change_p2p_channel_list_to_special_channel(u8* channel_list,u8 channel_num,ssize_t ie_len)
{
	u8 *pos, *end;
	u8 number_channel = 0;
	u8 index = 0;
	// attr_id(1)+len(2)+contry_string(3)+channel_list(x)
	// channel_list:operating_class(1)+number_channel(1)+channel_list(x)
	pos = channel_list+6;
	end = pos+ATBM_WPA_GET_LE16((const u8*)(&channel_list[1]))-3;
	
	if(ATBM_WPA_GET_LE16((const u8*)(&channel_list[1]))>=ie_len){
//		printk(KERN_ERR "%s:len err(%d),ie_len(%d)\n",__func__,ATBM_WPA_GET_LE16((const u8*)(&channel_list[1])),ie_len);
		return;
	}
	while(pos+2<end)
	{
		number_channel = pos[1];
		pos += 2;
		if((pos+number_channel>end)||(number_channel>14)){
			atbm_printk_p2p("%s:number_channel[%d] err\n",__func__,number_channel);
			break;
		}
		for(index=0;index<number_channel;index++){
			if(pos[index]<=14){
				atbm_printk_p2p("change channel(%d) to channel(%d)\n",pos[index],channel_num);
				pos[index] = channel_num;
			}
		}

		pos += number_channel;
	}
}
bool ieee80211_check_channel_combination(struct ieee80211_sub_if_data *ignor_sdata)
{
	struct ieee80211_sub_if_data *sdata;
	
	list_for_each_entry_rcu(sdata, &ignor_sdata->local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

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
		if (sdata->vif.type == NL80211_IFTYPE_AP &&
		    !sdata->u.ap.beacon)
			continue;

		return true;
	}

	return false;
}
//#define ATBM_PARASE_P2P_CAP
#define ATBM_P2P_PARAMS_WARNNING(param) //param = param
static void ieee80211_parase_p2p_capability(u8 *capability_pos)
{
#ifdef ATBM_PARASE_P2P_CAP
	if(capability_pos == NULL){
		return;
	}
	/*
	*dev cap and group cap
	*/
	{
		#define ATBM_P2P_GROUP_CAPAB_PERSISTENT_GROUP BIT(1)
		#define ATBM_P2P_GROUP_CAPAB_PERSISTENT_RECONN BIT(5)
		#define ATBM_P2P_DEV_CAPAB_INVITATION_PROCEDURE BIT(5)
		
		u8 *pos_group_cap = capability_pos+4; // 4 = 1(Attribute ID) + 2(Length) + 1(dev_cap)
		u8 group_cap = *pos_group_cap;
		u8 *pos_dev_cap = capability_pos+3;
		u8 dev_cap = *pos_dev_cap;
		u8 persistent_bit = ATBM_P2P_GROUP_CAPAB_PERSISTENT_GROUP | ATBM_P2P_GROUP_CAPAB_PERSISTENT_RECONN;
		u8 invitation_bit = ATBM_P2P_DEV_CAPAB_INVITATION_PROCEDURE;

		*pos_group_cap = group_cap&(~persistent_bit);
		*pos_dev_cap = dev_cap&(~invitation_bit);

		#undef ATBM_P2P_GROUP_CAPAB_PERSISTENT_GROUP
		#undef ATBM_P2P_GROUP_CAPAB_PERSISTENT_RECONN
		#undef ATBM_P2P_DEV_CAPAB_INVITATION_PROCEDURE
	}
#else
	ATBM_P2P_PARAMS_WARNNING(capability_pos);
#endif
}
bool ieee80211_parase_p2p_mgmt_frame(struct ieee80211_sub_if_data *sdata,struct sk_buff *skb,bool tx)
{
#ifdef ATBM_PARASE_P2P_CAP
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	u8 probe_req = ieee80211_is_probe_req(mgmt->frame_control);
	u8 probe_resp = ieee80211_is_probe_resp(mgmt->frame_control);
	u8 *ie = NULL;
	int ie_len = 0;
	struct atbm_p2p_message p2p_msg;

	if(!(probe_req||probe_resp)){
		return false;
	}

	if(probe_req){
		ie = mgmt->u.probe_req.variable;
		ie_len = skb->len - offsetof(struct atbm_ieee80211_mgmt, u.probe_req.variable);
	}
	else if(probe_resp){
		ie = mgmt->u.probe_resp.variable;
		ie_len = skb->len - offsetof(struct atbm_ieee80211_mgmt, u.probe_resp.variable);
	}

	if((ie == NULL)||(ie_len<=0)){
		return false;
	}

	if(ieee80211_parase_p2p_action_attrs(ie,ie_len,&p2p_msg)==false){
		return false;
	}
	ieee80211_parase_p2p_capability(p2p_msg.capability);
#else
	ATBM_P2P_PARAMS_WARNNING(priv);
	ATBM_P2P_PARAMS_WARNNING(skb);
	ATBM_P2P_PARAMS_WARNNING(tx);
#endif
	return true;
}
bool ieee80211_parase_p2p_action_frame(struct ieee80211_sub_if_data *sdata,struct sk_buff *skb,bool tx)
{
//	#define ATBM_CHANGE_LOCAL_REMOUT_ROLE
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(sdata->local, sdata);
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	int len = skb->len;
	u8* p2p_data = NULL;
	ssize_t p2p_data_len = 0;
	ssize_t p2p_check_offs = 0;
	struct atbm_p2p_message p2p_msg;

	/* drop too small frames */
	if (len < ATBM_IEEE80211_MIN_ACTION_SIZE){
		return false;
	}

	if(mgmt->u.action.category != ATBM_WLAN_CATEGORY_PUBLIC){
		return false;
	}

	p2p_data = (u8*)(&mgmt->u.action.u.vs_public_action);
	p2p_data_len = len - offsetof(struct atbm_ieee80211_mgmt, u.action.category);

	if((p2p_check_offs = ieee80211_p2p_action_check(p2p_data,p2p_data_len))==-1){
		return false;
	}
	p2p_data_len -= p2p_check_offs;
	p2p_data += p2p_check_offs;
	
	memset(&p2p_msg,0,sizeof(struct atbm_p2p_message));
	

	if(ieee80211_parase_p2p_action_attrs(&p2p_data[2],p2p_data_len-2,&p2p_msg)==false)
		return false;
	
	p2p_msg.dialog_token = p2p_data[1];
	atbm_printk_p2p("%s:operating_channel(%p),txrx(%d)\n",__func__,p2p_msg.operating_channel,(int)tx);
	ieee80211_parase_p2p_capability(p2p_msg.capability);
	/*
	*only for debug response status
	*/
	while(p2p_msg.status){
		// 3 = 1(Attribute ID) + 2(Length)
		atbm_printk_p2p("%s:status(%d),action(%d)\n",__func__,*(p2p_msg.status+3),p2p_data[0]);
		break;
	}
#ifdef ATBM_CHANGE_LOCAL_REMOUT_ROLE	
	/*
	*change go intend and tie breaker
	*/
	while(p2p_msg.go_intent){
		#define ATBM_GO_INTEND 			((14<<1) | 1)
		#define ATBM_STA_INTEND			(0)

		u8 *go_intent_pos = p2p_msg.go_intent+3;// 3 = 1(Attribute ID) + 2(Length)
		u8 org_intend;

		org_intend = *go_intent_pos;
		if(tx){
			*go_intent_pos = ATBM_GO_INTEND; //we send intend and tie breaker 0
		}else {
			*go_intent_pos = ATBM_STA_INTEND;
		}

		atbm_printk_p2p("%s:org_intend(%d),new_intend(%d),txrx(%d)\n",__func__,org_intend,*go_intent_pos,tx);
		break;
	}
#endif
	while(p2p_msg.operating_channel){
		
		u8 operating_channel_num = 0;
		u8 local_channel = 0;
		bool combination = false;
		
		combination = ieee80211_check_channel_combination(sdata);		
		atbm_printk_p2p("%s:chan_mode(%x)\n",__func__,(int)combination);
		// attr_id(1)+len(2)+contry_string(3)+operating_class(1)+channel_num(1)
		operating_channel_num = p2p_msg.operating_channel[7];
		local_channel = operating_channel_num >14?6:operating_channel_num;
		if(combination == true)
			local_channel = channel_hw_value(chan_state->oper_channel);		
		atbm_printk_p2p("%s:operating_channel_num(%d),local_channel(%d),action(%d),tx(%d)\n",__func__,
			operating_channel_num,local_channel,p2p_data[0],(int)tx);
		/*
		*if the p2p process is neg req, neg resp and neg cfg,we
		*only config the neg cfg oper channel.
		*/
		if(p2p_data[0] == ATBM_P2P_GO_NEG_CONF){
			if(combination == true)
				WARN_ON(operating_channel_num != local_channel);
			atbm_printk_p2p("%s:ATBM_P2P_GO_NEG_CONF\n",__func__);
			goto set_oper_channel;
		}
		if((p2p_data[0] != ATBM_P2P_INVITATION_REQ)&&(p2p_data[0] != ATBM_P2P_INVITATION_RESP)){
			if(((p2p_data[0] != ATBM_P2P_GO_NEG_REQ)&&(p2p_data[0] != ATBM_P2P_GO_NEG_RESP))||(combination == false))
				break;
			else{
				atbm_printk_p2p("%s:change_channel_and_list,local_channel(%d)\n",__func__,local_channel);
				goto change_channel_and_list;
			}
				
		}
		atbm_printk_p2p("%s:tx(%d),action(%d),operating_channel_num(%d)\n",__func__,(int)tx,(int)p2p_data[0],(int)operating_channel_num);

		if(((p2p_data[0] == ATBM_P2P_INVITATION_REQ)&&(combination == true))||
		   ((p2p_data[0] == ATBM_P2P_INVITATION_RESP)&&(combination == true))){
			goto change_channel_and_list;
		}
		else if(p2p_data[0] == ATBM_P2P_INVITATION_RESP){
			goto set_oper_channel;
		}
		else {
			break;
		}
		
change_channel_and_list:		
		p2p_msg.operating_channel[7] = local_channel;
		if(p2p_msg.channel_list)
		{
			ieee80211_change_p2p_channel_list_to_special_channel(p2p_msg.channel_list,local_channel,p2p_data_len-2);
		}

		if(p2p_data[0] != ATBM_P2P_INVITATION_RESP)
			break;
set_oper_channel:
		atbm_printk_p2p("%s p2p_oper_channel(%d)\n",__func__,local_channel);
		break;
	}
	atbm_printk_p2p("%s:group_bssid(%p),action(%x)\n",__func__,p2p_msg.group_bssid,p2p_data[0]);
	while(((p2p_data[0] == ATBM_P2P_INVITATION_REQ)||(p2p_data[0] == ATBM_P2P_INVITATION_RESP))&&(p2p_msg.group_bssid))
	{
		if(ATBM_WPA_GET_LE16((const u8*)(p2p_msg.group_bssid+1)) != 6){
			atbm_printk_err("%s:group_bssid is err(%d)\n",__func__,ATBM_WPA_GET_LE16((const u8*)(p2p_msg.group_bssid+1)));
			break;
		}
		atbm_printk_p2p("%s:group_bssid(%pM),own_addr(%pM)\n",__func__,p2p_msg.group_bssid+3,sdata->vif.addr);
		if(!memcmp(p2p_msg.group_bssid+3,sdata->vif.addr,6))
			break;
		break;
	}
	atbm_printk_p2p("%s:intended_addr(%p),action(%d),tx(%d)\n",__func__,p2p_msg.intended_addr,p2p_data[0],(int)tx);
	while(tx == false)
	{
		/*
		*get peer p2p interface addr,when the peer is in go mode,that address is the go bssid
		*/
		if((p2p_data[0] != ATBM_P2P_GO_NEG_REQ)&&(p2p_data[0] != ATBM_P2P_GO_NEG_RESP))
			break;
		if(!p2p_msg.intended_addr)
			break;
		if(ATBM_WPA_GET_LE16((const u8*)(p2p_msg.intended_addr+1)) != 6){
			break;
		}
		break;
	}

	return true;
}
#endif

void ieee80211_offchannel_stop_beaconing(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		/* disable beaconing */
		if (sdata->vif.type == NL80211_IFTYPE_AP
#ifdef CONFIG_ATBM_SUPPORT_IBSS
		    || sdata->vif.type == NL80211_IFTYPE_ADHOC
#endif
#ifdef CONFIG_MAC80211_ATBM_MESH
		    || sdata->vif.type == NL80211_IFTYPE_MESH_POINT
#endif
		    )
			ieee80211_bss_info_change_notify(
				sdata, BSS_CHANGED_BEACON_ENABLED);

		/*
		 * only handle non-STA interfaces here, STA interfaces
		 * are handled in ieee80211_offchannel_stop_station(),
		 * e.g., from the background scan state machine.
		 *
		 * In addition, do not stop monitor interface to allow it to be
		 * used from user space controlled off-channel operations.
		 */
		if (sdata->vif.type != NL80211_IFTYPE_STATION &&
		    sdata->vif.type != NL80211_IFTYPE_MONITOR) {
			set_bit(SDATA_STATE_OFFCHANNEL, &sdata->state);
			netif_tx_stop_all_queues(sdata->dev);
		}
	}
	mutex_unlock(&local->iflist_mtx);
}

void ieee80211_offchannel_stop_station(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;

	/*
	 * notify the AP about us leaving the channel and stop all STA interfaces
	 */
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			set_bit(SDATA_STATE_OFFCHANNEL, &sdata->state);
			netif_tx_stop_all_queues(sdata->dev);
#if 0
			/* TEMPHACK - FW manages power save mode when
			   doing ROC */
			if (sdata->u.mgd.associated)
				ieee80211_offchannel_ps_enable(sdata);
#endif
		}
	}
	mutex_unlock(&local->iflist_mtx);
}

void ieee80211_offchannel_return(struct ieee80211_local *local,
				 bool enable_beaconing)
{
	struct ieee80211_sub_if_data *sdata;

	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
	
	if (sdata->vif.type != NL80211_IFTYPE_MONITOR) {
		clear_bit(SDATA_STATE_OFFCHANNEL, &sdata->state);
	}
	/* TODO: Combo mode TEMPHACK */
#if 0
		if (!ieee80211_sdata_running(sdata))
			continue;
#endif
		/* Tell AP we're back */
#if 0
		/* TEMPHACK - FW handles the power save mode when
		   doing ROC */
		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			if (sdata->u.mgd.associated)
				ieee80211_offchannel_ps_disable(sdata);
		}
#endif

		if (sdata->vif.type != NL80211_IFTYPE_MONITOR) {
			/*
			 * This may wake up queues even though the driver
			 * currently has them stopped. This is not very
			 * likely, since the driver won't have gotten any
			 * (or hardly any) new packets while we weren't
			 * on the right channel, and even if it happens
			 * it will at most lead to queueing up one more
			 * packet per queue in mac80211 rather than on
			 * the interface qdisc.
			 */
#ifdef CONFIG_MAC80211_ATBM_ROAMING_CHANGES
			if (!sdata->queues_locked)
#endif
				netif_tx_wake_all_queues(sdata->dev);
		}

		/* re-enable beaconing */
		if (enable_beaconing &&
		    (sdata->vif.type == NL80211_IFTYPE_AP
#ifdef CONFIG_ATBM_SUPPORT_IBSS
		     || sdata->vif.type == NL80211_IFTYPE_ADHOC
#endif
#ifdef CONFIG_MAC80211_ATBM_MESH
		     || sdata->vif.type == NL80211_IFTYPE_MESH_POINT
#endif
		    ))
			ieee80211_bss_info_change_notify(
				sdata, BSS_CHANGED_BEACON_ENABLED);
	}
	mutex_unlock(&local->iflist_mtx);
}

void ieee80211_handle_roc_started(struct ieee80211_roc_work *roc)
{
	if (roc->notified)
		return;

	if (roc->mgmt_tx_cookie) {
		if (!WARN_ON(!roc->frame)) {
			ieee80211_tx_skb(roc->sdata, roc->frame);
			roc->frame = NULL;
		}
	} else {
	#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,6,0))
		cfg80211_ready_on_channel(roc->hw_roc_dev, roc->cookie,
					  roc->chan, roc->chan_type, roc->req_duration,
					  GFP_KERNEL);
	#elif (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,8,0))
		cfg80211_ready_on_channel(&roc->sdata->wdev, roc->cookie,
					  roc->chan, roc->chan_type, roc->req_duration,
					  GFP_KERNEL);
	#else
		cfg80211_ready_on_channel(&roc->sdata->wdev, roc->cookie,
					  roc->chan, roc->req_duration,
					  GFP_KERNEL);
	#endif
	}

	roc->notified = true;
}

static void ieee80211_hw_roc_start(struct atbm_work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, hw_roc_start);
	struct ieee80211_roc_work *roc, *dep, *tmp;

	mutex_lock(&local->mtx);

	if (list_empty(&local->roc_list))
		goto out_unlock;

	roc = list_first_entry(&local->roc_list, struct ieee80211_roc_work,
			       list);

	if (!roc->started)
		goto out_unlock;

	roc->hw_begun = true;
	roc->hw_start_time = local->hw_roc_start_time;
	roc->hw_extend_roc_time = local->hw_roc_extend_time;
	roc->local = local;

	ieee80211_handle_roc_started(roc);
	list_for_each_entry_safe(dep, tmp, &roc->dependents, list) {
		ieee80211_handle_roc_started(dep);

		if (dep->duration > roc->duration) {
			u32 dur = dep->duration;
			dep->duration = dur - roc->duration;
			roc->duration = dur;
			list_move(&dep->list, &roc->list);
		}
	}
 out_unlock:
	mutex_unlock(&local->mtx);
}

void ieee80211_ready_on_channel(struct ieee80211_hw *hw,unsigned long roc_extend)
{
	struct ieee80211_local *local = hw_to_local(hw);

	local->hw_roc_start_time = jiffies;
	local->hw_roc_extend_time = roc_extend;

	trace_api_ready_on_channel(local);

	ieee80211_queue_work(hw, &local->hw_roc_start);
}
//EXPORT_SYMBOL_GPL(ieee80211_ready_on_channel);

void ieee80211_start_next_roc(struct ieee80211_local *local)
{
	struct ieee80211_roc_work *roc;

	lockdep_assert_held(&local->mtx);
	
#ifdef CONFIG_ATBM_SUPPORT_CSA
	if (ieee80211_csa_busy(local)){
		return;
	}
#endif
	if (list_empty(&local->roc_list)) {
		//ieee80211_run_deferred_scan(local);
		if(ieee80211_work_busy(local)){
//			BUG_ON(local->roc_pendding);
//			BUG_ON(local->roc_pendding_sdata);
//			printk(KERN_ERR "%s:start pendding work,roc_pendding(%x)\n",__func__,(unsigned int )local->roc_pendding);
			ieee80211_queue_work(&local->hw, &local->work_work);
		}	
		ieee80211_run_pending_scan(local);
#ifdef CONFIG_ATBM_STA_LISTEN
		__ieee80211_recalc_idle(local);
#endif
		return;
	}
	
	roc = list_first_entry(&local->roc_list, struct ieee80211_roc_work,
			       list);

	if (WARN_ON_ONCE(roc->started))
		return;

	if (local->ops->remain_on_channel) {
		int ret, duration = roc->duration;

		/* XXX: duplicated, see ieee80211_start_roc_work() */
		if (!duration)
			duration = 10;

		ret = drv_remain_on_channel(local, roc->sdata, roc->chan, NL80211_CHAN_NO_HT,
					    duration,
					    (roc->mgmt_tx_cookie ? roc->mgmt_tx_cookie: roc->cookie));

		roc->started = true;

		if (ret) {
			atbm_printk_warn("failed to start next HW ROC (%d)\n", ret);
			/*
			 * queue the work struct again to avoid recursion
			 * when multiple failures occur
			 */
			ieee80211_remain_on_channel_expired(&local->hw,
					(roc->mgmt_tx_cookie ? roc->mgmt_tx_cookie: roc->cookie));
		} else
			local->hw_roc_channel = roc->chan;
	} else {
		/* delay it a bit */
		ieee80211_queue_delayed_work(&local->hw, &roc->work,
					     round_jiffies_relative(HZ/2));
	}
}

void ieee80211_roc_notify_destroy(struct ieee80211_roc_work *roc)
{
	struct ieee80211_roc_work *dep, *tmp;

	/* was never transmitted */
	if (roc->frame) {
#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,6,0))
		cfg80211_mgmt_tx_status(roc->hw_roc_dev,
					(unsigned long)roc->frame,
					roc->frame->data, roc->frame->len,
					false, GFP_KERNEL);
#else
		cfg80211_mgmt_tx_status(&roc->sdata->wdev,
					(unsigned long)roc->frame,
					roc->frame->data, roc->frame->len,
					false, GFP_KERNEL);
#endif
		atbm_kfree_skb(roc->frame);
	}
	if(roc->started&&(!roc->notified)&&(!roc->mgmt_tx_cookie)){
#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,6,0))
		cfg80211_ready_on_channel(roc->hw_roc_dev, roc->cookie,
					  roc->chan, roc->chan_type, roc->req_duration,
					  GFP_KERNEL);
#elif (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,8,0))
		cfg80211_ready_on_channel(&roc->sdata->wdev, roc->cookie,
					  roc->chan, roc->chan_type, roc->req_duration,
					  GFP_KERNEL);
#else
		cfg80211_ready_on_channel(&roc->sdata->wdev, roc->cookie,
					  roc->chan, roc->req_duration,
					  GFP_KERNEL);
#endif
	}
	if (!roc->mgmt_tx_cookie)
#if (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,6,0))
		cfg80211_remain_on_channel_expired(roc->hw_roc_dev,
						   roc->cookie, roc->chan, roc->chan_type,
						   GFP_KERNEL);
#elif (LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(3,8,0))
		cfg80211_remain_on_channel_expired(&roc->sdata->wdev,
						   roc->cookie, roc->chan, roc->chan_type,
						   GFP_KERNEL);
#else
		cfg80211_remain_on_channel_expired(&roc->sdata->wdev,
						   roc->cookie, roc->chan,
						   GFP_KERNEL);
#endif

	list_for_each_entry_safe(dep, tmp, &roc->dependents, list)
		ieee80211_roc_notify_destroy(dep);

	atbm_kfree(roc);
}

void ieee80211_sw_roc_work(struct atbm_work_struct *work)
{
	struct ieee80211_roc_work *roc =
		container_of(work, struct ieee80211_roc_work, work.work);
	struct ieee80211_sub_if_data *sdata = roc->sdata;
	struct ieee80211_local *local = sdata->local;
	bool started;

	mutex_lock(&local->mtx);

	if (roc->abort)
		goto finish;

	if (WARN_ON(list_empty(&local->roc_list)))
		goto out_unlock;

	if (WARN_ON(roc != list_first_entry(&local->roc_list,
					    struct ieee80211_roc_work,
					    list)))
		goto out_unlock;

	if (!roc->started) {
		struct ieee80211_roc_work *dep;

		/* start this ROC */

		/* switch channel etc */
		ieee80211_recalc_idle(local);

		local->tmp_channel = roc->chan;
		ieee80211_hw_config(local, 0);

		/* tell userspace or send frame */
		ieee80211_handle_roc_started(roc);
		list_for_each_entry(dep, &roc->dependents, list)
			ieee80211_handle_roc_started(dep);

		/* if it was pure TX, just finish right away */
		if (!roc->duration)
			goto finish;

		roc->started = true;
		ieee80211_queue_delayed_work(&local->hw, &roc->work,
					     msecs_to_jiffies(roc->duration));
	} else {
		/* finish this ROC */
 finish:
		list_del(&roc->list);
		started = roc->started;
		ieee80211_roc_notify_destroy(roc);

		if (started) {
			drv_flush(local, sdata, false);

			local->tmp_channel = NULL;
			ieee80211_hw_config(local, 0);

			ieee80211_offchannel_return(local, true);
		}

		ieee80211_recalc_idle(local);

		if (started)
			ieee80211_start_next_roc(local);
	}

 out_unlock:
	mutex_unlock(&local->mtx);
}

static void ieee80211_hw_roc_done(struct atbm_work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, hw_roc_done);
	struct ieee80211_roc_work *roc;
	u64	cookie;

	mutex_lock(&local->mtx);

	if (list_empty(&local->roc_list))
		goto out_unlock;

	roc = list_first_entry(&local->roc_list, struct ieee80211_roc_work,
			       list);

	if (!roc->started)
		goto out_unlock;

	cookie = roc->mgmt_tx_cookie ? roc->mgmt_tx_cookie: roc->cookie;
	atbm_printk_mgmt( "%s:cookie(%x),roc_cookie(%x)\n",__func__,(int)cookie,(int)local->roc_cookie);
	if(cookie != local->roc_cookie) {
		atbm_printk_err( "%s:cookie(%llx),local->roc_cookie(%llx)\n",__func__,cookie,local->roc_cookie);
		local->roc_cookie = 0;
		goto out_unlock;
	}
	local->roc_cookie = 0;
	list_del(&roc->list);

	ieee80211_roc_notify_destroy(roc);

	local->hw_roc_channel = NULL;

	/* if there's another roc, start it now */
	ieee80211_start_next_roc(local);

 out_unlock:
	mutex_unlock(&local->mtx);
}
void ieee80211_remain_on_channel_expired(struct ieee80211_hw *hw, u64 cookie)
{
	struct ieee80211_local *local = hw_to_local(hw);
//	struct ieee80211_roc_work *roc;
	local->roc_cookie = cookie;

	trace_api_remain_on_channel_expired(local);

	ieee80211_queue_work(hw, &local->hw_roc_done);
}
//EXPORT_SYMBOL_GPL(ieee80211_remain_on_channel_expired);
void ieee80211_hw_roc_setup(struct ieee80211_local *local)
{
	ATBM_INIT_WORK(&local->hw_roc_start, ieee80211_hw_roc_start);
	ATBM_INIT_WORK(&local->hw_roc_done, ieee80211_hw_roc_done);
	INIT_LIST_HEAD(&local->roc_list);
}

void ieee80211_roc_purge(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_roc_work *roc, *tmp;
	struct ieee80211_roc_work *pendding_roc = NULL;
	LIST_HEAD(tmp_list);

	mutex_lock(&local->mtx);
	list_for_each_entry_safe(roc, tmp, &local->roc_list, list) {
		if (roc->sdata != sdata)
			continue;

		if (roc->started && local->ops->remain_on_channel) {
			/* can race, so ignore return value */
			drv_cancel_remain_on_channel(local);
		}

		list_move_tail(&roc->list, &tmp_list);
		roc->abort = true;
	}
	local->hw_roc_channel = NULL;
	if(local->roc_pendding&&(local->roc_pendding_sdata == sdata)){
		pendding_roc = local->roc_pendding;
		local->roc_pendding = NULL;
		local->roc_pendding_sdata = NULL;
	}
	mutex_unlock(&local->mtx);
	
	if(pendding_roc != NULL){
		atbm_printk_mgmt("%s:cancle pendding_roc\n",__func__);
		ieee80211_roc_notify_destroy(pendding_roc);
	}
	list_for_each_entry_safe(roc, tmp, &tmp_list, list) {
		if (local->ops->remain_on_channel) {
			list_del(&roc->list);
			ieee80211_roc_notify_destroy(roc);
		} else {
			ieee80211_queue_delayed_work(&local->hw, &roc->work, 0);

			/* work will clean up etc */
			atbm_flush_delayed_work(&roc->work);
		}
	}

	WARN_ON_ONCE(!list_empty(&tmp_list));
}

void  ieee80211_roc_purge_current(struct ieee80211_local *local)
{
	struct ieee80211_roc_work *roc;
	
	if (list_empty(&local->roc_list)){
		goto exit;
	}

	roc = list_first_entry(&local->roc_list, struct ieee80211_roc_work,
			       list);

	if (!roc->started)
		goto exit;
	
	drv_cancel_remain_on_channel(local);
	local->hw_roc_channel = NULL;

	list_del(&roc->list);
	ieee80211_roc_notify_destroy(roc);
	
	ieee80211_start_next_roc(local);
exit:
	return;
}
#endif
