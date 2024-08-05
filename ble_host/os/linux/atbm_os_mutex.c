/**************************************************************************************************************
 * altobeam LINUX wifi hmac source code 
 *
 * Copyright (c) 2018, altobeam.inc   All rights reserved.
 *
 *  The source code contains proprietary information of AltoBeam, and shall not be distributed, 
 *  copied, reproduced, or disclosed in whole or in part without prior written permission of AltoBeam.
*****************************************************************************************************************/

#include "atbm_hal.h"
#include "atbm_os_mutex.h"

atbm_uint32 atbm_os_CreateSem(atbm_uint8 ubSemValue)
{
	struct semaphore *sem = NULL;

	if (ubSemValue == 0){
		wifi_printk(WIFI_DBG_ERROR,"invalid ubSemValue: %d err\n", ubSemValue);
		return -1;
	}

	sem = (struct semaphore *)atbm_kmalloc(sizeof(struct semaphore), GFP_KERNEL);
	if (sem == NULL){
		wifi_printk(WIFI_DBG_ERROR,"atbm_kmalloc err\n");
		return -1;
	}

	sema_init(sem, ubSemValue);

	return (atbm_uint32)sem;
}

atbm_int32 atbm_os_init_waitevent(atbm_os_wait_queue_head_t* wait_queue_head)
{
	if (wait_queue_head == NULL){
		wifi_printk(WIFI_DBG_ERROR,"wait_queue_head err\n");
		return -1;
	}

	sema_init(wait_queue_head, 0);
	return 0;
}

atbm_uint8 atbm_os_DeleteSem(atbm_os_wait_queue_head_t* wait_queue_head)
{
	if (wait_queue_head == NULL){
		wifi_printk(WIFI_DBG_ERROR,"wait_queue_head err\n");
		return -1;
	}

    return 0;
}

atbm_uint32 atbm_os_wait_event_count_get(atbm_os_wait_queue_head_t * wait_queue_head)
{
	return wait_queue_head->count;
}

atbm_uint8 atbm_os_wait_event_timeout(atbm_os_wait_queue_head_t * wait_queue_head, atbm_uint32 ulTimeout)
{
	if (wait_queue_head == NULL){
		wifi_printk(WIFI_DBG_ERROR,"wait_queue_head err\n");
		return -1;
	}

	//down(wait_queue_head);
	return down_timeout(wait_queue_head, ulTimeout);
}

atbm_uint8 atbm_os_wakeup_event(atbm_os_wait_queue_head_t * wait_queue_head)
{
	if (wait_queue_head == NULL){
		wifi_printk(WIFI_DBG_ERROR,"wait_queue_head err\n");
		return -1;
	}

	up(wait_queue_head);
	return 0;
}

atbm_uint32 atbm_os_mutexLockInit(atbm_mutex * pmutex)
{
	if (pmutex == NULL){
		wifi_printk(WIFI_DBG_ERROR,"pmutex err\n");
		return -1;
	}

	mutex_init(pmutex);
	return 0;
}

atbm_uint8 atbm_os_DeleteMutex(atbm_mutex * pmutex_id)
{
	if (pmutex_id == NULL){
		wifi_printk(WIFI_DBG_ERROR,"pmutex_id err\n");
		return -1;
	}

    return 0;
}

atbm_uint8 atbm_os_mutexLock(atbm_mutex * pmutex_id,atbm_uint32 ulTimeout)
{
	if (pmutex_id == NULL){
		wifi_printk(WIFI_DBG_ERROR,"pmutex_id err\n");
		return -1;
	}

    mutex_lock(pmutex_id);
	return 0;
}

atbm_uint8 atbm_os_mutexUnLock(atbm_mutex * pmutex_id)
{
	if (pmutex_id == NULL){
		wifi_printk(WIFI_DBG_ERROR,"pmutex_id err\n");
		return -1;
	}

    mutex_unlock(pmutex_id);
	return 0;
}

atbm_uint8 atbm_os_mutextryLock(atbm_mutex * pmutex_id)
{
	if (pmutex_id == NULL){
		wifi_printk(WIFI_DBG_ERROR,"pmutex_id err\n");
		return -1;
	}

    if (mutex_trylock(pmutex_id)){
		wifi_printk(WIFI_DBG_ERROR,"mutex_trylock err\n");
		return -1;
	}
	else {
		return 0;
	}
}

