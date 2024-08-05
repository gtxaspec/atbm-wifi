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

#include <assert.h>
#include "atbm_os_mem.h"

/* BLE */
#include "nimble/ble.h"
#include "nimble/ble_hci_trans.h"
#include "nimble/hci_common.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "controller/ble_ll_ctrl.h"
#include "controller/ble_ll_conn.h"
#include "ble_hs_priv.h"
#include "FreeRTOSConfig.h"
#include "hal.h"

#ifdef CFG_B2B_SIMU

#define HCI_REG 0x161010fc

#define CMD_SUCCEED		0x00
uint8_t ble_addr[6] = {0x11, 0x12, 0x13, 0x14,  0x15, 0xc0};

uint16_t op_test = 0;

/* List of OCF for Link Control commands (OGF=0x01) */

uint16_t g_sim_conn_handle = 0;
uint32_t g_sim_hci_sched = 0;

#if (CHIP_TYPE == CHIP_ARES_M)
#define SIM_SRAM           		(0x09014000)
#else
#define SIM_SRAM           		(0x0900f000)
#endif


#if GAP_CONNECT
static int hci_app_gap_event(struct ble_gap_event *event, void *arg)
{
	struct ble_gap_conn_desc desc;
	int i, rc;
	struct ble_sm_io pkey;
	struct os_mbuf *om;
	
	uint8_t local_irk[16] = {
		0xec, 0x02, 0x34, 0xa3, 0x57, 0xc8, 0xad, 0x05,
		0x34, 0x10, 0x10, 0xa6, 0x0a, 0x39, 0x7d, 0x9b
	};

	
	printf("gap event->type:%d\n", event->type);
	switch(event->type){
#if 1
		case BLE_GAP_EVENT_CONNECT:	
			if (event->connect.status == 0){
				rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
				if(desc.role == BLE_GAP_ROLE_MASTER){
					//bletest_send_acl_packet();
					HW_WRITE_REG_BIT(SIM_SRAM+0xd24,31,31,0x1);
				}
			}
			break;
			
		case BLE_GAP_EVENT_NOTIFY_RX:
        	printf("notification rx event; attr_handle=%d indication=%d "
                       "len=%d\n",
                       	event->notify_rx.attr_handle,
                       	event->notify_rx.indication,
                       	OS_MBUF_PKTLEN(event->notify_rx.om));
			om = event->notify_rx.om;
			while (om != NULL){
				printf("data:");
				for(i=0;i<om->om_len; i++){
					printf("%02X ", om->om_data[i]);
					if(0 == ((i+1)%16)){
						printf("\n");
						printf("data:");
					}
				}
				printf("\n::::\n");
				om = SLIST_NEXT(om, om_next);
			}
			printf("\n");
			break;
#endif
#if 0
			case BLE_GAP_EVENT_CONNECT: 	
				if (event->connect.status == 0) {
					rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
					assert(rc == 0);
					printf("desc.role:%d\n", desc.role);
					if(desc.role == BLE_GAP_ROLE_MASTER){
						ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID;
						ble_hs_cfg.sm_their_key_dist = 0;
						ble_hs_cfg.sm_bonding = 1;
						ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_ONLY;
						ble_hs_cfg.sm_keypress = 1;
						ble_hs_cfg.sm_mitm = 1;
						ble_hs_cfg.sm_sc = 0;
						ble_hs_pvcy_set_our_irk(local_irk);
						ble_gap_security_initiate(event->connect.conn_handle);
						printf("master smp start\n");
					}else{
						ble_hs_cfg.sm_our_key_dist = 0;
						ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID;
						ble_hs_cfg.sm_bonding = 1;
						ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_ONLY;
						ble_hs_cfg.sm_keypress = 1;
						ble_hs_cfg.sm_mitm = 1;
						ble_hs_cfg.sm_sc = 0;
						printf("slave wait smp\n");
					}
				}
				break;
	
			case BLE_GAP_EVENT_PASSKEY_ACTION:
				printf("passkey action event; action=%d",
							event->passkey.params.action);
				if(event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
					printf(" numcmp=%lu",
							   (unsigned long)event->passkey.params.numcmp);
					pkey.action = BLE_SM_IOACT_NUMCMP;
					pkey.numcmp_accept = 1;
				}
				printf("\n");
	
				if(event->passkey.params.action == BLE_SM_IOACT_INPUT){
					pkey.action = BLE_SM_IOACT_INPUT;
					pkey.passkey = 123456;
					printf("event->connect.conn_handle:%d, event->passkey.conn_handle:%d\n", 
							event->connect.conn_handle, event->passkey.conn_handle);
					ble_sm_inject_io(event->passkey.conn_handle, &pkey);
				}
				break;
#endif

		default:
			break;
	}
	
	return 0;
}
#endif

/* List of OCF for Link Control commands (OGF=0x01) */

/*
*  Command: Disconnect_Command
*  OGF	  : 0x06
*  OCF	  : 0x0001
*  Opcode : 
*  Param  : None
*  Return : Status

*/
u16 dis_handle;
u8 dis_reason;

int HCI_Disconnect_Command(){
	u16 conn_handle;
	u8 disconnect_reason;
	conn_handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	disconnect_reason = (HW_READ_REG(SIM_SRAM+0xa64)>>24) & 0xff;

//	iot_printf("HCI Command HCI_Disconnect_Command test Start ...\n");
	printf("conn_handle = %x\n",conn_handle);
	printf("disconnect_reason = %x\n",disconnect_reason);
	
	uint8_t buf[BLE_HCI_DISCONNECT_CMD_LEN];
	int rc;
    ble_hs_hci_cmd_build_disconnect(conn_handle, disconnect_reason,
                                    buf, sizeof buf);

		
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LINK_CTRL,
                                                BLE_HCI_OCF_DISCONNECT_CMD),
                                     buf, sizeof(buf));
#if LL_CHECK	
	if(rc == CMD_SUCCEED){
		iot_printf("sw_check_case10_disconnect_hci_cmd success\n");
		iot_printf(" \r\n HCI_Disconnect_Command SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_Disconnect_Command FAILED rc = %d\r\n",rc);
	}
#endif
	
//	iot_printf("HCI_Disconnect_Command test end ...\n");
	return rc;
}



/* List of OCF for Controller and Baseband commands (OGF=0x03) */

/*
 *  Command: Reset_Command
 *  OGF    : 0x03
 *  OCF    : 0x0003
 *  Opcode : 0x0c03
 *  Param  : None
 *  Return : Status
 *
 */

int  HCI_Reset_Command(void){
	int rc;
	rc =  ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND,
                                        BLE_HCI_OCF_CB_RESET),
                             NULL, 0, NULL, 0, NULL);
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n Reset_Command SUCCEED\r\n");
	}else{
		iot_printf(" \r\n Reset_Command FAILED\r\n");
	}
	
//	iot_printf("Reset_Command test end ...\n");
	return rc;
}
uint8 aptmo;

int  HCI_Write_Authenticated_Payload_Timeout_Command(){

	int rc;
	uint8_t buf[BLE_HCI_WR_AUTH_PYLD_TMO_LEN+2];
	uint8_t ack_params[BLE_HCI_WR_AUTH_PYLD_TMO_LEN];
    uint8_t ack_params_len;
	u16 conn_handle;
	u16 auth_pyld_tmo;
	struct ble_ll_conn_sm *connsm;
	conn_handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	auth_pyld_tmo = (HW_READ_REG(SIM_SRAM+0xac0)>>16) & 0xffff;
	
	ble_hs_hci_cmd_build_auth_pyld_tmo(conn_handle, auth_pyld_tmo,
                                    buf, sizeof buf);
	
	rc =  ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND,
                                        BLE_HCI_OCF_CB_WR_AUTH_PYLD_TMO),
                             buf, sizeof buf, ack_params, sizeof ack_params, &ack_params_len);
#if LL_CHECK
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n Write_Authenticated_Payload_Timeout_Command SUCCEED  conn_handle = %x\r\n",conn_handle);
		printf("sw_check_case7_write_authenticated_hci_cmd success\n");
		
	}else{
		iot_printf(" \r\n Write_Authenticated_Payload_Timeout_Command FAILED\r\n");
		iot_printf("\r\n sw_check_le_ping Write Authenticated Payload Timeout failed \r\n");
	}
#endif
	connsm = ble_ll_conn_find_active_conn(conn_handle);
	
	ble_ll_ctrl_proc_start(connsm, BLE_LL_CTRL_PROC_LE_PING);
//	iot_printf("Write_Authenticated_Payload_Timeout_Command test end ...\n");	
	return rc;
}

int  HCI_Read_Authenticated_Payload_Timeout_Command(){

    int rc;
    uint16_t handle;
    uint8_t buf[2];
    uint8_t ack_params_len;
    uint8_t rsp[BLE_HCI_RD_AUTH_PYLD_TMO_LEN];
    handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;

    put_le16(buf,handle);

    rc =  ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND,
                                        BLE_HCI_OCF_CB_RD_AUTH_PYLD_TMO),
                             buf, sizeof buf, rsp, sizeof rsp, &ack_params_len);

    if(rc == CMD_SUCCEED){
        iot_printf(" \r\n HCI_Read_Authenticated_Payload_Timeout_Command SUCCEED  conn_handle = %x,Auth_Pld_Tmt = %x\r\n",get_le16(rsp),get_le16(rsp+2));

    }else{
        iot_printf(" \r\n HCI_Read_Authenticated_Payload_Timeout_Command FAILED\r\n");
    }

    return rc;

}



/* List of OCF for Status parameters commands (OGF = 0x05) */
int
BLE_HCI_Read_RSSI_Command(void)
{

	uint16_t conn_handle;
	int8_t out_rssi;
    uint8_t buf[BLE_HCI_READ_RSSI_LEN];
    uint8_t params[BLE_HCI_READ_RSSI_ACK_PARAM_LEN];
    uint16_t params_conn_handle;
    uint8_t params_len;
    int rc;
	conn_handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;

    ble_hs_hci_cmd_build_read_rssi(conn_handle, buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_STATUS_PARAMS,
                                      BLE_HCI_OCF_RD_RSSI), buf, sizeof(buf),
                           params, sizeof(params), &params_len);

#if LL_CHECK
	if(rc == CMD_SUCCEED && (params_len == BLE_HCI_READ_RSSI_ACK_PARAM_LEN)){
		params_conn_handle = get_le16(params + 0);
		if(params_conn_handle == conn_handle){
			out_rssi = params[2];
			printf("sw_check_case12_read_rssi_hci_cmd succeed\n");
			iot_printf(" \r\n HCI_Read_RSSI SUCCEED RSSI = %d\r\n",out_rssi);
		}else{
		    iot_printf(" \r\n HCI_Read_RSSI CONN_HANDLE FAILED\r\n");
		}
    }else{
        iot_printf(" \r\n HCI_Read_RSSI FAILED\r\n");
    }
#endif

//	iot_printf("HCI_Read_RSSI test end ...\n");

    return 0;
}




/*
 *  Command: HCI_LE_Set_Event_Mask
 *  OGF    : 0x08
 *  OCF    : 0x0001
 *  Opcode : 0x2001
 *  Param  : LE_Event_Mask
 *  Return : Status
 *
 */
 
int
HCI_LE_Set_Event_Mask_Command(void)
{
//	iot_printf("HCI_LE_Set_Event_Mask test start ...\n");
	uint8_t rc;
    uint8_t buf[BLE_HCI_SET_LE_EVENT_MASK_LEN];
	
    uint64_t event_mask = 0xffffffffffffffff;
	
	//memcpy(event_mask,SIM_SRAM+0xa54,8);
    ble_hs_hci_cmd_build_le_set_event_mask(event_mask, buf, sizeof(buf));
	
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,BLE_HCI_OCF_LE_SET_EVENT_MASK), buf, sizeof(buf),NULL,0,NULL);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Event_Mask SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Set_Event_Mask FAILED\r\n");
	}
	
//	iot_printf("HCI_LE_Set_Event_Mask test end ...\n");
	return rc;
}

int
HCI_CB_Set_Event_Mask_Command(void)
{
	uint8_t rc;
    uint8_t buf[BLE_HCI_SET_LE_EVENT_MASK_LEN];

	
//	iot_printf("HCI_CB_Set_Event_Mask test start ...\n");

	ble_hs_hci_cmd_build_set_event_mask(0xffffffffffffffff, buf, sizeof buf);
	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND,BLE_HCI_OCF_CB_SET_EVENT_MASK), 
										buf, sizeof(buf),NULL,0,NULL);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_CB_Set_Event_Mask SUCCEED\r\n");
		#if LL_CHECK
		  printf("sw_check_case5_cb_set_event_hci_cmd success\n");
		#endif
	}else{
		iot_printf(" \r\n HCI_CB_Set_Event_Mask FAILED\r\n");
	}

//	iot_printf("HCI_CB_Set_Event_Mask test end ...\n");
	return rc;
}





/*
 *  Command: HCI_LE_Read_Buffer_Size
 *  OGF    : 0x08
 *  OCF    : 0x0002
 *  Opcode : 0x2002
 *  Param  : None
 *  Return : Status,
 *  		 HC_LE_ACL_Data_Packet_Length,
 *			 HC_Total_Num_LE_ACL_Data_Packets
 */
int
HCI_LE_Read_Buffer_Size_Command(void)
{
//	iot_printf("HCI_LE_Read_Buffer_Size test start ...\n");
    uint8_t ack_params[BLE_HCI_RD_BUF_SIZE_RSPLEN];
    uint8_t ack_params_len;
    int rc;

	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,BLE_HCI_OCF_LE_RD_BUF_SIZE),
						   NULL, 0,ack_params, sizeof(ack_params), &ack_params_len);

	if(rc == CMD_SUCCEED){
		iot_printf("\n Length     :0x%04x \n",get_le16(ack_params));
		iot_printf("\n Packets :0x%02x \n",ack_params[2]);
		iot_printf(" \r\n HCI_LE_Read_Buffer_Size SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Read_Buffer_Size FAILED\r\n");
	}

//	iot_printf("HCI_LE_Read_Buffer_Size test end ...\n");
	return rc;
}


/*
 *  Command: HCI_LE_READ_LOCAL_SUPPORTED_FEATURES_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0003
 *  Opcode : 0x2003
 *  Param  : None
 *  Return : Status,
 *  		 LE_Features
 */
int
HCI_LE_Read_Local_Supported_freatures_Command(void)
{

//	iot_printf("HCI_LE_READ_LOCAL_SUPPORTED_FEATURES_OCF test start ...\n");
    uint8_t ack_params[BLE_HCI_RD_LE_LOC_SUPP_FEAT_RSPLEN];
    uint8_t ack_params_len;
    int rc;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_RD_LOC_SUPP_FEAT),
                           NULL,0, ack_params, sizeof ack_params,
                           &ack_params_len);
   	if(rc == CMD_SUCCEED){
   		iot_printf("\n LE_Features	:0x%016x \n",get_le64(ack_params));
		iot_printf(" \r\n HCI_LE_READ_LOCAL_SUPPORTED_FEATURES_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_READ_LOCAL_SUPPORTED_FEATURES_OCF FAILED\r\n");
	}

    if (ack_params_len != BLE_HCI_RD_LE_LOC_SUPP_FEAT_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }

//	iot_printf("HCI_LE_READ_LOCAL_SUPPORTED_FEATURES_OCF test end ...\n");
	return rc;

}


/*
 *  Command: HCI_LE_SET_RANDOM_ADDRESS_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0005
 *  Opcode : 0x2005
 *  Param  : Random_Address
 *  Return : Status,
 */
