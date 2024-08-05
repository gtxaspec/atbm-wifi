/*
 * Copyright 
 */
#ifndef RC80211_RATE_CONTROL

#define RC80211_RATE_CONTROL


#define RATE_CONTROL_STATIC_DEBUG 0


struct cfg80211_bss;

#if 0
struct rate_control_ops {
	//struct module *module;
	void (*init)();
	void (*deinit)();
	void *(*alloc_sta)();
	void (*free_sta)(void *priv_sta);
	void (*rate_init)(struct cfg80211_rate  *sta, void *priv_sta);
	void (*tx_status)(struct cfg80211_rate  *sta, void *priv_sta,
			  struct txrate_status  * txstatus);
	void (*get_rate)(struct cfg80211_rate  *sta, void *priv_sta,
			  struct atbmwifi_ieee80211_tx_info *txrc);

};
#endif

struct hmac_rc_priv {
	struct ieee80211_hw *hw;
	bool has_mrr;
	unsigned int cw_min;
	unsigned int cw_max;
	unsigned int max_retry;
	unsigned int ewma_level;
	unsigned int segment_size;
	unsigned int update_interval;
	unsigned int lookaround_rate;
	unsigned int lookaround_rate_mrr;

#ifdef CONFIG_MAC80211_ATBM_DEBUGFS
	/*
	 * enable fixed rate processing per RC
	 *   - write static index to debugfs:ieee80211/phyX/rc/fixed_rate_idx
	 *   - write -1 to enable RC processing again
	 *   - setting will be applied on next update
	 */
	u32 fixed_rate_idx;
	struct dentry *dbg_fixed_rate;
#endif

};



extern struct rate_control_ops mac80211_ratectrl;












#define HMAC_SET_RATES_USED 			BIT(3)   /*if not set,  only maxrate&maxrate retry send to lmac*/





#define RATE_CONTROL_RATE_NUM 36
#define D11_NUM_ACIS			4	/* number of ACI based seq Queues*/


/*user set*/
#define MAX_THROUGHPUT_PRIORITY   1

#if MAX_THROUGHPUT_PRIORITY
#define MAX_SUCCESS_RATE_PRIORITY 0  /*depends MAX_THROUGHPUT_PRIORITY set */
#else
#define MAX_SUCCESS_RATE_PRIORITY 1  /*depends MAX_THROUGHPUT_PRIORITY set*/
#endif




#define MAX_STATICS_PERIOD         400    /*typically set 200ms, 500ms, 800ms */
#define MIN_TX_THR_PER_PERIOD  (MAX_STATICS_PERIOD/20)   /*less than this number packets txed, the result will not be accurate, 20ms  one packet*/


#if MAX_THROUGHPUT_PRIORITY
#define SUCCESS_PROB_WEIGHT_ALPHA  20     /*Add 20% weight for success probability, the value may be adjust from 10->30 */
#else
#define SUCCESS_PROB_WEIGHT_ALPHA  30    /*success probability have more wight*/
#endif


#define UNUSED_RATE_REDUCE_ALPHA  0 


#define BANDWIDTH_MORE_20M 1  //set more likely select 20M bandwidth.

/*
 upgade rate every period.
*/


/*internal params*/

/*max rates number send to LMAC, typically set 3 rates as limit,  
  optionally value: 2,3,4. available rates number aways <= MAX_RATE_NUMBER
   in the priority of [ highest rate, lowest rate,  max success prob rate, down rate.]
  */
#define MAX_RATE_NUMBER 4  
#define DEFAULT_RATE_TRANS  2

#define WSM_MAX_NUM_LNK_AP           4
#define RATE_CONTROL_UPDATE_PERIOD 200 

#define SAMPLE_RATE_BAD_SUCC_PROB_THRESH  40  //40%*255 success rate

#define SAMPLE_RATE_DOWN_SUCC_PROB_THRESH  70   //70% success rate, add down sample chance.
#define SAMPLE_RATE_UP_SUCC_PROB_THRESH  40     //40% success rate, auto down, add up sample chance.

