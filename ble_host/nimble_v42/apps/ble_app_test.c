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


/* Connection handle */
static uint16_t conn_handle;

static uint8_t own_addr_type;

static char *atbm_ble_device_name;

static struct ble_hs_adv_fields adv_data, resp_data;
static struct ble_gap_adv_params gap_adv_params;


static atbm_ble_advertise(void);
static int atbm_ble_gap_event(struct ble_gap_event *event, void *arg);

static int
atbm_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0) {
            transport_simple_ble_connect(event, arg);
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc != 0) {
                ESP_LOGE(TAG, "No open connection with the specified handle");
                return rc;
            }
			 conn_handle = event->connect.conn_handle;
        } else {
            /* Connection failed; resume advertising. */
            simple_ble_advertise();
			conn_handle = 0;
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGD(TAG, "disconnect; reason=%d ", event->disconnect.reason);
        transport_simple_ble_disconnect(event, arg);
		conn_handle = 0;
        /* Connection terminated; resume advertising. */
        simple_ble_advertise();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        simple_ble_advertise();
        return 0;

	case BLE_GAP_EVENT_SUBSCRIBE:
		   MODLOG_DFLT(INFO, "subscribe event attr_handle=%d\n",
					   event->subscribe.attr_handle);
	
		   if (event->subscribe.attr_handle == hrs_hrm_handle) {
			   notify_state = event->subscribe.cur_notify;
			   MODLOG_DFLT(INFO, "heart rate  measurement notify state = %d\n",
						   notify_state);
		   }
		   break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                 event->mtu.conn_handle,
                 event->mtu.channel_id,
                 event->mtu.value);
        transport_simple_ble_set_mtu(event, arg);
        return 0;
    }
    return 0;
}


static atbm_ble_advertise(void){
    int rc;

    adv_data.flags = (BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);

	adv_data.name = "atbm_ble";
	adv_data.name_len = strlen("atbm_ble");
	adv_data.name_is_complete = 1;

	adv_data.appearance = 0x2A37;
	adv_data.appearance_is_present = 1;

//    adv_data.num_uuids128 = 1;
//    adv_data.uuids128_is_complete = 1;

	rc = ble_gap_adv_set_fields(&adv_data);

	if (rc != 0) {
        iot_printf("Error setting advertisement data; rc = %d", rc);
        return;
    }

	rc = ble_gap_adv_rsp_set_fields((const struct ble_hs_adv_fields *) &resp_data);
    if (rc != 0) {
        iot_printf("Error in setting scan response; rc = %d", rc);
        return;
    }

	gap_adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    gap_adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    gap_adv_params.itvl_min = 0x100;
    gap_adv_params.itvl_max = 0x100;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                           &gap_adv_params, atbm_ble_gap_event, NULL);
	
    if (rc != 0) {
        /* If BLE Host is disabled, it probably means device is already
         * provisioned in previous session. Avoid error prints for this case.*/
        if (rc == BLE_HS_EDISABLED) {
            iot_printf("BLE Host is disabled !!");
        } else {
            iot_printf("Error enabling advertisement; rc = %d", rc);
        }
        return;
    }
}




