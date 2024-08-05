/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef H_BLE_HCI_COMMON_
#define H_BLE_HCI_COMMON_
#include "atbm_hal.h"
#include "ble.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HCI Command Header
 *
 * Comprises the following fields
 *  -> Opcode group field & Opcode command field (2)
 *  -> Parameter Length                          (1)
 *      Length of all the parameters (does not include any part of the hci
 *      command header
 */
#if BT_BLE_ALL_SUPPORT
#define BLE_HCI_CMD_HDR_LEN                 (4)
#else
#define BLE_HCI_CMD_HDR_LEN                 (3)
#endif  //BLE_HCI_CMD_HDR_LEN

/* Generic event header */
struct ble_hci_ev {
    uint8_t opcode;
    uint8_t length;
    uint8_t  data[0];
} __packed;

#define BLE_HCI_OPCODE_NOP                  (0)

/* Set opcode based on OCF and OGF */
#define BLE_HCI_OP(ogf, ocf)            ((ocf) | ((ogf) << 10))

/* Get the OGF and OCF from the opcode in the command */
#define BLE_HCI_OGF(opcode)                 (((opcode) >> 10) & 0x003F)
#define BLE_HCI_OCF(opcode)                 ((opcode) & 0x03FF)

/* Opcode Group definitions (note: 0x07 not defined in spec) */
#define BLE_HCI_OGF_LINK_CTRL               (0x01)
#define BLE_HCI_OGF_LINK_POLICY             (0x02)
#define BLE_HCI_OGF_CTLR_BASEBAND           (0x03)
#define BLE_HCI_OGF_INFO_PARAMS             (0x04)
#define BLE_HCI_OGF_STATUS_PARAMS           (0x05)
#define BLE_HCI_OGF_TESTING                 (0x06)
#define BLE_HCI_OGF_LE                      (0x08)
#define BLE_HCI_OGF_VENDOR                  (0x3F)

/*
 * Number of LE commands. NOTE: this is really just used to size the array
 * containing the lengths of the LE commands.
 */
#define BLE_HCI_NUM_LE_CMDS                 (79)

/* List of OCF for Link Control commands (OGF=0x01) */
#define BLE_HCI_OCF_DISCONNECT_CMD          (0x0006)
#define BLE_HCI_OCF_RD_REM_VER_INFO         (0x001D)

/* List of OCF for Controller and Baseband commands (OGF=0x03) */
#define BLE_HCI_OCF_CB_SET_EVENT_MASK       (0x0001)
#define BLE_HCI_OCF_CB_RESET                (0x0003)
#define BLE_HCI_OCF_CB_READ_TX_PWR          (0x002D)
#define BLE_HCI_OCF_CB_SET_CTLR_TO_HOST_FC  (0x0031)
#define BLE_HCI_OCF_CB_HOST_BUF_SIZE        (0x0033)
#define BLE_HCI_OCF_CB_HOST_NUM_COMP_PKTS   (0x0035)
#define BLE_HCI_OCF_CB_SET_EVENT_MASK2      (0x0063)
#define BLE_HCI_OCF_CB_RD_AUTH_PYLD_TMO     (0x007B)
#define BLE_HCI_OCF_CB_WR_AUTH_PYLD_TMO     (0x007C)

/* List of OCF for Info Param commands (OGF=0x04) */
#define BLE_HCI_OCF_IP_RD_LOCAL_VER         (0x0001)
#define BLE_HCI_OCF_IP_RD_LOC_SUPP_CMD      (0x0002)
#define BLE_HCI_OCF_IP_RD_LOC_SUPP_FEAT     (0x0003)
#define BLE_HCI_OCF_IP_RD_BUF_SIZE          (0x0005)
#define BLE_HCI_OCF_IP_RD_BD_ADDR           (0x0009)

/* List of OCF for Status parameters commands (OGF = 0x05) */
#define BLE_HCI_OCF_RD_RSSI                 (0x0005)

/* List of OCF for LE commands (OGF = 0x08) */
#define BLE_HCI_OCF_LE_SET_EVENT_MASK               (0x0001)
#define BLE_HCI_OCF_LE_RD_BUF_SIZE                  (0x0002)
#define BLE_HCI_OCF_LE_RD_LOC_SUPP_FEAT             (0x0003)
/* NOTE: 0x0004 is intentionally left undefined */
#define BLE_HCI_OCF_LE_SET_RAND_ADDR                (0x0005)
#define BLE_HCI_OCF_LE_SET_ADV_PARAMS               (0x0006)
#define BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR            (0x0007)
#define BLE_HCI_OCF_LE_SET_ADV_DATA                 (0x0008)
#define BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA            (0x0009)
#define BLE_HCI_OCF_LE_SET_ADV_ENABLE               (0x000A)
#define BLE_HCI_OCF_LE_SET_SCAN_PARAMS              (0x000B)
#define BLE_HCI_OCF_LE_SET_SCAN_ENABLE              (0x000C)            
#define BLE_HCI_OCF_LE_CREATE_CONN                  (0x000D)
#define BLE_HCI_OCF_LE_CREATE_CONN_CANCEL           (0x000E)
#define BLE_HCI_OCF_LE_RD_WHITE_LIST_SIZE           (0x000F)
#define BLE_HCI_OCF_LE_CLEAR_WHITE_LIST             (0x0010)
#define BLE_HCI_OCF_LE_ADD_WHITE_LIST               (0x0011)
#define BLE_HCI_OCF_LE_RMV_WHITE_LIST               (0x0012)
#define BLE_HCI_OCF_LE_CONN_UPDATE                  (0x0013)
#define BLE_HCI_OCF_LE_SET_HOST_CHAN_CLASS          (0x0014)
#define BLE_HCI_OCF_LE_RD_CHAN_MAP                  (0x0015)
#define BLE_HCI_OCF_LE_RD_REM_FEAT                  (0x0016)
#define BLE_HCI_OCF_LE_ENCRYPT                      (0x0017)
#define BLE_HCI_OCF_LE_RAND                         (0x0018)
#define BLE_HCI_OCF_LE_START_ENCRYPT                (0x0019)
#define BLE_HCI_OCF_LE_LT_KEY_REQ_REPLY             (0x001A)
#define BLE_HCI_OCF_LE_LT_KEY_REQ_NEG_REPLY         (0x001B)
#define BLE_HCI_OCF_LE_RD_SUPP_STATES               (0x001C)
#define BLE_HCI_OCF_LE_RX_TEST                      (0x001D)
#define BLE_HCI_OCF_LE_TX_TEST                      (0x001E)
#define BLE_HCI_OCF_LE_TEST_END                     (0x001F)
#define BLE_HCI_OCF_LE_REM_CONN_PARAM_RR            (0x0020)
#define BLE_HCI_OCF_LE_REM_CONN_PARAM_NRR           (0x0021)
#define BLE_HCI_OCF_LE_SET_DATA_LEN                 (0x0022)
#define BLE_HCI_OCF_LE_RD_SUGG_DEF_DATA_LEN         (0x0023)
#define BLE_HCI_OCF_LE_WR_SUGG_DEF_DATA_LEN         (0x0024)
#define BLE_HCI_OCF_LE_RD_P256_PUBKEY               (0x0025)
#define BLE_HCI_OCF_LE_GEN_DHKEY                    (0x0026)
#define BLE_HCI_OCF_LE_ADD_RESOLV_LIST              (0x0027)
#define BLE_HCI_OCF_LE_RMV_RESOLV_LIST              (0x0028)
#define BLE_HCI_OCF_LE_CLR_RESOLV_LIST              (0x0029)
#define BLE_HCI_OCF_LE_RD_RESOLV_LIST_SIZE          (0x002A)
#define BLE_HCI_OCF_LE_RD_PEER_RESOLV_ADDR          (0x002B)
#define BLE_HCI_OCF_LE_RD_LOCAL_RESOLV_ADDR         (0x002C)
#define BLE_HCI_OCF_LE_SET_ADDR_RES_EN              (0x002D)
#define BLE_HCI_OCF_LE_SET_RPA_TMO                  (0x002E)
#define BLE_HCI_OCF_LE_RD_MAX_DATA_LEN              (0x002F)
#define BLE_HCI_OCF_LE_RD_PHY                       (0x0030)
#define BLE_HCI_OCF_LE_SET_DEFAULT_PHY              (0x0031)
#define BLE_HCI_OCF_LE_SET_PHY                      (0x0032)
#define BLE_HCI_OCF_LE_ENH_RX_TEST                  (0x0033)
#define BLE_HCI_OCF_LE_ENH_TX_TEST                  (0x0034)
#define BLE_HCI_OCF_LE_SET_ADV_SET_RND_ADDR         (0x0035)
#define BLE_HCI_OCF_LE_SET_EXT_ADV_PARAM            (0x0036)
#define BLE_HCI_OCF_LE_SET_EXT_ADV_DATA             (0x0037)
#define BLE_HCI_OCF_LE_SET_EXT_SCAN_RSP_DATA        (0x0038)
#define BLE_HCI_OCF_LE_SET_EXT_ADV_ENABLE           (0x0039)
#define BLE_HCI_OCF_LE_RD_MAX_ADV_DATA_LEN          (0x003A)
#define BLE_HCI_OCF_LE_RD_NUM_OF_ADV_SETS           (0x003B)
#define BLE_HCI_OCF_LE_REMOVE_ADV_SET               (0x003C)
#define BLE_HCI_OCF_LE_CLEAR_ADV_SETS               (0x003D)
#define BLE_HCI_OCF_LE_SET_PER_ADV_PARAMS           (0x003E)
#define BLE_HCI_OCF_LE_SET_PER_ADV_DATA             (0x003F)
#define BLE_HCI_OCF_LE_SET_PER_ADV_ENABLE           (0x0040)
#define BLE_HCI_OCF_LE_SET_EXT_SCAN_PARAM           (0x0041)
#define BLE_HCI_OCF_LE_SET_EXT_SCAN_ENABLE          (0x0042)
#define BLE_HCI_OCF_LE_EXT_CREATE_CONN              (0x0043)
#define BLE_HCI_OCF_LE_PER_ADV_CREATE_SYNC          (0x0044)
#define BLE_HCI_OCF_LE_PER_ADV_CREATE_SYNC_CANCEL   (0x0045)
#define BLE_HCI_OCF_LE_PER_ADV_TERM_SYNC            (0x0046)
#define BLE_HCI_OCF_LE_ADD_DEV_TO_PER_ADV_LIST      (0x0047)
#define BLE_HCI_OCF_LE_REM_DEV_FROM_PER_ADV_LIST    (0x0048)
#define BLE_HCI_OCF_LE_CLEAR_PER_ADV_LIST           (0x0049)
#define BLE_HCI_OCF_LE_RD_PER_ADV_LIST_SIZE         (0x004A)
#define BLE_HCI_OCF_LE_RD_TRANSMIT_POWER            (0x004B)
#define BLE_HCI_OCF_LE_RD_RF_PATH_COMPENSATION      (0x004C)
#define BLE_HCI_OCF_LE_WR_RF_PATH_COMPENSATION      (0x004D)
#define BLE_HCI_OCF_LE_SET_PRIVACY_MODE             (0x004E)

#define BLE_HCI_OCF_LE_RX_TEST_V3                        (0x004F)
#define BLE_HCI_OCF_LE_TX_TEST_V3                        (0x0050)
#define BLE_HCI_OCF_LE_SET_CONNLESS_CTE_TX_PARAMS        (0x0051)
#define BLE_HCI_OCF_LE_SET_CONNLESS_CTE_TX_ENABLE        (0x0052)
#define BLE_HCI_OCF_LE_SET_CONNLESS_IQ_SAMPLING_ENABLE   (0x0053)
#define BLE_HCI_OCF_LE_SET_CONN_CTE_RX_PARAMS            (0x0054)
#define BLE_HCI_OCF_LE_SET_CONN_CTE_TX_PARAMS            (0x0055)
#define BLE_HCI_OCF_LE_SET_CONN_CTE_REQ_ENABLE           (0x0056)
#define BLE_HCI_OCF_LE_SET_CONN_CTE_RESP_ENABLE          (0x0057)
#define BLE_HCI_OCF_LE_RD_ANTENNA_INFO                   (0x0058)
#define BLE_HCI_OCF_LE_PERIODIC_ADV_RECEIVE_ENABLE       (0x0059)

#if SUPPORT_LE_AUDIO
/* Version 5.2 */
#define HCI_OCF_LE_READ_BUF_SIZE_V2                  0x60
#define HCI_OCF_LE_READ_ISO_TX_SYNC                  0x61
#define HCI_OCF_LE_SET_CIG_PARAMS                    0x62
#define HCI_OCF_LE_SET_CIG_PARAMS_TEST               0x63
#define HCI_OCF_LE_CREATE_CIS                        0x64
#define HCI_OCF_LE_REMOVE_CIG                        0x65
#define HCI_OCF_LE_ACCEPT_CIS_REQ                    0x66
#define HCI_OCF_LE_REJECT_CIS_REQ                    0x67
#define HCI_OCF_LE_CREATE_BIG                        0x68
#define HCI_OCF_LE_CREATE_BIG_TEST                   0x69
#define HCI_OCF_LE_TERMINATE_BIG                     0x6A
#define HCI_OCF_LE_BIG_CREATE_SYNC                   0x6B
#define HCI_OCF_LE_BIG_TERMINATE_SYNC                0x6C
#define HCI_OCF_LE_REQUEST_PEER_SCA                  0x6D
#define HCI_OCF_LE_SETUP_ISO_DATA_PATH               0x6E
#define HCI_OCF_LE_REMOVE_ISO_DATA_PATH              0x6F
#define HCI_OCF_LE_ISO_TX_TEST                       0x70
#define HCI_OCF_LE_ISO_RX_TEST                       0x71
#define HCI_OCF_LE_ISO_READ_TEST_COUNTERS            0x72
#define HCI_OCF_LE_ISO_TEST_END                      0x73
#define HCI_OCF_LE_SET_HOST_FEATURE                  0x74
#define HCI_OCF_LE_READ_ISO_LINK_QUAL                0x75
#define HCI_OCF_LE_READ_ENHANCED_TX_POWER            0x76
#define HCI_OCF_LE_READ_REMOTE_TX_POWER              0x77
#define HCI_OCF_LE_SET_PATH_LOSS_REPORTING_PARAMS    0x78
#define HCI_OCF_LE_SET_PATH_LOSS_REPORTING_ENABLE    0x79
#define HCI_OCF_LE_SET_TX_POWER_REPORT_ENABLE        0x7A
/* Version 5.2 */
#define HCI_OCF_READ_LOCAL_SUP_CODECS                0x0D
#define HCI_OCF_READ_LOCAL_SUP_CODEC_CAP             0x0E
#define HCI_OCF_READ_LOCAL_SUP_CONTROLLER_DLY        0x0F
  /* Version 5.2 */
#define HCI_OCF_CONFIG_DATA_PATH                     0x83
#endif


struct ble_hci_le_periodic_adv_create_sync_cp {
    uint8_t  options;
    uint8_t  sid;
    uint8_t  peer_addr_type;
    uint8_t  peer_addr[6];
    uint16_t skip;
    uint16_t sync_timeout;
    uint8_t  sync_cte_type;
} __packed;
struct ble_hci_le_add_dev_to_periodic_adv_list_cp {
    uint8_t peer_addr_type;
    uint8_t peer_addr[6];
    uint8_t sid;
} __packed;

struct ble_hci_le_rem_dev_from_periodic_adv_list_cp {
    uint8_t peer_addr_type;
    uint8_t peer_addr[6];
    uint8_t sid;
} __packed;

struct ble_hci_le_periodic_adv_term_sync_cp {
    uint16_t sync_handle;
} __packed;

struct ble_hci_le_periodic_adv_receive_enable_cp {
    uint16_t sync_handle;
    uint8_t enable;
} __packed;

struct ble_hci_le_rd_periodic_adv_list_size_rp {
    uint8_t list_size;
} __packed;

#define BLE_HCI_OCF_LE_PERIODIC_ADV_SYNC_TRANSFER        (0x005A)
struct ble_hci_le_periodic_adv_sync_transfer_cp {
    uint16_t conn_handle;
    uint16_t service_data;
    uint16_t sync_handle;
} __packed;
struct ble_hci_le_periodic_adv_sync_transfer_rp {
    uint16_t conn_handle;
} __packed;

#define BLE_HCI_OCF_LE_PERIODIC_ADV_SET_INFO_TRANSFER    (0x005B)
struct ble_hci_le_periodic_adv_set_info_transfer_cp {
    uint16_t conn_handle;
    uint16_t service_data;
    uint8_t adv_handle;
} __packed;
struct ble_hci_le_periodic_adv_set_info_transfer_rp {
    uint16_t conn_handle;
} __packed;

#define BLE_HCI_OCF_LE_PERIODIC_ADV_SYNC_TRANSFER_PARAMS (0x005C)
struct ble_hci_le_periodic_adv_sync_transfer_params_cp {
    uint16_t conn_handle;
    uint8_t  mode;
    uint16_t skip;
    uint16_t sync_timeout;
    uint8_t  sync_cte_type;
} __packed;
struct ble_hci_le_periodic_adv_sync_transfer_params_rp {
    uint16_t conn_handle;
} __packed;

#define BLE_HCI_OCF_LE_SET_DEFAULT_SYNC_TRANSFER_PARAMS  (0x005D)
struct ble_hci_le_set_default_periodic_sync_transfer_params_cp {
    uint8_t  mode;
    uint16_t skip;
    uint16_t sync_timeout;
    uint8_t  sync_cte_type;
} __packed;

#define BLE_HCI_OCF_LE_GENERATE_DHKEY_V2                 (0x005E)
#define BLE_HCI_OCF_LE_MODIFY_SCA                        (0x005F)


#define BLE_HCI_OCF_LE_REQ_PEER_SCA                 (0x006d)
struct ble_hci_le_set_periodic_adv_params_cp {
    uint8_t adv_handle;
    uint16_t min_itvl;
    uint16_t max_itvl;
    uint16_t props;
} __packed;

struct ble_hci_le_set_periodic_adv_enable_cp {
    uint8_t enable;
    uint8_t adv_handle;
} __packed;

#define BLE_HCI_OCF_LE_SET_HOST_FEAT                     (0x0074)
struct ble_hci_le_set_host_feat_cp {
    uint8_t bit_num;
    uint8_t val;
} __packed;

#define BLE_HCI_OCF_LE_READ_ENHANCED_TX_POWER            (0x0076)
struct ble_hci_le_read_enhanced_remote_tx_power_cp {
    uint16_t conn_handle;
    int8_t phy;
}__packed;

struct ble_hci_le_read_enhanced_remote_tx_power_rp {
    uint16_t conn_handle;
    uint8_t phy;
	int8_t cur_trans_power;
	int8_t max_trans_power;
	
}__packed;

#define BLE_HCI_OCF_LE_READ_REMOTE_TX_POWER              (0x0077)
struct ble_hci_le_read_remote_tx_power_cp {
    uint16_t conn_handle;
    uint8_t phy;
}__packed;

#define BLE_HCI_OCF_LE_SET_PATH_LOSS_REPORTING_PARAMS    (0x0078)
struct ble_hci_le_set_pathloss_report_param_cp {
    uint16_t conn_handle;
    int8_t highThreshold;
    int8_t highHysteresis;
    int8_t lowThreshold;
    int8_t lowHysteresis;
	uint16_t minTimeSpent;
}__packed;

#define BLE_HCI_OCF_LE_SET_PATH_LOSS_REPORTING_ENABLE    (0x0079)
struct ble_hci_le_set_pathloss_report_enable_cp {
    uint16_t conn_handle;
    uint8_t enable;
}__packed;

#define BLE_HCI_OCF_LE_SET_TX_POWER_REPORT_ENABLE        (0x007A)
struct ble_hci_le_set_tx_power_report_enable_cp {
    uint16_t conn_handle;
    uint8_t local_enable;
	uint8_t remote_enable;
}__packed;

/* --- LE set periodic advertising data (OCF 0x003F) */
#define BLE_HCI_MAX_PERIODIC_ADV_DATA_LEN                (252)
struct ble_hci_le_set_periodic_adv_data_cp {
    uint8_t adv_handle;
    uint8_t operation;
    uint8_t adv_data_len;
    uint8_t adv_data[0];
} __packed;

struct ble_hci_le_request_peer_sca_cp {
    uint16_t conn_handle;
} __packed;

#define BLE_HCI_OCF_LE_SET_DEFAULT_SUBRATE        (0x007D)
#define BLE_HCI_OCF_LE_CONN_SUBRATE_REQ        	  (0x007E)


/* --- LE set periodic advertising parameters (OCF 0x003E) */
#define BLE_HCI_LE_SET_PERIODIC_ADV_PROP_INC_TX_PWR (0x0040)
#define BLE_HCI_LE_SET_PERIODIC_ADV_PROP_MASK       (0x0040)

/* List of OCF for vendor-specific debug commands (OGF = 0x3F) */
#define BLE_CONN_ACTIVE_CONN_TEST_MODE		(0x01)
#define BLE_HCI_OCF_LE_AUXILIARY_PACKET_SET 	(0x0002)
#define BLE_HCI_CMD_ULP_SET_PUBLIC_ADDRESS      ((uint16)0x177)//HCI_CMD_ULP_SET_PUBLIC_ADDRESS

#define BLE_HCI_VS_SET_CONN_TX_PWR     (0x03F6)
#define BLE_HCI_VS_SET_CONN_PHY_TX_PWR (0x03DD)

#define BLE_HCI_VS_START_CONN_AFH    	(0x0300)
#define BLE_HCI_VS_SET_DEFAULT_PWR    	(0x0301)
#define BLE_HCI_VS_TONE_TX_START    	(0x0302)
#define BLE_HCI_VS_TONE_TX_STOP   		(0x0303)
#define BLE_HCI_VS_SET_SCAN_CHAN_MAP   	(0x0304)
#define BLE_HCI_VS_SET_SCAN_NAME_FILT   (0x0305)
#define BLE_HCI_VS_START_SRRC   		(0x0306)
#define BLE_HCI_VS_STOP_SRRC   			(0x0307)
#define BLE_HCI_VS_SET_PUB_ADDR   		(0x0308)
#define BLE_HCI_VS_SET_CE_TEST   		(0x0309)

#define BLE_HCI_VS_SET_AFH_PARAM   		(0x0321)




/* Command Specific Definitions */
#define BLE_HCI_VARIABLE_LEN                (0xFF)

/* --- Disconnect command (OGF 0x01, OCF 0x0006) --- */
#define BLE_HCI_DISCONNECT_CMD_LEN          (3)

/* --- Set event mask (OGF 0x03, OCF 0x0001 --- */
#define BLE_HCI_SET_EVENT_MASK_LEN          (8)

/* --- Set controller to host flow control (OGF 0x03, OCF 0x0031) --- */
#define BLE_HCI_CTLR_TO_HOST_FC_LEN         (1)

#define BLE_HCI_CTLR_TO_HOST_FC_OFF         (0)
#define BLE_HCI_CTLR_TO_HOST_FC_ACL         (1)
#define BLE_HCI_CTLR_TO_HOST_FC_SYNC        (2)
#define BLE_HCI_CTLR_TO_HOST_FC_BOTH        (3)

/* --- Host buffer size (OGF 0x03, OCF 0x0033) --- */
#define BLE_HCI_HOST_BUF_SIZE_LEN           (7)

/* --- Host number of completed packets (OGF 0x03, OCF 0x0035) --- */
#define BLE_HCI_HOST_NUM_COMP_PKTS_HDR_LEN  (1)
#define BLE_HCI_HOST_NUM_COMP_PKTS_ENT_LEN  (4)

/* --- Read BD_ADDR (OGF 0x04, OCF 0x0009 --- */
#define BLE_HCI_IP_RD_BD_ADDR_ACK_PARAM_LEN (6)

/* --- Read buffer size (OGF 0x04, OCF 0x0005) --- */
#define BLE_HCI_IP_RD_BUF_SIZE_LEN          (0)
#define BLE_HCI_IP_RD_BUF_SIZE_RSPLEN       (7) /* No status byte. */

/* --- Read/Write authenticated payload timeout (ocf 0x007B/0x007C) */
#define BLE_HCI_RD_AUTH_PYLD_TMO_LEN        (4)
#define BLE_HCI_WR_AUTH_PYLD_TMO_LEN        (2)

/* --- Read local version information (OGF 0x04, OCF 0x0001) --- */
#define BLE_HCI_RD_LOC_VER_INFO_RSPLEN      (8) /* No status byte. */

/* --- Read local supported command (OGF 0x04, OCF 0x0002) --- */
#define BLE_HCI_RD_LOC_SUPP_CMD_RSPLEN      (64) /* No status byte. */

/* --- Read local supported features (OGF 0x04, OCF 0x0003) --- */
#define BLE_HCI_RD_LOC_SUPP_FEAT_RSPLEN     (8) /* No status byte. */

/* --- Read RSSI (OGF 0x05, OCF 0x0005) --- */
#define BLE_HCI_READ_RSSI_LEN               (2)
#define BLE_HCI_READ_RSSI_ACK_PARAM_LEN     (3)  /* No status byte. */

/* --- LE set event mask (OCF 0x0001) --- */
#define BLE_HCI_SET_LE_EVENT_MASK_LEN       (8)

/* --- LE read buffer size (OCF 0x0002) --- */
#define BLE_HCI_RD_BUF_SIZE_LEN             (0)
#define BLE_HCI_RD_BUF_SIZE_RSPLEN          (3) /* No status byte. */
#if SUPPORT_LE_AUDIO
#define BLE_HCI_LEN_READ_BUF_SIZE_V2_RSP       (6) /* No status byte. */
#define BLE_HCI_LEN_READ_BUF_SIZE_V2           (6)       /*!< LE read ISO buffer size command complete event length. */

#define BLE_HCI_LEN_READ_TX_SYNC               (11)      /*!< LE read ISO Tx sync. */
#define BLE_HCI_LEN_REMOVE_CIG                  1       /*!< LE remove CIG. */
#define BLE_HCI_LEN_REJECT_CIS_REQ              2       /*!< LE reject CIS request. */
#define BLE_HCI_LEN_BIG_TERMINATE_SYNC          2       /*!< LE BIG terminate sync. */
#define BLE_HCI_LEN_SETUP_ISO_DATA_PATH         2       /*!< LE setup ISO data path. No status byte */
#define BLE_HCI_LEN_REMOVE_ISO_DATA_PATH        2       /*!< LE remove ISO data path. No status byte */
#define BLE_HCI_LEN_ISO_TX_TEST                 1       /*!< LE ISO Tx Test. */
#define BLE_HCI_LEN_ISO_RX_TEST                 1       /*!< LE ISO Rx Test. */
#define BLE_HCI_LEN_ISO_READ_TEST_COUNTER       15      /*!< LE ISO read test counter. */
#define BLE_HCI_LEN_ISO_TEST_END                15      /*!< LE ISO test end. */
#define BLE_HCI_LEN_SET_HOST_FEATURE            1       /*!< LE Set Host Feature. */
#define BLE_HCI_LEN_READ_ISO_LINK_QUAL          30      /*!< LE Read ISO link quality. No status byte*/
#define BLE_HCI_LEN_READ_ENH_TX_POWER_EVT       5       /*!< LE Read enhanced TX power.No status byte */
#define BLE_HCI_LEN_SET_TX_POWER_REPORT_EVT        2    /*!< LE Set transmit power reporting enable event.No status byte */
#define BLE_HCI_LEN_SET_PATH_LOSS_REPORTING_PARAMS 2    /*!< LE Set path loss reporting parameters event. No status byte*/
#define BLE_HCI_LEN_SET_PATH_LOSS_REPORTING_ENABLE 2    /*!< LE Set path loss reporting enable event. No status byte*/
#define BLE_HCI_LEN_CONFIG_DATA_PATH               0       /*!< Configure data path. No status byte */
#define BLE_HCI_LEN_READ_LOCAL_SUP_CODECS          2       /*!< Read local supported codecs.No status byte  */
#define BLE_HCI_LEN_READ_LOCAL_SUP_CODEC_CAP       1       /*!< Read local supported codec configuration.No status byte  */
#define BLE_HCI_LEN_READ_LOCAL_SUP_CONTROLLER_DLY  6       /*!< Read local supported controller delay. No status byte */
#endif

/* --- LE read local supported features (OCF 0x0003) --- */
#define BLE_HCI_RD_LE_LOC_SUPP_FEAT_RSPLEN  (8) /* No status byte. */

/* --- LE set random address (OCF 0x0005) */
#define BLE_HCI_SET_RAND_ADDR_LEN           (6)

/* --- LE set advertising parameters (OCF 0x0006) */
#define BLE_HCI_SET_ADV_PARAM_LEN           (15)

/* Advertising types */
#define BLE_HCI_ADV_TYPE_ADV_IND            (0)
#define BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_HD  (1)
#define BLE_HCI_ADV_TYPE_ADV_SCAN_IND       (2)
#define BLE_HCI_ADV_TYPE_ADV_NONCONN_IND    (3)
#define BLE_HCI_ADV_TYPE_ADV_DIRECT_IND_LD  (4)
#define BLE_HCI_ADV_TYPE_MAX                (4)

#define BLE_HCI_ADV_CONN_MASK               (0x0001)
#define BLE_HCI_ADV_SCAN_MASK               (0x0002)
#define BLE_HCI_ADV_DIRECT_MASK             (0x0004)
#define BLE_HCI_ADV_SCAN_RSP_MASK           (0x0008)
#define BLE_HCI_ADV_LEGACY_MASK             (0x0010)

#define BLE_HCI_ADV_DATA_STATUS_COMPLETE    (0x0000)
#define BLE_HCI_ADV_DATA_STATUS_INCOMPLETE  (0x0020)
#define BLE_HCI_ADV_DATA_STATUS_TRUNCATED   (0x0040)
#define BLE_HCI_ADV_DATA_STATUS_MASK        (0x0060)

/* Own address types */
#define BLE_HCI_ADV_OWN_ADDR_PUBLIC         (0)
#define BLE_HCI_ADV_OWN_ADDR_RANDOM         (1)
#define BLE_HCI_ADV_OWN_ADDR_PRIV_PUB       (2)
#define BLE_HCI_ADV_OWN_ADDR_PRIV_RAND      (3)
#define BLE_HCI_ADV_OWN_ADDR_MAX            (3)

/* Advertisement peer address Type */
#define BLE_HCI_ADV_PEER_ADDR_PUBLIC        (0)
#define BLE_HCI_ADV_PEER_ADDR_RANDOM        (1)
#define BLE_HCI_ADV_PEER_ADDR_MAX           (1)

/* --- LE advertising channel tx power (OCF 0x0007) */
#define BLE_HCI_ADV_CHAN_TXPWR_ACK_PARAM_LEN   (2)  /* Includes status byte. */
#define BLE_HCI_ADV_CHAN_TXPWR_MIN             (-20)
#define BLE_HCI_ADV_CHAN_TXPWR_MAX             (10)

/* --- LE set advertising data (OCF 0x0008) */
#define BLE_HCI_MAX_ADV_DATA_LEN            (31)
#define BLE_HCI_SET_ADV_DATA_LEN            (32)

/* --- LE set scan response data (OCF 0x0009) */
#define BLE_HCI_MAX_SCAN_RSP_DATA_LEN       (31)
#define BLE_HCI_SET_SCAN_RSP_DATA_LEN       (32)

/* --- LE set advertising enable (OCF 0x000a) */
#define BLE_HCI_SET_ADV_ENABLE_LEN          (1)

/* --- LE set scan enable (OCF 0x000c) */
#define BLE_HCI_SET_SCAN_ENABLE_LEN         (2)

/* Connect peer address type */
#define BLE_HCI_CONN_PEER_ADDR_PUBLIC        (0)
#define BLE_HCI_CONN_PEER_ADDR_RANDOM        (1)
#define BLE_HCI_CONN_PEER_ADDR_PUBLIC_IDENT  (2)
#define BLE_HCI_CONN_PEER_ADDR_RANDOM_IDENT  (3)
#define BLE_HCI_CONN_PEER_ADDR_MAX           (3)

/*
 * Advertising filter policy
 *
 * Determines how an advertiser filters scan and connection requests.
 *
 *  NONE: no filtering (default value). No whitelist used.
 *  SCAN: process all connection requests but only scans from white list.
 *  CONN: process all scan request but only connection requests from white list
 *  BOTH: ignore all scan and connection requests unless in white list.
 */
#define BLE_HCI_ADV_FILT_NONE               (0)
#define BLE_HCI_ADV_FILT_SCAN               (1)
#define BLE_HCI_ADV_FILT_CONN               (2)
#define BLE_HCI_ADV_FILT_BOTH               (3)
#define BLE_HCI_ADV_FILT_MAX                (3)

#define BLE_HCI_ADV_FILT_DEF                (BLE_HCI_ADV_FILT_NONE)

/* Advertising interval */
#define BLE_HCI_ADV_ITVL                    (625)           /* usecs */
#if BLE_SIM_MODE_SET
#define BLE_HCI_ADV_ITVL_MIN                (4) 
#else
#define BLE_HCI_ADV_ITVL_MIN                (32)            /* units */
#endif
#define BLE_HCI_ADV_ITVL_MAX                (16384)         /* units */
#define BLE_HCI_ADV_ITVL_NONCONN_MIN        (160)           /* units */

#define BLE_HCI_ADV_ITVL_DEF                (0x800)         /* 1.28 seconds */
#define BLE_HCI_ADV_CHANMASK_DEF            (0x7)           /* all channels */

/* Set scan parameters */
#define BLE_HCI_SET_SCAN_PARAM_LEN          (7)
#define BLE_HCI_SCAN_TYPE_PASSIVE           (0)
#define BLE_HCI_SCAN_TYPE_ACTIVE            (1)

/* Scan interval and scan window timing */
#define BLE_HCI_SCAN_ITVL                   (625)           /* usecs */
#define BLE_HCI_SCAN_ITVL_MIN               (4)             /* units */
#define BLE_HCI_SCAN_ITVL_MAX               (16384)         /* units */
#define BLE_HCI_SCAN_ITVL_DEF               (16)            /* units */
#define BLE_HCI_SCAN_WINDOW_MIN             (4)             /* units */
#define BLE_HCI_SCAN_WINDOW_MAX             (16384)         /* units */
#define BLE_HCI_SCAN_WINDOW_DEF             (16)            /* units */

/*
 * Scanning filter policy
 *  NO_WL:
 *      Scanner processes all advertising packets (white list not used) except
 *      directed, connectable advertising packets not sent to the scanner.
 *  USE_WL:
 *      Scanner processes advertisements from white list only. A connectable,
 *      directed advertisment is ignored unless it contains scanners address.
 *  NO_WL_INITA:
 *      Scanner process all advertising packets (white list not used). A
 *      connectable, directed advertisement shall not be ignored if the InitA
 *      is a resolvable private address.
 *  USE_WL_INITA:
 *      Scanner process advertisements from white list only. A connectable,
 *      directed advertisement shall not be ignored if the InitA is a
 *      resolvable private address.
 */
#define BLE_HCI_SCAN_FILT_NO_WL             (0)
#define BLE_HCI_SCAN_FILT_USE_WL            (1)
#define BLE_HCI_SCAN_FILT_NO_WL_INITA       (2)
#define BLE_HCI_SCAN_FILT_USE_WL_INITA      (3)
#define BLE_HCI_SCAN_FILT_MAX               (3)

/* Whitelist commands */
#define BLE_HCI_ADD_WHITE_LIST_LEN          (7)
#define BLE_HCI_RMV_WHITE_LIST_LEN          (7)

/* Create Connection */
#define BLE_HCI_CREATE_CONN_LEN             (25)
#define BLE_HCI_CONN_ITVL                   (1250)  /* usecs */
#define BLE_HCI_CONN_FILT_NO_WL             (0)
#define BLE_HCI_CONN_FILT_USE_WL            (1)
#define BLE_HCI_CONN_FILT_MAX               (1)

#if BLE_SIM_MODE_SET
#define BLE_HCI_CONN_ITVL_MIN               (0x0001)
#else
#define BLE_HCI_CONN_ITVL_MIN               (0x0006)
#endif

#define BLE_HCI_CONN_ITVL_MAX               (0x0c80)
#define BLE_HCI_CONN_LATENCY_MIN            (0x0000)
#define BLE_HCI_CONN_LATENCY_MAX            (0x01f3)
#define BLE_HCI_CONN_SPVN_TIMEOUT_MIN       (0x000a)
#define BLE_HCI_CONN_SPVN_TIMEOUT_MAX       (0x0c80)
#define BLE_HCI_CONN_SPVN_TMO_UNITS         (10)    /* msecs */
#define BLE_HCI_INITIATOR_FILT_POLICY_MAX   (1)

/* Peer Address Type */
#define BLE_HCI_CONN_PEER_ADDR_PUBLIC       (0)
#define BLE_HCI_CONN_PEER_ADDR_RANDOM       (1)
#define BLE_HCI_CONN_PEER_ADDR_PUB_ID       (2)
#define BLE_HCI_CONN_PEER_ADDR_RAND_ID      (3)
#define BLE_HCI_CONN_PEER_ADDR_MAX          (3)

/* --- LE connection update (OCF 0x0013) */
#define BLE_HCI_CONN_UPDATE_LEN             (14)

/* --- LE set host channel classification command (OCF 0x0014) */
#define BLE_HCI_SET_HOST_CHAN_CLASS_LEN     (5)

/* --- LE read channel map command (OCF 0x0015) */
#define BLE_HCI_RD_CHANMAP_LEN              (2)
#define BLE_HCI_RD_CHANMAP_RSP_LEN          (7)

/* --- LE read remote features (OCF 0x0016) */
#define BLE_HCI_CONN_RD_REM_FEAT_LEN        (2)

/* --- LE encrypt (OCF 0x0017) */
#define BLE_HCI_LE_ENCRYPT_LEN              (32)

/* --- LE rand (OCF 0x0018) */
#define BLE_HCI_LE_RAND_LEN                 (8)

/* --- LE start encryption (OCF 0x0019) */
#define BLE_HCI_LE_START_ENCRYPT_LEN        (28)

/* ---  LE long term key request reply command (OCF 0x001a) */
#define BLE_HCI_LT_KEY_REQ_REPLY_LEN        (18)
#define BLE_HCI_LT_KEY_REQ_REPLY_ACK_PARAM_LEN (2) /* No status byte. */

/* ---  LE long term key request negative reply command (OCF 0x001b) */
#define BLE_HCI_LT_KEY_REQ_NEG_REPLY_LEN    (2)
#define BLE_HCI_LT_KEY_REQ_NEG_REPLY_ACK_PARAM_LEN (2)

/* --- LE read supported states (OCF 0x001C) --- */
#define BLE_HCI_RD_SUPP_STATES_RSPLEN       (8)

/* --- LE receiver test command (OCF 0x001D) --- */
//add select phy_mode param
//#define BLE_HCI_RX_TEST_LEN                 (1)
#define BLE_HCI_RX_TEST_LEN                 (1)

/* --- LE transitter test command (OCF 0x001E) --- */
//add select phy_mode param
//#define BLE_HCI_TX_TEST_LEN                 (3)
#define BLE_HCI_TX_TEST_LEN                 (3)

/* --- LE remote connection parameter request reply (OCF 0x0020) */
#define BLE_HCI_CONN_PARAM_REPLY_LEN        (14)

/* --- LE remote connection parameter request negative reply (OCF 0x0021) */
#define BLE_HCI_CONN_PARAM_NEG_REPLY_LEN    (3)

/* --- LE set data length (OCF 0x0022) */
#define BLE_HCI_SET_DATALEN_LEN             (6)
#define BLE_HCI_SET_DATALEN_ACK_PARAM_LEN   (2)  /* No status byte. */
#define BLE_HCI_SET_DATALEN_TX_OCTETS_MIN   (0x001b)
#define BLE_HCI_SET_DATALEN_TX_OCTETS_MAX   (0x00fb)
#define BLE_HCI_SET_DATALEN_TX_TIME_MIN     (0x0148)
#define BLE_HCI_SET_DATALEN_TX_TIME_MAX     (0x4290)

/* --- LE read suggested default data length (OCF 0x0023) */
#define BLE_HCI_RD_SUGG_DATALEN_RSPLEN      (4)

/* --- LE write suggested default data length (OCF 0x0024) */
#define BLE_HCI_WR_SUGG_DATALEN_LEN         (4)

/* --- LE generate DHKEY command (OCF 0x0026) */
#define BLE_HCI_GEN_DHKEY_LEN               (64)

/* --- LE add device to resolving list (OCF 0x0027) */
#define BLE_HCI_ADD_TO_RESOLV_LIST_LEN      (39)

/* --- LE add device to resolving list (OCF 0x0028) */
#define BLE_HCI_RMV_FROM_RESOLV_LIST_LEN    (7)

/* --- LE read peer resolvable address (OCF 0x002B) */
#define BLE_HCI_RD_PEER_RESOLV_ADDR_LEN     (7)

/* --- LE read peer resolvable address (OCF 0x002C) */
#define BLE_HCI_RD_LOC_RESOLV_ADDR_LEN      (7)

/* --- LE set address resolution enable (OCF 0x002D) */
#define BLE_HCI_SET_ADDR_RESOL_ENA_LEN      (1)

/* --- LE set resolvable private address timeout (OCF 0x002E) */
#define BLE_HCI_SET_RESOLV_PRIV_ADDR_TO_LEN (2)

/* --- LE read maximum data length (OCF 0x002F) */
#define BLE_HCI_RD_MAX_DATALEN_RSPLEN       (8)

#define BLE_HCI_LE_PERIODIC_ADV_CREATE_SYNC_OPT_FILTER      	0x01
#define BLE_HCI_LE_PERIODIC_ADV_CREATE_SYNC_OPT_DISABLED    	0x02
#define BLE_HCI_LE_PERIODIC_ADV_CREATE_SYNC_OPT_DUPLICATE    	0x04

/* --- LE read maximum default PHY (OCF 0x0030) */
#define BLE_HCI_LE_RD_PHY_LEN               (2)
#define BLE_HCI_LE_RD_PHY_RSPLEN            (4)
#define BLE_HCI_LE_PHY_1M                   (1)
#define BLE_HCI_LE_PHY_2M                   (2)
#define BLE_HCI_LE_PHY_CODED                (3)

/* --- LE set default PHY (OCF 0x0031) */
#define BLE_HCI_LE_SET_DEFAULT_PHY_LEN              (3)
#define BLE_HCI_LE_PHY_NO_TX_PREF_MASK              (0x01)
#define BLE_HCI_LE_PHY_NO_RX_PREF_MASK              (0x02)
#define BLE_HCI_LE_PHY_1M_PREF_MASK                 (0x01)
#define BLE_HCI_LE_PHY_2M_PREF_MASK                 (0x02)
#define BLE_HCI_LE_PHY_CODED_PREF_MASK              (0x04)

#define BLE_HCI_LE_PHY_PREF_MASK_ALL                \
    (BLE_HCI_LE_PHY_1M_PREF_MASK | BLE_HCI_LE_PHY_2M_PREF_MASK |  \
     BLE_HCI_LE_PHY_CODED_PREF_MASK)

/* --- LE set PHY (OCF 0x0032) */
#define BLE_HCI_LE_SET_PHY_LEN                      (7)
#define BLE_HCI_LE_PHY_CODED_ANY                    (0x0000)
#define BLE_HCI_LE_PHY_CODED_S2_PREF                (0x0001)
#define BLE_HCI_LE_PHY_CODED_S8_PREF                (0x0002)

/* --- LE enhanced receiver test (OCF 0x0033) */
#define BLE_HCI_LE_ENH_RX_TEST_LEN                  (3)
#define BLE_HCI_LE_PHY_1M                           (1)
#define BLE_HCI_LE_PHY_2M                           (2)
#define BLE_HCI_LE_PHY_CODED                        (3)

/* --- LE enhanced transmitter test (OCF 0x0034) */
#define BLE_HCI_LE_ENH_TX_TEST_LEN                  (4)
#define BLE_HCI_LE_PHY_CODED_S8                     (3)
#define BLE_HCI_LE_PHY_CODED_S2                     (4)

/* --- LE set advertising set random address (OCF 0x0035) */
#define BLE_HCI_LE_SET_ADV_SET_RND_ADDR_LEN         (7)

/* --- LE set extended advertising parameters (OCF 0x0036) */
#define BLE_HCI_LE_SET_EXT_ADV_PARAM_LEN            (25)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_CONNECTABLE     (0x0001)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_SCANNABLE       (0x0002)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_DIRECTED        (0x0004)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_HD_DIRECTED     (0x0008)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY          (0x0010)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_ANON_ADV        (0x0020)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_INC_TX_PWR      (0x0040)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_MASK            (0x7F)

#define BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_IND      (0x0013)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_LD_DIR   (0x0015)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_HD_DIR   (0x001d)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_SCAN     (0x0012)
#define BLE_HCI_LE_SET_EXT_ADV_PROP_LEGACY_NONCONN  (0x0010)

/* --- LE set extended advertising data (OCF 0x0037) */
#define BLE_HCI_MAX_EXT_ADV_DATA_LEN                (251)
#define BLE_HCI_SET_EXT_ADV_DATA_HDR_LEN            (4)

#define BLE_HCI_LE_SET_EXT_ADV_DATA_LEN             BLE_HCI_VARIABLE_LEN
#define BLE_HCI_LE_SET_EXT_ADV_DATA_OPER_INT        (0)
#define BLE_HCI_LE_SET_EXT_ADV_DATA_OPER_FIRST      (1)
#define BLE_HCI_LE_SET_EXT_ADV_DATA_OPER_LAST       (2)
#define BLE_HCI_LE_SET_EXT_ADV_DATA_OPER_COMPLETE   (3)
#define BLE_HCI_LE_SET_EXT_ADV_DATA_OPER_UNCHANGED  (4)

/* --- LE set extended scan response data (OCF 0x0038) */
#define BLE_HCI_MAX_EXT_SCAN_RSP_DATA_LEN           (251)
#define BLE_HCI_SET_EXT_SCAN_RSP_DATA_HDR_LEN       (4)

#define BLE_HCI_LE_SET_EXT_SCAN_RSP_DATA_LEN        BLE_HCI_VARIABLE_LEN
#define BLE_HCI_LE_SET_EXT_SCAN_RSP_DATA_OPER_INT        (0)
#define BLE_HCI_LE_SET_EXT_SCAN_RSP_DATA_OPER_FIRST      (1)
#define BLE_HCI_LE_SET_EXT_SCAN_RSP_DATA_OPER_LAST       (2)
#define BLE_HCI_LE_SET_EXT_SCAN_RSP_DATA_OPER_COMPLETE   (3)

/* --- LE set extended advertising enable (OCF 0x0039) */
#define BLE_HCI_LE_SET_EXT_ADV_ENABLE_LEN           BLE_HCI_VARIABLE_LEN

/* --- LE remove advertising set (OCF 0x003C) */
#define BLE_HCI_LE_REMOVE_ADV_SET_LEN               (1)

/* --- LE read maximum advertising data length (OCF 0x003A) */
#define BLE_HCI_RD_MAX_ADV_DATA_LEN                 (2)

/* --- LE read number of supported advertising sets (OCF 0x003B) */
#define BLE_HCI_RD_NR_SUP_ADV_SETS                  (1)

/* --- LE set periodic advertising parameters (OCF 0x003E) */
#define BLE_HCI_LE_SET_PER_ADV_PARAMS_LEN           (7)

/* --- LE set periodic advertising data (OCF 0x003F) */
#define BLE_HCI_LE_SET_PER_ADV_DATA_LEN             BLE_HCI_VARIABLE_LEN

/* --- LE periodic advertising enable (OCF 0x0040) */
#define BLE_HCI_LE_SET_PER_ADV_ENABLE_LEN           (2)

/* --- LE set extended scan parameters (OCF 0x0041) */
#define BLE_HCI_LE_SET_EXT_SCAN_PARAM_LEN           BLE_HCI_VARIABLE_LEN
#define BLE_HCI_LE_EXT_SCAN_BASE_LEN                (3)
#define BLE_HCI_LE_EXT_SCAN_SINGLE_PARAM_LEN        (5)

/* --- LE set extended scan enable (OCF 0x0042) */
#define BLE_HCI_LE_SET_EXT_SCAN_ENABLE_LEN          (6)

/* --- LE extended create connection (OCF 0x0043) */
#define BLE_HCI_LE_EXT_CREATE_CONN_LEN              BLE_HCI_VARIABLE_LEN
#define BLE_HCI_LE_EXT_CREATE_CONN_BASE_LEN         (10)

/* --- LE periodic advertising create sync (OCF 0x0044) */
#define BLE_HCI_LE_PER_ADV_CREATE_SYNC_LEN          (14)

/* --- LE periodic advertising terminate (OCF 0x0046) */
#define BLE_HCI_LE_PER_ADV_TERM_SYNC_LEN            (2)

/* --- LE add device to periodic advertising list (OCF 0x0047) */
#define BLE_HCI_LE_ADD_DEV_TO_PER_ADV_LIST_LEN      (8)

/* --- LE remove device from periodic advertising list (OCF 0x0048) */
#define BLE_HCI_LE_REM_DEV_FROM_PER_ADV_LIST_LEN    (8)

/* --- LE write RF path (OCF 0x004D) */
#define BLE_HCI_LE_WR_RF_PATH_COMPENSATION_LEN      (4)

/* --- LE set privacy mode (OCF 0x004E) */
#define BLE_HCI_LE_SET_PRIVACY_MODE_LEN             (8)
#define BLE_HCI_PRIVACY_NETWORK                     (0)
#define BLE_HCI_PRIVACY_DEVICE                      (1)

/* Event Codes */
#define BLE_HCI_EVCODE_INQUIRY_CMP          (0x01)
#define BLE_HCI_EVCODE_INQUIRY_RESULT       (0x02)
#define BLE_HCI_EVCODE_CONN_DONE            (0x03)
#define BLE_HCI_EVCODE_CONN_REQUEST         (0x04)
#define BLE_HCI_EVCODE_DISCONN_CMP          (0x05)
#define BLE_HCI_EVCODE_AUTH_CMP             (0x06)
#define BLE_HCI_EVCODE_REM_NAME_REQ_CMP     (0x07)
#define BLE_HCI_EVCODE_ENCRYPT_CHG          (0x08)
#define BLE_HCI_EVCODE_CHG_LINK_KEY_CMP     (0x09)
#define BLE_HCI_EVCODE_MASTER_LINK_KEY_CMP  (0x0A)
#define BLE_HCI_EVCODE_RD_REM_SUPP_FEAT_CMP (0x0B)
#define BLE_HCI_EVCODE_RD_REM_VER_INFO_CMP  (0x0C)
#define BLE_HCI_EVCODE_QOS_SETUP_CMP        (0x0D)
#define BLE_HCI_EVCODE_COMMAND_COMPLETE     (0x0E)
#define BLE_HCI_EVCODE_COMMAND_STATUS       (0x0F)
#define BLE_HCI_EVCODE_HW_ERROR             (0x10)
#define BLE_HCI_EVCODE_NUM_COMP_PKTS        (0x13)
#define BLE_HCI_EVCODE_MODE_CHANGE          (0x14)
#define BLE_HCI_EVCODE_RETURN_LINK_KEYS     (0x15)
#define BLE_HCI_EVCODE_PIN_CODE_REQ         (0x16)
#define BLE_HCI_EVCODE_LINK_KEY_REQ         (0x17)
#define BLE_HCI_EVCODE_LINK_KEY_NOTIFY      (0x18)
#define BLE_HCI_EVCODE_LOOPBACK_CMD         (0x19)
#define BLE_HCI_EVCODE_DATA_BUF_OVERFLOW    (0x1A)
#define BLE_HCI_EVCODE_MAX_SLOTS_CHG        (0x1B)
#define BLE_HCI_EVCODE_READ_CLK_OFF_COMP    (0x1C)
#define BLE_HCI_EVCODE_CONN_PKT_TYPE_CHG    (0x1D)
#define BLE_HCI_EVCODE_QOS_VIOLATION        (0x1E)
/* NOTE: 0x1F not defined */
#define BLE_HCI_EVCODE_PSR_MODE_CHG         (0x20)
#define BLE_HCI_EVCODE_FLOW_SPEC_COMP       (0x21)
#define BLE_HCI_EVCODE_INQ_RESULT_RSSI      (0x22)
#define BLE_HCI_EVCODE_READ_REM_EXT_FEAT    (0x23)
/* NOTE: 0x24 - 0x2B not defined */
#define BLE_HCI_EVCODE_SYNCH_CONN_COMP      (0x2C)
#define BLE_HCI_EVCODE_SYNCH_CONN_CHG       (0x2D)
#define BLE_HCI_EVCODE_SNIFF_SUBRATING      (0x2E)
#define BLE_HCI_EVCODE_EXT_INQ_RESULT       (0x2F)
#define BLE_HCI_EVCODE_ENC_KEY_REFRESH      (0x30)
#define BLE_HCI_EVOCDE_IO_CAP_REQ           (0x31)
#define BLE_HCI_EVCODE_IO_CAP_RSP           (0x32)
#define BLE_HCI_EVCODE_USER_CONFIRM_REQ     (0x33)
#define BLE_HCI_EVCODE_PASSKEY_REQ          (0x34)
#define BLE_HCI_EVCODE_REM_OOB_DATA_REQ     (0x35)
#define BLE_HCI_EVCODE_SIMPLE_PAIR_COMP     (0x36)
/* NOTE: 0x37 not defined */
#define BLE_HCI_EVCODE_LNK_SPVN_TMO_CHG     (0x38)
#define BLE_HCI_EVCODE_ENH_FLUSH_COMP       (0x39)
#define BLE_HCI_EVCODE_USER_PASSKEY_NOTIFY  (0x3B)
#define BLE_HCI_EVCODE_KEYPRESS_NOTIFY      (0x3C)
#define BLE_HCI_EVCODE_REM_HOST_SUPP_FEAT   (0x3D)
#define BLE_HCI_EVCODE_LE_META              (0x3E)
/* NOTE: 0x3F not defined */
#define BLE_HCI_EVCODE_PHYS_LINK_COMP       (0x40)
#define BLE_HCI_EVCODE_CHAN_SELECTED        (0x41)
#define BLE_HCI_EVCODE_DISCONN_PHYS_LINK    (0x42)
#define BLE_HCI_EVCODE_PHYS_LINK_LOSS_EARLY (0x43)
#define BLE_HCI_EVCODE_PHYS_LINK_RECOVERY   (0x44)
#define BLE_HCI_EVCODE_LOGICAL_LINK_COMP    (0x45)
#define BLE_HCI_EVCODE_DISCONN_LOGICAL_LINK (0x46)
#define BLE_HCI_EVCODE_FLOW_SPEC_MODE_COMP  (0x47)
#define BLE_HCI_EVCODE_NUM_COMP_DATA_BLKS   (0x48)
#define BLE_HCI_EVCODE_AMP_START_TEST       (0x49)
#define BLE_HCI_EVOCDE_AMP_TEST_END         (0x4A)
#define BLE_HCI_EVOCDE_AMP_RCVR_REPORT      (0x4B)
#define BLE_HCI_EVCODE_SHORT_RANGE_MODE_CHG (0x4C)
#define BLE_HCI_EVCODE_AMP_STATUS_CHG       (0x4D)
#define BLE_HCI_EVCODE_TRIG_CLK_CAPTURE     (0x4E)
#define BLE_HCI_EVCODE_SYNCH_TRAIN_COMP     (0x4F)
#define BLE_HCI_EVCODE_SYNCH_TRAIN_RCVD     (0x50)
#define BLE_HCI_EVCODE_SLAVE_BCAST_RX       (0x51)
#define BLE_HCI_EVCODE_SLAVE_BCAST_TMO      (0x52)
#define BLE_HCI_EVCODE_TRUNC_PAGE_COMP      (0x53)
#define BLE_HCI_EVCODE_SLAVE_PAGE_RSP_TMO   (0x54)
#define BLE_HCI_EVCODE_SLAVE_BCAST_CHAN_MAP (0x55)
#define BLE_HCI_EVCODE_INQ_RSP_NOTIFY       (0x56)
#define BLE_HCI_EVCODE_AUTH_PYLD_TMO        (0x57)
#define BLE_HCI_EVCODE_VENDOR_DEBUG         (0xFF)

/* LE sub-event codes */
#define BLE_HCI_LE_SUBEV_CONN_COMPLETE          (0x01)
#define BLE_HCI_LE_SUBEV_ADV_RPT                (0x02)
#define BLE_HCI_LE_SUBEV_CONN_UPD_COMPLETE      (0x03)
#define BLE_HCI_LE_SUBEV_RD_REM_USED_FEAT       (0x04)
#define BLE_HCI_LE_SUBEV_LT_KEY_REQ             (0x05)
#define BLE_HCI_LE_SUBEV_REM_CONN_PARM_REQ      (0x06)
#define BLE_HCI_LE_SUBEV_DATA_LEN_CHG           (0x07)
#define BLE_HCI_LE_SUBEV_RD_LOC_P256_PUBKEY     (0x08)
#define BLE_HCI_LE_SUBEV_GEN_DHKEY_COMPLETE     (0x09)
#define BLE_HCI_LE_SUBEV_ENH_CONN_COMPLETE      (0x0A)
#define BLE_HCI_LE_SUBEV_DIRECT_ADV_RPT         (0x0B)
#define BLE_HCI_LE_SUBEV_PHY_UPDATE_COMPLETE    (0x0C)
#define BLE_HCI_LE_SUBEV_EXT_ADV_RPT            (0x0D)
#define BLE_HCI_LE_SUBEV_PER_ADV_SYNC_ESTAB     (0x0E)
#define BLE_HCI_LE_SUBEV_PER_ADV_RPT            (0x0F)
#define BLE_HCI_LE_SUBEV_PER_ADV_SYNC_LOST      (0x10)
#define BLE_HCI_LE_SUBEV_SCAN_TIMEOUT           (0x11)
#define BLE_HCI_LE_SUBEV_ADV_SET_TERMINATED     (0x12)
#define BLE_HCI_LE_SUBEV_SCAN_REQ_RCVD          (0x13)
#define BLE_HCI_LE_SUBEV_CHAN_SEL_ALG           (0x14)
#define BLE_HCI_LE_SUBEV_PERIODIC_ADV_SYNC_TRANSFER   	(0x18)
#define BLE_HCI_LE_SUBEV_REQ_PEER_SCA_COMP      		(0x1F)

#if SUPPORT_LE_AUDIO
/* Version 5.2 */
#define HCI_LE_CIS_EST_EVT                           0x19
#define BLE_HCI_LE_SUBEV_CIS_REQ                     0x1A
#define HCI_LE_CREATE_BIG_CMPL_EVT                   0x1B
#define HCI_LE_TERMINATE_BIG_CMPL_EVT                0x1C
#define HCI_LE_BIG_SYNC_EST_EVT                      0x1D
#define HCI_LE_BIG_SYNC_LOST_EVT                     0x1E
#define HCI_LE_REQ_PEER_SCA_CMPLT_EVT                0x1F
#define HCI_LE_PATH_LOSS_REPORT_EVT                  0x20
#define HCI_LE_POWER_REPORT_EVT                      0x21
#define HCI_LE_BIG_INFO_ADV_REPORT_EVT               0x22

/* Version 5.2 */
#define HCI_LEN_LE_CIS_EST                           29      /*!< CIS established event length. */
#define BLE_HCI_LE_EVENT_CIS_REQ_LEN                 7       /*!< CIS request event length. */
#define HCI_LEN_LE_PEER_SCA_CMPL                     5       /*!< Request peer SCA complete event length. */
#define HCI_LEN_LE_CREATE_BIG_CMPL(numBis)           (19 + (2 * numBis))    /*!< Create BIG complete event length. */
#define HCI_LEN_LE_TERMINATE_BIG_CMPL                3       /*!< Terminate BIG complete event length. */
#define HCI_LEN_LE_BIG_SYNC_EST(numBis)              (15 + (2 * numBis))    /*!< BIG sync established event length. */
#define HCI_LEN_LE_BIG_SYNC_LOST                     3       /*!< BIG sync lost event length. */
#define HCI_LEN_LE_POWER_REPORT                      9       /*!< Power reporting event length. */
#define HCI_LEN_LE_PATH_LOSS_ZONE                    5       /*!< Path loss reporting event length. */
#define HCI_LEN_LE_BIG_INFO_ADV_REPORT               20      /*!< BIG Info advertising report length. */
#endif
//new added 
#define BLE_HCI_LE_SUBEV_PATH_LOSS_THRESHOLD  (0x20)
#define BLE_HCI_LE_SUBEV_POWER_REPORTING_EVENT      (0x21)

#define BLE_HCI_LE_SUBEV_SUBRATE_CHANGE_EVENT      (0x23)

/* Generic event header */
#define BLE_HCI_EVENT_HDR_LEN               (2)

/* Event specific definitions */
/* Event disconnect complete */
#define BLE_HCI_EVENT_DISCONN_COMPLETE_LEN  (4)

/* Event encryption change (code=0x08) */
#define BLE_HCI_EVENT_ENCRYPT_CHG_LEN       (4)

/* Event hardware error (code=0x10) */
#define BLE_HCI_EVENT_HW_ERROR_LEN          (1)

/* Event key refresh complete (code=0x30) */
#define BLE_HCI_EVENT_ENC_KEY_REFRESH_LEN   (3)

/* Event command complete */
#define BLE_HCI_EVENT_CMD_COMPLETE_HDR_LEN  (5)
#define BLE_HCI_EVENT_CMD_COMPLETE_MIN_LEN  (6)

/* Event command status */
#define BLE_HCI_EVENT_CMD_STATUS_LEN        (6)

/* Number of completed packets */
#define BLE_HCI_EVENT_NUM_COMP_PKTS_HDR_LEN (1)
#define BLE_HCI_EVENT_NUM_COMP_PKTS_ENT_LEN (4)

/* Read remote version informaton */
#define BLE_HCI_EVENT_RD_RM_VER_LEN         (8)

/* Data buffer overflow event */
#define BLE_HCI_EVENT_DATABUF_OVERFLOW_LEN  (1)
#define BLE_HCI_EVENT_ACL_BUF_OVERFLOW      (0x01)

/* Advertising report */
#define BLE_HCI_ADV_RPT_EVTYPE_ADV_IND      (0)
#define BLE_HCI_ADV_RPT_EVTYPE_DIR_IND      (1)
#define BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND     (2)
#define BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND  (3)
#define BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP     (4)

/* Bluetooth 5, Vol 2, Part E, 7.7.65.13 */
#define BLE_HCI_LEGACY_ADV_EVTYPE_ADV_IND                 (0x13)
#define BLE_HCI_LEGACY_ADV_EVTYPE_ADV_DIRECT_IND          (0x15)
#define BLE_HCI_LEGACY_ADV_EVTYPE_ADV_SCAN_IND            (0x12)
#define BLE_HCI_LEGACY_ADV_EVTYPE_ADV_NONCON_IND          (0x10)
#define BLE_HCI_LEGACY_ADV_EVTYPE_SCAN_RSP_ADV_IND        (0x1b)
#define BLE_HCI_LEGACY_ADV_EVTYPE_SCAN_RSP_ADV_SCAN_IND   (0x1a)

/* --- LE remove device from periodic advertising list (OCF 0x0048) */
#define BLE_HCI_PERIODIC_DATA_STATUS_COMPLETE   0x00
#define BLE_HCI_PERIODIC_DATA_STATUS_INCOMPLETE 0x01
#define BLE_HCI_PERIODIC_DATA_STATUS_TRUNCATED  0x02


/* LE sub-event specific definitions */
#define BLE_HCI_LE_MIN_LEN                  (1) /* Not including event hdr. */

/* LE connection complete event (sub event 0x01) */
#define BLE_HCI_LE_CONN_COMPLETE_LEN            (19)
#define BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER    (0x00)
#define BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE     (0x01)

/* Maximum valid connection handle value */
#define BLE_HCI_LE_CONN_HANDLE_MAX              (0x0eff)

/* LE advertising report event. (sub event 0x02) */
#define BLE_HCI_LE_ADV_RPT_MIN_LEN          (12)
#define BLE_HCI_LE_ADV_DIRECT_RPT_LEN       (18)
#define BLE_HCI_LE_ADV_RPT_NUM_RPTS_MIN     (1)
#define BLE_HCI_LE_ADV_RPT_NUM_RPTS_MAX     (0x19)

/* Length of each record in an LE direct advertising report event. */
#define BLE_HCI_LE_ADV_DIRECT_RPT_SUB_LEN   (16)

/* LE connection update complete event (sub event 0x03) */
#define BLE_HCI_LE_CONN_UPD_LEN             (10)

/* LE long term key request event (sub event 0x05) */
#define BLE_HCI_LE_LT_KEY_REQ_LEN           (13)

/* LE connection update complete event (sub event 0x03) */
#define BLE_HCI_LE_RD_REM_USED_FEAT_LEN     (12)

/* LE remote connection parameter request event (sub event 0x06) */
#define BLE_HCI_LE_REM_CONN_PARM_REQ_LEN    (11)

/* LE data length change event (sub event 0x07) */
#define BLE_HCI_LE_DATA_LEN_CHG_LEN         (11)

/* LE PHY update complete event (sub event 0x0C) */
#define BLE_HCI_LE_PHY_UPD_LEN              (6)

/* LE Scan Timeout Event (sub event 0x11) */
#define BLE_HCI_LE_SUBEV_SCAN_TIMEOUT_LEN   (1)

/*  LE Advertising Set Terminated Event (sub event 0x12) */
#define BLE_HCI_LE_SUBEV_ADV_SET_TERMINATED_LEN   (6)

/* LE Scan Request Received event (sub event 0x13) */
#define BLE_HCI_LE_SUBEV_SCAN_REQ_RCVD_LEN   (9)

/* LE Channel Selection Algorithm event (sub event 0x14) */
#define BLE_HCI_LE_SUBEV_CHAN_SEL_ALG_LEN   (4)

/* Bluetooth Assigned numbers for version information.*/
#define BLE_HCI_VER_BCS_1_0b                (0)
#define BLE_HCI_VER_BCS_1_1                 (1)
#define BLE_HCI_VER_BCS_1_2                 (2)
#define BLE_HCI_VER_BCS_2_0_EDR             (3)
#define BLE_HCI_VER_BCS_2_1_EDR             (4)
#define BLE_HCI_VER_BCS_3_0_HCS             (5)
#define BLE_HCI_VER_BCS_4_0                 (6)
#define BLE_HCI_VER_BCS_4_1                 (7)
#define BLE_HCI_VER_BCS_4_2                 (8)
#define BLE_HCI_VER_BCS_5_0                 (9)

#define BLE_LMP_VER_BCS_1_0b                (0)
#define BLE_LMP_VER_BCS_1_1                 (1)
#define BLE_LMP_VER_BCS_1_2                 (2)
#define BLE_LMP_VER_BCS_2_0_EDR             (3)
#define BLE_LMP_VER_BCS_2_1_EDR             (4)
#define BLE_LMP_VER_BCS_3_0_HCS             (5)
#define BLE_LMP_VER_BCS_4_0                 (6)
#define BLE_LMP_VER_BCS_4_1                 (7)
#define BLE_LMP_VER_BCS_4_2                 (8)
#define BLE_LMP_VER_BCS_5_0                 (9)

/* Sub-event 0x0A: enhanced connection complete */
#define BLE_HCI_LE_ENH_CONN_COMPLETE_LEN    (31)

#define BLE_HCI_LE_AUXILIARY_PACKET_SET_LEN (2)
/*--- Shared data structures ---*/

/* Host buffer size (OGF=0x03, OCF=0x0033) */
struct hci_host_buf_size
{
    uint16_t acl_pkt_len;
    uint8_t sync_pkt_len;
    uint16_t num_acl_pkts;
    uint16_t num_sync_pkts;
};

/* Host number of completed packets (OGF=0x03, OCF=0x0035) */
struct hci_host_num_comp_pkts_entry
{
    uint16_t conn_handle;
    uint16_t num_pkts;
};

/* Read local version information (OGF=0x0004, OCF=0x0001) */
struct hci_loc_ver_info
{
   uint8_t status;
   uint8_t hci_version;
   uint16_t hci_revision;
   uint8_t lmp_pal_version;
   uint16_t mfrg_name;
   uint8_t lmp_pal_subversion;
};

/* set random address command (ocf = 0x0005) */
struct hci_rand_addr
{
    uint8_t addr[6];
};

/* set advertising parameters command (ocf = 0x0006) */
struct hci_adv_params
{
    uint8_t adv_type;
    uint8_t adv_channel_map;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t adv_filter_policy;
	uint8_t reverd[3];
    uint16_t adv_itvl_min;
    uint16_t adv_itvl_max;
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];
};
/* set scanninging parameters command (ocf = 0x000B) */
struct hci_scan_params
{
    uint8_t scan_type;
    uint8_t own_addr_type;
    uint8_t scan_filter_policy;
	uint8_t reverd;
    uint16_t scan_itvl;
    uint16_t scan_window;
};

