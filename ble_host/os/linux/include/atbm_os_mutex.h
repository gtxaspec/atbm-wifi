#ifndef ATBM_OS_MUTEX_H
#define ATBM_OS_MUTEX_H

#include "atbm_type.h"

#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/wait.h>

struct wait_queue_event{
	wait_queue_head_t wait_queue;
	int event;
};

typedef unsigned long atbm_uint32_lock;

typedef struct mutex atbm_mutex;
//typedef struct wait_queue_event atbm_os_wait_queue_head_t;

typedef struct semaphore atbm_os_wait_queue_head_t;

/*sem*/
atbm_uint32  atbm_os_CreateSem(atbm_uint8 ubSemValue);
atbm_uint32 atbm_os_wait_event_count_get(atbm_os_wait_queue_head_t * wait_queue_head);
atbm_uint8 atbm_os_wait_event_timeout(atbm_os_wait_queue_head_t * pulSemId, atbm_uint32 ulTimeout);
atbm_uint8 atbm_os_wakeup_event(atbm_os_wait_queue_head_t * pulSemId);
atbm_int32 atbm_os_init_waitevent(atbm_os_wait_queue_head_t* pulSemId);
atbm_uint8 atbm_os_DeleteSem(atbm_os_wait_queue_head_t* pulSem);
#define atbm_os_delete_waitevent(x) atbm_os_DeleteSem((atbm_os_wait_queue_head_t*)x)

/*mutex*/
atbm_uint32 atbm_os_mutexLockInit(atbm_mutex * pmutex_id);
atbm_uint8 atbm_os_DeleteMutex(atbm_mutex * pmutex_id);
atbm_uint8 atbm_os_mutexLock(atbm_mutex * pmutex_id,atbm_uint32 ulTimeout);
atbm_uint8 atbm_os_mutexUnLock(atbm_mutex * pmutex_id);

#endif /* ATBM_OS_MUTEX_H */

