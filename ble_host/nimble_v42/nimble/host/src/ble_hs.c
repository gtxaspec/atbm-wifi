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

#include "atbm_hal.h"
#include "sysinit/sysinit.h"
#include "syscfg/syscfg.h"
#include "stats/stats.h"
#include "nimble/ble_hci_trans.h"
#include "ble_hs_priv.h"
#include "ble_monitor_priv.h"
#include "nimble/nimble_npl.h"
#ifndef MYNEWT
#include "nimble/nimble_port.h"
#endif

#define BLE_HS_HCI_EVT_COUNT                    \
    (MYNEWT_VAL(BLE_HCI_EVT_HI_BUF_COUNT) +     \
     MYNEWT_VAL(BLE_HCI_EVT_LO_BUF_COUNT))

static void ble_hs_event_rx_hci_ev(struct ble_npl_event *ev);
static void ble_hs_event_tx_notify(struct ble_npl_event *ev);
static void ble_hs_event_reset(struct ble_npl_event *ev);
static void ble_hs_event_start_stage1(struct ble_npl_event *ev);
static void ble_hs_event_start_stage2(struct ble_npl_event *ev);
static void ble_hs_timer_sched(int32_t ticks_from_now);

struct os_mempool ble_hs_hci_ev_pool;
static os_membuf_t ble_hs_hci_os_event_buf[
    OS_MEMPOOL_SIZE(BLE_HS_HCI_EVT_COUNT, sizeof (struct ble_npl_event))
];

/** OS event - triggers tx of pending notifications and indications. */
static struct ble_npl_event ble_hs_ev_tx_notifications;

/** OS event - triggers a full reset. */
static struct ble_npl_event ble_hs_ev_reset;

static struct ble_npl_event ble_hs_ev_start_stage1;
static struct ble_npl_event ble_hs_ev_start_stage2;

uint8_t ble_hs_sync_state;
uint8_t ble_hs_enabled_state;
static int ble_hs_reset_reason;

#define BLE_HS_SYNC_RETRY_TIMEOUT_MS    100 /* ms */

static void *ble_hs_parent_task;

/**
 * Handles unresponsive timeouts and periodic retries in case of resource
 * shortage.
 */
static struct ble_npl_callout ble_hs_timer;

/* Shared queue that the host uses for work items. */
static struct ble_npl_eventq *ble_hs_evq;

static struct ble_mqueue ble_hs_rx_q;

static struct ble_npl_mutex ble_hs_mutex;

/** These values keep track of required ATT and GATT resources counts.  They
 * increase as services are added, and are read when the ATT server and GATT
 * server are started.
 */
uint16_t ble_hs_max_attrs;
uint16_t ble_hs_max_services;
uint16_t ble_hs_max_client_configs;

#if MYNEWT_VAL(BLE_HS_DEBUG)
static uint8_t ble_hs_dbg_mutex_locked;
#endif

STATS_SECT_DECL(ble_hs_stats) ble_hs_stats;
STATS_NAME_START(ble_hs_stats)
    STATS_NAME(ble_hs_stats, conn_create)
    STATS_NAME(ble_hs_stats, conn_delete)
    STATS_NAME(ble_hs_stats, hci_cmd)
    STATS_NAME(ble_hs_stats, hci_event)
    STATS_NAME(ble_hs_stats, hci_invalid_ack)
    STATS_NAME(ble_hs_stats, hci_unknown_event)
    STATS_NAME(ble_hs_stats, hci_timeout)
    STATS_NAME(ble_hs_stats, reset)
    STATS_NAME(ble_hs_stats, sync)
    STATS_NAME(ble_hs_stats, pvcy_add_entry)
    STATS_NAME(ble_hs_stats, pvcy_add_entry_fail)
STATS_NAME_END(ble_hs_stats)

struct ble_npl_eventq *
ble_hs_evq_get(void)
{
    return ble_hs_evq;
}

void
ble_hs_evq_set(struct ble_npl_eventq *evq)
{
    ble_hs_evq = evq;
}

