#include "atbm_hal.h"

#include "atbm_os_skbuf.h"
#include "atbm_os_iw_wsm.h"

#define EXTRA_TX_HEADROOM (WSM_TX_EXTRA_HEADROOM + 64 + 8/* TKIP IV */ + 12 /* TKIP ICV and MIC */)

typedef struct join_ap{
	char ssid[32];
	char key[64];
	WLAN_AUTH_MODE authMode;
	WLAN_ENCRYPTION encryption;
}join_info;

typedef struct create_ap{
	char ssid[32];
	char key[64];
	WLAN_AUTH_MODE authMode;
	WLAN_ENCRYPTION encryption;
	int channel;
	ATBM_BOOL bcst;
	char reserved[3];
}ap_info;

typedef struct tx_time{
	atbm_uint32 time_period;
	atbm_uint32 time_transmit;
}time_info;

typedef struct etf_cfg{
	atbm_uint32 channel;
	atbm_uint32 rate;
	atbm_uint32 is40m;
	atbm_uint32 greenfield;
}etf_info;

typedef struct p2p_cfg{
	atbm_uint32 timeout;
	atbm_uint32 go_intent;
	atbm_uint8 mac[ATBM_ETH_ALEN];
	atbm_uint8 reserved[2];
}p2p_info;

typedef struct mfg_cfg{
	atbm_uint32 channel;
	atbm_uint32 rate;
	atbm_int32 power;
	atbm_uint32 isWriteEfuse;
}mfg_info;

static join_info join_struct;
static ap_info ap_struct;
static time_info time_struct;
static etf_info etf_struct;
static p2p_info p2p_struct;
static mfg_info mfg_struct;
static ATBM_WIFI_MODE AP_sta_mode = ATBM_WIFI_STA_MODE;
static spinlock_t rx_spinlock;

extern atbm_void sta_deauth(struct atbmwifi_vif *priv);
extern atbm_int32 atbm_wifi_set_retry(atbm_uint32 retry_num, atbm_uint32 retry_time_ms);
extern atbm_int32 atbm_wifi_set_txpower(atbm_uint32 txpower_idx);
extern atbm_int32 atbm_wifi_set_tx_rate(atbm_int32 rate);
extern int atbmwifi_scan(struct atbmwifi_vif *priv);
extern atbm_int8 atbm_wifi_get_bssid(atbm_uint8 *bssid, atbm_int32 if_id);
extern atbm_int32 atbmwifi_enable_lmaclog(atbm_uint32 value);
extern atbm_int32 atbm_wifi_get_rssi_avg(atbm_void);
extern atbm_int32 atbm_wifi_set_tx_time(atbm_uint32 time_period, atbm_uint32 time_transmit);
extern atbm_int32 atbm_wifi_set_txpower_mode(atbm_uint32 txpower_idx);
extern atbm_int32 atbm_wifi_set_sgi(atbm_uint32 sgi);
extern atbm_int32 atbm_wifi_set_adptive(atbm_uint32 value);
extern atbm_int32 atbm_wifi_set_rate_txpower_mode(atbm_int32 txpower_idx);
extern atbm_int32 atbm_wifi_mfg_PT_Test(atbm_uint8 if_id, atbm_int32 isWriteEfuse);
extern int atbm_save_efuse(struct atbmwifi_common *hw_priv, struct efuse_headr *efuse_save);

atbm_void atbm_lwip_init(struct atbm_net_device *dev)
{
	//hal_create_mutex(&lwip_mutex);
}

atbm_void atbm_lwip_enable(struct atbm_net_device *dev)
{
	int queue_id;
	struct atbmwifi_vif *priv = netdev_drv_priv(dev);
	struct atbmwifi_common *hw_priv = _atbmwifi_vifpriv_to_hwpriv(priv);

	dev->lwip_enable=1;
	dev->lwip_queue_enable=1;

	for(queue_id = ATBM_IEEE80211_AC_VO; queue_id <= ATBM_IEEE80211_AC_BK; queue_id++){
		if(hw_priv->tx_queue[queue_id].overfull == ATBM_TRUE){
			dev->lwip_queue_enable=0;
		}
	}
}

atbm_void atbm_lwip_disable(struct atbm_net_device *dev)
{
	dev->lwip_enable=0;
	dev->lwip_queue_enable=0;
}

atbm_void atbm_lwip_txdone(struct atbm_net_device *dev)
{
}

atbm_void atbm_lwip_wake_queue(struct atbm_net_device *dev,int num)
{
	if(!dev->lwip_queue_enable && dev->lwip_enable){
		dev->lwip_queue_enable = 1;
	}
}

atbm_void atbm_lwip_stop_queue(struct atbm_net_device *dev,int num)
{
	if(dev->lwip_queue_enable && dev->lwip_enable){
		//hal_wait_for_mutex(&lwip_mutex,2000);
		dev->lwip_queue_enable = 0;
	}
}

atbm_void atbm_lwip_task_event(struct atbm_net_device *dev)
{
}

atbm_uint32 atbm_wifi_rx_pkt(struct atbm_net_device *dev, struct atbm_buff *at_skb)
{
	struct sk_buff *skb;
	static int spinlock_init = 0;
	skb = dev_alloc_skb(at_skb->dlen);
	atbm_memcpy(skb_put(skb, at_skb->dlen), at_skb->abuf, at_skb->dlen);
	atbm_dev_kfree_skb(at_skb);
	skb->dev = dev->nif;
	skb->protocol = eth_type_trans(skb, dev->nif);

	if (!spinlock_init)
	{
		spin_lock_init(&rx_spinlock);
		spinlock_init = 1;
	}

	atbm_spin_lock_bh(&rx_spinlock);
	netif_rx(skb);
	atbm_spin_unlock_bh(&rx_spinlock);

	return 0;
}

struct tcpip_opt lwip_tcp_opt ={
	.net_init = atbm_lwip_init,
	.net_enable = atbm_lwip_enable,//
	.net_disable = atbm_lwip_disable,//
	.net_rx = atbm_wifi_rx_pkt,
	.net_tx_done =	atbm_lwip_txdone,
	.net_start_queue =	atbm_lwip_wake_queue,
	.net_stop_queue =	atbm_lwip_stop_queue,
	.net_task_event =	atbm_lwip_task_event,//
};

atbm_void atbm_skbbuffer_init()
{
}

static int ieee80211_skb_resize(struct sk_buff *skb,
				int head_need, bool may_encrypt)
{
	int tail_need = 0;

	if (skb_cloned(skb))
		;
	else if (head_need || tail_need)
		;
	else
		return 0;

	if (pskb_expand_head(skb, head_need, tail_need, GFP_ATOMIC)) {
		wifi_printk(WIFI_ALWAYS,"failed to reallocate TX buffer\n");
		return -ENOMEM;
	}

	return 0;
}

static int atbm_open(struct net_device *dev)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	atomic_set((atbm_atomic_t *)&priv->enabled,1);
	
	//atbm_get_connectconfig(sdata->hw_priv);
	//atbm_set_tcp_port_filter(sdata->hw_priv);

	if(priv->connect_ok==0){
		netif_carrier_off(dev);
	}
	else {
		netif_carrier_on(dev);
	}
	netif_tx_start_all_queues(dev);
	wifi_printk(WIFI_ALWAYS,"%s()\n", __func__);
	return 0;
}

static int atbm_stop(struct net_device *dev)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	
	atomic_set((atbm_atomic_t *)&priv->enabled,0);
	netif_tx_stop_all_queues(dev);
	//sta_info_flush(vif->hw_priv,vif);
	//flush_work(&vif->work);

	//synchronize_rcu();
	//atbm_skb_queue_purge(&vif->skb_queue);	
	wifi_printk(WIFI_ALWAYS,"%s()\n", __func__);
	return 0;
}

static void atbm_sdata_uninit(struct net_device *dev)
{
	//struct atbm_net_device *netdev = netdev_priv(dev);
	//struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	//sta_info_flush(vif->hw_priv, vif);
}

void atbm_set_qos_hdr(struct atbmwifi_vif *vif, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;

	/* Fill in the QoS header if there is one. */
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *p = ieee80211_get_qos_ctl(hdr);
		u8 ack_policy, tid;

		tid = skb->priority & ATBM_IEEE80211_QOS_CTL_TAG1D_MASK;

		/* preserve EOSP bit */
		ack_policy = *p & ATBM_IEEE80211_QOS_CTL_EOSP;

		/* qos header is 2 bytes */
		*p++ = ack_policy | tid;
		*p =  0;
	}
}

netdev_tx_t atbm_subif_start_xmit(struct sk_buff *skb,
				    struct net_device *dev)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *vif = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	int ret = NETDEV_TX_BUSY;
	struct sk_buff *tmp_skb;
	struct atbm_buff *atbm_skb;
	atbm_uint8 *skb_pos;

	if (unlikely(skb->len < ETH_HLEN)) {
		ret = NETDEV_TX_OK;
		goto fail;
	}

	atbm_skb = atbm_dev_alloc_skb(skb->len);
	if (atbm_skb == NULL){
		ret = NETDEV_TX_OK;
		goto fail;
	}

	skb_pos = atbm_skb_put(atbm_skb,skb->len);
	atbm_memcpy(skb_pos, skb->data, skb->len);
	atbmwifi_tx_start(atbm_skb,vif);
	ret = NETDEV_TX_OK;

fail:
	if (ret == NETDEV_TX_OK)
		dev_kfree_skb(skb);

	return ret;
}

