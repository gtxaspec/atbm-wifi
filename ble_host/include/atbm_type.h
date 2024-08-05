/**************************************************************************************************************
 * altobeam RTOS
 *
 * Copyright (c) 2018, altobeam.inc   All rights reserved.
 *
 *  The source code contains proprietary information of AltoBeam, and shall not be distributed, 
 *  copied, reproduced, or disclosed in whole or in part without prior written permission of AltoBeam.
*****************************************************************************************************************/

#ifndef _ATBM_API_TYPE_H_
#define _ATBM_API_TYPE_H_

//#define jiffies 1000
#define ATBM_ALIGN(a,b)			(((a) + ((b) - 1)) & (~((b)-1)))
/*
;*****************************************************************************************************
;* 描	 述 : 定义系统的数据类型。
;*****************************************************************************************************
;*/
typedef unsigned char ATBM_BOOL;
typedef unsigned char atbm_uint8;		/* Unsigned  8 bit quantity							*/
typedef signed char atbm_int8;			/* Signed    8 bit quantity							 */
typedef unsigned short atbm_uint16; 	/* Unsigned 16 bit quantity							  */
typedef signed short atbm_int16;		/* Signed	16 bit quantity 						   */
typedef unsigned int atbm_uint32;		/* Unsigned 32 bit quantity							*/
typedef signed int atbm_int32;			/* Signed   32 bit quantity							 */
typedef float atbm_fp32;				/* Single precision floating point					   */
typedef unsigned long long atbm_uint64;	/* Unsigned  64 bit quantity							*/
typedef long long atbm_int64;			/* Signed    64 bit quantity							 */
typedef void atbm_void;
typedef atbm_uint32   ATBM_OS_CPU_SR;		/* Define size of CPU status register (PSR = 32 bits) */
typedef atbm_int32  atbm_size_t; 
/*ATBM_FALSE & ATBM_TRUE*/
#define ATBM_FALSE	0
#define ATBM_TRUE	1

/*Return value defination*/
#define WIFI_OK				0 	
#define WIFI_ERROR			-1
#ifndef LINUX_OS
#ifndef BIT
#define BIT(x)	(1 << (x))
#endif
#endif
#define NETDEV_ALIGN		32
#define ETHTOOL_BUSINFO_LEN 32


#define atbm_min(_a, _b) 	(((_a) < (_b)) ? (_a) : (_b))
#define atbm_max(_a, _b) 	(((_a) > (_b)) ? (_a) : (_b))


#define ATBM_NULL	((atbm_void *)0)

#define atbm_likely(a) (a)
#define atbm_unlikely(a) (a)

#define atbm_cpu_to_le16(v16) (v16)
#define atbm_cpu_to_le32(v32) (v32)
#define atbm_cpu_to_le64(v64) (v64)
#define atbm_le16_to_cpu(v16) (v16)
#define atbm_le32_to_cpu(v32) (v32)
#define atbm_le64_to_cpu(v64) (v64)

#define __atbm_cpu_to_le16(v16) (v16)
#define __atbm_cpu_to_le64(v64) (v64)
#define __atbm_le16_to_cpu(v16) (v16)
#define __atbm_le32_to_cpu(v32) (v32)

#define atbm_cpu_to_be16(a) atbm_htons(a)
#define atbm_cpu_to_be32(a) atbm_htonl(a)
#define atbm_be16_to_cpu(a) atbm_ntohs(a)
#define atbm_be32_to_cpu(a) atbm_ntohl(a)


#endif /*_API_TYPE_H_*/
