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
#include "nimble/nimble_npl.h"


struct ble_npl_event *npl_atbmos_eventq_get(struct ble_npl_eventq *evq, ble_npl_time_t tmo)
{
    struct ble_npl_event *ev = NULL;
    int ret;

	ret = atbm_os_MsgQ_Recv(&evq->q, &ev, sizeof(atbm_void*), tmo);
    if((ret == WIFI_OK) && (ev != NULL)){
        ev->queued = false;
    }
	else{
		ev = NULL;
	}

    return ev;
}

void npl_atbmos_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    if (ev->queued) {
        return;
    }

    ev->queued = true;
    atbm_os_MsgQ_Send(&evq->q, &ev, sizeof(atbm_void*), BLE_NPL_TIME_FOREVER);
}

void npl_atbmos_eventq_remove(struct ble_npl_eventq *evq,
                      struct ble_npl_event *ev)
{
    struct ble_npl_event *tmp_ev;
    int ret;

    if (!ev->queued) {
        return;
    }

	do{
		ret = atbm_os_MsgQ_Recv(&evq->q, &tmp_ev, sizeof(atbm_void *), 0);
	}while(ret == WIFI_OK);
  
    ev->queued = 0;
}

ble_npl_error_t npl_atbmos_mutex_init(struct ble_npl_mutex *mu)
{
    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }
	
	atbm_os_mutexLockInit(&mu->handle);

    return BLE_NPL_OK;
}

ble_npl_error_t npl_atbmos_mutex_pend(struct ble_npl_mutex *mu, ble_npl_time_t timeout)
{
    int ret;

    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }

	ret = atbm_os_mutexLock(&mu->handle, timeout);

    return ret == WIFI_OK ? BLE_NPL_OK : BLE_NPL_TIMEOUT;
}

ble_npl_error_t npl_atbmos_mutex_release(struct ble_npl_mutex *mu)
{
    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }

	atbm_os_mutexUnLock(&mu->handle);

    return BLE_NPL_OK;
}

ble_npl_error_t npl_atbmos_mutex_free(struct ble_npl_mutex *mu)
{
    if (!mu) {
        return BLE_NPL_INVALID_PARAM;
    }

	atbm_os_DeleteMutex(&mu->handle);

    return BLE_NPL_OK;
}


ble_npl_error_t npl_atbmos_sem_init(struct ble_npl_sem *sem, uint16_t tokens)
{
    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

	atbm_os_init_waitevent(&sem->handle);

    return BLE_NPL_OK;
}

ble_npl_error_t npl_atbmos_sem_pend(struct ble_npl_sem *sem, ble_npl_time_t timeout)
{
    int ret = 0;

    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

	ret = atbm_os_wait_event_timeout(&sem->handle, timeout);

    return ret == WIFI_OK ? BLE_NPL_OK : BLE_NPL_TIMEOUT;
}

unsigned int uxSemaphoreGetCount(void * xQueue)
{
	return atbm_os_wait_event_count_get((atbm_os_wait_queue_head_t *)xQueue);
}

ble_npl_error_t npl_atbmos_sem_release(struct ble_npl_sem *sem)
{
    if (!sem) {
        return BLE_NPL_INVALID_PARAM;
    }

	atbm_os_wakeup_event(&sem->handle);

    return BLE_NPL_OK;
}

void npl_atbmos_sem_free(struct ble_npl_sem *sem)
{
    if (!sem) {
        return;
    }
	
	atbm_os_DeleteSem(&sem->handle);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
static void os_callout_timer_cb(struct timer_list *arg)
#elif(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 129))
static void os_callout_timer_cb(unsigned long arg)
#else
static void os_callout_timer_cb(atbm_void *arg)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
	OS_TIMER *atbm_timer = from_timer(atbm_timer, arg, TimerHander);
	struct ble_npl_callout *co = (struct ble_npl_callout *)atbm_timer->pCallRef;
#else
	struct ble_npl_callout *co = (struct ble_npl_callout *)arg;
#endif

    assert(co);
	atbm_ClearActive(&co->handle);

    if (co->evq) {
        ble_npl_eventq_put(co->evq, &co->ev);
    } else {
        co->ev.fn(&co->ev);
    }
}

void npl_atbmos_callout_init(struct ble_npl_callout *co, struct ble_npl_eventq *evq,
                     ble_npl_event_fn *ev_cb, void *ev_arg)
{
    memset(co, 0, sizeof(*co));
	atbm_InitTimer(&co->handle, os_callout_timer_cb, co);
    co->evq = evq;
    ble_npl_event_init(&co->ev, ev_cb, ev_arg);
}

ble_npl_error_t npl_atbmos_callout_reset(struct ble_npl_callout *co, ble_npl_time_t ticks)
{
    if (ticks < 0) {
        return BLE_NPL_INVALID_PARAM;
    }

    if (ticks == 0) {
        ticks = 1;
    }

	atbm_StartTimer(&co->handle, ticks);

    return BLE_NPL_OK;
}


void npl_atbmos_callout_stop(struct ble_npl_callout *co)
{
	atbm_CancelTimer(&co->handle);	
}

void npl_atbmos_callout_free(struct ble_npl_callout *co)
{
	atbm_FreeTimer(&co->handle);
}

ble_npl_time_t npl_atbmos_callout_remaining_ticks(struct ble_npl_callout *co,
                                     ble_npl_time_t now)
{
    ble_npl_time_t rt;
    uint32_t exp;

    exp = atbm_TimerGetExpiry(&co->handle);

    if (exp > now) {
        rt = exp - now;
    } else {
        rt = 0;
    }

    return rt;
}

ble_npl_error_t npl_atbmos_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *out_ticks)
{
	*out_ticks = atbm_TimerMsToTick(ms);
    return 0;
}

ble_npl_error_t npl_atbmos_time_ticks_to_ms(ble_npl_time_t ticks, uint32_t *out_ms)
{
    *out_ms = atbm_TimerTickToMs(ticks);
    return 0;
}

