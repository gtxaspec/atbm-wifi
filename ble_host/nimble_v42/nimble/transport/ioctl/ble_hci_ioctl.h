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

#ifndef H_BLE_HCI_RAM_
#define H_BLE_HCI_RAM_

#include "nimble/ble_hci_trans.h"

#ifdef __cplusplus
extern "C" {
#endif


#define BLE_HCI_HIF_NONE        0x00
#define BLE_HCI_HIF_CMD         0x01
#define BLE_HCI_HIF_ACL         0x02
#define BLE_HCI_HIF_SCO         0x03
#define BLE_HCI_HIF_EVT         0x04
#define BLE_HCI_HIF_ISO         0x05

#define HCI_ACL_SHARE_SIZE		1538

#ifndef CONFIG_WIFI_BT_COMB
#define HI_MSG_ID_BLE_BASE			0xC00
#define HI_MSG_ID_BLE_EVENT			(HI_MSG_ID_BLE_BASE+0x01)
#define HI_MSG_ID_BLE_ACK			(HI_MSG_ID_BLE_BASE+0x02)
#else

#define HI_MSG_ID_BLE_BIT			BIT(8)
#define HI_MSG_ID_BLE_BASE			(0x800+HI_MSG_ID_BLE_BIT)
#define HI_MSG_ID_BLE_EVENT			(HI_MSG_ID_BLE_BASE+0x03)
#define HI_MSG_ID_BLE_ACK			(HI_MSG_ID_BLE_BASE+0x04)
#endif //CONFIG_WIFI_BT_COMB

struct ble_hci_hif_pkt {
    STAILQ_ENTRY(ble_hci_hif_pkt) next;
    void *data;
    uint8_t type;
};

void ble_hci_ram_init(void);
struct ble_hci_hif_pkt *ble_hci_trans_tx_pkt_get(void);
void ble_hci_trans_copy_data(struct ble_hci_hif_pkt *tx_pkt, uint8_t *output, uint32_t *putLen);
int ble_hci_trans_hs_rx(uint8_t ack, uint8_t *data, uint16_t data_len);
void _ble_hci_trans_copy_data(struct ble_hci_hif_pkt *tx_pkt, uint8_t *output, uint32_t *putLen);
void ble_hci_trans_free_hif_pkt(struct ble_hci_hif_pkt *tx_pkt);

#ifdef __cplusplus
}
#endif

#endif
