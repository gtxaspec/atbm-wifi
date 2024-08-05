#ifndef __INTERNAL_CMD__
#define __INTERNAL_CMD__
#include <linux/hash.h>
#include "mac80211/ieee80211_i.h"
#include "mac80211/atbm_common.h"
#include "wsm.h"
bool atbm_internal_cmd_scan_triger(struct ieee80211_sub_if_data *sdata,struct ieee80211_internal_scan_request *req);
bool atbm_internal_cmd_stainfo(struct ieee80211_local *local,struct ieee80211_internal_sta_req *sta_req);
bool atbm_internal_cmd_monitor_req(struct ieee80211_sub_if_data *sdata,struct ieee80211_internal_monitor_req *monitor_req);
bool atbm_internal_cmd_stop_monitor(struct ieee80211_sub_if_data *sdata);
bool atbm_internal_wsm_adaptive(struct atbm_common *hw_priv,struct ieee80211_internal_wsm_adaptive *adaptive);
bool atbm_internal_wsm_txpwr_dcxo(struct atbm_common *hw_priv,struct ieee80211_internal_wsm_txpwr_dcxo *txpwr_dcxo);
bool atbm_internal_wsm_txpwr(struct atbm_common *hw_priv,struct ieee80211_internal_wsm_txpwr *txpwr);
bool atbm_internal_freq_set(struct ieee80211_hw *hw,struct ieee80211_internal_set_freq_req *req);
bool atbm_internal_cmd_scan_build(struct ieee80211_local *local,struct ieee80211_internal_scan_request *req,
											   u8* channels,int n_channels,struct cfg80211_ssid *ssids,int n_ssids,
											   struct ieee80211_internal_mac *macs,int n_macs);
bool atbm_internal_channel_auto_select_results(struct ieee80211_sub_if_data *sdata,
												struct ieee80211_internal_channel_auto_select_results *results);
bool atbm_internal_channel_auto_select(struct ieee80211_sub_if_data *sdata,
													  struct ieee80211_internal_channel_auto_select_req *req);
bool atbm_internal_request_chip_cap(struct ieee80211_hw *hw,struct ieee80211_internal_req_chip *req);
#ifdef CONFIG_ATBM_SUPPORT_AP_CONFIG
bool atbm_internal_update_ap_conf(struct ieee80211_sub_if_data *sdata,struct ieee80211_internal_ap_conf *conf_req,bool clear);
#endif
bool atbm_internal_wsm_set_rate(struct atbm_common *hw_priv,struct ieee80211_internal_rate_req *req);
int atbm_internal_wsm_set_rate_power(struct atbm_common *hw_priv,struct ieee80211_internal_rate_power_req *req);
#ifdef CONFIG_ATBM_MONITOR_SPECIAL_MAC
bool atbm_internal_mac_monitor(struct ieee80211_hw *hw,struct ieee80211_internal_mac_monitor *monitor);
#endif
int set_target_power(int mode,int rateIndex,int bw,int power);

bool atbm_internal_cmd_req_iftype(struct ieee80211_sub_if_data *sdata,struct ieee80211_internal_iftype_req *req);
#define ATBM_GPIO_CONFIG__FUNCTION_CONFIGD		BIT(0)
#define ATBM_GPIO_CONFIG__INPUT					BIT(1)
#define ATBM_GPIO_CONFIG__OUTPUT				BIT(2)
#define ATBM_GPIO_CONFIG__PUP					BIT(3)
#define ATBM_GPIO_CONFIG__PUD					BIT(4)



struct atbm_ctr_addr{
	unsigned int base_addr;
	unsigned int val;
	char		 start_bit;
	char		 width;
};
struct atbm_gpio_config {
	unsigned int gpio;
	unsigned int flags;
	struct atbm_ctr_addr	fun_ctrl;
	struct atbm_ctr_addr	pup_ctrl;
	struct atbm_ctr_addr	pdu_ctrl;
	struct atbm_ctr_addr	dir_ctrl;
	struct atbm_ctr_addr	out_val;
	struct atbm_ctr_addr	in_val;
};
bool atbm_internal_gpio_config(struct atbm_common *hw_priv,int gpio,bool dir ,bool pu,bool default_val);
bool atbm_internal_gpio_output(struct atbm_common *hw_priv,int gpio,bool set);
bool atbm_internal_gpio_input(struct atbm_common *hw_priv,int gpio,bool *set);
bool atbm_internal_edca_update(struct ieee80211_sub_if_data *sdata,int queue,int aifs,int cw_win,int cw_max,int txop);


