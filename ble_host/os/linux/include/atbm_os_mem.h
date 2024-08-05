/**************************************************************************************************************
 * altobeam LINUX wifi hmac source code 
 *
 * Copyright (c) 2018, altobeam.inc   All rights reserved.
 *
 *  The source code contains proprietary information of AltoBeam, and shall not be distributed, 
 *  copied, reproduced, or disclosed in whole or in part without prior written permission of AltoBeam.
*****************************************************************************************************************/


#ifndef ATBM_OS_MEM_H
#define ATBM_OS_MEM_H

#include <linux/slab.h>
#include <asm/memory.h>
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/string.h>


void *atbm_kcalloc(size_t n, size_t size);

#define atbm_kmalloc(x,y)   kmalloc(x,y)
#define atbm_kzalloc(x,y)   kzalloc(x,y)  
#define realloc(x,y)        krealloc(x,y,GFP_KERNEL)
#define atbm_calloc         atbm_kcalloc
#define atbm_kfree(x)       kfree(x)           
#define atbm_memcpy         memcpy 
#define atbm_memset         memset 
#define atbm_memmove  		memmove
#define atbm_memcmp 		memcmp 

int atoi(const char *str);

#ifndef min
#define min(a, b) ((a)<(b)?(a):(b))
#endif

#ifndef max
#define max(a, b) ((a)>(b)?(a):(b))
#endif


#ifndef INT8_MIN
#define INT8_MIN	(-128)
#endif

#ifndef INT8_MAX
#define INT8_MAX	(127)
#endif


#ifndef UINT8_MAX
#define UINT8_MAX	0xFF
#endif

#ifndef UINT16_MAX
#define UINT16_MAX	0xFFFF
#endif

#ifndef UINT32_MAX
#define UINT32_MAX	0xFFFFFFFF
#endif

#ifndef INT32_MAX
#define INT32_MAX	0x7FFFFFFF
#endif

#ifndef UINT64_MAX
#define UINT64_MAX	0xFFFFFFFFFFFFFFFF
#endif


#define strtoul 		simple_strtoul
#define strtoull 		simple_strtoull
#define strtol 			simple_strtol

char *strtok(char* str, const char *delimit);

#endif /* ATBM_OS_MEM_H */

