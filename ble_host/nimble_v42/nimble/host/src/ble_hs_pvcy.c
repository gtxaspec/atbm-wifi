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

#include "atbm_debug.h"
#include "atbm_os_mem.h"
#include "stats/stats.h"
#include "ble_hs_priv.h"

static uint8_t ble_hs_pvcy_started;
static uint8_t ble_hs_pvcy_irk[16];

/** Use this as a default IRK if none gets set. */
const uint8_t ble_hs_pvcy_default_irk[16] = {
    0xef, 0x8d, 0xe2, 0x16, 0x4f, 0xec, 0x43, 0x0d,
    0xbf, 0x5b, 0xdd, 0x34, 0xc0, 0x53, 0x1e, 0xb8,
};

SRAM_CODE static int
ble_hs_pvcy_set_addr_timeout(uint16_t timeout)
{
    uint8_t buf[BLE_HCI_SET_RESOLV_PRIV_ADDR_TO_LEN];
    int rc;

    rc = ble_hs_hci_cmd_build_set_resolv_priv_addr_timeout(timeout, buf,
                                                           sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_SET_RPA_TMO),
                             buf, sizeof(buf), NULL, 0, NULL);
}

SRAM_CODE static int
ble_hs_pvcy_set_resolve_enabled(int enable)
{
    uint8_t buf[BLE_HCI_SET_ADDR_RESOL_ENA_LEN];
    int rc;

    rc = ble_hs_hci_cmd_build_set_addr_res_en(enable, buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_SET_ADDR_RES_EN),
                           buf, sizeof(buf), NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

SRAM_CODE int
ble_hs_pvcy_remove_entry(uint8_t addr_type, const uint8_t *addr)
{
    uint8_t buf[BLE_HCI_RMV_FROM_RESOLV_LIST_LEN];
    int rc;

    rc = ble_hs_hci_cmd_build_remove_from_resolv_list(addr_type, addr,
                                                      buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_RMV_RESOLV_LIST),
                           buf, sizeof(buf), NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

SRAM_CODE int
ble_hs_pvcy_clear_entries(void)
{
    int rc;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_CLR_RESOLV_LIST),
                           NULL, 0, NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

SRAM_CODE static int
ble_hs_pvcy_add_entry_hci(const uint8_t *addr, uint8_t addr_type,
                          const uint8_t *irk)
{
    struct hci_add_dev_to_resolving_list add;
    uint8_t buf[BLE_HCI_ADD_TO_RESOLV_LIST_LEN];
    ble_addr_t peer_addr;
    int rc;

    add.addr_type = addr_type;
    memcpy(add.addr, addr, 6);
    memcpy(add.local_irk, ble_hs_pvcy_irk, 16);
    memcpy(add.peer_irk, irk, 16);

    rc = ble_hs_hci_cmd_build_add_to_resolv_list(&add, buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_ADD_RESOLV_LIST),
                           buf, sizeof(buf), NULL, 0, NULL);
    if (rc != 0) {
        return rc;
    }


    /* FIXME Controller is BT5.0 and default privacy mode is network which
     * can cause problems for apps which are not aware of it. We need to
     * sort it out somehow. For now we set device mode for all of the peer
     * devices and application should change it to network if needed
     */
    peer_addr.type = addr_type;
    memcpy(peer_addr.val, addr, sizeof peer_addr.val);
    rc = ble_hs_pvcy_set_mode(&peer_addr, BLE_GAP_PRIVATE_MODE_DEVICE);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

SRAM_CODE int
ble_hs_pvcy_add_entry(const uint8_t *addr, uint8_t addr_type,
                      const uint8_t *irk)
{
    int rc;

    STATS_INC(ble_hs_stats, pvcy_add_entry);

    /* No GAP procedures can be active when adding an entry to the resolving
     * list (Vol 2, Part E, 7.8.38).  Stop all GAP procedures and temporarily
     * prevent any new ones from being started.
     */
    ble_gap_preempt();

    /* Try to add the entry now that GAP is halted. */
    rc = ble_hs_pvcy_add_entry_hci(addr, addr_type, irk);

    /* Allow GAP procedures to be started again. */
    ble_gap_preempt_done();

    if (rc != 0) {
        STATS_INC(ble_hs_stats, pvcy_add_entry_fail);
    }

    return rc;
}

SRAM_CODE int
ble_hs_pvcy_ensure_started(void)
{
    int rc;

    if (ble_hs_pvcy_started) {
        return 0;
    }

    /* Set up the periodic change of our RPA. */
    rc = ble_hs_pvcy_set_addr_timeout(MYNEWT_VAL(BLE_RPA_TIMEOUT));
    if (rc != 0) {
        return rc;
    }

    ble_hs_pvcy_started = 1;

    return 0;
}

SRAM_CODE int
ble_hs_pvcy_set_our_irk(const uint8_t *irk)
{
    uint8_t tmp_addr[6];
    uint8_t new_irk[16];
    int rc;

    if (irk != NULL) {
        memcpy(new_irk, irk, 16);
    } else {
        memcpy(new_irk, ble_hs_pvcy_default_irk, 16);
    }

    /* Clear the resolving list if this is a new IRK. */
    if (memcmp(ble_hs_pvcy_irk, new_irk, 16) != 0) {
        memcpy(ble_hs_pvcy_irk, new_irk, 16);

        rc = ble_hs_pvcy_set_resolve_enabled(0);
        if (rc != 0) {
            return rc;
        }

        rc = ble_hs_pvcy_clear_entries();
        if (rc != 0) {
            return rc;
        }

        rc = ble_hs_pvcy_set_resolve_enabled(1);
        if (rc != 0) {
            return rc;
        }

        /*
         * Add local IRK entry with 00:00:00:00:00:00 address. This entry will
         * be used to generate RPA for non-directed advertising if own_addr_type
         * is set to rpa_pub since we use all-zero address as peer addres in
         * such case. Peer IRK should be left all-zero since this is not for an
         * actual peer.
         */
        memset(tmp_addr, 0, 6);
        memset(new_irk, 0, 16);
        rc = ble_hs_pvcy_add_entry(tmp_addr, 0, new_irk);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

SRAM_CODE int
ble_hs_pvcy_our_irk(const uint8_t **out_irk)
{
    /* XXX: Return error if privacy not supported. */

    *out_irk = ble_hs_pvcy_irk;
    return 0;
}

SRAM_CODE int
ble_hs_pvcy_set_mode(const ble_addr_t *addr, uint8_t priv_mode)
{
    uint8_t buf[BLE_HCI_LE_SET_PRIVACY_MODE_LEN];
    int rc;

    rc = ble_hs_hci_cmd_build_le_set_priv_mode(addr->val, addr->type, priv_mode,
                                           buf, sizeof(buf));
    if (rc != 0) {
        return rc;
    }

    return ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                        BLE_HCI_OCF_LE_SET_PRIVACY_MODE),
                             buf, sizeof(buf), NULL, 0, NULL);
}