int DCXOCodeWrite(struct atbm_common *hw_priv,int data);
u8 DCXOCodeRead(struct atbm_common *hw_priv);
int atbm_internal_recv_6441_vendor_ie(struct atbm_vendor_cfg_ie *recv_ie);
struct atbm_vendor_cfg_ie * atbm_internal_get_6441_vendor_ie(void);
int country_chan_found(const char *country);
int atbm_set_country_code_on_driver(struct ieee80211_local *local,char *country);

unsigned int atbm_get_rate_from_rateid(unsigned char maxrate_id,unsigned char dcm_used,unsigned char support_40M_symbol);


/*
0_0000000 �?0x00	BPSK modulation, coding rate 1/2
0_0000001 �?0x01	QPSK modulation, coding rate 1/2
0_0000010 �?0x02	QPSK modulation, coding rate 3/4
0_0000011 �?0x03	16QAM modulation, coding rate 1/2
0_0000100 �?0x04	16QAM modulation, coding rate 3/4
0_0000101 �?0x05	64QAM modulation, coding rate 2/3
0_0000110 �?0x06	64QAM modulation, coding rate 3/4
0_0000111 �?0x07	64QAM modulation, coding rate 5/6
0_0001000 �?0x08	256QAM modulation, coding rate 3/4
0_0001001 �?0x09	256QAM modulation, coding rate 5/6
0_0001010 �?0x0a	1024QAM modulation, coding rate 3/4
0_0001011 �?0x0b	1024QAM modulation, coding rate 5/6

1001: 802.11ax HE MU PPDU
1000: 802.11ax HE TB PPDU
0111: 802.11ax HE ER SU PPDU
0110: 802.11ax HE SU PPDU
0101: 802.11n OFDM long (mixed-mode) preamble
0100: 802.11n OFDM short (greenfield-mode) preamble
0010: 802.11a OFDM preamble
0001: 802.11b DSSS long preamble
0000: 802.11b DSSS short preamble (note this is not supported for 

*/
typedef enum
{
    // 802.11 b
    _1Mbps_DSSS_                = 0x00,
    _2Mbps_DSSS_                = 0x01,
    _5_5Mbps_CCK_               = 0x02,
    _11Mbps_CCK_                = 0x03,

    // 802.11a Legacy modes
    _6Mbps_BPSK_Code1_2_        = 0x0B,
    _9Mbps_BPSK_Code3_4_        = 0x0F,
    _12Mbps_QPSK_Code1_2_       = 0x0A,
    _18Mbps_QPSK_Code3_4_       = 0x0E,
    _24Mbps_16QAM_Code1_2_      = 0x09,
    _36Mbps_16QAM_Code3_4_      = 0x0D,
    _48Mbps_64QAM_Code2_3_      = 0x08,
    _54Mbps_64QAM_Code3_4_      = 0x0C,

    // 802.11n Mixed/Greenfield Mode
    _6_5Mbps_BPSK_Code_1_2_     = 0x00,
    _13_5Mbps_QPSK_Code_1_2_    = 0x01,
    _19_5Mbps_QPSK_Code_3_4_    = 0x02,
    _26_Mbps_16QAM_Code_1_2_    = 0x03,
    _39_Mbps_16QAM_Code_3_4_    = 0x04,
    _52_Mbps_64QAM_Code_2_3_    = 0x05,
    _58_5Mbps_64QAM_Code_3_4_   = 0x06,
    _65_Mbps_64QAM_Code_5_6_    = 0x07,

	_xx_Mbps_256QAM_Code_3_4_ = 0x08,
	_xx_Mbps_256QAM_Code_5_6_ = 0x09,
	_xx_Mbps_1024QAM_Code_3_4_ = 0x0a,
	_xx_Mbps_1024QAM_Code_5_6_ = 0x0b,
	
	_6Mbps_MCS32_BPSK_Code1_2_	= 0x20

}WLAN_RATE_T;

typedef enum
{
    ATBM_WIFI_MODE_OFDM = 0,
    ATBM_WIFI_MODE_DSSS,

    ATBM_WIFI_MODE_MAX
}ATBMWiFiMode_e;


typedef enum
{
    ATBM_WIFI_BW_20M = 0,
    ATBM_WIFI_BW_40M,
    ATBM_WIFI_BW_20U,
    ATBM_WIFI_BW_20L,
    
    ATBM_WIFI_BW_RU242 = 2,
    ATBM_WIFI_BW_RU106 = 3,
    
    ATBM_WIFI_BW_MAX
}ATBMWiFiBandWidth_e;