int
HCI_LE_Set_Random_Address_Command(void)
{
//	iot_printf("HCI_LE_SET_RANDOM_ADDRESS_OCF test start ...\n");

    int rc;
	uint8_t buf[BLE_HCI_SET_RAND_ADDR_LEN];

	//memcpy(buf, ble_addr, BLE_DEV_ADDR_LEN);
	memcpy(buf, SIM_SRAM+0xa0c, BLE_DEV_ADDR_LEN);
	int i;
	printf("random_device_addr = ");
	for(i=0;i <= 5;i++){
		printf("%x ",buf[i]);
		}
	printf("\n");

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_RAND_ADDR), buf, sizeof(buf));
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_SET_RANDOM_ADDRESS_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_SET_RANDOM_ADDRESS_OCF FAILED\r\n");
	}
//	iot_printf("HCI_LE_SET_RANDOM_ADDRESS_OCF test end ...\n");
	return rc;
}


/*
 *  Command: HCI_LE_SET_ADVERTISING_PARAMETERS_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0006
 *  Opcode : 0x2006
 *  Param  : Advertising_Interval_Min,
			 Advertising_Interval_Max,
			 Advertising_Type,
			 Own_Address_Type,
			 Peer_Address_Type,
			 Peer_Address,
			 Advertising_Channel_Map,
			 Advertising_Filter_Policy
 *  Return : Status,
 */
int
HCI_LE_Set_Advertising_Parameters_Command(void)
{
//	iot_printf("HCI_LE_SET_ADVERTISING_PARAMETERS_OCF test start ...\n");
	int rc;
	struct hci_adv_params adv;
	uint8_t buf[BLE_HCI_SET_ADV_PARAM_LEN];
	int i;
	adv.adv_itvl_min = 0x0020;
	adv.adv_itvl_max = 0x4000;
	adv.adv_type = 0x00;
	adv.own_addr_type = 0x00;
	adv.peer_addr_type = 0x00;
	adv.adv_channel_map = 0x07;
	adv.adv_filter_policy = 0x00;
	
	memcpy(&adv, SIM_SRAM+0xa00, sizeof(struct hci_adv_params));	
	//memcpy(adv.peer_addr, ble_addr, BLE_DEV_ADDR_LEN);
	iot_printf("adv_type = %x \r\n",adv.adv_type);
	iot_printf("adv_channel_map = %x \r\n",adv.adv_channel_map);
	iot_printf("own_addr_type = %x \r\n",adv.own_addr_type);
	iot_printf("peer_addr_type = %x \r\n",adv.peer_addr_type);
	iot_printf("adv_filter_policy = %x \r\n",adv.adv_filter_policy);
	iot_printf("adv_itvl_min = %x \r\n",adv.adv_itvl_min);
	iot_printf("adv_itvl_max = %x \r\n",adv.adv_itvl_max);
	printf("peer_addr : ");
	for(i=0;i<6;i++){
		printf("%x ",adv.peer_addr[i]);
	}
	printf("\n");
	rc = ble_hs_hci_cmd_build_le_set_adv_params(&adv, buf, sizeof buf);
	
	if (!rc) {
		rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
													BLE_HCI_OCF_LE_SET_ADV_PARAMS),
										 buf, sizeof(buf));
		if(rc == CMD_SUCCEED){
			iot_printf(" \r\n HCI_LE_SET_ADVERTISING_PARAMETERS_OCF SUCCEED\r\n");
		}else{
		
			iot_printf(" \r\n HCI_LE_SET_ADVERTISING_PARAMETERS_OCF FAILED rc = %x\r\n",rc);
		}

	}
//	iot_printf("HCI_LE_SET_ADVERTISING_PARAMETERS_OCF test end ...\n");
	 
	return rc;
}


/*
 *  Command: HCI_LE_READ_ADVERTISING_CHANNEL_TX_POWER_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0007
 *  Opcode : 0x2007
 *  Param  : None
 *  Return : Status,
			 Transmit_Power_Level
 */
int
HCI_LE_Read_Advertising_Channel_Tx_Power_Command(void)
{

//	iot_printf("HCI_LE_READ_ADVERTISING_CHANNEL_TX_POWER_OCF test start ...\n");

    uint8_t params_len;
    int rc;
	int8_t out_tx_pwr;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR),
                           NULL, 0,&out_tx_pwr, 1, &params_len);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_READ_ADVERTISING_CHANNEL_TX_POWER_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_READ_ADVERTISING_CHANNEL_TX_POWER_OCF FAILED\r\n");
	}

    if (params_len != 1 || out_tx_pwr < BLE_HCI_ADV_CHAN_TXPWR_MIN || out_tx_pwr > BLE_HCI_ADV_CHAN_TXPWR_MAX) {
        BLE_HS_LOG(WARN, "advertiser txpwr out of range\n");
    }
//	iot_printf("HCI_LE_READ_ADVERTISING_CHANNEL_TX_POWER_OCF test end ...\n");

    return 0;

}



/*
 *  Command: HCI_LE_SET_ADVERTISING_DATA_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0008
 *  Opcode : 0x2008
 *  Param  : Advertising_Data_Length,
			 Advertising_Data
 *  Return : Status,

 */
int
HCI_LE_Set_Advertising_Data(void){

//	iot_printf("HCI_LE_SET_ADVERTISING_DATA_OCF test start ...\n");

    u8 data[31];
	u8  len;
    int rc;
    uint16_t opcode;
    uint8_t buf[BLE_HCI_SET_ADV_DATA_LEN];
	
	len = (HW_READ_REG(SIM_SRAM+0xab8)>>24) & 0xff;
	memcpy(data,SIM_SRAM+0xa9c,len);
	
    opcode = BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADV_DATA);
    rc = ble_hs_hci_cmd_build_le_set_adv_data(data, len, buf, sizeof buf);
    assert(rc == 0);
    rc =  ble_hs_hci_cmd_tx_empty_ack(opcode, buf, sizeof(buf));

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_SET_ADVERTISING_DATA_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_SET_ADVERTISING_DATA_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_SET_ADVERTISING_DATA_OCF test end ...\n");

	return rc;

}


/*
 *  Command: HCI_LE_SET_SCAN_RESPONSE_DATA_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0009
 *  Opcode : 0x2009
 *  Param  : Scan_Response_Data_Length,
			 Scan_Response_Data
 *  Return : Status,
 */
int
HCI_LE_Set_Scan_Response_Data(void){

//	iot_printf("HCI_LE_SET_SCAN_RESPONSE_DATA_OCF test start ...\n");

	u8 data[31];
	u8  len;
    int rc;
    uint16_t opcode;
    uint8_t buf[BLE_HCI_SET_SCAN_RSP_DATA_LEN];
	
	len = (HW_READ_REG(SIM_SRAM+0xae0)>>24) & 0xff;
	memcpy(data,SIM_SRAM+0xac4,len);

    opcode = BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA);

    rc = ble_hs_hci_cmd_build_le_set_scan_rsp_data(data, len, buf, sizeof buf);
    assert(rc == 0);
    rc =  ble_hs_hci_cmd_tx_empty_ack(opcode, buf, sizeof(buf));

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_SET_SCAN_RESPONSE_DATA_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_SET_SCAN_RESPONSE_DATA_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_SET_SCAN_RESPONSE_DATA_OCF test end ...\n");

	return rc;

}


/*
 *  Command: HCI_LE_SET_ADVERTISE_ENABLE_OCF
 *  OGF    : 0x08
 *  OCF    : 0x000A
 *  Opcode : 0x200A
 *  Param  : Advertising_Enable
 *  Return : Status,
 */
int
HCI_LE_Set_Adevertise_Enable(void){

//	iot_printf("HCI_LE_SET_ADVERTISE_ENABLE_OCF test start ...\n");

	uint8_t rc;
	uint8_t enable =  1;
	uint8_t disable = 0;
	u32 sim_params;
	sim_params = HW_READ_REG(HCI_REG);
	enable = (sim_params >> 30) & 0x1;
	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADV_ENABLE),&enable, 1);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_SET_ADVERTISE_ENABLE_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_SET_ADVERTISE_ENABLE_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_SET_ADVERTISE_ENABLE_OCF test end ...\n");

	return rc;

}



/*
 *  Command: HCI_LE_SET_SCAN_PARAMETERS_OCF
 *  OGF    : 0x08
 *  OCF    : 0x000B
 *  Opcode : 0x200B
 *  Param  : LE_Scan_Type,
			 LE_Scan_Interval,
		 	 LE_Scan_Window,
			 Own_Address_Type,
             Scanning_Filter_Policy
 *  Return : Status,
 */
int
HCI_LE_Set_Scan_Parameters(void){

//	iot_printf("HCI_LE_SET_SCAN_PARAMETERS_OCF test start ...\n");

    int rc;
    uint8_t own_addr_type;
    uint8_t buf[BLE_HCI_SET_SCAN_PARAM_LEN];
	
    own_addr_type = BLE_HCI_ADV_OWN_ADDR_PUBLIC;
	struct hci_scan_params scan;
	memcpy(&scan,SIM_SRAM+0xa14,sizeof(struct hci_scan_params));
	iot_printf("scan_type = %x \r\n",scan.scan_type);
	iot_printf("scan_filter_policy = %x \r\n",scan.scan_filter_policy);
	iot_printf("own_addr_type = %x \r\n",scan.own_addr_type);
	iot_printf("scan_itvl = %x \r\n",scan.scan_itvl);
	iot_printf("scan_window = %x \r\n",scan.scan_window);
	
    //rc = ble_hs_hci_cmd_build_le_set_scan_params(BLE_HCI_SCAN_TYPE_PASSIVE,
    //											 (700000 / BLE_HCI_SCAN_ITVL),
    //											 (700000 / BLE_HCI_SCAN_ITVL),
    //                                             own_addr_type,
    //                                             BLE_HCI_SCAN_FILT_NO_WL,
    //                                             buf, sizeof buf);
	rc = ble_hs_hci_cmd_build_le_set_scan_params(scan.scan_type,
    											 (scan.scan_itvl),
    											 (scan.scan_window),
                                                 scan.own_addr_type,
                                                 scan.scan_filter_policy,
                                                 buf, sizeof buf);
    assert(rc == 0);

    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_SCAN_PARAMS),buf, sizeof(buf));

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_SET_SCAN_PARAMETERS_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_SET_SCAN_PARAMETERS_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_SET_SCAN_PARAMETERS_OCF test end ...\n");

	return rc;



}

/*
 *  Command: HCI_LE_SET_SCAN_ENABLE_OCF
 *  OGF    : 0x08
 *  OCF    : 0x000C
 *  Opcode : 0x200C
 *  Param  : LE_Scan_Enable,
			 Filter_Duplicates
 *  Return : Status,
 */
int
HCI_LE_Set_Scan_Enable(void){

//    iot_printf("HCI_LE_SET_SCAN_ENABLE_OCF test start ...\n");

    uint8_t buf[BLE_HCI_SET_SCAN_ENABLE_LEN];
	uint8_t enable = 0x01;
	uint8_t filter_dups = 0x01;
	int rc;
	u32 sim_params;
	sim_params = HW_READ_REG(HCI_REG);
	enable = (sim_params >> 29) & 0x1;
	filter_dups = (sim_params >> 28) & 0x1;

    ble_hs_hci_cmd_build_le_set_scan_enable(!!enable, !!filter_dups,
                                            buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx_empty_ack(
            BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_SCAN_ENABLE),
            buf, sizeof(buf));

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_SET_SCAN_ENABLE_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_SET_SCAN_ENABLE_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_SET_SCAN_ENABLE_OCF test end ...\n");

	return rc;


}


/*
 *  Command: HCI_LE_CREATE_CONNECTION_OCF
 *  OGF    : 0x08
 *  OCF    : 0x000D
 *  Opcode : 0x200D
 *  Param  : LE_Scan_Interval,
			 LE_Scan_Window,
			 Initiator_Filter_Policy,
			 Peer_Address_Type,
			 Peer_Address,
			 Own_Address_Type,
			 Conn_Interval_Min,
			 Conn_Interval_Max,
			 Conn_Latency,
			 Supervision_Timeout,
			 Minimum_CE_Length,
			 Maximum_CE_Length
 *  Return : None,
 */
int
HCI_LE_Create_Connection(void){

    int rc;
    uint8_t buf[BLE_HCI_CREATE_CONN_LEN];
	struct hci_create_conn hcc;
	hcc.scan_itvl = 0x0004;
    hcc.scan_window = 0x0004;
    hcc.filter_policy = 0x00;
    hcc.peer_addr_type = 0x00;
    hcc.own_addr_type = 0x00;
    hcc.conn_itvl_min = 0x0006;
    hcc.conn_itvl_max = 0x0c80;
    hcc.conn_latency = 0x01f3;
    hcc.supervision_timeout = 0x0c80;
    hcc.min_ce_len = 0x0000;
    hcc.max_ce_len = 0xffff;
    memcpy(hcc.peer_addr, ble_addr, BLE_DEV_ADDR_LEN);
	//////////////////////for sim
	hcc.scan_itvl = HW_READ_REG(SIM_SRAM+0xa18) & 0xffff;
	hcc.scan_window = (HW_READ_REG(SIM_SRAM+0xa18)>>16) & 0xffff;
	hcc.filter_policy = HW_READ_REG(SIM_SRAM+0xa44) & 0xff;
	hcc.peer_addr_type = (HW_READ_REG(SIM_SRAM+0xa44)>>8) & 0xff;
	hcc.own_addr_type = (HW_READ_REG(SIM_SRAM+0xa44)>>16) & 0xff;
	memcpy(hcc.peer_addr, SIM_SRAM+0xa0c, BLE_DEV_ADDR_LEN);
	memcpy((char*)(&hcc)+14,SIM_SRAM+0xa48,12);
	
	//iot_printf("hcc.scan_itvl = %x \r\n",*(u32 *)SIM_SRAM+0x0c);
	iot_printf("scan_itvl = %x \r\n",hcc.scan_itvl);
	iot_printf("scan_window = %x \r\n",hcc.scan_window);
	iot_printf("filter_policy = %x \r\n",hcc.filter_policy);
	iot_printf("peer_addr_type = %x \r\n",hcc.peer_addr_type);
	iot_printf("own_addr_type = %x \r\n",hcc.own_addr_type);
	iot_printf("conn_itvl_min = %x \r\n",hcc.conn_itvl_min);
	iot_printf("conn_itvl_max = %x \r\n",hcc.conn_itvl_max);
	iot_printf("conn_latency = %x \r\n",hcc.conn_latency);
	iot_printf("supervision_timeout = %x \r\n",hcc.supervision_timeout);
	iot_printf("min_ce_len = %x \r\n",hcc.min_ce_len);
	iot_printf("max_ce_len = %x \r\n",hcc.max_ce_len);
	
	int i;
	printf("peer_addr : ");
	for(i=0;i<6;i++){
		printf("%x ",hcc.peer_addr[i]);
	}
	printf("\n");
#if GAP_CONNECT
	struct ble_gap_conn_params conn_params;
	ble_addr_t gap_peer_addr;
	conn_params.itvl_max = 6;
	conn_params.itvl_min = 6;
	conn_params.latency = hcc.conn_latency;
	conn_params.max_ce_len = hcc.max_ce_len;
	conn_params.min_ce_len = hcc.min_ce_len;
	conn_params.scan_itvl = hcc.scan_itvl;
	conn_params.scan_window = hcc.scan_window;
	conn_params.supervision_timeout = hcc.supervision_timeout;
	memcpy(gap_peer_addr.val, hcc.peer_addr, 6);
	gap_peer_addr.type = hcc.peer_addr_type;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

	rc = ble_gap_connect(hcc.own_addr_type, &gap_peer_addr, BLE_HS_FOREVER, &conn_params,
					hci_app_gap_event, NULL);


#else
    rc = ble_hs_hci_cmd_build_le_create_connection(&hcc, buf, sizeof buf);
    if (!rc) {
        rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                                    BLE_HCI_OCF_LE_CREATE_CONN),
                                         buf, sizeof(buf));
		if(rc == CMD_SUCCEED){
			
			iot_printf(" \r\n HCI_LE_CREATE_CONNECTION_OCF SUCCEED\r\n");
		}else{
			iot_printf(" \r\n HCI_LE_CREATE_CONNECTION_OCF FAILED\r\n");
		}
    }
