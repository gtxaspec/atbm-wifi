/**************************************************************************************************************
 * altobeam LINUX wifi hmac source code 
 *
 * Copyright (c) 2018, altobeam.inc   All rights reserved.
 *
 *  The source code contains proprietary information of AltoBeam, and shall not be distributed, 
 *  copied, reproduced, or disclosed in whole or in part without prior written permission of AltoBeam.
*****************************************************************************************************************/


#include "atbm_hal.h"
#include "atbm_os_atomic.h"


static spinlock_t irq_dis_lock;
static unsigned long irq_dis_flags;


unsigned int atbm_disable_irq(atbm_void)
{
	spin_lock_bh(&irq_dis_lock);
	return 0;
}

atbm_void atbm_enable_irq(atbm_void)
{
	spin_unlock_bh(&irq_dis_lock);
}

unsigned long atbm_local_irq_save(atbm_void)
{
	unsigned long flags;
	local_irq_save(flags);
	return (unsigned long)flags;
}

atbm_void atbm_local_irq_restore(unsigned long cpu_sr)
{
	local_irq_restore((unsigned long)cpu_sr);
}

atbm_void atbm_atomic_set(atbm_atomic_t *at, int val)
{
	atomic_set(at, val);
}

int atbm_atomic_read(atbm_atomic_t *at)
{
	return atomic_read(at);
}

int atbm_atomic_add_return(int val,atbm_atomic_t *at)
{
	atomic_add(val, at);
	return  atomic_read(at);
}

int atbm_atomic_xchg(atbm_atomic_t * v, int val)
{
	int tmp = atomic_read(v);
	atomic_set(v, val);
	return tmp;
}

int atbm_set_bit(int nr,atbm_uint32* addr)
{
	int mask,retval;
	unsigned long cpu_sr = atbm_local_irq_save();

	addr += nr >>5;
	mask = 1<<(nr & 0x1f);

	retval = (mask & *addr) != 0;
	*addr |= mask;
	atbm_local_irq_restore(cpu_sr);

	return  retval;
}

int atbm_clear_bit(int nr,atbm_uint32 * addr)
{
	int mask,retval;
	unsigned long cpu_sr = atbm_local_irq_save();

	addr += nr >>5;
	mask = 1<<(nr & 0x1f);

	retval = (mask & *addr) != 0;
	*addr &= ~mask;

	atbm_local_irq_restore(cpu_sr);

	return  retval;
}

int atbm_test_bit(int nr,atbm_uint32 * addr)
{
	int mask;

	addr += nr >>5;
	mask = 1<<(nr & 0x1f);

	return  ((mask & *addr) != 0);

}

int atbm_find_first_zero_bit(atbm_uint32 * addr,int size)
{
	int i = 0;

	for (i=0; i<size; i++){
		if(atbm_test_bit(i,addr) == 0){
			return i;
		}
	}

	return -1;
}