typedef enum
{
    ATBM_WIFI_CH_OFFSET_0 = 0,
    ATBM_WIFI_CH_OFFSET_10U,
    ATBM_WIFI_CH_OFFSET_10L,
    
    ATBM_WIFI_CH_OFFSET_MAX
}ATBMWiFiChanOff_e;


typedef enum
{
    ATBM_WIFI_OFDM_MD_LM = 0,
    ATBM_WIFI_OFDM_MD_MM = 1,
    ATBM_WIFI_OFDM_MD_GF = 200,//not support in Cronus
    ATBM_WIFI_OFDM_MD_HE_SU = 2,
    ATBM_WIFI_OFDM_MD_HE_ER_SU = 3,
    ATBM_WIFI_OFDM_MD_HE_TB = 4,
    ATBM_WIFI_OFDM_MD_HE_MU = 5,
    ATBM_WIFI_OFDM_MD_MAX
}ATBMWiFiOFDMMode_e;

#define OFDM_MD_IS_HE(_OFDM_Mode) ((ATBM_WIFI_OFDM_MD_HE_SU == _OFDM_Mode)||(ATBM_WIFI_OFDM_MD_HE_ER_SU == _OFDM_Mode)||(ATBM_WIFI_OFDM_MD_HE_TB == _OFDM_Mode)||(ATBM_WIFI_OFDM_MD_HE_MU == _OFDM_Mode))

typedef enum
{
	/*DSSS*/
	ATBM_WIFI_RATE_1M = 0,
	ATBM_WIFI_RATE_2M= 0x01,
	ATBM_WIFI_RATE_5D5M = 0x02,
	ATBM_WIFI_RATE_11M= 0x03,

	/*OFDM: LM*/
/*
	ATBM_WIFI_RATE_6M = 6,//0x0B,
	ATBM_WIFI_RATE_9M = 9,//0x0F,
	ATBM_WIFI_RATE_12M= 12,//0x0A,
	ATBM_WIFI_RATE_18M= 18,//0x0E,
	ATBM_WIFI_RATE_24M= 24,//0x09,
	ATBM_WIFI_RATE_36M= 36,//0x0D,
	ATBM_WIFI_RATE_48M= 48,//0x08,
	ATBM_WIFI_RATE_54M= 54,//0x0C,
*/

	//OFDM: LM
	ATBM_WIFI_RATE_6M = 0,
	ATBM_WIFI_RATE_9M,
	ATBM_WIFI_RATE_12M,
	ATBM_WIFI_RATE_18M,
	ATBM_WIFI_RATE_24M,
	ATBM_WIFI_RATE_36M,
	ATBM_WIFI_RATE_48M,
	ATBM_WIFI_RATE_54M,
	
	/*OFDM: MM/GF */
	ATBM_WIFI_RATE_MCS0 = 0x00,
	ATBM_WIFI_RATE_MCS1 = 0x01,
	ATBM_WIFI_RATE_MCS2 = 0x02,
	ATBM_WIFI_RATE_MCS3 = 0x03,
	ATBM_WIFI_RATE_MCS4 = 0x04,
	ATBM_WIFI_RATE_MCS5 = 0x05,
	ATBM_WIFI_RATE_MCS6 = 0x06,
	ATBM_WIFI_RATE_MCS7 = 0x07,

/* Transmit Mode �?802.11ax HE Mode
0_0000000 �?0x00  BPSK modulation, coding rate 1/2        MCS0
0_0000001 �?0x01  QPSK modulation, coding rate 1/2        MCS1
0_0000010 �?0x02  QPSK modulation, coding rate 3/4        MCS2
0_0000011 �?0x03  16QAM modulation, coding rate 1/2       MCS3
0_0000100 �?0x04  16QAM modulation, coding rate 3/4       MCS4
0_0000101 �?0x05  64QAM modulation, coding rate 2/3       MCS5
0_0000110 �?0x06  64QAM modulation, coding rate 3/4       MCS6
0_0000111 �?0x07  64QAM modulation, coding rate 5/6       MCS7
0_0001000 �?0x08  256QAM modulation, coding rate 3/4      MCS8
0_0001001 �?0x09  256QAM modulation, coding rate 5/6      MCS9
0_0001010 �?0x0a  1024QAM modulation, coding rate 3/4     MCS10
0_0001011 �?0x0b  1024QAM modulation, coding rate 5/6     MCS11

*/
	ATBM_WIFI_RATE_MCS8 = 0x08,
	ATBM_WIFI_RATE_MCS9 = 0x09,
	ATBM_WIFI_RATE_MCS10 = 0x0a,
	ATBM_WIFI_RATE_MCS11 = 0x0b,
	
	ATBM_WIFI_RATE_MCS32 = 0x20,

    
	ATBM_WIFI_RATE_MAX = 0xff,
}ATBMWiFiRate_e;
/*
typedef enum
{
	//DSSS
	ATBM_WIFI_RATE_1M = 0,
	ATBM_WIFI_RATE_2M,
	ATBM_WIFI_RATE_5D5M,
    ATBM_WIFI_RATE_11M,

	//OFDM: LM
	ATBM_WIFI_RATE_6M = 0,
	ATBM_WIFI_RATE_9M,
	ATBM_WIFI_RATE_12M,
	ATBM_WIFI_RATE_18M,
	ATBM_WIFI_RATE_24M,
	ATBM_WIFI_RATE_36M,
	ATBM_WIFI_RATE_48M,
	ATBM_WIFI_RATE_54M,

	//OFDM: MM/GF 
	ATBM_WIFI_RATE_MCS0 = 0,
	ATBM_WIFI_RATE_MCS1,
	ATBM_WIFI_RATE_MCS2,
	ATBM_WIFI_RATE_MCS3,
	ATBM_WIFI_RATE_MCS4,
	ATBM_WIFI_RATE_MCS5,
	ATBM_WIFI_RATE_MCS6,
	ATBM_WIFI_RATE_MCS7,
	ATBM_WIFI_RATE_MCS32,
  
    ATBM_WIFI_RATE_MAX
}ATBMWiFiRate_e;
*/

