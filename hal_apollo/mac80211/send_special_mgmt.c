
#include <net/atbm_mac80211.h>
#include <linux/kthread.h>

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

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "wme.h"
#include "atbm_common.h"


#include "../apollo.h"
#include "../internal_cmd.h"


enum {
	IEEE80211_SEND_SPECIAL_PROBE_RESP						= 1,
	IEEE80211_SEND_SPECIAL_ACTION							= 2,
};

static int ieee80211_send_mgmt_action_packet(struct ieee80211_sub_if_data *sdata,struct atbm_common *hw_priv)
{
	struct ieee80211_local *local = sdata->local;
//	struct atbm_common *hw_priv=(struct atbm_common *)local->hw.priv;
	
	//struct atbm_vif *priv = (struct atbm_vif *)sdata->vif.drv_priv;
//	int ret = 0;
	unsigned char da[6]={0};
	
	struct sk_buff *skb;
	struct atbm_ieee80211_mgmt *mgmt;
//	u16 params;
	unsigned long flags;
/*		
	int work_chan;

	work_chan = get_work_channel(sdata,1);
	if(!work_chan){
		atbm_printk_err("ieee80211_send_mgmt_action_packet: work chan = 0! \n");
		return -1;
	}
*/	
	if(sdata == NULL){
		atbm_printk_err("ieee80211_send_mgmt_action_packet sdata is NULL \n");
		msleep(1000);
		return -1 ;
	}
	
	if (!ieee80211_sdata_running(sdata)){
		atbm_printk_err("ieee80211_send_mgmt_action_packet net interface not runing! \n");
		msleep(1000);
		return -1 ;
	}
	
	skb = atbm_dev_alloc_skb(sizeof(*mgmt) + local->hw.extra_tx_headroom);
	if (!skb){
		atbm_printk_always("ieee80211_send_mgmt_action_packet :atbm_dev_alloc_skb err!! \n");
		return -ENOMEM;

	}
	atbm_skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = (struct atbm_ieee80211_mgmt *) atbm_skb_put(skb, 24);
	memset(da,0xff,6);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	if (sdata->vif.type == NL80211_IFTYPE_AP ||
	    sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
		memcpy(mgmt->bssid, sdata->vif.addr, ETH_ALEN);
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		memcpy(mgmt->bssid, sdata->u.mgd.bssid, ETH_ALEN);

	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);

	atbm_skb_put(skb, 1 + sizeof(mgmt->u.action.u.vs_public_action));

	spin_lock_irqsave(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
	mgmt->u.action.category = ATBM_WLAN_CATEGORY_VENDOR_SPECIFIC;
	mgmt->u.action.u.vs_public_action.action = hw_priv->customer_action_ie.action;
	mgmt->u.action.u.vs_public_action.oui[0] = 0;
	mgmt->u.action.u.vs_public_action.oui[1] = 0;
	mgmt->u.action.u.vs_public_action.oui[2] = 0;
	sdata = hw_priv->customer_action_ie.sdata;
	spin_unlock_irqrestore(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
	IEEE80211_SKB_CB(skb)->flags |=
			IEEE80211_TX_CTL_NO_CCK_RATE;
			
	ieee80211_tx_skb(sdata, skb);

	return 0;
}


static int ieee80211_send_probe_resp_packet(struct ieee80211_sub_if_data *sdata,struct atbm_common *hw_priv)
{
	struct sk_buff *skb;
	struct atbm_ieee80211_mgmt *mgmt = NULL;
	u8 dest[6]={0xff,0xff,0xff,0xff,0xff,0xff};
	u8 *special = NULL;
	unsigned long flags;

	if(sdata == NULL){
		atbm_printk_err("atbm_ieee80211_send_probe_resp not found ap mode! \n");
		msleep(1000);
		return -1 ;
	}
	
	if (!ieee80211_sdata_running(sdata)){
		atbm_printk_err("atbm_ieee80211_send_probe_resp net interface not runing! \n");
		msleep(1000);
		return -1 ;
	}
	if(sdata->vif.type != NL80211_IFTYPE_AP){
		atbm_printk_err("atbm_ieee80211_send_probe_resp , not ap mode! \n");
		msleep(1000);
		return -1 ;
	}

	
#ifdef ATBM_PROBE_RESP_EXTRA_IE
	skb = ieee80211_proberesp_get(&sdata->local->hw,&sdata->vif);
#endif
	if(skb == NULL){
		atbm_printk_err("atbm_ieee80211_send_probe_resp  skb err! \n");
		return -1;
	}

	/*
	*add special ie
	*/
	if(atbm_pskb_expand_head(skb,0,sizeof(struct atbm_vendor_cfg_ie),GFP_ATOMIC)){
		//atbm_printk_err("atbm_pskb_expand_head   err! sizeof(struct atbm_vendor_cfg_ie) = %d\n",sizeof(struct atbm_vendor_cfg_ie));
		atbm_dev_kfree_skb(skb);
		return 0;
	}
	special = skb->data + skb->len;
	//atbm_printk_err("atbm_ieee80211_send_probe_resp  special=%x \n",special);
	
	spin_lock_irqsave(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
	memcpy(special,&hw_priv->ap_vendor_cfg_ie.private_ie,sizeof(struct atbm_vendor_cfg_ie));
	sdata = hw_priv->ap_vendor_cfg_ie.ap_sdata;
	spin_unlock_irqrestore(&hw_priv->send_prvmgmt_skb_queue.lock,flags);

	atbm_skb_put(skb,sizeof(struct atbm_vendor_cfg_ie));
	

	mgmt = (struct atbm_ieee80211_mgmt *)skb->data;
	memcpy(mgmt->da,dest,6);

	IEEE80211_SKB_CB(skb)->flags |=
			IEEE80211_TX_CTL_NO_CCK_RATE;

	ieee80211_tx_skb(sdata, skb);
	return 0;

}

static int atbm_ieee80211_send_private_mgmt_thread(void *arg)
{
	
	struct atbm_common *hw_priv = arg;
	struct ieee80211_hw *hw = hw_priv->hw;
//	struct ieee80211_local *local = 
//		container_of(hw, struct ieee80211_local, hw);
	
		//sdata->local;
	
//	struct atbm_common *hw_priv = (struct atbm_common *) hw->priv;
//	hw_priv->stop_prvmgmt_thread = false;
//	hw_priv->start_send_prbresp = false;
	while(1){
		atbm_printk_err(" wait_event_interruptible from  send_prbresp_wq\n");
		wait_event_interruptible(hw_priv->send_prvmgmt_wq, hw_priv->start_send_prbresp || hw_priv->start_send_action);

		if(hw_priv->stop_prvmgmt_thread)
			break;
		

		while(1){
			if(!hw_priv->start_send_prbresp && !hw_priv->start_send_action)
				break;
			
			
			
			if(hw_priv->start_send_prbresp){
				if(ieee80211_send_probe_resp_packet(hw_priv->ap_vendor_cfg_ie.ap_sdata,hw_priv)){
					atbm_printk_err("ieee80211_send_probe_resp_packet err! \n");
					hw_priv->start_send_prbresp = 0;
				}
			}

			if(hw_priv->start_send_action){
				if(ieee80211_send_mgmt_action_packet(hw_priv->customer_action_ie.sdata,hw_priv)){
					atbm_printk_err("ieee80211_send_mgmt_action_packet err! \n");
					hw_priv->start_send_action = 0;
				}
			}

			
			msleep(4);
		}
	
	}
	while(1){
		if(kthread_should_stop())
			break;
		schedule_timeout_uninterruptible(msecs_to_jiffies(100));
	}

	atbm_printk_err("atbm_ieee80211_send_probe_resp drv_flush! \n");
	return 0;
}



static void ieee80211_send_mgmt_work_control(struct atbm_work_struct *work)
{
	
	struct atbm_common *hw_priv =
		container_of(work, struct atbm_common, send_prvmgmt_work);
	struct sk_buff *skb;
	struct sk_buff_head local_list;
	unsigned long flags;
	
	
	__atbm_skb_queue_head_init(&local_list);
	spin_lock_irqsave(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
	atbm_skb_queue_splice_tail_init(&hw_priv->send_prvmgmt_skb_queue, &local_list);
	spin_unlock_irqrestore(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
	

	while ((skb = __atbm_skb_dequeue(&local_list)) != NULL){
		//atbm_printk_err("ieee80211_send_mgmt_work_control skb=%x skb->data=%x\n",skb,skb->data);
		switch (skb->pkt_type) {
			case IEEE80211_SEND_SPECIAL_PROBE_RESP:{
				skb->pkt_type = 0;
			//	struct atbm_ap_vendor_cfg_ie *ap_vendor_cfg_ie;
				if(skb->protocol == 0){
					hw_priv->start_send_prbresp = false;
				}else{
					spin_lock_irqsave(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
					memcpy(&hw_priv->ap_vendor_cfg_ie,skb->data,sizeof(struct atbm_ap_vendor_cfg_ie));
					//ap_vendor_cfg_ie = (struct atbm_ap_vendor_cfg_ie *)skb->data;
					spin_unlock_irqrestore(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
				//	atbm_printk_err("sdata:%x , ssid=%s , psk=%s \n",
				//		hw_priv->ap_vendor_cfg_ie.ap_sdata,
				//		hw_priv->ap_vendor_cfg_ie.private_ie.ssid,
				//		hw_priv->ap_vendor_cfg_ie.private_ie.password);
					
					if(hw_priv->start_send_prbresp == true)
						break;
					
					hw_priv->start_send_prbresp = true;
					wake_up(&hw_priv->send_prvmgmt_wq);
					
				}
				
				atbm_dev_kfree_skb(skb);
				}break;
			case IEEE80211_SEND_SPECIAL_ACTION:{
				skb->pkt_type = 0;
				
				if(skb->protocol == 0){
					hw_priv->start_send_action = false;
				}else{
					spin_lock_irqsave(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
					memcpy(&hw_priv->customer_action_ie,skb->data,sizeof(struct atbm_customer_action));
					spin_unlock_irqrestore(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
				//	atbm_printk_err("sdata:%x , action:%d \n",
				//		hw_priv->customer_action_ie.sdata,hw_priv->customer_action_ie.action);
					if(hw_priv->start_send_action == true)
						break;
					
					hw_priv->start_send_action = true;
					wake_up(&hw_priv->send_prvmgmt_wq);
				}
				
				atbm_dev_kfree_skb(skb);
				}break;
			default:{
				skb->pkt_type = 0;

				
				}break;

		}
		
		
		
	}	

	
}

static void ieee80211_send_mgmt_queue_request(struct atbm_common *hw_priv,struct sk_buff *skb)
{
	unsigned long flags; 
	//bool work_runing = false;
	
	spin_lock_irqsave(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
	
	__atbm_skb_queue_tail(&hw_priv->send_prvmgmt_skb_queue, skb);
	
	//work_runing = (hw_priv->start_send_action | hw_priv->start_send_prbresp);
	
	spin_unlock_irqrestore(&hw_priv->send_prvmgmt_skb_queue.lock,flags);
	
	//if(work_runing == false)
	ieee80211_queue_work(hw_priv->hw,&hw_priv->send_prvmgmt_work);
	
}

int ieee80211_send_action_mgmt_queue(struct atbm_common *hw_priv,char *buff,bool start)
{
	struct sk_buff *skb;

	skb = atbm_dev_alloc_skb(sizeof(struct atbm_customer_action));

	if(skb == NULL)
		return -1;
	skb->pkt_type = IEEE80211_SEND_SPECIAL_ACTION;
	skb->protocol = start;
	if(buff){
		memcpy(skb->data,buff,sizeof(struct atbm_customer_action));
		atbm_skb_put(skb,sizeof(struct atbm_customer_action));
	}
	
		
	ieee80211_send_mgmt_queue_request(hw_priv,skb);
		
	return 0;
	
}
int ieee80211_send_probe_resp_mgmt_queue(struct atbm_common *hw_priv,char *buff,bool start)
{
	struct sk_buff *skb;

	skb = atbm_dev_alloc_skb(sizeof(struct atbm_ap_vendor_cfg_ie));

	if(skb == NULL)
		return -1;
	
	skb->pkt_type = IEEE80211_SEND_SPECIAL_PROBE_RESP;
	skb->protocol = start;
	if(buff){		
		memcpy(skb->data,buff,sizeof(struct atbm_ap_vendor_cfg_ie));
		atbm_skb_put(skb,sizeof(struct atbm_ap_vendor_cfg_ie));	
	}
	
	//atbm_printk_err("ieee80211_send_probe_resp_mgmt_queue skb=%x skb->data=%x\n",skb,skb->data);
	ieee80211_send_mgmt_queue_request(hw_priv,skb);
//	atbm_printk_err("%s %d \n",__func__,__LINE__);
	return 0;

}


//int atbm_send_probe_resp_init(struct atbm_common *hw_priv)
int atbm_send_private_mgmt_init(struct atbm_common *hw_priv)
{

	if(!hw_priv){
		atbm_printk_err("atbm_send_private_mgmt_init : hw_priv is NULL\n");
		return -1;
	}
	hw_priv->send_prvmgmt_thread = NULL;
	//hw_priv->bh_wq
	init_waitqueue_head(&hw_priv->send_prvmgmt_wq);
	hw_priv->stop_prvmgmt_thread = false;
	hw_priv->start_send_prbresp = false;
	hw_priv->start_send_action = false;
	hw_priv->send_prvmgmt_thread = 
	kthread_create(&atbm_ieee80211_send_private_mgmt_thread, hw_priv, ieee80211_alloc_name(hw_priv->hw,"atbm_prvmgmt"));
	
	if (!hw_priv->send_prvmgmt_thread){
		atbm_printk_err("kthread_create :error!! \n");
		hw_priv->send_prvmgmt_thread = NULL;
		return -1;
	}else{
		wake_up_process(hw_priv->send_prvmgmt_thread);
	}
	atbm_skb_queue_head_init(&hw_priv->send_prvmgmt_skb_queue);
	ATBM_INIT_WORK(&hw_priv->send_prvmgmt_work, ieee80211_send_mgmt_work_control);
	
	//ATBM_INIT_WORK(&hw_priv->send_prbresp_work, atbm_ieee80211_send_probe_resp);
	//mutex_init(&hw_priv->stop_send_prbresp_lock);
	return 0;
}
//int atbm_send_probe_resp_uninit(struct atbm_common *hw_priv) 
int atbm_send_private_mgmt_uninit(struct atbm_common *hw_priv) 
{
	struct task_struct *thread = hw_priv->send_prvmgmt_thread;
	if (WARN_ON(!thread))
		return -1;
//	atbm_flush_workqueue(&hw_priv->send_prvmgmt_work);
//	atbm_destroy_workqueue(&hw_priv->send_prvmgmt_work);
	atbm_cancel_work_sync(&hw_priv->send_prvmgmt_work);
	atbm_skb_queue_purge(&hw_priv->send_prvmgmt_skb_queue);

//	hw_priv->bh_thread = NULL;
	hw_priv->stop_prvmgmt_thread = true;
	hw_priv->start_send_prbresp = true;
	hw_priv->start_send_action = true;
	wake_up(&hw_priv->send_prvmgmt_wq);
	kthread_stop(thread);
	
	return 0;
}