#if MYNEWT_VAL(BLE_HS_DEBUG)
SRAM_CODE int
ble_hs_locked_by_cur_task(void)
{
#if MYNEWT
    struct os_task *owner;

    if (!ble_npl_os_started()) {
        return ble_hs_dbg_mutex_locked;
    }

    owner = ble_hs_mutex.mu.mu_owner;
    return owner != NULL && owner == os_sched_get_current_task();
#else
    return 1;
#endif
}
#endif

/**
 * Indicates whether the host's parent task is currently running.
 */
SRAM_CODE int
ble_hs_is_parent_task(void)
{
    return !ble_npl_os_started() ||
           ble_npl_get_current_task_id() == ble_hs_parent_task;
}

/**
 * Locks the BLE host mutex.  Nested locks allowed.
 */
SRAM_CODE void
ble_hs_lock_nested(void)
{
    int rc;

#if MYNEWT_VAL(BLE_HS_DEBUG)
    if (!ble_npl_os_started()) {
        ble_hs_dbg_mutex_locked = 1;
        return;
    }
#endif

    rc = ble_npl_mutex_pend(&ble_hs_mutex, BLE_NPL_TIME_FOREVER);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

/**
 * Unlocks the BLE host mutex.  Nested locks allowed.
 */
SRAM_CODE void
ble_hs_unlock_nested(void)
{
    int rc;

#if MYNEWT_VAL(BLE_HS_DEBUG)
    if (!ble_npl_os_started()) {
        ble_hs_dbg_mutex_locked = 0;
        return;
    }
#endif

    rc = ble_npl_mutex_release(&ble_hs_mutex);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0 || rc == OS_NOT_STARTED);
}

/**
 * Locks the BLE host mutex.  Nested locks not allowed.
 */
SRAM_CODE void
ble_hs_lock(void)
{
    BLE_HS_DBG_ASSERT(!ble_hs_locked_by_cur_task());
#if MYNEWT_VAL(BLE_HS_DEBUG)
    if (!ble_npl_os_started()) {
        BLE_HS_DBG_ASSERT(!ble_hs_dbg_mutex_locked);
    }
#endif

    ble_hs_lock_nested();
}

/**
 * Unlocks the BLE host mutex.  Nested locks not allowed.
 */
SRAM_CODE void
ble_hs_unlock(void)
{
#if MYNEWT_VAL(BLE_HS_DEBUG)
    if (!ble_npl_os_started()) {
        BLE_HS_DBG_ASSERT(ble_hs_dbg_mutex_locked);
    }
#endif

    ble_hs_unlock_nested();
}

SRAM_CODE void
ble_hs_process_rx_data_queue(void)
{
    struct os_mbuf *om;

    while ((om = ble_mqueue_get(&ble_hs_rx_q)) != NULL) {
#if BLE_MONITOR
        ble_monitor_send_om(BLE_MONITOR_OPCODE_ACL_RX_PKT, om);
#endif

        ble_hs_hci_evt_acl_process(om);
    }
}

SRAM_CODE static int
ble_hs_wakeup_tx_conn(struct ble_hs_conn *conn)
{
    struct os_mbuf_pkthdr *omp;
    struct os_mbuf *om;
    int rc;

    while ((omp = STAILQ_FIRST(&conn->bhc_tx_q)) != NULL) {
        STAILQ_REMOVE_HEAD(&conn->bhc_tx_q, omp_next);

        om = OS_MBUF_PKTHDR_TO_MBUF(omp);
        rc = ble_hs_hci_acl_tx_now(conn, &om);
        if (rc == BLE_HS_EAGAIN) {
            /* Controller is at capacity.  This packet will be the first to
             * get transmitted next time around.
             */
            STAILQ_INSERT_HEAD(&conn->bhc_tx_q, OS_MBUF_PKTHDR(om), omp_next);
            return BLE_HS_EAGAIN;
        }
    }

    return 0;
}

/**
 * Schedules the transmission of all queued ACL data packets to the controller.
 */
