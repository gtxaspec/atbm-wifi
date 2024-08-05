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

#include "syscfg/syscfg.h"

#if MYNEWT_VAL_SHELL_CMD

#include <assert.h>

#include "atbm_os_mem.h"

#include "nimble/ble.h"
#include "nimble/nimble_opt.h"
#include "nimble/hci_common.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "host/ble_eddystone.h"
#include "host/ble_hs_id.h"
#include "services/gatt/ble_svc_gatt.h"
#include "../src/ble_hs_priv.h"

#include "cmd.h"
#include "btshell.h"
#include "cmd_gatt.h"
#include "cmd_l2cap.h"

#include "cli.h"

#define bssnz_t

#define MYNEWT_VAL_SHELL_CMD_HELP 1


#define BTSHELL_MODULE "btshell"
static int cmd_bt_shell_help(int argc, char **argv);
SRAM_CODE int
cmd_parse_conn_start_end(uint16_t *out_conn, uint16_t *out_start,
                         uint16_t *out_end)
{
    int rc;

    *out_conn = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        return rc;
    }

    *out_start = parse_arg_uint16("start", &rc);
    if (rc != 0) {
        return rc;
    }

    *out_end = parse_arg_uint16("end", &rc);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

static const struct kv_pair cmd_own_addr_types[] = {
    { "public",     BLE_OWN_ADDR_PUBLIC },
    { "random",     BLE_OWN_ADDR_RANDOM },
    { "rpa_pub",    BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT },
    { "rpa_rnd",    BLE_OWN_ADDR_RPA_RANDOM_DEFAULT },
    { NULL }
};

static const struct kv_pair cmd_peer_addr_types[] = {
    { "public",     BLE_ADDR_PUBLIC },
    { "random",     BLE_ADDR_RANDOM },
    { "public_id",  BLE_ADDR_PUBLIC_ID },
    { "random_id",  BLE_ADDR_RANDOM_ID },
    { NULL }
};

static const struct kv_pair cmd_addr_type[] = {
    { "public",     BLE_ADDR_PUBLIC },
    { "random",     BLE_ADDR_RANDOM },
    { NULL }
};



/*****************************************************************************
 * $advertise                                                                *
 *****************************************************************************/
static const struct kv_pair cmd_adv_filt_types[] = {
    { "none", BLE_HCI_ADV_FILT_NONE },
    { "scan", BLE_HCI_ADV_FILT_SCAN },
    { "conn", BLE_HCI_ADV_FILT_CONN },
    { "both", BLE_HCI_ADV_FILT_BOTH },
    { NULL }
};

#if (MYNEWT_VAL_BLE_EXT_ADV)
static struct kv_pair cmd_ext_adv_phy_opts[] = {
    { "1M",          0x01 },
    { "2M",          0x02 },
    { "coded",       0x03 },
    { NULL }
};

static bool adv_instances[BLE_ADV_INSTANCES];

SRAM_CODE static int
cmd_advertise_configure(int argc, char **argv)
{
    struct ble_gap_ext_adv_params params = {0};
    int8_t selected_tx_power;
    uint8_t instance;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    instance = parse_arg_uint8_dflt("instance", 0, &rc);
    if (rc != 0 || instance >= BLE_ADV_INSTANCES) {
        iot_printf("err instance\n");
        return rc;
    }

    if (adv_instances[instance]) {
        iot_printf("instance already configured\n");
        return rc;
    }

    memset(&params, 0, sizeof(params));

    params.legacy_pdu = parse_arg_bool_dflt("legacy", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'legacy' parameter\n");
        return rc;
    }

    if (params.legacy_pdu) {
        params.connectable = 1;
        params.scannable = 1;
    }

    params.connectable = parse_arg_bool_dflt("connectable", params.connectable, &rc);
    if (rc != 0) {
        iot_printf("err 'connectable' parameter\n");
        return rc;
    }

    params.scannable = parse_arg_bool_dflt("scannable", params.scannable, &rc);
    if (rc != 0) {
        iot_printf("err 'scannable' parameter\n");
        return rc;
    }

    params.high_duty_directed = parse_arg_bool_dflt("high_duty", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'high_duty' parameter\n");
        return rc;
    }

    params.anonymous = parse_arg_bool_dflt("anonymous", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'anonymous' parameter\n");
        return rc;
    }

    params.include_tx_power = parse_arg_bool_dflt("include_tx_power", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'include_tx_power' parameter\n");
        return rc;
    }

    params.scan_req_notif = parse_arg_bool_dflt("scan_req_notif", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'scan_req_notif' parameter\n");
        return rc;
    }

    rc = parse_arg_mac("peer_addr", params.peer.val);
    if (rc == 0) {
        params.directed = 1;

        params.peer.type = parse_arg_kv_dflt("peer_addr_type",
                                             cmd_peer_addr_types,
                                             BLE_ADDR_PUBLIC, &rc);
        if (rc != 0) {
            iot_printf("err 'peer_addr_type' parameter\n");
            return rc;
        }
    }
    else if (rc == ENOENT) {
       /* skip, no peer address provided */
    } else {
        iot_printf("err 'peer_addr' parameter\n");
        return rc;
    }


    params.directed = parse_arg_bool_dflt("directed", params.directed, &rc);
    if (rc != 0) {
        iot_printf("err 'directed' parameter\n");
        return rc;
    }

    if (params.directed && params.legacy_pdu) {
        params.scannable = 0;
    }

    params.own_addr_type = parse_arg_kv_dflt("own_addr_type",
                                             cmd_own_addr_types,
                                             BLE_OWN_ADDR_PUBLIC, &rc);
    if (rc != 0) {
        iot_printf("err 'own_addr_type' parameter\n");
        return rc;
    }

    params.channel_map = parse_arg_uint8_dflt("channel_map", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'channel_map' parameter\n");
        return rc;
    }

    params.filter_policy = parse_arg_kv_dflt("filter", cmd_adv_filt_types,
                                             BLE_HCI_ADV_FILT_NONE, &rc);
    if (rc != 0) {
        iot_printf("err 'filter' parameter\n");
        return rc;
    }

    params.itvl_min = parse_arg_uint32_dflt("interval_min", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'interval_min' parameter\n");
        return rc;
    }

    params.itvl_max = parse_arg_uint32_dflt("interval_max", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'interval_max' parameter\n");
        return rc;
    }

    params.tx_power = parse_arg_long_bounds_dflt("tx_power",
                                             -127, 127, 127, &rc);
    if (rc != 0) {
        iot_printf("err 'tx_power' parameter\n");
        return rc;
    }

    params.primary_phy = parse_arg_kv_dflt("primary_phy", cmd_ext_adv_phy_opts,
                                           1, &rc);
    if (rc != 0) {
        iot_printf("err 'primary_phy' parameter\n");
        return rc;
    }

    params.secondary_phy = parse_arg_kv_dflt("secondary_phy",
                                         cmd_ext_adv_phy_opts,
                                         params.primary_phy, &rc);
    if (rc != 0) {
        iot_printf("err 'secondary_phy' parameter\n");
        return rc;
   }

    params.sid = parse_arg_uint8_dflt("sid", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'sid' parameter\n");
        return rc;
    }

	params.auxiliary_packet = parse_arg_uint8_dflt("auxiliary_packet", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'auxiliary_packet' parameter\n");
        return rc;
    }
    rc = btshell_ext_adv_configure(instance, &params, &selected_tx_power);
    if (rc) {
        iot_printf("failed to configure advertising instance rc=%d\n",rc);
        return rc;
    }

    iot_printf("Instance %u configured (selected tx power: %d)\n",
                   instance, selected_tx_power);

    adv_instances[instance] = true;

    return 0;
}

SRAM_CODE static int
cmd_advertise_set_addr(int argc, char **argv)
{
    ble_addr_t addr;
    uint8_t instance;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    instance = parse_arg_uint8_dflt("instance", 0, &rc);
    if (rc != 0 || instance >= BLE_ADV_INSTANCES) {
        iot_printf("err instance\n");
        return rc;
    }

    if (!adv_instances[instance]) {
        iot_printf("instance not configured\n");
        return rc;
    }

    rc = parse_arg_mac("addr", addr.val);
    if (rc != 0) {
        iot_printf("err 'addr' parameter\n");
                    return rc;
    }

    addr.type = BLE_ADDR_RANDOM;

    rc = ble_gap_ext_adv_set_addr(instance, &addr);
    if (rc) {
        iot_printf("failed to start advertising instance\n");
        return rc;
    }

    return 0;
}

SRAM_CODE static int
cmd_advertise_start(int argc, char **argv)
{
    int max_events;
    uint8_t instance;
    int duration;
    bool restart;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    instance = parse_arg_uint8_dflt("instance", 0, &rc);
    if (rc != 0 || instance >= BLE_ADV_INSTANCES) {
        iot_printf("err instance\n");
        return rc;
    }

    if (!adv_instances[instance]) {
        iot_printf("instance not configured\n");
        return rc;
    }

    duration = parse_arg_uint16_dflt("duration", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'duration' parameter\n");
        return rc;
    }

    max_events = parse_arg_uint8_dflt("max_events", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'max_events' parameter\n");
        return rc;
    }

    restart = parse_arg_bool_dflt("restart", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'restart' parameter\n");
        return rc;
    }

    rc = btshell_ext_adv_start(instance, duration, max_events, restart);
    if (rc) {
        iot_printf("failed to start advertising instance\n");
        return rc;
    }

    return 0;
}

SRAM_CODE static int
cmd_advertise_stop(int argc, char **argv)
{
    uint8_t instance;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    instance = parse_arg_uint8_dflt("instance", 0, &rc);
    if (rc != 0 || instance >= BLE_ADV_INSTANCES) {
        iot_printf("err instance\n");
        return rc;
    }

    if (!adv_instances[instance]) {
        iot_printf("instance not configured\n");
        return rc;
    }

    rc = btshell_ext_adv_stop(instance);
    if (rc) {
        iot_printf("failed to stop advertising instance\n");
        return rc;
    }

    return 0;
}

