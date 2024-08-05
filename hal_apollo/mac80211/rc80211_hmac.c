

/*
* Copyright (c) 2016, altobeam
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*/
//#ifdef CONFIG_MAC80211_RC_PID
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/ieee80211.h>
#include <net/atbm_mac80211.h>
#include "rate.h"
#include "mesh.h"
#include "rc80211_hmac.h"
//#include "apollo.h"
#include "ieee80211_i.h"


u16 			g_ch_idle_ratio = 500;

/*11b rate: 		1M,2M,5.5M,11M*/
u8				b_rate_group[4] =
{
	2, 4, 11, 22
};


// HT mode, MSC index and S short GI
//						0L	0S	1L	1S	2L	2S	3L	4L	 5L   6L   7L	7S
// uint8_t n_rate_group[15] = {13, 14, 26, 28, 39, 43, 52, 78, 104, 117, 130, 144}; // n rate; the real rate*2; 144 is mcs7/ short gi

/*BPSK QPSK QPSK 16Q  16Q 64Q  64Q	64Q*/
/*0,   1,	2,	 3,   4,  5,   6,	7 */
u8				n_rate_group[8] =
{
	13, 26, 39, 52, 78, 104, 117, 130
};


// n rate; the real rate*2; 130 is mcs7/ long gi
u8				n_rate_groups[8] =
{
	14, 28, 43, 58, 87, 116, 130, 144
};


// n rate; the real rate*2; 144 is mcs7/ short gi

/*11g mode*/
u8				g_rate_group[12] =
{
	12, 18, 24, 36, 48, 72, 96, 108
};


// b/a rate; the real rate*2

/*HE mode MSC0 - MCS7 rate X 2 */

/*MSC index:				 0	 1	 2	 3	  4    5	6	  7    8	9	 10   11 */
u8				ax_rate_group8[12] =
{
	17, 34, 52, 69, 103, 138, 155, 172, 206, 229, 255, 255
};


//258, 287
u8				ax_rate_group16[12] =
{
	16, 33, 49, 65, 98, 130, 146, 163, 195, 217, 244, 255
};


//271
u8				ax_rate_group32[12] =
{
	15, 29, 44, 59, 88, 117, 132, 146, 175, 195, 219, 243
};


/*the highest rate
MCS0-3 3.2
MCS4-7 1.6
MCS8-11 0.8
other rates following using same GI
*/

/*DCM MCS index:				 0	 1	 3	 4 */
u8				ax_rate_group_dcm8[4] =
{
	9, 17, 34, 52
};


u8				ax_rate_group_dcm16[4] =
{
	8, 16, 33, 49
};


u8				ax_rate_group_dcm32[4] =
{
	7, 15, 29, 44
};


/*ER no DCM MCS index: 0,  1,	2*/
u8				ax_rate_er242_8[3] =
{
	17, 34, 52
};


u8				ax_rate_er242_16[3] =
{
	16, 33, 49
};


u8				ax_rate_er242_32[3] =
{
	15, 29, 44
};

/*set 1, mcs0,1,2 use 2xp16
  set 0, mcs0,1,2 use 4xp32
*/
int he_set_mcs_2x_p16 = 1;


unsigned char bw_select_40m(signed char rssi, unsigned char ch_idle_ratio, unsigned short main_snd_ratio, 
	unsigned char m_tp_rate, u8 mtp_prob, unsigned char cur_band40_2_20m, unsigned short bww_pkt_cnt);

unsigned char bw_select_40m_simple(signed char rssi, 
	unsigned char m_tp_rate, u8 mtp_prob, unsigned char cur_band40_2_20m, 
	unsigned short bw_pkt_cnt);

void reset_rate_info(RATE_CONTROL_RATE * pHmac_rate, u8 is_ap_flag)
{
	int 			i;

	memset(pHmac_rate, '\0', sizeof(RATE_CONTROL_RATE));

	pHmac_rate->max_tp_rate_idx = 0xff;
	pHmac_rate->last_max_tp_rate_idx = 0xff;
	pHmac_rate->fast_sample_flag = 0;
	pHmac_rate->resample_required = 1;

	for (i = 0; i < RATE_CONTROL_RATE_NUM; i++)
	{
		pHmac_rate->sucess_prob[i] = 0;
	}

	pHmac_rate->is_ap_flag = is_ap_flag;
}


/*Phy rate index map to tx descr rate index*/
const s8		RateIndexMapFromPhyToDescr[38] =
{
	/*11b rate index*/
	DESCR_RATE_INDEX_B_1M, 
	DESCR_RATE_INDEX_B_2M, 
	DESCR_RATE_INDEX_B_5_5M, 
	DESCR_RATE_INDEX_B_11M, 

	/*unused rate index*/
	DESCR_RATE_INDEX_B_11M, 
	DESCR_RATE_INDEX_B_11M, 

	/*11 g rate index*/
	DESCR_RATE_INDEX_G_6M, 
	DESCR_RATE_INDEX_G_9M, 
	DESCR_RATE_INDEX_G_12M, 
	DESCR_RATE_INDEX_G_18M, 
	DESCR_RATE_INDEX_G_24M, 
	DESCR_RATE_INDEX_G_36M, 
	DESCR_RATE_INDEX_G_48M, 
	DESCR_RATE_INDEX_G_54M, 

	/*11n rate index*/
	DESCR_RATE_INDEX_HT_MCS0, 
	DESCR_RATE_INDEX_HT_MCS1, 
	DESCR_RATE_INDEX_HT_MCS2, 
	DESCR_RATE_INDEX_HT_MCS3, 
	DESCR_RATE_INDEX_HT_MCS4, 
	DESCR_RATE_INDEX_HT_MCS5, 
	DESCR_RATE_INDEX_HT_MCS6, 
	DESCR_RATE_INDEX_HT_MCS7, 

	/*msc32*/
	DESCR_RATE_INDEX_HT_MCS32, 

	/*HE Rate Index*/
	DESCR_RATE_INDEX_HE_MSCER0, 
	DESCR_RATE_INDEX_HE_MSCER1, 
	DESCR_RATE_INDEX_HE_MSCER2, 
	DESCR_RATE_INDEX_HE_MSC0, 
	DESCR_RATE_INDEX_HE_MSC1, 
	DESCR_RATE_INDEX_HE_MSC2, 
	DESCR_RATE_INDEX_HE_MSC3, 
	DESCR_RATE_INDEX_HE_MSC4, 
	DESCR_RATE_INDEX_HE_MSC5, 
	DESCR_RATE_INDEX_HE_MSC6, 
	DESCR_RATE_INDEX_HE_MSC7, 
	DESCR_RATE_INDEX_HE_MSC8, 
	DESCR_RATE_INDEX_HE_MSC9, 
	DESCR_RATE_INDEX_HE_MSC10, 
	DESCR_RATE_INDEX_HE_MSC11, 
};


/*Decr rate index to phy rate index*/
const s8		RateIndexMapFromDescrToPhy[38] =
{
	/*HE index*/
	RATE_INDEX_HE_MCS11, 
	RATE_INDEX_HE_MCS10, 
	RATE_INDEX_HE_MCS9, 
	RATE_INDEX_HE_MCS8, 
	RATE_INDEX_HE_MCS7, 
	RATE_INDEX_HE_MCS6, 
	RATE_INDEX_HE_MCS5, 
	RATE_INDEX_HE_MCS4, 
	RATE_INDEX_HE_MCS3, 
	RATE_INDEX_HE_MCS2, 
	RATE_INDEX_HE_MCS1, 
	RATE_INDEX_HE_MCS0, 
	RATE_INDEX_HE_MCSER2, 
	RATE_INDEX_HE_MCSER1, 
	RATE_INDEX_HE_MCSER0, 

	/*11n index*/
	RATE_INDEX_N_65M, 
	RATE_INDEX_N_58_5M, 
	RATE_INDEX_N_52M, 
	RATE_INDEX_N_39M, 
	RATE_INDEX_N_26M, 
	RATE_INDEX_N_19_5M, 
	RATE_INDEX_N_13M, 
	RATE_INDEX_N_6_5M, 
	RATE_INDEX_N_MCS32_6M, 

	/*11g*/
	RATE_INDEX_A_54M, 
	RATE_INDEX_A_48M, 
	RATE_INDEX_A_36M, 
	RATE_INDEX_A_24M, 
	RATE_INDEX_A_18M, 
	RATE_INDEX_A_12M, 
	RATE_INDEX_A_9M, 
	RATE_INDEX_A_6M, 
	RATE_INDEX_B_11M, 
	RATE_INDEX_B_5_5M, 
	RATE_INDEX_B_2M, 
	RATE_INDEX_B_1M
};


const char *	strRateIndexPhy[] =
{
	/*11b rate index*/
	"B_1M", 
	"B_2M", 
	"B55M", 
	"B11M", 

	/*unused rate index*/
	"B11M", 
	"B11M", 

	/*11 g rate index*/
	"G_6M", 
	"G_9M", 
	"G12M", 
	"G18M", 
	"G24M", 
	"G36M", 
	"G48M", 
	"G54M", 

	/*11n rate index*/
	"N_S0", 
	"N_S1", 
	"N_S2", 
	"N_S3", 
	"N_S4", 
	"N_S5", 
	"N_S6", 
	"N_S7", 

	/*msc32*/
	"NMS32", 

	/*HE Rate Index*/
	"HER0", 
	"HER1", 
	"HER2", 
	"HES0", 
	"HES1", 
	"HES2", 
	"HES3", 
	"HES4", 
	"HES5", 
	"HES6", 
	"HES7", 
	"HES8", 
	"HES9", 
	"HE10", 
	"HE11", 
};


const char *	strRateIndexDesc[] =
{
	/*HE index*/
	"HE11", 
	"HE10", 
	"HES9", 
	"HES8", 
	"HES7", 
	"HES6", 
	"HES5", 
	"HES4", 
	"HES3", 
	"HES2", 
	"HES1", 
	"HES0", 
	"HER2", 
	"HER1", 
	"HER0", 

	/*11n index*/
	"N65M", 
	"N58M", 
	"N52M", 
	"N39M", 
	"N26M", 
	"N19M", 
	"N13M", 
	"N_6M", 
	"N_6M", 

	/*11g*/
	"G54M", 
	"G48M", 
	"G36M", 
	"G24M", 
	"G18M", 
	"G12M", 
	"G_9M", 
	"G_6M", 
	"B11M", 
	"B_5M", 
	"B_2M", 
	"B_1M"
};


s8 phy_rate_index_to_descr(s8 PhyRateIndex)
{
	return RateIndexMapFromPhyToDescr[PhyRateIndex];
}


s8 descr_rate_index_to_phy(s8 DescrRateIndex)
{
	return RateIndexMapFromDescrToPhy[DescrRateIndex];
}


/* get rate group data index from lmac desc rateid*/
s8 wsm_rateid_to_rate_group(s8 rateid, RATE_MODE_TYPE * rate_mode)
{
	s8				rate_index = 0;

	if (rateid == DESCR_RATE_INDEX_HT_MCS32) /*set MCS0*/
		return 0;

	if (rateid <= DESCR_RATE_INDEX_HE_MSC0) /*HE mode*/
	{
		rate_index			= DESCR_RATE_INDEX_HE_MSC0 - rateid;
		*rate_mode			= RATE_HE;
	}
	else if (rateid <= DESCR_RATE_INDEX_HE_MSCER0) /*ER mode*/
	{
		rate_index			= DESCR_RATE_INDEX_HE_MSCER0 - rateid;
		*rate_mode			= RATE_HE_ER;
	}
	else if (rateid <= DESCR_RATE_INDEX_HT_MCS0) /*HT mode*/
	{
		rate_index			= DESCR_RATE_INDEX_HT_MCS0 - rateid;
		*rate_mode			= RATE_HT;
	}
	else if (rateid <= DESCR_RATE_INDEX_G_6M) /*11G mode*/
	{
		rate_index			= DESCR_RATE_INDEX_G_6M - rateid;
		*rate_mode			= RATE_G;
	}
	else if (rateid <= DESCR_RATE_INDEX_B_1M) /*11B mode*/
	{
		rate_index			= DESCR_RATE_INDEX_B_1M - rateid;
		*rate_mode			= RATE_B;
	}
	else 
	{
		rate_index			= 0;
	}

	return rate_index;
}


/*
convert rate index to 4bit index as group index.
*/
s8 rate_index_to_wsm_rate(s8 rate_index, RATE_MODE_TYPE rate_mode)
{
	s8				rateid = 0;

	switch (rate_mode)
	{
		case RATE_B:
			rateid = rate_index - RATE_INDEX_B_1M;
			break;

		case RATE_G:
			rateid = rate_index - RATE_INDEX_A_6M;
			break;

		case RATE_HT:
			rateid = rate_index - RATE_INDEX_N_6_5M;
			break;

		case RATE_HE:
			rateid = rate_index - RATE_INDEX_HE_MCS0;
			break;

		case RATE_HE_ER:
			rateid = rate_index - RATE_INDEX_HE_MCSER0;
			break;

		default:
			atbm_printk_rc("Warning: unknown rate mode! rate_index:%d rate_mode:%d\n", rate_index, rate_mode);
			break;
	}

	return rateid;
}


s8 rate_index_to_wsm_rate_mode(s8 rate_index, RATE_MODE_TYPE * rate_mode)
{
	s8				rateid = 0;

	*rate_mode			= RATE_UNKNOWN;

	if (rate_index >= RATE_INDEX_HE_MCS0)
	{
		*rate_mode			= RATE_HE;
	}
	else if (rate_index >= RATE_INDEX_HE_MCSER0)
	{
		*rate_mode			= RATE_HE_ER;
	}
	else if (rate_index >= RATE_INDEX_N_6_5M)
	{
		*rate_mode			= RATE_HT;
	}
	else if (rate_index >= RATE_INDEX_A_6M)
	{
		*rate_mode			= RATE_G;
	}
	else 
	{
		*rate_mode			= RATE_B;
	}

	switch (*rate_mode)
	{
		case RATE_B:
			rateid = rate_index - RATE_INDEX_B_1M;
			break;

		case RATE_G:
			rateid = rate_index - RATE_INDEX_A_6M;
			break;

		case RATE_HT:
			rateid = rate_index - RATE_INDEX_N_6_5M;
			break;

		case RATE_HE:
			rateid = rate_index - RATE_INDEX_HE_MCS0;
			break;

		case RATE_HE_ER:
			rateid = rate_index - RATE_INDEX_HE_MCSER0;
			break;

		default:
			atbm_printk_rc("Warning: unknown rate mode! rate_index:%d rate_mode:%d\n", rate_index, *rate_mode);
			break;
	}

	return rateid;
}