SRAM_CODE void
ble_hs_wakeup_tx(void)
{
    struct ble_hs_conn *conn;
    int rc;

    ble_hs_lock();

    /* If there is a connection with a partially transmitted packet, it has to
     * be serviced first.  The controller is waiting for the remainder so it
     * can reassemble it.
     */
    for (conn = ble_hs_conn_first();
         conn != NULL;
         conn = SLIST_NEXT(conn, bhc_next)) {

        if (conn->bhc_flags & BLE_HS_CONN_F_TX_FRAG) {
            rc = ble_hs_wakeup_tx_conn(conn);
            if (rc != 0) {
                goto done;
            }
            break;
        }
    }

    /* For each connection, transmit queued packets until there are no more
     * packets to send or the controller's buffers are exhausted.
     */
    for (conn = ble_hs_conn_first();
         conn != NULL;
         conn = SLIST_NEXT(conn, bhc_next)) {

        rc = ble_hs_wakeup_tx_conn(conn);
        if (rc != 0) {
            goto done;
        }
    }

done:
    ble_hs_unlock();
}

SRAM_CODE static void
ble_hs_clear_rx_queue(void)
{
    struct os_mbuf *om;

    while ((om = ble_mqueue_get(&ble_hs_rx_q)) != NULL) {
        os_mbuf_free_chain(om);
    }
}

SRAM_CODE int
ble_hs_is_enabled(void)
{
    return ble_hs_enabled_state == BLE_HS_ENABLED_STATE_ON;
}

SRAM_CODE int
ble_hs_synced(void)
{
    return ble_hs_sync_state == BLE_HS_SYNC_STATE_GOOD;
}

SRAM_CODE static int
ble_hs_sync(void)
{
    ble_npl_time_t retry_tmo_ticks;
    int rc;

    /* Set the sync state to "bringup."  This allows the parent task to send
     * the startup sequence to the controller.  No other tasks are allowed to
     * send any commands.
     */
    ble_hs_sync_state = BLE_HS_SYNC_STATE_BRINGUP;
	iot_printf("%s:%d\n",__FUNCTION__,__LINE__);
    rc = ble_hs_startup_go();
    if (rc == 0) {
        ble_hs_sync_state = BLE_HS_SYNC_STATE_GOOD;
    } else {
        ble_hs_sync_state = BLE_HS_SYNC_STATE_BAD;
    }

    retry_tmo_ticks = ble_npl_time_ms_to_ticks32(BLE_HS_SYNC_RETRY_TIMEOUT_MS);
    ble_hs_timer_sched(retry_tmo_ticks);

    if (rc == 0) {
        rc = ble_hs_misc_restore_irks();
        if (rc != 0) {
            BLE_HS_LOG(INFO, "Failed to restore IRKs from store; status=%d",
                       rc);
        }

        if (ble_hs_cfg.sync_cb != NULL) {
            ble_hs_cfg.sync_cb();
        }

        STATS_INC(ble_hs_stats, sync);
    }

    return rc;
}

SRAM_CODE static int
ble_hs_reset(void)
{
    uint16_t conn_handle;
    int rc;

    STATS_INC(ble_hs_stats, reset);

    ble_hs_sync_state = 0;

    /* Reset transport.  Assume success; there is nothing we can do in case of
     * failure.  If the transport failed to reset, the host will reset itself
     * again when it fails to sync with the controller.
     */
    (void)ble_hci_trans_reset();

    ble_hs_clear_rx_queue();

    while (1) {
        conn_handle = ble_hs_atomic_first_conn_handle();
        if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            break;
        }

        ble_gap_conn_broken(conn_handle, ble_hs_reset_reason);
    }

    if (ble_gap_adv_active_instance(0)) {
        /* Indicate to application that advertising has stopped. */
        ble_gap_adv_finished(0, ble_hs_reset_reason, 0, 0);
    }


    /* Clear configured addresses. */
    ble_hs_id_reset();

    if (ble_hs_cfg.reset_cb != NULL && ble_hs_reset_reason != 0) {
        ble_hs_cfg.reset_cb(ble_hs_reset_reason);
    }
    ble_hs_reset_reason = 0;
	iot_printf("%s:%d\n",__FUNCTION__,__LINE__);

    rc = ble_hs_sync();
    return rc;
}

/**
 * Called when the host timer expires.  Handles unresponsive timeouts and
 * periodic retries in case of resource shortage.
 */