/* LE create connection command (ocf=0x000d). */
struct hci_create_conn
{
    uint16_t scan_itvl;
    uint16_t scan_window;
    uint8_t filter_policy;
    uint8_t peer_addr_type;
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];
    uint8_t own_addr_type;
    uint8_t reseverd;
    uint16_t conn_itvl_min;
    uint16_t conn_itvl_max;
    uint16_t conn_latency;
    uint16_t supervision_timeout;
    uint16_t min_ce_len;
    uint16_t max_ce_len;
};

/* LE Read Local P-256 Public Key Complete Event */
struct hci_le_subev_rd_loc_p256_pubkey {
    uint8_t status;
    uint8_t pubkey[64];
} __packed;

/* LE Generate DHKey Complete Event */
struct hci_le_subev_gen_dhkey_complete {
    uint8_t status;
    uint8_t dhkey[32];
} __packed;

/* LE Directed Advertising Report Event */
struct hci_le_subev_direct_adv_rpt_param {
    uint8_t evt_type;
    uint8_t addr_type;
    uint8_t addr[6];
    uint8_t dir_addr_type;
    uint8_t dir_addr[6];
    int8_t rssi;
} __packed;

struct hci_le_subev_direct_adv_rpt {
    uint8_t num_reports;
    struct hci_le_subev_direct_adv_rpt_param params[0];
} __packed;