/*get ideal rate throughput for rateid from LMAC*/
u16 get_ideal_rate(u8 wsm_rateid, u8 short_gi_supported)
{
	RATE_MODE_TYPE	rate_mode;
	u16 			ideal_rate = 0;
	u8				rate_index;

	rate_index			= wsm_rateid_to_rate_group(wsm_rateid, &rate_mode);

	/*get current rate's ideal throughput*/
	switch (rate_mode)
	{
		case RATE_B:
			ideal_rate = b_rate_group[rate_index];
			break;

		case RATE_G:
			ideal_rate = g_rate_group[rate_index];
			break;

		case RATE_HT:
			if ((rate_index == 7) && (short_gi_supported))
			{
				ideal_rate			= n_rate_groups[rate_index];
			}
			else 
			{
				ideal_rate			= n_rate_group[rate_index];
			}

			break;

		case RATE_HE:
			if (rate_index >= (HE_SHORT_GI_START_RATE - RATE_INDEX_HE_MCS0))
			{
				ideal_rate			= ax_rate_group8[rate_index];

				if (rate_index == 10)
				{
					ideal_rate			= ideal_rate + 3;
				}

				if (rate_index == 11)
				{
					ideal_rate			= ideal_rate + 32;
				}
			}
			else 
			{
				if(he_set_mcs_2x_p16){
					ideal_rate	= ax_rate_group16[rate_index];
				}
				else{
					ideal_rate	= ax_rate_group32[rate_index];
				}
			}

			break;

		case RATE_HE_ER:
			if(he_set_mcs_2x_p16){
					ideal_rate = ax_rate_er242_16[rate_index];
				}
				else{
					ideal_rate = ax_rate_er242_32[rate_index];
				}			
			break;
		default:
			atbm_printk_rc("Warning: unknown rate mode! wsm_rateid:%d rate_index:%d\n", rate_index, wsm_rateid);
			break;
	}

	return ideal_rate;
}


/************************************************************************/

/*period update statics result, find max throughput rate etc.*/

