/**************************************************************************************************************
 * altobeam RTOS
 *
 * Copyright (c) 2018, altobeam.inc   All rights reserved.
 *
 *  The source code contains proprietary information of AltoBeam, and shall not be distributed, 
 *  copied, reproduced, or disclosed in whole or in part without prior written permission of AltoBeam.
*****************************************************************************************************************/

#ifndef ATBM_SYSOPS_H_
#define ATBM_SYSOPS_H_

#ifndef LINUX_OS
#define GFP_ATOMIC 0
#define GFP_KERNEL 1
#define GFP_DMA 2
#endif

#include "os_api.h"

#include "atbm_os_api.h"
#if ATBM_USB_BUS
#include "atbm_os_usb.h"
#endif
#if ATBM_SDIO_BUS
#include "atbm_os_sdio.h"
#endif
#include "atbm_list.h"
//#include "atbm_os_timer.h"

#include "atbm_os_thread.h"
#include "atbm_os_mutex.h"
#include "atbm_os_spinlock.h"
#include "atbm_os_skbuf.h"
#include "atbm_os_mem.h"
#include "atbm_os_atomic.h"
#include "atbm_os_timer.h"
#include "atbm_os_msgQ.h"

#if USE_MAIL_BOX
#include "atbm_os_mailbox.h"
#endif


 #endif//ATBM_SYSOPS_H_
