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

#include "os/os.h"
#include "atbm_hal.h"
#include "host/ble_hs.h"

struct log ble_hs_log;

void
ble_hs_log_mbuf(const struct os_mbuf *om)
{
    uint8_t u8_data;
    int i;

    for (i = 0; i < OS_MBUF_PKTLEN(om); i++) {
		os_mbuf_copydata(om, i, 1, &u8_data);
		BLE_HS_LOG(DEBUG, "0x%02x ", u8_data);
    }
}

void
ble_hs_log_flat_buf(const void *data, int len)
{
    const uint8_t *u8ptr;
    int i;

    u8ptr = data;
    for (i = 0; i < len; i++) {
        BLE_HS_LOG(DEBUG, "0x%02x ", u8ptr[i]);
    }
}




char* DecodeOGF(unsigned short OGF)
{
  switch(OGF)
  {
    case BLE_HCI_OGF_LINK_CTRL            : return "HCI_LINK_CMD";
    case BLE_HCI_OGF_LINK_POLICY          : return "HCI_LINK_POLICY";
    case BLE_HCI_OGF_CTLR_BASEBAND		  : return "HCI_CTLR_BB";
    case BLE_HCI_OGF_INFO_PARAMS		  : return "HCI_INFOR_PARAM";
    case BLE_HCI_OGF_STATUS_PARAMS        : return "HCI_STATUS_PARAM";
    case BLE_HCI_OGF_TESTING              : return "HCI_TESTING";
    case BLE_HCI_OGF_LE		              : return "HCI_LE_CMD";
    case BLE_HCI_OGF_VENDOR               : return "HCI_VENDOR";
  }

  return "UNKNOWN OGF";
}