typedef enum
{
    ATBM_WIFI_GI_MD_NORMAL = 0,
    ATBM_WIFI_GI_MD_SHORT,

    ATBM_WIFI_GILTF_0P8_1X = 0,
    ATBM_WIFI_GILTF_1P6_1X,
    ATBM_WIFI_GILTF_0P8_2X,
    ATBM_WIFI_GILTF_1P6_2X,
    ATBM_WIFI_GILTF_0P8_4X,
    ATBM_WIFI_GILTF_3P2_4X,
    
    ATBM_WIFI_GI_MD_MAX
}ATBMWiFiGIMode_e;

typedef enum
{
    ATBM_WIFI_PREAMBLE_LONG = 0,
    ATBM_WIFI_PREAMBLE_SHORT,
    
    ATBM_WIFI_PREAMBLE_MAX
}ATBMWiFiPreamble_e;

typedef enum
{
    ATBM_WIFI_TX_MODE_SINGLE = 0,
    ATBM_WIFI_TX_MODE_CONTINUE,
    
    ATBM_WIFI_TX_MODE_MAX
}ATBMWiFiTxMode_e;

/*parameters for PHY TX on UI*/
typedef struct
{
    unsigned int FreqMHz;
    unsigned int ChannelNum; /* 1~14 for standard channel, -1 means nonstandard channel and FreqMHz indicates frequency of the channel */
    unsigned int WiFiMode;
	unsigned int OFDMMode;
    unsigned int BW;    
    unsigned int ChOffset;
    unsigned int Rate;
    unsigned int GIMode;
	unsigned int PreambleMode;


	unsigned int PSDULen; /*PSDU length in byte*/
	unsigned int PacketInterval; /*packet interval in us*/
	unsigned int DigitalScaler;

	unsigned int PacketNum; /*packet number*/
	unsigned int DataRateMbps;
	
    unsigned int TxMode;
    unsigned int InfiniteLongPacket;//true: INF=1, send only one packet which is infinite long
    
    /*802.11ax params*/
    unsigned int MPDUNum;//the MPDU number of one AMPDU
    unsigned int MPDULen;//the length of MPDU, be multiple of 4

    
    unsigned int Smoothing;//
    unsigned int Sounding;//
    unsigned int Aggregation;//This field is used to indicate if the PSDU contains an A-MPDU
    unsigned int STBC;//0 or 1
    unsigned int LTFNum;// 1 LTF symbols; 2 LTF symbols; 4 LTF symbols; 6 LTF symbols; 8 LTF symbols;
    unsigned int BeamFormed;//
    unsigned int Doppler;//0 or 1
	unsigned int BurstLen; 
	unsigned int TxopDuration;
    unsigned int NoSigExtn;
    unsigned int ServiceField;//802.11b, tx vector0 [7:0]. This field specifies the service information transmitted in the frame header. 


    unsigned int TxPower;
    unsigned int TxStreams;
    unsigned int TxAntennas;
    //unsigned int Vector1TxMode; //This field is used to indicate which of the supported transmit modes is selected and which preamble to use.
                                //depending on "WiFiMode" and "OFDMMode" 
    unsigned int TxAbsPower;
    unsigned int TxPowerModeSel;


    unsigned int StartingStsNum;//The sum of the number of space-time streams assigned to other users with lower space-time stream indices than this user.
    unsigned int HELTFMode;//0: HE single stream pilot HE-LTF mode; 1: HE masked HE-LTF sequence mode
    unsigned int HESigA2Reserved;//Reserved field setting for HE-SIG-A2 of HE TB.
    unsigned int SpatialReuse1,SpatialReuse2,SpatialReuse3,SpatialReuse4;

    unsigned int TriggerResponding;
    unsigned int TriggerMethod;
    unsigned int BSSColor;
    unsigned int UplinkFlag;
    unsigned int ScramblerValueEn;//Scrambler Initial Value enable
    unsigned int ScramblerValue;//Scrambler Initial Value
    unsigned int LdpcExtrSysm;//Indicates the presence of the LDPC extra symbol segment in an HE TB PPDU(0: not present ; 1:  present)

    unsigned int ReservedForMAC;//Used for MAC rate down algorithms. Another setting of Control4[15:0]

    unsigned int  CFO;//This field is a measure of the carrier frequency offset estimated during the reception of the frame. The format is signed S(13,20)
    unsigned int  PPM;//This field is a measure of the ppm observed at the antenna during the reception of the current data frame. The format is signed S(14,25)

    
    unsigned int DCM;//Indicates whether Dual Carrier Modulation is used for the Data field in HE PPDU
    unsigned int Coding;//OFDM coding (0: Convolutional coding, BCC?    1: Advanced (LDPC) coding)
    unsigned int Padding;//NomPacketPadding, 0: 0us, 1: 8us,2: 16us
/* AFactor 
HE-SIG-A2 subfield of HE-SIG-A field, B13–B14, Pre-FEC Padding Factor
Set to 0 to indicate a pre-FEC padding factor of 4.
Set to 1 to indicate a pre-FEC padding factor of 1.
Set to 2 to indicate a pre-FEC padding factor of 2.
Set to 3 to indicate a pre-FEC padding factor of 3.
*/
    unsigned int AFactor;//A-factor or pre-FEC padding factor of 802.11ax trigger-based PPDU
    //unsigned int PacketExtDura;//Packet Extension Duration, 0us,4us,8,us,12us,16us
    unsigned int PEDisambiguity; //PE disambiguity(0 or 1)
    unsigned int MidamblePeriod;//Midamble Period (no midamble, 1: period = 10 symbols, 2: period = 20 symbols)
    unsigned int BeamChange;//0 or 1
	unsigned int RuAllocation; /*params_cont_tx_control_vectore4, [15:8] ,Indicate the RU allocated for current transmission in HE TB PPDU*/
    
    
    //RF BW paramter
    unsigned int DefaultRFBW;
	unsigned int StationID0;
	//20M precompensation sel
	int precompensation;
}ETF_PHY_TX_PARAM_T;

