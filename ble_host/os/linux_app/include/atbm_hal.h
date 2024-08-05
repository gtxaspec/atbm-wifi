#ifndef __ATBM_HAL__H__
#define __ATBM_HAL__H__

#include "stdint.h"
#include "stdio.h"
#include "errno.h"

#ifndef __packed
#define __packed    __attribute__((__packed__))
#endif

typedef signed char s8;

typedef unsigned char u8;

typedef signed short s16;

typedef unsigned short u16;

typedef signed int s32;

typedef unsigned int u32;

typedef signed long long s64;

typedef unsigned long long u64;

#include "atbm_type.h"
#include "syscfg/syscfg.h"
#include "atbm_debug.h"


#ifndef SRAM_CODE
#define SRAM_CODE
#endif

#ifndef min
#define min(a, b) ((a)<(b)?(a):(b))
#endif//min

#ifndef max
#define max(a, b) ((a)>(b)?(a):(b))
#endif
#define OS_TICKS_PER_SEC 1000
#define u8 unsigned char
#define atbm_kmalloc(__size,__a) malloc(__size)
#define atbm_kfree(__p) free(__p)
#define atbm_kzalloc(x,y)   calloc(1,x)  
#endif  //__ATBM_HAL__H__