#define SAMPLE_RATE_IDEAL_SUCC_PROB_THRESH  90  //90% success rate

#define SAMPLE_DCM_PROB_THRESH  50  //50% success rate


#define SND_IDLE_SET_20M_THRESH  20  //big value more likely to set 20M 

#define DEFAULT_AMPDU_PACKETS  50  // 6 //8

#define MCS_GROUP_RATES	8
//#ifndef RATE_INDEX_MAX
#define RATE_DESC_INDEX_MAX 36  //lmac rate id, no pbcc22 pbcc23. 36 rates
//#endif
#if 0
struct txrate_status {
	u16 txfail[RATE_INDEX_MAX];
	u16 txcnt[RATE_INDEX_MAX];
};
#endif


/*
 At most MAX_RATE_NUMBER of rate index will be used.
the following rate index may overlap.
max_tp_rate_index >= max_suc_rate_index >= down_rate_index >= lowest_rate_index
 
*/
typedef struct RATE_SET_STRUCT
{
	s8 max_tp_rate_index;   /*Max through put rate index, may be the highest rate index*/
	s8 max_tp_rate_try;    
	s8 max_suc_rate_index;  /*Max success possibility rate index,  may be the highest rate index or the lowest rate*/
	s8 max_suc_rate_try;
	s8 down_rate_index;     /*If the first rate failed, will try this rate, 
	                             should be a different rate index between highest and lowest rate if exist, may be equal to max success rate */
	s8 down_rate_try;
	s8 lowest_rate_index;   /*if all above rates failed, will try this rate*/
	s8 lowest_rate_try;
    s8 rate_id_num_limit;
    s8 rate_mode;
}RATE_SET_T;




typedef struct RATE_CONTROL_RATE_S
{

	u16 all_rate_transmit_cnt;    /*all tx packet counter in one period*/
	unsigned long  last_update_time;	// unit is 2^16us

	u16 success_cnt[RATE_CONTROL_RATE_NUM]; // in the period(RATE_CONTROL_UPDATE_PERIOD) sunccess count;
	u16 transmit_cnt[RATE_CONTROL_RATE_NUM]; //in the period(RATE_CONTROL_UPDATE_PERIOD) all transmit count;
	u32 totle_success_cnt[RATE_CONTROL_RATE_NUM]; // all success count *256
	u32 totle_transmit_cnt[RATE_CONTROL_RATE_NUM]; // all transmit count *256

	u8 current_rate_retry_num[D11_NUM_ACIS]; 
	u8 current_rate_idx[D11_NUM_ACIS]; // current ampdu or mpdu use rate index;


u8  sucess_probs[RATE_CONTROL_RATE_NUM]; // sucess probability *256

	u8  sucess_prob[RATE_CONTROL_RATE_NUM]; // sucess probability *256
	u8  totle_sucess_prob[RATE_CONTROL_RATE_NUM]; // sucess probability *256
	u32 rate_tp[RATE_CONTROL_RATE_NUM];


	u8  sucess_prob_miss_flag[RATE_CONTROL_RATE_NUM]; //rate was not continually used, lastest rate static have more weight.


    RATE_SET_T *rate_set;
	//int8_t   cfo;
	u8 max_tp_rate_idx;  // max throughput rate index;	
	u8 max_success_rate_idx;  // max throughput rate index;
	u8 rx_rate_idx;
	u8 packets_cnt;	

	u8 max_tp_rate_idx_history;  // max throughput rate index inlcude histroy statics;	


	u8 last_max_tp_rate_idx;
	u8 fast_sample_flag;

	u8 b_he_sta:1,
       b_ht_sta:1,
	   b_valid:1,
	   b_noht_rate:1;	

    
	u8 sample_tp_rate_idx;			    
	u8 sample_packets_cnt;
    
	u8 continue_ampdu_cnt;//[RATE_CONTROL_RATE_NUM];  
	u8 ampdu_retry_num;//[D11_NUM_ACIS];
	
    u8 ampdu_rate_can_change_falg;  /*after ampdu number packet, can rate index be changed.*/

	u8 bw_40M_flag;
	s8 bw_20M_cnt;//>0: use 20M bw; <=0:use 40M bw
	u8 max_prob;

	
//#ifdef  MINSTREL_RSSI_USED
		int mean_rssi; //add rx rssi 
		int max_rssi;
		int min_rssi;
		int total_rssi; //add rx rssi 
		int rssi_count;
		//	int table_flag;
		//	int table_count;
//#endif
    u16 main_snd_idle_ratio;
    u16 snd_idle_ratio;
	u16 main_idle_ratio;

	u16 last_period_pkt_cnt;
	u16 bw_total_pkt_cnt;
    unsigned long bw_total_pkt_update_time;

	u16 flag;	
	u16 rssi_sum;
	u16 rate_sum;
	u16 receive_data_num;
	s8 rssi;	
	u8 rssi_cnt;
	u8 rate_mean;
	
	//	uint8_t receive_nodata_num;		
	u8 is_ap_flag;
	u8 MacAddr[6];
	u8 reserve_lmac_rate[2];
	
	u8 n_signal_loss_flag;   //signal be very bad, txed ==0 or success ratio less than 20%
    u8 not_sample_cnt;
	u8 b_mode_sampled;   /*first time sample b mode*/

	u8 resample_required;
	u8 max_dcm_rate_idx;
	
	u32 tx_rc_flags;

	u8 max_he_index;
	u8 max_ht_index;
	u8 max_g_index;
    u8 max_b_index;
	u32 center_freq;


    /* statics for debug*/
    u32 total_throughput;   /*calculate base fixed time or packets*/
    u32 average_throughput;
    u32 peak_throughput;
    u32 lowest_throughput;   

}RATE_CONTROL_RATE;

