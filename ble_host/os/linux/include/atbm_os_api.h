/**************************************************************************************************************
 * altobeam RTOS wifi hmac source code 
 *
 * Copyright (c) 2018, altobeam.inc   All rights reserved.
 *
 *  The source code contains proprietary information of AltoBeam, and shall not be distributed, 
 *  copied, reproduced, or disclosed in whole or in part without prior written permission of AltoBeam.
*****************************************************************************************************************/


#ifndef ATBM_OS_API_H
#define ATBM_OS_API_H

#include "atbm_type.h"
#if ATBM_SDIO_BUS
#include "atbm_os_sdio.h"
#endif
#if ATBM_USB_BUS
#include "atbm_os_usb.h"
#endif
#include "atbm_os_mem.h"

#ifndef atbm_packed
#define atbm_packed __attribute__ ((packed))
#endif //__packed

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3, 10, 0))
struct __una_u16 { atbm_uint16 x; } atbm_packed ;
struct __una_u32  { atbm_uint32 x; } atbm_packed ;

static __INLINE atbm_uint16 __get_unaligned_cpu16(const atbm_void *p)
{
 	const struct __una_u16 *ptr = (const struct __una_u16 *)p;
 	return ptr->x;
}
#endif

static __INLINE atbm_void *strdup(const char *str)
{
	char *tmp = 0;
	unsigned int len = strlen(str);
	tmp = atbm_kmalloc(len+1, GFP_KERNEL);
	if (tmp == 0){
		return 0;
	}

	atbm_memcpy(tmp, str, len+1);
	return tmp;
}

void atbm_ioctl_ble_smt_event_async(uint8_t *event_buffer, uint16_t event_len);
#if ATBM_USB_BUS
int atbm_usb_register_init(void);
int atbm_usb_register_deinit(void);
#elif ATBM_SDIO_BUS
int atbm_sdio_register_init(void);
int atbm_sdio_register_deinit(void);
#endif

#endif //ATBM_OS_API_H
