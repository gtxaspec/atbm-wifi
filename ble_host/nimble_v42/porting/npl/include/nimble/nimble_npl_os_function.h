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

#ifndef _NIMBLE_NPL_OS_FUNC_H_
#define _NIMBLE_NPL_OS_FUNC_H_

#ifdef __cplusplus
extern "C" {
#endif

static inline bool
ble_npl_os_started(void)
{
    return true;
}

static inline void *
ble_npl_get_current_task_id(void)
{
    return (void *)(uint32_t)atbm_getCurThreadId();
}

static inline void
ble_npl_eventq_init(struct ble_npl_eventq *evq)
{
	evq->stack = (atbm_void *)atbm_kmalloc(32 * sizeof(struct ble_npl_eventq *), GFP_KERNEL);
	atbm_os_MsgQ_Create(&evq->q, evq->stack, sizeof(struct ble_npl_eventq *), 32);
}

static inline void
ble_npl_eventq_release(struct ble_npl_eventq *evq)
{
	atbm_kfree(evq->stack);
	atbm_os_MsgQ_Delete(&evq->q);
}


static inline struct ble_npl_event *
ble_npl_eventq_get(struct ble_npl_eventq *evq, ble_npl_time_t tmo)
{
    return npl_atbmos_eventq_get(evq, tmo);	
}

static inline void
ble_npl_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    npl_atbmos_eventq_put(evq, ev);
}

static inline void
ble_npl_eventq_remove(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    npl_atbmos_eventq_remove(evq, ev);
}

static inline void
ble_npl_event_run(struct ble_npl_event *ev)
{
    ev->fn(ev);
}

static inline bool
ble_npl_eventq_is_empty(struct ble_npl_eventq *evq)
{
    //return xQueueIsQueueEmptyFromISR(evq->q);
    return true;
}

static inline void
ble_npl_event_init(struct ble_npl_event *ev, ble_npl_event_fn *fn,
                   void *arg)
{
    memset(ev, 0, sizeof(*ev));
    ev->fn = fn;
    ev->arg = arg;
}

static inline bool
ble_npl_event_is_queued(struct ble_npl_event *ev)
{
    return ev->queued;
}

static inline void *
ble_npl_event_get_arg(struct ble_npl_event *ev)
{
    return ev->arg;
}

static inline void
ble_npl_event_set_arg(struct ble_npl_event *ev, void *arg)
{
    ev->arg = arg;
}

static inline ble_npl_error_t
ble_npl_mutex_init(struct ble_npl_mutex *mu)
{
    return npl_atbmos_mutex_init(mu);
}

static inline ble_npl_error_t
ble_npl_mutex_pend(struct ble_npl_mutex *mu, ble_npl_time_t timeout)
{
    return npl_atbmos_mutex_pend(mu, timeout);
}

static inline ble_npl_error_t
ble_npl_mutex_release(struct ble_npl_mutex *mu)
{
    return npl_atbmos_mutex_release(mu);
}

static inline ble_npl_error_t
ble_npl_mutex_free(struct ble_npl_mutex *mu)
{
    return npl_atbmos_mutex_free(mu);
}


static inline ble_npl_error_t
ble_npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens)
{
    return npl_atbmos_sem_init(sem, tokens);
}

static inline ble_npl_error_t
ble_npl_sem_pend(struct ble_npl_sem *sem, ble_npl_time_t timeout)
{
    return npl_atbmos_sem_pend(sem, timeout);
}

static inline ble_npl_error_t
ble_npl_sem_release(struct ble_npl_sem *sem)
{
    return npl_atbmos_sem_release(sem);
}

static inline void
ble_npl_sem_free(struct ble_npl_sem *sem)
{
	npl_atbmos_sem_free(sem);
}

extern unsigned  int uxSemaphoreGetCount(void * xQueue);

static inline uint16_t
ble_npl_sem_get_count(struct ble_npl_sem *sem)
{
    return uxSemaphoreGetCount(&sem->handle);
}

static inline void
ble_npl_callout_init(struct ble_npl_callout *co, struct ble_npl_eventq *evq,
                     ble_npl_event_fn *ev_cb, void *ev_arg)
{
    npl_atbmos_callout_init(co, evq, ev_cb, ev_arg);
}

static inline ble_npl_error_t
ble_npl_callout_reset(struct ble_npl_callout *co, ble_npl_time_t ticks)
{
    return npl_atbmos_callout_reset(co, ticks);
}

static inline void
ble_npl_callout_stop(struct ble_npl_callout *co)
{
	npl_atbmos_callout_stop(co);
}

static inline void
ble_npl_callout_free(struct ble_npl_callout *co)
{
	npl_atbmos_callout_free(co);
}

static inline bool
ble_npl_callout_is_active(struct ble_npl_callout *co)
{
    return atbm_TimerIsActive(&co->handle);
}

static inline ble_npl_time_t
ble_npl_callout_get_ticks(struct ble_npl_callout *co)
{
	return atbm_TimerGetExpiry(&co->handle);
}

static inline uint32_t
ble_npl_callout_remaining_ticks(struct ble_npl_callout *co,
                                ble_npl_time_t time)
{
    return npl_atbmos_callout_remaining_ticks(co, time);
}

static inline void
ble_npl_callout_set_arg(struct ble_npl_callout *co, void *arg)
{
    co->ev.arg = arg;
}

static inline uint32_t
ble_npl_time_get(void)
{
    return atbm_TimerTickGet();
}

static inline ble_npl_error_t
ble_npl_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *out_ticks)
{
    return npl_atbmos_time_ms_to_ticks(ms, out_ticks);
}

static inline ble_npl_error_t
ble_npl_time_ticks_to_ms(ble_npl_time_t ticks, uint32_t *out_ms)
{
    return ble_npl_time_ticks_to_ms(ticks, out_ms);
}

static inline ble_npl_time_t
ble_npl_time_ms_to_ticks32(uint32_t ms)
{
    return atbm_TimerMsToTick(ms);
}

static inline uint32_t
ble_npl_time_ticks_to_ms32(ble_npl_time_t ticks)
{
    return atbm_TimerTickToMs(ticks);
}

static inline void
ble_npl_time_delay(ble_npl_time_t ticks)
{
	atbm_SleepMs(atbm_TimerTickToMs(ticks));
}

#if 0//NIMBLE_CFG_CONTROLLER
/*static inline void
ble_npl_hw_set_isr(int irqn, void (*addr)(void))
{
    npl_atbmos_hw_set_isr(irqn, addr);
}*/
#endif

static inline uint32_t
ble_npl_hw_enter_critical(void)
{
    return atbm_disable_irq();
}

static inline void
ble_npl_hw_exit_critical(uint32_t ctx)
{
    atbm_enable_irq();
}

#ifdef __cplusplus
}
#endif

#endif  /* _NIMBLE_NPL_OS_FUNC_H_ */