#endif

//	iot_printf("HCI Command HCI_LE_CREATE_CONNECTION_OCF test end ...\n");
    return rc;
}

/*
 *  Command: HCI_LE_CREATE_CONNECTION_CANCEL_OCF
 *  OGF    : 0x08
 *  OCF    : 0x000E
 *  Opcode : 0x200E
 *  Param  : NONE
 *  Return : Status,
 */
int
HCI_LE_Create_Connection_Cancel(void){

    int rc;

    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                            BLE_HCI_OCF_LE_CREATE_CONN_CANCEL),
                                     NULL, 0);
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_CREATE_CONNECTION_CANCEL_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_CREATE_CONNECTION_CANCEL_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_CREATE_CONNECTION_CANCEL_OCF test end ...\n");

    return rc;

}

/*
 *  Command: HCI_LE_READ_WHITE_LIST_SIZE_OCF
 *  OGF    : 0x08
 *  OCF    : 0x000F
 *  Opcode : 0x200F
 *  Param  : NONE
 *  Return : Status,
 			 White_List_Size
 */
int
HCI_LE_Read_White_List_Size(void){

	int rc;
	uint8_t params_len;
	int8_t white_list_size;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_RD_WHITE_LIST_SIZE),
                           NULL, 0,&white_list_size, 1, &params_len);
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_READ_WHITE_LIST_SIZE_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_READ_WHITE_LIST_SIZE_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_READ_WHITE_LIST_SIZE_OCF test end ...\n");
	return rc;
}

/*
 *  Command: HCI_LE_CLEAR_WHITE_LIST_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0010
 *  Opcode : 0x2010
 *  Param  : NONE
 *  Return : Status,
 */
int
HCI_LE_Clear_White_List(void){

    int rc;

    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                            BLE_HCI_OCF_LE_CLEAR_WHITE_LIST),
                                     NULL, 0);
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_CLEAR_WHITE_LIST_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_CLEAR_WHITE_LIST_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_CLEAR_WHITE_LIST_OCF test end ...\n");
    return rc;

}

/*
 *  Command: HCI_LE_ADD_DEVICE_TO_WHITE_LIST_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0011
 *  Opcode : 0x2011
 *  Param  : Address_Type,
			 Address
 *  Return : Status,
 */
int
HCI_LE_Add_Device_To_White_List(void){

    int rc;
    uint8_t buf[BLE_HCI_ADD_WHITE_LIST_LEN];
	uint8_t addr_type;
	addr_type = (HW_READ_REG(SIM_SRAM+0xa10) >> 16) & 0xff;
	memcpy(ble_addr, SIM_SRAM+0xa0c, BLE_DEV_ADDR_LEN);
	int i;
	printf("addr_type = %0x\n",addr_type);
	printf("HCI_LE_Add_Device_To_White_List:ble_addr = ");
	for(i=0;i <= 5;i++){
		printf("%0x ",ble_addr[i]);
		}	
	printf("\n");
    rc = ble_hs_hci_cmd_build_le_add_to_whitelist(ble_addr, addr_type, buf,
                                                sizeof buf);
    if (!rc) {
        rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                                    BLE_HCI_OCF_LE_ADD_WHITE_LIST),
                                         buf, sizeof(buf));
		if(rc == CMD_SUCCEED){
			iot_printf(" \r\n HCI_LE_ADD_DEVICE_TO_WHITE_LIST_OCF SUCCEED\r\n");
		}else{
			iot_printf(" \r\n HCI_LE_ADD_DEVICE_TO_WHITE_LIST_OCF FAILED\r\n");
		}
    }

//	iot_printf("HCI Command HCI_LE_ADD_DEVICE_TO_WHITE_LIST_OCF test end ...\n");
    return rc;
}



/*
 *  Command: HCI_LE_REMOVE_DEVICE_FROM_WHITE_LIST_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0012
 *  Opcode : 0x2012
 *  Param  : Address_Type,
			 Address
 *  Return : Status,
 */
HCI_LE_Remove_Device_From_White_list(void){

	int rc;
    uint8_t buf[BLE_HCI_ADD_WHITE_LIST_LEN];
	uint8_t addr_type;
	addr_type = (HW_READ_REG(SIM_SRAM+0xa10) >> 16) & 0xff;
	memcpy(ble_addr, SIM_SRAM+0xa0c, BLE_DEV_ADDR_LEN);
	int i;
	iot_printf("addr_type = %x \r\n",addr_type);
	for(i=0;i <= 5;i++){
		iot_printf("ble_addr[%d] = %x \r\n",i,ble_addr[i]);
		}	

    rc = ble_hs_hci_cmd_build_le_add_to_whitelist(ble_addr, addr_type, buf,sizeof buf);

    if (!rc) {
        rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                                    BLE_HCI_OCF_LE_RMV_WHITE_LIST),
                                         buf, sizeof(buf));
		if(rc == CMD_SUCCEED){
			iot_printf(" \r\n HCI_LE_REMOVE_DEVICE_FROM_WHITE_LIST_OCF SUCCEED\r\n");
		}else{
			iot_printf(" \r\n HCI_LE_REMOVE_DEVICE_FROM_WHITE_LIST_OCF FAILED\r\n");
		}
    }
//	iot_printf("HCI Command HCI_LE_REMOVE_DEVICE_FROM_WHITE_LIST_OCF test end ...\n");

	return rc;
}



/*
 *  Command: HCI_LE_CONNECTION_UPDATE_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0013
 *  Opcode : 0x2013
 *  Param  : Connection_Handle,
   			 Conn_Interval_Min,
   			 Conn_Interval_Max,
   			 Conn_Latency,
   			 Supervision_Timeout,
   			 Minimum_CE_Length,
   			 Maximum_CE_Length
 *  Return : NONE,
 */

int
HCI_LE_Connect_Updata(void){

    uint8_t cmd[BLE_HCI_CONN_UPDATE_LEN];
    int rc;
	struct hci_conn_update hcu;
    hcu.handle = 0x0001;
    hcu.conn_itvl_min = 0x0002;
    hcu.conn_itvl_max = 0x0002;
    hcu.conn_latency = 0;
    hcu.supervision_timeout = 0x0c80;
    hcu.min_ce_len = 4;
    hcu.max_ce_len = 4;

    hcu.handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	// memcpy((char*)(&hcu)+2,SIM_SRAM+0xa48,12);
	
	iot_printf("hcc.handle = %x \r\n",hcu.handle);
	iot_printf("hcc.conn_itvl_min = %x \r\n",hcu.conn_itvl_min);
	iot_printf("hcc.conn_itvl_max = %x \r\n",hcu.conn_itvl_max);
	iot_printf("hcc.conn_latency = %x \r\n",hcu.conn_latency);
	iot_printf("hcc.supervision_timeout = %x \r\n",hcu.supervision_timeout);
	iot_printf("hcc.min_ce_len = %x \r\n",hcu.min_ce_len);
	iot_printf("hcc.max_ce_len = %x \r\n",hcu.max_ce_len);

    put_le16(cmd + 0, hcu.handle);
    put_le16(cmd + 2, hcu.conn_itvl_min);
    put_le16(cmd + 4, hcu.conn_itvl_max);
    put_le16(cmd + 6, hcu.conn_latency);
    put_le16(cmd + 8, hcu.supervision_timeout);
    put_le16(cmd + 10, hcu.min_ce_len);
    put_le16(cmd + 12, hcu.max_ce_len);

    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,BLE_HCI_OCF_LE_CONN_UPDATE),cmd, sizeof(cmd));
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_CONNECTION_UPDATE_OCF SUCCEED\r\n");
		#if LL_CHECK
		  printf("sw_check_case6_conn_update_hci_cmd success\n");
	    #endif
	}else{
		iot_printf(" \r\n HCI_LE_CONNECTION_UPDATE_OCF FAILED\r\n");
	}

	iot_printf("HCI Command HCI_LE_CONNECTION_UPDATE_OCF test end ...\n");

	return rc;
	
}

/*
 *  Command: HCI_LE_SET_HOST_CHANNEL_CLASSIFICATION_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0014
 *  Opcode : 0x2014
 *  Param  : Channel_Map
 *  Return : Status,
 */
int
HCI_LE_Set_Host_Channel_Classification(void){

	uint8_t rc;
    uint8_t buf[BLE_HCI_SET_HOST_CHAN_CLASS_LEN];
    uint8_t channel_map[BLE_HCI_SET_HOST_CHAN_CLASS_LEN];

	channel_map[0] = 0x1f;
	channel_map[1] = 0x2b;
	channel_map[2] = 0x0a;
	channel_map[3] = 0x00;
	channel_map[4] = 0x00;

    // memcpy(buf, SIM_SRAM+0xa60, BLE_HCI_SET_HOST_CHAN_CLASS_LEN);
	    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_HOST_CHAN_CLASS),
                                 	buf, sizeof(buf));

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_SET_HOST_CHANNEL_CLASSIFICATION_OCF SUCCEED\r\n");
	  #if LL_CHECK
		printf("sw_check_case0_set_host_channel_hci_cmd success\n");
	  #endif
	}else{
		iot_printf(" \r\n HCI_LE_SET_HOST_CHANNEL_CLASSIFICATION_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_SET_HOST_CHANNEL_CLASSIFICATION_OCF test end ...\n");

	return rc;
	
}

/*
 *  Command: HCI_LE_READ_CHANNEL_CLASSIFICATION_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0015
 *  Opcode : 0x2015
 *  Param  : Connection_Handle
 *  Return : Status,
			 Connection_Handle,
			 Channel_Map,
 */
int
HCI_LE_Read_Channel_Map(void){

    int rc;
    uint8_t buf[BLE_HCI_RD_CHANMAP_LEN];
    uint8_t rspbuf[BLE_HCI_RD_CHANMAP_RSP_LEN];
    uint8_t rsplen;
    uint8_t handle;
	int i;
	
	handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;

    put_le16(buf, handle);
	
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_CHAN_MAP),
                        buf, sizeof(buf), rspbuf, BLE_HCI_RD_CHANMAP_RSP_LEN, &rsplen);

	printf("read channel map:");
	for(i=0; i<BLE_HCI_RD_CHANMAP_RSP_LEN; i++){
		printf("%02X ", rspbuf[i]);
	}
	printf("\n");
	
	if (rsplen != BLE_HCI_RD_CHANMAP_RSP_LEN) {
        return BLE_HS_ECONTROLLER;
    }

	if(rc == CMD_SUCCEED){
		#if BLE_SIM_CMD_MODE
	       HW_WRITE_REG_BIT(SIM_SRAM+0xd24,10,10,0x1); // wait Read_Channel_Map SUCCEED
        #endif
		
		#if LL_CHECK
		  printf("sw_check_case0_read_channel_map_hci_cmd success\n");
	    #endif
		iot_printf(" \r\n HCI_LE_READ_CHANNEL_CLASSIFICATION_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_READ_CHANNEL_CLASSIFICATION_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_READ_CHANNEL_CLASSIFICATION_OCF test end ...\n");
	
    return rc;

}


/*
 *  Command: HCI_LE_READ_REMOTE_FEATURES_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0016
 *  Opcode : 0x2016
 *  Param  : Connection_Handle
 *  Return : NONE
 */
int
HCI_LE_Read_Remote_Features(void){

    uint8_t buf[BLE_HCI_CONN_RD_REM_FEAT_LEN];
	uint8_t rc;
	uint16_t handle = 0x0001;

	handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	iot_printf("handle:0x%X\n", handle);

    put_le16(buf, handle);

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_REM_FEAT),
                            buf, sizeof(buf), NULL, 0, NULL);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_READ_REMOTE_FEATURES_OCF SUCCEED\r\n");
		#if LL_CHECK
		  printf("sw_check_case5_read_remote_feature_hci_cmd success\n");
		#endif
	}else{
		iot_printf(" \r\n HCI_LE_READ_REMOTE_FEATURES_OCF FAILED,rc:%d\r\n", rc);
	}

//	iot_printf("HCI Command HCI_LE_READ_REMOTE_FEATURES_OCF test end ...\n");

	return rc;

}


/*
 *  Command: HCI_LE_ENCRYPT_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0017
 *  Opcode : 0x2017
 *  Param  : Key,
			 Plaintext_Data
 *  Return : NONE
 */

int
HCI_LE_Encrypt(void){
	int rc;
	uint8_t buf[BLE_HCI_LE_ENCRYPT_LEN];
	uint8_t rspbuf[16];
	uint8_t rsplen;

	uint8_t key[16] =
	{
		0x4c, 0x68, 0x38, 0x41, 0x39, 0xf5, 0x74, 0xd8,
		0x36, 0xbc, 0xf3, 0x4e, 0x9d, 0xfb, 0x01, 0xbf
	};

	/* Plaint text: 0x0213243546576879acbdcedfe0f10213 */
	uint8_t pt[16] =
	{
		0x02, 0x13, 0x24, 0x35, 0x46, 0x57, 0x68, 0x79,
		0xac, 0xbd, 0xce, 0xdf, 0xe0, 0xf1, 0x02, 0x13
	};

	memcpy(key,SIM_SRAM+0xa68,BLE_ENC_BLOCK_SIZE);
	memcpy(pt, SIM_SRAM+0xa78, BLE_ENC_BLOCK_SIZE);

	swap_buf(buf, key, BLE_ENC_BLOCK_SIZE);
	swap_buf(buf + BLE_ENC_BLOCK_SIZE, pt, BLE_ENC_BLOCK_SIZE);
	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
									 BLE_HCI_OCF_LE_ENCRYPT),
						  buf, sizeof(buf), rspbuf, 16, &rsplen);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_ENCRYPT_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_ENCRYPT_OCF FAILED\r\n");
	}

	if (rsplen != 16) {
	   return BLE_HS_ECONTROLLER;
	}


//	iot_printf("HCI Command HCI_LE_ENCRYPT_OCF test end ...\n");
	return rc;


}

/*
 *  Command: HCI_LE_RAND_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0018
 *  Opcode : 0x2018
 *  Param  : NONE
 *  Return : Status,
			 Random_Number
 */
int
HCI_LE_Rand(void){

    uint8_t rsp_buf[BLE_HCI_LE_RAND_LEN];
    uint8_t params_len;
    int rc;


    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RAND),
                           NULL, 0, rsp_buf, sizeof rsp_buf, &params_len);


	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_RAND_OCF SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_RAND_OCF FAILED\r\n");
	}

    if (params_len != sizeof rsp_buf) {
        return BLE_HS_ECONTROLLER;
    }

//	iot_printf("HCI Command HCI_LE_RAND_OCF test end ...\n");
	return rc;
}


/*
 *  Command: HCI_LE_START_ENCRYPTION_OCF
 *  OGF    : 0x08
 *  OCF    : 0x0019
 *  Opcode : 0x2019
 *  Param  : Connection_Handle,
			 Random_Number,
			 Encrypted_Diversifier,
			 Long_Term_Key
 *  Return : NONE
 */
