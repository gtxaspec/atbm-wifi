/*
 * ble_AT_cmd.c
 *
 *  Created on: 2021-1-8
 *      Author: NANXIAOFENG
 */

#include "nimble/hci_common.h"
#include "host/ble_gap.h"
#include "cli.h"


#ifndef CONFIG_LINUX_BLE_STACK_LIB

void ble_smart_cfg_test_ok(u8 *ssid, u8 *pwd);

static int ble_test_gap_event(struct ble_gap_event *event, void *arg)
{
	return 0;
}

void ble_scan_test_start(char *pLine)
{
	struct ble_gap_disc_params params = {0};
	int rc;
	
	params.limited = 0;
	params.passive = 1;
	params.itvl = 300;
	params.window = 300;
	params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;
	params.filter_duplicates = 0;
	rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, ble_test_gap_event, NULL);
	if(rc){
		iot_printf("ERROR(%d)\n",rc);
	}
	else{
		iot_printf("SCAN START\n");
	}
}

void ble_scan_test_stop(char *pLine)
{
    int rc;

    rc = ble_gap_disc_cancel();
	if(rc){
		iot_printf("ERROR(%d)\n",rc);
	}
	else{
		iot_printf("SCAN STOP\n");
	}
}

void ble_smt_test_ok(char *pLine)
{
	char *str;
	u8 ssid[33]={0};
	u8 pwd[65]={0};

	str = cli_get_token(&pLine);
	strcpy(ssid, str);
	str = cli_get_token(&pLine);
	strcpy(pwd, str);
	ble_smart_cfg_test_ok(ssid, pwd);
}
void ble_smt_start(char* pLine)
{
	//atbm ble smartconfig start
	ble_smart_cfg_startup();
}
void ble_smt_stop(char* pLine)
{
	//atbm ble smartconfig stop
	ble_smart_cfg_stop();
}

void ble_test_quit(char* pLine)
{
	//ble_ioctl_exit();
}


struct cli_cmd_struct ATCommands_ble[] =
{
	{.cmd ="AT+SMT_TEST",				.fn = ble_smt_test_ok,			.next = (void*)0},
	{.cmd = "AT+SMT_START",				.fn = ble_smt_start,			.next = (void*)0},
	{.cmd = "AT+SMT_STOP",				.fn = ble_smt_stop,			.next = (void*)0},
	{.cmd = "AT+BLE_QUIT",				.fn = ble_test_quit,			.next = (void*)0},
};
#endif

void atcmd_init_ble(void)
{
#if (CONFIG_BLE_PTS_TEST_MOD == 0)
	ble_smart_gatt_svcs_init();
/*** NOTICE if use customer sevices please ADD to here, comment out last line ***/
//	ble_hualai_gatt_svcs_init();
#endif

#ifndef CONFIG_LINUX_BLE_STACK_LIB
	cli_add_cmds(ATCommands_ble,sizeof(ATCommands_ble)/sizeof(ATCommands_ble[0]));
#endif
}



