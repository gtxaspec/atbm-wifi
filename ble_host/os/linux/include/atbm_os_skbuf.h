#ifndef ATBM_OS_SKBUFF_H
#define ATBM_OS_SKBUFF_H

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/ieee80211_radiotap.h>
#include <linux/ieee80211.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/leds.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/rtnetlink.h>
#include <net/cfg80211.h>
//#include <net/cfg80211-wext.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/rfkill.h>
#include <net/genetlink.h>


#define IEEE80211_ENCRYPT_HEADROOM 20
#define IEEE80211_ENCRYPT_TAILROOM 18

typedef struct net_device ATBM_NETIF;

struct atbm_net_device * atbm_alloc_netdev(atbm_int32 size);
atbm_void atbm_skbbuffer_init(atbm_void);
atbm_void * netdev_drv_priv(struct atbm_net_device *ndev);
int  atbm_register_netdevice(struct atbm_net_device *netdev);

#endif /* ATBM_OS_SKBUFF_H */