typedef ETF_PHY_TX_PARAM_T ETF_PHY_RX_PARAM_T;


typedef struct
{
	unsigned short          MsgLen;
	unsigned short          MsgId;
	ETF_PHY_TX_PARAM_T		TxConfig;
}ETF_HE_TX_CONFIG_REQ;

typedef struct
{
	unsigned short          MsgLen;
	unsigned short          MsgId;
	ETF_PHY_RX_PARAM_T		RxConfig;
}ETF_HE_RX_CONFIG_REQ;

/*
Register Name	                    Base	Width	Abs addr		
params_nt_contf	                    0080	31:0	ACD0080
params_cont_tx_rate	                0084	7:0	    ACD0084
params_cont_tx_length	            0088	18:0	ACD0088
params_cont_tx_control_vectore0	    008c	31:0	ACD008C
params_cont_tx_control_vectore1	    0090	31:0	ACD0090
params_cont_tx_control_vectore2	    0094	31:0	ACD0094
params_cont_tx_control_vectore3	    0098	31:0	ACD0098
params_cont_tx_control_vectore4	    009c	31:0	ACD009C
params_cont_tx_control_vectore5	    0100	31:0	ACD0100
//params_cont_tx_ampdu_num	        0104	16:0	ACD0104
params_cont_ifs_time    	        0104	20:0	ACD0104
params_cont_tx_mpdu_len	            0108	13:0	ACD0108
*/
#define  WIFI_AX_REG_NT_CONFIG          0xACD0080
#define  WIFI_AX_REG_TX_RATE            0xACD0084
#define  WIFI_AX_REG_TX_LEN             0xACD0088
#define  WIFI_AX_REG_TX_VECTOR0         0xACD008C
#define  WIFI_AX_REG_TX_VECTOR1         0xACD0090
#define  WIFI_AX_REG_TX_VECTOR2         0xACD0094
#define  WIFI_AX_REG_TX_VECTOR3         0xACD0098
#define  WIFI_AX_REG_TX_VECTOR4         0xACD009C
#define  WIFI_AX_REG_TX_VECTOR5         0xACD0100
//#define  WIFI_AX_REG_TX_MPDU_NUM        0xACD0104 //for AMPDU
#define  WIFI_AX_REG_TX_IFS_TIME        0xACD0104 
#define  WIFI_AX_REG_TX_MPDU_LEN        0xACD0108 //for AMPDU
#define  WIFI_AX_REG_SCRAMBLE_SEED       0xACD010C
//#define  WIFI_AX_REG_PRBS_SEED          0xACD0xxx
#define RFIP_MCS_LUT_BASE_ADDR	(0xACBE000)
int init_cfg_rate_power(void);


