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


#include "host/ble_hs_mbuf.h"
#include "host/ble_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "btshell.h"
#include "cmd.h"
#include "cmd_gatt.h"
#include "ble_hs_priv.h"
#include "cli.h"

#define bssnz_t
#define CMD_BUF_SZ      256
static bssnz_t uint8_t cmd_buf[CMD_BUF_SZ];

/*****************************************************************************
 * $gatt-discover                                                            *
 *****************************************************************************/

SRAM_CODE int
cmd_gatt_discover_characteristic(int argc, char **argv)
{
    uint16_t start_handle;
    uint16_t conn_handle;
    uint16_t end_handle;
    ble_uuid_any_t uuid;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rc = cmd_parse_conn_start_end(&conn_handle, &start_handle, &end_handle);
    if (rc != 0) {
        iot_printf("invalid 'conn start end' parameter\n");
        return rc;
    }

    rc = parse_arg_uuid("uuid", &uuid);
    if (rc == 0) {
        rc = btshell_disc_chrs_by_uuid(conn_handle, start_handle, end_handle,
                                       &uuid.u);
    } else if (rc == ENOENT) {
        rc = btshell_disc_all_chrs(conn_handle, start_handle, end_handle);
    } else  {
        iot_printf("invalid 'uuid' parameter\n");
        return rc;
    }
    if (rc != 0) {
        iot_printf("error discovering characteristics; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

SRAM_CODE int
cmd_gatt_discover_descriptor(int argc, char **argv)
{
    uint16_t start_handle;
    uint16_t conn_handle;
    uint16_t end_handle;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rc = cmd_parse_conn_start_end(&conn_handle, &start_handle, &end_handle);
    if (rc != 0) {
        iot_printf("invalid 'conn start end' parameter\n");
        return rc;
    }

    rc = btshell_disc_all_dscs(conn_handle, start_handle, end_handle);
    if (rc != 0) {
        iot_printf("error discovering descriptors; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

SRAM_CODE int
cmd_gatt_discover_service(int argc, char **argv)
{
    ble_uuid_any_t uuid;
    int conn_handle;
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

    rc = parse_arg_uuid("uuid", &uuid);
    if (rc == 0) {
        rc = btshell_disc_svc_by_uuid(conn_handle, &uuid.u);
    } else if (rc == ENOENT) {
        rc = btshell_disc_svcs(conn_handle);
    } else  {
        iot_printf("invalid 'uuid' parameter\n");
        return rc;
    }

    if (rc != 0) {
        iot_printf("error discovering services; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

SRAM_CODE int
cmd_gatt_discover_full(int argc, char **argv)
{
    int conn_handle;
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

    rc = btshell_disc_full(conn_handle);
    if (rc != 0) {
        iot_printf("error discovering all; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $gatt-exchange-mtu                                                        *
 *****************************************************************************/

SRAM_CODE int
cmd_gatt_exchange_mtu(int argc, char **argv)
{
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

    rc = btshell_exchange_mtu(conn_handle);
    if (rc != 0) {
        iot_printf("error exchanging mtu; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $gatt-notify                                                              *
 *****************************************************************************/

SRAM_CODE int
cmd_gatt_notify(int argc, char **argv)
{
    uint16_t attr_handle;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    attr_handle = parse_arg_uint16("attr", &rc);
    if (rc != 0) {
        iot_printf("invalid 'attr' parameter\n");
        return rc;
    }

    btshell_notify(attr_handle);

    return 0;
}

/*****************************************************************************
 * $gatt-read                                                                *
 *****************************************************************************/

#define CMD_READ_MAX_ATTRS  8

SRAM_CODE int
cmd_gatt_read(int argc, char **argv)
{
    static uint16_t attr_handles[CMD_READ_MAX_ATTRS];
    uint16_t conn_handle;
    uint16_t start;
    uint16_t end;
    uint16_t offset;
    ble_uuid_any_t uuid;
    uint8_t num_attr_handles;
    int is_uuid;
    int is_long;
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

    is_long = parse_arg_long("long", &rc);
    if (rc == ENOENT) {
        is_long = 0;
    } else if (rc != 0) {
        iot_printf("invalid 'long' parameter\n");
        return rc;
    }

    for (num_attr_handles = 0;
         num_attr_handles < CMD_READ_MAX_ATTRS;
         num_attr_handles++) {

        attr_handles[num_attr_handles] = parse_arg_uint16("attr", &rc);
        if (rc == ENOENT) {
            break;
        } else if (rc != 0) {
            iot_printf("invalid 'attr' parameter\n");
            return rc;
        }
    }

    rc = parse_arg_uuid("uuid", &uuid);
    if (rc == ENOENT) {
        is_uuid = 0;
    } else if (rc == 0) {
        is_uuid = 1;
    } else {
        iot_printf("invalid 'uuid' parameter\n");
        return rc;
    }

    start = parse_arg_uint16("start", &rc);
    if (rc == ENOENT) {
        start = 0;
    } else if (rc != 0) {
        iot_printf("invalid 'start' parameter\n");
        return rc;
    }

    end = parse_arg_uint16("end", &rc);
    if (rc == ENOENT) {
        end = 0;
    } else if (rc != 0) {
        iot_printf("invalid 'end' parameter\n");
        return rc;
    }

    offset = parse_arg_uint16("offset", &rc);
    if (rc == ENOENT) {
        offset = 0;
    } else if (rc != 0) {
        iot_printf("invalid 'offset' parameter\n");
        return rc;
    }

    if (num_attr_handles == 1) {
        if (is_long) {
            rc = btshell_read_long(conn_handle, attr_handles[0], offset);
        } else {
            rc = btshell_read(conn_handle, attr_handles[0]);
        }
    } else if (num_attr_handles > 1) {
        rc = btshell_read_mult(conn_handle, attr_handles, num_attr_handles);
    } else if (is_uuid) {
        if (start == 0 || end == 0) {
            rc = EINVAL;
        } else {
            rc = btshell_read_by_uuid(conn_handle, start, end, &uuid.u);
        }
    } else {
        rc = EINVAL;
    }

    if (rc != 0) {
        iot_printf("error reading characteristic; rc=%d\n", rc);
        return rc;
    }

    return 0;
}


/*****************************************************************************
 * $gatt-service-changed                                                     *
 *****************************************************************************/

SRAM_CODE int
cmd_gatt_service_changed(int argc, char **argv)
{
    uint16_t start;
    uint16_t end;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    start = parse_arg_uint16("start", &rc);
    if (rc != 0) {
        iot_printf("invalid 'start' parameter\n");
        return rc;
    }

    end = parse_arg_uint16("end", &rc);
    if (rc != 0) {
        iot_printf("invalid 'end' parameter\n");
        return rc;
    }

    ble_svc_gatt_changed(start, end);

    return 0;
}

/*****************************************************************************
 * $gatt-service-visibility                                                  *
 *****************************************************************************/

SRAM_CODE int
cmd_gatt_service_visibility(int argc, char **argv)
{
    uint16_t handle;
    bool vis;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    handle = parse_arg_uint16("handle", &rc);
    if (rc != 0) {
        iot_printf("invalid 'handle' parameter\n");
        return rc;
    }

    vis = parse_arg_bool("visibility", &rc);
    if (rc != 0) {
        iot_printf("invalid 'visibility' parameter\n");
        return rc;
    }

    ble_gatts_svc_set_visibility(handle, vis);

    return 0;
}

/*****************************************************************************
 * $gatt-find-included-services                                              *
 *****************************************************************************/

SRAM_CODE int
cmd_gatt_find_included_services(int argc, char **argv)
{
    uint16_t start_handle;
    uint16_t conn_handle;
    uint16_t end_handle;
    int rc;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rc = cmd_parse_conn_start_end(&conn_handle, &start_handle, &end_handle);
    if (rc != 0) {
        iot_printf("invalid 'conn start end' parameter\n");
        return rc;
    }

    rc = btshell_find_inc_svcs(conn_handle, start_handle, end_handle);
    if (rc != 0) {
        iot_printf("error finding included services; rc=%d\n", rc);
        return rc;
    }

    return 0;
}

/*****************************************************************************
 * $gatt-show                                                                *
 *****************************************************************************/

SRAM_CODE int
cmd_gatt_show(int argc, char **argv)
{
    struct btshell_conn *conn;
    struct btshell_svc *svc;
    int i;

    for (i = 0; i < btshell_num_conns; i++) {
        conn = btshell_conns + i;

        iot_printf("CONNECTION: handle=%d\n", conn->handle);

        SLIST_FOREACH(svc, &conn->svcs, next) {
            print_svc(svc);
        }
    }

    return 0;
}

SRAM_CODE int
cmd_gatt_show_local(int argc, char **argv)
{
    gatt_svr_print_svcs();
    return 0;
}

/*****************************************************************************
 * $gatt-write                                                               *
 *****************************************************************************/

SRAM_CODE int
cmd_gatt_write(int argc, char **argv)
{
    struct ble_gatt_attr attrs[(MYNEWT_VAL_BLE_GATT_WRITE_MAX_ATTRS)] = { { 0 } };
    uint16_t attr_handle;
    uint16_t conn_handle;
    uint16_t offset;
    int total_attr_len;
    int num_attrs;
    int attr_len;
    int is_long;
    int no_rsp;
    int rc;
    int i;

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("invalid 'conn' parameter\n");
        return rc;
    }

    no_rsp = parse_arg_bool_dflt("no_rsp", 0, &rc);
    if (rc != 0) {
        iot_printf("invalid 'no_rsp' parameter\n");
        return rc;
    }

    is_long = parse_arg_bool_dflt("long", 0, &rc);
    if (rc != 0) {
        iot_printf("invalid 'long' parameter\n");
        return rc;
    }

    total_attr_len = 0;
    num_attrs = 0;
    while (1) {
        attr_handle = parse_arg_uint16("attr", &rc);
        if (rc == ENOENT) {
            break;
        } else if (rc != 0) {
            rc = -rc;
            iot_printf("invalid 'attr' parameter\n");
            goto done;
        }

        rc = parse_arg_byte_stream("value", sizeof cmd_buf - total_attr_len,
                                   cmd_buf + total_attr_len, &attr_len);
        if (rc == ENOENT) {
            break;
        } else if (rc != 0) {
            iot_printf("invalid 'value' parameter\n");
            goto done;
        }

        offset = parse_arg_uint16("offset", &rc);
        if (rc == ENOENT) {
            offset = 0;
        } else if (rc != 0) {
            iot_printf("invalid 'offset' parameter\n");
            return rc;
        }

        if (num_attrs >= sizeof attrs / sizeof attrs[0]) {
            rc = -EINVAL;
            goto done;
        }

        attrs[num_attrs].handle = attr_handle;
        attrs[num_attrs].offset = offset;
        attrs[num_attrs].om = ble_hs_mbuf_from_flat(cmd_buf + total_attr_len,
                                                    attr_len);
        if (attrs[num_attrs].om == NULL) {
            goto done;
        }

        total_attr_len += attr_len;
        num_attrs++;
    }

    if (no_rsp) {
        if (num_attrs != 1) {
            rc = -EINVAL;
            goto done;
        }
        rc = btshell_write_no_rsp(conn_handle, attrs[0].handle, attrs[0].om);
        attrs[0].om = NULL;
    } else if (is_long) {
        if (num_attrs != 1) {
            rc = -EINVAL;
            goto done;
        }
        rc = btshell_write_long(conn_handle, attrs[0].handle,
                                attrs[0].offset, attrs[0].om);
        attrs[0].om = NULL;
    } else if (num_attrs > 1) {
        rc = btshell_write_reliable(conn_handle, attrs, num_attrs);
    } else if (num_attrs == 1) {
        rc = btshell_write(conn_handle, attrs[0].handle, attrs[0].om);
        attrs[0].om = NULL;
    } else {
        rc = -EINVAL;
    }

done:
    for (i = 0; i < sizeof attrs / sizeof attrs[0]; i++) {
        os_mbuf_free_chain(attrs[i].om);
    }

    if (rc != 0) {
        iot_printf("error writing characteristic; rc=%d\n", rc);
    }

    return rc;
}


SRAM_CODE int
cmd_gatt_thoughput(int argc, char **argv)
{
    struct ble_gatt_attr attrs[(MYNEWT_VAL_BLE_GATT_WRITE_MAX_ATTRS)] = { { 0 } };
    uint16_t attr_handle;
    uint16_t conn_handle;
    uint16_t offset;
    int total_attr_len;
    int num_attrs;
    int attr_len;
    int is_long;
    int no_rsp;
    int rc;
    int i;
	uint8_t data[256] = {0};
	struct os_mbuf *om;
	

    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("invalid 'conn' parameter\n");
        return rc;
    }

	attr_handle = parse_arg_uint16("attr", &rc);
	while(1) {
        om = ble_hs_mbuf_from_flat(data, 240);
		if(om != NULL){
			btshell_write_no_rsp(conn_handle, attr_handle, om);
		}
	}

    return rc;
}




SRAM_CODE int
cmd_gatt_exe_write(int argc, char **argv){
    int rc;
	uint8_t flags;
	uint16_t conn_handle;
	

	rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    conn_handle = parse_arg_uint16("conn", &rc);
    if (rc != 0) {
        iot_printf("invalid 'conn' parameter\n");
        return rc;
    }

	flags = parse_arg_uint16("flags", &rc);
    if (rc != 0) {
        iot_printf("invalid 'flags' parameter\n");
        return rc;
    }

    return ble_att_clt_tx_exec_write(conn_handle,flags);
}
#endif  //MYNEWT_VAL_SHELL_CMD
