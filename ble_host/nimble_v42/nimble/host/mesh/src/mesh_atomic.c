

#include "mesh_atomic.h"

atomic_val_t mesh_atomic_inc(atomic_t *target)
{
	atomic_val_t ret;

	vPortEnterCritical();
	
    ret = *target;
    (*target)++;	
	
	vPortExitCritical();

	return ret;
}


atomic_val_t mesh_atomic_dec(atomic_t *target)
{
	atomic_val_t ret;
	
	vPortEnterCritical();
	
    ret = *target;
    (*target)--;	
	
	vPortExitCritical();

	return ret;	
}

atomic_val_t mesh_atomic_get(const atomic_t *target)
{
	return *target;
}

atomic_val_t mesh_atomic_set(atomic_t *target, atomic_val_t value)
{
	atomic_val_t ret;
	
	vPortEnterCritical();
	
    ret = *target;
    *target = value;	
	
	vPortExitCritical();

	return ret;	

}

atomic_val_t mesh_atomic_or(atomic_t *target, atomic_val_t value)
{
	atomic_val_t ret;
	
	vPortEnterCritical();
	
    ret = *target;
    *target |= value;	
	
	vPortExitCritical();

	return ret;		
}

atomic_val_t mesh_atomic_xor(atomic_t *target, atomic_val_t value)
{
	atomic_val_t ret;
	
	vPortEnterCritical();
	
    ret = *target;
    *target ^= value;	
	
	vPortExitCritical();

	return ret;	

}

atomic_val_t mesh_atomic_and(atomic_t *target, atomic_val_t value)
{
	atomic_val_t ret;
	
	vPortEnterCritical();
	
    ret = *target;
    *target &= value;	
	
	vPortExitCritical();

	return ret;		
}

atomic_val_t mesh_atomic_nand(atomic_t *target, atomic_val_t value)
{
	atomic_val_t ret;
	
	vPortEnterCritical();
	
    ret = *target;
    *target = ~(*target & value);	
	
	vPortExitCritical();

	return ret;		
}


