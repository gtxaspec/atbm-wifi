#include "atbm_hal.h"
#include "atbm_os_timer.h"
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))

#include <linux/timekeeping.h>
#endif

int atbm_InitTimer(OS_TIMER *pTimer, TIMER_CALLBACK pCallback, atbm_void * CallRef)
{
	pTimer->pCallback = pCallback;
	pTimer->pCallRef = CallRef;
	pTimer->bTimerStart  = 0;
	pTimer->hnext = ATBM_NULL;
	return 0;
}

int atbm_StartTimer(OS_TIMER *pTimer, int Interval)
{
	if(pTimer->bTimerStart){
		del_timer(&(pTimer->TimerHander));
	}
	
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
	timer_setup(&(pTimer->TimerHander), pTimer->pCallback, 0);
#else
	init_timer(&(pTimer->TimerHander));
	pTimer->TimerHander.data = (unsigned long)(pTimer->pCallRef);
	pTimer->TimerHander.function = pTimer->pCallback;
#endif

	mod_timer(&(pTimer->TimerHander), msecs_to_jiffies(Interval)+jiffies);
	pTimer->bTimerStart = 1;
	return 0;
}

int atbm_CancelTimer(OS_TIMER *pTimer)
{	
	if (!pTimer->bTimerStart){
		return -1;
	}

	pTimer->bTimerStart = 0;
	del_timer(&(pTimer->TimerHander));
	return 0;
}

atbm_void atbm_FreeTimer(OS_TIMER *pTimer)
{
	if(pTimer->bTimerStart){
		atbm_CancelTimer(pTimer);
	}
}

atbm_void atbm_ClearActive(OS_TIMER *pTimer)
{
	pTimer->bTimerStart = 0;
}


atbm_void atbm_SleepMs(atbm_uint32 uiMiliSecond)
{
	msleep(uiMiliSecond);
}

ATBM_BOOL atbm_TimeAtSameRange(atbm_uint32 TimeFi,atbm_uint32 TimeSe)
{
	return ATBM_TRUE;
}

atbm_uint32 atbm_TimerGetExpiry(OS_TIMER *pTimer)
{
	return pTimer->TimerHander.expires;
}

atbm_uint8 atbm_TimerIsActive(OS_TIMER *pTimer)
{
	return pTimer->bTimerStart;
}


atbm_uint32 atbm_TimerMsToTick(atbm_uint32 ms)
{
	return msecs_to_jiffies(ms);
}

atbm_uint32 atbm_TimerTickToMs(atbm_uint32 tick)
{
	return jiffies_to_msecs(tick);
}

atbm_uint32 atbm_TimerTickGet(atbm_void)
{
	return jiffies;
}


unsigned int atbm_GetOsTimeMs(atbm_void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0))
	struct timespec64 ts;// = current_time();
	ktime_get_raw_ts64(&ts);
#else
	struct timespec ts = current_kernel_time();
#endif
	return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

unsigned int atbm_GetOsTime(atbm_void)
{
	return atbm_GetOsTimeMs();
}

int os_time_get(atbm_void)
{
	return 	atbm_GetOsTime();
}

ATBM_BOOL atbm_TimeAfter(atbm_uint32 tickMs)
{
	atbm_uint32 current_time = atbm_GetOsTimeMs();

	return ((signed int)((signed int)current_time - (signed int)tickMs < 0));	
}

atbm_void atbm_wifi_ticks_timer_init(atbm_void)
{
}

atbm_void atbm_wifi_ticks_timer_cancle(atbm_void)
{
}