static int atbm_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 256) || (new_mtu>1500)) {
		return -EINVAL;
	}
	dev->mtu = new_mtu;
	return 0;
}

static u16 atbm_netdev_select_queue(struct net_device *dev,
                                         struct sk_buff *skb
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0))
					,
                                         void *accel_priv,
                                         select_queue_fallback_t fallback
#endif
					 )

{
	return 0;
}

int atbm_netdev_ioctrl(struct net_device *dev, struct ifreq *rq, int cmd)
{	
	int ret = 0;
	return ret;
}

static const struct net_device_ops atbm_dataif_ops = {
	.ndo_open		= atbm_open,
	.ndo_stop		= atbm_stop,
	.ndo_uninit		= atbm_sdata_uninit,
	.ndo_start_xmit		= atbm_subif_start_xmit,
	.ndo_change_mtu 	= atbm_change_mtu,
	.ndo_select_queue	= atbm_netdev_select_queue,
#if defined(CONFIG_ATBM_IOCTRL)
	.ndo_do_ioctl = atbm_netdev_ioctrl,
#endif
};

static inline void netdev_attach_ops(struct net_device *dev,
		       const struct net_device_ops *ops)
{
	dev->netdev_ops = ops;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
static void atbm_if_free(struct net_device *dev)
{
        free_percpu(dev->tstats);
}
#endif

static void atbm_if_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	netdev_attach_ops(dev, &atbm_dataif_ops);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29))
	/* Do we need this ? */
	/* we will validate the address ourselves in ->open */
	dev->validate_addr = NULL;
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	dev->needs_free_netdev = true;
	dev->priv_destructor = atbm_if_free;
#endif
	dev->destructor = free_netdev;
}

static char *wifi_name[2] =
{
	"wlan0", "wlan1"
};

static int atbm_wext_giwname(struct net_device *dev,
			  struct iw_request_info *info,
			  char *name, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);

	strcpy(name, priv->if_name);
	return 0;
}

static int atbm_wext_siwmode(struct net_device *dev, struct iw_request_info *info,
			  u32 *mode, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_giwmode(struct net_device *dev, struct iw_request_info *info,
			  u32 *mode, char *extra)
{
	return -EOPNOTSUPP;
}

#define IW_ENC_CAPA_WAPI		0x00000020
#define IW_ENC_CAPA_CIPHER_SMS4		0x00000040

#define IW_AUTH_KEY_MGMT_WAPI_PSK	4

#define IW_AUTH_CIPHER_SMS4	0x00000040

static int atbm_wext_giwrange(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra)
{
	struct iw_range *range = (struct iw_range *) extra;

	data->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 21;
	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;
	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->max_encoding_tokens = 4;

	range->max_qual.updated = IW_QUAL_NOISE_INVALID;

	range->max_qual.level = -110;
	range->max_qual.qual = 70;
	range->avg_qual.qual = 35;
	range->max_qual.updated |= IW_QUAL_DBM;
	range->max_qual.updated |= IW_QUAL_QUAL_UPDATED;
	range->max_qual.updated |= IW_QUAL_LEVEL_UPDATED;
		
	range->avg_qual.level = range->max_qual.level / 2;
	range->avg_qual.noise = range->max_qual.noise / 2;
	range->avg_qual.updated = range->max_qual.updated;

	range->enc_capa |= (IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_WPA);
	range->enc_capa |= (IW_ENC_CAPA_CIPHER_CCMP | IW_ENC_CAPA_WPA2);
	range->enc_capa |= (IW_ENC_CAPA_CIPHER_SMS4 | IW_ENC_CAPA_WAPI);
	range->encoding_size[range->num_encoding_sizes++] = WLAN_KEY_LEN_WEP40;
	range->encoding_size[range->num_encoding_sizes++] = WLAN_KEY_LEN_WEP104;


	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);

	range->scan_capa |= IW_SCAN_CAPA_ESSID;
	return 0;
}

static int atbm_wext_siwrts(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *rts, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	atbm_uint32 rts_value;

	if (rts->disabled || !rts->fixed){
		rts_value = (atbm_uint32) -1;
	}
	else if (rts->value < 0){
		return -EINVAL;
	}
	else {
		rts_value = rts->value;
	}

	return wsm_set_rts(priv->hw_priv, rts_value, priv->if_id);
}

static int atbm_wext_giwrts(struct net_device *dev,
			 struct iw_request_info *info,
			 struct iw_param *rts, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);

	if(wsm_get_rts(priv->hw_priv, &(rts->value), priv->if_id)){
		return -1;
	}
	else {
		rts->disabled = rts->value == (atbm_uint32) -1;
		rts->fixed = 1;
		return 0;
	}
}

static int atbm_wext_siwfrag(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *frag, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_giwfrag(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_param *frag, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_siwretry(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *retry, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);

	if (retry->disabled || (retry->flags & IW_RETRY_TYPE) != IW_RETRY_LIMIT){
		return -EINVAL;
	}

	priv->hw_priv->long_frame_max_tx_count = retry->value;
	priv->hw_priv->short_frame_max_tx_count = retry->value;
	return 0;
}

static int atbm_wext_giwretry(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_param *retry, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);

	retry->disabled = 0;

	if (retry->flags == 0 || (retry->flags & IW_RETRY_SHORT)){
		retry->flags |= IW_RETRY_LIMIT;
		retry->value = priv->hw_priv->short_frame_max_tx_count;

		if (priv->hw_priv->short_frame_max_tx_count != priv->hw_priv->long_frame_max_tx_count){
			retry->flags |= IW_RETRY_LONG;
		}
		return 0;
	}

	if (retry->flags & IW_RETRY_LONG){
		retry->flags = IW_RETRY_LIMIT | IW_RETRY_LONG;
		retry->value = priv->hw_priv->long_frame_max_tx_count;
	}

	return 0;
}

static int atbm_wext_siwencode(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *erq, char *keybuf)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	struct wsm_add_key wsm_key;
	int idx;

	idx = erq->flags & IW_ENCODE_INDEX;
	if (idx == 0){
		strcpy(priv->config.password, keybuf);
		priv->config.password_len = strlen(keybuf);
		priv->config.key_mgmt = ATBM_KEY_WEP;
	}
	else if (idx < 1 || idx > 4){
		return -EINVAL;
	}
	else {
		if (erq->length == 0){
			priv->wep_default_key_id = idx;
			wsm_set_default_key(priv->hw_priv, idx, priv->if_id);
		}
		else {
			wsm_key.type = WSM_KEY_TYPE_WEP_DEFAULT;
			atbm_memcpy(wsm_key.wepGroupKey.keyData,keybuf, strlen(keybuf));
			wsm_key.wepGroupKey.keyLength = strlen(keybuf);
			wsm_key.wepGroupKey.keyId = idx;
			wsm_add_key(priv->hw_priv, &wsm_key, priv->if_id);
		}
	}

	return 0;
}

