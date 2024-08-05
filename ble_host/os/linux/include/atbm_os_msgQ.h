#ifndef ATBM_OS_MSGQ_H
#define ATBM_OS_MSGQ_H

#include <linux/wait.h>

struct os_msg_queue{
	atbm_uint32 input;
	atbm_uint32 output;
	atbm_uint8 *stack;
	atbm_uint32 *actsize;
	atbm_uint32 esize;
	atbm_uint32 ecount;
	atbm_uint32 curcount;
	atbm_spinlock_t spinlock;
	atbm_spinlock_t spinlock_msg;
	atbm_uint8 is_exit;
	atbm_uint8 reserved[2];
	struct semaphore wait_sem;
};

typedef struct os_msg_queue atbm_os_msgq;

atbm_int8 atbm_os_MsgQ_Create(atbm_os_msgq *pmsgQ, atbm_uint32 *pstack, atbm_uint32 item_size, atbm_uint32 item_num);
atbm_int8 atbm_os_MsgQ_Delete(atbm_os_msgq *pmsgQ);
atbm_int8 atbm_os_MsgQ_Recv(atbm_os_msgq *pmsgQ, atbm_void *pbuf, int len, atbm_uint32 val);
atbm_int8 atbm_os_MsgQ_Send(atbm_os_msgq *pmsgQ, atbm_void *pbuf, int len, atbm_uint32 val);

#endif /* ATBM_OS_MSGQ_H */