/* params_nt_contf
即NTCONTCONF寄存器，是主要的模式控制寄存器。包括是否是continuous模式，发送什么帧，IFS是多长等

params_nt_contf[0]          CONT        1：continuous模式    0：非continuous模式
params_nt_contf[1]          INF         1：无限帧�?   0：有限帧�?params_nt_contf[15:3]      IFS          帧之间的间隔长度，指的是phy_read下降沿到phy_enable上升沿之间的cycle�?params_nt_contf[31:22]      NFRAMES      n：n个frame
*/
typedef struct
{
    unsigned int CONT : 1;
    unsigned int INF : 1;
    unsigned int PRBSGEN : 1;
    unsigned int IFS : 13;
    unsigned int NBITS : 6;
    unsigned int NFRAMES : 10;
    
}NetConfField_t;
typedef struct
{
    union
    {
        unsigned int Reg;
        NetConfField_t Bits;
    };
}NtContf_t;


typedef WLAN_RATE_T ContTxRate_e;

typedef struct
{
    unsigned int Smoothing : 1;
    unsigned int Sounding : 1;
    unsigned int Aggregation : 1;
    unsigned int STBC : 2;
    unsigned int reserved1 : 2;//bit5~bit6
    unsigned int NumOfLTF : 3; // 0: 1 LTF symbols     1:02     3:04    5:06    7:08,     TB only, else set to 0
    unsigned int Beamformed : 1;
    unsigned int Doppler : 1;
    unsigned int BurstLength : 12;
    unsigned int TxopDuration : 7;
    unsigned int NoSigExtn : 1;
    
}TxCtlVec0Field_t;
typedef struct
{
    unsigned int ServiceField : 8;
    unsigned int reserved : 24;
    
}TxCtlVec0Field11b_t; //for 802.11b
typedef struct
{
    union
    {
        unsigned int Reg;
        TxCtlVec0Field_t Bits;
        TxCtlVec0Field11b_t Bits11b;
    };
}TxCtlVec0_t;

typedef struct
{
    unsigned int TxPower : 4;
    unsigned int reserved : 2;
    unsigned int TxStreams : 2;
    unsigned int TxAntennas : 2;
/*
 1001: 802.11ax HE MU PPDU
 1000: 802.11ax HE TB PPDU
 0111: 802.11ax HE ER SU PPDU
 0110: 802.11ax HE SU PPDU
 0101: 802.11n OFDM long (mixed-mode) preamble
 0100: 802.11n OFDM short (greenfield-mode) preamble
 0010: 802.11a OFDM preamble
 0001: 802.11b DSSS long preamble
 0000: 802.11b DSSS short preamble (note this is not supported for 1MBit/s)
 */    
    unsigned int TxMode : 4;
    unsigned int ChBW : 3;//0:20M. Valid for 802.11a/b/n modems, ER SU is 242 tone; 1:40M. Valid for 802.11a/b/n modems, ER SU is 106-tone
    unsigned int ChOffset : 3;
    unsigned int TxAbsPower : 8;
    unsigned int TxPowerModeSel : 2;
    unsigned int BeamChange : 1;
    unsigned int reserved2 : 1;//bit31
    
}TxCtlVec1Field_t;
typedef struct
{
    union
    {
        unsigned int Reg;
        TxCtlVec1Field_t Bits;
    };
}TxCtlVec1_t;


