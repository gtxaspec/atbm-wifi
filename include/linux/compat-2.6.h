#ifndef LINUX_26_COMPAT_H
#define LINUX_26_COMPAT_H

#include <linux/version.h>

#define LINUX_VERSION_IS_LESS(x1,x2,x3)	(LINUX_VERSION_CODE < KERNEL_VERSION(x1,x2,x3))
#define LINUX_VERSION_IS_GEQ(x1,x2,x3)	(LINUX_VERSION_CODE >= KERNEL_VERSION(x1,x2,x3))
#define LINUX_VERSION_IN_RANGE(x1,x2,x3, y1,y2,y3) \
	(LINUX_VERSION_IS_GEQ(x1,x2,x3) && LINUX_VERSION_IS_LESS(y1,y2,y3))
	
#define LINUX_VERSION_IS_LEQ_AND_NOT_CPTCFG(x1,x2,x3) ((LINUX_VERSION_CODE <= KERNEL_VERSION(x1,x2,x3))&&(!CONFIG_CPTCFG_CFG80211))

#define LINUX_VERSION_IS_LESS_AND_NOT_CPTCFG(x1,x2,x3) ((LINUX_VERSION_CODE < KERNEL_VERSION(x1,x2,x3))&&(!CONFIG_CPTCFG_CFG80211))
#define LINUX_VERSION_IS_LESS_OR_CPTCFG(x1,x2,x3) ((LINUX_VERSION_CODE < KERNEL_VERSION(x1,x2,x3))||(CONFIG_CPTCFG_CFG80211))
#define LINUX_VERSION_IS_LESS_AND_CPTCFG(x1,x2,x3) ((LINUX_VERSION_CODE < KERNEL_VERSION(x1,x2,x3))&&(CONFIG_CPTCFG_CFG80211))
#define LINUX_VERSION_IS_GEQ_OR_CPTCFG(x1,x2,x3) ((LINUX_VERSION_CODE >= KERNEL_VERSION(x1,x2,x3))||(CONFIG_CPTCFG_CFG80211))
#define LINUX_VERSION_IS_BIG_OR_CPTCFG_OR_COMPAT(x1,x2,x3) ((LINUX_VERSION_CODE >  KERNEL_VERSION(x1,x2,x3))||(CONFIG_CPTCFG_CFG80211)||CONFIG_CFG80211_COMPAT_MODIFIED)

#define LINUX_VERSION_IS_GEQ_OR_CPTCFG_OR_COMPAT(x1,x2,x3) ((LINUX_VERSION_CODE >=  KERNEL_VERSION(x1,x2,x3))||(CONFIG_CPTCFG_CFG80211)||CONFIG_CFG80211_COMPAT_MODIFIED)


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
#include <linux/kconfig.h>
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
#include <generated/autoconf.h>
#else
#include <linux/autoconf.h>
#endif

/* STERICSSON_WLAN_BUILT_IN hack*/
#ifndef COMPAT_STATIC
#include <linux/compat_autoconf.h>
#endif


#ifndef RHEL_RELEASE_VERSION
#define RHEL_RELEASE_VERSION(a,b) (((a) << 8) + (b))
#endif

#ifndef RHEL_RELEASE_CODE
#define RHEL_RELEASE_CODE 0
#endif//RHEL_RELEASE_CODE

#if LINUX_VERSION_IS_LESS(3,10,103)
#ifndef U32_MAX
#define U32_MAX 0xffffffff
#endif//RHEL_RELEASE_CODE
#endif

#if LINUX_VERSION_IS_LESS(4,9,0)

#ifndef GENL_UNS_ADMIN_PERM
#define GENL_UNS_ADMIN_PERM GENL_ADMIN_PERM
#endif
#endif

#ifndef CONFIG_CPTCFG_CFG80211
#define CONFIG_CPTCFG_CFG80211 0
#endif  //CONFIG_CPTCFG_CFG80211


#ifndef CONFIG_CFG80211_COMPAT_MODIFIED
#define CONFIG_CFG80211_COMPAT_MODIFIED 0
#endif  //CONFIG_CFG80211_COMPAT_MODIFIED


#if LINUX_VERSION_IS_LESS(3,11,0)
#define netdev_notifier_info_to_dev(ndev) ndev
#endif

/***************************************************************/
#if 0
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0))

typedef struct {
#ifdef CONFIG_NET_NS
    struct net *net;
#endif
} possible_net_t;

#else  //#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0))

#define possible_net_t struct net *
#endif  //#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0))
#endif
/***************************************************************/


#if CONFIG_CPTCFG_CFG80211
#include "backport/backport.h"
#else //CONFIG_CPTCFG_CFG80211
//#define __ASSEMBLY__

#ifndef __ASSEMBLY__
#define LINUX_BACKPORT(__sym) backport_ ##__sym
#endif //__ASSEMBLY__

#endif  //CONFIG_CPTCFG_CFG80211
/*
 * Each compat file represents compatibility code for new kernel
 * code introduced for *that* kernel revision.
 */

#include <linux/compat-2.6.22.h>
#include <linux/compat-2.6.23.h>
#include <linux/compat-2.6.24.h>
#include <linux/compat-2.6.25.h>
#include <linux/compat-2.6.26.h>
#include <linux/compat-2.6.27.h>
#include <linux/compat-2.6.28.h>
#include <linux/compat-2.6.29.h>
#include <linux/compat-2.6.30.h>
#include <linux/compat-2.6.31.h>
#include <linux/compat-2.6.32.h>
#include <linux/compat-2.6.33.h>
#include <linux/compat-2.6.34.h>
#include <linux/compat-2.6.35.h>
#include <linux/compat-2.6.36.h>
#include <linux/compat-2.6.37.h>
#include <linux/compat-2.6.38.h>
#include <linux/compat-2.6.39.h>
#include <linux/compat-3.0.h>
#include <linux/compat-3.1.h>
#include <linux/compat-3.2.h>
#include <linux/compat-3.3.h>
#include <linux/compat-3.4.h>
#include <linux/compat-3.5.h>
#include <linux/compat-3.8.h>
#include <linux/compat-3.10.h>
#endif /* LINUX_26_COMPAT_H */
