

#ifndef _BLE_SMART_CFG_H
#define _BLE_SMART_CFG_H

//#define BLE_ADV_CFG_TIMEOUT_MS				(600*1000)


#define BLE_ADV_CFG_TRANS_END				0xF0
#define BLE_ADV_CFG_CONN_WAIT				0xF1
#define BLE_ADV_CFG_CONN_SUCC				0xF2

#define BLE_ATBM_GAP_SVR					0xBBB0
#define BLE_ATBM_GAP_SVR1					0xBBB1

#define BLE_ATBM_GAP_SVR_BR					0xBAB0
#define BLE_ATBM_GAP_SVR_BR_SUC				0xBAB1



struct ble_adv_cfg_ind {
	uint32_t status;
	uint8_t ssid[32];
	uint8_t pwd[64];
	uint8_t ssid_len;
	uint8_t pwd_len;
};


extern void ble_adv_cfg_startup(void);


#endif