char* DecodeOCF(unsigned short opcode)
{
#if (DEFAULT_DEBUG_LEVEL > MODLOG_LEVEL_INFO)
	unsigned short OGF = BLE_HCI_OGF(opcode);
	unsigned short OCF = BLE_HCI_OCF(opcode);

	if(BLE_HCI_OGF_LE==OGF){
		switch(OCF)
		{
		  /* List of OCF for LE commands (OGF = 0x08) */
		  case BLE_HCI_OCF_LE_SET_EVENT_MASK			   : return "LE_SET_EV_MASK";				
		  case BLE_HCI_OCF_LE_RD_BUF_SIZE				   : return "LE_RD_BUF_SZ";			
		  case BLE_HCI_OCF_LE_RD_LOC_SUPP_FEAT			   : return "LE_RD_LOC_SUPP_FEAT";		   
		  case BLE_HCI_OCF_LE_SET_RAND_ADDR 			   : return "LE_S_RAND_ADDR"; 			
		  case BLE_HCI_OCF_LE_SET_ADV_PARAMS			   : return "LE_S_ADV_PARAM";			
		  case BLE_HCI_OCF_LE_RD_ADV_CHAN_TXPWR 		   : return "LE_RD_ADV_CHAN_TXPWR";		
		  case BLE_HCI_OCF_LE_SET_ADV_DATA				   : return "LE_ADV_DATA";				
		  case BLE_HCI_OCF_LE_SET_SCAN_RSP_DATA 		   : return "LE_SET_SCAN_RSP_D"; 		
		  case BLE_HCI_OCF_LE_SET_ADV_ENABLE			   : return "LE_ADV_EN";			
		  case BLE_HCI_OCF_LE_SET_SCAN_PARAMS			   : return "LE_SET_SCAN_PARAM";			
		  case BLE_HCI_OCF_LE_SET_SCAN_ENABLE			   : return "LE_SCAN_EN";			
		  case BLE_HCI_OCF_LE_CREATE_CONN				   : return "LE_CREATE_CONN";				
		  case BLE_HCI_OCF_LE_CREATE_CONN_CANCEL		   : return "LE_CREATE_CONN_CL";		
		  case BLE_HCI_OCF_LE_RD_WHITE_LIST_SIZE		   : return "LE_RD_WHLIST_SZ";		
		  case BLE_HCI_OCF_LE_CLEAR_WHITE_LIST			   : return "LE_CLEAR_WHLIST";			
		  case BLE_HCI_OCF_LE_ADD_WHITE_LIST			   : return "LE_ADD_WHLIST";			
		  case BLE_HCI_OCF_LE_RMV_WHITE_LIST			   : return "LE_RM_WHLIST";			
		  case BLE_HCI_OCF_LE_CONN_UPDATE				   : return "LE_CONN_UP";				
		  case BLE_HCI_OCF_LE_SET_HOST_CHAN_CLASS		   : return "LE_S_H_CH_CLASS";		
		  case BLE_HCI_OCF_LE_RD_CHAN_MAP				   : return "LE_RD_CH_MAP";				
		  case BLE_HCI_OCF_LE_RD_REM_FEAT				   : return "LE_RD_REM_FEAT";				
		  case BLE_HCI_OCF_LE_ENCRYPT					   : return "LE_ENCRYPT";					
		  case BLE_HCI_OCF_LE_RAND						   : return "LE_RAND";						
		  case BLE_HCI_OCF_LE_START_ENCRYPT 			   : return "LE_START_ENC"; 			
		  case BLE_HCI_OCF_LE_LT_KEY_REQ_REPLY			   : return "LE_LTKEY_R_REPLY";			
		  case BLE_HCI_OCF_LE_LT_KEY_REQ_NEG_REPLY		   : return "LE_LTKEY_R_NEG_REPLY";		
		  case BLE_HCI_OCF_LE_RD_SUPP_STATES			   : return "LE_RD_SUPP_ST";			
		  case BLE_HCI_OCF_LE_RX_TEST					   : return "LE_RX_TEST";					
		  case BLE_HCI_OCF_LE_TX_TEST					   : return "LE_TX_TEST";					
		  case BLE_HCI_OCF_LE_TEST_END					   : return "LE_TEST_END";					
		  case BLE_HCI_OCF_LE_REM_CONN_PARAM_RR 		   : return "LE_REM_CONN_RR"; 		
		  case BLE_HCI_OCF_LE_REM_CONN_PARAM_NRR		   : return "LE_REM_CONN_NRR";		
		  case BLE_HCI_OCF_LE_SET_DATA_LEN				   : return "LE_SET_DATA_LEN";				
		  case BLE_HCI_OCF_LE_RD_SUGG_DEF_DATA_LEN		   : return "LE_RD_SUGG_DLEN";		
		  case BLE_HCI_OCF_LE_WR_SUGG_DEF_DATA_LEN		   : return "LE_WR_SUGG_DLEN";		
		  case BLE_HCI_OCF_LE_RD_P256_PUBKEY			   : return "LE_RD_P256_PUBKEY";			
		  case BLE_HCI_OCF_LE_GEN_DHKEY 				   : return "LE_GEN_DHKEY"; 				
		  case BLE_HCI_OCF_LE_ADD_RESOLV_LIST			   : return "LE_ADD_RESOLIST";			
		  case BLE_HCI_OCF_LE_RMV_RESOLV_LIST			   : return "LE_RM_RESOLIST";			
		  case BLE_HCI_OCF_LE_CLR_RESOLV_LIST			   : return "LE_CL_RESOLIST";		   
		  case BLE_HCI_OCF_LE_RD_RESOLV_LIST_SIZE		   : return "LE_RD_RESOLIST_SZ";		
		  case BLE_HCI_OCF_LE_RD_PEER_RESOLV_ADDR		   : return "LE_RD_PEER_RESO_ADDR";		
		  case BLE_HCI_OCF_LE_RD_LOCAL_RESOLV_ADDR		   : return "LE_RD_LOCAL_RESO_ADDR";		
		  case BLE_HCI_OCF_LE_SET_ADDR_RES_EN			   : return "LE_S_ADDR_RES_EN";			
		  case BLE_HCI_OCF_LE_SET_RPA_TMO				   : return "LE_S_RPA_TO";				
		  case BLE_HCI_OCF_LE_RD_MAX_DATA_LEN			   : return "LE_RD_MAX_DLN";			
		  case BLE_HCI_OCF_LE_RD_PHY					   : return "LE_RD_PHY";					
		  case BLE_HCI_OCF_LE_SET_DEFAULT_PHY			   : return "LE_S_DEF_PHY";			
		  case BLE_HCI_OCF_LE_SET_PHY					   : return "LE_S_PHY";					
		  case BLE_HCI_OCF_LE_ENH_RX_TEST				   : return "LE_ENH_RX_TEST";				
		  case BLE_HCI_OCF_LE_ENH_TX_TEST				   : return "LE_ENH_TX_TEST";				
		  case BLE_HCI_OCF_LE_SET_ADV_SET_RND_ADDR		   : return "LE_S_ADV_RND_ADDR";		
		  case BLE_HCI_OCF_LE_SET_EXT_ADV_PARAM 		   : return "LE_S_EXADV_PARAM"; 		
		  case BLE_HCI_OCF_LE_SET_EXT_ADV_DATA			   : return "LE_S_EXADV_DATA";			
		  case BLE_HCI_OCF_LE_SET_EXT_SCAN_RSP_DATA 	   : return "LE_S_EXSCAN_RSPD"; 	
		  case BLE_HCI_OCF_LE_SET_EXT_ADV_ENABLE		   : return "LE_S_EXADV_EN";		
		  case BLE_HCI_OCF_LE_RD_MAX_ADV_DATA_LEN		   : return "LE_RD_MAX_ADV_DLEN";		
		  case BLE_HCI_OCF_LE_RD_NUM_OF_ADV_SETS		   : return "LE_RD_NUM_ADV_SET";		
		  case BLE_HCI_OCF_LE_REMOVE_ADV_SET			   : return "LE_RM_ADV_SET";			
		  case BLE_HCI_OCF_LE_CLEAR_ADV_SETS			   : return "LE_CL_ADV_SET";			
		  case BLE_HCI_OCF_LE_SET_PER_ADV_PARAMS		   : return "LE_S_PERADV_PARAM";		
		  case BLE_HCI_OCF_LE_SET_PER_ADV_DATA			   : return "LE_S_PERADV_DATA";			
		  case BLE_HCI_OCF_LE_SET_PER_ADV_ENABLE		   : return "LE_S_PERADV_EN";		
		  case BLE_HCI_OCF_LE_SET_EXT_SCAN_PARAM		   : return "LE_S_EXSCAN_PARAM";		
		  case BLE_HCI_OCF_LE_SET_EXT_SCAN_ENABLE		   : return "LE_S_EXSCAN_EN";		
		  case BLE_HCI_OCF_LE_EXT_CREATE_CONN			   : return "LE_EXT_CREATE_CONN";			
		  case BLE_HCI_OCF_LE_PER_ADV_CREATE_SYNC		   : return "LE_PERADV_CREATE_SYC";		
		  case BLE_HCI_OCF_LE_PER_ADV_CREATE_SYNC_CANCEL   : return "LE_PERADV_CREATE_SYC_CL";
		  case BLE_HCI_OCF_LE_PER_ADV_TERM_SYNC 		   : return "LE_PERADV_TERM_SYC"; 		
		  case BLE_HCI_OCF_LE_ADD_DEV_TO_PER_ADV_LIST	   : return "LE_ADD_DEV_TO_PERADV";	
		  case BLE_HCI_OCF_LE_REM_DEV_FROM_PER_ADV_LIST    : return "LE_RM_DEV_FROM_PE_ADV";
		  case BLE_HCI_OCF_LE_CLEAR_PER_ADV_LIST		   : return "LE_CL_PERADV";		
		  case BLE_HCI_OCF_LE_RD_PER_ADV_LIST_SIZE		   : return "LE_RD_PERADV_SZ";		
		  case BLE_HCI_OCF_LE_RD_TRANSMIT_POWER 		   : return "LE_RD_TX_POWER"; 		
		  case BLE_HCI_OCF_LE_RD_RF_PATH_COMPENSATION	   : return "LE_RD_RF_PATH_COMP";	
		  case BLE_HCI_OCF_LE_WR_RF_PATH_COMPENSATION	   : return "LE_WR_RF_PATH_COMP";	
		  case BLE_HCI_OCF_LE_SET_PRIVACY_MODE			   : return "LE_SET_PRIV_MD";
		  default			   : return "LE_UNKNOW";
		}
	}
	else if(BLE_HCI_OGF_LINK_CTRL==OGF){
		switch(OCF)
		{
		  /* List of OCF for Link Control commands (OGF=0x01) */
		  case BLE_HCI_OCF_DISCONNECT_CMD		: return "HCIDISCON_CMD ";
		  case BLE_HCI_OCF_RD_REM_VER_INFO		: return "RD_REM_VER_INFO"; 
		  default			   : return "LC_UNKNOW";
		}
	}
	else if(BLE_HCI_OGF_CTLR_BASEBAND==OGF){
		switch(OCF)
		{
		  /* List of OCF for Controller and Baseband commands (OGF=0x03) */
		  case BLE_HCI_OCF_CB_SET_EVENT_MASK	   :  return "C_SET_EVENT_MASK";	   
		  case BLE_HCI_OCF_CB_RESET 			   :  return "C_RESET";			   
		  case BLE_HCI_OCF_CB_READ_TX_PWR		   :  return "C_RD_TX_PWR";		  
		  case BLE_HCI_OCF_CB_SET_CTLR_TO_HOST_FC  :  return "C_SET_CTLR_TO_HOST_FC";
		  case BLE_HCI_OCF_CB_HOST_BUF_SIZE 	   :  return "C_HOST_BUF_SZ";	  
		  case BLE_HCI_OCF_CB_HOST_NUM_COMP_PKTS   :  return "C_HOST_NUM_COMP_PKTS"; 
		  case BLE_HCI_OCF_CB_SET_EVENT_MASK2	   :  return "C_SET_EVENT_M2";	  
		  case BLE_HCI_OCF_CB_RD_AUTH_PYLD_TMO	   :  return "C_RD_AUTH_TO";   
		  case BLE_HCI_OCF_CB_WR_AUTH_PYLD_TMO	   :  return "C_WR_AUTH_TO";   
		  default			   : return "CB_UNKNOW";
	   }
   }
   else if(BLE_HCI_OGF_INFO_PARAMS==OGF){
		switch(OCF)
		{
		  /* List of OCF for Info Param commands (OGF=0x04) */
		  case BLE_HCI_OCF_IP_RD_LOCAL_VER		  : return "IP_RD_LC_VER";	  
		  case BLE_HCI_OCF_IP_RD_LOC_SUPP_CMD	  : return "IP_RD_LC_SUPP_CMD";  
		  case BLE_HCI_OCF_IP_RD_LOC_SUPP_FEAT	  : return "IP_RD_LC_SUPP_FEAT"; 
		  case BLE_HCI_OCF_IP_RD_BUF_SIZE		  : return "IP_RD_BUF_SZ";	  
		  case BLE_HCI_OCF_IP_RD_BD_ADDR		  : return "IP_RD_BD_ADDR"; 	  
		  default			   : return "IP_RD_UNKNOW";
	   }
   }  
   else if(BLE_HCI_OGF_STATUS_PARAMS==OGF){
		switch(OCF)
		{
		  /* List of OCF for Status parameters commands (OGF = 0x05) */ 	
		  case BLE_HCI_OCF_RD_RSSI	: return "HCIRD_RSSI"; 	  
		  default					: return "STATUS_UNKNOW";
	   }
   } 
   else {
		return "VENDOR_UNKNOW";
   }  
 #else
 	
 	return "HCI";
 #endif //DEFAULT_DEBUG_LEVEL
}





