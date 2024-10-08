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




#include "host/ble_gap.h"
#include "host/ble_l2cap.h"
#include "btshell.h"
#include "cmd.h"
#include "cmd_l2cap.h"
#include "ble_hs_priv.h"
#include "cli.h"


SRAM_CODE int
cmd_l2cap_mtu(int argc, char **argv)
{
	int rc;
	uint16_t mtu;
	
    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }	
	
    mtu = parse_arg_uint16_dflt("mtu", 100, &rc);	
    if (rc != 0) {
        iot_printf("invalid 'mtu' parameter\n");
        return rc;
    }

	iot_printf("set mtu:%d\n", mtu);
	ble_l2cap_coc_mtu_set(mtu);
	
	return 0;
}

/*****************************************************************************
 * $l2cap-update                                                             *
 *****************************************************************************/
SRAM_CODE int
cmd_l2cap_update(int argc, char **argv)
{
    struct ble_l2cap_sig_update_params params;
    uint16_t conn_handle;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("invalid 'conn' parameter\n");
        return rc;
    }

    params.itvl_min = parse_arg_uint16_dflt("interval_min",
                                            BLE_GAP_INITIAL_CONN_ITVL_MIN,
                                            &rc);
    if (rc != 0) {
        iot_printf("invalid 'interval_min' parameter\n");
        return rc;
    }

    params.itvl_max = parse_arg_uint16_dflt("interval_max",
                                            BLE_GAP_INITIAL_CONN_ITVL_MAX,
                                            &rc);
    if (rc != 0) {
        iot_printf("invalid 'interval_max' parameter\n");
        return rc;
    }

    params.slave_latency = parse_arg_uint16_dflt("latency", 0, &rc);
    if (rc != 0) {
        iot_printf("invalid 'latency' parameter\n");
        return rc;
    }

    params.timeout_multiplier = parse_arg_uint16_dflt("timeout", 0x0100, &rc);
    if (rc != 0) {
        iot_printf("invalid 'timeout' parameter\n");
        return rc;
    }

    rc = btshell_l2cap_update(conn_handle, &params);
    if (rc != 0) {
        iot_printf("error txing l2cap update; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $l2cap-create-server                                                      *
 *****************************************************************************/

SRAM_CODE int
cmd_l2cap_create_server(int argc, char **argv)
{
    uint16_t psm = 0;
    int error;
    int accept_response = 0;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    psm = parse_arg_uint16("psm", &rc);
    if (rc != 0) {
        iot_printf("invalid 'psm' parameter\n");
        return rc;
    }

    error = parse_arg_uint32_dflt("error", 0, &rc);
    if (rc != 0) {
        iot_printf("invalid 'error' parameter\n");
        return rc;
    }

    switch (error) {
    case 1:
        accept_response = BLE_HS_EAUTHEN;
        break;
    case 2:
        accept_response = BLE_HS_EAUTHOR;
        break;
    case 3:
        accept_response = BLE_HS_EENCRYPT_KEY_SZ;
        break;
    }

    rc = btshell_l2cap_create_srv(psm, accept_response);
    if (rc) {
        iot_printf("Server create error: 0x%02x", rc);
    }

    return 0;
}

/*****************************************************************************
 * $l2cap-connect                                                            *
 *****************************************************************************/

SRAM_CODE int
cmd_l2cap_connect(int argc, char **argv)
{
    uint16_t conn = 0;
    uint16_t psm = 0;
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

    psm = parse_arg_uint16("psm", &rc);
    if (rc != 0) {
        iot_printf("invalid 'psm' parameter\n");
        return rc;
    }

	rc = btshell_l2cap_connect(conn, psm);
    if (rc != 0) {
        iot_printf("btshell_l2cap_connect err:%d\n", rc);
    }
	
    return rc;
}

/*****************************************************************************
 * $l2cap-disconnect                                                         *
 *****************************************************************************/

SRAM_CODE int
cmd_l2cap_disconnect(int argc, char **argv)
{
    uint16_t conn;
    uint16_t idx;
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

    idx = parse_arg_uint16("idx", &rc);
    if (rc != 0) {
        iot_printf("invalid 'idx' parameter\n");
        return rc;
    }

    return btshell_l2cap_disconnect(conn, idx);
}

/*****************************************************************************
 * $l2cap-send                                                               *
 *****************************************************************************/

SRAM_CODE int
cmd_l2cap_send(int argc, char **argv)
{
    uint16_t conn;
    uint16_t idx;
    uint16_t bytes;
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

    idx = parse_arg_uint16("idx", &rc);
    if (rc != 0) {
       iot_printf("invalid 'idx' parameter\n");
       return rc;
    }

    bytes = parse_arg_uint16("bytes", &rc);
    if (rc != 0) {
       iot_printf("invalid 'bytes' parameter\n");
       return rc;
    }
	
	rc = btshell_l2cap_send(conn, idx, bytes);
	if (rc != 0) {
        iot_printf("btshell_l2cap_send err:%d\n", rc);
    }

    return rc;
}

SRAM_CODE int 
cmd_l2cap_send_inc(int argc, char **argv)
{
    uint16_t conn;
    uint16_t idx;
    uint16_t bytes;
    int rc;
	int i;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
       iot_printf("invalid 'conn' parameter\n");
       return rc;
    }

    idx = parse_arg_uint16("idx", &rc);
    if (rc != 0) {
       iot_printf("invalid 'idx' parameter\n");
       return rc;
    }

    bytes = parse_arg_uint16("bytes", &rc);
    if (rc != 0) {
       iot_printf("invalid 'bytes' parameter\n");
       return rc;
    }

	for(i=1; i<=bytes; i++){
		rc = btshell_l2cap_send(conn, idx, i);
		if (rc != 0) {
	        iot_printf("btshell_l2cap_send err:%d\n", rc);
	    }
		ble_npl_time_delay(ble_npl_time_ms_to_ticks32(1000));
	}
	
    return rc;
}

SRAM_CODE int
cmd_l2cap_show_coc(int argc, char **argv)
{
    struct btshell_conn *conn = NULL;
    struct btshell_l2cap_coc *coc;
    int i, j;

    for (i = 0; i < btshell_num_conns; i++) {
        conn = btshell_conns + i;

        if (SLIST_EMPTY(&conn->coc_list)) {
            continue;
        }

        iot_printf("conn_handle: 0x%04x\n", conn->handle);
        j = 0;
        SLIST_FOREACH(coc, &conn->coc_list, next) {
            iot_printf("    idx: %i, chan pointer = %p\n", j++, coc->chan);
        }
    }

    return 0;
}

#endif  //MYNEWT_VAL_SHELL_CMD