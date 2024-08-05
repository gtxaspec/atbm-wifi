/**************************************************************************************************************
 * altobeam RTOS wifi hmac source code 
 *
 * Copyright (c) 2018, altobeam.inc   All rights reserved.
 *
 *  The source code contains proprietary information of AltoBeam, and shall not be distributed, 
 *  copied, reproduced, or disclosed in whole or in part without prior written permission of AltoBeam.
*****************************************************************************************************************/

#include "atbm_hal.h"
#include "atbm_os_mem.h"

int atoi(const char *str)
{
	char *after;
	return simple_strtol(str,&after,10);
}

void *atbm_kcalloc(size_t n, size_t size)
{
	return kcalloc(n,size,GFP_KERNEL);
}

char *strtok(char* str, const char *delimit)
{
    static char *tmp = NULL;
    char *ret = NULL;
	char *p = NULL;
	int i;
	
    if(delimit == NULL){
		return str;
	}
	
    if(str != NULL){
		tmp = str;
	}
        
    if(tmp == NULL){
		return NULL;
	}
	
    ret = tmp;
    p = strstr(tmp, delimit);
    if(p != NULL){
        tmp = p + strlen(delimit);
        for(i = 0; i < strlen(delimit); i ++){
            *(p + i) = '\0';
        }
    } 
	else{        
        tmp = NULL;
    }
    return ret;
}


