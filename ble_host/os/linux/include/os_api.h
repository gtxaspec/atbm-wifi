#ifndef OS_API_H
#define OS_API_H

#include "atbm_type.h"
#include <linux/random.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
#define prandom_u32 random32
#endif /* LINUX_3_10_COMPAT_H */


#define __INLINE        inline
#define iot_printf      printk
#define atbm_random()   prandom_u32()

#define TargetUsb_lmac_start()

atbm_uint32 atbm_os_random(void);
#define ZEROSIZE 0

#endif //OS_API_H