static int atbm_wext_siwencodeext(struct net_device *dev,
				      struct iw_request_info *info,
				      struct iw_point *erq, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_giwencode(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *erq, char *keybuf)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_siwfreq(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_freq *wextfreq, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_giwfreq(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_freq *freq, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);

	if (priv->connect_ok){
		freq->m = atbmwifi_ieee80211_channel_to_frequency(priv->bss.channel_num, ATBM_IEEE80211_BAND_2GHZ);
		freq->e = 6;
		freq->i = priv->bss.channel_num;
		return 0;
	}
	else {
		return -1;
	}
}

static int atbm_wext_siwtxpower(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *data, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_giwtxpower(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *data, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_siwgenie(struct net_device *dev,
			   struct iw_request_info *info,
			   struct iw_point *data, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_siwauth(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *data, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_giwauth(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *data, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_siwpower(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *wrq, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	atbm_uint32 ps = 0;
	struct wsm_operational_mode mode;
	int timeout = 0;

	if (wrq->disabled) {
		ps = wsm_power_mode_active;
	} else {
		switch (wrq->flags & IW_POWER_MODE) {
		case IW_POWER_ON:       /* If not specified */
		case IW_POWER_MODE:     /* If set all mask */
		case IW_POWER_ALL_R:    /* If explicitely state all */
			ps = wsm_power_mode_quiescent;
			break;
		default:                /* Otherwise we ignore */
			return -EINVAL;
		}

		if (wrq->flags & ~(IW_POWER_MODE | IW_POWER_TIMEOUT))
			return -EINVAL;

		if (wrq->flags & IW_POWER_TIMEOUT)
			timeout = wrq->value / 1000;
	}

	atbm_memset(&mode, 0, sizeof(struct wsm_operational_mode));
	mode.power_mode = ps;
	mode.disableMoreFlagUsage = ATBM_TRUE;

	wsm_set_operational_mode(priv->hw_priv, &mode, priv->if_id);
	return 0;

}

static int atbm_wext_giwpower(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *wrq, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_siwrate(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rate, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	atbm_int32 rate_value;

	if (rate->value < 0){
		return -EINVAL;
	}

	rate_value = rate->value / 100000;

	if (atbm_wifi_set_tx_rate(rate_value)){
		return -1;
	}
	else {
		priv->hw_priv->max_rates = rate_value;
		return 0;
	}
}

static int atbm_wext_giwrate(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *rate, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	rate->value = 100000 * priv->hw_priv->max_rates;
	return 0;
}

static struct iw_statistics *atbm_wireless_stats(struct net_device *dev)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	static struct iw_statistics wstats;
	int sig = 0;

	if (priv->iftype != ATBM_NL80211_IFTYPE_STATION){
		return NULL;
	}

	if (!priv->connect_ok) {
		return NULL;
	}

	memset(&wstats, 0, sizeof(wstats));
	wstats.qual.updated |= IW_QUAL_LEVEL_UPDATED;
	wstats.qual.updated |= IW_QUAL_QUAL_UPDATED;
	wstats.qual.updated |= IW_QUAL_DBM;

	sig = priv->bss.rssi;
	if (sig < -110){
		sig = -110;
	}
	else if (sig > -40) {
		sig = -40;
	}

	wstats.qual.qual = sig + 110;

	return &wstats;
}

static int atbm_wext_siwap(struct net_device *dev,
			       struct iw_request_info *info,
			       struct sockaddr *ap_addr, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_giwap(struct net_device *dev,
			       struct iw_request_info *info,
			       struct sockaddr *ap_addr, char *extra)
{
	return -EOPNOTSUPP;
}

static int atbm_wext_siwmlme(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	struct iw_mlme *mlme = (struct iw_mlme *)extra;

	if (priv->iftype != ATBM_NL80211_IFTYPE_STATION){
		return -EINVAL;
	}

	if (mlme->addr.sa_family != ARPHRD_ETHER){
		return -EINVAL;
	}

	switch (mlme->cmd){
		case IW_MLME_DEAUTH:
		case IW_MLME_DISASSOC:
			sta_deauth(priv);
			return 0;
		default:
			return -EOPNOTSUPP;
	}
}

static int atbm_wext_siwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	struct iw_scan_req *wreq = NULL;

	if (wrqu->data.length == sizeof(struct iw_scan_req)){
		wreq = (struct iw_scan_req *)extra;
	}

	if (wreq) {
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			if (wreq->essid_len > ATBM_IEEE80211_MAX_SSID_LEN) {
				return -EINVAL;
			}
			memcpy(priv->ssid, wreq->essid, wreq->essid_len);
			priv->ssid_length= wreq->essid_len;
			memcpy(priv->config.ssid, wreq->essid, wreq->essid_len);
			priv->config.ssid_len = wreq->essid_len;
		}
	}

	if (priv->iftype != ATBM_NL80211_IFTYPE_STATION){
		atbmwifi_start_sta(priv);
	}

	atbmwifi_scan(priv);
	return 0;
}


static char *translate_scan_result(struct iw_request_info* info, struct atbmwifi_scan_result_info *pscan_rssult,
				char *start, char *stop)
{
	struct iw_event iwe;
	int sig;

	/*  AP MAC address  */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;

	atbm_memcpy(iwe.u.ap_addr.sa_data, pscan_rssult->BSSID, ETH_ALEN);
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_ADDR_LEN);

	/* Add the ESSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;	
	iwe.u.data.length = min((u16)pscan_rssult->ssidlen, (u16)32);
	start = iwe_stream_add_point(info, start, stop, &iwe, pscan_rssult->ssid);

	  /* Add mode */
	iwe.cmd = SIOCGIWMODE;
	iwe.u.mode = IW_MODE_MASTER;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_UINT_LEN);

	 /* Add frequency/channel */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = atbmwifi_ieee80211_channel_to_frequency(pscan_rssult->channel, ATBM_IEEE80211_BAND_2GHZ) * 100000;
	iwe.u.freq.e = 6;
	iwe.u.freq.i = pscan_rssult->channel;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_FREQ_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (pscan_rssult->encrypt)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	start = iwe_stream_add_point(info, start, stop, &iwe, pscan_rssult->ssid);

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_INVALID | IW_QUAL_DBM;
	iwe.u.qual.level = pscan_rssult->rssi;
	sig = pscan_rssult->rssi;
	if (sig < -110)		/* rather bad */
		sig = -110;
	else if (sig > -40)	/* perfect */
		sig = -40;
	iwe.u.qual.qual = sig + 110;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_QUAL_LEN);

	return start;	
}


static int atbm_wext_giwscan(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	char *ev = extra;
	char *stop = ev + data->length;
	int i = 0;

	if (priv->scan.in_progress){
	//	printk("atbm_wext_giwscan wait\n");
		return -EAGAIN;
	};

	if (!priv->scan.in_progress){
		for (i=0; i<priv->scan_ret.len; i++){
			if (stop - ev <= IW_EV_ADDR_LEN){
				return -E2BIG;
			}
			ev = translate_scan_result(info, priv->scan_ret.info+i, ev, stop);
		}

		return 0;
	}
	else {
		return -1;
	}
}

static int atbm_wext_siwessid(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *data, char *ssid)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	atbm_uint8 ssid_tmp[ATBM_IEEE80211_MAX_SSID_LEN];
	atbm_uint8 password_tmp[64];

	if (ssid == NULL || data->length <= 0){
		return -EINVAL;
	}

	atbm_memset(priv->config.ssid, 0, sizeof(priv->config.ssid));
	priv->config.ssid_len = strlen(ssid);
	strcpy(priv->config.ssid, ssid);

	if (priv->iftype != ATBM_NL80211_IFTYPE_STATION){
		atbmwifi_start_sta(priv);
	}

	strcpy(ssid_tmp, priv->config.ssid);
	strcpy(password_tmp, priv->config.password);

	if (wifi_ConnectAP_vif(priv->if_id, ssid_tmp,priv->config.ssid_len,password_tmp,
		priv->config.password_len,priv->config.key_mgmt)){
		return -1;
	}
	else {
		return 0;
	}
}

static int atbm_wext_giwessid(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *data, char *ssid)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	atbm_uint8 bssid[ATBM_ETH_ALEN];

	if (priv->iftype != ATBM_NL80211_IFTYPE_STATION){
		return -EOPNOTSUPP;
	}

	data->flags = 0;
	data->length = 0;

	if (atbm_wifi_get_bssid(bssid, priv->if_id)){
		return -1;
	}
	else {
		data->flags = 1;
		data->length = strlen(bssid);
		memcpy(ssid, bssid, strlen(bssid));
	}

	return 0;
}

static int atbm_wext_siwpmksa(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *data, char *extra)
{
	return -EOPNOTSUPP;
}


static const iw_handler cfg80211_handlers[] = {
	[IW_IOCTL_IDX(SIOCGIWNAME)]	= (iw_handler) atbm_wext_giwname,
	[IW_IOCTL_IDX(SIOCSIWFREQ)]	= (iw_handler) atbm_wext_siwfreq,
	[IW_IOCTL_IDX(SIOCGIWFREQ)]	= (iw_handler) atbm_wext_giwfreq,
	[IW_IOCTL_IDX(SIOCSIWMODE)]	= (iw_handler) atbm_wext_siwmode,
	[IW_IOCTL_IDX(SIOCGIWMODE)]	= (iw_handler) atbm_wext_giwmode,
	[IW_IOCTL_IDX(SIOCGIWRANGE)]	= (iw_handler) atbm_wext_giwrange,
	[IW_IOCTL_IDX(SIOCSIWAP)]	= (iw_handler) atbm_wext_siwap,
	[IW_IOCTL_IDX(SIOCGIWAP)]	= (iw_handler) atbm_wext_giwap,
	[IW_IOCTL_IDX(SIOCSIWMLME)]	= (iw_handler) atbm_wext_siwmlme,
	[IW_IOCTL_IDX(SIOCSIWSCAN)]	= (iw_handler) atbm_wext_siwscan,
	[IW_IOCTL_IDX(SIOCGIWSCAN)]	= (iw_handler) atbm_wext_giwscan,
	[IW_IOCTL_IDX(SIOCSIWESSID)]	= (iw_handler) atbm_wext_siwessid,
	[IW_IOCTL_IDX(SIOCGIWESSID)]	= (iw_handler) atbm_wext_giwessid,
	[IW_IOCTL_IDX(SIOCSIWRATE)]	= (iw_handler) atbm_wext_siwrate,
	[IW_IOCTL_IDX(SIOCGIWRATE)]	= (iw_handler) atbm_wext_giwrate,
	[IW_IOCTL_IDX(SIOCSIWRTS)]	= (iw_handler) atbm_wext_siwrts,
	[IW_IOCTL_IDX(SIOCGIWRTS)]	= (iw_handler) atbm_wext_giwrts,
	[IW_IOCTL_IDX(SIOCSIWFRAG)]	= (iw_handler) atbm_wext_siwfrag,
	[IW_IOCTL_IDX(SIOCGIWFRAG)]	= (iw_handler) atbm_wext_giwfrag,
	[IW_IOCTL_IDX(SIOCSIWTXPOW)]	= (iw_handler) atbm_wext_siwtxpower,
	[IW_IOCTL_IDX(SIOCGIWTXPOW)]	= (iw_handler) atbm_wext_giwtxpower,
	[IW_IOCTL_IDX(SIOCSIWRETRY)]	= (iw_handler) atbm_wext_siwretry,
	[IW_IOCTL_IDX(SIOCGIWRETRY)]	= (iw_handler) atbm_wext_giwretry,
	[IW_IOCTL_IDX(SIOCSIWENCODE)]	= (iw_handler) atbm_wext_siwencode,
	[IW_IOCTL_IDX(SIOCGIWENCODE)]	= (iw_handler) atbm_wext_giwencode,
	[IW_IOCTL_IDX(SIOCSIWPOWER)]	= (iw_handler) atbm_wext_siwpower,
	[IW_IOCTL_IDX(SIOCGIWPOWER)]	= (iw_handler) atbm_wext_giwpower,
	[IW_IOCTL_IDX(SIOCSIWGENIE)]	= (iw_handler) atbm_wext_siwgenie,
	[IW_IOCTL_IDX(SIOCSIWAUTH)]	= (iw_handler) atbm_wext_siwauth,
	[IW_IOCTL_IDX(SIOCGIWAUTH)]	= (iw_handler) atbm_wext_giwauth,
	[IW_IOCTL_IDX(SIOCSIWENCODEEXT)]= (iw_handler) atbm_wext_siwencodeext,
	[IW_IOCTL_IDX(SIOCSIWPMKSA)]	= (iw_handler) atbm_wext_siwpmksa,
};