SRAM_CODE static int
cmd_advertise_remove(int argc, char **argv)
{
    uint8_t instance;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    instance = parse_arg_uint8_dflt("instance", 0, &rc);
    if (rc != 0 || instance >= BLE_ADV_INSTANCES) {
        iot_printf("err instance\n");
        return rc;
    }

    if (!adv_instances[instance]) {
        iot_printf("instance not configured\n");
        return rc;
    }

    rc = ble_gap_ext_adv_remove(instance);
    if (rc) {
        iot_printf("failed to remove advertising instance\n");
        return rc;
    }

    adv_instances[instance] = false;

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param advertise_configure_params[] = {
    {"instance", "default: 0"},
    {"auxiliary_packet", "auxiliary packet, usage: =[0-1], default: 0"},
    {"connectable", "connectable advertising, usage: =[0-1], default: 0"},
    {"scannable", "scannable advertising, usage: =[0-1], default: 0"},
    {"directed", "directed advertising, usage: =[0-1], default: 0"},
    {"peer_addr_type", "usage: =[public|random|public_id|random_id], default: public"},
    {"peer_addr", "usage: =[XX:XX:XX:XX:XX:XX]"},
    {"own_addr_type", "usage: =[public|random|rpa_pub|rpa_rnd], default: public"},
    {"channel_map", "usage: =[0x00-0xff], default: 0"},
    {"filter", "usage: =[none|scan|conn|both], default: none"},
    {"interval_min", "usage: =[0-UINT32_MAX], default: 0"},
    {"interval_max", "usage: =[0-UINT32_MAX], default: 0"},
    {"tx_power", "usage: =[-127-127], default: 127"},
    {"primary_phy", "usage: =[1M|coded], default: 1M"},
    {"secondary_phy", "usage: =[1M|2M|coded], default: primary_phy"},
    {"sid", "usage: =[0-UINT8_MAX], default: 0"},
    {"high_duty", "usage: =[0-1], default: 0"},
    {"anonymous", "enable anonymous advertising, usage: =[0-1], default: 0"},
    {"legacy", "use legacy PDUs, usage: =[0-1], default: 0"},
    {"include_tx_power", "include TX power in PDU, usage: =[0-1], default: 0"},
    {"scan_req_notif", "enable Scan Request notification usage: =[0-1], default: 0"},
    {NULL, NULL}
};

static const struct shell_cmd_help advertise_configure_help = {
    .summary = "configure new advertising instance",
    .usage = NULL,
    .params = advertise_configure_params,
};

static const struct shell_param advertise_set_addr_params[] = {
    {"instance", "default: 0"},
    {"addr", "usage: =[XX:XX:XX:XX:XX:XX]"},
    {NULL, NULL}
};

static const struct shell_cmd_help advertise_set_addr_help = {
    .summary = "set advertising instance random address",
    .usage = NULL,
    .params = advertise_set_addr_params,
};

static const struct shell_param advertise_start_params[] = {
    {"instance", "default: 0"},
    {"duration", "advertising duration in 10ms units, default: 0 (forever)"},
    {"max_events", "max number of advertising events, default: 0 (no limit)"},
    {"restart", "restart advertising after disconnect, usage: =[0-1], default: 0"},
    {NULL, NULL}
};

static const struct shell_cmd_help advertise_start_help = {
    .summary = "start advertising instance",
    .usage = NULL,
    .params = advertise_start_params,
};

static const struct shell_param advertise_stop_params[] = {
    {"instance", "default: 0"},
    {NULL, NULL}
};

static const struct shell_cmd_help advertise_stop_help = {
    .summary = "stop advertising instance",
    .usage = NULL,
    .params = advertise_stop_params,
};

static const struct shell_param advertise_remove_params[] = {
    {"instance", "default: 0"},
    {NULL, NULL}
};

static const struct shell_cmd_help advertise_remove_help = {
    .summary = "remove advertising instance",
    .usage = NULL,
    .params = advertise_remove_params,
};
#endif

#else
static const struct kv_pair cmd_adv_conn_modes[] = {
    { "non", BLE_GAP_CONN_MODE_NON },
    { "und", BLE_GAP_CONN_MODE_UND },
    { "dir", BLE_GAP_CONN_MODE_DIR },
    { NULL }
};

static const struct kv_pair cmd_adv_disc_modes[] = {
    { "non", BLE_GAP_DISC_MODE_NON },
    { "ltd", BLE_GAP_DISC_MODE_LTD },
    { "gen", BLE_GAP_DISC_MODE_GEN },
    { NULL }
};

SRAM_CODE static int
cmd_advertise(int argc, char **argv)
{
    struct ble_gap_adv_params params;
    int32_t duration_ms;
    ble_addr_t peer_addr;
    ble_addr_t *peer_addr_param = &peer_addr;
    uint8_t own_addr_type;
    bool restart;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "stop") == 0) {
        rc = btshell_adv_stop();
        if (rc != 0) {
            iot_printf("advertise stop fail: %d\n", rc);
            return rc;
        }

        return 0;
    }

    params.conn_mode = parse_arg_kv_dflt("conn", cmd_adv_conn_modes,
                                         BLE_GAP_CONN_MODE_UND, &rc);
    if (rc != 0) {
        iot_printf("err 'conn'\n");
        return rc;
    }

    params.disc_mode = parse_arg_kv_dflt("discov", cmd_adv_disc_modes,
                                         BLE_GAP_DISC_MODE_GEN, &rc);
    if (rc != 0) {
        iot_printf("err 'discov'\n");
        return rc;
    }

    peer_addr.type = parse_arg_kv_dflt("peer_addr_type", cmd_peer_addr_types,
                                       BLE_ADDR_PUBLIC, &rc);
    if (rc != 0) {
        iot_printf("err 'peer_addr_type'\n");
        return rc;
    }

    rc = parse_arg_mac("peer_addr", peer_addr.val);
    if (rc == ENOENT) {
        peer_addr_param = NULL;
    } else if (rc != 0) {
        iot_printf("err 'peer_addr'\n");
        return rc;
    }

    restart = parse_arg_bool_dflt("restart", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'restart'\n");
        return rc;
    }

    own_addr_type = parse_arg_kv_dflt("own_addr_type", cmd_own_addr_types,
                                      BLE_OWN_ADDR_PUBLIC, &rc);
    if (rc != 0) {
        iot_printf("err 'own_addr_type'\n");
        return rc;
    }

    params.channel_map = parse_arg_uint8_dflt("channel_map", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'chan_map'\n");
        return rc;
    }

    params.filter_policy = parse_arg_kv_dflt("filter", cmd_adv_filt_types,
                                             BLE_HCI_ADV_FILT_NONE, &rc);
    if (rc != 0) {
        iot_printf("err 'filter'\n");
        return rc;
    }

    params.itvl_min = parse_arg_uint16_dflt("interval_min", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'interval_min'\n");
        return rc;
    }

    params.itvl_max = parse_arg_uint16_dflt("interval_max", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'interval_max'\n");
        return rc;
    }

    params.high_duty_cycle = parse_arg_bool_dflt("high_duty", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'high_duty'\n");
        return rc;
    }

    duration_ms = parse_arg_long_bounds_dflt("duration", 1, INT32_MAX,
                                             BLE_HS_FOREVER, &rc);
    if (rc != 0) {
        iot_printf("err 'duration'\n");
        return rc;
    }

    rc = btshell_adv_start(own_addr_type, peer_addr_param, duration_ms,
                           &params, restart);
    if (rc != 0) {
        iot_printf("advertise fail: %d\n", rc);
        return rc;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param advertise_params[] = {
    {"stop", "stop advertising procedure"},
    {"conn", "connectable mode, usage: =[non|und|dir], default: und"},
    {"discov", "discoverable mode, usage: =[non|ltd|gen], default: gen"},
    {"peer_addr_type", "usage: =[public|random|public_id|random_id], default: public"},
    {"peer_addr", "usage: =[XX:XX:XX:XX:XX:XX]"},
    {"own_addr_type", "usage: =[public|random|rpa_pub|rpa_rnd], default: public"},
    {"channel_map", "usage: =[0x00-0xff], default: 0"},
    {"filter", "usage: =[none|scan|conn|both], default: none"},
    {"interval_min", "usage: =[0-UINT16_MAX], default: 0"},
    {"interval_max", "usage: =[0-UINT16_MAX], default: 0"},
    {"high_duty", "usage: =[0-1], default: 0"},
    {"duration", "usage: =[1-INT32_MAX], default: INT32_MAX"},
    {"restart", "restart advertising after disconnect, usage: =[0-1], default: 0"},
    {NULL, NULL}
};

static const struct shell_cmd_help advertise_help = {
    .summary = "start/stop advertising with specific parameters",
    .usage = NULL,
    .params = advertise_params,
};
#endif
#endif

/*****************************************************************************
 * $connect                                                                  *
 *****************************************************************************/

static struct kv_pair cmd_ext_conn_phy_opts[] = {
    { "none",        0x00 },
    { "1M",          0x01 },
    { "coded",       0x02 },
    { "both",        0x03 },
    { "all",         0x04 },
    { NULL }
};

SRAM_CODE static int
cmd_connect(int argc, char **argv)
{
    struct ble_gap_conn_params phy_1M_params = {0};
    struct ble_gap_conn_params phy_coded_params = {0};
    struct ble_gap_conn_params phy_2M_params = {0};
    uint8_t ext;
    int32_t duration_ms;
    ble_addr_t peer_addr;
    ble_addr_t *peer_addr_param = &peer_addr;
    int own_addr_type;
    int rc;
	char *str = NULL;
    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "cancel") == 0) {
        rc = btshell_conn_cancel();
        if (rc != 0) {
            iot_printf("connection cancel fail: %d\n", rc);
            return rc;
        }

        return 0;
    }
	str = "extended";
    ext = parse_arg_kv_dflt(str, cmd_ext_conn_phy_opts, 0, &rc);
    if (rc != 0) {
        goto __error;
    }
	str = "peer_addr_type";
    peer_addr.type = parse_arg_kv_dflt(str, cmd_peer_addr_types,
                                       BLE_ADDR_PUBLIC, &rc);
    if (rc != 0) {
        goto __error;
    }
	str = "peer_addr";

    rc = parse_arg_mac(str, peer_addr.val);
    if (rc == ENOENT) {
        /* Allow "addr" for backwards compatibility. */
        rc = parse_arg_mac("addr", peer_addr.val);
    }

    if (rc == ENOENT) {
        /* With no "peer_addr" specified we'll use white list */
        peer_addr_param = NULL;
    } else if (rc != 0) {
        goto __error;
    }
	str = "own_addr_type";
    own_addr_type = parse_arg_kv_dflt(str, cmd_own_addr_types,
                                      BLE_OWN_ADDR_PUBLIC, &rc);
    if (rc != 0) {
        goto __error;
        return rc;
    }
	str = "duration";

    duration_ms = parse_arg_long_bounds_dflt(str, 1, INT32_MAX, 0, &rc);
    if (rc != 0) {
        goto __error;
        return rc;
    }
	
	str ="scan_interval";
    phy_1M_params.scan_itvl = parse_arg_uint16_dflt(str, 0x0010, &rc);
    if (rc != 0) {
        goto __error;
    
    }
	
	str ="scan_window";
    phy_1M_params.scan_window = parse_arg_uint16_dflt(str, 0x0010, &rc);
    if (rc != 0) {
        goto __error;    
    }
	str ="interval_min";

    phy_1M_params.itvl_min = parse_arg_uint16_dflt(str,
                                                   BLE_GAP_INITIAL_CONN_ITVL_MIN,
                                                   &rc);
    if (rc != 0) {
        goto __error;
    }
	str ="interval_max";

    phy_1M_params.itvl_max = parse_arg_uint16_dflt(str,
                                                   BLE_GAP_INITIAL_CONN_ITVL_MAX,
                                                   &rc);
    if (rc != 0) {
        goto __error;
    }
	str ="latency";

    phy_1M_params.latency = parse_arg_uint16_dflt(str, 0, &rc);
    if (rc != 0) {
        goto __error;    
    }
	str ="timeout";

    phy_1M_params.supervision_timeout = parse_arg_uint16_dflt(str, 0x0100, &rc);
    if (rc != 0) {
        goto __error; 
    }
	str ="min_conn_event_len";

    phy_1M_params.min_ce_len = parse_arg_uint16_dflt(str,
                                                     0x0010, &rc);
    if (rc != 0) {
        goto __error; 
    }
	str ="max_conn_event_len";

    phy_1M_params.max_ce_len = parse_arg_uint16_dflt(str,
                                                     0x0300, &rc);
    if (rc != 0) {
        goto __error; 
    
    }

    if (ext == 0x00) {
        rc = btshell_conn_initiate(own_addr_type, peer_addr_param, duration_ms,
                                   &phy_1M_params);
        iot_printf("error connecting; rc=%d\n", rc);
        return rc;
    }

    if (ext == 0x01) {
        rc = btshell_ext_conn_initiate(own_addr_type, peer_addr_param,
                                       duration_ms, &phy_1M_params,
                                       NULL, NULL);
        iot_printf("error connecting; rc=%d\n", rc);
        return rc;
    }
	str ="coded_scan_interval";

    /* Get coded params */
    phy_coded_params.scan_itvl = parse_arg_uint16_dflt(str,
                                                       0x0010, &rc);
    if (rc != 0) {
        goto __error; 
    }
	
	str ="coded_scan_window";
    phy_coded_params.scan_window = parse_arg_uint16_dflt(str,
                                                         0x0010, &rc);
    if (rc != 0) {
        goto __error; 
    
    }
	str ="coded_interval_min";

    phy_coded_params.itvl_min = parse_arg_uint16_dflt(str,
                                                      BLE_GAP_INITIAL_CONN_ITVL_MIN,
                                                      &rc);
    if (rc != 0) {
        goto __error;
    }
	str ="coded_interval_max";

    phy_coded_params.itvl_max = parse_arg_uint16_dflt(str,
                                                      BLE_GAP_INITIAL_CONN_ITVL_MAX,
                                                      &rc);
    if (rc != 0) {
        goto __error;    
    }
	str ="coded_latency";

    phy_coded_params.latency = parse_arg_uint16_dflt("coded_latency", 0, &rc);
    if (rc != 0) {
        goto __error;
    }
	str ="coded_timeout";

    phy_coded_params.supervision_timeout =parse_arg_uint16_dflt(str, 0x0100, &rc);

    if (rc != 0) {
        goto __error;
    
    }
	str ="coded_min_conn_event";

    phy_coded_params.min_ce_len =
        parse_arg_uint16_dflt(str, 0x0010, &rc);
    if (rc != 0) {
        goto __error;
    
    }
	str ="coded_max_conn_event";

    phy_coded_params.max_ce_len = parse_arg_uint16_dflt("coded_max_conn_event",
                                                        0x0300, &rc);
    if (rc != 0) {
        goto __error;
    }
	str ="2M_interval_min";

    /* Get 2M params */
    phy_2M_params.itvl_min = parse_arg_uint16_dflt(str,
                                                   BLE_GAP_INITIAL_CONN_ITVL_MIN,
                                                   &rc);
    if (rc != 0) {
        goto __error;
    
    }
	str ="2M_interval_max";

    phy_2M_params.itvl_max = parse_arg_uint16_dflt(str,
                                                   BLE_GAP_INITIAL_CONN_ITVL_MAX, &rc);
    if (rc != 0) {
        goto __error;
    
    }
	str ="2M_latency";

    phy_2M_params.latency =
        parse_arg_uint16_dflt(str, 0, &rc);
    if (rc != 0) {
        goto __error;
    }
	str ="2M_timeout";

    phy_2M_params.supervision_timeout = parse_arg_uint16_dflt(str,
                                                              0x0100, &rc);

    if (rc != 0) {
        goto __error;
    }
	str ="2M_min_conn_event";

    phy_2M_params.min_ce_len = parse_arg_uint16_dflt(str, 0x0010,
                                                     &rc);
    if (rc != 0) {
        goto __error;
    
    }
	str ="2M_max_conn_event";

    phy_2M_params.max_ce_len = parse_arg_uint16_dflt(str,
                                                     0x0300, &rc);
    if (rc != 0) {
        goto __error;
    
    }

    if (ext == 0x02) {
        rc = btshell_ext_conn_initiate(own_addr_type, peer_addr_param,
                                       duration_ms, NULL, NULL, &phy_coded_params);
        return rc;
    }

    if (ext == 0x03) {
        rc = btshell_ext_conn_initiate(own_addr_type, peer_addr_param,
                                       duration_ms, &phy_1M_params, NULL,
                                       &phy_coded_params);
        return rc;
    }

    rc = btshell_ext_conn_initiate(own_addr_type, peer_addr_param,
                                   duration_ms, &phy_1M_params,
                                   &phy_2M_params,
                                   &phy_coded_params);
    return rc;
__error:
	
    iot_printf("err %s\n",str);
    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param connect_params[] = {
    {"cancel", "cancel connection procedure"},
    {"extended", "usage: =[none|1M|coded|both|all], default: none"},
    {"peer_addr_type", "usage: =[public|random|public_id|random_id], default: public"},
    {"peer_addr", "usage: =[XX:XX:XX:XX:XX:XX]"},
    {"own_addr_type", "usage: =[public|random|rpa_pub|rpa_rnd], default: public"},
    {"duration", "usage: =[1-INT32_MAX], default: 0"},
    {"scan_interval", "usage: =[0-UINT16_MAX], default: 0x0010"},
    {"scan_window", "usage: =[0-UINT16_MAX], default: 0x0010"},
    {"interval_min", "usage: =[0-UINT16_MAX], default: 30"},
    {"interval_max", "usage: =[0-UINT16_MAX], default: 50"},
    {"latency", "usage: =[UINT16], default: 0"},
    {"timeout", "usage: =[UINT16], default: 0x0100"},
    {"min_conn_event_len", "usage: =[UINT16], default: 0x0010"},
    {"max_conn_event_len", "usage: =[UINT16], default: 0x0300"},
    {"coded_scan_interval", "usage: =[0-UINT16_MAX], default: 0x0010"},
    {"coded_scan_window", "usage: =[0-UINT16_MAX], default: 0x0010"},
    {"coded_interval_min", "usage: =[0-UINT16_MAX], default: 30"},
    {"coded_interval_max", "usage: =[0-UINT16_MAX], default: 50"},
    {"coded_latency", "usage: =[UINT16], default: 0"},
    {"coded_timeout", "usage: =[UINT16], default: 0x0100"},
    {"coded_min_conn_event_len", "usage: =[UINT16], default: 0x0010"},
    {"coded_max_conn_event_len", "usage: =[UINT16], default: 0x0300"},
    {"2M_interval_min", "usage: =[0-UINT16_MAX], default: 30"},
    {"2M_interval_max", "usage: =[0-UINT16_MAX], default: 50"},
    {"2M_latency", "usage: =[UINT16], default: 0"},
    {"2M_timeout", "usage: =[UINT16], default: 0x0100"},
    {"2M_min_conn_event_len", "usage: =[UINT16], default: 0x0010"},
    {"2M_max_conn_event_len", "usage: =[UINT16], default: 0x0300"},
    {NULL, NULL}
};

static const struct shell_cmd_help connect_help = {
    .summary = "start/stop connection procedure with specific parameters",
    .usage = NULL,
    .params = connect_params,
};
#endif

/*****************************************************************************
 * $disconnect                                                               *
 *****************************************************************************/