int
HCI_LE_Start_Encryption(void){

    int rc;
    uint8_t buf[BLE_HCI_LE_START_ENCRYPT_LEN];

	struct hci_start_encrypt cmd;

	uint8_t term_key[16] =
	{
		0x4c, 0x68, 0x38, 0x41, 0x39, 0xf5, 0x74, 0xd8,
		0x36, 0xbc, 0xf3, 0x4e, 0x9d, 0xfb, 0x01, 0xbf
	};
    //cmd.connection_handle = 0x0001;
    cmd.encrypted_diversifier = 0x0000000000000000;
    cmd.random_number = 0x0000;
	// memcpy(cmd.long_term_key, term_key, 16);
	
    cmd.connection_handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
    //cmd.encrypted_diversifier = (HW_READ_REG(SIM_SRAM+0xa5c)>>16) & 0xffff;
	//memcpy(cmd.random_number,SIM_SRAM+0xa88,8);
	memcpy(cmd.long_term_key,SIM_SRAM+0xa68,BLE_ENC_BLOCK_SIZE);

    ble_hs_hci_cmd_build_le_start_encrypt(&cmd, buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                                BLE_HCI_OCF_LE_START_ENCRYPT),
                                          buf, sizeof(buf));

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_START_ENCRYPTION_OCF SUCCEED\r\n");
		#if LL_CHECK
		  printf("sw_check_case8_start_enc_hci_cmd success\n");
		#endif
	}else{
		iot_printf(" \r\n HCI_LE_START_ENCRYPTION_OCF FAILED\r\n");
	}

//	iot_printf("HCI Command HCI_LE_START_ENCRYPTION_OCF test end ...\n");

    return rc;

}

/*
 *  test the complete procedure
 *  1.calculate ltk
 *  2.use ltk encryption
 *  return 0:succ
 */
static int 
ble_test_smp(){
    int rc;
    uint16_t connection_handle;
	connection_handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
    rc = ble_sm_pair_initiate(connection_handle);
	if(rc){
        printf("error SMP\n");
	}
	return rc;
}

/*
 *  Command: HCI_LE_LONG_TERM_KEY_REQUEST_REPLY_OCF
 *  OGF    : 0x08
 *  OCF    : 0x001A
 *  Opcode : 0x201A
 *  Param  : Connection_Handle,
			 Long_Term_Key
 *  Return : Status,
			 Connection_Handle
 */
int
HCI_LE_Long_Term_Key_Request_Reply(void){


    struct hci_lt_key_req_reply hkr;
	/* LTK 0x4C68384139F574D836BCF34E9DFB01BF */
	uint8_t bletest_LTK[16] =
	{
	    0x4C,0x68,0x38,0x41,0x39,0xF5,0x74,0xD8,
	    0x36,0xBC,0xF3,0x4E,0x9D,0xFB,0x01,0xBF
	};
    uint16_t ack_conn_handle;
    uint8_t buf[BLE_HCI_LT_KEY_REQ_REPLY_LEN];
    uint8_t ack_params_len;
    int rc;

    hkr.conn_handle = 0x0001; /* Connection_Handle */
    hkr.conn_handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	memcpy(hkr.long_term_key,SIM_SRAM+0xa68,BLE_ENC_BLOCK_SIZE);
	
    // swap_buf(hkr.long_term_key, (uint8_t *)bletest_LTK, 16);

    ble_hs_hci_cmd_build_le_lt_key_req_reply(&hkr, buf, sizeof buf);
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_LT_KEY_REQ_REPLY),
                           buf, sizeof(buf), &ack_conn_handle,
                           sizeof(ack_conn_handle), &ack_params_len);

 	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_LONG_TERM_KEY_REQUEST_REPLY_OCF SUCCEED\r\n");
		#if LL_CHECK
		  printf("sw_check_case8_ltk_req_hci_cmd success\n");
		#endif
	}else{
		iot_printf(" \r\n HCI_LE_LONG_TERM_KEY_REQUEST_REPLY_OCF FAILED\r\n");
	}
    if (ack_params_len != BLE_HCI_LT_KEY_REQ_REPLY_ACK_PARAM_LEN) {
        return -1;
    }

    if (le16toh(ack_conn_handle) != hkr.conn_handle) {
        return -1;
    }
    return rc;
}

/*
 *  Command: HCI_LE_LONG_TERM_KEY_REQUEST_NEG_REPLY_OCF
 *  OGF    : 0x08
 *  OCF    : 0x001B
 *  Opcode : 0x201B
 *  Param  : Connection_Handle,
 *  Return : Status,
			 Connection_Handle
 */
int
HCI_LE_Long_Term_Key_Request_Negative_Reply(void){

    int rc;
    uint8_t buf[sizeof(uint16_t)];
    uint16_t ack_conn_handle;
    uint8_t rsplen;
	uint16_t handle = 0x0001;
    handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;

    put_le16(buf, handle);
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_LT_KEY_REQ_NEG_REPLY),
                           buf, sizeof(uint16_t), &ack_conn_handle, 2, &rsplen);


	if(rc == CMD_SUCCEED){
		if (rsplen != 2) {
            rc = -1;
        }else{
			iot_printf(" \r\n HCI_LE_LONG_TERM_KEY_REQUEST_NEG_REPLY_OCF SUCCEED\r\n");
		}

	}else{
		iot_printf(" \r\n HCI_LE_LONG_TERM_KEY_REQUEST_NEG_REPLY_OCF FAILED\r\n");
	}

    return rc;

}


/*
 *  Command: HCI_LE_READ_SUPPORTED_STATES_OCF
 *  OGF    : 0x08
 *  OCF    : 0x001C
 *  Opcode : 0x201C
 *  Param  : NONE,
 *  Return : Status,
			 LE_States
 */
int
HCI_LE_Read_Supported_States(void){

    int rc;;
    uint8_t rspbuf[BLE_HCI_RD_SUPP_STATES_RSPLEN];
    uint8_t rsplen;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_SUPP_STATES),
                            NULL, 0, rspbuf, BLE_HCI_RD_SUPP_STATES_RSPLEN, &rsplen);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_READ_SUPPORTED_STATES_OCF SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_READ_SUPPORTED_STATES_OCF FAILED\r\n");
	}


    if (rsplen != BLE_HCI_RD_SUPP_STATES_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }
    return rc;

}


/*
 *  Command: HCI_LE_RECEIVER_TEST_OCF
 *  OGF    : 0x08
 *  OCF    : 0x001D
 *  Opcode : 0x201D
 *  Param  : RX_Channel,
 *  Return : Status,
 */
int HCI_LE_Receiver_Test(void){

	uint8_t rc;
	uint8_t buf[1];
	buf[0] = 0x00;
	buf[0] = HW_READ_REG(SIM_SRAM+0xa94) & 0xff;
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_RX_TEST),
                                      buf, sizeof(buf));

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_RECEIVER_TEST_OCF SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_RECEIVER_TEST_OCF FAILED\r\n");
	}
    return rc;
}

/*
 *  Command: HCI_LE_TRANSMITTER_TEST_OCF
 *  OGF    : 0x08
 *  OCF    : 0x001E
 *  Opcode : 0x201E
 *  Param  : TX_Channel,
			 Length_Of_Test_Data,
			 Packet_Payload,
 *  Return : Status,
 */
 int
 HCI_LE_Transmitter_Test(void){
	uint8_t rc;
	uint8_t buf[3];
	buf[0] = 0x00;
	buf[1] = 0x00;
	buf[2] = 0x00;

	buf[0] = (HW_READ_REG(SIM_SRAM+0xa94)>>8) & 0xff;
	buf[1] = (HW_READ_REG(SIM_SRAM+0xa94)>>16) & 0xff;
	buf[2] = (HW_READ_REG(SIM_SRAM+0xa94)>>24) & 0xff;
	printf("HCI_LE_Transmitter_Test:");
	printf("buf[0] = %0x, buf[1] = %0x, buf[2] = %0x \n\n",
		buf[0],buf[1],buf[2]);
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_TX_TEST),
                                      buf, sizeof(buf));

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_TRANSMITTER_TEST_OCF SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_TRANSMITTER_TEST_OCF FAILED\r\n");
	}
    return rc;
 }


/*
 *  Command: HCI_LE_Test_End
 *  OGF    : 0x08
 *  OCF    : 0x001F
 *  Opcode : 0x201F
 *  Param  : NONE
 *  Return : Status,
 			 Number_Of_Packets
 */

int
HCI_LE_Test_End(void){

    uint8_t rc;
	uint8_t rspbuf[8];
	uint8_t rsplen;

	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_TEST_END),
                            NULL, 0, rspbuf, 8, &rsplen);

							
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Test_End SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Test_End FAILED\r\n");
	}

    return rc;

}


/*
 *  Command: LE_Remote_Connection_Parameter_ Request_Reply
 *  OGF    : 0x08
 *  OCF    : 0x0020
 *  Opcode : 0x2020
 *  Param  : Connection_Handle,
			 Interval_Min,
			 Interval_Max,
			 Latency,
			 Timeout,
			 Minimum_CE_Length,
			 Maximum_CE_Length
 *  Return : Status,
			 Connection_Handle
 */
int
HCI_LE_Remote_Connection_Parameter_Request_Reply(void){

    uint8_t cmd[BLE_HCI_CONN_PARAM_REPLY_LEN];
	struct hci_conn_param_reply hcr;
	uint8_t rspbuf[1];
	uint8_t rsplen;
	int rc;


	hcr.handle = 0x0001;
	hcr.conn_itvl_min = 0x0006;
	hcr.conn_itvl_max = 0x0c80;
	hcr.conn_latency = 0x1f3;
	hcr.supervision_timeout = 0x0c80;
	hcr.min_ce_len = 0x0000;
	hcr.max_ce_len = 0xffff;

    hcr.handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	memcpy((char*)(&hcr)+2,SIM_SRAM+0xa48,12);
	
	iot_printf("handle = %x \r\n",hcr.handle);
	iot_printf("conn_itvl_min = %x \r\n",hcr.conn_itvl_min);
	iot_printf("conn_itvl_max = %x \r\n",hcr.conn_itvl_max);
	iot_printf("conn_latency = %x \r\n",hcr.conn_latency);
	iot_printf("supervision_timeout = %x \r\n",hcr.supervision_timeout);
	iot_printf("min_ce_len = %x \r\n",hcr.min_ce_len);
	iot_printf("max_ce_len = %x \r\n",hcr.max_ce_len);

    put_le16(cmd + 0,  hcr.handle);
    put_le16(cmd + 2,  hcr.conn_itvl_min);
    put_le16(cmd + 4,  hcr.conn_itvl_max);
    put_le16(cmd + 6,  hcr.conn_latency);
    put_le16(cmd + 8,  hcr.supervision_timeout);
    put_le16(cmd + 10, hcr.min_ce_len);
    put_le16(cmd + 12, hcr.max_ce_len);

	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_REM_CONN_PARAM_RR),
                            cmd, BLE_HCI_CONN_PARAM_REPLY_LEN, rspbuf, 1, &rsplen);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_READ_SUPPORTED_STATES_OCF SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_READ_SUPPORTED_STATES_OCF FAILED\r\n");
	}

  	return rc;

}


/*
 *  Command: LE_Remote_Connection_Parameter_Request_Negative_Reply
 *  OGF    : 0x08
 *  OCF    : 0x0021
 *  Opcode : 0x2021
 *  Param  : Connection_Handle,
			 Reason
 *  Return : Status,
			 Connection_Handle
 */
int
HCI_LE_Remote_Connection_Paramter_Request_Negative_Reply(void){

	int rc;
	uint8_t rspbuf[3];
	uint8_t rsplen;

	uint8_t cmd[BLE_HCI_CONN_PARAM_NEG_REPLY_LEN];

	struct hci_conn_param_neg_reply hcr;

	hcr.handle = 0x0001;
	hcr.reason = 0x11;

    hcr.handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	hcr.reason = (HW_READ_REG(SIM_SRAM+0xa14) >> 24) & 0xff;


    put_le16(cmd + 0, hcr.handle);
    put_le16(cmd + 2, hcr.reason);


   rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_REM_CONN_PARAM_NRR),
                            cmd, BLE_HCI_CONN_PARAM_NEG_REPLY_LEN, rspbuf, 3, &rsplen);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n LE_Remote_Connection_Parameter_Request_Negative_Reply SUCCEED\r\n");

	}else{
		iot_printf(" \r\n LE_Remote_Connection_Parameter_Request_Negative_Reply FAILED\r\n");
	}

  	return rc;

}

/*
 *  Command: HCI_LE_Set_Data_Length
 *  OGF    : 0x08
 *  OCF    : 0x0022
 *  Opcode : 0x2022
 *  Param  : Connection_Handle,
			 TxOctets,
			 TxTime
 *  Return : Status,
			 Connection_Handle
 */
int
HCI_LE_Set_Data_Length(void){
	int rc;
	uint8_t buf[BLE_HCI_SET_DATALEN_LEN];
	uint8_t rspbuf[2];
	uint8_t rsplen;
	uint16_t handle = 0x0001;
	uint16_t txoctets = 0x001b;
	uint16_t txtime = 0x0148;

    handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
    txoctets = HW_READ_REG(SIM_SRAM+0xa90) & 0xffff;
    txtime = (HW_READ_REG(SIM_SRAM+0xa90)>>16) & 0xffff;

	put_le16(buf, handle);
	put_le16(buf + 2, txoctets);
	put_le16(buf + 4, txtime);
	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_DATA_LEN),
				   buf, sizeof(buf), rspbuf, 2, &rsplen);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Data_Length SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Set_Data_Length FAILED\r\n");
	}

	if (rsplen != 2) {
	   return BLE_HS_ECONTROLLER;
	}

	return rc;

}

/*
 *  Command: HCI_LE_Read_Suggested_Default_Data_Length
 *  OGF    : 0x08
 *  OCF    : 0x0023
 *  Opcode : 0x2023
 *  Param  : NONE
 *  Return : Status,
			 SuggestedMaxTxOctets,
			 SuggestedMaxTxTime
 */
int
HCI_LE_Read_Suggested_Default_Data_Length(void){

    int rc;
    uint8_t rspbuf[BLE_HCI_RD_SUGG_DATALEN_RSPLEN];
    uint8_t rsplen;
	int i;
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_SUGG_DEF_DATA_LEN),
                            NULL, 0, rspbuf, BLE_HCI_RD_SUGG_DATALEN_RSPLEN, &rsplen);

	if(rc == CMD_SUCCEED){
		#if BLE_SIM_CMD_MODE
	       HW_WRITE_REG_BIT(SIM_SRAM+0xd24,9,9,0x1); // wait Read_Suggested_Default_Data_Length SUCCEED
	       printf("sw_check_case1_read_suggested_default_hci_cmd success\n");
        #endif
		iot_printf(" \r\n HCI_LE_Read_Suggested_Default_Data_Length SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Read_Suggested_Default_Data_Length FAILED\r\n");
	}

	printf("read Default_Data_Length:");
	for(i=0; i<BLE_HCI_RD_SUGG_DATALEN_RSPLEN; i++){
		printf("%02X ", rspbuf[i]);
	}
	printf("\n");
	
    if (rsplen != BLE_HCI_RD_SUGG_DATALEN_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }

    return rc;

}

/*
 *  Command: HCI_LE_Read_Local_P_256_Public_Key
 *  OGF    : 0x08
 *  OCF    : 0x0025
 *  Opcode : 0x2025
 *  Param  : NONE
 *  Return : NONE
 */
int
HCI_LE_Read_Local_P_256_Public_Key(void){
	int rc;
    rc =  ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_P256_PUBKEY), NULL, 0);
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Read_Local_P_256_Public_Key SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Read_Local_P_256_Public_Key FAILED\r\n");
	}

	return rc;
}

/*
 *  Command: HCI_LE_Write_Suggested_Default_Data_Length
 *  OGF    : 0x08
 *  OCF    : 0x0024
 *  Opcode : 0x2024
 *  Param  : SuggestedMaxTxOctets,
			 SuggestedMaxTxTime
 *  Return : Status,
 */
