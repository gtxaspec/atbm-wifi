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

#include <net/atbm_mac80211.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <net/ip.h>
#include "mac80211/ieee80211_i.h"
#include "apollo.h"
#include "wsm.h"
#include "bh.h"
#include "ap.h"
#include "debug.h"
#include "sta.h"
#include "sbus.h"


#define MAC_HDR_LEN_ADD3 24
//	P2P Public Action Frame Type
#define	P2P_GO_NEGO_REQ						0
#define	P2P_GO_NEGO_RESP						1
#define	P2P_GO_NEGO_CONF						2
#define	P2P_INVIT_REQ							3
#define	P2P_INVIT_RESP							4
#define	P2P_DEVDISC_REQ						5
#define	P2P_DEVDISC_RESP						6
#define	P2P_PROVISION_DISC_REQ				7
#define	P2P_PROVISION_DISC_RESP				8
#define	P2POUI							0x506F9A09
#define ACT_PUBLIC_P2P   9  // WIFI_DIRECT

#define _VENDOR_SPECIFIC_IE_		221
#define	P2P_ATTR_GO_INTENT				0x04

#define MAX_IE_SZ 255
#define _PUBLIC_ACTION_IE_OFFSET_	8
#define P2P_GET_LE16(a) ((u16) (((a)[1] << 8) | (a)[0]))

#ifdef CONFIG_ATBM_SUPPORT_P2P

