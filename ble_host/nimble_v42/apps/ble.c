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

#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char gap_name[] = "ble_fpga";

static uint8_t own_addr_type;

static int ble_hci_sync_ok = 0;


#if 0
static void start_advertise(void);

/* Convert from ms to 0.625ms units */
#define ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)


#define TEST_SCAN_INTERVAL_MS 100
#define TEST_SCAN_WINDOW_MS   100
#define TEST_SCAN_INTERVAL    ADV_SCAN_UNIT(TEST_SCAN_INTERVAL_MS)
#define TEST_SCAN_WINDOW      ADV_SCAN_UNIT(TEST_SCAN_WINDOW_MS)


static void
put_ad(uint8_t ad_type, uint8_t ad_len, const void *ad, uint8_t *buf,
       uint8_t *len)
{
    buf[(*len)++] = ad_len + 1;
    buf[(*len)++] = ad_type;

    memcpy(&buf[*len], ad, ad_len);

    *len += ad_len;
}

static void
update_adv_data(void)
{
    int rc;
	struct ble_hs_adv_fields fields;

	memset(&fields, 0, sizeof(fields));
	
	 /*
	  * Advertise two flags:
	  * 	 o Discoverability in forthcoming advertisement (general)
	  * 	 o BLE-only (BR/EDR unsupported)
	  */
	 fields.flags = BLE_HS_ADV_F_DISC_GEN |
					 BLE_HS_ADV_F_BREDR_UNSUP;
	
	 /*
	  * Indicate that the TX power level field should be included; have the
	  * stack fill this value automatically.  This is done by assigning the
	  * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
	  */
	 fields.tx_pwr_lvl_is_present = 1;
	 fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
	
	 fields.name = (uint8_t *)gap_name;
	 fields.name_len = strlen(gap_name);
	 fields.name_is_complete = 1;
	
	 rc = ble_gap_adv_set_fields(&fields);
	 if (rc != 0) {
		 iot_printf("ble_gap_adv_set_fields err:%d\n", rc);
		 return;
	 }

}

static void print_adv_fields(const struct ble_hs_adv_fields *fields)
{
    char s[BLE_HS_ADV_MAX_SZ];

    if (fields->name != NULL) {
        assert(fields->name_len < sizeof s - 1);
        memcpy(s, fields->name, fields->name_len);
        s[fields->name_len] = '\0';
        iot_printf("    name(%scomplete)=%s\n",
                    fields->name_is_complete ? "" : "in", s);
    }
}


static int
gap_event_cb(struct ble_gap_event *event, void *arg)
{
	int rc;
	struct ble_hs_adv_fields fields;
	
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
		
        if (event->connect.status) {
            start_advertise();
        }
		iot_printf("BLE_GAP_EVENT_CONNECT ok\n");
        break;

    case BLE_GAP_EVENT_DISCONNECT:
		iot_printf("BLE_GAP_EVENT_DISCONNECT reson:%d\n", event->disconnect.reason);
        start_advertise();
        break;
	case BLE_GAP_EVENT_ADV_COMPLETE:
		break;

    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                     event->disc.length_data);
        if (rc != 0) {
            return 0;
        }

        /* An advertisment report was received during GAP discovery. */
        print_adv_fields(&fields);

        return 0;
    }

    return 0;
}

static void
start_scan(void)
{
	struct ble_gap_disc_params scan_param =
		{ .passive = 0, .filter_duplicates = 0, .itvl =
		  TEST_SCAN_INTERVAL, .window = TEST_SCAN_WINDOW};
	ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &scan_param, gap_event_cb, NULL);
}

static void
start_advertise(void)
{
    struct ble_gap_adv_params advp;
    int rc;

    update_adv_data();

    memset(&advp, 0, sizeof advp);
    advp.conn_mode = BLE_GAP_CONN_MODE_UND;
    advp.disc_mode = BLE_GAP_DISC_MODE_GEN;
	advp.itvl_max = 300;
	advp.itvl_min = 300;
	advp.channel_map = 0x01;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &advp, gap_event_cb, NULL);
    assert(rc == 0);
}
#endif

static void
app_ble_sync_cb(void)
{
#ifdef CFG_B2B_SIMU
	hci_test_app();
#else
    int rc;
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    assert(rc == 0);

	ble_hci_sync_ok = 1;
#endif


	ble_smart_cfg_startup();
//	ble_adv_cfg_startup();
}

void
nimble_host_task(void *param)
{
    ble_hs_cfg.sync_cb = app_ble_sync_cb;

	iot_printf("nimble_host_task\n");

    //ble_svc_gap_device_name_set(gap_name);

    nimble_port_run();
}

void ble_hci_sync_init(void)
{
	ble_hci_sync_ok = 0;
}


int ble_hci_sync_get(void)
{
	return ble_hci_sync_ok;
}


