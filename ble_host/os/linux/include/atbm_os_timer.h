#ifndef ATBM_OS_TIMER_H
#define ATBM_OS_TIMER_H

#include <linux/version.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/delay.h>


#define TIMER_1ST_PARAM
#define OS_WAIT_FOREVER		MAX_SCHEDULE_TIMEOUT


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
typedef atbm_void  (*TIMER_CALLBACK)(struct timer_list *in_timer);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 129))
typedef atbm_void (*TIMER_CALLBACK)(unsigned long CallRef);
#else
typedef atbm_void (*TIMER_CALLBACK)(atbm_void * CallRef);

#endif



struct OS_TIMER_S
{
    atbm_uint32          TimerId;
    TIMER_CALLBACK  pCallback;
    atbm_void *          pCallRef;
	atbm_int8 *         pTimerName;
	struct timer_list	TimerHander;
	int 			bTimerStart;
	struct OS_TIMER_S *hnext;
};


typedef struct OS_TIMER_S OS_TIMER;

 int atbm_InitTimer(OS_TIMER *pTimer, TIMER_CALLBACK pCallback, atbm_void * CallRef);
 int atbm_StartTimer(OS_TIMER *pTimer, int Interval/*ms*/);
 int atbm_CancelTimer(OS_TIMER *pTimer);
 atbm_void atbm_FreeTimer(OS_TIMER *pTimer);
 atbm_void atbm_SleepMs(atbm_uint32 uiMiliSecond);
 atbm_void atbm_ClearActive(OS_TIMER *pTimer);

 atbm_uint32 atbm_TimerGetExpiry(OS_TIMER *pTimer);
 atbm_uint8 atbm_TimerIsActive(OS_TIMER *pTimer);
 atbm_uint32 atbm_TimerMsToTick(atbm_uint32 ms);
 atbm_uint32 atbm_TimerTickToMs(atbm_uint32 tick);
 atbm_uint32 atbm_TimerTickGet(atbm_void);

 unsigned int atbm_GetOsTimeMs(atbm_void);
 unsigned int atbm_GetOsTime(atbm_void);

ATBM_BOOL atbm_TimeAfter(atbm_uint32 tickMs);
#define atbm_mdelay atbm_SleepMs
atbm_void atbm_wifi_ticks_timer_init(atbm_void);
#endif /* ATBM_OS_TIMER_H */