static int trans_auth(char *auth)
{
	if (!strcmp(auth, "disable")){
		return WLAN_WPA_AUTH_DISABLED;
	}
	else if (!strcmp(auth, "none")){
		return WLAN_WPA_AUTH_NONE;
	}
	else if (!strcmp(auth, "wpa_psk")){
		return WLAN_WPA_AUTH_PSK;
	}
	else if (!strcmp(auth, "wpa2_psk")){
		return WLAN_WPA2_AUTH_PSK;
	}
	else if (!strcmp(auth, "mix_psk")){
		return WLAN_MIX_AUTH_PSK;
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid auth.\n"
			"please input refer: disable/none/wpa_psk/wpa2_psk/mix_psk.\n"
			"notes:disable or none is set for enc:wep and wep_shared.\n");
		return -1;
	}
}

static int trans_enc(char *enc)
{
	if (!strcmp(enc, "none")){
		return WLAN_ENCRYPT_NONE;
	}
	else if (!strcmp(enc, "wep")){
		return WLAN_ENCRYPT_WEP;
	}
	else if (!strcmp(enc, "wep_shared")){
		return WLAN_ENCRYPT_WEP_SHARED;
	}
	else if (!strcmp(enc, "tkip")){
		return WLAN_ENCRYPT_TKIP;
	}
	else if (!strcmp(enc, "aes")){
		return WLAN_ENCRYPT_AES;
	}
	else if (!strcmp(enc, "wsec")){
		return WLAN_ENCRYPT_WSEC;
	}
	else if (!strcmp(enc, "fips")){
		return WLAN_ENCRYPT_FIPS;
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid enc.\n"
			"please input refer: none/wep/wep_shared/tkip/aes/wsec/fips.\n");
		return -1;
	}
}

static atbm_uint32 trans_rate(atbm_uint32 rate)
{
	switch (rate){
		case 10:
			return WLAN_RATE_1M;
			break;
		case 20:
			return WLAN_RATE_2M;
			break;
		case 55:
			return WLAN_RATE_5M5;
			break;
		case 110:
			return WLAN_RATE_11M;
			break;
		case 60:
			return WLAN_RATE_6M;
			break;
		case 90:
			return WLAN_RATE_9M;
			break;
		case 120:
			return WLAN_RATE_12M;
			break;
		case 180:
			return WLAN_RATE_18M;
			break;
		case 240:
			return WLAN_RATE_24M;
			break;
		case 360:
			return WLAN_RATE_36M;
			break;
		case 480:
			return WLAN_RATE_48M;
			break;
		case 540:
			return WLAN_RATE_54M;
			break;
		case 65:
			return WLAN_MCS_RATE_0;
			break;
		case 130:
			return WLAN_MCS_RATE_1;
			break;
		case 195:
			return WLAN_MCS_RATE_2;
			break;
		case 260:
			return WLAN_MCS_RATE_3;
			break;
		case 390:
			return WLAN_MCS_RATE_4;
			break;
		case 520:
			return WLAN_MCS_RATE_5;
			break;
		case 585:
			return WLAN_MCS_RATE_6;
			break;
		case 650:
			return WLAN_MCS_RATE_7;
			break;
		default:
			wifi_printk(WIFI_DBG_ERROR,"invalid rate.\n"
				"please input refer (M * 10, example:1M = 1*10 = 10):\n"
				"10/20/55/110/60/90/120/180/240/360/480/540/65/130/195/260/390/520/585/650.\n");
			return -1;
	}
}

static int trans_rate_enum(int rate_value)
{
	int rate = -1;

	switch(rate_value){
		case 10: 
			rate = WSM_TRANSMIT_RATE_1;
			break;
		case 20: 
			rate = WSM_TRANSMIT_RATE_2;
			break;
		case 55: 
			rate = WSM_TRANSMIT_RATE_5;
			break;
		case 110: 
			rate = WSM_TRANSMIT_RATE_11;
			break;
		case 60: 
			rate = WSM_TRANSMIT_RATE_6;
			break;
		case 90:
			rate = WSM_TRANSMIT_RATE_9;
			break;
		case 120: 
			rate = WSM_TRANSMIT_RATE_12;
			break;
		case 180: 
			rate = WSM_TRANSMIT_RATE_18;
			break;
		case 240: 
			rate = WSM_TRANSMIT_RATE_24;
			break;
		case 360: 
			rate = WSM_TRANSMIT_RATE_36;
			break;
		case 480: 
			rate = WSM_TRANSMIT_RATE_48;
			break;
		case 540: 
			rate = WSM_TRANSMIT_RATE_54;
			break;
		case 65: 
			rate = WSM_TRANSMIT_RATE_HT_6;
			break;
		case 130: 
			rate = WSM_TRANSMIT_RATE_HT_13;
			break;
		case 195: 
			rate = WSM_TRANSMIT_RATE_HT_19;
			break;
		case 260: 
			rate = WSM_TRANSMIT_RATE_HT_26;
			break;
		case 390: 
			rate = WSM_TRANSMIT_RATE_HT_39;
			break;
		case 520: 
			rate = WSM_TRANSMIT_RATE_HT_52;
			break;
		case 585: 
			rate = WSM_TRANSMIT_RATE_HT_58;
			break;
		case 650: 
			rate = WSM_TRANSMIT_RATE_HT_65;
			break;
		default:
			wifi_printk(WIFI_DBG_ERROR, "invalid rate!\n");
			return -ATBM_EINVAL;			
	}

	return rate;
}

static int trans_wifimode(char *mode)
{
	if (!strcmp(mode, "sta")){
		return ATBM_WIFI_STA_MODE;
	}
	else if (!strcmp(mode, "ap")){
		return ATBM_WIFI_AP_MODE;
	}
	else if (!strcmp(mode, "adhoc")){
		return ATBM_WIFI_ADHOC;
	}
	else if (!strcmp(mode, "monitor")){
		return ATBM_WIFI_MONITOR;
	}
	else if (!strcmp(mode, "p2p_client")){
		return ATBM_WIFI_P2P_CLIENT;
	}
	else if (!strcmp(mode, "p2p_go")){
		return ATBM_WIFI_P2P_GO;
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid mode.\n"
			"please input refer: sta/ap/adhoc/monitor/p2p_client/p2p_go.\n");
		return -1;
	}
}

static int get_wifi_mode(atbm_uint8 if_id, struct iw_point *data, char *extra)
{
	atbm_int32 mode = atbm_wifi_get_current_mode_vif(if_id);
	if (mode == -1){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_get_current_mode_vif err\n");
		return -1;
	}

	switch (mode){
		case ATBM_NL80211_IFTYPE_STATION:
			strcpy(extra, "STATION");
			break;
		case ATBM_NL80211_IFTYPE_AP:
			strcpy(extra, "AP");
			break;
		case ATBM_NL80211_IFTYPE_ADHOC:
			strcpy(extra, "ADHOC");
			break;
		case ATBM_NL80211_IFTYPE_MONITOR:
			strcpy(extra, "MONITOR");
			break;
		case ATBM_NL80211_IFTYPE_P2P_CLIENT:
			strcpy(extra, "P2P CLIENT");
			break;
		case ATBM_NL80211_IFTYPE_P2P_GO:
			strcpy(extra, "P2P GO");
			break;
		default:
			strcpy(extra, "INVALID");
			break;
	}

	data->length = strlen(extra);
	return 0;
}

static int get_mac_address(struct iw_point *data, char *extra)
{
	atbm_uint8 mac[ATBM_ETH_ALEN];
	atbm_wifi_get_mac_address(mac);
	sprintf(extra, "%pM", mac);
	data->length = strlen(extra);
	return 0;
}

static int get_driver_version(struct iw_point *data, char *extra)
{
	signed char *version;

	version = atbm_wifi_get_driver_version();
	if (version){
		sprintf(extra, "%s", version);
		data->length = strlen(extra);
	}
	return 0;
}

static int get_connect_status(atbm_uint8 if_id, struct iw_point *data, char *extra)
{
	if (if_id != 0){
		wifi_printk(WIFI_DBG_ERROR,"only if_id 0 support STA mode\n");
		return -1;
	}
	
	if (atbm_wifi_isconnected(if_id)){
		strcpy(extra, "connected");
	}
	else {
		strcpy(extra, "disconnected");
	}

	data->length = strlen(extra);
	return 0;
}

static int get_connected_info(atbm_uint8 if_id, struct iw_point *data, char *extra)
{
	ATBM_WLAN_CONNECTION_INFO connect_info;

	if (if_id != 0){
		wifi_printk(WIFI_DBG_ERROR,"only if_id 0 support STA mode\n");
		return -1;
	}

	atbm_memset(&connect_info, 0, sizeof(connect_info));

	if (atbm_wifi_get_connected_info(&connect_info)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_get_connected_info err\n");
		return -1;
	}
	else {
		sprintf(extra, "\n\tSsid : %s\n\tRssi : %d\n\tChannel : %d\n", connect_info.Ssid, connect_info.Rssi, connect_info.channel);
		data->length = strlen(extra);
		return 0;
	}
}

