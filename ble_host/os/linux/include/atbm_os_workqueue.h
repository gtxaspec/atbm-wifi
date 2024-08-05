#ifndef ATBM_OS_WORKQUEUE_H
#define ATBM_OS_WORKQUEUE_H
#include "atbm_type.h"
atbm_uint8 atbm_os_GetMessage(atbm_uint32 ulMQId, atbm_void **msg, atbm_uint32 ulTimeout);
atbm_uint8 atbm_os_PutMessage(atbm_uint32 ulMQId, atbm_void *msg);
atbm_uint8 atbm_os_DeleteMQueue(atbm_uint32 ulMQId);
#endif /* ATBM_OS_WORKQUEUE_H */