int
HCI_LE_Write_Suggested_Defalt_Data_Length(void){

    uint8_t buf[BLE_HCI_WR_SUGG_DATALEN_LEN];
	uint16_t txoctets = 0x001b;
	uint16_t txtime = 0x0148;
	int rc;

    txoctets = HW_READ_REG(SIM_SRAM+0xa98) & 0xffff;
    txtime = (HW_READ_REG(SIM_SRAM+0xa98)>>16) & 0xffff;
	printf("HCI_LE_Write_Suggested_Defalt_Data_Length: %x,%x\n",txoctets,txtime);
	
    put_le16(buf, txoctets);
    put_le16(buf + 2, txtime);
    rc =  ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_WR_SUGG_DEF_DATA_LEN),
                            buf, sizeof(buf), NULL, 0, NULL);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Write_Suggested_Default_Data_Length SUCCEED\r\n");
		printf("sw_check_case1_write_suggested_default_hci_cmd success\n");

	}else{
		iot_printf(" \r\n HCI_LE_Write_Suggested_Default_Data_Length FAILED\r\n");
	}

	return rc;

}
/*
 *  Command: HCI_LE_Generate_DHKey
 *  OGF    : 0x08
 *  OCF    : 0x0026
 *  Opcode : 0x2026
 *  Param  : Remote_P-256_Public_Key
 *  Return : NONE
 */
int
HCI_LE_Generate_DHKey(void){

	int rc;

	uint8_t DHKey[32] =
	{
	    0x4C,0x68,0x38,0x41,0x39,0xF5,0x74,0xD8,
	    0x36,0xBC,0xF3,0x4E,0x9D,0xFB,0x01,0xBF,
	    0x4C,0x68,0x38,0x41,0x39,0xF5,0x74,0xD8,
	    0x36,0xBC,0xF3,0x4E,0x9D,0xFB,0x01,0xBF
	};

    rc =  ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_GEN_DHKEY), DHKey, 32);


	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Generate_DHKey SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Generate_DHKey FAILED\r\n");
	}

	return rc;

}

/*
 *  Command: HCI_LE_Add_Device_To_Resolving_List
 *  OGF    : 0x08
 *  OCF    : 0x0027
 *  Opcode : 0x2027
 *  Param  : Peer_Identity_Address_Type,
			 Peer_Identity_Address,
			 Peer_IRK,
			 Local_IRK
 *  Return : Status
 */
int
HCI_LE_Add_Device_To_Resolving_List(void){

//	ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND,
//													BLE_HCI_OCF_CB_RESET),
//										 NULL, 0);

    int rc;
    struct hci_add_dev_to_resolving_list padd;
    uint8_t buf[BLE_HCI_ADD_TO_RESOLV_LIST_LEN];

	uint8_t local_irk[16] = {
		0xec, 0x02, 0x34, 0xa3, 0x57, 0xc8, 0xad, 0x05,
		0x34, 0x10, 0x10, 0xa6, 0x0a, 0x39, 0x7d, 0x9b
	};

	uint8_t peer_irk[16] = {
	    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10
	};

    padd.addr_type = 0x01;
    memcpy(padd.addr, ble_addr, BLE_DEV_ADDR_LEN);
    swap_buf(padd.local_irk, local_irk, 16);
    swap_buf(padd.peer_irk, peer_irk, 16);
	int i;
	memcpy(&padd,SIM_SRAM+0xa1c,sizeof(struct hci_add_dev_to_resolving_list));
	iot_printf("padd.addr_type = %x \r\n",padd.addr_type);

	printf("padd.addr : ");
	for(i=0;i<6;i++){
		printf("%x ",padd.addr[i]);
	}
	printf("\n");
	
	//printf("padd.peer_irk : ");
	//for(i=0;i<16;i++){
	//	printf("%x ",padd.peer_irk[i]);
	//}
	//printf("\n");	
	
	//printf("padd.local_irk : ");
	//for(i=0;i<16;i++){
	//	printf("%x ",padd.local_irk[i]);
	//}
	//printf("\n");
	
    rc = ble_hs_hci_cmd_build_add_to_resolv_list(&padd, buf, sizeof buf);
    if (!rc) {
        rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_ADD_RESOLV_LIST),
                                      buf, sizeof(buf));
    }

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Add_Device_To_Resolving_List SUCCEED\r\n");

	}else{

		iot_printf(" \r\n HCI_LE_Add_Device_To_Resolving_List FAILED rc = %x\r\n",rc);
	}
    return rc;

}


/*
 *  Command: HCI_LE_Remove_Device_From_Resolving_List
 *  OGF    : 0x08
 *  OCF    : 0x0028
 *  Opcode : 0x2028
 *  Param  : Peer_Identity_Address_Type,
			 Peer_Identity_Address,
 *  Return : Status
 */
int
HCI_LE_Remove_Device_From_Resolving_List(void){

    uint8_t buf[BLE_HCI_RMV_FROM_RESOLV_LIST_LEN];
    int rc;
    u8 addr_type = 0x00;
	u8 addr[BLE_DEV_ADDR_LEN];
    memcpy(addr, ble_addr, BLE_DEV_ADDR_LEN);
	struct hci_params{
		uint8_t addr_type;
		uint8_t addr[BLE_DEV_ADDR_LEN];	
	};
	struct hci_params paddr;
	memcpy(&paddr,SIM_SRAM+0xa1c,sizeof(struct hci_params));
	
    rc = ble_hs_hci_cmd_build_remove_from_resolv_list(paddr.addr_type, paddr.addr,
                                                      buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_RMV_RESOLV_LIST),
                           buf, sizeof(buf), NULL, 0, NULL);

 	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Remove_Device_From_Resolving_List SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Remove_Device_From_Resolving_List FAILED\r\n");
	}

    return rc;


}

/*
 *  Command: HCI_LE_Clear_Resolving_List
 *  OGF    : 0x08
 *  OCF    : 0x0029
 *  Opcode : 0x2029
 *  Param  : None
 *  Return : Status
 */
int
HCI_LE_Remove_Clear_Resolving_List(void){

    int rc;

    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_CLR_RESOLV_LIST),
                           			  NULL, 0);

   	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Clear_Resolving_List SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Clear_Resolving_List FAILED\r\n");
	}

    return rc;
}

/*
 *  Command: HCI_LE_Read_Resolving_List_Size
 *  OGF    : 0x08
 *  OCF    : 0x002A
 *  Opcode : 0x202A
 *  Param  : None
 *  Return : Status
 */
int
HCI_LE_Read_Resolving_List_Size(void){

	int rc;
	uint8_t buf[BLE_HCI_SET_ADDR_RESOL_ENA_LEN];
    uint8_t rsq;
	uint8_t rsq_len;
	uint8_t enable;
	rc = ble_hs_hci_cmd_build_set_addr_res_en(enable, buf, sizeof buf);
	//iot_printf("rc33333333333333333 = %d\n\n",rc);
	if (!rc) {
		rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
									  BLE_HCI_OCF_LE_RD_RESOLV_LIST_SIZE),
									  NULL,0, &rsq,1,&rsq_len);
		//iot_printf("rc4444444444444444444 = %d\n\n",rc);
	}


   	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Read_Resolving_List_Size SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Read_Resolving_List_Size FAILED\r\n");
	}
	return rc;
}


/*
 *  Command: HCI_LE_Read_Peer_Resolvable_Address
 *  OGF    : 0x08
 *  OCF    : 0x002B
 *  Opcode : 0x202B
 *  Param  : Peer_Identity_Address_Type,
			 Peer_Identity_Address,
 *  Return : Status
 			 Peer_Resolvable_Address
 */
int
HCI_LE_Read_Peer_Resolvable_Address(void){
	int rc;
	uint8_t buf[BLE_HCI_RD_PEER_RESOLV_ADDR_LEN];
	uint8_t rsqbuf[BLE_HCI_RD_PEER_RESOLV_ADDR_LEN-1];
	uint8_t rsq_len;
	int i;
	struct hci_params{
		uint8_t Peer_Identity_Address_Type;	
		uint8_t Peer_Identity_Address[6];
		uint8_t revered;
	};
	struct hci_params paddr;
	// printf("RPA_REG_0 = %x\n",HW_READ_REG(SIM_SRAM+0xa1c));
	// printf("RPA_REG_1 = %x\n",HW_READ_REG(SIM_SRAM+0xa20));
	
	memcpy(&paddr,SIM_SRAM+0xa1c,sizeof(struct hci_params));
	put_le16(buf, paddr.Peer_Identity_Address_Type);
	//swap_buf(buf+1,paddr.Peer_Identity_Address,6);
	memcpy(buf+1,paddr.Peer_Identity_Address,6);
//	buf[0] = 0x00;
//	buf[1] = 0x63;
//	buf[2] = 0x56;
//	buf[3] = 0xf0;
//	buf[4] = 0xb1;
//	buf[5] = 0x09;
//	buf[6] = 0x96;

	
	printf("Peer RPA and addr type:");
	for(i=0; i<BLE_HCI_RD_PEER_RESOLV_ADDR_LEN; i++){
		printf("%02X ", buf[i]);
	}
	printf("\n");
	
	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
									  BLE_HCI_OCF_LE_RD_PEER_RESOLV_ADDR),
									  buf,BLE_HCI_RD_PEER_RESOLV_ADDR_LEN, rsqbuf,BLE_HCI_RD_PEER_RESOLV_ADDR_LEN-1,&rsq_len);

	printf("Read_Peer_Resolvable_Address:");
	for(i=0; i<BLE_HCI_RD_PEER_RESOLV_ADDR_LEN-1; i++){
		printf("%02X ", rsqbuf[i]);
	}
	printf("\n");
	
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Read_Peer_Resolvable_Address SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Read_Peer_Resolvable_Address FAILED\r\n");
	}
	
	return rc;

}


/*
 *  Command: HCI_LE_Read_Local_Resolvable_Address
 *  OGF    : 0x08
 *  OCF    : 0x002C
 *  Opcode : 0x202C
 *  Param  : Peer_Identity_Address_Type,
			 Peer_Identity_Address
 *  Return : Status
 			 Local_Resolvable_Address
 */
int
HCI_LE_Read_Local_Resolvable_Address(void){
	int rc;
	uint8_t buf[BLE_HCI_RD_LOC_RESOLV_ADDR_LEN];
	uint8_t Peer_Identity_Address_Type = 0x00;
	uint8_t Peer_Identity_Address[6] = {0x11,0x12,0x13,0x14,0x15,0x16};
	uint8_t rsqbuf[BLE_HCI_RD_LOC_RESOLV_ADDR_LEN-1];
	uint8_t rsq_len;
	int i;
	struct hci_params{
		uint8_t Peer_Identity_Address_Type;
		uint8_t Peer_Identity_Address[6];	
		uint8_t revered;
	};
	struct hci_params paddr;
	memcpy(&paddr,SIM_SRAM+0xa1c,sizeof(struct hci_params));
	put_le16(buf, paddr.Peer_Identity_Address_Type);
	memcpy(buf+1,paddr.Peer_Identity_Address,6);
	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
									  BLE_HCI_OCF_LE_RD_LOCAL_RESOLV_ADDR),
									  buf,BLE_HCI_RD_LOC_RESOLV_ADDR_LEN, rsqbuf,BLE_HCI_RD_LOC_RESOLV_ADDR_LEN-1,&rsq_len);

	printf("Read_Local_Resolvable_Address:");
	for(i=0; i<BLE_HCI_RD_LOC_RESOLV_ADDR_LEN-1; i++){
		printf("%02X ", rsqbuf[i]);
	}
	printf("\n");

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Read_Local_Resolvable_Address SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Read_Local_Resolvable_Address FAILED\r\n");
	}

	return rc;
}



/*
 *  Command: HCI_LE_Set_Address_Resolution_Enable
 *  OGF    : 0x08
 *  OCF    : 0x002D
 *  Opcode : 0x202D
 *  Param  : Address_Resolution_Enable
 *  Return : Status
 */
int
HCI_LE_Set_Address_Resolution_Enable(void){

    int rc;
    uint8_t buf[BLE_HCI_SET_ADDR_RESOL_ENA_LEN];
	uint8_t enable = 0x01;

	enable = (HW_READ_REG(HCI_REG)>>26) && 0x1;

    rc = ble_hs_hci_cmd_build_set_addr_res_en(enable, buf, sizeof buf);
    if (!rc) {
        rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_SET_ADDR_RES_EN),
                                      buf, sizeof(buf));
    }
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Address_Resolution_Enable SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Set_Address_Resolution_Enable FAILED\r\n");
	}
    return rc;

}


/*
 *  Command: HCI_LE_Set_Resolvable_Private_Address_Timeout
 *  OGF    : 0x08
 *  OCF    : 0x002E
 *  Opcode : 0x202E
 *  Param  : RPA_Timeout
 *  Return : Status
 */
int
HCI_LE_Set_Resolvable_Private_Address_Timeout(void){

    uint8_t buf[BLE_HCI_SET_RESOLV_PRIV_ADDR_TO_LEN];
    int rc;
	uint16_t timeout = 0x0384;

	put_le16(buf, timeout);

    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_SET_RPA_TMO),
                                      buf, sizeof(buf));

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Resolvable_Private_Address_Timeout SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Set_Resolvable_Private_Address_Timeout FAILED\r\n");
	}
    return rc;


}


/*
 *  Command: HCI_LE_Read_Maximum_Data_Length
 *  OGF    : 0x08
 *  OCF    : 0x002F
 *  Opcode : 0x202F
 *  Param  : RPA_Timeout
 *  Return : Status
			 spportedMaxTxOctets,
             supportedMaxTxTime,
			 supportedMaxRxOctets,
 			 supportedMaxRxTime
 */
int
HCI_LE_Read_Maximum_Data_Length(void){

    int rc;
    uint8_t rspbuf[BLE_HCI_RD_MAX_DATALEN_RSPLEN];
    uint8_t rsplen;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_MAX_DATA_LEN),
                            NULL, 0, rspbuf, BLE_HCI_RD_MAX_DATALEN_RSPLEN, &rsplen);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Read_Maximum_Data_Length SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Read_Maximum_Data_Length FAILED\r\n");
	}

    if (rsplen != BLE_HCI_RD_MAX_DATALEN_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }
    return rc;

}



/*
 *  Command: HCI_LE_Read_PHY
 *  OGF    : 0x08
 *  OCF    : 0x0030
 *  Opcode : 0x2030
 *  Param  : RPA_Timeout
 *  Return : Status
			 Connection_Handle,
			 TX_PHY,
			 RX_PHY
 */
int
HCI_LE_Read_PHY(void){

    int rc;
    uint8_t rspbuf[BLE_HCI_LE_RD_PHY_RSPLEN];
    uint8_t rsplen;
	uint8_t cmd[BLE_HCI_LE_RD_PHY_LEN];
	uint16_t Connection_Handle = 0x0001;
	Connection_Handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	
	put_le16(cmd, Connection_Handle);
	int i;
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_PHY),
                            cmd, BLE_HCI_LE_RD_PHY_LEN, rspbuf, BLE_HCI_LE_RD_PHY_RSPLEN, &rsplen);

	printf("read phy:");
	for(i=0; i<BLE_HCI_LE_RD_PHY_RSPLEN; i++){
		printf("%02X ", rspbuf[i]);
	}
	printf("\n");
	
	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Read_PHY SUCCEED\r\n");
		#if LL_CHECK
		  printf("sw_check_case4_read_phy_hci_cmd success\n");
		#endif
	}else{
		iot_printf(" \r\n HCI_LE_Read_PHY FAILED\r\n");
	}
	printf("read_phy rsplen = %d\n",rsplen);
    if (rsplen != BLE_HCI_LE_RD_PHY_RSPLEN) {
        return BLE_HS_ECONTROLLER;
    }
    return rc;


}


/*
 *  Command: HCI_LE_Set_Default_PHY
 *  OGF    : 0x08
 *  OCF    : 0x0031
 *  Opcode : 0x2031
 *  Param  : ALL_PHYS,
			 TX_PHYS,
			 RX_PHYS
 *  Return : Status
 */