static int get_rssi_avg(struct iw_point *data, char *extra)
{
	atbm_int32 rssi_avg;

	rssi_avg = atbm_wifi_get_rssi_avg();
	sprintf(extra, "%d", rssi_avg);
	data->length = strlen(extra);
	return 0;
}

static int get_assoc_list(atbm_uint8 if_id, struct iw_point *data, char *extra)
{
	WLAN_MACLIST mac_list;

	if (if_id != 1){
		wifi_printk(WIFI_DBG_ERROR,"only if_id 1 support AP mode\n");
		return -1;
	}

	memset(&mac_list, 0, sizeof(mac_list));
	if (atbm_wifi_get_associated_client_list((atbm_uint8 *)(&mac_list), sizeof(mac_list))){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_get_associated_client_list err\n");
		return -1;
	}
	else {
		int i = 0;
		char *tmp = extra;
		strcpy(tmp, "\n");
		for (i=0; i<mac_list.count; i++){
			sprintf(tmp+strlen(tmp), "%pM\n", mac_list.ea[i].mac);
		}
		data->length = strlen(extra);
		return 0;
	}
}

static int get_256bits_efuse(struct iw_point *data, char *extra)
{
	if (atbm_wifi_get_256BITSEFUSE(extra, 32)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_get_256BITSEFUSE err\n");
		return -1;
	}
	else {
		data->length = 32;
		return 0;
	}
}

static int get_mfg_rx_pkt_cnt(struct iw_point *data, char *extra)
{
	atbm_int32 count = 0;

	if (atbm_wifi_mfg_get_RxPkt(&count)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_mfg_get_RxPkt err\n");
		return -1;
	}
	else {
		sprintf(extra, "%d", count);
		data->length = strlen(extra);
		return 0;
	}
}