#if 1
/* LE create connection command (ocf=0x0043). */
struct hci_ext_conn_params
{
    uint16_t scan_itvl;
    uint16_t scan_window;
    uint16_t conn_itvl_min;
    uint16_t conn_itvl_max;
    uint16_t conn_latency;
    uint16_t supervision_timeout;
    uint16_t min_ce_len;
    uint16_t max_ce_len;
};

struct hci_ext_create_conn
{
    uint8_t filter_policy;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];
    uint8_t init_phy_mask;
    struct hci_ext_conn_params params[3];
};

struct hci_ext_adv_report_param {
    uint16_t evt_type;
    uint8_t addr_type;
    uint8_t addr[6];
    uint8_t prim_phy;
    uint8_t sec_phy;
    uint8_t sid;
    uint8_t tx_power;
    int8_t rssi;
    uint16_t periodic_itvl;
    uint8_t dir_addr_type;
    uint8_t dir_addr[6];
    uint8_t adv_data_len;
    uint8_t adv_data[0];
} __packed;

struct hci_ext_adv_report {
    /* We support one report per event for now */
    uint8_t subevt;
    uint8_t num_reports;
    struct hci_ext_adv_report_param params[0];
} __packed;

/* Ext Adv Set enable parameters, not in HCI order */
struct hci_ext_adv_set
{
    uint8_t handle;
    uint8_t events;
    uint16_t duration;
};