int HCI_LE_Set_Default_PHY(void){

    uint8_t buf[BLE_HCI_LE_SET_DEFAULT_PHY_LEN];
    int rc;
	uint8_t tx_phys_mask = 0x01;
	uint8_t rx_phys_mask = 0x01;

    rc = ble_hs_hci_cmd_build_le_set_default_phy(tx_phys_mask, rx_phys_mask,
                                                 buf, sizeof(buf));
    if (rc == 0) {
        rc =  ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_SET_DEFAULT_PHY),
                            buf, sizeof(buf), NULL, 0, NULL);

		if(rc == CMD_SUCCEED){
			iot_printf(" \r\n HCI_LE_Set_Default_PHY SUCCEED\r\n");

		}else{
			iot_printf(" \r\n HCI_LE_Set_Default_PHY FAILED\r\n");
		}
    }

    return rc;

}


/*
 *  Command: HCI_LE_Set_PHY
 *  OGF    : 0x08
 *  OCF    : 0x0032
 *  Opcode : 0x2032
 *  Param  : Connection_Handle,
			 ALL_PHYS,
			 TX_PHYS,
			 RX_PHYS,
			 PHY_options
 *  Return : None
 */
int
HCI_LE_Set_PHY(){
	int rc;
	struct ble_hs_conn *conn;
    uint8_t buf[BLE_HCI_LE_SET_PHY_LEN];
    uint16_t conn_handle;
	uint8_t tx_phys_mask;
	uint8_t rx_phys_mask;
	uint16_t phy_opts;
	
	conn_handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	tx_phys_mask = (HW_READ_REG(SIM_SRAM+0xae4)>>24) & 0xff;
	rx_phys_mask = (HW_READ_REG(SIM_SRAM+0xae4)>>16) & 0xff;
	phy_opts = HW_READ_REG(SIM_SRAM+0xae4) & 0xff;
	
    rc = ble_hs_hci_cmd_build_le_set_phy(conn_handle, tx_phys_mask,
                                         rx_phys_mask, phy_opts, buf,
                                         sizeof(buf));
    if (rc == 0) {
        rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_PHY),
						       buf, sizeof(buf), NULL, 0, NULL);
		if(rc == CMD_SUCCEED){
			iot_printf(" \r\n HCI_LE_Set_PHY SUCCEED\r\n");
			#if LL_CHECK
		  	  printf("sw_check_case4_set_phy_hci_cmd success\n");
		    #endif
		}else{
			iot_printf(" \r\n HCI_LE_Set_PHY FAILED\r\n");
		}
    }

	return rc;

}

/*
 *  Command: HCI_LE_Enhanced_Receiver_Test
 *  OGF    : 0x08
 *  OCF    : 0x0033
 *  Opcode : 0x2033
 *  Param  : RX_Channel,
			 PHY,
			 Modulation_Index
 *  Return : Status
 */
int
HCI_LE_Enhanced_Receiver_Test(void){

	int rc;
	uint8_t cmd[3];
	uint8_t RX_Channel = 0x27;
	uint8_t PHY = 0x01;
	uint8_t Modulation_Index = 0x01;
	cmd[0] = RX_Channel;
	cmd[1] = PHY;
	cmd[2] = Modulation_Index;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_ENH_RX_TEST), cmd, 3);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Enhanced_Receiver_Test SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Enhanced_Receiver_Test FAILED\r\n");
	}


	return rc;
}



/*
 *  Command: HCI_LE_Enhanced_Transmitter_Test
 *  OGF    : 0x08
 *  OCF    : 0x0034
 *  Opcode : 0x2034
 *  Param  : TX_Channel,
			 Length_Of_Test_Data,
			 Packet_Payload,
			 PHY
 *  Return : Status
 */
int HCI_LE_Enhanced_Tranmitter_Test(void){
	int rc;
	uint8_t cmd[4];
	uint8_t TX_Channel = 0x27;
	uint8_t Length_Of_Test_Data = 0x01;
	uint8_t Packet_Payload = 0x06;
	uint8_t PHY = 0x01;
	cmd[0] = TX_Channel;
	cmd[1] = Length_Of_Test_Data;
	cmd[2] = Packet_Payload;
	cmd[3] = PHY;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_ENH_TX_TEST), cmd, 4);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Enhanced_Transmitter_Test SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Enhanced_Transmitter_Test FAILED\r\n");
	}

	return rc;

}


/*
 *  Command: HCI_LE_Set_Advertising_Set_Random_Address
 *  OGF    : 0x08
 *  OCF    : 0x0035
 *  Opcode : 0x2035
 *  Param  : Advertising_Handle,
			 Random_Address
 *  Return : Status
 */
int
HCI_LE_Set_Advertising_Set_Random_Address(void){

	int rc;
	uint8_t cmd[7];
	uint8_t Advertising_Handle = 0x27;

	cmd[0] = Advertising_Handle;
	memcpy(cmd+1, ble_addr, 6);

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_ENH_TX_TEST), cmd, 7);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Enhanced_Transmitter_Test SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Enhanced_Transmitter_Test FAILED\r\n");
	}

	return rc;


}

#if (MYNEWT_VAL_BLE_VERSION >= 50)

/*
 *  Command: HCI_LE_Set_ExtendedAdvertising_Parameters
 *  OGF    : 0x08
 *  OCF    : 0x0036
 *  Opcode : 0x2036
 *  Param  : Advertising_Handle,
			 Advertising_Event_Properties,
			 Primary_Advertising_Interval_Min,
			 Primary_Advertising_Interval_Max,
			 Primary_Advertising_Channel_Map,
			 Own_Address_Type,
			 Peer_Address_Type,
			 Peer_Address,
			 Advertising_Filter_Policy,
			 Advertising_Tx_Power,
			 Primary_Advertising_PHY,
			 Secondary_Advertising_Max_Skip,
			 Secondary_Advertising_PHY,
			 Advertising_SID,
			 Scan_Request_Notification_Enable
 *  Return : Status
 			 Selected_Tx_Power
 */
int
HCI_LE_Set_ExtendedAdvertising_Parameters(void){

	int rc;
	uint8_t buf[BLE_HCI_LE_SET_EXT_ADV_PARAM_LEN];
    uint8_t rsp;
	uint32_t tmp;
	struct hci_ext_adv_params hci_adv_params;

	uint8_t instance = 0xef;

    hci_adv_params.properties = 0x0000;
    hci_adv_params.min_interval = 0x000020;
    hci_adv_params.max_interval = 0xFFFFFF;
    hci_adv_params.chan_map = 0x01;
    hci_adv_params.own_addr_type = 0x00;
    hci_adv_params.peer_addr_type = 0x00;
    hci_adv_params.filter_policy = 0x00;
    hci_adv_params.tx_power =0x01;
    hci_adv_params.primary_phy = 0x01;
    hci_adv_params.max_skip = 0x00;
    hci_adv_params.secondary_phy = 0x01;
    hci_adv_params.sid = 0x01;
    hci_adv_params.scan_req_notif = 0x00;

    memcpy(hci_adv_params.peer_addr, ble_addr, 6);


	buf[0] = instance;
    put_le16(&buf[1], hci_adv_params.properties);

    tmp = htole32(hci_adv_params.min_interval);
    memcpy(&buf[3], &tmp, 3);

    tmp = htole32(hci_adv_params.max_interval);
    memcpy(&buf[6], &tmp, 3);

    buf[9] = hci_adv_params.chan_map;
    buf[10] = hci_adv_params.own_addr_type;
    buf[11] = hci_adv_params.peer_addr_type;
    memcpy(&buf[12], hci_adv_params.peer_addr, BLE_DEV_ADDR_LEN);
    buf[18] = hci_adv_params.filter_policy;
    buf[19] = hci_adv_params.tx_power;
    buf[20] = hci_adv_params.primary_phy;
    buf[21] = hci_adv_params.max_skip;
    buf[22] = hci_adv_params.secondary_phy;
    buf[23] = hci_adv_params.sid;
    buf[24] = hci_adv_params.scan_req_notif;
	
	if (rc == 0) {
	   rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_EXT_ADV_PARAM), buf, sizeof(buf), &rsp, 1, NULL);

		if(rc == CMD_SUCCEED){
			iot_printf(" \r\n HCI_LE_Set_ExtendedAdvertising_Parameters SUCCEED\r\n");

		}else{
			iot_printf(" \r\n HCI_LE_Set_ExtendedAdvertising_Parameters FAILED\r\n");
		}


	}

	return rc;

}



/*
 *  Command: HCI_LE_Set_Extended_Advertising_Data
 *  OGF    : 0x08
 *  OCF    : 0x0037
 *  Opcode : 0x2037
 *  Param  : Advertising_Handle,
			 Operation,
			 Fragment_Preference,
			 Advertising_Data_Length,
			 Advertising_Data
 *  Return : Status
 */
int
HCI_LE_Set_Extend_Advertising_Data(void){

	int rc;
	uint8_t buf[7];
	uint8_t Advertising_Handle = 0x00;
	uint8_t Operation = 0x00;
	uint8_t Fragment_Preference = 0x00;
	uint8_t Advertising_Data_Length = 0x03;
	uint8_t Advertising_Data[3] = {0x10,0x11,0x12};

	buf[0] = Advertising_Handle;
	buf[1] = Operation;
	buf[2] = Fragment_Preference;
	buf[3] = Advertising_Data_Length;
	memcpy( &buf[4], Advertising_Data, 3);


    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_EXT_ADV_DATA), buf,7);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Extended_Advertising_Data SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Set_Extended_Advertising_Data FAILED\r\n");
	}

	return rc;
}



/*
 *  Command: HCI_LE_Set_Extended_Scan_Response_Data
 *  OGF    : 0x08
 *  OCF    : 0x0038
 *  Opcode : 0x2038
 *  Param  : Advertising_Handle,
			 Operation,
			 Fragment_Preference,
			 Scan_Response_Data_Length,
			 Scan_Response_Data
 *  Return : Status
 */
int
HCI_LE_Set_Extended_Scan_Responese_Data(void){
	int rc;

	uint8_t buf[7];
	uint8_t Advertising_Handle = 0x00;
	uint8_t Operation = 0x00;
	uint8_t Fragment_Preference = 0x00;
	uint8_t Scan_Response_Data_Length = 0x03;
	uint8_t Scan_Response_Data[3] = {0x10,0x11,0x12};

	buf[0] = Advertising_Handle;
	buf[1] = Operation;
	buf[2] = Fragment_Preference;
	buf[3] = Scan_Response_Data_Length;
	memcpy( &buf[4], Scan_Response_Data, 3);


	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_EXT_SCAN_RSP_DATA), buf,7);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Extended_Scan_Response_Data SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Set_Extended_Scan_Response_Data FAILED\r\n");
	}
	return rc;
}



/*
 *  Command: LE_Set_Extended_Advertising_Enable
 *  OGF    : 0x08
 *  OCF    : 0x0039
 *  Opcode : 0x2039
 *  Param  : Enable,
			 Number_of_Sets,
			 Advertising_Handle[i],
			 Duration[i],
			 Max_Extended_Advertising_Events[i]
 *  Return : Status
 */
int
HCI_LE_Set_Extended_Advertising_Enable(void){

	int rc;
	uint8_t buf[6];
	uint8_t Enable = 0x01;
	uint8_t Number_of_Sets = 0x01;
	uint8_t Advertising_Handle = 0x01;
	uint16_t Duration = 0x0001;
	uint8_t Max_Extended_Advertising_Events = 0x01;

	buf[0] = Enable;
	buf[1] = Number_of_Sets;
	buf[2] = Advertising_Handle;
	buf[3] = Duration&0xff;
	buf[4] = (Duration>>8);
	buf[5] = Max_Extended_Advertising_Events;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_EXT_ADV_ENABLE), buf,6);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Resolvable_Private_Address_Timeout SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_Set_Resolvable_Private_Address_Timeout FAILED\r\n");
	}
	return rc;
}



/*
 *  Command: HCI_LE_Read_Maximum_Advertising_Data_Length
 *  OGF    : 0x08
 *  OCF    : 0x003A
 *  Opcode : 0x203A
 *  Param  : None
 *  Return : Status
 			 Maximum_Advertising_Data_Length
 */
int
HCI_LE_Read_Maximum_Advertising_Data_Length(void){

	int rc;
	uint8_t rsq;
	uint8_t rsq_len;
	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_MAX_ADV_DATA_LEN),
						   NULL,0,&rsq, 1,&rsq_len);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Read_Maximum_Advertising_Data_Length SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Read_Maximum_Advertising_Data_Length FAILED\r\n");
	}
	return rc;
}


/*
 *  Command: HCI_LE_Read_Number_of_Supported_Advertising_Sets
 *  OGF    : 0x08
 *  OCF    : 0x003B
 *  Opcode : 0x203B
 *  Param  : None
 *  Return : Status,
 			 Num_Supported_Advertising_Sets
 */
int
HCI_LE_Read_Number_Of_Supported_Advertising_Sets(void){

	int rc;
	uint8_t rsq;
	uint8_t rsq_len;
	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_NUM_OF_ADV_SETS),
						   NULL,0,&rsq, 1,&rsq_len);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Read_Number_of_Supported_Advertising_Sets SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Read_Number_of_Supported_Advertising_Sets FAILED\r\n");
	}
	return rc;

}



/*
 *  Command: HCI_LE_Remove_Advertising_Set
 *  OGF    : 0x08
 *  OCF    : 0x003C
 *  Opcode : 0x203C
 *  Param  : Advertising_Handle
 *  Return : Status
 */
int
HCI_LE_Remove_Advertising_Sets(void){
	int rc;
	uint8_t Advertising_Handle = 0x01;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_REMOVE_ADV_SET), &Advertising_Handle,1);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Remove_Advertising_Set SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Remove_Advertising_Set FAILED\r\n");
	}
	return rc;
}

/*
 *  Command: HCI_LE_Clear_Advertising_Sets
 *  OGF    : 0x08
 *  OCF    : 0x003D
 *  Opcode : 0x203D
 *  Param  : None
 *  Return : Status
 */
int
HCI_LE_Clear_Advertising_Sets(void){
	int rc;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CLEAR_ADV_SETS), NULL,0);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Remove_Advertising_Set SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Remove_Advertising_Set FAILED\r\n");
	}

	return rc;
}



/*
 *  Command: HCI_LE_Set_Periodic_Advertising_Parameters
 *  OGF    : 0x08
 *  OCF    : 0x003E
 *  Opcode : 0x203E
 *  Param  : Advertising_Handle,
			 Periodic_Advertising_Interval_Min,
			 Periodic_Advertising_Interval_Max,
			 Periodic_Advertising_Properties
 *  Return : Status
 */
int
HCI_LE_Set_Periodic_Advertising_Parameters(void){

	int rc;
	uint8_t cmd[7];
	uint8_t Advertising_Handle;
	uint16_t Periodic_Advertising_Interval_Min;
	uint16_t Periodic_Advertising_Interval_Max;
	uint16_t Periodic_Advertising_Properties;
	cmd[0] = Advertising_Handle;
	put_le16(cmd + 1, Periodic_Advertising_Interval_Min);
	put_le16(cmd + 3, Periodic_Advertising_Interval_Max);
	put_le16(cmd + 5, Periodic_Advertising_Properties);

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_PER_ADV_PARAMS), cmd,7);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Periodic_Advertising_Parameters SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Set_Periodic_Advertising_Parameters FAILED\r\n");
	}

	return 0;
}




/*
 *  Command: LE_Set_Periodic_Advertising_Data
 *  OGF    : 0x08
 *  OCF    : 0x003F
 *  Opcode : 0x203F
 *  Param  : Advertising_Handle,
			 Operation,
			 Advertising_Data_Length,
			 Advertising_Data
 *  Return : Status
 */