struct txrate_status {
	u16 txfail[RATE_DESC_INDEX_MAX];
	u16 txcnt[RATE_DESC_INDEX_MAX];
};

#if 0
struct rate_control_ops {
	void (*init)();
	void (*deinit)();
	void *(*alloc_sta)(void);
	void (*free_sta)(void *priv_sta);
	void (*rate_init)(struct cfg80211_rate  *sta, void *priv_sta);
	void (*tx_status)(struct cfg80211_rate  *sta, void *priv_sta, struct txrate_status  * txstatus);
	void (*get_rate)(struct cfg80211_rate  *sta, void *priv_sta,struct atbmwifi_ieee80211_tx_info *txrc);

};
#endif

/*------------------*/
#define RATE_IINDEX_11N_INDEX			22
#define RATE_INDEX_B_1M           		0
#define RATE_INDEX_B_2M           		1
#define RATE_INDEX_B_5_5M         		2
#define RATE_INDEX_B_11M          		3
#define RATE_INDEX_PBCC_22M       		4     // not supported/unused
#define RATE_INDEX_PBCC_33M       		5     // not supported/unused
#define RATE_INDEX_A_6M           		6
#define RATE_INDEX_A_9M           		7
#define RATE_INDEX_A_12M          		8
#define RATE_INDEX_A_18M          		9
#define RATE_INDEX_A_24M          		10
#define RATE_INDEX_A_36M          		11
#define RATE_INDEX_A_48M          		12
#define RATE_INDEX_A_54M          		13
#define RATE_INDEX_N_6_5M         		14
#define RATE_INDEX_N_13M          		15
#define RATE_INDEX_N_19_5M        		16
#define RATE_INDEX_N_26M          		17
#define RATE_INDEX_N_39M          		18
#define RATE_INDEX_N_52M          		19
#define RATE_INDEX_N_58_5M        		20
#define RATE_INDEX_N_65M          		21
#define RATE_INDEX_N_MCS32_6M     		22
#define RATE_INDEX_HE_MCSER0			23
#define RATE_INDEX_HE_MCSER1			24
#define RATE_INDEX_HE_MCSER2            25
#define RATE_INDEX_HE_MCS0              26
#define RATE_INDEX_HE_MCS1              27
#define RATE_INDEX_HE_MCS2              28
#define RATE_INDEX_HE_MCS3              29
#define RATE_INDEX_HE_MCS4              30
#define RATE_INDEX_HE_MCS5              31
#define RATE_INDEX_HE_MCS6              32
#define RATE_INDEX_HE_MCS7              33
#define RATE_INDEX_HE_MCS8              34
#define RATE_INDEX_HE_MCS9              35
#define RATE_INDEX_HE_MCS10             36
#define RATE_INDEX_HE_MCS11             37