/* Ext Advertising Parameters */
struct hci_ext_adv_params
{
    uint16_t properties;
    uint32_t min_interval;
    uint32_t max_interval;
    uint8_t chan_map;
    uint8_t own_addr_type;
    uint8_t peer_addr_type;
    uint8_t peer_addr[6];
    uint8_t filter_policy;
    int8_t tx_power;
    uint8_t primary_phy;
    uint8_t max_skip;
    uint8_t secondary_phy;
    uint8_t sid;
    uint8_t scan_req_notif;
};

/* LE Extended Advertising Report Event */
struct hci_le_subev_ext_adv_rpt {
    uint8_t num_reports;
    struct hci_ext_adv_report_param params[0];
} __packed;

/* LE Periodic Advertising Sync Established Event */
struct hci_le_subev_per_adv_sync_estab {
    uint8_t status;
    uint16_t sync_handle;
    uint8_t sid;
    uint8_t adv_addr_type;
    uint8_t adv_addr[6];
    uint8_t adv_phy;
    uint16_t per_adv_ival;
    uint8_t adv_clk_accuracy;
} __packed;

/* LE Periodic Advertising Report Event */
struct hci_le_subev_per_adv_rpt {
    uint16_t sync_handle;
    uint8_t tx_power;
    int8_t rssi;
    uint8_t _unused;
    uint8_t data_status;
    uint8_t data_length;
    uint8_t data[0];
} __packed;

