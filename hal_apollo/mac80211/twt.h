/*
 * Copyright 2002-2005, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef TWT_H
#define TWT_H

#include <linux/list.h>
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/workqueue.h>
#include <linux/average.h>
#include <linux/ratelimit.h>
#include "atbm_workqueue.h"

bool ieee80211_process_rx_twt_action(struct ieee80211_rx_data *rx);
void ieee80211_rx_twt_action(struct ieee80211_sub_if_data *sdata,struct sta_info *sta,struct sk_buff *skb);
bool ieee80211_twt_sta_request_work(struct sta_info *sta,struct ieee80211_twt_request_params *twt_request);
bool ieee80211_twt_sta_suspend_state_work(struct ieee80211_sub_if_data *sdata,enum ieee80211_sta_twt_suspend_state state);
void ieee80211_twt_sta_init(struct sta_info *sta);
void ieee80211_twt_sta_deinit(struct sta_info *sta);
bool ieee80211_tx_h_twt_buf(struct ieee80211_sub_if_data* sdata, struct sta_info* sta, struct sk_buff* skb);
bool ieee80211_twt_sta_teardown(struct sta_info *sta);
#endif /* STA_INFO_H */
