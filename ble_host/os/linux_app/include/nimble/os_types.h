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

#ifndef _NPL_OS_TYPES_H
#define _NPL_OS_TYPES_H

#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>

/* The highest and lowest task priorities */
#define OS_TASK_PRI_HIGHEST (sched_get_priority_max(SCHED_RR))
#define OS_TASK_PRI_LOWEST  (sched_get_priority_min(SCHED_RR))

typedef uint32_t ble_npl_time_t;
typedef int32_t ble_npl_stime_t;

//typedef int os_sr_t;
typedef int ble_npl_stack_t;


struct ble_npl_event {
    uint8_t                 ev_queued;
    ble_npl_event_fn       *ev_cb;
    void                   *ev_arg;
};

struct ble_npl_eventq {
    void               *q;
};

struct ble_npl_callout {
    struct ble_npl_event    c_ev;
    struct ble_npl_eventq  *c_evq;
    uint32_t                c_ticks;
    timer_t                 c_timer;
    bool                    c_active;
};

struct ble_npl_mutex {
    pthread_mutex_t         lock;
    pthread_mutexattr_t     attr;
    struct timespec         wait;
};

struct ble_npl_sem {
    sem_t                   lock;
	int 					vaild;
};

struct ble_npl_task {
    pthread_t               handle;
    pthread_attr_t          attr;
    struct sched_param      param;
    const char*             name;
};

//#include <linux/sched.h>
//#include <linux/kthread.h>
//#include <linux/err.h>

typedef void* pAtbm_thread_t;

enum ATBM_THREAD_PRIO
{
    WORK_TASK_PRIO,
    BLE_TASK_PRIO,
#if ATBM_SDIO_BUS && (!ATBM_TXRX_IN_ONE_THREAD)
    TX_BH_TASK_PRIO,
    RX_BH_TASK_PRIO,
#else
    BH_TASK_PRIO,
#endif
    ELOOP_TASK_PRIO,
    BLE_AT_PRIO,
    BLE_APP_PRIO,
};

#define HMAC_MAX_THREAD_PRIO 100
#define HMAC_MIN_THREAD_PRIO 10

pAtbm_thread_t atbm_createThread(int32_t(*task)(void* p_arg), void* p_arg, int prio);
int atbm_stopThread(pAtbm_thread_t thread_id);
int atbm_ThreadStopEvent(pAtbm_thread_t thread_id);
int atbm_getCurThreadId(void);

typedef void *(*ble_npl_task_func_t)(void *);

int ble_npl_task_init(struct ble_npl_task *t, const char *name, ble_npl_task_func_t func,
		 void *arg, uint8_t prio, ble_npl_time_t sanity_itvl,
		 ble_npl_stack_t *stack_bottom, uint16_t stack_size);

int ble_npl_task_remove(struct ble_npl_task *t);

uint8_t ble_npl_task_count(void);

void ble_npl_task_yield(void);

#endif // _NPL_OS_TYPES_H