static int set_lmaclog_enable(atbm_uint32 value)
{
	if (atbmwifi_enable_lmaclog(value)){
		wifi_printk(WIFI_DBG_ERROR,"atbmwifi_enable_lmaclog err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int set_txpower(atbm_uint32 value)
{
	if (atbm_wifi_set_txpower(value)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_set_txpower err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int set_txpower_mode(atbm_uint32 value)
{
	if (atbm_wifi_set_txpower_mode(value)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_set_txpower_mode err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int set_sgi(atbm_uint32 value)
{
	if (atbm_wifi_set_sgi(value)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_set_sgi err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int set_adaptive(atbm_uint32 value)
{
	if (atbm_wifi_set_adptive(value)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_set_adptive err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int atbm_iwpriv_get_info(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	int ret = 0;
	char param[32];

	if (data->length <= 0){
		wifi_printk(WIFI_DBG_ERROR," cmd need 1 argument.\n");
		return -1;
	}

	if (copy_from_user(param, data->pointer, data->length)){
		wifi_printk(WIFI_DBG_ERROR,"copy data err\n");
		return -1;
	}

	if (!strcmp(param, "driver_version")){
		ret = get_driver_version(data, extra);
	}
	else if (!strcmp(param, "rssi_avg")){
		ret = get_rssi_avg(data, extra);
	}
	else if (!strcmp(param, "mode")){
		ret = get_wifi_mode(priv->if_id, data, extra);
	}
	else if (!strcmp(param, "mac")){
		ret = get_mac_address(data, extra);
	}
	else if (!strcmp(param, "connect_status")){
		ret = get_connect_status(priv->if_id, data, extra);
	}
	else if (!strcmp(param, "connected_info")){
		ret = get_connected_info(priv->if_id, data, extra);
	}
	else if (!strcmp(param, "assoc_list")){
		ret = get_assoc_list(priv->if_id, data, extra);
	}
	else if (!strcmp(param, "256bit_efuse")){
		ret = get_256bits_efuse(data, extra);
	}
	else if (!strcmp(param, "mfg_rx_pkt_cnt")){
		ret = get_mfg_rx_pkt_cnt(data, extra);
	}
	else {
		wifi_printk(WIFI_DBG_ERROR," invalid argument: %s\n", param);
		ret = -1;
	}

	return ret;
}

static int atbm_iwpriv_set_info(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	int ret = 0;
	atbm_uint32 value = 0;
	int length = data->length;

	if (length <= 0){
		wifi_printk(WIFI_DBG_ERROR," cmd need 1 argument.\n");
		return -1;
	}

	if (!atbm_memcmp(extra, "lmaclog=", 8)){
		if (length <= 8+1){
			wifi_printk(WIFI_DBG_ERROR," lmaclog value is null.\n");
			return -1;
		}
		value = atoi(extra+8);
		ret = set_lmaclog_enable(value);
	}
	else if (!atbm_memcmp(extra, "txpower=", 8)){
		if (length <= 8+1){
			wifi_printk(WIFI_DBG_ERROR," txpower value is null.\n");
			return -1;
		}
		value = atoi(extra+8);
		ret = set_txpower(value);
	}
	else if (!atbm_memcmp(extra, "txpower_mode=", 13)){
		if (length <= 13+1){
			wifi_printk(WIFI_DBG_ERROR," txpower_mode value is null.\n");
			return -1;
		}
		value = atoi(extra+13);
		ret = set_txpower_mode(value);
	}
	else if (!atbm_memcmp(extra, "sgi=", 4)){
		if (length <= 4+1){
			wifi_printk(WIFI_DBG_ERROR," sgi value is null.\n");
			return -1;
		}
		value = atoi(extra+4);
		ret = set_sgi(value);
	}
	else if (!atbm_memcmp(extra, "adaptive=", 9)){
		if (length <= 9+1){
			wifi_printk(WIFI_DBG_ERROR," adaptive value is null.\n");
			return -1;
		}
		value = atoi(extra+9);
		ret = set_adaptive(value);
	}
	else {
		wifi_printk(WIFI_DBG_ERROR," invalid argument: %s\n", extra);
		ret = -1;
	}

	return ret;
}


static int atbm_iwpriv_set_join_param(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);

	if (priv->if_id != 0){
		wifi_printk(WIFI_DBG_ERROR,"only if_id 0 support STA mode\n");
		return -1;
	}

	if (!atbm_memcmp(extra, "ssid=", 5)){
		if (data->length <= 5+1){
			wifi_printk(WIFI_DBG_ERROR,"ssid is null\n");
			return -1;
		}
		strcpy(join_struct.ssid, extra+5);
	}
	else if (!atbm_memcmp(extra, "auth=", 5)){
		
		int auth = trans_auth(extra+5);
		if (auth == -1){
			wifi_printk(WIFI_DBG_ERROR,"invalid auth: %s\n", extra+5);
			return -1;
		}
		join_struct.authMode = auth;
	}
	else if (!atbm_memcmp(extra, "enc=", 4)){
		int enc = trans_enc(extra+4);
		if (enc == -1){
			wifi_printk(WIFI_DBG_ERROR,"invalid enc: %s\n", extra+4);
			return -1;
		}
		join_struct.encryption = enc;
	}
	else if (!atbm_memcmp(extra, "key=", 4)){
		if (data->length <= 4+1){
			wifi_printk(WIFI_DBG_ERROR,"key is null\n");
			return -1;
		}
		strcpy(join_struct.key, extra+4);
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid argument: %s\n", extra);
		return -1;
	}
	return 0;
}

static int atbm_iwpriv_join(struct net_device *dev, struct iw_request_info *info,
				struct iw_param *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	int ret = 0;

	if (priv->if_id != 0){
		wifi_printk(WIFI_DBG_ERROR,"only if_id 0 support STA mode\n");
		return -1;
	}

	if (data->value){
		wifi_printk(WIFI_ALWAYS,"start join ap\n"
			"ssid[%s] key[%s] auth[%x] enc[%d]\n", join_struct.ssid, join_struct.key, join_struct.authMode, join_struct.encryption);

		if (priv->iftype != ATBM_NL80211_IFTYPE_STATION){
			atbmwifi_start_sta(priv);
		}

		ret = atbm_wifi_sta_join_ap(join_struct.ssid, NULL, join_struct.authMode, join_struct.encryption, join_struct.key);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_sta_join_ap err\n");
		}
	}
	else {
		ret = atbm_wifi_sta_disjoin_ap();
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_sta_disjoin_ap err\n");
		}
	}

	return ret;
}

static int atbm_iwpriv_set_ap_param(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	int ch = 0;

	if (priv->if_id != 1){
		wifi_printk(WIFI_DBG_ERROR,"only if_id 1 support AP mode\n");
		return -1;
	}

	if (!atbm_memcmp(extra, "ssid=", 5)){
		if (data->length <= 5+1){
			wifi_printk(WIFI_DBG_ERROR,"ssid is null\n");
			return -1;
		}
		strcpy(ap_struct.ssid, extra+5);
	}
	else if (!atbm_memcmp(extra, "auth=", 5)){
		
		int auth = trans_auth(extra+5);
		if (auth == -1){
			wifi_printk(WIFI_DBG_ERROR,"invalid auth: %s\n", extra+5);
			return -1;
		}
		ap_struct.authMode = auth;
	}
	else if (!atbm_memcmp(extra, "enc=", 4)){
		int enc = trans_enc(extra+4);
		if (enc == -1){
			wifi_printk(WIFI_DBG_ERROR,"invalid enc: %s\n", extra+4);
			return -1;
		}
		ap_struct.encryption = enc;
	}
	else if (!atbm_memcmp(extra, "key=", 4)){
		if (data->length <= 4+1){
			wifi_printk(WIFI_DBG_ERROR,"key is null\n");
			return -1;
		}
		strcpy(ap_struct.key, extra+4);
	}
	else if (!atbm_memcmp(extra, "ch=", 3)){
		if (data->length <= 3+1){
			wifi_printk(WIFI_DBG_ERROR,"ch is null\n");
			return -1;
		}
		ch = atoi(extra+3);
		if (ch < 1 || ch > 14){
			wifi_printk(WIFI_DBG_ERROR,"invalid channel set\n");
			return -1;
		}
		ap_struct.channel = ch;
	}
	else if (!atbm_memcmp(extra, "bcst=", 5)){
		if (data->length <= 5+1){
			wifi_printk(WIFI_DBG_ERROR,"bcst is null\n");
			return -1;
		}
		if (!strcmp(extra+5, "TRUE")){
			ap_struct.bcst = ATBM_TRUE;
		}
		else {
			ap_struct.bcst = ATBM_FALSE;
		}
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid argument: %s\n", extra);
		return -1;
	}
	return 0;
}

static int atbm_iwpriv_ap(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);

	if (priv->if_id != 1){
		wifi_printk(WIFI_DBG_ERROR,"only if_id 1 support AP mode\n");
		return -1;
	}

	wifi_printk(WIFI_ALWAYS,"start create ap\n"
		"ssid[%s] key[%s] auth[%x] enc[%d] ch[%d] bcst[%d]\n", ap_struct.ssid, ap_struct.key, 
		ap_struct.authMode, ap_struct.encryption, ap_struct.channel, ap_struct.bcst);

	if (priv->iftype != ATBM_NL80211_IFTYPE_AP){
		atbmwifi_start_ap(priv);
	}

	if (atbm_wifi_ap_create(ap_struct.ssid, ap_struct.authMode, ap_struct.encryption, ap_struct.key, ap_struct.channel, ap_struct.bcst)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_ap_create err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int atbm_iwpriv_set_tx_time(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, int *extra)
{
	wifi_printk(WIFI_ALWAYS, "tx_period[%d] time_transmit[%d]\n", extra[0], extra[1]);

	if (atbm_wifi_set_tx_time(extra[0], extra[1])){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_set_tx_time err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int atbm_iwpriv_set_retry(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, int *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);

	wifi_printk(WIFI_ALWAYS, "retry_num[%d] retry_time[%d]\n", extra[0], extra[1]);

	if (atbm_wifi_set_retry(extra[0],  extra[1])){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_set_retry err\n");
		return -1;
	}
	else {
		priv->hw_priv->long_frame_max_tx_count = extra[0];
		priv->hw_priv->short_frame_max_tx_count = extra[0];
		return 0;
	}
}

static int atbm_iwpriv_set_256bits_efuse(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	if (atbm_wifi_set_256BITSEFUSE(extra, 32)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_set_256BITSEFUSE err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int atbm_iwpriv_set_efuse_item(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	struct efuse_headr efuse_data_local;
	char param[32];

	if (data->length <= 0){
		wifi_printk(WIFI_DBG_ERROR,"need 1 argument\n");
		return -1;
	}

	if (copy_from_user(param, data->pointer, data->length)){
		wifi_printk(WIFI_DBG_ERROR,"copy data err\n");
		return -1;
	}

	memset(&efuse_data_local, 0, sizeof(struct efuse_headr));
	if (wsm_get_efuse_data(priv->hw_priv, &efuse_data_local, sizeof(struct efuse_headr))){
		wifi_printk(WIFI_DBG_ERROR,"wsm_get_efuse_data err\n");
		return -1;
	}

	if(!atbm_memcmp(param, "specific=", 9)){
		if (data->length <= 9+1){
			wifi_printk(WIFI_DBG_ERROR,"specific is null\n");
			return -1;
		}
		efuse_data_local.specific = atoi(param+9);
	}
	else if(!atbm_memcmp(param, "version=", 8)){
		if (data->length <= 8+1){
			wifi_printk(WIFI_DBG_ERROR,"version is null\n");
			return -1;
		}
		efuse_data_local.version = atoi(param+8);
	}
	else if(!atbm_memcmp(param, "dcxo_trim=", 10)){
		if (data->length <= 10+1){
			wifi_printk(WIFI_DBG_ERROR,"dcxo_trim is null\n");
			return -1;
		}
		efuse_data_local.dcxo_trim = atoi(param+10);
	}
	else if(!atbm_memcmp(param, "delta_gain1=", 12)){
		if (data->length <= 12+1){
			wifi_printk(WIFI_DBG_ERROR,"delta_gain1 is null\n");
			return -1;
		}
		efuse_data_local.delta_gain1 = atoi(param+12);
	}
	else if(!atbm_memcmp(param, "delta_gain2=", 12)){
		if (data->length <= 12+1){
			wifi_printk(WIFI_DBG_ERROR,"delta_gain2 is null\n");
			return -1;
		}
		efuse_data_local.delta_gain2 = atoi(param+12);
	}
	else if(!atbm_memcmp(param, "delta_gain3=", 12)){
		if (data->length <= 12+1){
			wifi_printk(WIFI_DBG_ERROR,"delta_gain3 is null\n");
			return -1;
		}
		efuse_data_local.delta_gain3 = atoi(param+12);
	}
	else if(!atbm_memcmp(param, "tj_room=", 8)){
		if (data->length <= 8+1){
			wifi_printk(WIFI_DBG_ERROR,"tj_room is null\n");
			return -1;
		}
		efuse_data_local.Tj_room = atoi(param+8);
	}
	else if(!atbm_memcmp(param, "topref_ctrl_bias_res_trim=", 26)){
		if (data->length <= 26+1){
			wifi_printk(WIFI_DBG_ERROR,"topref_ctrl_bias_res_trim is null\n");
			return -1;
		}
		efuse_data_local.topref_ctrl_bias_res_trim = atoi(param+26);
	}
	else if(!atbm_memcmp(param, "power_supply_sel=", 17)){
		if (data->length <= 17+1){
			wifi_printk(WIFI_DBG_ERROR,"power_supply_sel is null\n");
			return -1;
		}
		efuse_data_local.PowerSupplySel = atoi(param+17);
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid efuse item.\n"
			"please input refer: specific/version/dcxo_trim/delta_gain1/delta_gain2/delta_gain3/tj_room/topref_ctrl_bias_res_trim/power_supply_sel.\n");
		return -1;
	}

	if (atbm_save_efuse(priv->hw_priv, &efuse_data_local)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_save_efuse err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int atbm_iwpriv_set_efuse_mac(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	struct efuse_headr efuse_data_local;
	int i = 0;

	memset(&efuse_data_local, 0, sizeof(struct efuse_headr));
	if (wsm_get_efuse_data(priv->hw_priv, &efuse_data_local, sizeof(struct efuse_headr))){
		wifi_printk(WIFI_DBG_ERROR,"wsm_get_efuse_data err\n");
		return -1;
	}

	for (i=0; i<ATBM_ETH_ALEN; i++){
		efuse_data_local.mac[i] = extra[i];
	}

	wifi_printk(WIFI_ALWAYS, "mac[%pM]\n", efuse_data_local.mac);

	if (atbm_save_efuse(priv->hw_priv, &efuse_data_local)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_save_efuse err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int atbm_iwpriv_mfg_cfg(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, int *extra)
{
	int channel;
	int rate;
	int power;

	channel = extra[0];
	if (channel < 1 || channel > 14){
		wifi_printk(WIFI_DBG_ERROR,"invalid channel\n");
		return -1;
	}

	rate = trans_rate(extra[1]);
	if (rate == -1){
		wifi_printk(WIFI_DBG_ERROR,"invalid rate: %d\n", extra[1]);
		return -1;
	}

	power = extra[2];
	if (power > 16 || power < -16){
		wifi_printk(WIFI_DBG_ERROR,"invalid txpower\n");
		return -1;
	}

	mfg_struct.channel = channel;
	mfg_struct.rate = rate;
	mfg_struct.power = power;
	mfg_struct.isWriteEfuse = extra[3];
	return 0;
}

static int atbm_iwpriv_mfg(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	int ret = 0;
	char param[32];

	if (data->length <= 0){
		wifi_printk(WIFI_DBG_ERROR,"need 1 argument\n");
		return -1;
	}

	if (copy_from_user(param, data->pointer, data->length)){
		wifi_printk(WIFI_DBG_ERROR,"copy data err\n");
		return -1;
	}

	wifi_printk(WIFI_ALWAYS, "ch[%d] rate[%d] power[%d] isWriteEfuse[%d]\n", 
		mfg_struct.channel, mfg_struct.rate, mfg_struct.power, mfg_struct.isWriteEfuse);

	if (priv->iftype != ATBM_NL80211_IFTYPE_STATION){
		atbmwifi_start_sta(priv);
	}

	if (!strcmp(param, "start")){
		atbm_wifi_mfg_start();
	}
	else if (!strcmp(param, "tx_pkt_bg")){
		ret = atbm_wifi_mfg_set_pktTxBG(mfg_struct.channel, mfg_struct.rate, mfg_struct.power);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_mfg_set_pktTxBG err\n");
		}
	}
	else if (!strcmp(param, "tx_pkt_n")){
		ret = atbm_wifi_mfg_set_PktTxN(mfg_struct.channel, mfg_struct.rate, mfg_struct.power);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_mfg_set_PktTxN err\n");
		}
	}
	else if (!strcmp(param, "pt_test")){
		ret = atbm_wifi_mfg_PT_Test(priv->if_id, mfg_struct.isWriteEfuse);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_mfg_PT_Test err\n");
		}
	}
	else if (!strcmp(param, "single_tone")){
		ret = atbm_wifi_mfg_CarrierTone(mfg_struct.channel);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_mfg_CarrierTone err\n");
		}
	}
	else if (!strcmp(param, "rx_pkt")){
		ret = atbm_wifi_mfg_set_PktRxMode(mfg_struct.channel);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_mfg_set_PktRxMode err\n");
		}
	}
	else if (!strcmp(param, "stop")){
		ret = atbm_wifi_mfg_stop();
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_mfg_stop err\n");
		}
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid argument:%s\n", param);
		ret = -1;
	}

	return ret;
}

static int atbm_iwpriv_etf_cfg(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, int *extra)
{
	int rate = 0;

	if (extra[0] < 1 || extra[0] > 14){
		wifi_printk(WIFI_DBG_ERROR,"invalid channel\n");
		return -1;
	}

	rate = trans_rate_enum(extra[1]);
	if (rate < 0){
		wifi_printk(WIFI_DBG_ERROR,"invalid rate\n");
		return -1;
	}

	etf_struct.channel = extra[0];
	etf_struct.rate = extra[1];
	etf_struct.is40m = extra[2];
	etf_struct.greenfield = extra[3];
	return 0;
}

static int atbm_iwpriv_etf(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, int *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	int ret = 0;
	char param[32];

	if (data->length <= 0){
		wifi_printk(WIFI_DBG_ERROR,"need 1 argument\n");
		return -1;
	}

	if (copy_from_user(param, data->pointer, data->length)){
		wifi_printk(WIFI_DBG_ERROR,"copy data err\n");
		return -1;
	}

	wifi_printk(WIFI_ALWAYS, "ch[%d] rate[%d] 40M[%d] greenfield[%d]\n", 
		etf_struct.channel, etf_struct.rate, etf_struct.is40m, etf_struct.greenfield);

	if (priv->iftype != ATBM_NL80211_IFTYPE_STATION){
		atbmwifi_start_sta(priv);
	}

	if (!strcmp(param, "start_tx")){
		ret = atbm_wifi_etf_start_tx(etf_struct.channel, etf_struct.rate, etf_struct.is40m, etf_struct.greenfield);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_etf_start_tx err\n");
		}
	}
	else if (!strcmp(param, "stop_tx")){
		ret = atbm_wifi_etf_stop_tx();
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_etf_stop_tx err\n");
		}
	}
	else if (!strcmp(param, "start_rx")){
		ret = atbm_wifi_etf_start_rx(etf_struct.channel, etf_struct.is40m);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_etf_start_rx err\n");
		}
	}
	else if (!strcmp(param, "stop_rx")){
		ret = atbm_wifi_etf_stop_rx();
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_etf_stop_rx err\n");
		}
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid argument:%s\n", param);
		ret = -1;
	}

	return ret;
}	

static int atbm_iwpriv_smartconfig(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	struct atbm_net_device *netdev = netdev_priv(dev);
	struct atbmwifi_vif *priv = (struct atbmwifi_vif *)netdev_drv_priv(netdev);
	int ret = 0;
	char param[32];

	if (priv->if_id != 0){
		wifi_printk(WIFI_DBG_ERROR,"only if_id 0 support STA mode\n");
		return -1;
	}

	if (data->length <= 0){
		wifi_printk(WIFI_DBG_ERROR,"need 1 argument\n");
		return -1;
	}

	if (copy_from_user(param, data->pointer, data->length)){
		wifi_printk(WIFI_DBG_ERROR,"copy data err\n");
		return -1;
	}

	if (priv->iftype != ATBM_NL80211_IFTYPE_STATION){
		atbmwifi_start_sta(priv);
	}
	
	if (!strcmp(param, "start")){
		ret = atbm_smartconfig_start();
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_smartconfig_start err\n");
		}
	}
	else if (!strcmp(param, "stop")){
		ret = atbm_smartconfig_stop();
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_smartconfig_stop err\n");
		}
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid argument:%s\n", param);
		ret = -1;
	}

	return ret;
}	

static int atbm_iwpriv_wps_wifi_mode(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	ATBM_WIFI_MODE mode;
	char param[32];

	if (data->length <= 0){
		wifi_printk(WIFI_DBG_ERROR,"need 1 argument\n");
		return -1;
	}

	if (copy_from_user(param, data->pointer, data->length)){
		wifi_printk(WIFI_DBG_ERROR,"copy data err\n");
		return -1;
	}

	if (!atbm_memcmp(param, "mode=", 5)){
		mode = trans_wifimode(param+5);
		if (mode == -1){
			wifi_printk(WIFI_DBG_ERROR,"invalid mode:%s\n", param+5);
			return -1;
		}

		AP_sta_mode = mode;
		return 0;
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid argument:%s\n", param);
		return -1;
	}
}

static int atbm_iwpriv_wpspbc_start(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
	wifi_printk(WIFI_ALWAYS, "wps wifi mode[%d]\n", AP_sta_mode);

	if (atbm_wpspbc_start(AP_sta_mode)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wpspbc_start err\n");
		return -1;
	}
	else {
		return 0;
	}
}

static int atbm_iwpriv_wpspin_start(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
#if CONFIG_WPS
	char pin[PIN_CODE_LENGTH+1];
	int i = 0;

	for (i=0; i<PIN_CODE_LENGTH; i++){
		pin[i] = extra[i];
	}
	pin[PIN_CODE_LENGTH] = '\0';
	
	wifi_printk(WIFI_ALWAYS, "wps wifi mode[%d]\n", AP_sta_mode);

	if (atbm_wpspin_start(AP_sta_mode, pin)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wpspin_start err\n");
		return -1;
	}
	else {
		return 0;
	}
#endif
	wifi_printk(WIFI_DBG_ERROR,"not support\n");
	return -EOPNOTSUPP;
}

static int atbm_iwpriv_wpsmode_cancel(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
#if CONFIG_WPS
	wifi_printk(WIFI_ALWAYS, "wps wifi mode[%d]\n", AP_sta_mode);

	if (atbm_wpsmode_cancel(AP_sta_mode)){
		wifi_printk(WIFI_DBG_ERROR,"atbm_wpsmode_cancel err\n");
		return -1;
	}
	else {
		return 0;
	}
#endif
	wifi_printk(WIFI_DBG_ERROR,"not support\n");
	return -EOPNOTSUPP;
}

static int atbm_iwpriv_p2p_mac(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
#if CONFIG_P2P
	int i = 0;

	for (i=0; i<ATBM_ETH_ALEN; i++){
		p2p_struct.mac[i] = extra[i];
	}

	wifi_printk(WIFI_ALWAYS, "p2p mac:%pM\n", p2p_struct.mac);
	return 0;
#endif
	wifi_printk(WIFI_DBG_ERROR,"not support\n");
	return -EOPNOTSUPP;
}

static int atbm_iwpriv_p2p_cfg(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, int *extra)
{
#if CONFIG_P2P
	wifi_printk(WIFI_ALWAYS, "p2p_cfg: go_intent[%d] timeout[%d]\n", extra[0], extra[1]);
	p2p_struct.go_intent = extra[0];
	p2p_struct.timeout = extra[1];
	return 0;
#endif
	wifi_printk(WIFI_DBG_ERROR,"not support\n");
	return -EOPNOTSUPP;
}

static int atbm_iwpriv_p2p(struct net_device *dev, struct iw_request_info *info,
				struct iw_point *data, char *extra)
{
#if CONFIG_P2P
	int ret = 0;
	char param[32];

	if (data->length <= 0){
		wifi_printk(WIFI_DBG_ERROR,"need 1 argument.\n");
		return -1;
	}

	if (copy_from_user(param, data->pointer, data->length)){
		wifi_printk(WIFI_DBG_ERROR,"copy data err\n");
		return -1;
	}

	if (!strcmp(param, "start")){
		ret = atbm_wifi_p2p_start();
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_p2p_start err\n");
		}
	}
	else if (!strcmp(param, "find")){
		ret = atbm_wifi_p2p_find(p2p_struct.timeout);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_p2p_find err\n");
		}
	}
	else if (!strcmp(param, "find_accept")){
		ret = atbm_wifi_p2p_find_accept(p2p_struct.go_intent);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_p2p_find_accept err\n");
		}
	}
	else if (!strcmp(param, "find_stop")){
		ret = atbm_wifi_p2p_find_stop(p2p_struct.timeout);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_p2p_find_stop err\n");
		}
	}
	else if (!strcmp(param, "show_peer")){
		ret = atbm_wifi_p2p_show_peers();
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_p2p_show_peers err\n");
		}
	}
	else if (!strcmp(param, "go_start")){
		ret = atbm_wifi_p2p_go_start();
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_p2p_go_start err\n");
		}
	}
	else if (!strcmp(param, "connect")){
		ret = atbm_wifi_p2p_connect(p2p_struct.mac, p2p_struct.go_intent);
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_p2p_connect err\n");
		}
	}
	else if (!strcmp(param, "stop")){
		ret = atbm_wifi_p2p_stop();
		if (ret){
			wifi_printk(WIFI_DBG_ERROR,"atbm_wifi_p2p_stop err\n");
		}
	}
	else {
		wifi_printk(WIFI_DBG_ERROR,"invalid argument:%s\n", extra);
		ret = -1;
	}

	return ret;
