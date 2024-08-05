#ifndef ATBM_OS_THREAD_H
#define ATBM_OS_THREAD_H

#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/err.h>

typedef atbm_void * pAtbm_thread_t ;

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

pAtbm_thread_t atbm_createThread(atbm_int32(*task)(atbm_void *p_arg), atbm_void *p_arg, int prio);
int atbm_stopThread(pAtbm_thread_t thread_id);
int atbm_ThreadStopEvent(pAtbm_thread_t thread_id);
int atbm_getCurThreadId(void);

#endif /* ATBM_OS_THREAD_H */