typedef struct
{
    unsigned int PEDisambiguity : 1;
    unsigned int StartingStsNum : 3;
    unsigned int HELTFMode : 1;
    unsigned int HESigA2Reserved : 9;    
    unsigned int PreFecPaddingFactor : 2;
    unsigned int SpatialReuse1 : 4;    
    unsigned int SpatialReuse2 : 4;    
    unsigned int SpatialReuse3 : 4;    
    unsigned int SpatialReuse4 : 4;    
    
}TxCtlVec2Field_t;
typedef struct
{
    union
    {
        unsigned int Reg;
        TxCtlVec2Field_t Bits;
    };
}TxCtlVec2_t;

typedef struct
{
    unsigned int TriggerResponding : 1;
    unsigned int TriggerMethod : 1;
    unsigned int NomPacketPadding : 2;
    unsigned int reserved : 1;//bit4
    unsigned int BSSColor : 6;
    unsigned int UplinkFlag : 1;
    unsigned int ScramblerInitialvalue : 7;
    unsigned int ScramblerInitialvalue_en : 1;
    unsigned int LdpcExtraSymbol : 1;
    unsigned int reserved2 : 11;//bit21~bit31
    
}TxCtlVec3Field_t;
typedef struct
{
    union
    {
        unsigned int Reg;
        TxCtlVec3Field_t Bits;
    };
}TxCtlVec3_t;

typedef struct
{
    unsigned int HELTFType : 2;
    unsigned int MidamblePeriod : 2;
    unsigned int Dcm : 1;
    unsigned int Coding : 1;
    unsigned int GI_Type : 2;
    unsigned int RUAllocation : 8;
    unsigned int ReservedForMAC : 16;    
    
}TxCtlVec4Field_t;
typedef struct
{
    union
    {
        unsigned int Reg;
        TxCtlVec4Field_t Bits;
    };
}TxCtlVec4_t;

typedef struct
{
    unsigned int CFO : 13; //This field is a measure of the carrier frequency offset estimated during the reception of the frame. The format is signed S(13,20)
    unsigned int reserved1 : 3;
    unsigned int PPM : 14; //This field is a measure of the ppm observed at the antenna during the reception of the current data frame. The format is signed S(14,25)
    unsigned int reserved2 : 2;
    
}TxCtlVec5Field_t;
typedef struct
{
    union
    {
        unsigned int Reg;
        TxCtlVec5Field_t Bits;
    };
}TxCtlVec5_t;

/*
Register Name	                    Base	Width	Abs addr		
params_nt_contf	                    0080	31:0	ACD0080
params_cont_tx_rate	                0084	7:0	    ACD0084
params_cont_tx_length	            0088	18:0	ACD0088
params_cont_tx_control_vectore0	    008c	31:0	ACD008C
params_cont_tx_control_vectore1	    0090	31:0	ACD0090
params_cont_tx_control_vectore2	    0094	31:0	ACD0094
params_cont_tx_control_vectore3	    0098	31:0	ACD0098
params_cont_tx_control_vectore4	    009c	31:0	ACD009C
params_cont_tx_control_vectore5	    0100	31:0	ACD0100
*/
typedef struct
{
    NtContf_t nt_contf;
    ContTxRate_e tx_rate;
    unsigned int tx_length;//Length of the PSDU in terms of bytes
    TxCtlVec0_t TxVector0;
    TxCtlVec1_t TxVector1;
    TxCtlVec2_t TxVector2;
    TxCtlVec3_t TxVector3;
    TxCtlVec4_t TxVector4;
    TxCtlVec5_t TxVector5;
    //unsigned int MPDUNum; //for AMPDU
    unsigned int TxIfsTime;//WIFI_AX_REG_TX_IFS_TIME
    unsigned int MPDULen; //for AMPDU
    
    unsigned int ScrambleSeed;
	int precom;//20M precompensation sel
}ContTxParam_t;


typedef struct
{
    unsigned int FreqMHz;//channel center frequency in MHz

    unsigned int BandWidthMHz;
    unsigned int PrimaryChUpper;//1:primary is upper; 0:primary is lower
    unsigned int StationID0;
    unsigned int StationID1;
    unsigned int StationID2;
    unsigned int StationID3;

    //RF BW paramter
    unsigned int DefaultRFBW;

    unsigned int RFRegNeedSet;
}ContRxParam_t;