#endif
	wifi_printk(WIFI_DBG_ERROR,"not support\n");
	return -EOPNOTSUPP;
}

static const struct iw_priv_args cfg80211_private_args[] = {
	{SIOCIWFIRSTPRIV+0,  IW_PRIV_TYPE_CHAR|128,                    IW_PRIV_TYPE_NONE, "set"},
	{SIOCIWFIRSTPRIV+1,  IW_PRIV_TYPE_CHAR|32 ,                IW_PRIV_TYPE_CHAR|128, "get"},
	{SIOCIWFIRSTPRIV+2,  IW_PRIV_TYPE_CHAR|128,                    IW_PRIV_TYPE_NONE, "join_param"},
	{SIOCIWFIRSTPRIV+3,  IW_PRIV_TYPE_INT|IW_PRIV_SIZE_FIXED|1,    IW_PRIV_TYPE_NONE, "join"},
	{SIOCIWFIRSTPRIV+4,  IW_PRIV_TYPE_CHAR|128,                    IW_PRIV_TYPE_NONE, "ap_param"},
	{SIOCIWFIRSTPRIV+5,  IW_PRIV_TYPE_NONE,                        IW_PRIV_TYPE_NONE, "ap"},
	{SIOCIWFIRSTPRIV+6,  IW_PRIV_TYPE_INT|IW_PRIV_SIZE_FIXED|4,    IW_PRIV_TYPE_NONE, "mfg_cfg"},
	{SIOCIWFIRSTPRIV+7,  IW_PRIV_TYPE_CHAR|32,                     IW_PRIV_TYPE_NONE, "mfg"},
	{SIOCIWFIRSTPRIV+8,  IW_PRIV_TYPE_INT|IW_PRIV_SIZE_FIXED|4,    IW_PRIV_TYPE_NONE, "etf_cfg"},
	{SIOCIWFIRSTPRIV+9,  IW_PRIV_TYPE_CHAR|32,                     IW_PRIV_TYPE_NONE, "etf"},
	{SIOCIWFIRSTPRIV+10, IW_PRIV_TYPE_BYTE|IW_PRIV_SIZE_FIXED|6,   IW_PRIV_TYPE_NONE, "p2p_mac"},
	{SIOCIWFIRSTPRIV+11, IW_PRIV_TYPE_CHAR|32,                     IW_PRIV_TYPE_NONE, "p2p"},
	{SIOCIWFIRSTPRIV+12, IW_PRIV_TYPE_INT|IW_PRIV_SIZE_FIXED|2,    IW_PRIV_TYPE_NONE, "p2p_cfg"},
	{SIOCIWFIRSTPRIV+13, IW_PRIV_TYPE_CHAR|32,                     IW_PRIV_TYPE_NONE, "efuse_option"},
	{SIOCIWFIRSTPRIV+14, IW_PRIV_TYPE_BYTE|IW_PRIV_SIZE_FIXED|6,   IW_PRIV_TYPE_NONE, "efuse_mac"},
	{SIOCIWFIRSTPRIV+15, IW_PRIV_TYPE_CHAR|128,                    IW_PRIV_TYPE_NONE, "smartconfig"},
	{SIOCIWFIRSTPRIV+16, IW_PRIV_TYPE_INT|IW_PRIV_SIZE_FIXED|2,    IW_PRIV_TYPE_NONE, "tx_time"},
	{SIOCIWFIRSTPRIV+17, IW_PRIV_TYPE_CHAR|32,                     IW_PRIV_TYPE_NONE, "wps_wifi_mode"},
	{SIOCIWFIRSTPRIV+18, IW_PRIV_TYPE_INT|IW_PRIV_SIZE_FIXED|2,    IW_PRIV_TYPE_NONE, "retry"},
	{SIOCIWFIRSTPRIV+19, IW_PRIV_TYPE_NONE,                        IW_PRIV_TYPE_NONE, "wpspbc_start"},
	{SIOCIWFIRSTPRIV+20, IW_PRIV_TYPE_INT|IW_PRIV_SIZE_FIXED|32,   IW_PRIV_TYPE_NONE, "set_256_efuse"},
	{SIOCIWFIRSTPRIV+21, IW_PRIV_TYPE_NONE,                        IW_PRIV_TYPE_NONE, "wps_mode_cancel"},
	{SIOCIWFIRSTPRIV+22, IW_PRIV_TYPE_BYTE|IW_PRIV_SIZE_FIXED|8,   IW_PRIV_TYPE_NONE, "wpspin_start"},
};