SRAM_CODE static void
ble_hs_timer_exp(struct ble_npl_event *ev)
{
    int32_t ticks_until_next;

    switch (ble_hs_sync_state) {
    case BLE_HS_SYNC_STATE_GOOD:
        ticks_until_next = ble_gattc_timer();
        ble_hs_timer_sched(ticks_until_next);

        ticks_until_next = ble_gap_timer();
        ble_hs_timer_sched(ticks_until_next);

        ticks_until_next = ble_l2cap_sig_timer();
        ble_hs_timer_sched(ticks_until_next);

        ticks_until_next = ble_sm_timer();
        ble_hs_timer_sched(ticks_until_next);

        ticks_until_next = ble_hs_conn_timer();
        ble_hs_timer_sched(ticks_until_next);
        break;

    case BLE_HS_SYNC_STATE_BAD:
        ble_hs_reset();
        break;

    case BLE_HS_SYNC_STATE_BRINGUP:
    default:
        /* The timer should not be set in this state. */
        assert(0);
        break;
    }

}

SRAM_CODE static void
ble_hs_timer_reset(uint32_t ticks)
{
    int rc;

    if (!ble_hs_is_enabled()) {
        ble_npl_callout_stop(&ble_hs_timer);
    } else {
        rc = ble_npl_callout_reset(&ble_hs_timer, ticks);
        BLE_HS_DBG_ASSERT_EVAL(rc == 0);
    }
}

SRAM_CODE static void
ble_hs_timer_sched(int32_t ticks_from_now)
{
    ble_npl_time_t abs_time;

    if (ticks_from_now == BLE_HS_FOREVER) {
        return;
    }

    /* Reset timer if it is not currently scheduled or if the specified time is
     * sooner than the previous expiration time.
     */
    abs_time = ble_npl_time_get() + ticks_from_now;
    if (!ble_npl_callout_is_active(&ble_hs_timer) ||
            ((ble_npl_stime_t)(abs_time -
                               ble_npl_callout_get_ticks(&ble_hs_timer))) < 0) {
  
        ble_hs_timer_reset(ticks_from_now);
    }
}

SRAM_CODE void
ble_hs_timer_resched(void)
{
    /* Reschedule the timer to run immediately.  The timer callback will query
     * each module for an up-to-date expiration time.
     */
    ble_hs_timer_reset(0);
}

SRAM_CODE static void
ble_hs_sched_start_stage2(void)
{
    ble_npl_eventq_put((struct ble_npl_eventq *)ble_hs_evq_get(),
                       &ble_hs_ev_start_stage2);
}

SRAM_CODE void
ble_hs_sched_start(void)
{
#ifdef MYNEWT
    ble_npl_eventq_put((struct ble_npl_eventq *)os_eventq_dflt_get(),
                       &ble_hs_ev_start_stage1);
#else
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &ble_hs_ev_start_stage1);
#endif
}

SRAM_CODE static void
ble_hs_event_rx_hci_ev(struct ble_npl_event *ev)
{
    uint8_t *hci_evt;
    int rc;

    hci_evt = ble_npl_event_get_arg(ev);
    rc = os_memblock_put(&ble_hs_hci_ev_pool, ev);
    BLE_HS_DBG_ASSERT_EVAL(rc == 0);

#if BLE_MONITOR
    ble_monitor_send(BLE_MONITOR_OPCODE_EVENT_PKT, hci_evt,
                     hci_evt[1] + BLE_HCI_EVENT_HDR_LEN);
#endif

    ble_hs_hci_evt_process(hci_evt);
}

SRAM_CODE static void
ble_hs_event_tx_notify(struct ble_npl_event *ev)
{
    ble_gatts_tx_notifications();
}

SRAM_CODE static void
ble_hs_event_rx_data(struct ble_npl_event *ev)
{
    ble_hs_process_rx_data_queue();
}

SRAM_CODE static void
ble_hs_event_reset(struct ble_npl_event *ev)
{
    ble_hs_reset();
}

/**
 * Implements the first half of the start process.  This just enqueues another
 * event on the host parent task's event queue.
 *
 * Starting is done in two stages to allow the application time to configure
 * the event queue to use after system initialization but before the host
 * starts.
 */