/* LE Periodic Advertising Sync Lost Event */
struct hci_le_subev_per_adv_sync_lost {
    uint16_t sync_handle;
} __packed;

/* LE Advertising Set Terminated Event */
struct hci_le_subev_adv_set_terminated {
    uint8_t status;
    uint8_t adv_handle;
    uint16_t conn_handle;
    uint8_t num_compl_ext_adv_ev;
} __packed;

/* LE Scan Request Received Event */
struct hci_le_subev_scan_req_rcvd {
    uint8_t adv_handle;
    uint8_t scan_addr_type;
    uint8_t scan_addr[6];
} __packed;

#endif

/* LE Channel Selection Algorithm Event */
struct hci_le_subev_chan_sel_alg {
    uint16_t conn_handle;
    uint8_t chan_sel_alg;
} __packed;

/* LE connection update command (ocf=0x0013). */
struct hci_conn_update
{
    uint16_t handle;
    uint16_t conn_itvl_min;
    uint16_t conn_itvl_max;
    uint16_t conn_latency;
    uint16_t supervision_timeout;
    uint16_t min_ce_len;
    uint16_t max_ce_len;
};

/* LE start encryption command (ocf=0x0019) (note: fields out of order). */
struct hci_start_encrypt
{
    uint16_t connection_handle;
    uint16_t encrypted_diversifier;
    uint64_t random_number;
    uint8_t long_term_key[16];
};