static const iw_handler cfg80211_private_handler[] = {
	(iw_handler) atbm_iwpriv_set_info,
	(iw_handler) atbm_iwpriv_get_info,
	(iw_handler) atbm_iwpriv_set_join_param,
 	(iw_handler) atbm_iwpriv_join,
 	(iw_handler) atbm_iwpriv_set_ap_param,
	(iw_handler) atbm_iwpriv_ap,
	(iw_handler) atbm_iwpriv_mfg_cfg,
	(iw_handler) atbm_iwpriv_mfg,
	(iw_handler) atbm_iwpriv_etf_cfg,
	(iw_handler) atbm_iwpriv_etf,
	(iw_handler) atbm_iwpriv_p2p_mac,
	(iw_handler) atbm_iwpriv_p2p,
	(iw_handler) atbm_iwpriv_p2p_cfg,
	(iw_handler) atbm_iwpriv_set_efuse_item,
	(iw_handler) atbm_iwpriv_set_efuse_mac,
	(iw_handler) atbm_iwpriv_smartconfig,
	(iw_handler) atbm_iwpriv_set_tx_time,
	(iw_handler) atbm_iwpriv_wps_wifi_mode,
	(iw_handler) atbm_iwpriv_set_retry,
	(iw_handler) atbm_iwpriv_wpspbc_start,
	(iw_handler) atbm_iwpriv_set_256bits_efuse,
	(iw_handler) atbm_iwpriv_wpsmode_cancel,
	(iw_handler) atbm_iwpriv_wpspin_start,
	(iw_handler) atbm_iwpriv_wps_wifi_mode,
	(iw_handler) atbm_iwpriv_wpspin_start,
};

const struct iw_handler_def cfg80211_wext_handler = {
	.num_standard		= ARRAY_SIZE(cfg80211_handlers),
	.standard		    = cfg80211_handlers,
	.num_private        = ARRAY_SIZE(cfg80211_private_handler),
	.private            = cfg80211_private_handler,
	.num_private_args   = ARRAY_SIZE(cfg80211_private_args),
	.private_args       = cfg80211_private_args,
	.get_wireless_stats = atbm_wireless_stats,
};

extern struct net init_net;

struct atbm_net_device * atbm_alloc_netdev(atbm_int32 size)
{
	ATBM_NETIF *nif;
	struct atbm_net_device *netdev;
	static int index = 0;
	int ret = 0;

	rtnl_lock();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,00))
		nif = alloc_netdev_mqs(size + sizeof(*netdev),
					wifi_name[index],NET_NAME_UNKNOWN, atbm_if_setup, 4, 1);
#else	
		nif = alloc_netdev_mqs(size + sizeof(*netdev),
					wifi_name[index], atbm_if_setup, 4, 1);
#endif
		if (!nif){
			rtnl_unlock();
			return NULL;
		}

	dev_net_set(nif, &init_net);

#ifdef CHKSUM_HW_SUPPORT
	nif->hw_features = (NETIF_F_RXCSUM|NETIF_F_IP_CSUM);
	nif->features |= ndev->hw_features;
	wifi_printk(WIFI_ALWAYS,"+++++++++++++++++++++++++++++++hw checksum open ++++++++++++++++++++\n");
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26))
	nif->needed_headroom =  EXTRA_TX_HEADROOM 
							+ 4*6 /* four MAC addresses */
							+ 2 + 2 + 2 + 2 /* ctl, dur, seq, qos */
							+ 6 /* mesh */
							+ 8 /* rfc1042/bridge tunnel */
							- ETH_HLEN /* ethernet hard_header_len */
							+ IEEE80211_ENCRYPT_HEADROOM;
	nif->needed_tailroom = IEEE80211_ENCRYPT_TAILROOM;
#endif

	ret = dev_alloc_name(nif, nif->name);
	
	if(ret<0){
		goto fail;
	}

	nif->wireless_handlers = &cfg80211_wext_handler;

	ret = register_netdevice(nif);
	if (ret)
		goto fail;

	netdev = netdev_priv(nif);
	netdev->nif = nif;
	index++;
	rtnl_unlock();

	atbm_memset(&join_struct, 0, sizeof(join_struct));
	atbm_memset(&ap_struct, 0, sizeof(ap_struct));
	atbm_memset(&time_struct, 0, sizeof(time_struct));
	atbm_memset(&etf_struct, 0, sizeof(etf_struct));
	atbm_memset(&p2p_struct, 0, sizeof(p2p_struct));
	atbm_memset(&mfg_struct, 0, sizeof(mfg_struct));

	return  netdev;

fail:
	free_netdev(nif);
	rtnl_unlock();
	return NULL;
}
atbm_void * netdev_drv_priv(struct atbm_net_device *ndev)
{
	return &ndev->drv_priv[0];

}
atbm_void atbm_free_netdev(struct atbm_net_device * netdev)
{
	if(netdev != ATBM_NULL){
		rtnl_lock();
		unregister_netdevice(netdev->nif);
		rtnl_unlock();
	}
}

atbm_void atbm_set_netif(ATBM_NETIF *pNetIf)
{
}