SRAM_CODE static void
ble_hs_event_start_stage1(struct ble_npl_event *ev)
{
    ble_hs_sched_start_stage2();
}

/**
 * Implements the second half of the start process.  This actually starts the
 * host.
 *
 * Starting is done in two stages to allow the application time to configure
 * the event queue to use after system initialization but before the host
 * starts.
 */
SRAM_CODE static void
ble_hs_event_start_stage2(struct ble_npl_event *ev)
{
    int rc;
    rc = ble_hs_start();
    assert(rc == 0);
}

SRAM_CODE void
ble_hs_enqueue_hci_event(uint8_t *hci_evt)
{
    struct ble_npl_event *ev;

    ev = os_memblock_get(&ble_hs_hci_ev_pool);
    if (ev == NULL) {
        ble_hci_trans_buf_free(hci_evt);
    } else {
        ble_npl_event_init(ev, ble_hs_event_rx_hci_ev, hci_evt);
        ble_npl_eventq_put(ble_hs_evq, ev);
    }
}

/**
 * Schedules for all pending notifications and indications to be sent in the
 * host parent task.
 */
SRAM_CODE void
ble_hs_notifications_sched(void)
{
#if !MYNEWT_VAL(BLE_HS_REQUIRE_OS)
    if (!ble_npl_os_started()) {
        ble_gatts_tx_notifications();
        return;
    }
#endif

    ble_npl_eventq_put(ble_hs_evq, &ble_hs_ev_tx_notifications);
}

SRAM_CODE void
ble_hs_sched_reset(int reason)
{
    BLE_HS_DBG_ASSERT(ble_hs_reset_reason == 0);

    ble_hs_reset_reason = reason;
    ble_npl_eventq_put(ble_hs_evq, &ble_hs_ev_reset);
}

SRAM_CODE void
ble_hs_hw_error(uint8_t hw_code)
{
    ble_hs_sched_reset(BLE_HS_HW_ERR(hw_code));
}

SRAM_CODE int
ble_hs_start(void)
{
    int rc;

    ble_hs_lock();
    switch (ble_hs_enabled_state) {
    case BLE_HS_ENABLED_STATE_ON:
        rc = BLE_HS_EALREADY;
        break;

    case BLE_HS_ENABLED_STATE_STOPPING:
        rc = BLE_HS_EBUSY;
        break;

    case BLE_HS_ENABLED_STATE_OFF:
        ble_hs_enabled_state = BLE_HS_ENABLED_STATE_ON;
        rc = 0;
        break;

    default:
        assert(0);
        rc = BLE_HS_EUNKNOWN;
        break;
    }
    ble_hs_unlock();

    if (rc != 0) {
        return rc;
    }

    ble_hs_parent_task = ble_npl_get_current_task_id();

    /* Stop the timer just in case the host was already running (e.g., unit
     * tests).
     */
    ble_npl_callout_stop(&ble_hs_timer);
    ble_npl_callout_init(&ble_hs_timer, ble_hs_evq, ble_hs_timer_exp, NULL);

    rc = ble_gatts_start();
    if (rc != 0) {
        return rc;
    }

    ble_hs_sync();

    return 0;
}

/**
 * Called when a data packet is received from the controller.  This function
 * consumes the supplied mbuf, regardless of the outcome.
 *
 * @param om                    The incoming data packet, beginning with the
 *                                  HCI ACL data header.
 *
 * @return                      0 on success; nonzero on failure.
 */
SRAM_CODE static int
ble_hs_rx_data(struct os_mbuf *om, void *arg)
{
    int rc;

    /* If flow control is enabled, mark this packet with its corresponding
     * connection handle.
     */
    ble_hs_flow_fill_acl_usrhdr(om);

    rc = ble_mqueue_put(&ble_hs_rx_q, ble_hs_evq, om);
    if (rc != 0) {
        os_mbuf_free_chain(om);
        return BLE_HS_EOS;
    }

    return 0;
}

