
/**************************************************************************************************************
 * altobeam LINUX wifi hmac source code 
 *
 * Copyright (c) 2018, altobeam.inc   All rights reserved.
 *
 *  The source code contains proprietary information of AltoBeam, and shall not be distributed, 
 *  copied, reproduced, or disclosed in whole or in part without prior written permission of AltoBeam.
*****************************************************************************************************************/
#include "atbm_hal.h"
#include "atbm_os_msgQ.h"

static atbm_uint32 get_index(atbm_uint32 all, atbm_uint32 div)
{
	while (all>=div){
		all -= div;
	}

	return all;
}

atbm_int8 atbm_os_MsgQ_Create(atbm_os_msgq *pmsgQ, atbm_uint32 *pstack, atbm_uint32 item_size, atbm_uint32 item_num)
{
	atbm_int8 ret = WIFI_OK;

	if(pmsgQ == ATBM_NULL){
		wifi_printk(WIFI_DBG_MSG,"pmsgQ is null\n");
		ret = WIFI_ERROR;
		goto error;
	}

	if((pstack == ATBM_NULL)
		|| (item_size == 0)
		|| (item_num == 0)){
		wifi_printk(WIFI_DBG_MSG,"param err\n");
		ret = WIFI_ERROR;
		goto error;
	}

	memset(pmsgQ, 0, sizeof(atbm_os_msgq));
	pmsgQ->stack = (atbm_uint8 *)pstack;
	pmsgQ->esize = item_size;
	pmsgQ->ecount = item_num;

	pmsgQ->actsize = atbm_kmalloc(sizeof(atbm_uint32)*pmsgQ->ecount, GFP_KERNEL);
	if(pmsgQ->actsize == ATBM_NULL){
		wifi_printk(WIFI_DBG_MSG,"atbm_kmalloc err\n");
		ret = WIFI_ERROR;
		goto error;
	}
	
	sema_init(&pmsgQ->wait_sem, 0);
	atbm_spin_lock_init(&pmsgQ->spinlock);
	atbm_spin_lock_init(&pmsgQ->spinlock_msg);

error:
	return ret;
}

atbm_int8 atbm_os_MsgQ_Delete(atbm_os_msgq *pmsgQ)
{
	atbm_int8 ret = WIFI_OK;

	if(pmsgQ == ATBM_NULL){
		wifi_printk(WIFI_DBG_MSG,"pmsgQ is null\n");
		ret = WIFI_ERROR;
		goto error;
	}

	pmsgQ->is_exit = 1;
	pmsgQ->curcount = 1;
	up(&(pmsgQ->wait_sem));

	atbm_kfree(pmsgQ->actsize);

error:
	return ret;
}

atbm_int8 atbm_os_MsgQ_Recv(atbm_os_msgq *pmsgQ, atbm_void *pbuf, int len, atbm_uint32 val)
{
	atbm_int8 ret = WIFI_OK;
	atbm_uint32 index = 0;
	atbm_uint32 actual_size = 0;
	atbm_uint8 need_wake = 0;

	if(pmsgQ == ATBM_NULL){
		wifi_printk(WIFI_DBG_ERROR,"pmsgQ is null\n");
		ret = WIFI_ERROR;
		goto error;
	}

	if((pbuf == ATBM_NULL)
		|| (len == 0)){
		wifi_printk(WIFI_DBG_ERROR,"param err\n");
		ret = WIFI_ERROR;
		goto error;
	}

	if (pmsgQ->ecount <= 0){
		wifi_printk(WIFI_DBG_ERROR,"pmsgQ->ecount err\n");
		ret = WIFI_ERROR;
		goto error;
	}

	if (pmsgQ->curcount == 0){
		if (val){
			//down(&(pmsgQ->wait_sem));
			if(down_timeout(&(pmsgQ->wait_sem), val)){
				ret = WIFI_ERROR;
				goto error;
			}
			
			if (pmsgQ->is_exit){
				wifi_printk(WIFI_DBG_ERROR,"msg delete\n");
				ret = WIFI_ERROR;
				goto error;
			}
		}
		else {
			wifi_printk(WIFI_DBG_ERROR,"no msg send\n");
			ret = WIFI_ERROR;
			goto error;
		}
	}

	if (pmsgQ->curcount == 0){
		ret = WIFI_ERROR;
		goto error;
	}

	atbm_spin_lock_bh(&pmsgQ->spinlock_msg);
	index = get_index(pmsgQ->output,pmsgQ->ecount);
	actual_size = len>pmsgQ->actsize[index]?pmsgQ->actsize[index]:len;

	memcpy(pbuf, pmsgQ->stack+index*pmsgQ->esize, actual_size);

	if (pmsgQ->curcount == pmsgQ->ecount){
		need_wake = 1;
	}

	pmsgQ->output++;
	pmsgQ->curcount--;
	atbm_spin_unlock_bh(&pmsgQ->spinlock_msg);

	if (need_wake){
		up(&(pmsgQ->wait_sem));
	}
	
error:
	return ret;
}

atbm_int8 atbm_os_MsgQ_Send(atbm_os_msgq *pmsgQ, atbm_void *pbuf, int len, atbm_uint32 val)
{
	atbm_int8 ret = WIFI_OK;
	atbm_uint32 index = 0;
	atbm_uint32 actual_size = 0;
	atbm_uint8 need_wake = 0;

	if(pmsgQ == ATBM_NULL){
		wifi_printk(WIFI_DBG_ERROR,"pmsgQ is null\n");
		ret = WIFI_ERROR;
		goto error;
	}

	if((pbuf == ATBM_NULL)
		|| (len == 0)){
		wifi_printk(WIFI_DBG_ERROR,"param err\n");
		ret = WIFI_ERROR;
		goto error;
	}

	if (pmsgQ->ecount <= 0){
		wifi_printk(WIFI_DBG_ERROR,"pmsgQ->ecount err\n");
		ret = WIFI_ERROR;
		goto error;
	}

	if (pmsgQ->curcount >= pmsgQ->ecount){
		if (val){
			down(&(pmsgQ->wait_sem));
			if (pmsgQ->is_exit){
				wifi_printk(WIFI_DBG_ERROR,"msg delete\n");
				ret = WIFI_ERROR;
				goto error;
			}
		}
		else {
			wifi_printk(WIFI_DBG_ERROR,"no msg receive\n");
			ret = WIFI_ERROR;
			goto error;
		}
	}

	atbm_spin_lock(&pmsgQ->spinlock_msg);
	index = get_index(pmsgQ->input,pmsgQ->ecount);
	actual_size = len>pmsgQ->esize?pmsgQ->esize:len;

	memcpy(pmsgQ->stack+index*pmsgQ->esize, pbuf, actual_size);

	if (pmsgQ->curcount == 0){
		need_wake = 1;
	}

	pmsgQ->actsize[index] = actual_size;
	pmsgQ->input++;
	pmsgQ->curcount++;
	atbm_spin_unlock(&pmsgQ->spinlock_msg);

	if (need_wake){
		up(&(pmsgQ->wait_sem));
	}

error:
	return ret;
}