/************************************************************************/
void hmac_tx_statu(void * priv, struct ieee80211_sta * sta, void * priv_sta)
{

	int 			i;
	int 			rate_start_idx = 0;
	int 			rate_end_idx = RATE_DESC_INDEX_MAX - 1;
	u16 			main_snd_idle_ratio = 0;
	u16 			snd_idle_ratio = 0;
	static u16 			main_idle_ratio = 0;

	struct txrate_status * status = (struct txrate_status *) sta->txs_retrys;
	RATE_MODE_TYPE	rate_mode;
	u8				bw40_band_select = 0;

	u16 			ideal_rate_tp;

	//	RATE_MODE_TYPE rate_mode;
	//	uint16_t sucess_prob;
	u32 			current_tp;
	u32 			max_tp = 0;
	u8				max_tp_idx = 0;
	u8				cur_prob = 0;

	u32 			max_tp_history = 0;
	u8				max_tp_idx_history = 0;


	u32 			max_success_prob = 0;
	u8				max_success_idx = 0;
	u8				signal_loss_flag = 1;
	u8				short_gi_supported = 0;
	u32 sum_tp = 0;
	u32 sum_tx_cnt = 0;
	/*find the link of sta mode or one of the AP's station*/
	struct RATE_CONTROL_RATE_S * rates_info = priv_sta;
	rates_info->n_signal_loss_flag = 0;
	rates_info->last_period_pkt_cnt = 0;
	rates_info->last_max_tp_rate_idx = rates_info->max_tp_rate_idx;

	// rates_info->last_update_time = jiffies;
	//atbm_printk_rc("\nhmac_tx_status..\n");
	// dump_mem((unsigned char *)txstatus, 32);
	for (i = 0; i <= rate_end_idx; i++)
	{
		if (status->txcnt[i] > 0)
		{
			atbm_printk_rc("rateid:%d %s failed:%4d  cnt:%4d ratio:%2d\n", i, strRateIndexDesc[i], status->txfail[i],
				 status->txcnt[i], (status->txcnt[i] -status->txfail[i]) * 100 / status->txcnt[i]);
		}
	}


	if (rates_info->tx_rc_flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
	{
		if (rates_info->tx_rc_flags & IEEE80211_TX_RC_40M_TO_20M)
		{
			if (rates_info->tx_rc_flags & IEEE80211_TX_RC_SHORT_GI_20M)
			{
				short_gi_supported	= 1;
			}
		}
		else 
		{
			if (rates_info->tx_rc_flags & IEEE80211_TX_RC_SHORT_GI_40M)
			{
				short_gi_supported	= 1;
			}
		}
	}
	else 
	{
		if (rates_info->tx_rc_flags & IEEE80211_TX_RC_SHORT_GI_20M)
		{
			short_gi_supported	= 1;
		}
	}


	//	printk(" rate_end_idx:%d loss_flag:%d\n", rate_end_idx,cur_prob,rates_info->n_signal_loss_flag);
	for (i = rate_start_idx; i <= rate_end_idx; i++)
	{
		cur_prob			= 0;

		if (status->txcnt[i] > 0) /* have txed data.*/
		{
			ideal_rate_tp		= get_ideal_rate(i, short_gi_supported);

			//rates_info->sucess_prob_miss_flag[i] = 0;
		}
		else /*no txed data in this period */
		{
			/*how to update history data, if no data send on this rate id*/
			/*not used rate but have success history, should degrade priority by reduce success probability, for 
				example retain 90% after each period*/
			if (rates_info->sucess_prob[i] > 0)
			{
				ideal_rate_tp		= get_ideal_rate(i, short_gi_supported);
				rates_info->sucess_prob[i] = (rates_info->sucess_prob[i] * (100 - UNUSED_RATE_REDUCE_ALPHA)) / 100; // no need to reduce.
				cur_prob			= rates_info->sucess_prob[i];
				rates_info->sucess_prob_miss_flag[i] += 1; //rate not continue send. history weight will be less

				if (rates_info->sucess_prob_miss_flag[i] > 200)
				{
					rates_info->sucess_prob_miss_flag[i] = 200; //set not changed
				}
			}
			else /*not used rate.*/
			{
				continue;
			}
		}

		/*update success ratio*/
		rates_info->transmit_cnt[i] = status->txcnt[i];
		rates_info->success_cnt[i] = status->txcnt[i] -status->txfail[i];
		rates_info->last_period_pkt_cnt = rates_info->last_period_pkt_cnt + rates_info->success_cnt[i];

		if ((status->txcnt[i] >= status->txfail[i]) && (status->txcnt[i] > 0))
		{
			cur_prob			= ((status->txcnt[i] -status->txfail[i]) * 255) / status->txcnt[i];

			if (cur_prob > SAMPLE_RATE_BAD_SUCC_PROB_THRESH)
			{
				// atbm_printk_rc("set 0\n");
				signal_loss_flag	= 0;
			}

			// atbm_printk_rc("cur_prob:%d loss_flag:%d\n", cur_prob,rates_info->n_signal_loss_flag);
			rates_info->totle_transmit_cnt[i] =
				 (rates_info->totle_transmit_cnt[i] *4) / 5 + rates_info->transmit_cnt[i];
			rates_info->totle_success_cnt[i] = (rates_info->totle_success_cnt[i] *4) / 5 + rates_info->success_cnt[i];
			rates_info->totle_sucess_prob[i] =
				 (rates_info->totle_success_cnt[i] *255) / rates_info->totle_transmit_cnt[i];
		}
		else 
		{
			/*if txcnt == txfail, use default 0, if txcnt ==0, use history value, not required to set 0 here.*/
		}

		/*
		the more data txed the more weight,  show the TX condition more accurate: good or bad
		the more success prob, the more weight.
		*/
		if (rates_info->sucess_prob[i] == 0) /*history have less than 1/8 packets than current*/
		{
			// history = (current*256);
			rates_info->sucess_prob[i] = cur_prob;
		}
		else if (rates_info->transmit_cnt[i] > MIN_TX_THR_PER_PERIOD * 5) /*for example 200ms, 10*5 packets Txed*/
		{
			// history=1/4*history + 3/4*(current*256);
			rates_info->sucess_prob[i] = ((rates_info->sucess_prob[i] +cur_prob * 3) >> 2);
		}
		else if (rates_info->transmit_cnt[i] > MIN_TX_THR_PER_PERIOD * 2)
		{
			// history=1/2*history + 1/2*(current*256);
			rates_info->sucess_prob[i] = ((rates_info->sucess_prob[i] *2 + cur_prob * 2) >> 2);
		}
		else if (rates_info->transmit_cnt[i] > MIN_TX_THR_PER_PERIOD)
		{ // 1/2*history + 1/2*current*256;
			rates_info->sucess_prob[i] = ((rates_info->sucess_prob[i] *2 + cur_prob * 2) >> 2);
		}
		else 
		{
			// 1/2*history + 1/2*current*256;
			//rates_info->sucess_prob[i] = ((rates_info->sucess_prob[i] * 3 + cur_prob) >> 2);
			rates_info->sucess_prob[i] = ((rates_info->sucess_prob[i] *2 + cur_prob * 2) >> 2);
		}

		/*specially add 50% current weight for sample rate, as sample rate have less packets [5/8] */
		if ((rates_info->sample_tp_rate_idx == descr_rate_index_to_phy(i)) && (rates_info->sample_packets_cnt > 0))
		{
			rates_info->sucess_prob[i] = ((rates_info->sucess_prob[i] *2 + cur_prob * 2) >> 2);
		}

		/*specially add 50% weight for new rate result*/
		else if ((rates_info->sucess_prob_miss_flag[i] > 5) && (rates_info->sucess_prob[i] != 0))
		{
			rates_info->sucess_prob[i] = ((rates_info->sucess_prob[i] *2 + cur_prob * 2) >> 2);
			rates_info->sucess_prob_miss_flag[i] = 0;
		}
		else //not add
		{

		}

		if (status->txcnt[i] > 0) /* have txed data.*/
		{
			rates_info->sucess_prob_miss_flag[i] = 0; //clear flag			   
		}

		if ((status->txcnt[i] > 10) && (status->txfail[i] == status->txcnt[i])) /* have txed data. all faile*/
		{
			rates_info->sucess_prob[i] = ((rates_info->sucess_prob[i] *2 + cur_prob * 2) >> 2);
		}

		/*calibrate based on smoothly history value*/
#if 1

		if (rates_info->transmit_cnt[i] > 0)
		{
			if (rates_info->totle_sucess_prob[i] > (255 * 60) / 100) //>60%
			{
				if (rates_info->transmit_cnt[i] *4 > rates_info->totle_transmit_cnt[i])
				{
					//7/8 + 1/8
					rates_info->sucess_prob[i] =
						 ((rates_info->sucess_prob[i] *7 + rates_info->totle_sucess_prob[i] *1) >> 3);

				}
				else if (rates_info->transmit_cnt[i] *8 > rates_info->totle_transmit_cnt[i])
				{
					//6/8 + 2/8
					rates_info->sucess_prob[i] =
						 ((rates_info->sucess_prob[i] *6 + rates_info->totle_sucess_prob[i] *2) >> 3);

				}
				else //5/8 + 3/8
				{
					rates_info->sucess_prob[i] =
						 ((rates_info->sucess_prob[i] *5 + rates_info->totle_sucess_prob[i] *3) >> 3);
				}
			}
			else 
			{
				if (rates_info->transmit_cnt[i] *4 > rates_info->totle_transmit_cnt[i])
				{
					//6/8 + 2/8
					rates_info->sucess_prob[i] =
						 ((rates_info->sucess_prob[i] *6 + rates_info->totle_sucess_prob[i] *2) >> 3);

				}
				else if (rates_info->transmit_cnt[i] *8 > rates_info->totle_transmit_cnt[i])
				{
					//5/8 + 3/8
					rates_info->sucess_prob[i] =
						 ((rates_info->sucess_prob[i] *5 + rates_info->totle_sucess_prob[i] *3) >> 3);

				}
				else 
				{
					//0.5 + 0.5
					rates_info->sucess_prob[i] =
						 ((rates_info->sucess_prob[i] *2 + rates_info->totle_sucess_prob[i] *2) >> 2);
				}

			}


		}
		else 
		{
			//rates_info->sucess_probs[i] = 0;
		}

#endif


		/*force 20M if 40M support.*/
		//if((rates_info->tx_rc_flags & IEEE80211_TX_RC_40_MHZ_WIDTH) && (( i == DESCR_RATE_INDEX_HT_MCS0)||(i == DESCR_RATE_INDEX_HT_MCS1))&&(rates_info->max_tp_rate_idx == PAS_DescrRateIndexToPhy(i)))
		if ((rates_info->tx_rc_flags & IEEE80211_TX_RC_40_MHZ_WIDTH) && (i <= DESCR_RATE_INDEX_HT_MCS0))
		{
			// ideal_rate_tp = ideal_rate_tp * 2; 
			if (rates_info->tx_rc_flags & IEEE80211_TX_RC_40M_TO_20M)
			{
				ideal_rate_tp		= (ideal_rate_tp * 3) / 4; //half bandwidth compare to 40M, but may reduce avoid time, not 1/2.
				atbm_printk_rc("id:%02d %s pb:%03d tolpb:%03d  realtp:%d 40M_to_20M\n", i, strRateIndexDesc[i],
					 rates_info->sucess_prob[i], rates_info->totle_sucess_prob[i],
					 rates_info->sucess_prob[i] *ideal_rate_tp);
			}
		}

		if ((rates_info->tx_rc_flags & IEEE80211_TX_RC_40_MHZ_WIDTH) && (i >= DESCR_RATE_INDEX_G_54M)) //mixed 40M and 20m bw rates
		{
			ideal_rate_tp		= (ideal_rate_tp) / 2;
		}

		//ER0 will force set DCM if DCM support. if DCM not support, only ER0
		if (i == DESCR_RATE_INDEX_HE_MSCER0)
		{
			if (rates_info->tx_rc_flags & IEEE80211_TX_RC_HE_SUPPORT_DCM)
			{
				ideal_rate_tp		= ideal_rate_tp / 2; //DCM half rate. have set by array data value
			}

			atbm_printk_rc("id:%02d %s pb:%d tolpb:%d  realtp:%d 40M_to_20M\n", i, strRateIndexDesc[i],
				 rates_info->sucess_prob[i], rates_info->totle_sucess_prob[i],
				 rates_info->sucess_prob[i] *ideal_rate_tp);
		}

		/* tp = (sucess_prob  + sucess_prob^2 * alpha/100) * ideal_rate,  sucess_prob have more weight than real value
			 */
		current_tp			=
			 (u32) ((rates_info->sucess_prob[i] * (100 + (rates_info->sucess_prob[i] *SUCCESS_PROB_WEIGHT_ALPHA) / 100) *ideal_rate_tp) / 100);
		rates_info->rate_tp[i] = current_tp;

		/*find the max success rate index and max throughput rate index*/
		if (rates_info->sucess_prob[i] >= max_success_prob)
		{
			max_success_prob	= rates_info->sucess_prob[i];
			max_success_idx 	= i;
		}

		if ((current_tp >= max_tp) && ((status->txcnt[i] > 0) || (i > DESCR_RATE_INDEX_B_11M))) //not purely based on history data.
		{
			if ((sta->he_cap.has_he) && (rates_info->max_he_index)) //filter out 11g and 11n
			{
				if ((i <= DESCR_RATE_INDEX_HE_MSCER0) || (i > DESCR_RATE_INDEX_B_11M))
				{
					if (status->txcnt[i] > 0)
					{
						max_tp				= current_tp;
						max_tp_idx			= i;
					}
				}
			}
			else if (sta->ht_cap.ht_supported) //filter out 11g 
			{
				if (((i <= DESCR_RATE_INDEX_HT_MCS0) && (i >= DESCR_RATE_INDEX_HT_MCS7)) ||
					 (i > DESCR_RATE_INDEX_B_11M))
				{
					if (status->txcnt[i] > 0)
					{
						max_tp				= current_tp;
						max_tp_idx			= i;
					}
				}
			}
			else 
			{
				if (status->txcnt[i] > 0)
				{
					max_tp				= current_tp;
					max_tp_idx			= i;
				}
			}
		}

		//find max tp in short history. may be equal to mtp cur txed.
		if ((current_tp >= max_tp_history) && (rates_info->sucess_prob_miss_flag[i] <= 6)) //not purely based on history data.
		{
			if ((sta->he_cap.has_he) && (rates_info->max_he_index)) //filter out 11g and 11n
			{
				if ((i <= DESCR_RATE_INDEX_HE_MSCER0) || (i > DESCR_RATE_INDEX_B_11M))
				{
					max_tp_history		= current_tp;
					max_tp_idx_history	= i;
				}
			}
			else if (sta->ht_cap.ht_supported) //filter out 11g 
			{
				if (((i <= DESCR_RATE_INDEX_HT_MCS0) && (i >= DESCR_RATE_INDEX_HT_MCS7)) ||
					 (i > DESCR_RATE_INDEX_B_11M))
				{
					max_tp_history		= current_tp;
					max_tp_idx_history	= i;
				}
			}
			else 
			{
				max_tp_history		= current_tp;
				max_tp_idx_history	= i;
			}
		}



#if RATE_CONTROL_STATIC_DEBUG
		rates_info->total_throughput = rates_info->total_throughput + rates_info->success_cnt[i] *ideal_rate_tp;
#endif
		if((status->txcnt[i] > 0)||(max_tp_idx == i))
		{
		     //to avoid overflow here / 10 
			 sum_tp = sum_tp + ((rates_info->sucess_prob[i] * ideal_rate_tp) / 10) * rates_info->totle_transmit_cnt[i];
			 sum_tx_cnt = sum_tx_cnt + rates_info->totle_transmit_cnt[i];
        }
		atbm_printk_rc("id:%02d %s pb:%03d tolpb:%03d  curtp:%d realtp:%d\n", i, strRateIndexDesc[i],
			 rates_info->sucess_prob[i], rates_info->totle_sucess_prob[i], current_tp,
			 rates_info->sucess_prob[i] *ideal_rate_tp);
	}

	rates_info->n_signal_loss_flag = signal_loss_flag;


	/*record the max TP and max success prob rate index, will be used directly in set rate functions*/
	if (max_tp != 0)
	{
		rates_info->max_tp_rate_idx = descr_rate_index_to_phy(max_tp_idx);
	}


	if (max_tp_history != 0) //all rates failed
	{
		rates_info->max_tp_rate_idx_history = descr_rate_index_to_phy(max_tp_idx_history);
	}


	rates_info->max_success_rate_idx = descr_rate_index_to_phy(max_success_idx);

	//atbm_printk_rc("max_success_rate_idx:%d max_tp_rate_idx:%d PHY max_tp:%d\n", rates_info->max_success_rate_idx, rates_info->max_tp_rate_idx, max_tp);
	rates_info->sample_packets_cnt = 0; 			/*clear sample packet number*/
	rates_info->sample_tp_rate_idx = 0;

	if (rates_info->max_success_rate_idx > rates_info->max_tp_rate_idx)
	{
		atbm_printk_rc("!!warning!! status: max_success_rate_idx:%d > max_tp_rate_idx:%d\n",
			 rates_info->max_success_rate_idx, rates_info->max_tp_rate_idx);
		rates_info->max_success_rate_idx = rates_info->max_tp_rate_idx; /*may be history data related. new sample 
			period have great changes of different rates*/
	}

	/*debug info*/
	for (i = rate_start_idx; i <= rate_end_idx; i++)
	{
		// atbm_printk_rc("rateid:%d sucess_prob:%d current_tp:%d\n", i, rates_info->sucess_prob[i], rates_info->sucess_prob[i]*ideal_rate);
	}

	//	printk("loss flag:%d \n",	rates_info->n_signal_loss_flag);
	rates_info->not_sample_cnt++;

	//upgrade to supported mode sample, 11b or 11g -> 11n ax
	// if((rates_info->not_sample_cnt > 10)&&(rates_info->sucess_prob[max_tp_idx] > 100)) //11g->11n,11ax
	if ((rates_info->not_sample_cnt > 4)) //11g->11n,11ax
	{
		wsm_rateid_to_rate_group(max_tp_idx, &rate_mode);

		// atbm_printk_rc("max_tp_idx:%d rate_mdoe:%d \n",	max_tp_idx,rate_mode);
		if (sta->he_cap.has_he) /*if HE mode support, rate not only HE mode start with HE mode*/
		{
			if (rate_mode != RATE_HE)
			{
				rates_info->resample_required = 1;
			}
			else 
			{
			}

		}
		else if (sta->ht_cap.ht_supported) /*if HT mode set, highest rate not on HT mode, try start with HT mode*/
		{
			if ((rate_mode != RATE_HT) && (rate_mode != RATE_B))
			{
				rates_info->resample_required = 1;
			}
			else 
			{
				if (rates_info->n_signal_loss_flag) //add start sample
				{

					rates_info->resample_required = 1;
				}

			}

		}
		else 
		{
		}

		//11g only will not have other mode..
#if 0

		if (rates_info->last_max_tp_rate_idx != rates_info->max_tp_rate_idx)
		{
			rates_info->fast_sample_flag = 1;
		}
		else 
		{
			rates_info->fast_sample_flag = 0;
		}

		rates_info->last_max_tp_rate_idx = rates_info->max_tp_rate_idx;
#endif


		rates_info->not_sample_cnt = 0;
	}

	main_snd_idle_ratio = (u16) (sta->txs_retrys[RATE_DESC_INDEX_MAX * 2]);
	snd_idle_ratio		= (u16) (sta->txs_retrys[RATE_DESC_INDEX_MAX * 2 + 1]);
	main_idle_ratio 	= (u16) (sta->txs_retrys[RATE_DESC_INDEX_MAX * 2 + 2]);

	rates_info->main_snd_idle_ratio = main_snd_idle_ratio;
	rates_info->snd_idle_ratio = snd_idle_ratio;
	rates_info->main_idle_ratio = main_idle_ratio;

	//atbm_printk_rc("##### Rx mainidl:%d sndidl:%d main_idle\n", main_snd_idle_ratio, snd_idle_ratio, main_idle_ratio);
	g_ch_idle_ratio 	= rates_info->main_idle_ratio; //current no main_snd_idle-ratio.

	//update, history data as one sample datas
	if (rates_info->rssi_count)
		rates_info->mean_rssi = rates_info->total_rssi / rates_info->rssi_count;

	rates_info->rssi_count = 1;
	rates_info->total_rssi = rates_info->mean_rssi;
	rates_info->mean_rssi = rates_info->mean_rssi;
	rates_info->max_rssi = -128;
	rates_info->min_rssi = 127;

	if (rates_info->max_tp_rate_idx >= RATE_INDEX_N_6_5M) //HT or HE mode
	{

		if ((rates_info->tx_rc_flags & IEEE80211_TX_RC_40_MHZ_WIDTH) &&
			 (rates_info->tx_rc_flags & IEEE80211_TX_RC_20_MHZ_WIDTH)) //support 40M&20M
		{

			//bw_select_11n(rates_info->mean_rssi,	unsigned char ch_idle_ratio,  unsigned char m_tp_rate,	unsigned char mian_snd_ratio)		
			rates_info->bw_total_pkt_cnt += rates_info->last_period_pkt_cnt;

			if (time_after(jiffies, rates_info->bw_total_pkt_update_time + (2000 * HZ) / 1000))
			{
				if (rates_info->snd_idle_ratio > 0)
				{ //have valid idle ratio

					rates_info->bw_total_pkt_cnt =
						 (rates_info->bw_total_pkt_cnt * HZ) / (jiffies - rates_info->bw_total_pkt_update_time);
					bw40_band_select	= bw_select_40m((signed char) rates_info->mean_rssi,
						 (u8) rates_info->snd_idle_ratio, rates_info->main_snd_idle_ratio, 
						rates_info->max_tp_rate_idx, rates_info->sucess_prob[max_tp_idx],
						 ! ! (rates_info->tx_rc_flags & IEEE80211_TX_RC_40M_TO_20M), rates_info->bw_total_pkt_cnt);

				}
				else  //no idle info
				{
				rates_info->bw_total_pkt_cnt =
						 (rates_info->bw_total_pkt_cnt * HZ) / (jiffies - rates_info->bw_total_pkt_update_time);
					bw40_band_select	= bw_select_40m_simple((signed char) rates_info->mean_rssi,						 
						rates_info->max_tp_rate_idx, rates_info->sucess_prob[max_tp_idx],
						 ! ! (rates_info->tx_rc_flags & IEEE80211_TX_RC_40M_TO_20M), rates_info->bw_total_pkt_cnt);
				}

				
					if (bw40_band_select == 0)
					{
						rates_info->tx_rc_flags |= IEEE80211_TX_RC_40M_TO_20M;
						atbm_printk_rc(" bw_select_11n sig set 40M_to_20M flag bw_total_pkt_cnt:%d\n",
							 rates_info->bw_total_pkt_cnt);
					}
					else 
					{
						if (rates_info->tx_rc_flags & IEEE80211_TX_RC_40M_TO_20M) //switch form 20M to 40M, max tp rate -1
						{
							u8				wsm_rateid;

							wsm_rateid			= rate_index_to_wsm_rate_mode(rates_info->max_tp_rate_idx,
								 &rate_mode);

							if (wsm_rateid > 1)
							{
								rates_info->max_tp_rate_idx = rates_info->max_tp_rate_idx - 2;

								//printk(KERN_ALERT "back40M rates_info->max_tp_rate_idx:%d\n", rates_info->max_tp_rate_idx);
							}
							//printk(KERN_ALERT "back40m  rates_info->max_tp_rate_idx:%d\n", rates_info->max_tp_rate_idx);
						}

						// printk(KERN_ALERT "bw_select_11n 40M  rates_info->max_tp_rate_idx:%d\n", rates_info->max_tp_rate_idx);
						rates_info->tx_rc_flags &= ~IEEE80211_TX_RC_40M_TO_20M;
					}

					rates_info->bw_total_pkt_update_time = jiffies;
					rates_info->bw_total_pkt_cnt = 0;
			}

		}
	}
	
	if(sum_tx_cnt > 0)
	{
	  if(main_idle_ratio == 0){
		   rates_info->average_throughput = ((sum_tp/sum_tx_cnt)*30)/4;
	  }
	  else
	  {
		   rates_info->average_throughput = ((sum_tp/sum_tx_cnt)*(100 + main_idle_ratio/10))/20;
	  } 
	}
	else
	{

		rates_info->average_throughput = 0;
	}
//	rates_info->average_throughput /= 512;
//	atbm_printk_init("mtp:%s sum_tp:%d sum_tx_cnt:%d  main_idle_ratio:%d avermean_rssiage_throughput:%d Mbps\n", 
//	strRateIndexPhy[rates_info->max_tp_rate_idx],sum_tp, sum_tx_cnt, main_idle_ratio, rates_info->average_throughput);

	
	atbm_printk_rc("mtp:%s %d  mean_rssi:%d  rc_flags:%x\n\n", strRateIndexPhy[rates_info->max_tp_rate_idx], max_tp,
		 rates_info->mean_rssi, rates_info->tx_rc_flags);
}


void get_rate_max_min(RATE_MODE_TYPE rate_mode, struct RATE_CONTROL_RATE_S * rates_info, u8 * max_phy_rate,
	 u8 * min_phy_rate)
{

	// RATE_MODE_TYPE rate_mode;
	// wsm_rateid_to_rate_group(rateid, &rate_mode);

	/*get max rate*/
	switch (rate_mode)
	{
		case RATE_B:
			if (rates_info != NULL)
			{
				if (rates_info->max_b_index >= 2)
				{
					*max_phy_rate		= RATE_INDEX_B_5_5M;
				}
				else 
				{
					*max_phy_rate		= rates_info->max_b_index + RATE_INDEX_B_1M;
				}

				if ((rates_info->max_g_index == 0) && (rates_info->max_ht_index == 0)) //11b only
				{
					*max_phy_rate		= rates_info->max_b_index + RATE_INDEX_B_1M;
				}
			}
			else 
			{
				*max_phy_rate		= RATE_INDEX_B_5_5M;
			}

			*min_phy_rate = RATE_INDEX_B_1M;
			break;

		case RATE_G:
			if (rates_info != NULL)
			{
				if (rates_info->max_g_index >= 7)
				{
					*max_phy_rate		= RATE_INDEX_A_54M;
				}
				else 
				{
					*max_phy_rate		= RATE_INDEX_A_6M + rates_info->max_g_index;
				}

				*min_phy_rate		= RATE_INDEX_A_6M;
			}
			else 
			{
				*max_phy_rate		= RATE_INDEX_A_54M;
				*min_phy_rate		= RATE_INDEX_A_6M;
			}

			break;

		case RATE_HT:
			if (rates_info != NULL)
			{
				if (rates_info->max_ht_index >= 7)
				{
					*max_phy_rate		= RATE_INDEX_N_65M;
				}
				else 
				{
					*max_phy_rate		= RATE_INDEX_N_6_5M + rates_info->max_ht_index;
				}

				*min_phy_rate		= RATE_INDEX_N_6_5M;
			}
			else 
			{
				*max_phy_rate		= RATE_INDEX_N_65M;
				*min_phy_rate		= RATE_INDEX_N_6_5M;
			}

			break;

		case RATE_HE:
			if (rates_info != NULL)
			{
				if (rates_info->max_he_index > 10)
				{
					*max_phy_rate		= RATE_INDEX_HE_MCS11; //
				}
				else 
				{
					*max_phy_rate		= RATE_INDEX_HE_MCS0 + rates_info->max_he_index;
				}
			}
			else 
			{
				*max_phy_rate		= RATE_INDEX_HE_MCS9;
			}

			*min_phy_rate = RATE_INDEX_HE_MCS0;
			break;

		case RATE_HE_ER:
			*max_phy_rate = RATE_INDEX_HE_MCSER0;
			*min_phy_rate = RATE_INDEX_HE_MCSER0;
			break;

		default:
			atbm_printk_rc("Warning: unkown rate mdoe!\n");
			break;
	}
}



/*
#define RATE_INDEX_N_6_5M				14
#define RATE_INDEX_N_13M				15
#define RATE_INDEX_N_19_5M				16
#define RATE_INDEX_N_26M				17
#define RATE_INDEX_N_39M				18
#define RATE_INDEX_N_52M				19
#define RATE_INDEX_N_58_5M				20
#define RATE_INDEX_N_65M				21
*/

/* first sample circle 200ms packet sequence.
65M->6.5M
52M->6.5M
26M->6.5M
*/
static void get_rate_start_11n(struct ieee80211_sta * sta, void * priv_sta, 
	struct ieee80211_tx_rate_control * txrc)
{
	// static int pkt_num = 0;
	RATE_SET_T *	prate_set;
	u8				iter_idx = 0;

	struct RATE_CONTROL_RATE_S * rates_info = priv_sta;
	prate_set			= rates_info->rate_set;

	prate_set->rate_mode = RATE_HT;
	iter_idx			= rates_info->packets_cnt % 3; /*0, 1,2*/
	prate_set->max_tp_rate_index = RATE_INDEX_N_65M - 2 * iter_idx;
	prate_set->max_tp_rate_try = 2;


	if (prate_set->max_tp_rate_index > rates_info->max_ht_index + RATE_INDEX_N_6_5M)
	{
		prate_set->max_tp_rate_index = rates_info->max_ht_index + RATE_INDEX_N_6_5M;
	}

	if (rates_info->max_tp_rate_idx != 0xff)
	{
		if (rates_info->max_tp_rate_idx <= RATE_INDEX_B_11M) //from 11b
		{
			prate_set->max_tp_rate_index = RATE_INDEX_N_13M;
		}
		else 
		{
			prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx; //not down		   
		}

#if BANDWIDTH_MORE_20M

		if ((rates_info->tx_rc_flags & IEEE80211_TX_RC_40_MHZ_WIDTH) &&
			 (rates_info->tx_rc_flags & IEEE80211_TX_RC_20_MHZ_WIDTH))
		{
			rates_info->tx_rc_flags |= IEEE80211_TX_RC_40M_TO_20M;
			txrc->info->control.tx_rc_flag |= IEEE80211_TX_RC_40M_TO_20M;
		}

#endif
	}

	prate_set->lowest_rate_index = RATE_INDEX_N_6_5M;
	prate_set->lowest_rate_try = 2;

	prate_set->rate_id_num_limit = 3;
	prate_set->rate_mode = RATE_HT;

	atbm_printk_rc("get_rate_start_11n>> max_tp:%d > down:%d lowest:%d prate_set:0x%p\n",
		 prate_set->max_tp_rate_index, prate_set->down_rate_index, prate_set->lowest_rate_index, prate_set);
	rates_info->packets_cnt++;
}



/*
#define DESCR_RATE_INDEX_HE_MSC11		0
#define DESCR_RATE_INDEX_HE_MSC10		1
#define DESCR_RATE_INDEX_HE_MSC9		2
#define DESCR_RATE_INDEX_HE_MSC8		3
#define DESCR_RATE_INDEX_HE_MSC7		4
#define DESCR_RATE_INDEX_HE_MSC6		5
#define DESCR_RATE_INDEX_HE_MSC5		6
#define DESCR_RATE_INDEX_HE_MSC4		7
#define DESCR_RATE_INDEX_HE_MSC3		8
#define DESCR_RATE_INDEX_HE_MSC2		9
#define DESCR_RATE_INDEX_HE_MSC1		10
#define DESCR_RATE_INDEX_HE_MSC0		11
#define DESCR_RATE_INDEX_HE_MSCER2		12
#define DESCR_RATE_INDEX_HE_MSCER1		13
#define DESCR_RATE_INDEX_HE_MSCER0		14
*/
// RATE_INDEX_HE_MCS8

/*MSC index:					  0   1   2   3   4   5   6    7	8	 9	 10   11 */
// uint8_t ax_rate_group8[12]  =  {17, 34, 52, 69, 103, 138, 155, 172, 206, 229, 255, 255};
// uint8_t ax_rate_group16[12]	= {16, 33, 49, 65,	98, 130, 146, 163, 195, 217, 244, 255};

/*ER no DCM MCS index: 0,  1,  2*/
// uint8_t ax_rate_er242_8[3]  = {17, 34, 52};
// uint8_t ax_rate_er242_16[3] = {16, 33, 49};

/* first sample circle 200ms packet sequence. , MCS9,10,11 upgrade by sample. LMAC down to 11b mode if all retry 
	failed.
MSC8->MSC0
MSC6->MSC0
MSC4->MSC0 
*/
void get_rate_start_11ax(struct ieee80211_sta * sta, void * priv_sta, 
	struct ieee80211_tx_rate_control * txrc)
{
	// static int pkt_num = 0;
	RATE_SET_T *	prate_set;
	u8				iter_idx = 0;

	struct RATE_CONTROL_RATE_S * rates_info = priv_sta;
	u8				max_sample_rate = RATE_INDEX_HE_MCS8;

	prate_set			= rates_info->rate_set;

	iter_idx			= rates_info->packets_cnt % 4; /*0, 1, 2, 3*/
	prate_set->rate_mode = RATE_HE;

	if (iter_idx < 3) /*HE mode*/
	{
		prate_set->max_tp_rate_index = max_sample_rate - 2 * iter_idx;
		prate_set->max_tp_rate_try = 2;
	}
	else /*for low rate mode sample*/
	{
		prate_set->max_tp_rate_index = RATE_INDEX_HE_MCS2;
		prate_set->max_tp_rate_try = 2;
	}

	if (prate_set->max_tp_rate_index > rates_info->max_he_index + RATE_INDEX_HE_MCS0) //AP may set max rate.
	{
		prate_set->max_tp_rate_index = rates_info->max_he_index + RATE_INDEX_HE_MCS0;
	}

	if (rates_info->max_tp_rate_idx != 0xff)
	{
		if (rates_info->max_tp_rate_idx <= RATE_INDEX_B_11M) //from 11b
		{
			prate_set->max_tp_rate_index = RATE_INDEX_HE_MCS1;
		}
		else 
		{
			prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx; //not down		   
		}

#if BANDWIDTH_MORE_20M

		if ((rates_info->tx_rc_flags & IEEE80211_TX_RC_40_MHZ_WIDTH) &&
			 (rates_info->tx_rc_flags & IEEE80211_TX_RC_20_MHZ_WIDTH))
		{
			rates_info->tx_rc_flags |= IEEE80211_TX_RC_40M_TO_20M;
			txrc->info->control.tx_rc_flag |= IEEE80211_TX_RC_40M_TO_20M;
		}

#endif
	}

	if (prate_set->max_tp_rate_index < RATE_INDEX_HE_MCSER0)
	{
		atbm_printk_rc("warning get_rate_start_11ax, not on 11b or ax mode\n");
		prate_set->max_tp_rate_index = RATE_INDEX_HE_MCS1;
	}

	if (prate_set->max_tp_rate_index >= RATE_INDEX_HE_MCS0)
	{
		prate_set->lowest_rate_index = RATE_INDEX_HE_MCS0;
		prate_set->rate_id_num_limit = 4;
		prate_set->rate_mode = RATE_HE;
	}
	else 
	{
		prate_set->lowest_rate_index = RATE_INDEX_HE_MCSER0;
		prate_set->rate_id_num_limit = 4;
		prate_set->rate_mode = RATE_HE_ER;
	}

	atbm_printk_rc("get_rate_start_11ax >> max_tp:%d > down:%d lowest:%d\n", prate_set->max_tp_rate_index,
		 prate_set->down_rate_index, prate_set->lowest_rate_index);
	rates_info->packets_cnt++;
}


/*
#define RATE_INDEX_A_6M 				6
#define RATE_INDEX_A_9M 				7
#define RATE_INDEX_A_12M				8
#define RATE_INDEX_A_18M				9
#define RATE_INDEX_A_24M				10
#define RATE_INDEX_A_36M				11
#define RATE_INDEX_A_48M				12
#define RATE_INDEX_A_54M				13
54M->6M
36M->6
18M->6
*/
void get_rate_start_11g(struct ieee80211_sta * sta, void * priv_sta, 
	struct ieee80211_tx_rate_control * txrc)
{
	// static int pkt_num = 0;
	RATE_SET_T *	prate_set;
	u8				iter_idx = 0;

	struct RATE_CONTROL_RATE_S * rates_info = priv_sta;
	prate_set			= rates_info->rate_set;

	prate_set->rate_mode = RATE_G;
	iter_idx			= rates_info->packets_cnt % 3; /*0, 1, 2*/
	prate_set->max_tp_rate_index = RATE_INDEX_A_54M - 2 * iter_idx;
	prate_set->max_tp_rate_try = 2;

	if (prate_set->max_tp_rate_index > rates_info->max_g_index + RATE_INDEX_A_6M)
	{
		prate_set->max_tp_rate_index = rates_info->max_g_index + RATE_INDEX_A_6M;
	}

	if (rates_info->max_tp_rate_idx != 0xff)
	{
		prate_set->max_tp_rate_index = RATE_INDEX_A_18M;
	}


	prate_set->lowest_rate_index = RATE_INDEX_A_6M;
	prate_set->lowest_rate_try = 2;
	prate_set->rate_id_num_limit = 4;
	prate_set->rate_mode = RATE_G;

	// atbm_printk_rc("\nget_rate_start_11g>> max_tp:%d > down:%d lowest:%d n", prate_set->max_tp_rate_index, prate_set->down_rate_index, prate_set->lowest_rate_index);
	rates_info->packets_cnt++;
}


void get_rate_set_11b(struct ieee80211_sta * sta, void * priv_sta, 
	struct ieee80211_tx_rate_control * txrc)
{

	RATE_SET_T *	prate_set;

	u8				max_phy_rate = RATE_INDEX_B_5_5M; // max possible rate's index.
	u8				min_phy_rate = RATE_INDEX_B_2M;

	struct RATE_CONTROL_RATE_S * rates_info = priv_sta;
	prate_set			= rates_info->rate_set;
	memset(prate_set, '\0', sizeof(RATE_SET_T));

	prate_set->rate_mode = RATE_B;
	get_rate_max_min(RATE_B, rates_info, &max_phy_rate, &min_phy_rate); // update params		
	prate_set->max_tp_rate_index = max_phy_rate;
	prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS;
	prate_set->lowest_rate_index = RATE_INDEX_B_1M;
	prate_set->lowest_rate_try = 7;

	if (prate_set->max_tp_rate_index > RATE_INDEX_B_2M)
	{
		prate_set->rate_id_num_limit = 3;
		prate_set->down_rate_index = RATE_INDEX_B_2M; /*If the first rate failed, will try this rate, should be a 
			different rate index*/
		prate_set->down_rate_try = 2;
	}
	else if (prate_set->max_tp_rate_index > RATE_INDEX_B_1M)
	{
		prate_set->rate_id_num_limit = 2;
	}
	else 
	{
		prate_set->rate_id_num_limit = 1;
	}

	rates_info->packets_cnt++;

	// atbm_printk_rc("get_rate_start_11g>> max_tp:%d > down:%d lowest:%d \n", prate_set->max_tp_rate_index, prate_set->down_rate_index, prate_set->lowest_rate_index);
}


/*internal function*/
u32 rate_info_to32bit(RATE_SET_T * prate_set)
{
	u32 			u32rate_set = 0;
	RATE_SET_T		rate_set;						/*only save 4bit rate index*/

	rate_set.max_suc_rate_index = rate_index_to_wsm_rate(prate_set->max_suc_rate_index, prate_set->rate_mode);
	rate_set.down_rate_index = rate_index_to_wsm_rate(prate_set->down_rate_index, prate_set->rate_mode);
	rate_set.lowest_rate_index = rate_index_to_wsm_rate(prate_set->lowest_rate_index, prate_set->rate_mode);

	u32rate_set 		|= ((rate_set.max_suc_rate_index & 0x0f) << 28);
	u32rate_set 		|= ((prate_set->max_suc_rate_try & 0x0f) << 24);

	u32rate_set 		|= ((rate_set.down_rate_index & 0x0f) << 20);
	u32rate_set 		|= ((prate_set->down_rate_try & 0x0f) << 16);

	u32rate_set 		|= ((rate_set.lowest_rate_index & 0x0f) << 12);
	u32rate_set 		|= ((prate_set->lowest_rate_try & 0x0f) << 8);

	u32rate_set 		|= ((prate_set->rate_id_num_limit & 0x0f) << 4);
	u32rate_set 		|= (prate_set->rate_mode & 0x0f);

	//atbm_printk_rc("%s rateset:0x%x maxSuc:%d try:%d minRate:%d try:%d\n", __func__, u32rate_set,
	//			   prate_set->max_suc_rate_index, prate_set->max_suc_rate_try, prate_set->lowest_rate_index, prate_set->lowest_rate_try);
	return u32rate_set;
}


// fixed rate, set equal rates.
int get_fixed_rate_set(u8 rateid, u8 * wsm_rateid, u32 * wsm_rateset)
{
	int 			ret = 0;
	RATE_MODE_TYPE	rate_mode;

	RATE_SET_T		rate_set =
	{
		0
	};

	*wsm_rateid 		= rate_index_to_wsm_rate_mode(rateid, &rate_mode);
	rate_set.rate_mode	= rate_mode;

	/*lowest rate will be used if rates set flag be set.*/
	rate_set.lowest_rate_index = rateid;			/*set lowest rate == highest rate. highest rate was set by tx_max_
		rate*/
	rate_set.lowest_rate_try = MAX_TRANS_NUMBER_RATE;
	rate_set.rate_id_num_limit = 1;
	*wsm_rateset		= rate_info_to32bit(&rate_set);

	//atbm_printk_rc("%s rate_index:%x rate_mode:%d wsm_rateset:0x%x\n", __func__, *wsm_rateid, rate_mode, *wsm_rateset);
	return ret;
}


void get_max_assoc_rate(struct ieee80211_sta * sta, struct RATE_CONTROL_RATE_S * rates_info)
{
	u8				max_rcs_index = 0;
	u32 			rate_mask = 0;

	if ((sta->he_cap.has_he) && ieee80211_he_mcs_rate_support(sta->he_cap.he_mcs_nss_supp.rx_mcs_80, 1))
	{
		max_rcs_index		= ieee80211_he_mcs_operation_rate(sta->he_cap.he_mcs_nss_supp.rx_mcs_80, 1);
		rates_info->max_he_index = max_rcs_index;
		atbm_printk_rc("max_rcs_indexHe:%d \n", rates_info->max_he_index);
	}

	if ((sta->ht_cap.ht_supported == true) && (sta->ht_cap.mcs.rx_mask[0]))
	{
		/*
		*only support one stream
		*/
		if (sta->ht_cap.mcs.rx_mask[0] == 0xff)
		{
			rates_info->max_ht_index = 7;
		}
		else 
		{
			rate_mask			= sta->ht_cap.mcs.rx_mask[0];
			rates_info->max_ht_index = fls(rate_mask) - 1;
		}

		//if(sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20){
		//	t->tx_info->control.rates[0].flags |= IEEE80211_TX_RC_SHORT_GI;
		//}
		atbm_printk_rc("max_rcs_indexHt:%d \n", rates_info->max_ht_index);
	}

	if (sta->supp_rates[0])
	{
	         sta->supp_rates[0]=0x08;
         	rate_mask			= sta->supp_rates[0];
		rates_info->max_g_index = fls(rate_mask) - 1;

		if (rates_info->max_g_index >= 4) //has 11g rates
		{
			rates_info->max_g_index = rates_info->max_g_index - 4;
			rates_info->max_b_index = fls(rate_mask & 0x0f);

			if (rates_info->max_b_index > 1)
			{
				rates_info->max_b_index = rates_info->max_b_index - 1;
			}
		}
		else 
		{
			rates_info->max_b_index = rates_info->max_g_index + RATE_INDEX_B_1M;
			rates_info->max_g_index = 0;

		}

		atbm_printk_rc("maxrateidx 11n:%d 11g:%d 11b:%d rate_mask:0x%x\n", rates_info->max_ht_index,
			 rates_info->max_g_index, rates_info->max_b_index, rate_mask);

	}
}



/*change max tx rate:mat_tp_rate*/
u8 down_sample_selection(RATE_MODE_TYPE rate_mode, struct RATE_CONTROL_RATE_S * rates_info, u8 max_tp_rate)
{
	u8				down_max_rate = max_tp_rate;
	u8				max_phy_rate, min_phy_rate;

	get_rate_max_min(rate_mode, rates_info, &max_phy_rate, &min_phy_rate); // update params

	/*set default upgrade rate.*/
	if (max_tp_rate > min_phy_rate + 1)
	{
		down_max_rate		= rates_info->max_tp_rate_idx - 1;
	}

	atbm_printk_rc("max_tp:%d  down_sample max_rate:%d \n", max_tp_rate, down_max_rate);
	return down_max_rate;
}




#if 1

/*
rssi: Rx packets mean rssi.
ch_idle_ratio:	channel idle ratio
main_snd_ratio: channel idle ratio
m_tp_rate:		max_tp_rate phy_index.
mtp_prob:		mmax_tp_rate's success prob.
cur_band40_2_20m: 1:on 20m of 40m, 0:40m
*/
unsigned char bw_select_40m(signed char rssi, unsigned char ch_idle_ratio, unsigned short main_snd_ratio, 
	unsigned char m_tp_rate, u8 mtp_prob, unsigned char cur_band40_2_20m, 
	unsigned short bw_pkt_cnt)
{

	unsigned char	bw_40m_select = 1;

	unsigned char	bw40m_to_20m_idle_thresh = SND_IDLE_SET_20M_THRESH;
	unsigned char	mtp_prob_thresh = 60;

	unsigned char	rate_index_n_thresh = RATE_INDEX_N_26M;
	unsigned char	rate_index_he_thresh = RATE_INDEX_HE_MCS3;

#if BANDWIDTH_MORE_20M
	unsigned short	pkt_cnt_thresh = 1200;
	unsigned char	snd_idle_thresh = 40;

	if (cur_band40_2_20m)
	{
		rate_index_n_thresh = RATE_INDEX_N_52M;
		rate_index_he_thresh = RATE_INDEX_HE_MCS5;
		pkt_cnt_thresh		= (pkt_cnt_thresh * 7) / 5;
		snd_idle_thresh 	= snd_idle_thresh + 10;
	}
	else 
	{
		rate_index_n_thresh = RATE_INDEX_N_39M;
		rate_index_he_thresh = RATE_INDEX_HE_MCS4;
	}

#endif



#if BANDWIDTH_MORE_20M
	bw40m_to_20m_idle_thresh = bw40m_to_20m_idle_thresh + 5;
#endif

	//atbm_printk_err("rssi:%d  snd_ratio:%d  m_sndr:%d m_tp:%d mtp_prob:%d	bw20:%d\n",rssi, ch_idle_ratio,main_snd_ratio, m_tp_rate, mtp_prob, cur_band40_2_20m);
	if (cur_band40_2_20m)
	{
		bw_40m_select		= 0;
	}

	if ((ch_idle_ratio == 0) || (main_snd_ratio == 0))
	{
		return bw_40m_select;
	}

	/*set bw select thresh*/
	if (main_snd_ratio > 60)
	{
		bw40m_to_20m_idle_thresh = SND_IDLE_SET_20M_THRESH + 4; //main > snd, likely to remain 40M
	}
	else if (main_snd_ratio > 40)
	{
		bw40m_to_20m_idle_thresh = SND_IDLE_SET_20M_THRESH;
	}
	else 
	{
		bw40m_to_20m_idle_thresh = SND_IDLE_SET_20M_THRESH - 2;
	}

	if (cur_band40_2_20m) //if set 20M, more likely to keep 20M, if not set more likely keep 40M
	{
		bw40m_to_20m_idle_thresh += 2;
		mtp_prob_thresh 	+= 10;
	}

	if (rssi < -75) //||(ch_idle_ratio <= bw40m_to_20m_idle_thresh))
	{
		bw_40m_select		= 0;
	}
	else //>-75dbm && >20%
	{
		if ((m_tp_rate < RATE_INDEX_N_13M) || ((m_tp_rate < RATE_INDEX_HE_MCS1) && (m_tp_rate >= RATE_INDEX_HE_MCS0)))
		{
			bw_40m_select		= 0;
		}
		else if ((m_tp_rate <= rate_index_n_thresh) || ((m_tp_rate <= rate_index_he_thresh) &&
			 (m_tp_rate >= RATE_INDEX_HE_MCS0)))
		{
			if (ch_idle_ratio < (bw40m_to_20m_idle_thresh + 5))
			{
				bw_40m_select		= 0;
			}
			else 
			{
				bw_40m_select		= 1;
			}

#if BANDWIDTH_MORE_20M

			if ((bw_pkt_cnt < pkt_cnt_thresh) && (ch_idle_ratio < snd_idle_thresh)) //more likely to select 20M
			{
				bw_40m_select		= 0;
			}

#endif

		}
		else //>=39M
		{
			if ((ch_idle_ratio < bw40m_to_20m_idle_thresh) && (mtp_prob < mtp_prob_thresh))
			{
				bw_40m_select		= 0;
			}
			else 
			{
				bw_40m_select		= 1;
			}
		}
	}

	return bw_40m_select;
}


unsigned char bw_select_40m_simple(signed char rssi, 
	unsigned char m_tp_rate, u8 mtp_prob, unsigned char cur_band40_2_20m, 
	unsigned short bw_pkt_cnt)
{

	unsigned char	bw_40m_select = 1;

	unsigned char	mtp_prob_thresh = 75;  //30%

	unsigned char	rate_index_n_thresh = RATE_INDEX_N_26M;
	unsigned char	rate_index_he_thresh = RATE_INDEX_HE_MCS3;

#if BANDWIDTH_MORE_20M
	unsigned short	pkt_cnt_thresh = 1200;

	if (cur_band40_2_20m)
	{
		rate_index_n_thresh = RATE_INDEX_N_52M;
		rate_index_he_thresh = RATE_INDEX_HE_MCS5;
		pkt_cnt_thresh		= (pkt_cnt_thresh * 7) / 5;		
	}
	else 
	{
		rate_index_n_thresh = RATE_INDEX_N_39M;
		rate_index_he_thresh = RATE_INDEX_HE_MCS4;
	}

#endif


	//atbm_printk_err("rssi:%d  m_tp:%d mtp_prob:%d bw_pkt_cnt:%d	bw20:%d\n",rssi,  m_tp_rate, mtp_prob, bw_pkt_cnt, cur_band40_2_20m);
	if (cur_band40_2_20m)
	{
		bw_40m_select		= 0;
	}


	if (cur_band40_2_20m) //if set 20M, more likely to keep 20M, if not set more likely keep 40M
	{
		mtp_prob_thresh 	+= 10;
	}

	if (rssi < -75) //||(ch_idle_ratio <= bw40m_to_20m_idle_thresh))
	{
		bw_40m_select		= 0;
	}
	else //>-75dbm && >20%
	{
		if ((m_tp_rate < RATE_INDEX_N_13M) || ((m_tp_rate < RATE_INDEX_HE_MCS1) && (m_tp_rate >= RATE_INDEX_HE_MCS0)))
		{
			bw_40m_select = 0;
		}
		else if ((m_tp_rate <= rate_index_n_thresh) || ((m_tp_rate <= rate_index_he_thresh) &&
			 (m_tp_rate >= RATE_INDEX_HE_MCS0)))
		{
		
		   if ( (mtp_prob < mtp_prob_thresh + 10)) 
			{
				bw_40m_select		= 0;
			}
			else 
			{
				bw_40m_select		= 1;
			}

#if BANDWIDTH_MORE_20M
			if ((bw_pkt_cnt < pkt_cnt_thresh) && (mtp_prob < mtp_prob_thresh + 30)) //more likely to select 20M
			{
				bw_40m_select		= 0;
			}
			else 
			{
				bw_40m_select		= 1;
			}

#endif

		}
		else //>=39M
		{
			if ((bw_pkt_cnt < pkt_cnt_thresh) && (mtp_prob < mtp_prob_thresh))
			{
				bw_40m_select		= 0;
			}
			else 
			{
				bw_40m_select		= 1;
			}
		}
	}

	return bw_40m_select;
}


#endif


void set_lowest_rate(u8 rateidx, struct ieee80211_tx_rate_control * txrc)
{
	struct ieee80211_tx_info * info = txrc->info;
	RATE_SET_T		rate_set;
	u8				wsm_rate_id;
	u8				max_rate_idx = 0;

	memset(&rate_set, '\0', sizeof(RATE_SET_T));
	rate_set.lowest_rate_try = MAX_TRANS_NUMBER_RATE;
	rate_set.max_tp_rate_try = MAX_TRANS_NUMBER_RATE;

	//set rate
	if (rateidx >= RATE_INDEX_A_6M) //11g mode
	{
		rate_set.max_tp_rate_index = RATE_INDEX_A_6M;
		rate_set.lowest_rate_index = RATE_INDEX_A_6M; //default set same rate					

		if (rateidx == RATE_INDEX_A_6M) //lowest rate.
		{
			rate_set.rate_id_num_limit = 1;
			rate_set.rate_mode	= RATE_G;
		}
		else 
		{
			rate_set.rate_id_num_limit = 2;
			rate_set.rate_mode	= RATE_G;
			rate_set.max_tp_rate_index = rateidx;
		}
	}
	else //11b mode
	{
		rate_set.max_tp_rate_index = RATE_INDEX_B_1M;
		rate_set.lowest_rate_index = RATE_INDEX_B_1M; //default set same rate	   

		if (rateidx == RATE_INDEX_B_1M)
		{
			rate_set.rate_id_num_limit = 1;
			rate_set.rate_mode	= RATE_B;
		}
		else 
		{
			rate_set.rate_id_num_limit = 2;
			rate_set.rate_mode	= RATE_B;
			rate_set.max_tp_rate_index = rateidx;
		}
	}

	//rate_set.lowest_rate_index = rate_index_to_wsm_rate(rate_set.lowest_rate_index, rate_set.rate_mode);					 
	//rate_info_to32bit(&rate_set);    //to get rate mode  
	wsm_rate_id 		= rate_index_to_wsm_rate(rate_set.max_tp_rate_index, rate_set.rate_mode);

	//atbm_printk_rc("rate:%x %x \n", rate_set.max_tp_rate_index, wsm_rate_id);
	max_rate_idx		|= ((wsm_rate_id & 0x0f) << 4);
	max_rate_idx		|= (rate_set.max_tp_rate_try & 0x0f);

	info->control.tx_rate_sets = rate_info_to32bit(&rate_set) | HMAC_SET_RATES_USED;
	info->control.tx_max_rate = max_rate_idx;

	//atbm_printk_rc("wsmrate %x %x \n", info->control.tx_max_rate, wsm_rate_id);
}



static void hmac_get_rate(void * priv, struct ieee80211_sta * sta, void * priv_sta, 
	struct ieee80211_tx_rate_control * txrc)
{
	u8				rateid;
	u8				max_suc_wsm_rateid;
	u8				maxrateidx;
	u8				down_step = 1;
	u16 			rate_update_interval = 200;
	u8				max_phy_rate = RATE_INDEX_N_65M; // max possible rate's index.
	u8				min_phy_rate = RATE_INDEX_N_6_5M;
	u8				wsm_rate_id;
	u8				upgrade_allowed = 0;
	u8				change_flag = 0;
	u8				init_sample = 0;
	u16 			sample_number = 0;

	RATE_MODE_TYPE	rate_mode;

	RATE_SET_T		rate_sets;
	RATE_SET_T *	prate_set;

	struct ieee80211_tx_info * info = txrc->info;
	u32 			rate_mask = 0;

	struct hmac_rc_priv * rc_priv = priv;
	struct atbm_ieee80211_sta_he_cap * he_cap;

	/*txrc have no rate set, rate_sets will convert to one u32 data*/
	struct RATE_CONTROL_RATE_S * rates_info = priv_sta;
	info->control.tx_rate_sets = 0;
	info->control.tx_max_rate = 0;
	info->tx_update_rate = 0;
	info->sample_pkt_flag = 0;
	info->control.tx_rc_flag = 0;
	info->control.force_policyid = 0;

	/*Set manual set rate.*/
	if (txrc->manual)
	{
		u8				wsm_rateid;
		u32 			wsm_rateset;

		get_fixed_rate_set(txrc->manual_rate, &wsm_rateid, &wsm_rateset);

		if ((txrc->manual & TX_RATE_FIXED_AUTO_DOWN) == 0)
		{
			wsm_rateset 		|= HMAC_SET_RATES_USED; //use hmac set policy, not auto down
		}

		info->control.tx_max_rate = ((wsm_rateid << 4) | 0x07);
		info->control.tx_rate_sets = wsm_rateset;

		//add optional fixed rate statics
		if (txrc->manual & TX_RATE_FIXED_STATIC)
		{
			if ((sta != NULL) && (rates_info != NULL))
			{
				if (time_after(jiffies, rates_info->last_update_time + (rate_update_interval * HZ) / 1000))
				{
					info->tx_update_rate = 1;
					rates_info->last_update_time = jiffies;
				}
			}
		}

		if (txrc->manual_rate >= RATE_INDEX_HE_MCSER0)
		{
			if ((rates_info->tx_rc_flags & IEEE80211_TX_RC_HE_LDPC) || (txrc->manual_rate == RATE_INDEX_HE_MCS10) ||
				 (txrc->manual_rate == RATE_INDEX_HE_MCS11))
			{
				info->control.tx_rc_flag |= IEEE80211_TX_RC_HE_LDPC;
			}
		}

		//atbm_printk_rc("manual_fixed_rate:%x	wsm->txRateSets :%x \n", info->control.tx_max_rate,   info->control.tx_rate_sets );
		return;
	}

	/*set rate for broadcast and mgmt frame, rate set by hmac */
	if (rate_control_send_low(sta, priv_sta, txrc))
	{
		///	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(txrc->skb);
		if ((sta != NULL) && (rates_info != NULL))
		{
			if ((rates_info->mean_rssi > -50) && (info->control.tx_max_rate == RATE_INDEX_B_1M) &&
				 (rates_info->mean_rssi != 0))
			{
				info->control.tx_max_rate = RATE_INDEX_B_2M;
				atbm_printk_rc("############%s: changed send low idx:%d !\n", __func__, info->control.tx_max_rate);
			}
		}

		//atbm_printk_rc("%s: send low idx:%d !\n", __func__, info->control.tx_max_rate);
		set_lowest_rate(info->control.tx_max_rate, txrc);
		atbm_printk_rc("%s: send low idx:%x rateset:%x !\n", __func__, info->control.tx_max_rate,
			 info->control.tx_rate_sets);
		return;
	}

	if (sta == NULL)
	{
	}
	else 
	{
		rate_mask			= sta->supp_rates[0];
	}

	he_cap				= &sta->he_cap;

	rates_info->rate_set = &rate_sets;

	prate_set			= rates_info->rate_set;

	memset(prate_set, '\0', sizeof(RATE_SET_T));

	maxrateidx			= rates_info->max_tp_rate_idx;

	// atbm_printk_rc("%s: rates_info:0x%p get rate mdoe! max_tp_rate_idx:%d \n", __func__, rates_info, rates_info->max_tp_rate_idx);

	/*init sample, max rate index not exist, or resample required*/
	if ((maxrateidx == 0xff) || (rates_info->resample_required))
	{
		/*at least sample one time 11b mode, if signal be very bad*/
#if 0

		if ((rates_info->b_mode_sampled == 0) && (rates_info->n_signal_loss_flag))
		{
			get_rate_set_11b(sta, priv_sta, txrc);
			rates_info->sample_packets_cnt++;
			rates_info->sample_tp_rate_idx = prate_set->max_tp_rate_index;
			rates_info->b_mode_sampled = 1;
		}

#endif

		if ((sta->he_cap.has_he) && (rates_info->max_he_index)) /*if HE mode support, start with HE mode*/
		{
			//	printk("dcm cap:0x%x\n", sta->he_cap.he_cap_elem.phy_cap_info[3]&0x03);
			atbm_printk_rc("sample	he \n");
			get_rate_start_11ax(sta, priv_sta, txrc);
		}
		else if (sta->ht_cap.ht_supported) /*if HE mode not support,  start with HT mode*/
		{
			atbm_printk_rc("sample	11n \n");
			get_rate_start_11n(sta, priv_sta, txrc);
		}
		else if (fls(rate_mask) > 4) /*if HE&HT mode not support,  start with 11g mode*/
		{
			atbm_printk_rc("sample 11g \n");
			get_rate_start_11g(sta, priv_sta, txrc);
		}
		else //11B only
		{
			atbm_printk_rc("sample 11b tp:%d pcnt:%d\n", maxrateidx, rates_info->packets_cnt);

			get_rate_set_11b(sta, priv_sta, txrc);
			rates_info->packets_cnt++;
		}

		init_sample 		= 1;
		info->control.force_policyid |= (2 << 4);
		info->sample_pkt_flag = 1;

		//atbm_printk_rc("sample  he info->control.force_policyid %x\n", info->control.force_policyid);
	}
	else /*based on statics and updated rate info*/
	{

		/*1, set Max tp rate.

		a, max TP success ratio > 80%(thresh set) for example, will try to use upgraded rate.
		upgrade manually one time each period, if AMPDU support, send 8 or 16 packets, or 10 percent of last period 
			packets
		b, downgrade manually one time each period, if AMPDU support, send 8 or 16 packets or 10 percent of last 
			period packets,

		less than 50% success?	  seems not needed , LMAC will automatically downgrade based rate set.

		c, other packets on max_tp_rate_index set
		*/
		rateid				= phy_rate_index_to_descr(rates_info->max_tp_rate_idx);
		max_suc_wsm_rateid	= phy_rate_index_to_descr(rates_info->max_success_rate_idx);

		wsm_rateid_to_rate_group(rateid, &rate_mode);

		prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx; //default rate
		prate_set->rate_mode = rate_mode;

		//if(rates_info->n_signal_loss_flag)
		//printk("loss flag:%d samplecnt:%d\n",rates_info->n_signal_loss_flag,rates_info->sample_packets_cnt);
		get_rate_max_min(rate_mode, rates_info, &max_phy_rate, &min_phy_rate);

		// atbm_printk_rc("max_phy_rate:%d min_phy_rate:%d\n", max_phy_rate, min_phy_rate);
		//not reach max rate, and success ratio > thresh. not upgrade too much
		if (rates_info->max_tp_rate_idx < max_phy_rate)
		{
			upgrade_allowed 	= 1;
		}

		// if((rate_mode == RATE_B)&&(rates_info->sucess_prob[rateid] > (20 + rates_info->max_tp_rate_idx *10)))  //11b mode, more requirement to upgrade sample
		if ((rate_mode == RATE_B) || (rate_mode == RATE_HE_ER)) //11b mode, switch mode 
		{
			upgrade_allowed 	= 1;
		}

		sample_number		= rates_info->last_period_pkt_cnt / 20;

		if (sample_number > 50)
		{
			sample_number		= 50;
		}

		if (sample_number < 6)
		{
			if (sample_number >= 4)
			{
				sample_number		= 6;
			}
			else if (sample_number >= 2)
			{
				sample_number		= 4;
			}
			else 
			{
				sample_number		= 2;
			}
		}

		/*prob >80, sample a few packets each period, 11B and assoc mode rate. other cases add 50% upgrade sample 
			chance	*/
		if (((rates_info->sucess_prob[rateid] > (SAMPLE_RATE_IDEAL_SUCC_PROB_THRESH * 256) / 100) ||
			 ((rates_info->not_sample_cnt % 2) == 0)) && (upgrade_allowed)) /*normal upgrade rate sample, B mode back 
			sample*/
		{
			if (rates_info->sample_packets_cnt < sample_number) // sample ampdu number packets.
			{
				if (((rate_mode == RATE_B) && (rates_info->max_tp_rate_idx >= 2)) || (rate_mode == RATE_HE_ER)) //11b upgrade to 11g or 11n, ER0 upgrade to normal MCS0
				{
					if ((sta->he_cap.has_he) && (rates_info->max_he_index)) /*if HE mode support, start with HE mode*/
					{
						get_rate_max_min(RATE_HE, rates_info, &max_phy_rate, &min_phy_rate); // update params
						prate_set->rate_mode = RATE_HE;

						//	printk("dcm cap:0x%x\n", sta->he_cap.he_cap_elem.phy_cap_info[3]&0x03);
						prate_set->max_tp_rate_index = min_phy_rate + 1;
						if (rates_info->tx_rc_flags & IEEE80211_TX_RC_HE_SUPPORT_DCM) //support DCM, ER0 aways send with DCM
						{
							prate_set->max_tp_rate_index = min_phy_rate;
						}
					}
					else if (sta->ht_cap.ht_supported) /*if HE mode not support,  start with HT mode*/
					{
						get_rate_max_min(RATE_HT, rates_info, &max_phy_rate, &min_phy_rate); // update params
						prate_set->rate_mode = RATE_HT;

						//	if(rates_info->tx_rc_flags & IEEE80211_TX_RC_HT_LDPC)  //Support LDPC from 6.5M
						//	{
						//		prate_set->max_tp_rate_index = min_phy_rate;						   
						//	}
						//	else
						//	{
						prate_set->max_tp_rate_index = RATE_INDEX_N_13M; //BCC mode from 13M

						//	}							
					}
					else if (fls(rate_mask) > 4) /*if HE&HT mode not support, start with 11g mode*/
					{
						get_rate_max_min(RATE_G, rates_info, &max_phy_rate, &min_phy_rate); // update params
						prate_set->rate_mode = RATE_G;
						prate_set->max_tp_rate_index = min_phy_rate + 1;
					}
					else 
					{
						get_rate_max_min(RATE_B, rates_info, &max_phy_rate, &min_phy_rate); // update params
						prate_set->rate_mode = RATE_B;

						//atbm_printk_rc("### upgrade from 11B mode max_phy:%d tp:%x \n", max_phy_rate,rates_info->max_tp_rate_idx);
						if (rates_info->max_tp_rate_idx < max_phy_rate)
						{
							prate_set->max_tp_rate_index = (rates_info->max_tp_rate_idx + 1);
						}
					}

					change_flag 		= 1;
					atbm_printk_rc("######### upgrade from 11B mode tp:%x\n", rates_info->max_tp_rate_idx);

				}
				else if (rates_info->max_tp_rate_idx_history != rates_info->max_tp_rate_idx) //have history max tp rate, but not sampled or send on last period.
				{
					atbm_printk_rc("######### sample tp:%d histroy tp:%d\n", rates_info->max_tp_rate_idx,
						 rates_info->max_tp_rate_idx_history);
					prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx_history; //sample histroy max rate.		
					sample_number		= 2;
					rate_index_to_wsm_rate_mode(prate_set->max_tp_rate_index, &rate_mode);
					prate_set->rate_mode = rate_mode;
				}
				else //upgrade one step mode.
				{
					//may add signal strength calibration, direct upgrade	
					u8				descrateid = 0;

					descrateid			= phy_rate_index_to_descr(rates_info->max_tp_rate_idx);

					if ((rates_info->max_tp_rate_idx < max_phy_rate) &&
						 ((rates_info->last_max_tp_rate_idx >= rates_info->max_tp_rate_idx) ||
						 ((rates_info->last_max_tp_rate_idx < rates_info->max_tp_rate_idx) &&
						 (rates_info->sucess_prob[descrateid] > (30 * 256) / 100)))) //max tp rate not upgraded, or max tp rate upgraded, success > 40%
					{

						prate_set->max_tp_rate_index = (rates_info->max_tp_rate_idx + 1);

						if ((rates_info->sucess_prob_miss_flag[descrateid] == 0) &&
							 (rates_info->sucess_prob[descrateid] < (40 * 256) / 100))
						{
							if (sample_number > 8)
							{
								sample_number		= 8;
							}

							if (rates_info->sucess_prob[descrateid] < (20 * 256) / 100) //too bad
							{
								if (sample_number > 2)
								{
									sample_number		= 2;
								}
							}
						}
					}
					else 
					{
						prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx;
					}

				}

				rates_info->sample_packets_cnt++;
				rates_info->sample_tp_rate_idx = prate_set->max_tp_rate_index;
				info->control.force_policyid |= (2 << 4);
				info->sample_pkt_flag = 1;
				atbm_printk_rc("upgrade samp_cnt:%d  max tp prob:%d set index:%d max_tp_rate_idx:%d his_tp_id:%d\n",
					 rates_info->sample_packets_cnt, rates_info->sucess_prob[rateid], prate_set->max_tp_rate_index,
					 rates_info->max_tp_rate_idx, rates_info->max_tp_rate_idx_history);
			}
			else 
			{
				prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx;
				atbm_printk_rc("normal tp:%d smpcnt:%d\n", prate_set->max_tp_rate_index,
					 rates_info->sample_packets_cnt);
			}

			prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS;

			//	atbm_printk_rc("Good signal get rate max tp prob:%d set index:%d max_tp_rate_idx:%d\n", rates_info->sucess_prob[rateid], prate_set->max_tp_rate_index, rates_info->max_tp_rate_idx);
		}
		else if ((rates_info->max_tp_rate_idx == min_phy_rate) && ((sta->he_cap.has_he) &&
			 (rates_info->max_he_index))) /*HE msc0 down to ER +DCM mode sample*/
		{
			if ((rates_info->sucess_prob[rateid] < (SAMPLE_DCM_PROB_THRESH * 256) / 100) && (rate_mode == RATE_HE) &&
				 (rates_info->tx_rc_flags & IEEE80211_TX_RC_HE_ER))
			{
				if (rates_info->sample_packets_cnt < sample_number)
				{
					atbm_printk_rc("######### set_he mode to ER mode:%x cnt:%d\n", rates_info->max_tp_rate_idx,
						 rates_info->sample_packets_cnt);
					rate_mode			= RATE_HE_ER;
					prate_set->rate_mode = RATE_HE_ER;
					prate_set->max_tp_rate_index = RATE_INDEX_HE_MCSER0;
					prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS;
					get_rate_max_min(RATE_HE_ER, rates_info, &max_phy_rate, &min_phy_rate); // update min_phy_rate params

					if ((rates_info->tx_rc_flags & IEEE80211_TX_RC_HE_SUPPORT_DCM) &&
						 (rates_info->max_tp_rate_idx <= min_phy_rate + rates_info->max_dcm_rate_idx))
					{
						atbm_printk_rc("######### set_dcm_flag:%x\n", prate_set->max_tp_rate_index);
						info->control.tx_rc_flag |= IEEE80211_TX_RC_HE_DCM;
					}

					rates_info->sample_packets_cnt++;
					info->sample_pkt_flag = 1;

				}
				else 
				{
					prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx;
					prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS;
				}
			}
			else 
			{
				prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx;
				prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS;
			}
		}
		else if ((rates_info->sucess_prob[rateid] < (SAMPLE_RATE_DOWN_SUCC_PROB_THRESH * 256) / 100) &&
			 ((rate_mode == RATE_HT) || (rate_mode == RATE_G) || ((rate_mode == RATE_HE) &&
			 ((rates_info->tx_rc_flags & IEEE80211_TX_RC_HE_ER) == 0))))
		{ //HE(no ER), HT or 11G low rate signal, down to 11B mode sample

			if (rates_info->sample_packets_cnt < min((u16) 6, sample_number)) // sample  packets.
			{
				if (rates_info->max_tp_rate_idx <= (min_phy_rate + 1))
				{
					// printk("######### down to 11B mode :%x\n", rates_info->max_tp_rate_idx);
					if(rates_info->max_b_index != 0)
					{
						rate_mode			= RATE_B;
						get_rate_max_min(RATE_B, rates_info, &max_phy_rate, &min_phy_rate); // update params				
						prate_set->rate_mode = RATE_B;
						prate_set->max_tp_rate_index = max_phy_rate;
					}else
						{
						prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx;
						}
					rates_info->sample_packets_cnt++;

					prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS;

					rates_info->sample_tp_rate_idx = prate_set->max_tp_rate_index;
					atbm_printk_rc("######### low rate down to 11B mode :%x\n", rates_info->max_tp_rate_idx);
					change_flag 		= 1;
				}
				else //down rate sample on same mode
				{
					prate_set->max_tp_rate_index = down_sample_selection(rate_mode, rates_info,
						 rates_info->max_tp_rate_idx);
					prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx;
					prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS + 1;
					rates_info->sample_packets_cnt++;
				}

				info->sample_pkt_flag = 1;
			}
			else // normal tx mode
			{
				prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx;
			}

			prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS + 1;

		} /*signal may changed, add 11b sample */
		else if ((rates_info->n_signal_loss_flag) && ((rate_mode == RATE_HT) || (rate_mode == RATE_G)))
		{ // BAD SIGNAL, DOWN TO 11B mode

			if (rates_info->sample_packets_cnt < min((u16) 6, sample_number)) // 11b sample less number packets.
			{
				// printk("Down to 11B mode :%x\n", rates_info->max_tp_rate_idx);
				if(rates_info->max_b_index != 0)
				{
					rate_mode			= RATE_B;
					get_rate_max_min(RATE_B, rates_info, &max_phy_rate, &min_phy_rate); // update params				
					prate_set->rate_mode = RATE_B;
					prate_set->max_tp_rate_index = max_phy_rate;
				}else
					{
						prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx;
					}
				prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS;

				rates_info->sample_packets_cnt++;
				rates_info->sample_tp_rate_idx = prate_set->max_tp_rate_index;
				atbm_printk_rc("######### sginal  down to 11B mode loss_flag:%d rate:%d\n",
					 rates_info->n_signal_loss_flag, rates_info->max_tp_rate_idx);
				change_flag 		= 1;
				info->sample_pkt_flag = 1;
			}
			else 
			{
				prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx;
			}

			prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS;


		}
		else //Normal Tx rate
		{
			// atbm_printk_rc("get rate max tp:%d\n", rates_info->max_tp_rate_idx);
			prate_set->max_tp_rate_index = rates_info->max_tp_rate_idx;
			prate_set->max_tp_rate_try = DEFAULT_RATE_TRANS;
		}


		/* bandwidth select, HT&HE*/
		if (prate_set->max_tp_rate_index >= RATE_INDEX_N_6_5M) //HT or HE mode
		{
			if ((rates_info->tx_rc_flags & IEEE80211_TX_RC_40_MHZ_WIDTH) &&
				 (rates_info->tx_rc_flags & IEEE80211_TX_RC_20_MHZ_WIDTH))
			{
				if (rates_info->tx_rc_flags & IEEE80211_TX_RC_40M_TO_20M)
				{
					info->control.tx_rc_flag |= IEEE80211_TX_RC_40M_TO_20M; //force 20M bandwidth

					// atbm_printk_rc("######### BW select 20M rate:%d\n",rates_info->max_tp_rate_idx);
				}
			}
		}

		/*2, set the lowest rate index*/
		prate_set->lowest_rate_index = min_phy_rate; /*if all above rates failed, will try this rate*/

		if (prate_set->lowest_rate_index != prate_set->max_tp_rate_index)
		{
			prate_set->rate_id_num_limit = 2;
			prate_set->lowest_rate_try = DEFAULT_RATE_TRANS + 3; /*add a new rate.*/
		}
		else 
		{
			prate_set->rate_id_num_limit = 1;

			/*add retransmit for highest rate*/
			prate_set->max_tp_rate_try = prate_set->max_tp_rate_try + DEFAULT_RATE_TRANS;
			prate_set->lowest_rate_try = 0;
		}


		/*3, set max success prob rate index, may be equal to max tp rate or lowest rate.*/
		if (prate_set->rate_id_num_limit < MAX_RATE_NUMBER)
		{

			prate_set->max_suc_rate_index = rates_info->max_success_rate_idx;

			if (prate_set->max_suc_rate_index >= prate_set->max_tp_rate_index) /*Max success possibility rate index, 
				may be the highest rate index*/
			{
				prate_set->max_suc_rate_index = prate_set->max_tp_rate_index; /*set equal rate, rate number not 
					changed*/
				prate_set->max_suc_rate_try = 0;
			}
			else /*try to add new rate*/
			{
				if (prate_set->max_suc_rate_index > prate_set->lowest_rate_index)
				{
					prate_set->max_suc_rate_try = DEFAULT_RATE_TRANS + 2;
					prate_set->rate_id_num_limit++; /*add a new rate.*/
				}
				else 
				{
					// max_suc_rate equal lowest rate, rate number will not be changed.
					prate_set->max_suc_rate_index = prate_set->lowest_rate_index;
					prate_set->max_suc_rate_try = 0;
				}
			}

			if ((prate_set->max_tp_rate_index > (prate_set->lowest_rate_index + 2))) //insert a rate. 
			{
				prate_set->max_suc_rate_index = prate_set->max_tp_rate_index - 1;
			}


		}
		else /*can not add max success prob rate anymore.*/
		{
			prate_set->max_suc_rate_try = 0;
		}

		/*
		down rate depends on the highest rate and lowest rate and MAX_RATE_NUMBER as limit.
		*/
		// down_rate = rates_info->max_tp_rate_idx - down_step;

		/*4, set optionally down rate between max success rate and lowest rate*/
		if (prate_set->rate_id_num_limit < MAX_RATE_NUMBER) /*can set more rate*/
		{
			if (prate_set->max_tp_rate_index > (prate_set->lowest_rate_index + 1)) /* there is gap between max success
				 rate and lowest rate, for example: 4->2, or 5->2*/
			{
				if (prate_set->max_tp_rate_index > (prate_set->lowest_rate_index + 2)) /*down step too big. change 
					down step*/
				{
					down_step			= 2;

					if (prate_set->max_suc_rate_try == 0) //insert one rate if max success rate not set: equal max or lowest.
					{
						prate_set->max_suc_rate_index = prate_set->max_tp_rate_index - 1;
						prate_set->max_suc_rate_try = DEFAULT_RATE_TRANS;
						prate_set->rate_id_num_limit++;
					}
				}
				else 
				{
					down_step			= 1;
				} /*down step not changed.*/

				prate_set->down_rate_index = prate_set->max_tp_rate_index - down_step; /*If the first rate failed, 
					will try this rate, should be a different rate index*/

				if (prate_set->down_rate_index != prate_set->max_suc_rate_index)
				{
					prate_set->down_rate_try = DEFAULT_RATE_TRANS + 2;
					prate_set->rate_id_num_limit++; /*add a new rate.*/

					if (change_flag)
						atbm_printk_rc("rate_set->down_rate_index:%d\n", prate_set->down_rate_index);

				}
			}
			else /*no gap between max success rate and lowest rate to insert one rate.*/
			{
				prate_set->down_rate_index = prate_set->lowest_rate_index; /*equal rate, will not be set by lmac*/
				prate_set->down_rate_try = 0;
			}
		}
		else 
		{
			prate_set->down_rate_index = 0;
			prate_set->down_rate_try = 0;
		}
	}



	/*HE GI mode change to short gi */
	//	if((sta->he_cap.has_he)&&(rates_info->sucess_prob[rateid] > (SAMPLE_RATE_IDEAL_SUCC_PROB_THRESH * 256) / 100)) 
	if ((prate_set->max_tp_rate_index >= RATE_INDEX_HE_MCSER0) && ((rates_info->max_tp_rate_idx != 0xff)||(init_sample == 1)))//lmac short gi from rate index, if hmac change, need to change lmac
	{
		if (prate_set->max_tp_rate_index >= HE_SHORT_GI_START_RATE)
		{
#if 0
			if (rates_info->tx_rc_flags & IEEE80211_TX_RC_SHORT_GI_40M)
			{
				info->control.tx_rc_flag |= IEEE80211_TX_RC_SHORT_GI_40M;
				info->control.tx_rc_flag |= IEEE80211_TX_RC_SHORT_GI;
			}

			if (rates_info->tx_rc_flags & IEEE80211_TX_RC_SHORT_GI_20M)
			{
				info->control.tx_rc_flag |= IEEE80211_TX_RC_SHORT_GI_20M;
				info->control.tx_rc_flag |= IEEE80211_TX_RC_SHORT_GI;
			}
#endif
			info->control.tx_rc_flag |= IEEE80211_TX_RC_SHORT_GI;

			prate_set->max_tp_rate_try += 1;
		}

		if ((rates_info->tx_rc_flags & IEEE80211_TX_RC_HE_LDPC) ||
			 (prate_set->max_tp_rate_index == RATE_INDEX_HE_MCS10) ||
			 (prate_set->max_tp_rate_index == RATE_INDEX_HE_MCS11))
		{
			info->control.tx_rc_flag |= IEEE80211_TX_RC_HE_LDPC;
		}

		if (rates_info->tx_rc_flags & IEEE80211_TX_RC_HE_SUPPORT_DCM)
		{
			info->control.tx_rc_flag |= IEEE80211_TX_RC_HE_SUPPORT_DCM;
		}
	}

	/*HT mode:good signal on highest rate, upgrade to short GI */
	if ((rate_mode == RATE_HT) && ((rates_info->max_tp_rate_idx != 0xff)||(init_sample == 1)))
	{
		// USE short GI
		rateid				= phy_rate_index_to_descr(rates_info->max_tp_rate_idx);

		if ((prate_set->max_tp_rate_index >= RATE_INDEX_N_65M) &&
			 (rates_info->sucess_prob[rateid] > (SAMPLE_RATE_UP_SUCC_PROB_THRESH * 256) / 100))
		{

			if (rates_info->tx_rc_flags & IEEE80211_TX_RC_SHORT_GI_40M)
			{
				info->control.tx_rc_flag |= IEEE80211_TX_RC_SHORT_GI_40M;
				info->control.tx_rc_flag |= IEEE80211_TX_RC_SHORT_GI;
			}

			if (rates_info->tx_rc_flags & IEEE80211_TX_RC_SHORT_GI_20M)
			{
				info->control.tx_rc_flag |= IEEE80211_TX_RC_SHORT_GI_20M;
				info->control.tx_rc_flag |= IEEE80211_TX_RC_SHORT_GI;
			}
		}

		if (rates_info->tx_rc_flags & IEEE80211_TX_RC_HT_LDPC)
		{
			info->control.tx_rc_flag |= IEEE80211_TX_RC_HT_LDPC;
		}
	}




	/*Build rate policy and set update flag*/
	txrc->max_rate_idx	= 0;

	//	if( prate_set->ma x_tp_rate_index>23)
	//	atbm_printk_rc("######## %s max_tp_rate_index:%d lowest_rate_index:%d \n", __func__, prate_set->max_tp_rate_index, prate_set->lowest_rate_index );
	rate_index_to_wsm_rate_mode(prate_set->max_tp_rate_index, &rate_mode);
	wsm_rate_id 		= rate_index_to_wsm_rate(prate_set->max_tp_rate_index, prate_set->rate_mode);

	if (rate_mode != prate_set->rate_mode)
	{
		prate_set->rate_mode = rate_mode;
		printk(KERN_ALERT "Err rate_index:%d %d %d\n", prate_set->max_tp_rate_index, rate_mode, prate_set->rate_mode);
	}

	if (wsm_rate_id <= 4)
	{
		prate_set->max_tp_rate_try += 2;
	}

	txrc->max_rate_idx	|= ((wsm_rate_id & 0x0f) << 4);

	if (prate_set->max_tp_rate_try > 15)
	{
		prate_set->max_tp_rate_try = 15;
	}

	txrc->max_rate_idx	|= (prate_set->max_tp_rate_try & 0x0f);

	//manual set fixed rate, not set rates flag, lmac use policy 1.
	info->control.tx_rate_sets = rate_info_to32bit(prate_set);

	if (init_sample == 0)
	{
		//	info->control.tx_rate_sets	|= HMAC_SET_RATES_USED;
	}

	info->control.tx_max_rate = txrc->max_rate_idx;




	/*if max tp rate changes, use short update period, if max tp rate be stable use long update period*/
	rate_update_interval = rc_priv->update_interval;

	if (rates_info->last_max_tp_rate_idx != rates_info->max_tp_rate_idx)
	{
		rate_update_interval = rc_priv->update_interval / 2;
	}

	//atbm_printk_rc("jiffies:%ld  lstime:%ld  hz:%d  interval:%d\n", jiffies, tmptime, HZ, rc_priv->update_interval);
	if (time_after(jiffies, rates_info->last_update_time + (rate_update_interval * HZ) / 1000))
	{
		// minstrel_update_stats(mp, mi);
		atbm_printk_rc("set rate status update flag sample_number:%d \n", sample_number);
		info->tx_update_rate = 1;
		rates_info->last_update_time = jiffies;

		//print_phy_rate(prate_set->max_tp_rate_index);
		atbm_printk_rc("%s tx_max_rate:0x%x tx_rate_sets:0x%x lstime:%ld hashe:%d\n",
			 strRateIndexPhy[prate_set->max_tp_rate_index], info->control.tx_max_rate, info->control.tx_rate_sets,
			 rates_info->last_update_time, sta->he_cap.has_he);
	}

	//force update based on sample packets.
	if (rates_info->packets_cnt == 6)
	{
		info->tx_update_rate = 1;
		rates_info->last_update_time = jiffies;
	}

	if (rates_info->packets_cnt == 12)
	{
		rates_info->packets_cnt = 0;
		rates_info->resample_required = 0;
		info->tx_update_rate = 1;
		rates_info->last_update_time = jiffies;
	}
}



static void hmac_rate_update(void * priv, struct ieee80211_supported_band * sband, 
	struct ieee80211_sta * sta, void * priv_sta, 
	u32 changed, enum nl80211_channel_type oper_chan_type)
{
	//	unsigned char is_ap_flag = 0;
	//	minstrel_ht_update_caps(priv, sband, sta, priv_sta, oper_chan_type);
	struct atbm_ieee80211_sta_he_cap * he_cap;
	u16 			sta_ht_cap = sta->ht_cap.cap;
	u8				is_ap_flag = 0;

	struct RATE_CONTROL_RATE_S * rates_info = priv_sta;
	struct sta_info * stai = container_of(sta, struct sta_info, sta);
	struct ieee80211_local * local = stai->local;
	struct ieee80211_channel_state * chan_state = ieee80211_get_channel_state(local, stai->sdata);
	RATE_CONTROL_RATE * rate_info = priv_sta;

	//	  is_ap_flag = !sta->is_sta;
	reset_rate_info(rate_info, is_ap_flag);
	rate_info->last_update_time = jiffies;
	rate_info->bw_total_pkt_update_time = jiffies;
	atbm_printk_rc("rate_info: 0x%p last_update_time:%ld centerFreq:%d channel_type:%d\n", rate_info,
		 rate_info->last_update_time, chan_state->conf.channel->center_freq, chan_state->conf.channel_type);
	rate_info->center_freq = chan_state->conf.channel->center_freq;


	/*
	IEEE80211_TX_RC_HT_LDPC			= BIT(8),
	IEEE80211_TX_RC_HE_LDPC			= BIT(9),
	IEEE80211_TX_RC_HE_DCM			= BIT(10),
	IEEE80211_TX_RC_HE_ER			= BIT(11),
	IEEE80211_TX_RC_40M_TO_20M		= BIT(12),
	*/
	atbm_printk_rc("%s	sta_ht_cap:%x \n", __func__, sta_ht_cap);

	if (sta_ht_cap & IEEE80211_HT_CAP_LDPC_CODING)
	{
		rates_info->tx_rc_flags |= IEEE80211_TX_RC_HT_LDPC;
		atbm_printk_err(" %s,%d:HT LDPC\n", __func__, __LINE__);
	}

	if (sta_ht_cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)
	{
		rates_info->tx_rc_flags |= IEEE80211_TX_RC_20_MHZ_WIDTH;
		atbm_printk_rc("######## %s,%d:HT 20_40M\n", __func__, __LINE__);

		//rates_info->tx_rc_flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;	//set by oper_chan_type.
	}

	if (oper_chan_type != NL80211_CHAN_HT40MINUS && oper_chan_type != NL80211_CHAN_HT40PLUS)
	{
		atbm_printk_rc("%s,%d:IEEE80211_HT_CAP_SUP_WIDTH_20_40 not support 40M\n", __func__, __LINE__);

		//sta_cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		rates_info->tx_rc_flags &= ~IEEE80211_TX_RC_40_MHZ_WIDTH;
	}
	else 
	{
		atbm_printk_rc("%s,%d:IEEE80211_HT_CAP_SUP_WIDTH_20_40 support 40M\n", __func__, __LINE__);
		rates_info->tx_rc_flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
	}


	if (sta_ht_cap & IEEE80211_HT_CAP_SGI_20)
	{
		rates_info->tx_rc_flags |= IEEE80211_TX_RC_SHORT_GI_20M;
		atbm_printk_rc("HT support SHORT_GI_20M\n");
	}

	if (sta_ht_cap & IEEE80211_HT_CAP_SGI_40)
	{
		rates_info->tx_rc_flags |= IEEE80211_TX_RC_SHORT_GI_40M;
		atbm_printk_rc("HT support SHORT_GI_40M\n");
	}


	// if((rates_info->max_he_index == 0)&&(rates_info->max_ht_index == 0)&&(rates_info->max_g_index == 0))
	get_max_assoc_rate(sta, rates_info);


	he_cap				= &sta->he_cap;

	if (he_cap != NULL)
	{
		atbm_printk_rc("%s	\n", __func__);

		if (he_cap->has_he) /*if HE mode support, start with HE mode*/
		{

			atbm_printk_err("dcm peer rx:%x sta tx depends\n", (sta->he_cap.he_cap_elem.phy_cap_info[3] >> 3) & 0x03);
													//peer RX

			if (((sta->he_cap.he_cap_elem.phy_cap_info[3] >> 3) & 0x03) ==
				 ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK)
			{
				rates_info->max_dcm_rate_idx = 0;
				rates_info->tx_rc_flags |= IEEE80211_TX_RC_HE_SUPPORT_DCM;
			}
			else if (((sta->he_cap.he_cap_elem.phy_cap_info[3] >> 3) & 0x03) ==
				 ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_QPSK)
			{
				rates_info->max_dcm_rate_idx = 1;
				rates_info->tx_rc_flags |= IEEE80211_TX_RC_HE_SUPPORT_DCM;
			}
			else if (((sta->he_cap.he_cap_elem.phy_cap_info[3] >> 3) & 0x03) ==
				 ATBM_IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM)
			{
				rates_info->max_dcm_rate_idx = 4;	// MCS3,4
				rates_info->tx_rc_flags |= IEEE80211_TX_RC_HE_SUPPORT_DCM;
			}
			else 
			{
				rates_info->max_dcm_rate_idx = 0;
				rates_info->tx_rc_flags &= ~IEEE80211_TX_RC_HE_SUPPORT_DCM;
			}

			if (sta->he_cap.he_cap_elem.phy_cap_info[1] &ATBM_IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD)
			{
				rates_info->tx_rc_flags |= IEEE80211_TX_RC_HE_LDPC;
			}
			else
			{
				rates_info->tx_rc_flags &= ~IEEE80211_TX_RC_HE_LDPC;
				rates_info->tx_rc_flags &= ~IEEE80211_TX_RC_HE_SUPPORT_DCM;
			}

			if (sta->he_cap.he_operation.he_oper_params & ATBM_IEEE80211_HE_OPERATION_ER_SU_DISABLE)
			{
				rates_info->tx_rc_flags &= ~IEEE80211_TX_RC_HE_ER;
			}
			else 
			{
				rates_info->tx_rc_flags |= IEEE80211_TX_RC_HE_ER; //HE ER operation.
			}


		}
	}

	rates_info->rssi_count = 0;
	rates_info->total_rssi = 0;
	rates_info->mean_rssi = 0;
	rates_info->max_rssi = -128;

	rates_info->min_rssi = 127;

	rates_info->last_update_time = jiffies;

	// atbm_printk_rc("hmac_alloc_sta: 0x%x last_update_time:%d\n", pHmac_rate, pHmac_rate->last_update_time );
	rates_info->b_valid = true;

	// reset_rate_info(rate_info, is_ap_flag);
}


static void hmac_rate_init(void * priv, struct ieee80211_supported_band * sband, 
	struct ieee80211_sta * sta, void * priv_sta)
{
	u8				is_ap_flag = 0;

	struct sta_info * stai = container_of(sta, struct sta_info, sta);
	struct ieee80211_local * local = stai->local;
	struct ieee80211_channel_state * chan_state = ieee80211_get_channel_state(local, stai->sdata);
	RATE_CONTROL_RATE * rate_info = priv_sta;

	//	  is_ap_flag = !sta->is_sta;
	reset_rate_info(rate_info, is_ap_flag);
	rate_info->last_update_time = jiffies;
	rate_info->bw_total_pkt_update_time = jiffies;
	atbm_printk_rc("rate_info: 0x%p last_update_time:%ld centerFreq:%d channel_type:%d\n", rate_info,
		 rate_info->last_update_time, chan_state->conf.channel->center_freq, chan_state->conf.channel_type);
	rate_info->center_freq = chan_state->conf.channel->center_freq;

	hmac_rate_update(priv, sband, sta, priv_sta, 0, chan_state->conf.channel_type);

	//	rate_info->b_ht_sta  = sta->ht;
}


static void * hmac_alloc_sta(void * priv, struct ieee80211_sta * sta, gfp_t gfp)
{

	RATE_CONTROL_RATE * pHmac_rate;

	// uint8_t is_ap_flag;
	atbm_printk_rc("%s	\n", __func__);

	pHmac_rate			= atbm_kzalloc(sizeof(RATE_CONTROL_RATE) +sizeof(struct txrate_status) + 2 * sizeof(u32),
		 gfp);

	if (!pHmac_rate)
		return NULL;

	pHmac_rate->rssi_count = 0;
	pHmac_rate->total_rssi = 0;
	pHmac_rate->mean_rssi = 0;
	pHmac_rate->max_rssi = -128;

	pHmac_rate->min_rssi = 127;

	pHmac_rate->last_update_time = jiffies;

	// atbm_printk_rc("hmac_alloc_sta: 0x%x last_update_time:%d\n", pHmac_rate, pHmac_rate->last_update_time );
	pHmac_rate->b_valid = true;

	sta->txs_retrys 	= (u16 *) (pHmac_rate + 1);

	sta->n_rates		= RATE_DESC_INDEX_MAX;


	return pHmac_rate;
}


static void * hmac_alloc(struct ieee80211_hw * hw, struct dentry * debugfsdir)
{
	struct hmac_rc_priv * mp;

	// hmac_init();
	mp					= atbm_kzalloc(sizeof(struct hmac_rc_priv), GFP_ATOMIC);

	if (!mp)
		return NULL;

	mp->hw				= hw;
	mp->update_interval = MAX_STATICS_PERIOD;
	return mp;
}


static void hmac_free(void * priv)
{

	atbm_kfree(priv);
}


static void hmac_free_sta(void * priv, struct ieee80211_sta * sta, 
	void * priv_sta)
{
	atbm_kfree(priv_sta);
}


static void hmac_rx_status(void * priv, struct ieee80211_supported_band * sband, 
	struct ieee80211_sta * sta, void * priv_sta, 
	struct sk_buff * skb)
{
	RATE_CONTROL_RATE * rc_sta_info = priv_sta;

	struct ieee80211_rx_status * status = IEEE80211_SKB_RXCB(skb);
	int 			rssi = status->signal;

	//	printk(KERN_ERR "wsm_receive_indication rssi:%d\n", rx.rcpiRssi);
	rc_sta_info->total_rssi = rc_sta_info->total_rssi + rssi;
//	if(ieee80211_is_data(status->frame_ctrl) || ieee80211_is_data_qos(status->frame_ctrl))
	if(status->flag & RX_FLAG_HE)
		rc_sta_info->rx_rate_idx = status->rate_idx + 26;
	else if (status->flag & RX_FLAG_HE_ER)
		rc_sta_info->rx_rate_idx = status->rate_idx + 23;
	else if (status->flag & RX_FLAG_HT)
		rc_sta_info->rx_rate_idx = status->rate_idx + 14;
	else if (status->flag & RX_FLAG_11G){
		if (status->band == IEEE80211_BAND_5GHZ)
			rc_sta_info->rx_rate_idx = status->rate_idx + 6;
		else
			rc_sta_info->rx_rate_idx = status->rate_idx + 2;
	}else
		rc_sta_info->rx_rate_idx = status->rate_idx;
	
//		atbm_printk_err("hmac_rx_status:rx_rate_idx=%d \n",rc_sta_info->rx_rate_idx);
	rc_sta_info->rssi_count++;

	if (rc_sta_info->min_rssi > rssi)
		rc_sta_info->min_rssi = rssi;

	if (rc_sta_info->max_rssi < rssi)
		rc_sta_info->max_rssi = rssi;

}


struct rate_control_ops mac80211_ratectrl =
{
.name = "rc_hmac", 
.hmac_tx_status = hmac_tx_statu,					/*update one sta's rate statics*/
.get_rate = hmac_get_rate,							/*get rate for tx packet*/
.rate_init = hmac_rate_init,						/*init and reset sta rate*/
.rate_update = hmac_rate_update,					/*reset sta rate*/
.alloc_sta = hmac_alloc_sta,						/*alloc hmac rate control resource*/
.alloc = hmac_alloc,								/*alloc hw global rate control, link rate control ops*/
.free_sta = hmac_free_sta, 
.free = hmac_free, 
.rx_status = hmac_rx_status, 
};


int rc80211_hmac_init(void)
{
	atbm_printk_rc("mac80211 rate control hmac init\n");
	return ieee80211_rate_control_register(&mac80211_ratectrl);
}


void rc80211_hmac_exit(void)
{
	ieee80211_rate_control_unregister(&mac80211_ratectrl);
}


//#endif