int HCI_LE_Set_Periodic_Advertising_Data_Timeout(void){

	int rc;
	uint8_t cmd[4];
	uint8_t Advertising_Handle;
	uint8_t Operation;
	uint8_t Advertising_Data_Length;
	uint8_t Advertising_Data;
	cmd[0] = Advertising_Handle;
	cmd[1] = Operation;
	cmd[2] = Advertising_Data_Length;
	cmd[3] = Advertising_Data;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_PER_ADV_DATA), cmd,4);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Periodic_Advertising_Parameters SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Set_Periodic_Advertising_Parameters FAILED\r\n");
	}
	return rc;
}



/*
 *  Command: HCI_LE_Set_Periodic_Advertising_Enable
 *  OGF    : 0x08
 *  OCF    : 0x0040
 *  Opcode : 0x2040
 *  Param  : Enable,
			 Advertising_Handle
 *  Return : Status
 */
int
HCI_LE_Set_Periodic_Advertising_Enable(void){

	int rc;
	uint8_t Advertising_Handle = 0x01;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_PER_ADV_ENABLE), &Advertising_Handle,1);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Remove_Advertising_Set SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Remove_Advertising_Set FAILED\r\n");
	}

	return rc;
}

/*
 *  Command: HCI_LE_Set_Extended_Scan_Parameters
 *  OGF    : 0x08
 *  OCF    : 0x0041
 *  Opcode : 0x2041
 *  Param  : Own_Address_Type
			 Scanning_Filter_Policy
			 Scanning_PHYs,
			 Scan_Type[i],
			 Scan_Interval[i],
			 Scan_Window[i]
 *  Return : Status
 */
int
HCI_LE_Set_Extended_Scan_Parameters(void){
	int rc;
	uint8_t cmd[8];
	uint8_t Own_Address_Type;
	uint8_t Scanning_Filter_Policy;
	uint8_t Scanning_PHYs;
	uint8_t Scan_Type;
	uint16_t Scan_Interval;
	uint16_t Scan_Window;


	cmd[0] = Own_Address_Type;
	cmd[1] = Scanning_Filter_Policy;
	cmd[2] = Scanning_PHYs;
	cmd[3] = Scan_Type;
	put_le16(cmd + 4, Scan_Interval);
	put_le16(cmd + 6, Scan_Window);

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_EXT_SCAN_PARAM), cmd,8);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Remove_Advertising_Set SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Remove_Advertising_Set FAILED\r\n");
	}

	return rc;

}



/*
 *  Command: HCI_LE_Set_Extended_Scan_Enable
 *  OGF    : 0x08
 *  OCF    : 0x0042
 *  Opcode : 0x2042
 *  Param  : Enable,
			 Filter_Duplicates,
			 Duration,
			 Period
 *  Return : Status
 */
int
HCI_LE_Set_Extended_Scan_Enable(void){

	int rc;

    uint8_t buf[BLE_HCI_LE_SET_EXT_SCAN_ENABLE_LEN];

	uint8_t enable;
	uint8_t filter_duplicates;
	uint16_t duration;
	uint16_t period;

	buf[0] = enable;
	buf[1] = filter_duplicates;
	put_le16(buf + 3, duration);
	put_le16(buf + 5, period);

    rc = ble_hs_hci_cmd_tx_empty_ack(
        BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_EXT_SCAN_ENABLE),
        buf, sizeof(buf));

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Extended_Scan_Enable SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Set_Extended_Scan_Enable FAILED\r\n");
	}


}




/*
 *  Command: LE_Extended_Create_Connection
 *  OGF    : 0x08
 *  OCF    : 0x0043
 *  Opcode : 0x2043
 *  Param  : Initiator_Filter_Policy,
			 Own_Address_Type,
			 Peer_Address_Type,
			 Peer_Address,
			 Initiating_PHYs,
			 Scan_Interval[i],
			 Scan_Window[i],
			 Conn_Interval_Min[i],
			 Conn_Interval_Max[i],
			 Conn_Latency[i],
			 Supervision_Timeout[i],
			 Minimum_CE_Length[i],
			 Maximum_CE_Length[i]
 *  Return : None
 */
int
HCI_LE_Extended_Create_Connection(void){
	int rc;
	uint8_t cmd[26];
	uint8_t Initiator_Filter_Policy;
	uint8_t Own_Address_Type;
	uint8_t Peer_Address_Type;
	uint8_t Peer_Address[6];
	uint8_t Initiating_PHYs;
	uint16_t Scan_Interval;
	uint16_t Scan_Window;
	uint16_t Conn_Interval_Min;
	uint16_t Conn_Interval_Max;
	uint16_t Conn_Latency;
	uint16_t Supervision_Timeout;
	uint16_t Minimum_CE_Length;
	uint16_t Maximum_CE_Length;


	cmd[0] = Initiator_Filter_Policy;
	cmd[1] = Own_Address_Type;
	cmd[2] = Peer_Address_Type;
	memcpy(&cmd[3], Peer_Address,6);

	put_le16(cmd + 9,  Scan_Interval);
	put_le16(cmd + 11, Scan_Window);
	put_le16(cmd + 13, Conn_Interval_Min);
	put_le16(cmd + 15, Conn_Interval_Max);
	put_le16(cmd + 17, Conn_Latency);
	put_le16(cmd + 19, Supervision_Timeout);
	put_le16(cmd + 21, Minimum_CE_Length);
	put_le16(cmd + 23, Maximum_CE_Length);



	rc = ble_hs_hci_cmd_tx_empty_ack(
		 BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_EXT_CREATE_CONN),
		 cmd, sizeof(cmd));

	 if(rc == CMD_SUCCEED){
		 iot_printf(" \r\n HCI_LE_Set_Extended_Scan_Enable SUCCEED\r\n");
	 }else{
		 iot_printf(" \r\n HCI_LE_Set_Extended_Scan_Enable FAILED\r\n");
	 }

	 return rc;

}



/*
 *  Command: The LE_Periodic_Advertising_Create_Sync
 *  OGF    : 0x08
 *  OCF    : 0x0044
 *  Opcode : 0x2044
 *  Param  : Filter_Policy,
			 Advertising_SID,
			 Advertiser_Address_Type,
			 Advertiser_Address,
			 Skip,
			 Sync_Timeout,
			 Unused
 *  Return : None
 */
int
HCI_LE_Periodic_Advertising_Create_Sync(void){

	int rc;
	uint8_t cmd[11];
	uint8_t Filter_Policy;
	uint8_t Advertising_SID;
	uint8_t Advertiser_Address_Type;
	uint8_t Advertiser_Address[6];
	uint16_t Skip;
	uint16_t Sync_Timeout;
	uint8_t Unused;

	cmd[0] = Filter_Policy;
	cmd[1] = Advertising_SID;
	cmd[2] = Advertiser_Address_Type;
	memcpy(&cmd[3], Advertiser_Address,6);
	put_le16(cmd + 9,  Skip);
	put_le16(cmd + 11, Sync_Timeout);
	cmd[12] = Unused;

	rc = ble_hs_hci_cmd_tx_empty_ack(
		 BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_PER_ADV_CREATE_SYNC),
		 cmd, sizeof(cmd));

	 if(rc == CMD_SUCCEED){
		 iot_printf(" \r\n HCI_LE_Set_Extended_Scan_Enable SUCCEED\r\n");
	 }else{
		 iot_printf(" \r\n HCI_LE_Set_Extended_Scan_Enable FAILED\r\n");
	 }

	 return rc;
}



/*
 *  Command: HCI_LE_Periodic_Advertising_Create_Sync_Cancel
 *  OGF    : 0x08
 *  OCF    : 0x0045
 *  Opcode : 0x2045
 *  Param  : None
 *  Return : Status
 */
int
HCI_LE_Periodic_Advertising_Create_Sync_Cancel(void){

	int rc;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_PER_ADV_CREATE_SYNC_CANCEL), NULL,0);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Periodic_Advertising_Create_Sync_Cancel SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Periodic_Advertising_Create_Sync_Cancel FAILED\r\n");
	}

	return rc;
}




/*
 *  Command: HCI_LE_Periodic_Advertising_Terminate_Sync
 *  OGF    : 0x08
 *  OCF    : 0x0046
 *  Opcode : 0x2046
 *  Param  : Sync_Handle
 *  Return : Status
 */
int
HCI_LE_Periodic_Advertising_Terminate_Sync(void){
	int rc;
	uint8_t cmd[2];
	uint16_t Sync_Handle;
	put_le16(cmd,  Sync_Handle);

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_PER_ADV_TERM_SYNC), cmd,2);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Periodic_Advertising_Terminate_Sync SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Periodic_Advertising_Terminate_Sync FAILED\r\n");
	}

	return rc;
}




/*
 *  Command: HCI_LE_Add_Device_To_Periodic_Advertiser_List
 *  OGF    : 0x08
 *  OCF    : 0x0047
 *  Opcode : 0x2047
 *  Param  : Advertiser_Address_Type,
			 Advertiser_Address,
			 Advertising_SID
 *  Return : Status
 */
int
HCI_LE_Add_Device_To_Periodic_Advertiser_List(void){

	int rc;
	uint8_t cmd[8];
	uint8_t Advertiser_Address_Type;
	uint8_t Advertiser_Address[6];
	uint8_t Advertising_SID;

	cmd[0] = Advertiser_Address_Type;
	memcpy(&cmd[1], Advertiser_Address,6);
	cmd[8] = Advertising_SID;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_ADD_DEV_TO_PER_ADV_LIST), cmd,8);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Periodic_Advertising_Terminate_Sync SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Periodic_Advertising_Terminate_Sync FAILED\r\n");
	}

	return rc;
}



/*
 *  Command: HCI_LE_Remove_Device_From_Periodic_Advertiser_List
 *  OGF    : 0x08
 *  OCF    : 0x0048
 *  Opcode : 0x2048
 *  Param  : Advertiser_Address_Type,
			 Advertiser_Address,
             Advertising_SID
 *  Return : Status
 */
int
HCI_LE_Remove_Device_From_Periodic_Advertiser_List(void){

	int rc;
	uint8_t cmd[8];
	uint8_t Advertiser_Address_Type;
	uint8_t Advertiser_Address[6];
	uint8_t Advertising_SID;

	cmd[0] = Advertiser_Address_Type;
	memcpy(&cmd[1], Advertiser_Address,6);
	cmd[8] = Advertising_SID;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_REM_DEV_FROM_PER_ADV_LIST), cmd,8);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Periodic_Advertising_Terminate_Sync SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Periodic_Advertising_Terminate_Sync FAILED\r\n");
	}

	return rc;
}



/*
 *  Command: HCI_LE_Clear_Periodic_Advertiser_List
 *  OGF    : 0x08
 *  OCF    : 0x0049
 *  Opcode : 0x2049
 *  Param  : None
 *  Return : Status
 */
int
HCI_LE_Clear_Periodic_Advertiser_List(void){


	int rc;

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_CLEAR_PER_ADV_LIST), NULL,0);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Clear_Periodic_Advertiser_List SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Clear_Periodic_Advertiser_List FAILED\r\n");
	}

	return rc;
}



/*
 *  Command: LE_Read_Periodic_Advertiser_List_Size
 *  OGF    : 0x08
 *  OCF    : 0x004A
 *  Opcode : 0x204A
 *  Param  : None
 *  Return : Status
 			 Periodic_Advertiser_List_Size
 */
int
HCI_LE_Read_Periodic_Advertiser_List_Size(void){

	int rc;
	uint8_t rsq;
	uint8_t rsq_len;

	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_PER_ADV_LIST_SIZE), NULL,0,&rsq,1,&rsq_len);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n LE_Read_Periodic_Advertiser_List_Size SUCCEED\r\n");
	}else{
		iot_printf(" \r\n LE_Read_Periodic_Advertiser_List_Size FAILED\r\n");
	}

	return rc;
}



/*
 *  Command: HCI_LE_Read_Transmit_Power
 *  OGF    : 0x08
 *  OCF    : 0x004B
 *  Opcode : 0x204B
 *  Param  : None
 *  Return : Status
 			 Min_ Tx_Power,
			 Max_ Tx_Power
 */
int
HCI_LE_Read_transmit_Power(void){

	int rc;
	uint8_t rsq[2];
	uint8_t rsq_len[3];

	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_TRANSMIT_POWER), NULL,0,rsq,2,rsq_len);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Clear_Periodic_Advertiser_List SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Clear_Periodic_Advertiser_List FAILED\r\n");
	}

	return rc;
}



/*
 *  Command: LE_Read_RF_Path_Compensation
 *  OGF    : 0x08
 *  OCF    : 0x004C
 *  Opcode : 0x204C
 *  Param  : None
 *  Return : Status
			 RF_Tx_Path_Compensation_Value,
             RF_Rx_Path_Compensation_Value
 */
int
HCI_LE_Read_RF_Path_Compensation_List(void){
	int rc;
	uint8_t rsq[2];
	uint8_t rsq_len[3];

	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_RD_RF_PATH_COMPENSATION), NULL,0,rsq,2,rsq_len);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n LE_Read_RF_Path_Compensation SUCCEED\r\n");
	}else{
		iot_printf(" \r\n LE_Read_RF_Path_Compensation FAILED\r\n");
	}

	return rc;
}



/*
 *  Command: HCI_LE_Write_RF_Path_Compensation
 *  OGF    : 0x08
 *  OCF    : 0x004D
 *  Opcode : 0x204D
 *  Param  : RF_Tx_Path_Compensation_Value,
			 RF_Rx_Path_Compensation_Value
 *  Return : Status
 */
int
HCI_LE_Write_RF_Path_Compensation(void){

	int rc;
	uint8_t cmd[4];
	uint16_t RF_Tx_Path_Compensation_Value;
	uint16_t RF_Rx_Path_Compensation_Value;

	put_le16(cmd,  RF_Tx_Path_Compensation_Value);
	put_le16(cmd+2,  RF_Rx_Path_Compensation_Value);

	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_WR_RF_PATH_COMPENSATION), cmd,4);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Write_RF_Path_Compensation SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Write_RF_Path_Compensation FAILED\r\n");
	}
	return rc;
}

#endif

/*
 *  Command: HCI_LE_Set_Privacy_Mode
 *  OGF    : 0x08
 *  OCF    : 0x004E
 *  Opcode : 0x204E
 *  Param  : Peer_Identity_Address_Type,
			 Peer_Identity_Address,
			 Privacy_Mode
 *  Return : Status
 */
int
HCI_LE_Set_Privacy_Mode(void){

	int rc;
	uint8_t cmd[8];
	uint8_t Peer_Identity_Address_Type;
	uint8_t Peer_Identity_Address[6];
	uint8_t Privacy_Mode;

	//cmd[0] = Peer_Identity_Address_Type;
	//memcpy(&cmd[1], Peer_Identity_Address,6);
	//cmd[8] = Privacy_Mode;

	memcpy(&cmd[0],SIM_SRAM+0xa1c ,8);
	printf("Peer_Identity_Address_Type = %0x \r\n",cmd[0]);
	int i;
	printf("Peer_Identity_Address : ");
	for(i=0;i<6;i++){
		printf("%x ",cmd[1+i]);
	}
	printf("\n");
	printf("Privacy_Mode = %0x \r\n",cmd[7]);
	rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_PRIVACY_MODE), cmd,8);

	if(rc == CMD_SUCCEED){
		iot_printf(" \r\n HCI_LE_Set_Privacy_Mode SUCCEED\r\n");
	}else{
		iot_printf(" \r\n HCI_LE_Set_Privacy_Mode FAILED\r\n");
	}
	return rc;
}

