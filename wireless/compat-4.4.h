/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Compatibility header for kernel 4.4
 * Provides missing definitions and structures for newer cfg80211 APIs
 */

#ifndef __COMPAT_4_4_H
#define __COMPAT_4_4_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/skbuff.h>
#include <net/cfg80211.h>

/* skb_put_zero was added in kernel 4.13 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0)
static inline void *skb_put_zero(struct sk_buff *skb, unsigned int len)
{
	void *tmp = skb_put(skb, len);
	memset(tmp, 0, len);
	return tmp;
}
#endif

/* Firmware path configuration */
#ifndef CONFIG_FW_NAME_WIFI6
#define CONFIG_FW_NAME_WIFI6 "atbm6062u_wifi.bin"
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)

/* struct element was added in kernel 4.18 */
struct element {
	u8 id;
	u8 datalen;
	u8 data[];
} __packed;

/* transmitted_bss was added in kernel 4.19 for multi-BSSID support */
#define cfg80211_bss_has_transmitted_bss(bss) (0)

/* CFG80211_MAX_WEP_KEYS was added later */
#ifndef CFG80211_MAX_WEP_KEYS
#define CFG80211_MAX_WEP_KEYS 4
#endif

/* struct cfg80211_connect_resp_params was added in kernel 4.12 */
struct cfg80211_connect_resp_params {
	int status;
	const u8 *bssid;
	const u8 *req_ie;
	size_t req_ie_len;
	const u8 *resp_ie;
	size_t resp_ie_len;
	u16 timeout_reason;
};

/* struct cfg80211_roam_info was added in kernel 4.12 */
struct cfg80211_roam_info {
	struct ieee80211_channel *channel;
	const u8 *bssid;
	const u8 *req_ie;
	size_t req_ie_len;
	const u8 *resp_ie;
	size_t resp_ie_len;
};

/* struct netlink_ext_ack was added in kernel 4.12 */
struct netlink_ext_ack {
	const char *_msg;
};

/* struct mgmt_frame_regs was added in kernel 5.8 */
struct mgmt_frame_regs {
	u16 global_stypes;
	u16 interface_stypes;
};

/*
 * When building compat44_exports.c, we skip the static inline definitions
 * and only provide the exported function declarations
 */
#ifndef COMPAT44_EXPORTS_BUILD

/* Newer _khz variants of frequency functions - map to older MHz versions */
#define ieee80211_channel_to_freq_khz(chan, band) \
	(ieee80211_channel_to_frequency(chan, band) * 1000)

#define ieee80211_freq_khz_to_channel(freq_khz) \
	ieee80211_frequency_to_channel((freq_khz) / 1000)

static inline struct ieee80211_channel *
ieee80211_get_channel_khz(struct wiphy *wiphy, u32 freq_khz)
{
	return ieee80211_get_channel(wiphy, freq_khz / 1000);
}

/* cfg80211_rx_mgmt_khz - map to older cfg80211_rx_mgmt (returns bool in 4.4) */
static inline int
cfg80211_rx_mgmt_khz(struct wireless_dev *wdev, int freq_khz, int sig_dbm,
		     const u8 *buf, size_t len, u32 flags)
{
	return cfg80211_rx_mgmt(wdev, freq_khz / 1000, sig_dbm, buf, len, flags) ? 0 : -EINVAL;
}

/* cfg80211_report_obss_beacon_khz - map to older version */
#define cfg80211_report_obss_beacon_khz(wiphy, frame, len, freq_khz, sig_dbm) \
	cfg80211_report_obss_beacon(wiphy, frame, len, (freq_khz) / 1000, sig_dbm)

/* cfg80211_find_ie_match - implement matching function not in 4.4 */
static inline const u8 *
cfg80211_find_ie_match(u8 eid, const u8 *ies, unsigned int len,
		       const u8 *match, unsigned int match_len,
		       unsigned int match_offset)
{
	const u8 *pos = ies;

	while (pos + 1 < ies + len) {
		if (pos + 2 + pos[1] > ies + len)
			break;

		if (pos[0] == eid) {
			/* Found the element, check if it matches */
			if (match_len == 0)
				return pos;

			if (pos[1] >= match_offset + match_len &&
			    memcmp(pos + 2 + match_offset, match, match_len) == 0)
				return pos;
		}

		pos += 2 + pos[1];
	}

	return NULL;
}