/* LE long term key request reply command (ocf=0x001a). */
struct hci_lt_key_req_reply
{
    uint16_t conn_handle;
    uint8_t long_term_key[16];
};

/* LE Remote connection parameter request reply command */
struct hci_conn_param_reply
{
    uint16_t handle;
    uint16_t conn_itvl_min;
    uint16_t conn_itvl_max;
    uint16_t conn_latency;
    uint16_t supervision_timeout;
    uint16_t min_ce_len;
    uint16_t max_ce_len;
};

/* LE Remote connection parameter request negative reply command */
struct hci_conn_param_neg_reply
{
    uint16_t handle;
    uint8_t reason;
};

/* Encryption change event (code=0x08) (note: fields out of order) */
struct hci_encrypt_change
{
    uint8_t status;
    uint8_t encryption_enabled;
    uint16_t connection_handle;
};

/* Encryption key refresh complete event (code=0x30) */
struct hci_encrypt_key_refresh
{
    uint8_t status;
    uint16_t connection_handle;
};

/* Connection complete LE meta subevent */
struct hci_le_conn_complete
{
    uint8_t subevent_code;
    uint8_t status;
    uint16_t connection_handle;
    uint8_t role;
    uint8_t peer_addr_type;
    uint8_t peer_addr[BLE_DEV_ADDR_LEN];
    uint16_t conn_itvl;
    uint16_t conn_latency;
    uint16_t supervision_timeout;
    uint8_t master_clk_acc;
    uint8_t local_rpa[BLE_DEV_ADDR_LEN];
    uint8_t peer_rpa[BLE_DEV_ADDR_LEN];
};