int
HCI_LE_Read_Rem_Version(void)
{
    uint8_t buf[sizeof(uint16_t)];
	uint16_t handle;
	
	handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	iot_printf("\r\n Read_Rem_Version start handle:%X \r\n", handle);
    put_le16(buf, handle);
    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LINK_CTRL, BLE_HCI_OCF_RD_REM_VER_INFO),
                                buf, sizeof(buf), NULL, 0, NULL);
}

	
/*
*send  acl data
*
*data:incrementing pattern (starting from 0)
*/
#if GAP_CONNECT
int
bletest_send_acl_packet(void)
{
    int i;
	int rc;
    struct os_mbuf *om = NULL;
    uint16_t handle;
    uint8_t *dptr;
    uint16_t len = 8;
	struct ble_hs_conn *conn;
	handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	conn = ble_hs_conn_find_by_idx(0);

	if(handle == conn->bhc_handle){
        printf("conn exist\n");
	}

    om = os_msys_get_pkthdr(len, 0);
	
    if (om){
        om->om_len = len;
        /* Put the HCI header in the mbuf */
		printf("bletest_send_acl_packet handle:%0x\n",handle);
		dptr = om->om_data;
        /* Fill with incrementing pattern (starting from 0) */
        for (i = 0; i < len; ++i) {
            *dptr = i;
            ++dptr;
        }
        /* Transmit it */
        OS_MBUF_PKTHDR(om)->omp_len = om->om_len;
		printf("bletest_send_acl_packet, om->om_data:%X, om->om_len:%0x\n", om->om_data, om->om_len);

        rc = ble_hs_hci_acl_tx_now(conn, &om);
    }else{
		printf("os_msys_get_pkthdr malloc err\n");
	}
	
    return rc;
}
#else
int
bletest_send_acl_packet(void)
{
    int i;
	int rc;
    struct os_mbuf *om = NULL;
    uint16_t handle;
    uint8_t *dptr;
    uint8_t len = 8;
	struct ble_hs_conn *conn;
	handle = HW_READ_REG(SIM_SRAM+0xa5c) & 0xfff;
	conn = ble_hs_conn_find_by_idx(0);

	if(handle == conn->bhc_handle){
        printf("conn exist\n");
	}

    if (os_msys_num_free() >= 4) {
        om = os_msys_get_pkthdr(len + 4, sizeof(struct ble_mbuf_hdr));
    }
	
    if (om){
        om->om_len = len;
        /* HCI header will be put in ble_hs_hci_acl_tx_now */
		printf("bletest_send_acl_packet handle:%0x\n",handle);
        //put_le16(om->om_data, handle);
        //put_le16(om->om_data + 2, len);
        dptr = om->om_data;

		/*put l2cap header here, CID(0xffff) is dummy*/
        put_le16(dptr, len - 4);
        dptr[2] = 0xff;
        dptr[3] = 0xff;
        dptr += 4;
        len -= 4;
		
        /* Fill data with fixed pattern (0) */
        for (i = 0; i < len; ++i) {
            *dptr = 0;
            ++dptr;
        }
		
        /* Transmit it */
        OS_MBUF_PKTHDR(om)->omp_len = om->om_len;
		printf("bletest_send_acl_packet om->om_len:%0x\n",om->om_len);

		
        rc = ble_hs_hci_acl_tx_now(conn, &om);
    }
    return rc;
}

#endif
/* List of OCF for vendor-specific debug commands (OGF = 0x3F) */

extern int ble_ll_conn_get_active_conn(void);

start_scan_test(void)
{
	HCI_LE_Set_Scan_Parameters();
	HCI_LE_Set_Scan_Enable();
}


/*

Step 1: 
Step 2:
Step 3: 

*/
int ble_hci_test_set_ranrom_addr(){
	HCI_LE_Rand();
}

//***********************************************//
//0x161010fc
//bit[31] send_st
//bit[30] adv_en
//bit[29] scan_en
//bit[28] filter_duplicates
//bit[27] 1:hci command send success
//		  0:hci command send failed		
//bit[26] 1: set ACL data flag

//****************simulation function**************//




void hci_sim(void){
	u32 sim_params;
	u32 send_begin;
	u32 send_end;	
	u16 op_test;
	int rc;
	hal_sleep(1);
	//lmac_Wait(2000);
	//HCI_Reset_Command();
	while(1){
#if 0
		if(g_sim_hci_sched & BIT(31)){
			ocf = g_sim_hci_sched &0xFFFF;
			printf("sim ocf:%X\n", ocf);
			switch(ocf){
				case 0x0001:
					HCI_CB_Set_Event_Mask_Command();
					HCI_LE_Read_Rem_Version();
					break;
				case 0x0002:
					HCI_LE_Set_PHY(BIT(1), BIT(1), 0);	//2M PHY
					break;
				case 0x0003:
					HCI_LE_Set_PHY(BIT(2), BIT(2), 1);	//CODE PHY S=2
					break;
				case 0x0004:
					HCI_LE_Set_PHY(BIT(2), BIT(2), 2);	//CODE PHY S=8
					break;
				default:
					iot_printf("sim ocf unkown !!!\n");
					break;
			}
			g_sim_hci_sched = 0;
		}
#endif		
		sim_params = HW_READ_REG(HCI_REG);
		send_begin = (sim_params >> 31) & 0x1;
	    op_test = sim_params & 0xffff;

		if(send_begin == 1){
			iot_printf("\r\nHci command sending begin,op_test:0x%X\r\n", op_test);
			
			if(BLE_HCI_OGF(op_test) == BLE_HCI_OGF_LINK_CTRL){
				switch(BLE_HCI_OCF(op_test)){
					case BLE_HCI_OCF_DISCONNECT_CMD:
						rc = HCI_Disconnect_Command();
						break;
					case BLE_HCI_OCF_RD_REM_VER_INFO:
						rc = HCI_LE_Read_Rem_Version();
						break;
					default:
						iot_printf("########ocf value is illegal##########\n\n");
						break;	
				}
			}
			
			if(BLE_HCI_OGF(op_test) == BLE_HCI_OGF_CTLR_BASEBAND){
				switch(BLE_HCI_OCF(op_test)){
					case BLE_HCI_OCF_CB_WR_AUTH_PYLD_TMO:
						rc = HCI_Write_Authenticated_Payload_Timeout_Command();
						break;
					case BLE_HCI_OCF_CB_SET_EVENT_MASK:
						rc = HCI_CB_Set_Event_Mask_Command();
					default:
						iot_printf("########ocf value is illegal##########\n\n");
						break;	
				}
			}

			if(BLE_HCI_OGF(op_test) == BLE_HCI_OGF_STATUS_PARAMS){
				switch(BLE_HCI_OCF(op_test)){
					case BLE_HCI_OCF_RD_RSSI :
						rc = BLE_HCI_Read_RSSI_Command();
						break;
					default:
						iot_printf("########ocf value is illegal##########\n\n");
					break;
				}
			}

			if(BLE_HCI_OGF(op_test) == BLE_HCI_OGF_VENDOR){
				switch(BLE_HCI_OCF(op_test)){
					case BLE_CONN_ACTIVE_CONN_TEST_MODE :
						rc = ble_ll_conn_get_active_conn();
						break;
					default:
						iot_printf("########ocf value is illegal##########\n\n");
					break;
				}
			}

			
			if(BLE_HCI_OGF(op_test) == BLE_HCI_OGF_LE){
				switch(BLE_HCI_OCF(op_test)){
				case 0x0001:
					rc = HCI_LE_Set_Event_Mask_Command();
					break;	
				case 0x0002:
					rc = HCI_LE_Read_Buffer_Size_Command();
					break;
				case 0x0003:
					rc = HCI_LE_Read_Local_Supported_freatures_Command();
					break;
				case 0x0005:
					rc = HCI_LE_Set_Random_Address_Command();
					break;
				case 0x0006:
					rc = HCI_LE_Set_Advertising_Parameters_Command();
					break;
				case 0x0007:
					rc = HCI_LE_Read_Advertising_Channel_Tx_Power_Command();
					break;
				case 0x0008:
					rc = HCI_LE_Set_Advertising_Data();
					break;
				case 0x0009:
					rc = HCI_LE_Set_Scan_Response_Data();
					break;
				case 0x000a:
					rc = HCI_LE_Set_Adevertise_Enable();
					break;
				case 0x000b:
					rc = HCI_LE_Set_Scan_Parameters();
					break;
				case 0x000c:
					rc = HCI_LE_Set_Scan_Enable();
					break;
				case 0x000d:
					rc = HCI_LE_Create_Connection();
					break;
				case 0x000e:
					rc = HCI_LE_Create_Connection_Cancel();
					break;
				case 0x000f:
					rc = HCI_LE_Read_White_List_Size();
					break;
				case 0x0010:
					rc = HCI_LE_Clear_White_List();
					break;
				case 0x0011:
					rc = HCI_LE_Add_Device_To_White_List();
					break;
				case 0x0012:
					rc = HCI_LE_Remove_Device_From_White_list();
					break;
				case 0x0013:
					rc = HCI_LE_Connect_Updata();
					break;
				case 0x0014:
					rc = HCI_LE_Set_Host_Channel_Classification();
					break;
				case 0x0015:
					rc = HCI_LE_Read_Channel_Map();
					break;
				case 0x0016:
					rc = HCI_LE_Read_Remote_Features();
					break;
				case 0x0017:
					rc = HCI_LE_Encrypt();
					break;
				case 0x0018:
					rc = HCI_LE_Rand();
					break;
				case 0x0019:
					rc = HCI_LE_Start_Encryption();
					break;
				case 0x001a:
					rc = HCI_LE_Long_Term_Key_Request_Reply();
					break;
				case 0x001b:
					rc = HCI_LE_Long_Term_Key_Request_Negative_Reply();
					break;
				case 0x001c:
					rc = HCI_LE_Read_Supported_States();
					break;
				case 0x001d:
					rc = HCI_LE_Receiver_Test();
					break;
				case 0x001e:
					rc = HCI_LE_Transmitter_Test();
					break;
			    case 0x1f:
				    rc = HCI_LE_Test_End();
				    break;
			    case 0x20:
				    rc = HCI_LE_Remote_Connection_Parameter_Request_Reply();
				    break;
			    case 0x21:
				    rc = HCI_LE_Remote_Connection_Paramter_Request_Negative_Reply();
				    break;
			    case 0x22:
				    rc = HCI_LE_Set_Data_Length();
				    break;
			    case 0x23:
				    rc = HCI_LE_Read_Suggested_Default_Data_Length();
				    break;
			    case 0x24:
				    rc = HCI_LE_Write_Suggested_Defalt_Data_Length();
				    break;
			    case 0x25:
				    rc = HCI_LE_Read_Local_P_256_Public_Key();
				    break;
			    case 0x26:
				    rc = HCI_LE_Generate_DHKey();
				    break;
			    case 0x27:
				    rc = HCI_LE_Add_Device_To_Resolving_List();
				    break;
			    case 0x28:
				    rc = HCI_LE_Remove_Device_From_Resolving_List();
				    break;
			    case 0x29:
				    rc = HCI_LE_Remove_Clear_Resolving_List();
				    break;
			    case 0x2a:
				    rc = HCI_LE_Read_Resolving_List_Size();
				    break;
			    case 0x2b:
				    rc = HCI_LE_Read_Peer_Resolvable_Address();
				    break;
			    case 0x2c:
				    rc = HCI_LE_Read_Local_Resolvable_Address();
				    break;
			    case 0x2d:
				    rc = HCI_LE_Set_Address_Resolution_Enable();
				    break;
			    case 0x2e:
				    rc = HCI_LE_Set_Resolvable_Private_Address_Timeout();
				    break;
			    case 0x2f:
				    rc = HCI_LE_Read_Maximum_Data_Length();
				    break;
			    case 0x30:
				    rc = HCI_LE_Read_PHY();
				    break;
			    case 0x31:
				    rc = HCI_LE_Set_Default_PHY();
				    break;
			    case 0x32:
				    rc = HCI_LE_Set_PHY();
				    break;
			    case 0x33:
				    rc = HCI_LE_Enhanced_Receiver_Test();
				    break;
			    case 0x34:
				    rc = HCI_LE_Enhanced_Tranmitter_Test();
				    break;
			    case 0x35:
				    rc = HCI_LE_Set_Advertising_Set_Random_Address();
				    break;
#if (MYNEWT_VAL_BLE_VERSION >= 50)
			    case 0x36:
				    rc = HCI_LE_Set_ExtendedAdvertising_Parameters();
				    break;
			    case 0x37:
				    rc = HCI_LE_Set_Extend_Advertising_Data();
				    break;
			    case 0x38:
				    rc = HCI_LE_Set_Extended_Scan_Responese_Data();
				    break;
			    case 0x39:
				    rc = HCI_LE_Set_Extended_Advertising_Enable();
				    break;
			    case 0x3a:
				    rc = HCI_LE_Read_Maximum_Advertising_Data_Length();
				    break;
			    case 0x3b:
				    rc = HCI_LE_Read_Number_Of_Supported_Advertising_Sets();
				    break;
			    case 0x3c:
				    rc = HCI_LE_Remove_Advertising_Sets();
				    break;
			    case 0x3d:
				    rc = HCI_LE_Clear_Advertising_Sets();
				    break;
			    case 0x3e:
				    rc = HCI_LE_Set_Periodic_Advertising_Parameters();
				    break;
			    case 0x3f:
				    rc = HCI_LE_Set_Periodic_Advertising_Data_Timeout();
				    break;
			    case 0x40:
				    rc = HCI_LE_Set_Periodic_Advertising_Enable();
				    break;
			    case 0x41:
				    rc = HCI_LE_Set_Extended_Scan_Parameters();
				    break;
			    case 0x42:
				    rc = HCI_LE_Set_Extended_Scan_Enable();
				    break;
			    case 0x43:
				    rc = HCI_LE_Extended_Create_Connection();
				    break;
			    case 0x44:
				    rc = HCI_LE_Periodic_Advertising_Create_Sync();
				    break;
			    case 0x45:
				    rc = HCI_LE_Periodic_Advertising_Create_Sync_Cancel();
				    break;
			    case 0x46:
				    rc = HCI_LE_Periodic_Advertising_Terminate_Sync();
				    break;
			    case 0x47:
				    rc = HCI_LE_Add_Device_To_Periodic_Advertiser_List();
				    break;
			    case 0x48:
				    rc = HCI_LE_Remove_Device_From_Periodic_Advertiser_List();
				    break;
			    case 0x49:
				    rc = HCI_LE_Clear_Periodic_Advertiser_List();
				    break;
			    case 0x4a:
				    rc = HCI_LE_Read_Periodic_Advertiser_List_Size();
				    break;
			    case 0x4b:
				    rc = HCI_LE_Read_transmit_Power();
				    break;
			    case 0x4c:
				    rc = HCI_LE_Read_RF_Path_Compensation_List();
				    break;
			    case 0x4d:
				    rc = HCI_LE_Write_RF_Path_Compensation();
				    break;
#endif
			    case 0x4e:
				    rc = HCI_LE_Set_Privacy_Mode();
				    break;

				case 0x64:
					 rc = bletest_send_acl_packet();
					 hal_sleep(1);
					 break;
				default:
					 iot_printf("########ocf value is illegal##########\n\n");
					 break;
				}
			}
			if(rc == 0){
				/////////set hci command send success flag///////
				HW_WRITE_REG_BIT(HCI_REG,27,27,0x1);
			}
			else{
				iot_printf("########hci command send failed,ogf = %x,ocf = %x,rc = %x########\n\n",
					BLE_HCI_OGF(op_test),BLE_HCI_OCF(op_test),rc);
			}
			HW_WRITE_REG_BIT(HCI_REG,31,31,0x0);//clear hci command send begin flag
			HW_WRITE_REG_BIT(HCI_REG,25,25,0x1);//set hci command send end flag

			HW_WRITE_REG_BIT(HCI_REG,25,25,0x0);//clear hci command send end flag
			HW_WRITE_REG_BIT(HCI_REG,27,27,0x0);//clear hci command send success flag

		}
	
	}
}

TaskHandle_t hci_task_h;

void hci_test_app(void){

	//HCI_LE_Set_Event_Mask_Command();
	xTaskCreate(hci_sim, "ble_hci", 1024,
                NULL, tskIDLE_PRIORITY + 1, &hci_task_h);
}


#endif