#if ATBM_P2P_CHANGE
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
void atbm_parase_p2p_scan_resp(struct atbm_vif *priv,struct sk_buff *skb)
{
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	struct atbm_common	*hw_priv = priv->hw_priv;

	if(!atomic_read(&hw_priv->scan.in_progress)){
		return;
	}
	/*
	*only parase beacon and probe_resp frame
	*/
	if(!(ieee80211_is_beacon(mgmt->frame_control) || 
		ieee80211_is_probe_resp(mgmt->frame_control))){
		return;
	}
	/*
	*only check p2p interface
	*/
	if(priv->if_id != 1){
		return;
	}
	atbm_printk_p2p("go_bssid_set(%d),receive_go_resp(%d)\n",atomic_read(&hw_priv->go_bssid_set),
		atomic_read(&hw_priv->receive_go_resp));
	if(atomic_read(&hw_priv->go_bssid_set) != 1){
		return;
	}
	if(atomic_read(&hw_priv->receive_go_resp) == 1){
		return;
	}
	atbm_printk_p2p("go_bssid(%pM),bssid(%pM)\n",hw_priv->go_bssid,mgmt->bssid);
	if(!memcmp(hw_priv->go_bssid,mgmt->bssid,6)){
		atomic_set(&hw_priv->receive_go_resp,1);
	}
}
bool atbm_check_channel_combination(struct atbm_common *hw_priv,struct atbm_vif *ignore_priv)
{
	//BUG_ON(!ignore_priv);
	
	if(!ignore_priv){
		atbm_printk_err("%s %d ,ERROR !!! ignore_priv is NULL\n",__func__,__LINE__);
		return false;
	}	
	return (atomic_read(&hw_priv->combination)?true:false);
}
//#define ATBM_PARASE_P2P_CAP
#define ATBM_P2P_PARAMS_WARNNING(param) //param = param
static void atbm_parase_p2p_capability(u8 *capability_pos)
{
#ifdef ATBM_PARASE_P2P_CAP  //if open this macro,you need to modify offchannel.c of wpa_supplicant synchronously
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
bool atbm_parase_p2p_mgmt_frame(struct atbm_vif *priv,struct sk_buff *skb,bool tx)
{
#ifdef ATBM_PARASE_P2P_CAP
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	u8 probe_req = ieee80211_is_probe_req(mgmt->frame_control);
	u8 probe_resp = ieee80211_is_probe_resp(mgmt->frame_control);
	u8 *ie = NULL;
	int ie_len = 0;
	struct atbm_p2p_message p2p_msg;
	
	if(priv->if_id == 0){
		return false;
	}

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
	atbm_parase_p2p_capability(p2p_msg.capability);
#else
	ATBM_P2P_PARAMS_WARNNING(priv);
	ATBM_P2P_PARAMS_WARNNING(skb);
	ATBM_P2P_PARAMS_WARNNING(tx);
#endif
	return true;
}
bool atbm_parase_p2p_action_frame(struct atbm_vif *priv,struct sk_buff *skb,bool tx)
{
//	#define ATBM_CHANGE_LOCAL_REMOUT_ROLE
	struct atbm_ieee80211_mgmt *mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	struct atbm_common	*hw_priv = priv->hw_priv;
	int len = skb->len;
	u8* p2p_data = NULL;
	ssize_t p2p_data_len = 0;
	ssize_t p2p_check_offs = 0;
	struct atbm_p2p_message p2p_msg;
	
	if(priv->if_id == 0){
		return false;
	}
	if (!ieee80211_is_action(mgmt->frame_control))
	{
		//printk(KERN_ERR "%s:is not action\n",__func__);
		atbm_parase_p2p_mgmt_frame(priv,skb,tx);
		return false;
	}

	/* drop too small frames */
	if (len < IEEE80211_MIN_ACTION_SIZE){
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
	
	if(p2p_data[0] == ATBM_P2P_INVITATION_REQ){
		if(p2p_msg.operating_channel)
			atomic_set(&hw_priv->operating_channel_combination,1);
		else
			atomic_set(&hw_priv->operating_channel_combination,0);
	}
	atbm_parase_p2p_capability(p2p_msg.capability);
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
		#define ATBM_GO_INTEND 			((15<<1) | 1)
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
		
		combination = atbm_check_channel_combination(hw_priv,priv);		
		atbm_printk_p2p("%s:chan_mode(%x),operating_channel_combination(%d)\n",__func__,(int)combination,
			atomic_read(&hw_priv->operating_channel_combination));
		// attr_id(1)+len(2)+contry_string(3)+operating_class(1)+channel_num(1)
		operating_channel_num = p2p_msg.operating_channel[7];
		local_channel = operating_channel_num>14?6:operating_channel_num;
		if(combination == true)
			local_channel = channel_hw_value(hw_priv->channel);		
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
			atomic_set(&hw_priv->p2p_oper_channel,0);
			if(((p2p_data[0] != ATBM_P2P_GO_NEG_REQ)&&(p2p_data[0] != ATBM_P2P_GO_NEG_RESP))||(combination == false))
				break;
			else{
				atbm_printk_p2p("%s:change_channel_and_list,local_channel(%d)\n",__func__,local_channel);
				goto change_channel_and_list;
			}
				
		}
		/*
		*process the invitation req and invitation reps
		*/
		if(p2p_data[0] == ATBM_P2P_INVITATION_REQ)
			atomic_set(&hw_priv->p2p_oper_channel,0);
		atbm_printk_p2p("%s:tx(%d),action(%d),operating_channel_num(%d),p2p_oper_channel(%d)\n",
		__func__,(int)tx,(int)p2p_data[0],(int)operating_channel_num,(int)atomic_read(&hw_priv->p2p_oper_channel));
		if(atomic_read(&hw_priv->p2p_oper_channel)) WARN_ON(local_channel != atomic_read(&hw_priv->p2p_oper_channel));
		local_channel = atomic_read(&hw_priv->p2p_oper_channel)?atomic_read(&hw_priv->p2p_oper_channel):local_channel;

		if(((p2p_data[0] == ATBM_P2P_INVITATION_REQ)&&(combination == true))||
		   ((p2p_data[0] == ATBM_P2P_INVITATION_RESP)&&(atomic_read(&hw_priv->operating_channel_combination))&&(combination == true))){
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
		atomic_set(&hw_priv->p2p_oper_channel,local_channel);
		break;
	}
	atbm_printk_p2p("%s:group_bssid(%p),action(%x)\n",__func__,p2p_msg.group_bssid,p2p_data[0]);
	while(((p2p_data[0] == ATBM_P2P_INVITATION_REQ)||(p2p_data[0] == ATBM_P2P_INVITATION_RESP))&&(p2p_msg.group_bssid))
	{
		if(ATBM_WPA_GET_LE16((const u8*)(p2p_msg.group_bssid+1)) != 6){
			atbm_printk_err("%s:group_bssid is err(%d)\n",__func__,ATBM_WPA_GET_LE16((const u8*)(p2p_msg.group_bssid+1)));
			break;
		}
		atbm_printk_p2p("%s:group_bssid(%pM),own_addr(%pM)\n",__func__,p2p_msg.group_bssid+3,priv->vif->addr);
		if(!memcmp(p2p_msg.group_bssid+3,priv->vif->addr,6))
			break;

		atomic_set(&hw_priv->go_bssid_set,1);
		memcpy(hw_priv->go_bssid,p2p_msg.group_bssid+3,6);
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
		
		atomic_set(&hw_priv->go_bssid_set,1);
		memcpy(hw_priv->go_bssid,p2p_msg.intended_addr+3,6);
		break;
	}

	return true;
}
#endif


#endif