/* Connection update complete LE meta subevent */
struct hci_le_conn_upd_complete
{
    uint8_t subevent_code;
    uint8_t status;
    uint16_t connection_handle;
    uint16_t conn_itvl;
    uint16_t conn_latency;
    uint16_t supervision_timeout;
};

/* Remote connection parameter request LE meta subevent */
struct hci_le_conn_param_req
{
    uint8_t subevent_code;
    uint16_t connection_handle;
    uint16_t itvl_min;
    uint16_t itvl_max;
    uint16_t latency;
    uint16_t timeout;
};

/* Read Remote Supported Features complete LE meta subevent */
struct hci_le_rd_rem_supp_feat_complete
{
    uint8_t subevent_code;
    uint8_t status;
    uint16_t connection_handle;
    uint8_t features[8];
};

/* LE long term key request event (note: fields out of order). */
struct hci_le_lt_key_req
{
    uint64_t random_number;
    uint16_t connection_handle;
    uint16_t encrypted_diversifier;
    uint8_t subevent_code;
};

/* Disconnection complete event (note: fields out of order). */
struct hci_disconn_complete
{
    uint16_t connection_handle;
    uint8_t status;
    uint8_t reason;
};

/* Read RSSI command-complete parameters (note: fields out of order). */
struct hci_read_rssi_ack_params
{
    uint16_t connection_handle;
    uint8_t status;
    int8_t rssi;
};