/* ieee80211_bss_get_elem - newer element access API */
static inline const struct element *
ieee80211_bss_get_elem(struct cfg80211_bss *bss, u8 id)
{
	const struct cfg80211_bss_ies *ies;

	ies = rcu_dereference(bss->ies);
	if (!ies)
		return NULL;

	return (const struct element *)cfg80211_find_ie(id, ies->data, ies->len);
}

/* cfg80211_find_elem_match - newer element matching API */
static inline const struct element *
cfg80211_find_elem_match(u8 eid, const u8 *ies, unsigned int len,
			 const u8 *match, unsigned int match_len,
			 unsigned int match_offset)
{
	return (const struct element *)cfg80211_find_ie_match(eid, ies, len,
							      match, match_len,
							      match_offset);
}

/* ieee80211_data_to_8023_exthdr - extended header version */
static inline int
ieee80211_data_to_8023_exthdr(struct sk_buff *skb, struct ethhdr *ehdr,
			      const u8 *addr, enum nl80211_iftype iftype,
			      u8 data_offset)
{
	/* Older kernel doesn't support data_offset, just call regular version */
	return ieee80211_data_to_8023(skb, addr, iftype);
}

/* cfg80211_bss_color_notify - WiFi 6 BSS color notification (not in 4.4) */
static inline int
cfg80211_bss_color_notify(struct net_device *dev, gfp_t gfp,
			  enum nl80211_commands cmd, u8 count, u64 color_bitmap)
{
	/* Not supported in kernel 4.4, just return success */
	return 0;
}

/* cfg80211_merge_profile - WiFi 6 multi-BSSID profile merging (not in 4.4) */
static inline void
cfg80211_merge_profile(const u8 *ie, size_t ielen,
		       const struct element *mbssid_elem,
		       const struct element *sub_elem,
		       u8 *merged_ie, size_t *merged_len)
{
	/* Not supported in kernel 4.4, just copy original IE */
	if (merged_ie && merged_len) {
		memcpy(merged_ie, ie, ielen);
		*merged_len = ielen;
	}
}

#else /* COMPAT44_EXPORTS_BUILD */

/* When building exports, just declare the functions as extern */
extern struct ieee80211_channel *ieee80211_get_channel_khz(struct wiphy *wiphy, u32 freq_khz);
extern int ieee80211_channel_to_freq_khz(int chan, enum nl80211_band band);
extern int ieee80211_freq_khz_to_channel(u32 freq_khz);
extern int cfg80211_rx_mgmt_khz(struct wireless_dev *wdev, int freq_khz, int sig_dbm,
				const u8 *buf, size_t len, u32 flags);
extern void cfg80211_report_obss_beacon_khz(struct wiphy *wiphy,
					     const u8 *frame, size_t len,
					     int freq_khz, int sig_dbm);
extern int ieee80211_data_to_8023_exthdr(struct sk_buff *skb, struct ethhdr *ehdr,
					  const u8 *addr, enum nl80211_iftype iftype,
					  u8 data_offset);
extern const struct element *ieee80211_bss_get_elem(struct cfg80211_bss *bss, u8 id);
extern const struct element *cfg80211_find_elem_match(u8 eid, const u8 *ies, unsigned int len,
						       const u8 *match, unsigned int match_len,
						       unsigned int match_offset);
extern int cfg80211_bss_color_notify(struct net_device *dev, gfp_t gfp,
				      enum nl80211_commands cmd, u8 count, u64 color_bitmap);
extern void cfg80211_merge_profile(const u8 *ie, size_t ielen,
				    const struct element *mbssid_elem,
				    const struct element *sub_elem,
				    u8 *merged_ie, size_t *merged_len);

#endif /* COMPAT44_EXPORTS_BUILD */

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0) */

#endif /* __COMPAT_4_4_H */