void ETF_PHY_TxRxParamInit(ETF_HE_TX_CONFIG_REQ* pItem);
void ETF_PHY_Stop_Rx(ETF_HE_RX_CONFIG_REQ* pItem);
void ETF_PHY_Stop_Tx(ETF_HE_TX_CONFIG_REQ* pItem);
void ETF_PHY_Start_Rx(ETF_HE_RX_CONFIG_REQ* pItem);
void ETF_PHY_Start_Tx_Step2(ETF_HE_TX_CONFIG_REQ* pItem);
void ETF_PHY_Start_Tx_Step1(ETF_HE_TX_CONFIG_REQ* pItem);

typedef struct{
	int channel;
	int mode;
	int rate_id;
	int bw;
	int chOff;
	int ldpc;
	int pktlen;
	int precom;
}atbm_etf_tx_param;


typedef struct{
	int channel;
	int mode;
	int rate_id;
	int bw;
	int chOff;
	int ldpc;
	int pktlen;
	int precom;
	char threshold_param[256];
}start_tx_param_t;

typedef struct{
	int channel;
	int bw;
	int chOff;
	int mode;//rx counter:0:ofmd;1:dsss
}start_rx_param_t;


typedef struct{
	int rxSuccess;
	int rxError;

}get_result_rx_data;
#define DCXO_CODE_MINI		0//24//0
#define DCXO_CODE_MAX		127//38//63
#define CHIP_VERSION_REG 0x0acc017c //chip version reg address
#define _32bitTo5bit(dg) (dg &= 0x1f)
#define _5bitTo32bit(delta_gain) ((delta_gain & 0x10)?(delta_gain |0xffffffe0):(delta_gain))


int atbm_ladder_txpower_init(void);
int atbm_ladder_txpower_15(void);
int atbm_ladder_txpower_63(void);

u32 MyRand(void);
int MacStringToHex(char *mac, u8  *umac);
u32 GetChipVersion(struct atbm_common *hw_priv);

void SingleToneDisable(void);
void etf_PT_test_config(char *param);

int atbm_internal_start_tx(struct atbm_common *hw_priv,start_tx_param_t *tx_param);
	
int atbm_internal_stop_tx(struct atbm_common *hw_priv);

int atbm_internal_start_rx(struct atbm_common *hw_priv,start_rx_param_t *rx_param);

int atbm_internal_stop_rx(struct atbm_common *hw_priv,get_result_rx_data *rx_data);
void atbm_etf_tx_rx_param_config(ETF_PHY_TX_PARAM_T *pParamTxRx, int channel, int mode, int rate, int chBW, int chOff, int ldpc);
int atbm_set_channel(struct atbm_common *hw_priv, u8 flag);

void etf_set_deltagain(struct efuse_headr efuse);
void set_reg_deltagain(struct efuse_headr *efuse);
void get_reg_deltagain(struct efuse_headr *efuse);
int Get_MCS_LUT_Offset_Index(int WiFiMode,int OFDMMode,int ChBW,int RateIndex);

unsigned int Get_MCS_LUT_Addr_ByTxUICtrlIndex(int WiFiModeCtrlIndex,int OfdmModeCtrlIndex,int ChBwCtrlIndex,int RateCtrlIndex);
unsigned int Get_MCS_LUT_Addr(int wifi_mode,int ofdm_mode, int ch_bw, int rate_index);
unsigned int HW_READ_REG_BIT(unsigned int addr,int endbit,int startbit);
void HW_WRITE_REG_BIT(unsigned int addr,unsigned int endBit,unsigned int startBit,unsigned int data);


void set_power_by_mode(int wifi_mode, int ofdm_mode, int bw, int rateIndex, int delfault_power, int power_delta, int powerTarFlag);
void get_power_by_bandwidth(int bw);
void set_power_by_bandwidth(int bw, u32 addrOffset, int delfault_power, int power_delta);
int atbm_internat_cmd_get_cfg_txpower(struct atbm_common *hw_priv,char **results);
int atbm_internat_cmd_get_txpower(struct atbm_common *hw_priv,char **results);
int atbm_set_txpower_mode(int power_value);


void atbm_set_extern_pa_gpio(void);
int open_auto_cfo(struct atbm_common *hw_priv,int open);

int get_work_channel(struct ieee80211_sub_if_data *sdata,int get_new_sdata);

int atbm_Default_driver_configuration(struct atbm_common *hw_priv);
int atbm_get_sta_wifi_connect_status(struct ieee80211_sub_if_data *sdata);
int atbm_internal_rate_to_rateidx(struct atbm_common *hw_priv,int rate);
void SingleToneEnable(void);
void  __PHY_RF_TX_Cal_Force_Set(u8 PowerIndex, Power_LUT_bit *pforcetable, Power_LUT_bit *pMemoryGainTable);




#endif