SRAM_CODE static int
cmd_disconnect(int argc, char **argv)
{
    uint16_t conn_handle;
    uint8_t reason;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("err 'conn' \n");
        return rc;
    }

    reason = parse_arg_uint8_dflt("reason", BLE_ERR_REM_USER_CONN_TERM, &rc);
    if (rc != 0) {
        iot_printf("err 'reason' \n");
        return rc;
    }

    rc = btshell_term_conn(conn_handle, reason);
    if (rc != 0) {
        iot_printf("error terminating connection; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

SRAM_CODE static int
cmd_show_conn(int argc, char **argv)
{
    struct ble_gap_conn_desc conn_desc;
    struct btshell_conn *conn;
    int rc;
    int i;

    for (i = 0; i < btshell_num_conns; i++) {
        conn = btshell_conns + i;

        rc = ble_gap_conn_find(conn->handle, &conn_desc);
        if (rc == 0) {
            print_conn_desc(&conn_desc);
        }
    }

    return 0;
}

SRAM_CODE static int
cmd_show_addr(int argc, char **argv)
{
    uint8_t id_addr[6];
    int rc;

    iot_printf("public_id_addr=");
    rc = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, id_addr, NULL);
    if (rc == 0) {
        print_addr(id_addr);
    } else {
        iot_printf("none");
    }

    iot_printf(" random_id_addr=");
    rc = ble_hs_id_copy_addr(BLE_ADDR_RANDOM, id_addr, NULL);
    if (rc == 0) {
        print_addr(id_addr);
    } else {
        iot_printf("none");
    }
    iot_printf("\n");

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param disconnect_params[] = {
    {"conn", "connection handle parameter, usage: =<UINT16>"},
    {"reason", "disconnection reason, usage: =[UINT8], default: 19 (remote user terminated connection)"},
    {NULL, NULL}
};

static const struct shell_cmd_help disconnect_help = {
    .summary = "disconnect command",
    .usage = NULL,
    .params = disconnect_params,
};
#endif

/*****************************************************************************
 * $set-scan-opts                                                            *
 *****************************************************************************/

static struct btshell_scan_opts g_scan_opts = {
        .limit = UINT16_MAX,
        .ignore_legacy = 0,
};

SRAM_CODE static int
cmd_set_scan_opts(int argc, char **argv)
{
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    g_scan_opts.limit = parse_arg_uint16_dflt("decode_limit", UINT16_MAX, &rc);
    if (rc != 0) {
        iot_printf("err 'decode_limit' \n");
        return rc;
    }

    g_scan_opts.ignore_legacy = parse_arg_bool_dflt("ignore_legacy", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'ignore_legacy' \n");
        return rc;
    }

    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param set_scan_opts_params[] = {
    {"decode_limit", "usage: =[0-UINT16_MAX], default: UINT16_MAX"},
    {"ignore_legacy", "usage: =[0-1], default: 0"},
    {NULL, NULL}
};

static const struct shell_cmd_help set_scan_opts_help = {
    .summary = "set scan options",
    .usage = NULL,
    .params = set_scan_opts_params,
};
#endif

/*****************************************************************************
 * $scan                                                                     *
 *****************************************************************************/

static const struct kv_pair cmd_scan_filt_policies[] = {
    { "no_wl", BLE_HCI_SCAN_FILT_NO_WL },
    { "use_wl", BLE_HCI_SCAN_FILT_USE_WL },
    { "no_wl_inita", BLE_HCI_SCAN_FILT_NO_WL_INITA },
    { "use_wl_inita", BLE_HCI_SCAN_FILT_USE_WL_INITA },
    { NULL }
};

static struct kv_pair cmd_scan_ext_types[] = {
    { "none",       0x00 },
    { "1M",         0x01 },
    { "coded",      0x02 },
    { "both",       0x03 },
    { NULL }
};

static struct btshell_scan_opts g_scan_opts;

int scan_show_name_valid = 0;
u8 scan_show_name[128];
int g_scan_name_cnt=0;
uint8_t g_scan_name_table[64][64];
short g_scan_show = 0;
int g_scan_addr_cnt=0;
uint8_t g_scan_addr_table[64][6];
int g_show_addr=0;
SRAM_CODE static int
cmd_scan(int argc, char **argv)
{
    struct ble_gap_disc_params params = {0};
    struct ble_gap_ext_disc_params uncoded = {0};
    struct ble_gap_ext_disc_params coded = {0};
    uint8_t extended;
    int32_t duration_ms;
    uint8_t own_addr_type;
    uint16_t duration;
    uint16_t period;
	uint8_t *scanName;
	int scanNameLen;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }
    memset(g_scan_name_table, 0, sizeof(g_scan_name_table));
	scan_show_name_valid = 0;
    g_scan_name_cnt = 0;
    if (argc > 1 && strcmp(argv[1], "cancel") == 0) {
        rc = btshell_scan_cancel();
        if (rc != 0) {
            iot_printf("scan cancel fail: %d\n", rc);
            return rc;
        }
        return 0;
    }

    if(parse_arg_extract("show_addr"))
    {
	iot_printf("show_addr\n");
 	g_show_addr = 1;
        g_scan_addr_cnt=0;
	memset(g_scan_addr_table, 0, sizeof(g_scan_addr_table));
    }
    scanName = (uint8_t *)parse_arg_extract("show_name");
    if (scanName != NULL) {
        scanNameLen = strlen((char *)scanName);
		if(scanNameLen < 127){
			scan_show_name_valid = 1;
			memset(scan_show_name, 0, 128);
			memcpy(scan_show_name, scanName, scanNameLen);
		}
    }

    extended = parse_arg_kv_dflt("extended", cmd_scan_ext_types, 0, &rc);
    if (rc != 0) {
        iot_printf("err 'extended' \n");
        return rc;
    }

    duration_ms = parse_arg_long_bounds_dflt("duration", 1, INT32_MAX,
                                             BLE_HS_FOREVER, &rc);
    if (rc != 0) {
        iot_printf("err 'duration' \n");
        return rc;
    }

    params.limited = parse_arg_bool_dflt("limited", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'limited' \n");
        return rc;
    }

    params.passive = parse_arg_bool_dflt("passive", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'passive' \n");
        return rc;
    }

    params.itvl = parse_arg_uint16_dflt("interval", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'interval' \n");
        return rc;
    }

    params.window = parse_arg_uint16_dflt("window", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'window' \n");
        return rc;
    }

    params.filter_policy = parse_arg_kv_dflt("filter", cmd_scan_filt_policies,
                                             BLE_HCI_SCAN_FILT_NO_WL, &rc);
    if (rc != 0) {
        iot_printf("err 'filter' \n");
        return rc;
    }

    params.filter_duplicates = parse_arg_bool_dflt("nodups", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'nodups' \n");
        return rc;
    }

    own_addr_type = parse_arg_kv_dflt("own_addr_type", cmd_own_addr_types,
                                      BLE_OWN_ADDR_PUBLIC, &rc);
    if (rc != 0) {
        iot_printf("err 'own_addr_type' \n");
        return rc;
    }

    if (extended == 0) {
        rc = btshell_scan(own_addr_type, duration_ms, &params, &g_scan_opts);
        if (rc != 0) {
            iot_printf("error scanning; rc=%d\n", rc);
            return rc;
        }

        return 0;
    }

    /* Copy above parameters to uncoded params */
    uncoded.passive = params.passive;
    uncoded.itvl = params.itvl;
    uncoded.window = params.window;

    duration = parse_arg_uint16_dflt("extended_duration", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'extended_duration'\n");
        return rc;
    }

    period = parse_arg_uint16_dflt("extended_period", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'extended_period'\n");
        return rc;
    }

    coded.itvl = parse_arg_uint16_dflt("longrange_interval", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'longrange_interval' \n");
        return rc;
    }

    coded.window = parse_arg_uint16_dflt("longrange_window", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'longrange_window' \n");
        return rc;
    }

    coded.passive = parse_arg_uint16_dflt("longrange_passive", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'longrange_passive' \n");
        return rc;
    }

    switch (extended) {
    case 0x01:
        rc = btshell_ext_scan(own_addr_type, duration, period,
                              params.filter_duplicates, params.filter_policy,
                              params.limited, &uncoded, NULL,
                              &g_scan_opts);
        break;
    case 0x02:
        rc = btshell_ext_scan(own_addr_type, duration, period,
                              params.filter_duplicates, params.filter_policy,
                              params.limited, NULL, &coded,
                              &g_scan_opts);
        break;
    case 0x03:
        rc = btshell_ext_scan(own_addr_type, duration, period,
                              params.filter_duplicates, params.filter_policy,
                              params.limited, &uncoded, &coded,
                              &g_scan_opts);
        break;
    default:
        rc = -1;
        iot_printf("err 'extended' \n");
        break;
    }

    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param scan_params[] = {
    {"cancel", "cancel scan procedure"},
    {"extended", "usage: =[none|1M|coded|both], default: none"},
    {"duration", "usage: =[1-INT32_MAX], default: INT32_MAX"},
    {"limited", "usage: =[0-1], default: 0"},
    {"passive", "usage: =[0-1], default: 0"},
    {"interval", "usage: =[0-UINT16_MAX], default: 0"},
    {"window", "usage: =[0-UINT16_MAX], default: 0"},
    {"filter", "usage: =[no_wl|use_wl|no_wl_inita|use_wl_inita], default: no_wl"},
    {"nodups", "usage: =[0-1], default: 0"},
    {"own_addr_type", "usage: =[public|random|rpa_pub|rpa_rnd], default: public"},
    {"extended_duration", "usage: =[0-UINT16_MAX], default: 0"},
    {"extended_period", "usage: =[0-UINT16_MAX], default: 0"},
    {"longrange_interval", "usage: =[0-UINT16_MAX], default: 0"},
    {"longrange_window", "usage: =[0-UINT16_MAX], default: 0"},
    {"longrange_passive", "usage: =[0-1], default: 0"},
    {NULL, NULL}
};

static const struct shell_cmd_help scan_help = {
    .summary = "start/stop scan procedure with specific parameters",
    .usage = NULL,
    .params = scan_params,
};
#endif

/*****************************************************************************
 * $set                                                                      *
 *****************************************************************************/

static const struct kv_pair cmd_set_addr_types[] = {
    { "public",         BLE_ADDR_PUBLIC },
    { "random",         BLE_ADDR_RANDOM },
    { NULL }
};

SRAM_CODE static int
cmd_set_addr(void)
{
    uint8_t addr[6];
    int addr_type;
    int rc;

    addr_type = parse_arg_kv_dflt("addr_type", cmd_set_addr_types,
                                  BLE_ADDR_PUBLIC, &rc);
    if (rc != 0) {
        iot_printf("err 'addr_type'\n");
        return rc;
    }

    rc = parse_arg_mac("addr", addr);
    if (rc != 0) {
        iot_printf("err 'addr'\n");
        return rc;
    }

    switch (addr_type) {
    case BLE_ADDR_PUBLIC:
        rc = ble_hs_vs_set_pub_addr(addr);
        if (rc != 0) {
            return rc;
        }
        break;

    case BLE_ADDR_RANDOM:
        rc = ble_hs_id_set_rnd(addr);
        if (rc != 0) {
            return rc;
        }
        break;

    default:
        return BLE_HS_EUNKNOWN;
    }

    return 0;
}

SRAM_CODE static int
cmd_set(int argc, char **argv)
{
    uint16_t mtu;
    uint8_t irk[16];
    int good = 0;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rc = parse_arg_find_idx("addr");
    if (rc != -1) {
        rc = cmd_set_addr();
        if (rc != 0) {
            return rc;
        }
        good = 1;
    }

    mtu = parse_arg_uint16("mtu", &rc);
    if (rc == 0) {
        rc = ble_att_set_preferred_mtu(mtu);
        if (rc == 0) {
            good = 1;
        }
    } else if (rc != ENOENT) {
        iot_printf("err 'mtu'\n");
        return rc;
    }



    mtu = parse_arg_uint16("scan_show", &rc);
    if (rc == 0) {
        // rc = ble_att_set_preferred_mtu(mtu);
        g_scan_show = mtu;
        if (rc == 0) {
            good = 1;
        }
    }
    else if (rc != ENOENT) {
        iot_printf("err 'mtu'\n");
        return rc;
    }

    rc = parse_arg_byte_stream_exact_length("irk", irk, 16);
    if (rc == 0) {
        good = 1;
        ble_hs_pvcy_set_our_irk(irk);
    } else if (rc != ENOENT) {
        iot_printf("err 'irk'\n");
        return rc;
    }

    if (!good) {
        iot_printf("Error: no valid settings specified\n");
        return -1;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param set_params[] = {
    {"addr", "set device address, usage: =[XX:XX:XX:XX:XX:XX]"},
    {"addr_type", "set device address type, usage: =[public|random], default: public"},
    {"mtu", "Maximum Transimssion Unit, usage: =[0-UINT16_MAX]"},
    {"irk", "Identity Resolving Key, usage: =[XX:XX...], len=16 octets"},
    {NULL, NULL}
};

static const struct shell_cmd_help set_help = {
    .summary = "set device parameters",
    .usage = NULL,
    .params = set_params,
};
#endif

/*****************************************************************************
 * $set-adv-data                                                             *
 *****************************************************************************/

#define CMD_ADV_DATA_MAX_UUIDS16                8
#define CMD_ADV_DATA_MAX_UUIDS32                8
#define CMD_ADV_DATA_MAX_UUIDS128               2
#define CMD_ADV_DATA_MAX_PUBLIC_TGT_ADDRS       8
#define CMD_ADV_DATA_SVC_DATA_UUID16_MAX_LEN    BLE_HS_ADV_MAX_FIELD_SZ
#define CMD_ADV_DATA_SVC_DATA_UUID32_MAX_LEN    BLE_HS_ADV_MAX_FIELD_SZ
#define CMD_ADV_DATA_SVC_DATA_UUID128_MAX_LEN   BLE_HS_ADV_MAX_FIELD_SZ
#define CMD_ADV_DATA_URI_MAX_LEN                BLE_HS_ADV_MAX_FIELD_SZ
#define CMD_ADV_DATA_MFG_DATA_MAX_LEN           BLE_HS_ADV_MAX_FIELD_SZ

#if (MYNEWT_VAL_BLE_EXT_ADV)
static void
update_pattern(uint8_t *buf, int counter)
{
    int i;

    for (i = 0; i < 10; i += 2) {
        counter += 2;
        buf[i] = (counter / 1000) << 4 | (counter / 100 % 10);
        buf[i + 1] = (counter / 10 % 10) << 4 | (counter % 10);
    }
}
#endif

SRAM_CODE static int
cmd_set_adv_data_or_scan_rsp(int argc, char **argv, bool scan_rsp)
{
    static bssnz_t ble_uuid16_t uuids16[CMD_ADV_DATA_MAX_UUIDS16];
    static bssnz_t ble_uuid32_t uuids32[CMD_ADV_DATA_MAX_UUIDS32];
    static bssnz_t ble_uuid128_t uuids128[CMD_ADV_DATA_MAX_UUIDS128];
    static bssnz_t uint8_t
        public_tgt_addrs[CMD_ADV_DATA_MAX_PUBLIC_TGT_ADDRS]
                        [BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN];
    static bssnz_t uint8_t slave_itvl_range[BLE_HS_ADV_SLAVE_ITVL_RANGE_LEN];
    static bssnz_t uint8_t
        svc_data_uuid16[CMD_ADV_DATA_SVC_DATA_UUID16_MAX_LEN];
    static bssnz_t uint8_t
        svc_data_uuid32[CMD_ADV_DATA_SVC_DATA_UUID32_MAX_LEN];
    static bssnz_t uint8_t
        svc_data_uuid128[CMD_ADV_DATA_SVC_DATA_UUID128_MAX_LEN];
    static bssnz_t uint8_t uri[CMD_ADV_DATA_URI_MAX_LEN];
    static bssnz_t uint8_t mfg_data[CMD_ADV_DATA_MFG_DATA_MAX_LEN];
    struct ble_hs_adv_fields adv_fields;
    uint32_t uuid32;
    uint16_t uuid16;
    uint8_t uuid128[16];
    uint8_t public_tgt_addr[BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN];
    uint8_t eddystone_url_body_len;
    uint8_t eddystone_url_suffix;
    uint8_t eddystone_url_scheme;
    int8_t eddystone_measured_power = 0;
    char eddystone_url_body[BLE_EDDYSTONE_URL_MAX_LEN];
    char *eddystone_url_full;
    int svc_data_uuid16_len;
    int svc_data_uuid32_len;
    int svc_data_uuid128_len;
    int uri_len;
    int mfg_data_len;
    int tmp;
    int rc;
#if (MYNEWT_VAL_BLE_EXT_ADV)
    uint8_t instance;
    uint8_t extra_data[10];
    uint16_t counter;
    uint16_t extra_data_len;
    struct os_mbuf *adv_data;
#endif

    memset(&adv_fields, 0, sizeof adv_fields);

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

#if (MYNEWT_VAL_BLE_EXT_ADV)
    instance = parse_arg_uint8_dflt("instance", 0, &rc);
    if (rc != 0 || instance >= BLE_ADV_INSTANCES) {
        iot_printf("err instance\n");
        return rc;
    }

    if (!adv_instances[instance]) {
        iot_printf("instance not configured\n");
        return rc;
    }
#endif

    tmp = parse_arg_uint8("flags", &rc);
    if (rc == 0) {
        adv_fields.flags = tmp;
    } else if (rc != ENOENT) {
        iot_printf("err 'flags'\n");
        return rc;
    }

    while (1) {
        uuid16 = parse_arg_uint16("uuid16", &rc);
        if (rc == 0) {
            if (adv_fields.num_uuids16 >= CMD_ADV_DATA_MAX_UUIDS16) {
                iot_printf("err 'uuid16'\n");
                return EINVAL;
            }
            uuids16[adv_fields.num_uuids16] = (ble_uuid16_t) BLE_UUID16_INIT(uuid16);
            adv_fields.num_uuids16++;
        } else if (rc == ENOENT) {
            break;
        } else {
            iot_printf("err 'uuid16'\n");
            return rc;
        }
    }
    if (adv_fields.num_uuids16 > 0) {
        adv_fields.uuids16 = uuids16;
    }

    tmp = parse_arg_bool_dflt("uuids16_is_complete", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'uuids16_is_complete'\n");
        return rc;
    }

    while (1) {
        uuid32 = parse_arg_uint32("uuid32", &rc);
        if (rc == 0) {
            if (adv_fields.num_uuids32 >= CMD_ADV_DATA_MAX_UUIDS32) {
                iot_printf("err 'uuid32'\n");
                return EINVAL;
            }
            uuids32[adv_fields.num_uuids32] = (ble_uuid32_t) BLE_UUID32_INIT(uuid32);
            adv_fields.num_uuids32++;
        } else if (rc == ENOENT) {
            break;
        } else {
            iot_printf("err 'uuid32'\n");
            return rc;
        }
    }
    if (adv_fields.num_uuids32 > 0) {
        adv_fields.uuids32 = uuids32;
    }

    tmp = parse_arg_bool_dflt("uuids32_is_complete", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'uuids32_is_complete'\n");
        return rc;
    }

    while (1) {
        rc = parse_arg_byte_stream_exact_length("uuid128", uuid128, 16);
        if (rc == 0) {
            if (adv_fields.num_uuids128 >= CMD_ADV_DATA_MAX_UUIDS128) {
                iot_printf("err 'uuid128'\n");
                return EINVAL;
            }
            ble_uuid_init_from_buf((ble_uuid_any_t *) &uuids128[adv_fields.num_uuids128],
                                   uuid128, 16);
            adv_fields.num_uuids128++;
        } else if (rc == ENOENT) {
            break;
        } else {
            iot_printf("err 'uuid128'\n");
            return rc;
        }
    }
    if (adv_fields.num_uuids128 > 0) {
        adv_fields.uuids128 = uuids128;
    }

    tmp = parse_arg_bool_dflt("uuids128_is_complete", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'uuids128_is_complete'\n");
        return rc;
    }

    adv_fields.name = (uint8_t *)parse_arg_extract("name");
    if (adv_fields.name != NULL) {
        adv_fields.name_len = strlen((char *)adv_fields.name);
    }

    tmp = parse_arg_long_bounds("tx_power_level", INT8_MIN, INT8_MAX, &rc);
    if (rc == 0) {
        adv_fields.tx_pwr_lvl = tmp;
        adv_fields.tx_pwr_lvl_is_present = 1;
    } else if (rc != ENOENT) {
        iot_printf("err 'tx_power_level'\n");
        return rc;
    }

    rc = parse_arg_byte_stream_exact_length("slave_interval_range",
                                            slave_itvl_range,
                                            BLE_HS_ADV_SLAVE_ITVL_RANGE_LEN);
    if (rc == 0) {
        adv_fields.slave_itvl_range = slave_itvl_range;
    } else if (rc != ENOENT) {
        iot_printf("err 'slave_interval_range'\n");
        return rc;
    }

    rc = parse_arg_byte_stream("service_data_uuid16",
                               CMD_ADV_DATA_SVC_DATA_UUID16_MAX_LEN,
                               svc_data_uuid16, &svc_data_uuid16_len);
    if (rc == 0) {
        adv_fields.svc_data_uuid16 = svc_data_uuid16;
        adv_fields.svc_data_uuid16_len = svc_data_uuid16_len;
    } else if (rc != ENOENT) {
        iot_printf("err 'service_data_uuid16'\n");
        return rc;
    }

    while (1) {
        rc = parse_arg_byte_stream_exact_length(
            "public_target_address", public_tgt_addr,
            BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN);
        if (rc == 0) {
            if (adv_fields.num_public_tgt_addrs >=
                CMD_ADV_DATA_MAX_PUBLIC_TGT_ADDRS) {

                iot_printf("err 'public_target_address' parameter\n");
                return EINVAL;
            }
            memcpy(public_tgt_addrs[adv_fields.num_public_tgt_addrs],
                   public_tgt_addr, BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN);
            adv_fields.num_public_tgt_addrs++;
        } else if (rc == ENOENT) {
            break;
        } else {
            iot_printf("err 'public_target_address' parameter\n");
            return rc;
        }
    }
    if (adv_fields.num_public_tgt_addrs > 0) {
        adv_fields.public_tgt_addr = (void *)public_tgt_addrs;
    }

    adv_fields.appearance = parse_arg_uint16("appearance", &rc);
    if (rc == 0) {
        adv_fields.appearance_is_present = 1;
    } else if (rc != ENOENT) {
        iot_printf("err 'appearance'\n");
        return rc;
    }

    adv_fields.adv_itvl = parse_arg_uint16("advertising_interval", &rc);
    if (rc == 0) {
        adv_fields.adv_itvl_is_present = 1;
    } else if (rc != ENOENT) {
        iot_printf("err 'advertising_interval'\n");
        return rc;
    }

    rc = parse_arg_byte_stream("service_data_uuid32",
                               CMD_ADV_DATA_SVC_DATA_UUID32_MAX_LEN,
                               svc_data_uuid32, &svc_data_uuid32_len);
    if (rc == 0) {
        adv_fields.svc_data_uuid32 = svc_data_uuid32;
        adv_fields.svc_data_uuid32_len = svc_data_uuid32_len;
    } else if (rc != ENOENT) {
        iot_printf("err 'service_data_uuid32'\n");
        return rc;
    }

    rc = parse_arg_byte_stream("service_data_uuid128",
                               CMD_ADV_DATA_SVC_DATA_UUID128_MAX_LEN,
                               svc_data_uuid128, &svc_data_uuid128_len);
    if (rc == 0) {
        adv_fields.svc_data_uuid128 = svc_data_uuid128;
        adv_fields.svc_data_uuid128_len = svc_data_uuid128_len;
    } else if (rc != ENOENT) {
        iot_printf("err 'service_data_uuid128' parameter\n");
        return rc;
    }

    rc = parse_arg_byte_stream("uri", CMD_ADV_DATA_URI_MAX_LEN, uri, &uri_len);
    if (rc == 0) {
        adv_fields.uri = uri;
        adv_fields.uri_len = uri_len;
    } else if (rc != ENOENT) {
        iot_printf("err 'uri'\n");
        return rc;
    }
#if (MYNEWT_VAL_BLE_EXT_ADV)

    rc = parse_arg_byte_stream("mfg_data", BLE_HCI_MAX_EXT_ADV_DATA_LEN,
                               mfg_data, &mfg_data_len);
#else
    rc = parse_arg_byte_stream("mfg_data", CMD_ADV_DATA_MFG_DATA_MAX_LEN,
                               mfg_data, &mfg_data_len);
#endif
    if (rc == 0) {
        adv_fields.mfg_data = mfg_data;
        adv_fields.mfg_data_len = mfg_data_len;
    } else if (rc != ENOENT) {
        iot_printf("err 'mfg_data'\n");
        return rc;
    }

    tmp = parse_arg_long_bounds("eddystone_measured_power", -100, 20, &rc);
    if (rc == 0) {
        eddystone_measured_power = tmp;
    } else if (rc != ENOENT) {
        iot_printf("err 'eddystone_measured_power' parameter\n");
        return rc;
    }

    eddystone_url_full = parse_arg_extract("eddystone_url");
    if (eddystone_url_full != NULL) {
        rc = parse_eddystone_url(eddystone_url_full, &eddystone_url_scheme,
                                 eddystone_url_body,
                                 &eddystone_url_body_len,
                                 &eddystone_url_suffix);
        if (rc != 0) {
            goto done;
        }

        rc = ble_eddystone_set_adv_data_url(&adv_fields, eddystone_url_scheme,
                                            eddystone_url_body,
                                            eddystone_url_body_len,
                                            eddystone_url_suffix,
                                            eddystone_measured_power);
    } else {
#if (MYNEWT_VAL_BLE_EXT_ADV)
        /* Default to legacy PDUs size, mbuf chain will be increased if needed
         */
        adv_data = os_msys_get_pkthdr(BLE_HCI_MAX_ADV_DATA_LEN, 0);
        if (!adv_data) {
            rc = ENOMEM;
            goto done;
        }

        rc = ble_hs_adv_set_fields_mbuf(&adv_fields, adv_data);
        if (rc) {
            goto done;
        }

        /* Append some extra data, if requested */
        extra_data_len = parse_arg_uint16("extra_data_len", &rc);
        if (rc == 0) {
            counter = 0;
            extra_data_len = min(extra_data_len, 1650);
            while (counter < extra_data_len) {
                update_pattern(extra_data, counter);

                os_mbuf_append(adv_data, extra_data,
                               min(extra_data_len - counter, 10));

                counter += 10;
            }
        }

        if (scan_rsp) {
            rc = ble_gap_ext_adv_rsp_set_data(instance, adv_data);
        } else {
            rc = ble_gap_ext_adv_set_data(instance, adv_data);
        }
#else
        if (scan_rsp) {
            rc = ble_gap_adv_rsp_set_fields(&adv_fields);
        } else {
            rc = ble_gap_adv_set_fields(&adv_fields);
        }
#endif
    }
done:
    if (rc != 0) {
        iot_printf("error setting advertisement data; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

SRAM_CODE static int
cmd_set_adv_data(int argc, char **argv)
{
    return cmd_set_adv_data_or_scan_rsp(argc, argv, false);
}
pAtbm_thread_t ble_test_thread = NULL;
static const uint8_t test_f[250]={
0x00, 0x36, 0x35, 0x34, 0x78, 0x58, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x01, 0x00, 0x00, 0x30, 0x02, 0x09, 0x00, 0x00,
0x78, 0x36, 0x35, 0x34, 0x78, 0x58, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x01, 0x00, 0x00, 0x30, 0x02, 0x09, 0x00, 0x00,
0x78, 0x36, 0x35, 0x34, 0x78, 0x58, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x01, 0x00, 0x00, 0x30, 0x02, 0x09, 0x00, 0x00,
0x78, 0x36, 0x35, 0x34, 0x78, 0x58, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x01, 0x00, 0x00, 0x30, 0x02, 0x09, 0x00, 0x00,
0x78, 0x36, 0x35, 0x34, 0x78, 0x58, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x01, 0x00, 0x00, 0x30, 0x02, 0x09, 0x00, 0x00,//10
0x78, 0x36, 0x35, 0x34, 0x78, 0x58, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x01, 0x00, 0x00, 0x30, 0x02, 0x09, 0x00, 0x00,
0x78, 0x36, 0x35, 0x34, 0x78, 0x58, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x01, 0x00, 0x00, 0x30, 0x02, 0x09, 0x00, 0x00,
0x78, 0x36, 0x35, 0x34, 0x78, 0x58, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x01, 0x00, 0x00, 0x30, 0x02, 0x09, 0x00, 0x00,
0x78, 0x36, 0x35, 0x34, 0x78, 0x58, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x01, 0x00, 0x00, 0x30, 0x02, 0x09, 0x00, 0x00,
0x78, 0x36, 0x35, 0x34, 0x78, 0x58, 0x00, 0x00, 0x00, 0x00, 
0x00, 0x00, 0x01, 0x00, 0x00, 0x30, 0x02, 0x09, 0x00, 0x00,//20
};
struct ble_tset_notify{
	int cnt;
	uint8_t len;
	uint8_t timer;
};
struct ble_tset_notify ble_tset_notify_var;
extern uint16_t ble_conn_handle;
extern uint16_t blesmt_notify_handle;
extern uint8_t atbm_ble_status;
extern int gatt_svr_chr_notify(uint16_t conn_handle, uint16_t chr_val_handle, const char *data, int len);
SRAM_CODE static int send_notify_task(void* argc)
{
	struct ble_tset_notify *value;
	value=argc;
	 int i;
	int rc,cnt;
	iot_printf("timer %dms  cnt %d len %d\n", value->timer,value->cnt,value->len);
	for(i=0;i<value->cnt;i++)
	{
		usleep(value->timer*1000);
		//test_f[0]++;
		iot_printf(" send len %d\n",value->len);
		rc=gatt_svr_chr_notify(ble_conn_handle, blesmt_notify_handle, test_f, value->len);
		
		if(atbm_ble_status != 2){
			break;
		}
		while(rc && (cnt<250)&&(atbm_ble_status==2))
		{
			cnt++;
			iot_printf(" wait rc=%d\n",rc);
			usleep(100*1000);
			rc=gatt_svr_chr_notify(ble_conn_handle, blesmt_notify_handle, test_f, value->len);
			if(rc)
			{
				iot_printf(" error %d \n",value->cnt);
			}
		}
	}
	ble_test_thread = NULL;
}

SRAM_CODE static int ble_sent_test(int argc, char **argv)
{
	int rc,c;
	rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }
	ble_tset_notify_var.timer = parse_arg_uint16("interval",&rc);

	ble_tset_notify_var.cnt = parse_arg_uint32("number",&rc);
                                            
	ble_tset_notify_var.len= parse_arg_uint8("len",&rc);
	iot_printf("timer %dms  cnt %d len %d\n", ble_tset_notify_var.timer,ble_tset_notify_var.cnt,ble_tset_notify_var.len);
	
	if(ble_test_thread)
		atbm_stopThread(ble_test_thread);
	ble_test_thread = atbm_createThread(send_notify_task, &ble_tset_notify_var, BLE_APP_PRIO);
	return 0;
}
static const struct shell_param ble_send_notify_params[] = {
    {"len", "notify send len, usage: =<UINT16>"},
    {"timer", "notify Sending interval usage: =<UINT16>"},
    {"cnt", "notify send number, usage: =<UINT16>"},
    {NULL, NULL}
};
static const struct shell_cmd_help send_notify_help = {
    .summary = "send notify",
    .usage = "Timed transmission",
    .params = ble_send_notify_params,
};
SRAM_CODE static int
cmd_set_scan_rsp(int argc, char **argv)
{
    return cmd_set_adv_data_or_scan_rsp(argc, argv, true);
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param set_adv_data_params[] = {
    {"instance", "default: 0"},
    {"flags", "usage: =[0-UINT8_MAX]"},
    {"uuid16", "usage: =[UINT16]"},
    {"uuid16_is_complete", "usage: =[0-1], default=0"},
    {"uuid32", "usage: =[UINT32]"},
    {"uuid32_is_complete", "usage: =[0-1], default=0"},
    {"uuid128", "usage: =[XX:XX...], len=16 octets"},
    {"uuid128_is_complete", "usage: =[0-1], default=0"},
    {"tx_power_level", "usage: =[INT8_MIN-INT8_MAX]"},
    {"slave_interval_range", "usage: =[XX:XX:XX:XX]"},
    {"public_target_address", "usage: =[XX:XX:XX:XX:XX:XX]"},
    {"appearance", "usage: =[UINT16]"},
    {"name", "usage: =[string]"},
    {"advertising_interval", "usage: =[UINT16]"},
    {"service_data_uuid16", "usage: =[XX:XX...]"},
    {"service_data_uuid32", "usage: =[XX:XX...]"},
    {"service_data_uuid128", "usage: =[XX:XX...]"},
    {"uri", "usage: =[XX:XX...]"},
    {"mfg_data", "usage: =[XX:XX...]"},
    {"measured_power", "usage: =[-100-20]"},
    {"eddystone_url", "usage: =[string]"},
#if (MYNEWT_VAL_BLE_EXT_ADV)
    {"extra_data_len", "usage: =[UINT16]"},
#endif
    {NULL, NULL}
};

static const struct shell_cmd_help set_adv_data_help = {
    .summary = "set advertising data",
    .usage = NULL,
    .params = set_adv_data_params,
};

static const struct shell_cmd_help set_scan_rsp_help = {
    .summary = "set scan response",
    .usage = NULL,
    .params = set_adv_data_params,
};
#endif

/*****************************************************************************
 * $set-priv-mode                                                            *
 *****************************************************************************/

SRAM_CODE static int
cmd_set_priv_mode(int argc, char **argv)
{
    ble_addr_t addr;
    uint8_t priv_mode;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    addr.type = parse_arg_kv_dflt("addr_type", cmd_set_addr_types,
                                  BLE_ADDR_PUBLIC, &rc);
    if (rc != 0) {
        iot_printf("err 'addr_type'\n");
        return rc;
    }

    rc = parse_arg_mac("addr", addr.val);
    if (rc != 0) {
        iot_printf("err 'addr'\n");
        return rc;
    }

    priv_mode = parse_arg_uint8("mode", &rc);
    if (rc != 0) {
        iot_printf("missing mode\n");
        return rc;
    }

    return ble_gap_set_priv_mode(&addr, priv_mode);
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param set_priv_mode_params[] = {
    {"addr", "set priv mode for device address, usage: =[XX:XX:XX:XX:XX:XX]"},
    {"addr_type", "set priv mode for device address type, usage: =[public|random], default: public"},
    {"mode", "set priv mode, usage: =[0-UINT8_MAX]"},
    {NULL, NULL}
};

static const struct shell_cmd_help set_priv_mode_help = {
    .summary = "set priv mode",
    .usage = NULL,
    .params = set_priv_mode_params,
};
#endif

/*****************************************************************************
 * $white-list                                                               *
 *****************************************************************************/

#define CMD_WL_MAX_SZ   8

SRAM_CODE static int
cmd_white_list(int argc, char **argv)
{
    static ble_addr_t addrs[CMD_WL_MAX_SZ];
    int addrs_cnt;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    addrs_cnt = 0;
    while (1) {
        if (addrs_cnt >= CMD_WL_MAX_SZ) {
            return EINVAL;
        }

        rc = parse_arg_mac("addr", addrs[addrs_cnt].val);
        if (rc == ENOENT) {
            break;
        } else if (rc != 0) {
            iot_printf("err 'addr'\n");
            return rc;
        }
		
        addrs[addrs_cnt].type = parse_arg_kv("addr_type", cmd_addr_type, &rc);
        if (rc != 0) {
            iot_printf("err 'addr'\n");
            return rc;
        }
		iot_printf("type:%x,addr:%02X:%02X:%02X:%02X:%02X:%02X\n", addrs[addrs_cnt].type,
			addrs[addrs_cnt].val[0],addrs[addrs_cnt].val[1],addrs[addrs_cnt].val[2],
			addrs[addrs_cnt].val[3],addrs[addrs_cnt].val[4],addrs[addrs_cnt].val[5]);

        addrs_cnt++;
    }

    if (addrs_cnt == 0) {
        return EINVAL;
    }

    btshell_wl_set(addrs, addrs_cnt);

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param white_list_params[] = {
    {"addr", "white-list device addresses, usage: =[XX:XX:XX:XX:XX:XX]"},
    {"addr_type", "white-list address types, usage: =[public|random]"},
    {NULL, NULL}
};
	


static const struct shell_cmd_help white_list_help = {
    .summary = "set white-list addresses",
    .usage = NULL,
    .params = white_list_params,
};
#endif

SRAM_CODE static int
cmd_white_list_add(int argc, char **argv)
{
	int rc;
	ble_addr_t addr;
	
    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }	
	
	rc = parse_arg_mac("addr", addr.val);
	if (rc != 0) {
        iot_printf("err 'addr'\n");
        return rc;
    }
	
    addr.type = parse_arg_kv("addr_type", cmd_addr_type, &rc);
    if (rc != 0) {
        iot_printf("err 'addr_type'\n");
        return rc;
    }

	if(ble_gap_wl_busy()){
		iot_printf("err 'ble_gap_wl_busy'\n");
		return -1;
	}
	
	rc = ble_gap_wl_tx_add(&addr);
    if (rc != 0) {
        iot_printf("ble_gap_wl_tx_add err:%d\n", rc);
        return rc;
    }
	
	return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param wl_add_params[] = {
    {"addr", "white-list device addresses, usage: =[XX:XX:XX:XX:XX:XX]"},
    {"addr_type", "white-list address types, usage: =[public|random]"},
    {NULL, NULL}
};

static const struct shell_cmd_help wl_add_help = {
    .summary = "add white-list addr",
    .usage = NULL,
    .params = wl_add_params,
};
#endif

SRAM_CODE static int
cmd_white_list_clr(int argc, char **argv)
{
	int rc;
	
	if(ble_gap_wl_busy()){
		iot_printf("err 'ble_gap_wl_busy'\n");
		return -1;
	}

	rc = ble_gap_wl_tx_clear();
    if (rc != 0) {
        iot_printf("ble_gap_wl_tx_clear err:%d\n", rc);
        return rc;
    }
	
	return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param wl_clr_params[] = {
    {NULL, NULL}
};

static const struct shell_cmd_help wl_clr_help = {
    .summary = "add white-list addr",
    .usage = NULL,
    .params = wl_clr_params,
};
#endif


SRAM_CODE static int
cmd_white_list_add_test(int argc, char **argv)
{
	uint8_t cnt;
	int i, rc;
	ble_addr_t addr;
	
	if (argc < 2) {
		iot_printf("argc err\n");
		return -1;
	}
	
	cnt = strtoul(argv[1], NULL, 0);
	
	if(ble_gap_wl_busy()){
		iot_printf("err 'ble_gap_wl_busy'\n");
		return -1;
	}
	
	rc = ble_gap_wl_tx_clear();
	if (rc != 0) {
		iot_printf("white list clr err\n");
		return rc;
	}
	
	addr.val[0] = 0x00;
	addr.val[1] = 0x11;
	addr.val[2] = 0x11;
	addr.val[3] = 0x11;
	addr.val[4] = 0x11;
	addr.val[5] = 0x11;
	addr.type = 0;

	for(i=0; i<cnt; i++){
		addr.val[0] = i;
		rc = ble_gap_wl_tx_add(&addr);
	    if (rc != 0) {
	        iot_printf("ble_gap_wl_tx_add err:%d\n", rc);
	        return rc;
	    }		
	}
	
	return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param wl_add_test_params[] = {
    {NULL, NULL}
};

static const struct shell_cmd_help wl_add_test_help = {
    .summary = "add white-list addr test cnt",
    .usage = NULL,
    .params = wl_add_test_params,
};
#endif


/*****************************************************************************
 * $pvcy-add                                                             *
 *****************************************************************************/
SRAM_CODE static int
cmd_pvcy_add(int argc, char **argv)
{
	int rc;
	int i;
	ble_addr_t addr;
	uint8_t irk[16];

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }	
	
	rc = parse_arg_mac("addr", addr.val);
	if (rc != 0) {
        iot_printf("err 'addr'\n");
        return rc;
    }
	
    addr.type = parse_arg_kv("addr_type", cmd_addr_type, &rc);
    if (rc != 0) {
        iot_printf("err 'addr_type'\n");
        return rc;
    }	
	
	rc = parse_arg_byte_stream_exact_length("irk", irk, 16);
    if (rc != 0) {
        iot_printf("err 'irk'\n");
        return rc;
    }
	
	iot_printf("type:%x,addr:%02X:%02X:%02X:%02X:%02X:%02X\n", addr.type,
		addr.val[0],addr.val[1],addr.val[2],addr.val[3],addr.val[4],addr.val[5]);
	iot_printf("irk");
	for(i=0;i<16;i++){
		iot_printf(":%02X", irk[i]);
	}
	iot_printf("\n");

	rc = btshell_pvcy_add(&addr, irk);
    if (rc != 0) {
        iot_printf("btshell_pvcy_add err:%d\n", rc);
        return rc;
    }		
	
	return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param pvcy_add_params[] = {
    {"addr", "pvcy list device addresses, usage: =[XX:XX:XX:XX:XX:XX]"},
    {"addr_type", "pvcy list address types, usage: =[public|random]"},
    {"irk", "Identity Resolving Key, usage: =[XX:XX...], len=16 octets"},
    {NULL, NULL}
};

static const struct shell_cmd_help pvcy_add_help = {
    .summary = "add privcy list addr and irk",
    .usage = NULL,
    .params = pvcy_add_params,
};
#endif

SRAM_CODE static int
cmd_pvcy_rmv(int argc, char **argv)
{
	int rc;
	ble_addr_t addr;
	
    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }	
	
	rc = parse_arg_mac("addr", addr.val);
    if (rc != 0) {
        iot_printf("err 'addr'\n");
        return rc;
    }
	
    addr.type = parse_arg_kv("addr_type", cmd_addr_type, &rc);
    if (rc != 0) {
        iot_printf("err 'addr_type'\n");
        return rc;
    }	

	rc = btshell_pvcy_rmv(&addr);
    if (rc != 0) {
        iot_printf("btshell_pvcy_rmv err:%d\n", rc);
        return rc;
    }	

	return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param pvcy_rmv_params[] = {
    {"addr", "white-list device addresses, usage: =[XX:XX:XX:XX:XX:XX]"},
    {"addr_type", "white-list address types, usage: =[public|random]"},
    {NULL, NULL}
};

static const struct shell_cmd_help pvcy_rmv_help = {
    .summary = "remove privcy addr and irk",
    .usage = NULL,
    .params = pvcy_rmv_params,
};
#endif


SRAM_CODE static int
cmd_pvcy_add_test(int argc, char **argv)
{
	uint8_t cnt;
	int i, rc;
	ble_addr_t addr;
	uint8_t irk[16];
	
	if (argc < 2) {
		iot_printf("argc err\n");
		return -1;
	}
	
	cnt = strtoul(argv[1], NULL, 0);
	
	addr.val[0] = 0x00;
	addr.val[1] = 0x11;
	addr.val[2] = 0x11;
	addr.val[3] = 0x11;
	addr.val[4] = 0x11;
	addr.val[5] = 0x11;
	addr.type = 0;
	memset(irk, 0x33, 16);
	
	rc = ble_hs_pvcy_clear_entries();
    if (rc != 0) {
        iot_printf("ble_hs_pvcy_clear_entries err:%d\n", rc);
        return rc;
    }

	for(i=0; i<cnt; i++){
		addr.val[0] = i;
		irk[0] = i;
		rc = btshell_pvcy_add(&addr, irk);
	    if (rc != 0) {
	        iot_printf("btshell_pvcy_add err:%d\n", rc);
	        return rc;
	    }			
	}
	
	return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param pvcy_test_params[] = {
    {NULL, NULL}
};

static const struct shell_cmd_help pvcy_test_help = {
    .summary = "add privcy addr and irk test cnt",
    .usage = NULL,
    .params = pvcy_test_params,
};
#endif


/*****************************************************************************
 * $conn-rssi                                                                *
 *****************************************************************************/

SRAM_CODE static int
cmd_conn_rssi(int argc, char **argv)
{
    uint16_t conn_handle;
    int8_t rssi;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("err 'conn' parameter\n");
        return rc;
    }

    rc = btshell_rssi(conn_handle, &rssi);
    if (rc != 0) {
        iot_printf("error reading rssi; rc=%d\n", rc);
        return rc;
    }

    iot_printf("conn=%d rssi=%d\n", conn_handle, rssi);

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param conn_rssi_params[] = {
    {"conn", "connection handle parameter, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help conn_rssi_help = {
    .summary = "check connection rssi",
    .usage = NULL,
    .params = conn_rssi_params,
};
#endif

/*****************************************************************************
 * $conn-update-params                                                       *
 *****************************************************************************/

SRAM_CODE static int
cmd_conn_update_params(int argc, char **argv)
{
    struct ble_gap_upd_params params;
    uint16_t conn_handle;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("err 'conn'\n");
        return rc;
    }

    params.itvl_min = parse_arg_uint16_dflt("interval_min",
                                            BLE_GAP_INITIAL_CONN_ITVL_MIN,
                                            &rc);
    if (rc != 0) {
        iot_printf("err 'interval_min'\n");
        return rc;
    }

    params.itvl_max = parse_arg_uint16_dflt("interval_max",
                                            BLE_GAP_INITIAL_CONN_ITVL_MAX,
                                            &rc);
    if (rc != 0) {
        iot_printf("err 'interval_max'\n");
        return rc;
    }

    params.latency = parse_arg_uint16_dflt("latency", 0, &rc);
    if (rc != 0) {
        iot_printf("err 'latency'\n");
        return rc;
    }

    params.supervision_timeout = parse_arg_uint16_dflt("timeout", 0x0100, &rc);
    if (rc != 0) {
        iot_printf("err 'timeout'\n");
        return rc;
    }

    params.min_ce_len = parse_arg_uint16_dflt("min_conn_event_len",
                                              0x0010, &rc);
    if (rc != 0) {
        iot_printf("err 'min_conn_event_len'\n");
        return rc;
    }

    params.max_ce_len = parse_arg_uint16_dflt("max_conn_event_len",
                                              0x0300, &rc);
    if (rc != 0) {
        iot_printf("err 'max_conn_event_len'\n");
        return rc;
    }

    rc = btshell_update_conn(conn_handle, &params);
    if (rc != 0) {
        iot_printf("error updating connection; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param conn_update_params_params[] = {
    {"conn", "conn_update_paramsion handle, usage: =<UINT16>"},
    {"interval_min", "usage: =[0-UINT16_MAX], default: 30"},
    {"interval_max", "usage: =[0-UINT16_MAX], default: 50"},
    {"latency", "usage: =[UINT16], default: 0"},
    {"timeout", "usage: =[UINT16], default: 0x0100"},
    {"min_conn_event_len", "usage: =[UINT16], default: 0x0010"},
    {"max_conn_event_len", "usage: =[UINT16], default: 0x0300"},
    {NULL, NULL}
};

static const struct shell_cmd_help conn_update_params_help = {
    .summary = "update connection parameters",
    .usage = "conn_update_params usage",
    .params = conn_update_params_params,
};
#endif

/*****************************************************************************
 * $conn-datalen                                                             *
 *****************************************************************************/

SRAM_CODE static int
cmd_conn_datalen(int argc, char **argv)
{
    uint16_t conn_handle;
    uint16_t tx_octets;
    uint16_t tx_time;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("err 'conn'\n");
        return rc;
    }

    tx_octets = parse_arg_uint16("octets", &rc);
    if (rc != 0) {
        iot_printf("err 'octets'\n");
        return rc;
    }

    tx_time = parse_arg_uint16("time", &rc);
    if (rc != 0) {
        iot_printf("err 'time'\n");
        return rc;
    }

    rc = btshell_datalen(conn_handle, tx_octets, tx_time);
    if (rc != 0) {
        iot_printf("error setting data length; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param conn_datalen_params[] = {
    {"conn", "conn_datalen handle, usage: =<UINT16>"},
    {"octets", "usage: =<UINT16>"},
    {"time", "usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help conn_datalen_help = {
    .summary = "set data length parameters for connection",
    .usage = NULL,
    .params = conn_datalen_params,
};
#endif

/*****************************************************************************
 * keystore                                                                  *
 *****************************************************************************/

static const struct kv_pair cmd_keystore_entry_type[] = {
    { "msec",       BLE_STORE_OBJ_TYPE_PEER_SEC },
    { "ssec",       BLE_STORE_OBJ_TYPE_OUR_SEC },
    { "cccd",       BLE_STORE_OBJ_TYPE_CCCD },
    { NULL }
};

SRAM_CODE static int
cmd_keystore_parse_keydata(int argc, char **argv, union ble_store_key *out,
                           int *obj_type)
{
    int rc;

    memset(out, 0, sizeof(*out));
    *obj_type = parse_arg_kv("type", cmd_keystore_entry_type, &rc);
    if (rc != 0) {
        iot_printf("err 'type'\n");
        return rc;
    }

    switch (*obj_type) {
    case BLE_STORE_OBJ_TYPE_PEER_SEC:
    case BLE_STORE_OBJ_TYPE_OUR_SEC:
        out->sec.peer_addr.type = parse_arg_kv("addr_type",
                                               cmd_addr_type, &rc);
        if (rc != 0) {
            iot_printf("err 'addr_type'\n");
            return rc;
        }

        rc = parse_arg_mac("addr", out->sec.peer_addr.val);
        if (rc != 0) {
            iot_printf("err 'addr'\n");
            return rc;
        }

        out->sec.ediv = parse_arg_uint16("ediv", &rc);
        if (rc != 0) {
            iot_printf("err 'ediv'\n");
            return rc;
        }

        out->sec.rand_num = parse_arg_uint64("rand", &rc);
        if (rc != 0) {
            iot_printf("err 'rand'\n");
            return rc;
        }
        return 0;

    default:
        return EINVAL;
    }
}

SRAM_CODE static int
cmd_keystore_parse_valuedata(int argc, char **argv,
                             int obj_type,
                             union ble_store_key *key,
                             union ble_store_value *out)
{
    int rc;
    int valcnt = 0;
    memset(out, 0, sizeof(*out));

    switch (obj_type) {
        case BLE_STORE_OBJ_TYPE_PEER_SEC:
        case BLE_STORE_OBJ_TYPE_OUR_SEC:
            rc = parse_arg_byte_stream_exact_length("ltk", out->sec.ltk, 16);
            if (rc == 0) {
                out->sec.ltk_present = 1;
                swap_in_place(out->sec.ltk, 16);
                valcnt++;
            } else if (rc != ENOENT) {
                iot_printf("err 'ltk'\n");
                return rc;
            }
            rc = parse_arg_byte_stream_exact_length("irk", out->sec.irk, 16);
            if (rc == 0) {
                out->sec.irk_present = 1;
                swap_in_place(out->sec.irk, 16);
                valcnt++;
            } else if (rc != ENOENT) {
                iot_printf("err 'irk'\n");
                return rc;
            }
            rc = parse_arg_byte_stream_exact_length("csrk", out->sec.csrk, 16);
            if (rc == 0) {
                out->sec.csrk_present = 1;
                swap_in_place(out->sec.csrk, 16);
                valcnt++;
            } else if (rc != ENOENT) {
                iot_printf("err 'csrk'\n");
                return rc;
            }
            out->sec.peer_addr = key->sec.peer_addr;
            out->sec.ediv = key->sec.ediv;
            out->sec.rand_num = key->sec.rand_num;
            break;
    }

    if (valcnt) {
        return 0;
    }
    return -1;
}

/*****************************************************************************
 * keystore-add                                                              *
 *****************************************************************************/

SRAM_CODE static int
cmd_keystore_add(int argc, char **argv)
{
    union ble_store_key key;
    union ble_store_value value;
    int obj_type;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rc = cmd_keystore_parse_keydata(argc, argv, &key, &obj_type);

    if (rc) {
        return rc;
    }

    rc = cmd_keystore_parse_valuedata(argc, argv, obj_type, &key, &value);

    if (rc) {
        return rc;
    }

    switch(obj_type) {
        case BLE_STORE_OBJ_TYPE_PEER_SEC:
            rc = ble_store_write_peer_sec(&value.sec);
            break;
        case BLE_STORE_OBJ_TYPE_OUR_SEC:
            rc = ble_store_write_our_sec(&value.sec);
            break;
        case BLE_STORE_OBJ_TYPE_CCCD:
            rc = ble_store_write_cccd(&value.cccd);
            break;
        default:
            rc = ble_store_write(obj_type, &value);
    }
    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param keystore_add_params[] = {
    {"type", "entry type, usage: =<msec|ssec|cccd>"},
    {"addr_type", "usage: =<public|random>"},
    {"addr", "usage: =<XX:XX:XX:XX:XX:XX>"},
    {"ediv", "usage: =<UINT16>"},
    {"rand", "usage: =<UINT64>"},
    {"ltk", "usage: =<XX:XX:...>, len=16 octets"},
    {"irk", "usage: =<XX:XX:...>, len=16 octets"},
    {"csrk", "usage: =<XX:XX:...>, len=16 octets"},
    {NULL, NULL}
};

static const struct shell_cmd_help keystore_add_help = {
    .summary = "add data to keystore",
    .usage = NULL,
    .params = keystore_add_params,
};
#endif

/*****************************************************************************
 * keystore-del                                                              *
 *****************************************************************************/

SRAM_CODE static int
cmd_keystore_del(int argc, char **argv)
{
    union ble_store_key key;
    int obj_type;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rc = cmd_keystore_parse_keydata(argc, argv, &key, &obj_type);

    if (rc) {
        return rc;
    }
    rc = ble_store_delete(obj_type, &key);
    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param keystore_del_params[] = {
    {"type", "entry type, usage: =<msec|ssec|cccd>"},
    {"addr_type", "usage: =<public|random>"},
    {"addr", "usage: =<XX:XX:XX:XX:XX:XX>"},
    {"ediv", "usage: =<UINT16>"},
    {"rand", "usage: =<UINT64>"},
    {NULL, NULL}
};

static const struct shell_cmd_help keystore_del_help = {
    .summary = "remove data from keystore",
    .usage = NULL,
    .params = keystore_del_params,
};
#endif

/*****************************************************************************
 * keystore-show                                                             *
 *****************************************************************************/

SRAM_CODE static int
cmd_keystore_iterator(int obj_type,
                      union ble_store_value *val,
                      void *cookie) {

    switch (obj_type) {
        case BLE_STORE_OBJ_TYPE_PEER_SEC:
        case BLE_STORE_OBJ_TYPE_OUR_SEC:
            iot_printf("Key: ");
            if (ble_addr_cmp(&val->sec.peer_addr, BLE_ADDR_ANY) == 0) {
                iot_printf("ediv=%u ", val->sec.ediv);
                iot_printf("ediv=%llu ", val->sec.rand_num);
            } else {
                iot_printf("addr_type=%u ", val->sec.peer_addr.type);
                print_addr(val->sec.peer_addr.val);
            }
            iot_printf("\n");

            if (val->sec.ltk_present) {
                iot_printf("    LTK: ");
                print_bytes(val->sec.ltk, 16);
                iot_printf("\n");
            }
            if (val->sec.irk_present) {
                iot_printf("    IRK: ");
                print_bytes(val->sec.irk, 16);
                iot_printf("\n");
            }
            if (val->sec.csrk_present) {
                iot_printf("    CSRK: ");
                print_bytes(val->sec.csrk, 16);
                iot_printf("\n");
            }
            break;
    }
    return 0;
}

SRAM_CODE static int
cmd_keystore_show(int argc, char **argv)
{
    int type;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    type = parse_arg_kv("type", cmd_keystore_entry_type, &rc);
    if (rc != 0) {
        iot_printf("err 'type'\n");
        return rc;
    }

    ble_store_iterate(type, &cmd_keystore_iterator, NULL);
    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param keystore_show_params[] = {
    {"type", "entry type, usage: =<msec|ssec|cccd>"},
    {NULL, NULL}
};

static const struct shell_cmd_help keystore_show_help = {
    .summary = "show data in keystore",
    .usage = NULL,
    .params = keystore_show_params,
};
#endif //MYNEWT_VAL_SHELL_CMD_HELP

/*****************************************************************************
 * $auth-passkey                                                             *
 *****************************************************************************/

SRAM_CODE static int
cmd_auth_passkey(int argc, char **argv)
{
    uint16_t conn_handle;
    struct ble_sm_io pk;
    char *yesno;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("err 'conn'\n");
        return rc;
    }

    pk.action = parse_arg_uint16("action", &rc);
    if (rc != 0) {
        iot_printf("err 'action'\n");
        return rc;
    }

    switch (pk.action) {
        case BLE_SM_IOACT_INPUT:
        case BLE_SM_IOACT_DISP:
           /* passkey is 6 digit number */
           pk.passkey = parse_arg_long_bounds("key", 0, 999999, &rc);
           if (rc != 0) {
               iot_printf("err 'key'\n");
               return rc;
           }
           break;

        case BLE_SM_IOACT_OOB:
            rc = parse_arg_byte_stream_exact_length("oob", pk.oob, 16);
            if (rc != 0) {
                iot_printf("err 'oob'\n");
                return rc;
            }
            break;

        case BLE_SM_IOACT_NUMCMP:
            yesno = parse_arg_extract("yesno");
            if (yesno == NULL) {
                iot_printf("err 'yesno' parameter\n");
                return EINVAL;
            }

            switch (yesno[0]) {
            case 'y':
            case 'Y':
                pk.numcmp_accept = 1;
                break;
            case 'n':
            case 'N':
                pk.numcmp_accept = 0;
                break;

            default:
                iot_printf("err 'yesno'\n");
                return EINVAL;
            }
            break;

       default:
         iot_printf("err passkey action action=%d\n", pk.action);
         return EINVAL;
    }

    rc = ble_sm_inject_io(conn_handle, &pk);
    if (rc != 0) {
        iot_printf("error providing passkey; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param auth_passkey_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"action", "auth action type, usage: =<UINT16>"},
    {"key", "usage: =[0-999999]"},
    {"oob", "usage: =[XX:XX...], len=16 octets"},
    {"yesno", "usage: =[string]"},
    {NULL, NULL}
};

static const struct shell_cmd_help auth_passkey_help = {
    .summary = "set authorization passkey options",
    .usage = NULL,
    .params = auth_passkey_params,
};
#endif

/*****************************************************************************
 * $security-pair                                                            *
 *****************************************************************************/

SRAM_CODE static int
cmd_security_pair(int argc, char **argv)
{
    uint16_t conn_handle;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("err 'conn'\n");
        return rc;
    }

    rc = btshell_sec_pair(conn_handle);
    if (rc != 0) {
        iot_printf("error initiating pairing; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param security_pair_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help security_pair_help = {
    .summary = "start pairing procedure for connection",
    .usage = NULL,
    .params = security_pair_params,
};
#endif

/*****************************************************************************
 * $security-unpair                                                            *
 *****************************************************************************/

SRAM_CODE static int
cmd_security_unpair(int argc, char **argv)
{
    ble_addr_t peer;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rc = parse_arg_mac("peer_addr", peer.val);
    if (rc == 0) {

        peer.type = parse_arg_kv_dflt("peer_addr_type",
                                      cmd_peer_addr_types,
                                      BLE_ADDR_PUBLIC, &rc);
        if (rc != 0) {
            iot_printf("err 'peer_addr_type'\n");
            return rc;
        }
    } else {
        iot_printf("err 'peer_addr'\n");
        return rc;
    }

    rc = ble_gap_unpair(&peer);
    if (rc != 0) {
        iot_printf("error unpairing; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param security_unpair_params[] = {
    {"peer_addr_type", "usage: =[public|random|public_id|random_id], default: public"},
    {"peer_addr", "usage: =[XX:XX:XX:XX:XX:XX]"},
    {NULL, NULL}
};

static const struct shell_cmd_help security_unpair_help = {
    .summary = "unpair a peer device",
    .usage = NULL,
    .params = security_unpair_params,
};
#endif

/*****************************************************************************
 * $security-start                                                           *
 *****************************************************************************/

SRAM_CODE static int
cmd_security_start(int argc, char **argv)
{
    uint16_t conn_handle;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("err 'conn'\n");
        return rc;
    }

    rc = btshell_sec_start(conn_handle);
    if (rc != 0) {
        iot_printf("error starting security; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param security_start_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help security_start_help = {
    .summary = "start security procedure for connection",
    .usage = NULL,
    .params = security_start_params,
};
#endif

/*****************************************************************************
 * $security-encryption                                                      *
 *****************************************************************************/

SRAM_CODE static int
cmd_security_encryption(int argc, char **argv)
{
    uint16_t conn_handle;
    uint16_t ediv;
    uint64_t rand_val;
    uint8_t ltk[16];
    int rc;
    int auth;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("err 'conn' \n");
        return rc;
    }

    ediv = parse_arg_uint16("ediv", &rc);
    if (rc == ENOENT) {
        rc = btshell_sec_restart(conn_handle, NULL, 0, 0, 0);
    } else {
        rand_val = parse_arg_uint64("rand", &rc);
        if (rc != 0) {
            iot_printf("err 'rand' \n");
            return rc;
        }

        auth = parse_arg_bool("auth", &rc);
        if (rc != 0) {
            iot_printf("err 'auth' \n");
            return rc;
        }

        rc = parse_arg_byte_stream_exact_length("ltk", ltk, 16);
        if (rc != 0) {
            iot_printf("err 'ltk' \n");
            return rc;
        }

        rc = btshell_sec_restart(conn_handle, ltk, ediv, rand_val, auth);
    }

    if (rc != 0) {
        iot_printf("error initiating encryption; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param security_encryption_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"ediv", "usage: =[UINT16]"},
    {"rand", "usage: =[UINT64]"},
    {"auth", "usage: =[0-1]"},
    {"ltk", "usage: =[XX:XX...], len=16 octets"},
    {NULL, NULL}
};

static const struct shell_cmd_help security_encryption_help = {
    .summary = "start encryption procedure for connection",
    .usage = NULL,
    .params = security_encryption_params,
};
#endif

/*****************************************************************************
 * $security-set-data                                                        *
 *****************************************************************************/

SRAM_CODE static int
cmd_security_set_data(int argc, char **argv)
{
    uint8_t tmp;
    int good;
    int rc;

    good = 0;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    tmp = parse_arg_bool("oob_flag", &rc);
    if (rc == 0) {
        ble_hs_cfg.sm_oob_data_flag = tmp;
        good++;
    } else if (rc != ENOENT) {
        iot_printf("err 'oob_flag' \n");
        return rc;
    }

    tmp = parse_arg_bool("mitm_flag", &rc);
    if (rc == 0) {
        good++;
        ble_hs_cfg.sm_mitm = tmp;
    } else if (rc != ENOENT) {
        iot_printf("err 'mitm_flag' \n");
        return rc;
    }

    tmp = parse_arg_uint8("io_capabilities", &rc);
    if (rc == 0) {
        good++;
        ble_hs_cfg.sm_io_cap = tmp;
    } else if (rc != ENOENT) {
        iot_printf("err 'io_capabilities' \n");
        return rc;
    }

    tmp = parse_arg_uint8("our_key_dist", &rc);
    if (rc == 0) {
        good++;
        ble_hs_cfg.sm_our_key_dist = tmp;
    } else if (rc != ENOENT) {
        iot_printf("err 'our_key_dist' \n");
        return rc;
    }

    tmp = parse_arg_uint8("their_key_dist", &rc);
    if (rc == 0) {
        good++;
        ble_hs_cfg.sm_their_key_dist = tmp;
    } else if (rc != ENOENT) {
        iot_printf("err 'their_key_dist' \n");
        return rc;
    }

    tmp = parse_arg_bool("bonding", &rc);
    if (rc == 0) {
        good++;
        ble_hs_cfg.sm_bonding = tmp;
    } else if (rc != ENOENT) {
        iot_printf("err 'bonding' \n");
        return rc;
    }

    tmp = parse_arg_bool("sc", &rc);
    if (rc == 0) {
        good++;
        ble_hs_cfg.sm_sc = tmp;
    } else if (rc != ENOENT) {
        iot_printf("err 'sc' \n");
        return rc;
    }

    if (!good) {
        iot_printf("Error: no valid settings specified\n");
        return -1;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param security_set_data_params[] = {
    {"oob_flag", "usage: =[0-1]"},
    {"mitm_flag", "usage: =[0-1]"},
    {"io_capabilities", "usage: =[UINT8]"},
    {"our_key_dist", "usage: =[UINT8]"},
    {"their_key_dist", "usage: =[UINT8]"},
    {"bonding", "usage: =[0-1]"},
    {"sc", "usage: =[0-1]"},
    {NULL, NULL}
};

static const struct shell_cmd_help security_set_data_help = {
    .summary = "set security data",
    .usage = NULL,
    .params = security_set_data_params,
};
#endif

/*****************************************************************************
 * $test-tx                                                                  *
 *                                                                           *
 * Command to transmit 'num' packets of size 'len' at rate 'r' to
 * handle 'h' Note that length must be <= 251. The rate is in msecs.
 *
 *****************************************************************************/

SRAM_CODE static int
cmd_test_tx(int argc, char **argv)
{
    int rc;
    uint16_t rate;
    uint16_t len;
    uint16_t handle;
    uint16_t num;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rate = parse_arg_uint16("rate", &rc);
    if (rc != 0) {
        iot_printf("err 'rate'\n");
        return rc;
    }

    len = parse_arg_uint16("length", &rc);
    if (rc != 0) {
        iot_printf("err 'length'\n");
        return rc;
    }
    if ((len > 251) || (len < 4)) {
        iot_printf("error: len must be  4 ~ 251");
    }

    num = parse_arg_uint16("num", &rc);
    if (rc != 0) {
        iot_printf("err 'num'\n");
        return rc;
    }

    handle = parse_arg_uint16("handle", &rc);
    if (rc != 0) {
        iot_printf("err 'handle'\n");
        return rc;
    }

    rc = btshell_tx_start(handle, len, rate, num);
    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param test_tx_params[] = {
    {"num", "number of packets, usage: =<UINT16>"},
    {"length", "size of packet, usage: =<UINT16>"},
    {"rate", "rate of tx, usage: =<UINT16>"},
    {"handle", "handle to tx to, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help test_tx_help = {
    .summary = "test packet transmission",
    .usage = NULL,
    .params = test_tx_params,
};
#endif

/*****************************************************************************
 * $phy-set                                                                  *
 *****************************************************************************/

SRAM_CODE static int
cmd_phy_set(int argc, char **argv)
{
    uint16_t conn;
    uint8_t tx_phys_mask;
    uint8_t rx_phys_mask;
    uint16_t phy_opts;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("invalid 'conn' parameter\n");
        return rc;
    }

    tx_phys_mask = parse_arg_uint8("tx_phys_mask", &rc);
    if (rc != 0) {
        iot_printf("invalid 'tx_phys_mask' parameter\n");
        return rc;
    }

    rx_phys_mask = parse_arg_uint8("rx_phys_mask", &rc);
    if (rc != 0) {
        iot_printf("invalid 'rx_phys_mask' parameter\n");
        return rc;
    }

    phy_opts = parse_arg_uint16("phy_opts", &rc);
    if (rc != 0) {
        iot_printf("invalid 'phy_opts' parameter\n");
        return rc;
    }

    return ble_gap_set_prefered_le_phy(conn, tx_phys_mask, rx_phys_mask,
                                       phy_opts);
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param phy_set_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"tx_phys_mask", "usage: =<UINT8>"},
    {"rx_phys_mask", "usage: =<UINT8>"},
    {"phy_opts", "usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help phy_set_help = {
    .summary = "set preferred PHYs",
    .usage = NULL,
    .params = phy_set_params,
};
#endif


/*****************************************************************************
 * $default_power_set                                                                 *
 *****************************************************************************/
SRAM_CODE static int
cmd_default_power_set(int argc, char **argv)
{
    int rc, dbm;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }
	
	dbm = parse_arg_long_bounds("tx_pwr", 0, 15, &rc);
	if(rc != 0){
	    iot_printf("invalid 'conn' parameter\n");
        return rc;	
	}

	ble_gap_set_default_tx_power((int8_t)dbm);
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param pwr_set_params[] = {
    {"tx_pwr", "ble default pwr(dbm), usage: =<0 ~ 15>"},
    {NULL, NULL}
};

static const struct shell_cmd_help pwr_set_help = {
    .summary = "set ble default power(dbm)",
    .usage = NULL,
    .params = pwr_set_params,
};
#endif


/*****************************************************************************
 * $phy-set-default                                                          *
 *****************************************************************************/

SRAM_CODE static int
cmd_phy_set_default(int argc, char **argv)
{
    uint8_t tx_phys_mask;
    uint8_t rx_phys_mask;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    tx_phys_mask = parse_arg_uint8("tx_phys_mask", &rc);
    if (rc != 0) {
        iot_printf("invalid 'tx_phys_mask' parameter\n");
        return rc;
    }

    rx_phys_mask = parse_arg_uint8("rx_phys_mask", &rc);
    if (rc != 0) {
        iot_printf("invalid 'rx_phys_mask' parameter\n");
        return rc;
    }

    return ble_gap_set_prefered_default_le_phy(tx_phys_mask, rx_phys_mask);
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param phy_set_default_params[] = {
    {"tx_phys_mask", "usage: =<UINT8>"},
    {"rx_phys_mask", "usage: =<UINT8>"},
    {NULL, NULL}
};

static const struct shell_cmd_help phy_set_default_help = {
    .summary = "set preferred default PHYs",
    .usage = NULL,
    .params = phy_set_default_params,
};
#endif

/*****************************************************************************
 * $phy-read                                                                 *
 *****************************************************************************/

SRAM_CODE static int
cmd_phy_read(int argc, char **argv)
{
    uint16_t conn = 0;
    uint8_t tx_phy;
    uint8_t rx_phy;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("invalid 'conn' parameter\n");
        return rc;
    }

    rc = ble_gap_read_le_phy(conn, &tx_phy, &rx_phy);
    if (rc != 0) {
        iot_printf("Could not read PHY error: %d\n", rc);
        return rc;
    }

    iot_printf("TX_PHY: %d\n", tx_phy);
    iot_printf("RX_PHY: %d\n", tx_phy);

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param phy_read_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help phy_read_help = {
    .summary = "read PHYs",
    .usage = NULL,
    .params = phy_read_params,
};
#endif


/*****************************************************************************
 * $host-enable                                                              *
 *****************************************************************************/

SRAM_CODE static int
cmd_host_enable(int argc, char **argv)
{
    ble_hs_sched_start();

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_cmd_help host_enable_help = {
    .summary = "start the NimBLE host",
    .usage = NULL,
    .params = NULL,
};
#endif

/*****************************************************************************
 * $host-disable                                                              *
 *****************************************************************************/

SRAM_CODE static void
on_stop(int status, void *arg)
{
    if (status == 0) {
        iot_printf("host stopped\n");
    } else {
        iot_printf("host failed stop; rc=%d\n", status);
    }
}

SRAM_CODE static int
cmd_host_disable(int argc, char **argv)
{
    static struct ble_hs_stop_listener listener;
    int rc;

    rc = ble_hs_stop(&listener, on_stop, NULL);
    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_cmd_help host_disable_help = {
    .summary = "stop the NimBLE host",
    .usage = NULL,
    .params = NULL,
};
#endif
SRAM_CODE static  int rpa_timeout(int argc, char **argv){

    uint8_t buf[BLE_HCI_SET_RESOLV_PRIV_ADDR_TO_LEN];
    int rc;
	uint16_t timeout;
	
    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    timeout = parse_arg_uint16("timeout", &rc);
    if (rc != 0) {
        iot_printf("invalid 'timeout' parameter\n");
        return rc;
    }

	put_le16(buf, timeout);

    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_SET_RPA_TMO),
                                      buf, sizeof(buf));

	if(rc == 0){
		iot_printf(" send rpa timeout suc \n");

	}else{
		iot_printf(" send rpa timeout failed \n");
	}
    return rc;


}
SRAM_CODE int smp_data_inject(int argc, char **argv)
{
    struct ble_sm_io pkey;
	int rc,data,type;
    uint16_t handle;
	rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }
	
    handle =  parse_arg_uint8("handle", &rc);
	if (rc != 0) {
        iot_printf("invalid 'handle' parameter\n");
        return rc;
    }

    type  = parse_arg_uint8("type", &rc);
    if (rc != 0) {
        iot_printf("invalid 'type' parameter\n");
        return rc;
    }

	data = parse_arg_uint32("data", &rc);
    if (rc != 0) {
        iot_printf("invalid 'data' parameter\n");
        return rc;
    }
	
	pkey.action = type;
	pkey.passkey = data;
	ble_sm_inject_io(handle, &pkey);
	iot_printf("inject:type:%d,passkey:%d\n",type,data);
	
	return 0;
}

SRAM_CODE int ble_hci_le_set_adv_data(int argc, char **argv)
{
    int rc;
    uint8_t data[31];
    uint16_t opcode;
    uint8_t buf[BLE_HCI_SET_ADV_DATA_LEN];
    int len = 24;
    uint8_t adv_data[24]={0x05,0x03,0x00,0x18,0x01,0x18,0x0D,0x09,0x50,0x54,0x53,0x2D,
    0x47,0x41,0x50,0x2D,0x30,0x36,0x42,0x38,0x03,0x19,0x00,0x00	};
	int i;
    
    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }
    
    rc = parse_arg_to_hex("data", sizeof(data), data, &len);
    if(rc)
    {	
        memcpy(data,adv_data,24);
    }
    printf("advtise data [%d]:\n",len);   
    for( i =0; i<len ;i++)
        printf("%02x",data[i]);
    printf("\n");
     //memcpy(data,adv_data,24);
    
    opcode = BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_SET_ADV_DATA);
    rc = ble_hs_hci_cmd_build_le_set_adv_data(data, len, buf, sizeof buf);
    assert(rc == 0);
    return ble_hs_hci_cmd_tx_empty_ack(opcode, buf, sizeof(buf));
}
static const struct shell_param smp_data_inject_params[] = {
    {"handle", "usage: =<UINT16>"},
    {"type", "usage: =<UINT8>"},
    {"data", "usage: =UINT32"},
    {NULL, NULL}
};

static const struct shell_cmd_help smp_data_inject_help = {
    .summary = "support smp passkey entry ",
    .usage = NULL,
    .params = smp_data_inject_params,
};


static const struct shell_param rpa_timeout_params[] = {
    {"timeout", " usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help rpa_timeout_help = {
    .summary = "rpa timeout",
    .usage = NULL,
    .params = rpa_timeout_params,
};

static const struct shell_param  ble_hci_le_set_adv_data_params[] = {
    {"data", " usage: =<string>"},
    {NULL, NULL}
};
static const struct shell_cmd_help ble_hci_le_set_adv_data_help = {
    .summary = "hci set adv data",
    .usage = NULL,
    .params = ble_hci_le_set_adv_data_params,
};

/*****************************************************************************
 * $gatt-discover                                                            *
 *****************************************************************************/

static const struct shell_param gatt_discover_characteristic_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"uuid", "discover by uuid, usage: =[UUID]"},
    {"start", "start handle, usage: =<UINT16>"},
    {"end", "end handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_discover_characteristic_help = {
    .summary = "perform characteristic discovery procedure",
    .usage = NULL,
    .params = gatt_discover_characteristic_params,
};

static const struct shell_param gatt_discover_descriptor_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"start", "start handle, usage: =<UINT16>"},
    {"end", "end handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_discover_descriptor_help = {
    .summary = "perform descriptor discovery procedure",
    .usage = NULL,
    .params = gatt_discover_descriptor_params,
};

static const struct shell_param gatt_discover_service_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"uuid", "discover by uuid, usage: =[UUID]"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_discover_service_help = {
    .summary = "perform service discovery procedure",
    .usage = NULL,
    .params = gatt_discover_service_params,
};

static const struct shell_param gatt_discover_full_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_discover_full_help = {
    .summary = "perform full discovery procedure",
    .usage = NULL,
    .params = gatt_discover_full_params,
};

/*****************************************************************************
 * $gatt-exchange-mtu                                                        *
 *****************************************************************************/

static const struct shell_param gatt_exchange_mtu_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_exchange_mtu_help = {
    .summary = "perform mtu exchange procedure",
    .usage = NULL,
    .params = gatt_exchange_mtu_params,
};

/*****************************************************************************
 * $gatt-find-included-services                                              *
 *****************************************************************************/

static const struct shell_param gatt_find_included_services_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"start", "start handle, usage: =<UINT16>"},
    {"end", "end handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_find_included_services_help = {
    .summary = "perform find included services procedure",
    .usage = NULL,
    .params = gatt_find_included_services_params,
};

/*****************************************************************************
 * $gatt-notify                                                                *
 *****************************************************************************/

static const struct shell_param gatt_notify_params[] = {
    {"attr", "attribute handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_notify_help = {
    .summary = "notify about attribute value changed",
    .usage = NULL,
    .params = gatt_notify_params,
};

/*****************************************************************************
 * $gatt-read                                                                *
 *****************************************************************************/

static const struct shell_param gatt_read_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"long", "is read long, usage: =[0-1], default=0"},
    {"attr", "attribute handle, usage: =<UINT16>"},
    {"offset", "offset value, usage: =<UINT16>"},
    {"uuid", "read by uuid, usage: =[UUID]"},
    {"start", "start handle, usage: =<UINT16>"},
    {"end", "end handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_read_help = {
    .summary = "perform gatt read procedure",
    .usage = NULL,
    .params = gatt_read_params,
};

/*****************************************************************************
 * $gatt-service-changed                                                     *
 *****************************************************************************/

static const struct shell_param gatt_service_changed_params[] = {
    {"start", "start handle, usage: =<UINT16>"},
    {"end", "end handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_service_changed_help = {
    .summary = "send service changed indication",
    .usage = NULL,
    .params = gatt_service_changed_params,
};

/*****************************************************************************
 * $gatt-service-visibility                                                  *
 *****************************************************************************/

static const struct shell_param gatt_service_visibility_params[] = {
    {"handle", "usage: =<UINT16>"},
    {"visibility", "usage: =<0-1>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_service_visibility_help = {
    .summary = "change service visibility",
    .usage = NULL,
    .params = gatt_service_visibility_params,
};

/*****************************************************************************
 * $gatt-show                                                                *
 *****************************************************************************/

static const struct shell_param gatt_show_params[] = {
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_show_help = {
    .summary = "show discovered gatt database",
    .usage = NULL,
    .params = gatt_show_params,
};

static const struct shell_cmd_help gatt_show_local_help = {
    .summary = "show local gatt database",
    .usage = NULL,
    .params = gatt_show_params,
};

static const struct shell_cmd_help gatt_show_addr_help = {
    .summary = "show device address",
    .usage = NULL,
    .params = gatt_show_params,
};

static const struct shell_cmd_help gatt_show_conn_help = {
    .summary = "show connections information",
    .usage = NULL,
    .params = gatt_show_params,
};

/*****************************************************************************
 * $gatt-write                                                                *
 *****************************************************************************/

static const struct shell_param gatt_write_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"no_rsp", "write without response, usage: =[0-1], default=0"},
    {"long", "is write long, usage: =[0-1], default=0"},
    {"attr", "attribute handle, usage: =<UINT16>"},
    {"offset", "attribute handle, usage: =<UINT16>"},
    {"value", "usage: =<octets>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_write_help = {
    .summary = "perform gatt write procedure",
    .usage = NULL,
    .params = gatt_write_params,
};


static const struct shell_param gatt_thou_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"attr", "attribute handle, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_thou_help = {
    .summary = "gatt thoughput test",
    .usage = NULL,
    .params = gatt_thou_params,
};


static const struct shell_param gatt_write_exe_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"flags", "usage: =<octet>"},
    {NULL, NULL}
};

static const struct shell_cmd_help gatt_exe_write_help = {
    .summary = "perform gatt exe write procedure",
    .usage = NULL,
    .params = gatt_write_exe_params,
};


#if (MYNEWT_VAL_BLE_L2CAP_COC_MAX_NUM)
#if (MYNEWT_VAL_SHELL_CMD_HELP)
/*****************************************************************************
 * $l2cap-update                                                             *
 *****************************************************************************/

static const struct shell_param l2cap_update_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"interval_min", "usage: =[0-UINT16_MAX], default: 30"},
    {"interval_max", "usage: =[0-UINT16_MAX], default: 50"},
    {"latency", "usage: =[UINT16], default: 0"},
    {"timeout", "usage: =[UINT16], default: 0x0100"},
    {NULL, NULL}
};

static const struct shell_cmd_help l2cap_update_help = {
    .summary = "update l2cap parameters for connection",
    .usage = NULL,
    .params = l2cap_update_params,
};


/*****************************************************************************
 * $l2cap-mtu                                                             *
 *****************************************************************************/
static const struct shell_param l2cap_mtu_params[] = {
    {"mtu", "usage: =[0-UINT16_MAX], default: 100"},
    {NULL, NULL}
};

static const struct shell_cmd_help l2cap_mtu_help = {
    .summary = "set l2cap mtu",
    .usage = NULL,
    .params = l2cap_mtu_params,
};

/*****************************************************************************
 * $l2cap-create-server                                                      *
 *****************************************************************************/

static const struct shell_param l2cap_create_server_params[] = {
    {"psm", "usage: =<UINT16>"},
    {"error", "usage: used for PTS testing:"},
    {"", "0 - always accept"},
    {"", "1 - reject with insufficient authentication"},
    {"", "2 - reject with insufficient authorization"},
    {"", "3 - reject with insufficient key size"},
    {NULL, NULL}
};

static const struct shell_cmd_help l2cap_create_server_help = {
    .summary = "create l2cap server",
    .usage = NULL,
    .params = l2cap_create_server_params,
};

/*****************************************************************************
 * $l2cap-connect                                                            *
 *****************************************************************************/

static const struct shell_param l2cap_connect_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"psm", "usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help l2cap_connect_help = {
    .summary = "perform l2cap connect procedure",
    .usage = NULL,
    .params = l2cap_connect_params,
};

/*****************************************************************************
 * $l2cap-disconnect                                                         *
 *****************************************************************************/

static const struct shell_param l2cap_disconnect_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"idx", "usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help l2cap_disconnect_help = {
    .summary = "perform l2cap disconnect procedure",
    .usage = "use gatt-show-coc to get the parameters",
    .params = l2cap_disconnect_params,
};

/*****************************************************************************
 * $l2cap-send                                                               *
 *****************************************************************************/

static const struct shell_param l2cap_send_params[] = {
    {"conn", "connection handle, usage: =<UINT16>"},
    {"idx", "usage: =<UINT16>"},
    {"bytes", "number of bytes to send, usage: =<UINT16>"},
    {NULL, NULL}
};

static const struct shell_cmd_help l2cap_send_help = {
    .summary = "perform l2cap send procedure",
    .usage = "use l2cap-show-coc to get the parameters",
    .params = l2cap_send_params,
};

/*****************************************************************************
 * $l2cap-show-coc                                                           *
 *****************************************************************************/

static const struct shell_param l2cap_show_coc_params[] = {
    {NULL, NULL}
};

static const struct shell_cmd_help l2cap_show_coc_help = {
    .summary = "show coc information",
    .usage = NULL,
    .params = l2cap_show_coc_params,
};

#endif
#endif


SRAM_CODE static int cmd_dtm_tx(int argc, char **argv)
{
    int rc;
    uint8_t channel;
    uint8_t length;
	uint8_t payload;
	uint8_t phyMode;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "stop") == 0) {
        rc = ble_hs_dtm_end();
        if (rc != 0) {
            iot_printf("dtm stop fail: %d\n", rc);
            return rc;
        }
        return 0;
    }

    channel = parse_arg_uint8("chan", &rc);
    if (rc != 0) {
        iot_printf("err 'chan'\n");
        return rc;
    }
	
    length = parse_arg_uint8("len", &rc);
    if (rc != 0) {
        iot_printf("err 'len'\n");
        return rc;
    }

    payload = parse_arg_uint8("payload", &rc);
    if (rc != 0) {
        iot_printf("err 'payload'\n");
        return rc;
    }
	
    phyMode = parse_arg_uint8("phyMode", &rc);
    if (rc != 0) {
        iot_printf("err 'phyMode'\n");
        return rc;
    }

	rc = ble_hs_dtm_tx_test(channel,length,payload,phyMode);

    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param dtm_tx_params[] = {
	{"stop", "stop dtm test"},
    {"chan", "rf channel, usage: =<UINT8>"},
    {"len", "size of packet, usage: =<UINT8>"},
    {"payload", "dtm tx payload byte_pattern, usage: =<UINT18> 0 ~ 7"},
    {"phyMode", "dtm tx phy, usage: =<UINT8> 1~4"},
    {NULL, NULL}
};

static const struct shell_cmd_help dtm_tx_help = {
    .summary = "dtm tx start",
    .usage = NULL,
    .params = dtm_tx_params,
};
#endif


SRAM_CODE static int cmd_dtm_rx(int argc, char **argv)
{
    int rc;
    uint8_t channel;
	uint8_t phyMode;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "stop") == 0) {
        rc = ble_hs_dtm_end();
        if (rc != 0) {
            iot_printf("dtm stop fail: %d\n", rc);
            return rc;
        }
        return 0;
    }

    channel = parse_arg_uint8("chan", &rc);
    if (rc != 0) {
        iot_printf("err 'chan'\n");
        return rc;
    }
	
    phyMode = parse_arg_uint8("phyMode", &rc);
    if (rc != 0) {
        iot_printf("err 'phyMode'\n");
        return rc;
    }
	
	rc = ble_hs_dtm_rx_test(channel, phyMode);

    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param dtm_rx_params[] = {
	{"stop", "stop dtm test"},
    {"chan", "rf channel, usage: =<UINT8>"},
    {"phyMode", "dtm tx phy, usage: =<UINT8> 1~4"},
    {NULL, NULL}
};

static const struct shell_cmd_help dtm_rx_help = {
    .summary = "dtm rx start",
    .usage = NULL,
    .params = dtm_rx_params,
};
#endif

SRAM_CODE static int cmd_tone_tx(int argc, char **argv)
{
    int rc;
    uint8_t channel;
    uint8_t offset;
	uint8_t power;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "stop") == 0) {
        rc = ble_tone_tx_stop();
        if (rc != 0) {
            iot_printf("dtm stop fail: %d\n", rc);
            return rc;
        }
        return 0;
    }

    channel = parse_arg_uint8("chan", &rc);
    if (rc != 0) {
        iot_printf("err 'chan'\n");
        return rc;
    }

    power = parse_arg_uint8("power", &rc);
    if (rc != 0) {
        iot_printf("err 'payload'\n");
        return rc;
    }

	rc = ble_tone_tx_start(channel, power);
    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param tone_tx_params[] = {
	{"stop", "stop tone tx"},
    {"chan", "rf channel, usage: =<UINT8>"},
    {"power", "tone tx power step, usage: =<UINT18> 0 ~ 6"},
    {NULL, NULL}
};

static const struct shell_cmd_help tone_tx_help = {
    .summary = "tone tx start",
    .usage = NULL,
    .params = tone_tx_params,
};
#endif


SRAM_CODE static int cmd_srrc_set(int argc, char **argv)
{
    int rc;
    uint8_t channel;
    uint8_t offset;
	uint8_t power;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "start") == 0) {
        rc = ble_start_srrc();
    }
	else if (argc > 1 && strcmp(argv[1], "stop") == 0) {
		 rc = ble_stop_srrc();
	}
	
    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param srrc_set_params[] = {
	{"star/stop", "start or stop srrc mode"},
    {NULL, NULL}
};

static const struct shell_cmd_help srrc_set_help = {
    .summary = "srrc_set start or stop",
    .usage = NULL,
    .params = srrc_set_params,
};
#endif



SRAM_CODE static int cmd_scan_name_set(int argc, char **argv)
{
    uint8_t *name;
	int name_len;
	int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

	name = (uint8_t *)parse_arg_extract("scan_name");
	if(name == NULL){
		iot_printf("err 'scan_name'\n");
		return -1;
	}
	name_len = strlen(name);
	
	rc = ble_hs_scan_set_name_filt(name_len, name);
	
	return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param name_set_params[] = {
	{"name", "scan name, usage: =str"},
    {NULL, NULL}
};

static const struct shell_cmd_help name_set_help = {
    .summary = "scan name set",
    .usage = NULL,
    .params = name_set_params,
};
#endif


SRAM_CODE static int cmd_afh_set(int argc, char **argv)
{
    uint8_t threshold;
    uint8_t period;
	int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

	threshold = parse_arg_uint8("threshold", &rc);
	if(rc != 0 || threshold > 100){
		iot_printf("err 'threshold'\n");
		return -1;
	}

    period = parse_arg_uint8("period", &rc);
	if(rc != 0 || period > 30){
		iot_printf("err 'period'\n");
		return -1;
	}
	
	rc = ble_hs_afh_param_set(threshold, period);
	
	return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param afh_set_params[] = {
	{"threshold", "afh threshold(good packet ratio), usage: =<UINT8_T>, 0~100%"},
	{"period", "afh timer period, usage: =<UINT8_T>, 0~30s"},
    {NULL, NULL}
};

static const struct shell_cmd_help afh_set_help = {
    .summary = "afh param set",
    .usage = NULL,
    .params = afh_set_params,
};
#endif


SRAM_CODE static int cmd_ble_ce_test(int argc, char **argv)
{
    uint8_t enable;
	int tmp;
	int8_t rssi = 0;
	int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

	enable = parse_arg_bool("enable", &rc);
	if (rc != 0) {
		iot_printf("err param 'enable'\n");
		return rc;
	}


	tmp = parse_arg_long_bounds("rssi", -80, -50, &rc);
	if (rc == 0) {
		rssi = tmp;
	}
	else if (rc != ENOENT) {
        iot_printf("err 'rssi' parameter\n");
        return rc;
    }
	
	rc = ble_hs_set_ce_test(enable, rssi);
	
	return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param ble_ce_set_params[] = {
	{"enable", "enable/disable ble ce test, usage: =[0 ~ 1]"},
	{"rssi",   "noise threshold, usage: =[-80 ~ -50]"},	
    {NULL, NULL}
};

static const struct shell_cmd_help ble_ce_set_help = {
    .summary = "ble ce test",
    .usage = NULL,
    .params = ble_ce_set_params,
};
#endif




//---------------------------------------------------
//add for ble rf-phy test

SRAM_CODE static int cmd_reset(void)
{
    int rc;

    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_CTLR_BASEBAND,
                                                BLE_HCI_OCF_CB_RESET),
                                     NULL, 0);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param reset_params[] = {
    {NULL, NULL}
};

static const struct shell_cmd_help reset_help = {
    .summary = "reset ",
    .usage = NULL,
    .params = reset_params,
};
#endif


SRAM_CODE static int cmd_receiver_test(int argc, char **argv)
{
    int rc;
    uint8_t channel;
	uint8_t phyMode;
    uint8_t Modulation_Index;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "stop") == 0) {
        rc = ble_hs_dtm_end();
        if (rc != 0) {
            iot_printf("dtm stop fail: %d\n", rc);
            return rc;
        }
        return 0;
    }

    channel = parse_arg_uint8("chan", &rc);
    if (rc != 0) {
        iot_printf("err 'chan'\n");
        return rc;
    }
	
    phyMode = parse_arg_uint8("phyMode", &rc);
    if (rc != 0) {
        iot_printf("err 'phyMode'\n");
        return rc;
    }

    Modulation_Index = parse_arg_uint8("Modulation_Index", &rc);
    if (rc != 0) {
        iot_printf("err 'phyMode'\n");
        return rc;
    }

    rc = ble_hs_rx_test( channel,  phyMode,  Modulation_Index);

    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param receiver_test_params[] = {
	{"stop", "stop receiver test"},
    {"chan", "rf channel, usage: =<UINT8>"},
    {"phyMode", "receiver set to use phy, usage: =<UINT8> 1~4"},
    {"Modulation_Index", "usage: =<UINT8> "},
    {NULL, NULL}
};

static const struct shell_cmd_help receiver_test_help = {
    .summary = "receiver test start",
    .usage = NULL,
    .params = receiver_test_params,
};
#endif



SRAM_CODE static int cmd_transmitter_test(int argc, char **argv)
{
    int rc;
    uint8_t channel;
    uint8_t length;
	uint8_t payload;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    if (argc > 1 && strcmp(argv[1], "stop") == 0) {
        rc = ble_hs_dtm_end();
        if (rc != 0) {
            iot_printf("cmd_transmitter_test stop fail: %d\n", rc);
            return rc;
        }
        return 0;
    }

    channel = parse_arg_uint8("chan", &rc);
    if (rc != 0) {
        iot_printf("err 'chan'\n");
        return rc;
    }
	
    length = parse_arg_uint8("len", &rc);
    if (rc != 0) {
        iot_printf("err 'len'\n");
        return rc;
    }

    payload = parse_arg_uint8("payload", &rc);
    if (rc != 0) {
        iot_printf("err 'payload'\n");
        return rc;
    }

	rc = ble_hs_tx_test( channel,  length,  payload);

    return rc;
}

#if (MYNEWT_VAL_SHELL_CMD_HELP)
static const struct shell_param transmitter_test_params[] = {
	{"stop", "stop dtm test"},
    {"chan", "rf channel, usage: =<UINT8>"},
    {"len", "size of packet, usage: =<UINT8>"},
    {"payload", "dtm tx payload byte_pattern, usage: =<UINT18> 0 ~ 7"},
    {NULL, NULL}
};

static const struct shell_cmd_help transmitter_test_help = {
    .summary = "transmitter tx start",
    .usage = NULL,
    .params = transmitter_test_params,
};
#endif
//--------------------------------------------------------------

static const struct shell_cmd btshell_commands[] = {
#if (MYNEWT_VAL_BLE_EXT_ADV)
    {
        .sc_cmd = "advertise-configure",
        .sc_cmd_func = cmd_advertise_configure,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &advertise_configure_help,
#endif
    },
    {
        .sc_cmd = "advertise-set-addr",
        .sc_cmd_func = cmd_advertise_set_addr,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &advertise_set_addr_help,
#endif
    },
    {
        .sc_cmd = "advertise-set-adv-data",
        .sc_cmd_func = cmd_set_adv_data,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &set_adv_data_help,
#endif
    },
    {
        .sc_cmd = "advertise-set-scan-rsp",
        .sc_cmd_func = cmd_set_scan_rsp,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &set_scan_rsp_help,
#endif
    },
    {
        .sc_cmd = "advertise-start",
        .sc_cmd_func = cmd_advertise_start,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &advertise_start_help,
#endif
    },
    {
        .sc_cmd = "advertise-stop",
        .sc_cmd_func = cmd_advertise_stop,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &advertise_stop_help,
#endif
    },
    {
        .sc_cmd = "advertise-remove",
        .sc_cmd_func = cmd_advertise_remove,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &advertise_remove_help,
#endif
    },
#else
    {
        .sc_cmd = "advertise",
        .sc_cmd_func = cmd_advertise,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &advertise_help,
#endif
    },
#endif
    {
        .sc_cmd = "connect",
        .sc_cmd_func = cmd_connect,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &connect_help,
#endif
    },
    {
        .sc_cmd = "disconnect",
        .sc_cmd_func = cmd_disconnect,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &disconnect_help,
#endif
    },
    {
        .sc_cmd = "show-addr",
        .sc_cmd_func = cmd_show_addr,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_show_addr_help,
#endif
    },
    {
        .sc_cmd = "show-conn",
        .sc_cmd_func = cmd_show_conn,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_show_conn_help,
#endif
    },
    {
        .sc_cmd = "set-scan-opts",
        .sc_cmd_func = cmd_set_scan_opts,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &set_scan_opts_help,
#endif
    },
    {
        .sc_cmd = "scan",
        .sc_cmd_func = cmd_scan,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &scan_help,
#endif
    },
    {
        .sc_cmd = "set",
        .sc_cmd_func = cmd_set,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &set_help,
#endif
    },
	{
		.sc_cmd = "send-notify",
		.sc_cmd_func = ble_sent_test,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &send_notify_help,
#endif
	},

#if !(MYNEWT_VAL_BLE_EXT_ADV)
    {
        .sc_cmd = "set-adv-data",
        .sc_cmd_func = cmd_set_adv_data,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &set_adv_data_help,
#endif
    },
    {
        .sc_cmd = "set-scan-rsp",
        .sc_cmd_func = cmd_set_scan_rsp,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &set_scan_rsp_help,
#endif
    },
#endif
    {
        .sc_cmd = "set-priv-mode",
        .sc_cmd_func = cmd_set_priv_mode,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &set_priv_mode_help,
#endif
    },
    {
        .sc_cmd = "white-list",
        .sc_cmd_func = cmd_white_list,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &white_list_help,
#endif
    },
	{
		.sc_cmd = "white-list-add",
		.sc_cmd_func = cmd_white_list_add,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &wl_add_help,
#endif
	},
	{
		.sc_cmd = "white-list-clr",
		.sc_cmd_func = cmd_white_list_clr,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &wl_clr_help,
#endif
	},
	{
		.sc_cmd = "white-list-test",
		.sc_cmd_func = cmd_white_list_add_test,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &wl_add_test_help,
#endif
	},
	{
		.sc_cmd = "pvcy-add",
		.sc_cmd_func = cmd_pvcy_add,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &pvcy_add_help,
#endif
	},
	{
		.sc_cmd = "pvcy-rmv",
		.sc_cmd_func = cmd_pvcy_rmv,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &pvcy_rmv_help,
#endif
	},
	{
		.sc_cmd = "pvcy-test",
		.sc_cmd_func = cmd_pvcy_add_test,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &pvcy_test_help,
#endif
	},
    {
        .sc_cmd = "conn-rssi",
        .sc_cmd_func = cmd_conn_rssi,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &conn_rssi_help,
#endif
    },
    {
        .sc_cmd = "conn-update-params",
        .sc_cmd_func = cmd_conn_update_params,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &conn_update_params_help,
#endif
    },
    {
        .sc_cmd = "conn-datalen",
        .sc_cmd_func = cmd_conn_datalen,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &conn_datalen_help,
#endif
    },
    {
        .sc_cmd = "gatt-discover-characteristic",
        .sc_cmd_func = cmd_gatt_discover_characteristic,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_discover_characteristic_help,
#endif
    },
    {
        .sc_cmd = "gatt-discover-descriptor",
        .sc_cmd_func = cmd_gatt_discover_descriptor,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_discover_descriptor_help,
#endif
    },
    {
        .sc_cmd = "gatt-discover-service",
        .sc_cmd_func = cmd_gatt_discover_service,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_discover_service_help,
#endif
    },
    {
        .sc_cmd = "gatt-discover-full",
        .sc_cmd_func = cmd_gatt_discover_full,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_discover_full_help,
#endif
    },
    {
        .sc_cmd = "gatt-find-included-services",
        .sc_cmd_func = cmd_gatt_find_included_services,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_find_included_services_help,
#endif
    },
    {
        .sc_cmd = "gatt-exchange-mtu",
        .sc_cmd_func = cmd_gatt_exchange_mtu,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_exchange_mtu_help,
#endif
    },
    {
        .sc_cmd = "gatt-read",
        .sc_cmd_func = cmd_gatt_read,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_read_help,
#endif
    },
    {
        .sc_cmd = "gatt-notify",
        .sc_cmd_func = cmd_gatt_notify,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_notify_help,
#endif
    },
    {
        .sc_cmd = "gatt-service-changed",
        .sc_cmd_func = cmd_gatt_service_changed,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_service_changed_help,
#endif
    },
    {
        .sc_cmd = "gatt-service-visibility",
        .sc_cmd_func = cmd_gatt_service_visibility,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_service_visibility_help,
#endif
    },
    {
        .sc_cmd = "gatt-show",
        .sc_cmd_func = cmd_gatt_show,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_show_help,
#endif
    },
    {
        .sc_cmd = "gatt-show-local",
        .sc_cmd_func = cmd_gatt_show_local,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_show_local_help,
#endif
    },
    {
        .sc_cmd = "gatt-write",
        .sc_cmd_func = cmd_gatt_write,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_write_help,
#endif
    },
    
	{
		.sc_cmd = "gatt-thou",
		.sc_cmd_func = cmd_gatt_thoughput,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &gatt_thou_help,
#endif
	},

    {
        .sc_cmd = "gatt-exe-write",
        .sc_cmd_func = cmd_gatt_exe_write,//used when flags set 0.
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &gatt_write_help,
#endif
    },
#if (MYNEWT_VAL_BLE_L2CAP_COC_MAX_NUM)
	{
		.sc_cmd = "l2cap-mtu",
		.sc_cmd_func = cmd_l2cap_mtu,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &l2cap_mtu_help,
#endif
	},
    {
        .sc_cmd = "l2cap-update",
        .sc_cmd_func = cmd_l2cap_update,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &l2cap_update_help,
#endif
    },
    {
        .sc_cmd = "l2cap-create-server",
        .sc_cmd_func = cmd_l2cap_create_server,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &l2cap_create_server_help,
#endif
    },
    {
        .sc_cmd = "l2cap-connect",
        .sc_cmd_func = cmd_l2cap_connect,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &l2cap_connect_help,
#endif
    },
    {
        .sc_cmd = "l2cap-disconnect",
        .sc_cmd_func = cmd_l2cap_disconnect,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &l2cap_disconnect_help,
#endif
    },
    {
        .sc_cmd = "l2cap-send",
        .sc_cmd_func = cmd_l2cap_send,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &l2cap_send_help,
#endif
    },
	{
		.sc_cmd = "l2cap-send-inc",
		.sc_cmd_func = cmd_l2cap_send_inc,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &l2cap_send_help,
#endif
	},
    {
        .sc_cmd = "l2cap-show-coc",
        .sc_cmd_func = cmd_l2cap_show_coc,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &l2cap_show_coc_help,
#endif
    },
#endif
    {
        .sc_cmd = "keystore-add",
        .sc_cmd_func = cmd_keystore_add,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &keystore_add_help,
#endif
    },
    {
        .sc_cmd = "keystore-del",
        .sc_cmd_func = cmd_keystore_del,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &keystore_del_help,
#endif
    },
    {
        .sc_cmd = "keystore-show",
        .sc_cmd_func = cmd_keystore_show,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &keystore_show_help,
#endif
    },
    {
        .sc_cmd = "auth-passkey",
        .sc_cmd_func = cmd_auth_passkey,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &auth_passkey_help,
#endif
    },
    {
        .sc_cmd = "security-pair",
        .sc_cmd_func = cmd_security_pair,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &security_pair_help,
#endif
    },
    {
        .sc_cmd = "security-unpair",
        .sc_cmd_func = cmd_security_unpair,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &security_unpair_help,
#endif
    },
    {
        .sc_cmd = "security-start",
        .sc_cmd_func = cmd_security_start,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &security_start_help,
#endif
    },
    {
        .sc_cmd = "security-encryption",
        .sc_cmd_func = cmd_security_encryption,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &security_encryption_help,
#endif
    },
    {
        .sc_cmd = "security-set-data",
        .sc_cmd_func = cmd_security_set_data,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &security_set_data_help,
#endif
    },
    {
        .sc_cmd = "test-tx",
        .sc_cmd_func = cmd_test_tx,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &test_tx_help,
#endif
    },  
	 
    {
        .sc_cmd = "phy-set",
        .sc_cmd_func = cmd_phy_set,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &phy_set_help,
#endif
    },
    {
        .sc_cmd = "phy-set-default",
        .sc_cmd_func = cmd_phy_set_default,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &phy_set_default_help,
#endif
    },
    {
        .sc_cmd = "phy-read",
        .sc_cmd_func = cmd_phy_read,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &phy_read_help,
#endif
    },   
	
    {
        .sc_cmd = "host-enable",
        .sc_cmd_func = cmd_host_enable,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &host_enable_help,
#endif
    },
    {
        .sc_cmd = "host-disable",
        .sc_cmd_func = cmd_host_disable,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &host_disable_help,
#endif
    },
    {
        .sc_cmd = "rpa-timeout",
        .sc_cmd_func = rpa_timeout,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &rpa_timeout_help,
#endif
    },
    {
        .sc_cmd = "hciadv-data",
        .sc_cmd_func = ble_hci_le_set_adv_data,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &ble_hci_le_set_adv_data_help ,
#endif
    },
    {
        .sc_cmd = "smdata-inject",
        .sc_cmd_func = smp_data_inject,
#if (MYNEWT_VAL_SHELL_CMD_HELP)

        .help = &smp_data_inject_help,
#endif
    },
    
	{
		.sc_cmd = "dtm-tx",
		.sc_cmd_func = cmd_dtm_tx,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &dtm_tx_help,
#endif
	},
	
	{
		.sc_cmd = "dtm-rx",
		.sc_cmd_func = cmd_dtm_rx,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &dtm_rx_help,
#endif
	},

	{
		.sc_cmd = "pwr-set",
		.sc_cmd_func = cmd_default_power_set,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &pwr_set_help,
#endif
	},
	
	{
		.sc_cmd = "tone-tx",
		.sc_cmd_func = cmd_tone_tx,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &tone_tx_help,
#endif
	},

	{
		.sc_cmd = "srrc-set",
		.sc_cmd_func = cmd_srrc_set,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &srrc_set_help,
#endif
	},

	{
		.sc_cmd = "set-scan-name",
		.sc_cmd_func = cmd_scan_name_set,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &name_set_help,
#endif
	},

    {
		.sc_cmd = "set-afh",
		.sc_cmd_func = cmd_afh_set,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &afh_set_help,
#endif
	},

	{
		.sc_cmd = "ce-test",
		.sc_cmd_func = cmd_ble_ce_test,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &ble_ce_set_help,
#endif
	},

//-----------------------------------------------------------------
//add for ble rf-phy test
	{
		.sc_cmd = "reset",
		.sc_cmd_func = cmd_reset,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		.help = &reset_help,
#endif
	},
//-----------------------------------------------------------------
    {
        .sc_cmd = "help",
        .sc_cmd_func = cmd_bt_shell_help,
#if (MYNEWT_VAL_SHELL_CMD_HELP)
        .help = &host_disable_help,
#endif
    },
  //  { NULL, NULL, NULL },
};


static int cmd_bt_shell_help(int argc, char **argv){
	int cmdid=0;

	for(cmdid=0;cmdid<sizeof(btshell_commands)/sizeof(btshell_commands[0]);cmdid++){	
		iot_printf("%s	:",btshell_commands[cmdid].sc_cmd);
#if (MYNEWT_VAL_SHELL_CMD_HELP)
		iot_printf("%s",btshell_commands[cmdid].help->summary);
#endif
	    iot_printf("\n");
	}
	return 0;
}

static void print_command_params(int cmdid)
{
    const struct shell_cmd *shell_cmd = &btshell_commands[cmdid];
    int i;

    if (!(shell_cmd->help && shell_cmd->help->params))
    {
        return;
    }

    for (i = 0; shell_cmd->help->params[i].param; i++)
    {
        iot_printf("%-30s%s\n", shell_cmd->help->params[i].param,
                       shell_cmd->help->params[i].param_desc);
    }
}


#define MAX_ARGV_CNT 16
static  void cli_btshell(char *pLine)
{
	int argc=0;
	char *argv[MAX_ARGV_CNT];
	char * str;
	int cmdid=0;
	const struct shell_cmd *cmd;
	
	while(1){
	    str = cli_get_token(&pLine);
	    if (str[0] == 0)
	    {
	        break;
	    }		
		argv[argc]=str;
		argc++;
		if(argc>=MAX_ARGV_CNT){
			iot_printf("too much args error!!\n");
			return;
		}
	}
	if(argc >=1){
		for(cmdid=0;cmdid<sizeof(btshell_commands)/sizeof(btshell_commands[0]);cmdid++){
			if(cli_string_cmmpare(argv[0], "help")){
				if(argc==2){
					if (cli_string_cmmpare(argv[1], btshell_commands[cmdid].sc_cmd)){
#if (MYNEWT_VAL_SHELL_CMD_HELP)
			        	cmd = &btshell_commands[cmdid];

			            if (!cmd->help || (!cmd->help->summary &&
			                               !cmd->help->usage &&
			                               !cmd->help->params))
			            {
			                iot_printf("(no help available)\n");
			                return;
			            }

			            if (cmd->help->summary)
			            {
			                iot_printf("Summary:\n");
			                iot_printf("%s\n", cmd->help->summary);
			            }

			            if (cmd->help->usage)
			            {
			                iot_printf("Usage:\n");
			                iot_printf("%s\n", cmd->help->usage);
			            }

			            if (cmd->help->params)
			            {
			                iot_printf("Parameters:\n");
			                print_command_params(cmdid);
			            }
						return;
#endif
					}
				}else{
					for(cmdid=0;cmdid<sizeof(btshell_commands)/sizeof(btshell_commands[0]);cmdid++){
						iot_printf("%s\n",btshell_commands[cmdid].sc_cmd);
					}
					return;
				}
			}
			else if (cli_string_cmmpare(argv[0], btshell_commands[cmdid].sc_cmd)){
				 btshell_commands[cmdid].sc_cmd_func(argc,argv);
				 break;
			}
		}
		if(cmdid >= sizeof(btshell_commands)/sizeof(btshell_commands[0])){
			iot_printf("cmd not found!!!\n");
		}
	}
}

static struct cli_cmd_struct btShellCommands[] =
{
	{.cmd ="btshell",	.fn = cli_btshell,		.next = (void*)0 },
};

void btshell_cmd_init(void)
{

    cli_add_cmds(&btShellCommands[0],
		sizeof(btShellCommands)/sizeof(btShellCommands[0]));
}

#endif  //MYNEWT_VAL_SHELL_CMD