/**
 * Enqueues an ACL data packet for transmission.  This function consumes the
 * supplied mbuf, regardless of the outcome.
 *
 * @param om                    The outgoing data packet, beginning with the
 *                                  HCI ACL data header.
 *
 * @return                      0 on success; nonzero on failure.
 */
SRAM_CODE int
ble_hs_tx_data(struct os_mbuf *om)
{
#if BLE_MONITOR
    ble_monitor_send_om(BLE_MONITOR_OPCODE_ACL_TX_PKT, om);
#endif

    ble_hci_trans_hs_acl_tx(om);
    return 0;
}

SRAM_CODE int ble_hs_rx_test(uint8_t channel, uint8_t phy_mode,uint8_t Modulation_Index)
{

	uint8_t rc;
	uint8_t buf[3];
	buf[0] = channel;
	buf[1] = phy_mode;
	buf[2] = Modulation_Index;
	iot_printf("HCI_LE_Receiver_Test:");
	iot_printf("buf[0] = %0x, buf[1] = %0x\n\n",buf[0],buf[1]);
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_RX_TEST),
                                      buf, sizeof(buf));
	if(rc == 0){
		iot_printf(" \r\n BLE_HCI_OCF_LE_RX_TEST SUCCEED\r\n");

	}else{
		iot_printf(" \r\n BLE_HCI_OCF_LE_RX_TEST FAILED\r\n");
	}
    return rc;
}

SRAM_CODE int ble_hs_tx_test(uint8_t channel, uint8_t length, uint8_t Packet_Payload)
{
	uint8_t rc;
	uint8_t buf[3];
	buf[0] = channel;
	buf[1] = length;
	buf[2] = Packet_Payload;

	iot_printf("HCI_LE_Transmitter_Test:");
	iot_printf("buf[0] = %0x, buf[1] = %0x, buf[2] = %0x \n\n",
		buf[0],buf[1],buf[2]);
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_TX_TEST),
                                      buf, sizeof(buf));

	if(rc == 0){
		iot_printf(" \r\n BLE_HCI_OCF_LE_TX_TEST SUCCEED\r\n");

	}else{
		iot_printf(" \r\n BLE_HCI_OCF_LE_TX_TEST FAILED\r\n");
	}
    return rc;
}


SRAM_CODE int ble_hs_dtm_tx_test(uint8_t channel, uint8_t length, uint8_t Packet_Payload, uint8_t phy_mode)
{
	uint8_t rc;
	uint8_t buf[4];
	buf[0] = channel;
	buf[1] = length;
	buf[2] = Packet_Payload;
	buf[3] = phy_mode;

	iot_printf("HCI_LE_Enhanced_Transmitter_Test:");
	iot_printf("buf[0] = %0x, buf[1] = %0x, buf[2] = %0x, buf[3] = %0x \n\n",
		buf[0],buf[1],buf[2],buf[3]);
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_ENH_TX_TEST),
                                      buf, sizeof(buf));

	if(rc == 0){
		iot_printf(" \r\n HCI_LE_TRANSMITTER_TEST_OCF SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_TRANSMITTER_TEST_OCF FAILED\r\n");
	}
    return rc;
}

SRAM_CODE int ble_hs_dtm_rx_test(uint8_t channel, uint8_t phy_mode)
{

	uint8_t rc;
	uint8_t buf[3];
	buf[0] = channel;
	buf[1] = phy_mode;
	buf[2] = 0x00;
	iot_printf("HCI_LE_Enhanced_Receiver_Test:");
	iot_printf("buf[0] = %0x, buf[1] = %0x\n\n",buf[0],buf[1]);
    rc = ble_hs_hci_cmd_tx_empty_ack(BLE_HCI_OP(BLE_HCI_OGF_LE,
                                      BLE_HCI_OCF_LE_ENH_RX_TEST),
                                      buf, sizeof(buf));
	if(rc == 0){
		iot_printf(" \r\n HCI_LE_RECEIVER_TEST_OCF SUCCEED\r\n");

	}else{
		iot_printf(" \r\n HCI_LE_RECEIVER_TEST_OCF FAILED\r\n");
	}
    return rc;
}