#define DESCR_RATE_INDEX_HE_MSC11       0
#define DESCR_RATE_INDEX_HE_MSC10       1
#define DESCR_RATE_INDEX_HE_MSC9        2
#define DESCR_RATE_INDEX_HE_MSC8        3
#define DESCR_RATE_INDEX_HE_MSC7        4
#define DESCR_RATE_INDEX_HE_MSC6        5
#define DESCR_RATE_INDEX_HE_MSC5        6
#define DESCR_RATE_INDEX_HE_MSC4        7
#define DESCR_RATE_INDEX_HE_MSC3        8
#define DESCR_RATE_INDEX_HE_MSC2        9
#define DESCR_RATE_INDEX_HE_MSC1        10
#define DESCR_RATE_INDEX_HE_MSC0        11
#define DESCR_RATE_INDEX_HE_MSCER2      12
#define DESCR_RATE_INDEX_HE_MSCER1      13
#define DESCR_RATE_INDEX_HE_MSCER0      14

#define DESCR_RATE_INDEX_HT_MCS7        15
#define DESCR_RATE_INDEX_HT_MCS6        16
#define DESCR_RATE_INDEX_HT_MCS5        17
#define DESCR_RATE_INDEX_HT_MCS4        18
#define DESCR_RATE_INDEX_HT_MCS3        19
#define DESCR_RATE_INDEX_HT_MCS2        20
#define DESCR_RATE_INDEX_HT_MCS1        21
#define DESCR_RATE_INDEX_HT_MCS0        22
#define DESCR_RATE_INDEX_HT_MCS32       23


#define DESCR_RATE_INDEX_G_54M        	24
#define DESCR_RATE_INDEX_G_48M        	25
#define DESCR_RATE_INDEX_G_36M        	26
#define DESCR_RATE_INDEX_G_24M        	27
#define DESCR_RATE_INDEX_G_18M        	28
#define DESCR_RATE_INDEX_G_12M        	29
#define DESCR_RATE_INDEX_G_9M           30
#define DESCR_RATE_INDEX_G_6M           31

#define DESCR_RATE_INDEX_B_11M          32
#define DESCR_RATE_INDEX_B_5_5M         33
#define DESCR_RATE_INDEX_B_2M           34
#define DESCR_RATE_INDEX_B_1M           35

#define MAX_TRANS_NUMBER_RATE 15   //at most set 15, 4bit value.


#define HE_SHORT_GI_START_RATE RATE_INDEX_HE_MCS3


/*
input:  LMAC Tx rate Setting  
output: tx count and failed count of each packet.
*/
typedef struct packet_desc_struct
{
	/*input params*/
	unsigned int  packet_id;
	s8 rate_table_id;   
	unsigned int  rate_policy[5];

	s8 highest_rateid;
	s8 lowest_rateid;
	s8 data_rate_retry_cnt_stop_en;

	/*output statics*/
	s8 txed_cnt[RATE_CONTROL_RATE_NUM];
	s8 succeed_rate_id;
}PACKET_DESC_S;

u32 rate_info_to32bit(RATE_SET_T *prate_set);

//extern struct txrate_status lmac_rate_status;



#endif /* RC80211_PID_H */
