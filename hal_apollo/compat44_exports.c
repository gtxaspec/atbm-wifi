// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel 4.4 compatibility exports for atbm6062u driver
 * Provides missing cfg80211/ieee80211 symbols as exported functions
 *
 * COMPAT44_EXPORTS_BUILD is defined in the Makefile for this file,
 * which causes compat-4.4.h to provide extern declarations instead of
 * static inline definitions. We just implement the functions here.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <net/cfg80211.h>

/* Implementation of ieee80211_get_channel_khz */
struct ieee80211_channel *
ieee80211_get_channel_khz(struct wiphy *wiphy, u32 freq_khz)
{
	return ieee80211_get_channel(wiphy, freq_khz / 1000);
}
EXPORT_SYMBOL(ieee80211_get_channel_khz);

int ieee80211_channel_to_freq_khz(int chan, enum nl80211_band band)
{
	return ieee80211_channel_to_frequency(chan, band) * 1000;
}
EXPORT_SYMBOL(ieee80211_channel_to_freq_khz);

int ieee80211_freq_khz_to_channel(u32 freq_khz)
{
	return ieee80211_frequency_to_channel(freq_khz / 1000);
}
EXPORT_SYMBOL(ieee80211_freq_khz_to_channel);

int cfg80211_rx_mgmt_khz(struct wireless_dev *wdev, int freq_khz, int sig_dbm,
			 const u8 *buf, size_t len, u32 flags)
{
	return cfg80211_rx_mgmt(wdev, freq_khz / 1000, sig_dbm, buf, len, flags) ? 0 : -EINVAL;
}
EXPORT_SYMBOL(cfg80211_rx_mgmt_khz);

void cfg80211_report_obss_beacon_khz(struct wiphy *wiphy,
				     const u8 *frame, size_t len,
				     int freq_khz, int sig_dbm)
{
	cfg80211_report_obss_beacon(wiphy, frame, len, freq_khz / 1000, sig_dbm);
}
EXPORT_SYMBOL(cfg80211_report_obss_beacon_khz);

int ieee80211_data_to_8023_exthdr(struct sk_buff *skb, struct ethhdr *ehdr,
				  const u8 *addr, enum nl80211_iftype iftype,
				  u8 data_offset)
{
	/* Older kernel doesn't support data_offset, just call regular version */
	return ieee80211_data_to_8023(skb, addr, iftype);
}
EXPORT_SYMBOL(ieee80211_data_to_8023_exthdr);

const struct element *
ieee80211_bss_get_elem(struct cfg80211_bss *bss, u8 id)
{
	const struct cfg80211_bss_ies *ies;

	ies = rcu_dereference(bss->ies);
	if (!ies)
		return NULL;

	return (const struct element *)cfg80211_find_ie(id, ies->data, ies->len);
}
EXPORT_SYMBOL(ieee80211_bss_get_elem);

const struct element *
cfg80211_find_elem_match(u8 eid, const u8 *ies, unsigned int len,
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
				return (const struct element *)pos;

			if (pos[1] >= match_offset + match_len &&
			    memcmp(pos + 2 + match_offset, match, match_len) == 0)
				return (const struct element *)pos;
		}

		pos += 2 + pos[1];
	}

	return NULL;
}
EXPORT_SYMBOL(cfg80211_find_elem_match);

int cfg80211_bss_color_notify(struct net_device *dev, gfp_t gfp,
			       enum nl80211_commands cmd, u8 count, u64 color_bitmap)
{
	/* Not supported in kernel 4.4, just return success */
	return 0;
}
EXPORT_SYMBOL(cfg80211_bss_color_notify);

void cfg80211_merge_profile(const u8 *ie, size_t ielen,
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
EXPORT_SYMBOL(cfg80211_merge_profile);