SRAM_CODE int ble_hs_dtm_end(void)
{

    uint8_t rc;
	uint8_t rspbuf[2];
	uint8_t rsplen;

	rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_LE, BLE_HCI_OCF_LE_TEST_END),
                            NULL, 0, rspbuf, 2, &rsplen);

							
	if(rc == 0){
		iot_printf(" \r\n HCI_LE_Test_End SUCCEED\r\n");
		return (rspbuf[1]<<8 | rspbuf[0]);

	}else{
		iot_printf(" \r\n HCI_LE_Test_End FAILED\r\n");
		return rc;
	}
}

SRAM_CODE int ble_tone_tx_start(uint8_t channel, uint8_t power)
{
	int rc;
	uint8_t buf[2];
	
	buf[0] = channel;
	buf[1] = power;

	iot_printf("ble_tone_tx_start, ch:%d, pwr:%d\n", channel, power);
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_VENDOR, BLE_HCI_VS_TONE_TX_START),
        						buf, sizeof(buf), NULL, 0, NULL);
	if(rc == 0){
		iot_printf(" \r\n ble_tone_tx_start SUCCEED\r\n");

	}else{
		iot_printf(" \r\n ble_tone_tx_start FAILED\r\n");
	}
	return rc;
}

SRAM_CODE int ble_tone_tx_stop(void)
{
	int rc;

	iot_printf("ble_tone_tx_stop\n");
    rc =  ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_VENDOR, BLE_HCI_VS_TONE_TX_STOP),
        						NULL, 0, NULL, 0, NULL);
	if(rc == 0){
		iot_printf(" \r\n ble_tone_tx_stop SUCCEED\r\n");

	}else{
		iot_printf(" \r\n ble_tone_tx_stop FAILED\r\n");
	}
	return rc;
}

SRAM_CODE int ble_start_srrc(void)
{
	int rc;

	iot_printf("ble_start_srrc\n");
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_VENDOR, BLE_HCI_VS_START_SRRC),
        						NULL, 0, NULL, 0, NULL);
	if(rc == 0){
		iot_printf(" \r\n ble_start_srrc SUCCEED\r\n");

	}else{
		iot_printf(" \r\n ble_start_srrc FAILED\r\n");
	}
	return rc;
}

SRAM_CODE int ble_stop_srrc(void)
{
	int rc;

	iot_printf("ble_stop_srrc\n");
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_VENDOR, BLE_HCI_VS_STOP_SRRC),
        						NULL, 0, NULL, 0, NULL);
	if(rc == 0){
		iot_printf(" \r\n ble_stop_srrc SUCCEED\r\n");

	}else{
		iot_printf(" \r\n ble_stop_srrc FAILED\r\n");
	}
	return rc;
}

SRAM_CODE int ble_hs_scan_set_name_filt(uint8_t name_len, uint8_t *name)
{
	int rc;
	uint8_t buf[32];


	if(name_len > 31){
		return -1;
	}
	
	buf[0] = name_len;
	memcpy(&buf[1], name, name_len);

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_VENDOR, BLE_HCI_VS_SET_SCAN_NAME_FILT),
        						buf, (buf[0]+1), NULL, 0, NULL);
	
	return rc;
}


SRAM_CODE int ble_hs_afh_param_set(uint8_t threshold, uint8_t period)
{
    int rc;
	uint8_t buf[2];
	
	buf[0] = threshold;
	buf[1] = period;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_VENDOR, BLE_HCI_VS_SET_AFH_PARAM),
        						buf, 2, NULL, 0, NULL);

	if(rc == 0){
		iot_printf("ble set afh param SUCCEED\r\n");
	}else{
		iot_printf("ble set afh param FAILED\r\n");
	}
	
}



SRAM_CODE int ble_hs_set_ce_test(uint8_t enable, int8_t rssi)
{
	int rc;
	uint8_t buf[2];

	
	buf[0] = enable;
	buf[1] = rssi;

    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_VENDOR, BLE_HCI_VS_SET_CE_TEST),
        						buf, 2, NULL, 0, NULL);

	if(rc == 0){
		iot_printf("ble set ce test SUCCEED\r\n");

	}else{
		iot_printf("ble set ce test FAILED\r\n");
	}
	
	return rc;
}