/* PHY updated completed LE meta subevent */
struct hci_le_phy_upd_complete
{
    uint8_t subevent_code;
    uint8_t status;
    uint16_t connection_handle;
    uint8_t tx_phy;
    uint8_t rx_phy;
};

/* LE Advertising Set Terminated subevent*/
struct hci_le_adv_set_terminated
{
    uint8_t subevent_code;
    uint8_t status;
    uint8_t adv_handle;
    uint16_t conn_handle;
    uint8_t completed_events;
};

struct ble_hci_ev_le_subev_periodic_adv_sync_estab {
    uint8_t  subev_code;
    uint8_t  status;
    uint16_t sync_handle;
    uint8_t  sid;
    uint8_t  peer_addr_type;
    uint8_t  peer_addr[6];
    uint8_t  phy;
    uint16_t interval;
    uint8_t  aca;
} __packed;

struct ble_hci_ev_le_subev_periodic_adv_rpt {
    uint8_t  subev_code;
    uint16_t sync_handle;
    int8_t   tx_power;
    int8_t   rssi;
    uint8_t  cte_type;
    uint8_t  data_status;
    uint8_t  data_len;
    uint8_t  data[0];
} __packed;

struct ble_hci_ev_le_subev_periodic_adv_sync_lost {
    uint8_t  subev_code;
    uint16_t sync_handle;
} __packed;

struct ble_hci_ev_le_subev_periodic_adv_sync_transfer {
    uint8_t  subev_code;
    uint8_t  status;
    uint16_t conn_handle;
    uint16_t service_data;
    uint16_t sync_handle;
    uint8_t  sid;
    uint8_t  peer_addr_type;
    uint8_t  peer_addr[6];
    uint8_t  phy;
    uint16_t interval;
    uint8_t  aca;
} __packed;

#define BLE_HCI_DATA_HDR_SZ                 4
#define BLE_HCI_DATA_HANDLE(handle_pb_bc)   (((handle_pb_bc) & 0x0fff) >> 0)
#define BLE_HCI_DATA_PB(handle_pb_bc)       (((handle_pb_bc) & 0x3000) >> 12)
#define BLE_HCI_DATA_BC(handle_pb_bc)       (((handle_pb_bc) & 0xc000) >> 14)

struct hci_data_hdr
{
    uint16_t hdh_handle_pb_bc;
    uint16_t hdh_len;
};

#define BLE_HCI_PB_FIRST_NON_FLUSH          0
#define BLE_HCI_PB_MIDDLE                   1
#define BLE_HCI_PB_FIRST_FLUSH              2
#define BLE_HCI_PB_FULL                     3

struct hci_add_dev_to_resolving_list {
    uint8_t addr_type;
    uint8_t addr[6];
	uint8_t reverd;
    uint8_t local_irk[16];
    uint8_t peer_irk[16];
};

/* External data structures */
extern const uint8_t g_ble_hci_le_cmd_len[BLE_HCI_NUM_LE_CMDS];

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_HCI_COMMON_ */