SRAM_CODE int ble_hs_hci_set_pub_addr(uint8_t *addr)
{
	int rc;
	uint8_t buf[BLE_DEV_ADDR_LEN];

	memcpy(&buf[0], addr, BLE_DEV_ADDR_LEN);
    rc = ble_hs_hci_cmd_tx(BLE_HCI_OP(BLE_HCI_OGF_VENDOR, BLE_HCI_VS_SET_PUB_ADDR),
        						buf, BLE_DEV_ADDR_LEN, NULL, 0, NULL);

	if(rc == 0){
		iot_printf("ble set pub addr SUCCEED\r\n");

	}else{
		iot_printf("ble set pub addr FAILED\r\n");
	}

	return rc;
}




SRAM_CODE void
ble_hs_init(void)
{
    int rc;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    /* Create memory pool of OS events */
    rc = os_mempool_init(&ble_hs_hci_ev_pool, BLE_HS_HCI_EVT_COUNT,
                         sizeof (struct ble_npl_event), ble_hs_hci_os_event_buf,
                         "ble_hs_hci_ev_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    /* These get initialized here to allow unit tests to run without a zeroed
     * bss.
     */
    ble_hs_reset_reason = 0;
    ble_hs_enabled_state = BLE_HS_ENABLED_STATE_OFF;

    ble_npl_event_init(&ble_hs_ev_tx_notifications, ble_hs_event_tx_notify,
                       NULL);
    ble_npl_event_init(&ble_hs_ev_reset, ble_hs_event_reset, NULL);
    ble_npl_event_init(&ble_hs_ev_start_stage1, ble_hs_event_start_stage1,
                       NULL);
    ble_npl_event_init(&ble_hs_ev_start_stage2, ble_hs_event_start_stage2,
                       NULL);

    ble_hs_hci_init();

    rc = ble_hs_conn_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_l2cap_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_att_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_att_svr_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gap_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gattc_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_gatts_init();
    SYSINIT_PANIC_ASSERT(rc == 0);

    ble_hs_stop_init();

    ble_mqueue_init(&ble_hs_rx_q, ble_hs_event_rx_data, NULL);

    rc = stats_init_and_reg(
        STATS_HDR(ble_hs_stats), STATS_SIZE_INIT_PARMS(ble_hs_stats,
        STATS_SIZE_32), STATS_NAME_INIT_PARMS(ble_hs_stats), "ble_hs");
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = ble_npl_mutex_init(&ble_hs_mutex);
    SYSINIT_PANIC_ASSERT(rc == 0);


#if MYNEWT_VAL(BLE_HS_DEBUG)
    ble_hs_dbg_mutex_locked = 0;
#endif

    /* Configure the HCI transport to communicate with a host. */
    ble_hci_trans_cfg_hs(ble_hs_hci_rx_evt, NULL, ble_hs_rx_data, NULL);

#ifdef MYNEWT
    ble_hs_evq_set((struct ble_npl_eventq *)os_eventq_dflt_get());
#else
    ble_hs_evq_set(nimble_port_get_dflt_eventq());
#endif
	
//    ble_npl_callout_stop(&ble_hs_timer);
//    ble_npl_callout_init(&ble_hs_timer, ble_hs_evq, ble_hs_timer_exp, NULL);

#if BLE_MONITOR
    rc = ble_monitor_init();
    SYSINIT_PANIC_ASSERT(rc == 0);
#endif

    /* Enqueue the start event to the default event queue.  Using the default
     * queue ensures the event won't run until the end of main().  This allows
     * the application to configure this package in the meantime.
     */
#if MYNEWT_VAL(BLE_HS_AUTO_START)
#ifdef MYNEWT
    ble_npl_eventq_put((struct ble_npl_eventq *)os_eventq_dflt_get(),
                       &ble_hs_ev_start_stage1);
#else
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &ble_hs_ev_start_stage1);
#endif
#endif

#if BLE_MONITOR
    ble_monitor_new_index(0, (uint8_t[6]){ }, "nimble0");
#endif
}

void ble_hs_free(void)
{
	ble_npl_callout_free(&ble_hs_timer);
	ble_gatts_reset();
}

