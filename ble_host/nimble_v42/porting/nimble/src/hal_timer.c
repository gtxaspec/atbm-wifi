/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "atbm_os_mem.h"

#include <assert.h>

#include "os/os.h"
//#include "nrfx.h"
#include "hal/hal_timer.h"

#include "dev_int.h"
#include "hw_defs.h"
#include "ble_juno_reg.h"
         

/* IRQ prototype */
typedef void (*hal_timer_irq_handler_t)(void);


extern void ble_rx_timeout_isr(void);
extern void ble_phy_isr(unsigned Priority);

struct atbm_ble_timer atbm_ble_timer0;


void hal_timer_set_ocmp(struct atbm_ble_timer *bsptimer, uint32_t expiry)
{
	uint32_t val;

	if((int)(expiry - HW_READ_REG(BLE_TIMER0_CNT)) < 5){
		printf("timer set over dif:%d\n", (int)(expiry - HW_READ_REG(BLE_TIMER0_CNT)));
		return;
	}

	HW_WRITE_REG(BLE_TIMER0_CC4, expiry);
	
	val = HW_READ_REG(BLE_TIMER_MATCH_EN);
	val |= TIMER0_MATCH4_EN;
	HW_WRITE_REG(BLE_TIMER_MATCH_EN, val);	

	val = HW_READ_REG(BLE_TIMER_MATCH_INT_EN);
	val |= TIMER0_MATCH4_EN;
	HW_WRITE_REG(BLE_TIMER_MATCH_INT_EN, val);	
}

void hal_timer_set_stop(void)
{
	uint32_t val;

	HW_WRITE_REG(BLE_TIMER0_CC4, 0);
	
	val = HW_READ_REG(BLE_TIMER_MATCH_EN);
	val &= ~TIMER0_MATCH4_EN;
	HW_WRITE_REG(BLE_TIMER_MATCH_EN, val);	

	val = HW_READ_REG(BLE_TIMER_MATCH_INT_EN);
	val &= ~TIMER0_MATCH4_EN;
	HW_WRITE_REG(BLE_TIMER_MATCH_INT_EN, val);			
}



/**
 * hal timer chk queue
 *
 *
 * @param bsptimer
 */
NORELOC static void hal_timer_chk_queue(struct atbm_ble_timer *bsptimer)
{
    int32_t diffTime;
    int sr;
    struct hal_timer *timer;

    /* disable interrupts */
	OS_ENTER_CRITICAL(sr);
    while ((timer = TAILQ_FIRST(&bsptimer->hal_timer_q)) != NULL) {
		diffTime = (int32_t)(timer->expiry - hal_timer_read());
		if(diffTime < 20){
            TAILQ_REMOVE(&bsptimer->hal_timer_q, timer, link);
            timer->link.tqe_prev = NULL;
            timer->cb_func(timer->cb_arg);			
		}else{
			break;
		}
    }

    /* Any timers left on queue? If so, we need to set OCMP */
    timer = TAILQ_FIRST(&bsptimer->hal_timer_q);
    if (timer) {
		hal_timer_set_ocmp(bsptimer, timer->expiry);
    } else {
        hal_timer_set_stop();     
    }
	OS_EXIT_CRITICAL(sr);
}


NORELOC static void hal_timer_call_direct(struct hal_timer *timer)
{
	int sr;
	OS_ENTER_CRITICAL(sr);
	timer->cb_func(timer->cb_arg);	
	OS_EXIT_CRITICAL(sr);
}


/**
 * hal timer irq handler
 *
 * Generic HAL timer irq handler.
 *
 * @param tmr
 */
/**
 * hal timer irq handler
 *
 * This is the global timer interrupt routine.
 *
 */

void hal_ble_timer_irq_handler(uint32_t IrqPriority, uint32_t irq_en)
{
	uint32_t val;
	int32_t diff;
	
	val = HW_READ_REG(BLE_TIMER_MATCH_FLAG);
	HW_WRITE_REG(BLE_TIMER_MATCH_FLAG, val);
	//HW_WRITE_REG(BLE_TIMER_MATCH_FLAG, 0);

	if(val&TIMER0_MATCH4_EN){
		if((int)(HW_READ_REG(BLE_TIMER0_CNT)-HW_READ_REG(BLE_TIMER0_CC4)) > 1000){
			iot_printf("timer irq too late, CNT:%X, CC4:%X\n", 
					HW_READ_REG(BLE_TIMER0_CNT), HW_READ_REG(BLE_TIMER0_CC4));
		}
		hal_timer_chk_queue(&atbm_ble_timer0);
	}
	
	if(val&TIMER0_MATCH5_EN){
		//iot_printf("rx st to\n");
		//iot_printf("rx start timeout,%d\n", hal_timer_read());
		if(!(irq_en & BIT_MATCH_INT)){
			ble_rx_timeout_isr();
		}	
	}
	
	if(val&TIMER0_MATCH6_EN){
		//iot_printf("rx end timeout,%d\n", hal_timer_read());
		ble_rx_timeout_isr();
	}

}


/**
 * hal timer init
 *
 * Initialize platform specific timer items
 *
 * @param timer_num     Timer number to initialize
 * @param cfg           Pointer to platform specific configuration
 *
 * @return int          0: success; error code otherwise
 */
int hal_timer_init(int timer_num, void *cfg)
{
	u32 val;
	
	val = HW_READ_REG(BLE_TIMER0_CTRL);
	val = TIMER_START | TIMER_CLR | TIMER_PRES_1M; 
	HW_WRITE_REG(BLE_TIMER0_CTRL, val);	

	val = HW_READ_REG(BLE_TIMER_MATCH_INT_EN);
	val |= LL_INT_EN;
	HW_WRITE_REG(BLE_TIMER_MATCH_INT_EN, val);	
	
	IRQ_RegisterHandler(DEV_INT_NUM_BLE, ble_phy_isr);

	return 0;
}

/**
 * hal timer config
 *
 * Configure a timer to run at the desired frequency. This starts the timer.
 *
 * @param timer_num
 * @param freq_hz
 *
 * @return int
 */
int hal_timer_config(int timer_num, uint32_t freq_hz)
{
	return 0;
}

/**
 * hal timer deinit
 *
 * De-initialize a HW timer.
 *
 * @param timer_num
 *
 * @return int
 */
int hal_timer_deinit(int timer_num)
{
	u32 val;
	
	val = HW_READ_REG(BLE_TIMER0_CTRL);
	val = TIMER_STOP | TIMER_CLR; 
	HW_WRITE_REG(BLE_TIMER0_CTRL, val);

	val = HW_READ_REG(BLE_TIMER_MATCH_EN);
	val &= ~TIMER0_MATCH4_EN;
	HW_WRITE_REG(BLE_TIMER_MATCH_EN, val);	
	
	val = HW_READ_REG(BLE_TIMER_MATCH_INT_EN);
	val &= ~TIMER0_MATCH4_EN;
	HW_WRITE_REG(BLE_TIMER_MATCH_INT_EN, val);		//use time0_match4 irq en

	return 0;
}


/**
 * hal timer read
 *
 * Returns the timer counter. NOTE: if the timer is a 16-bit timer, only
 * the lower 16 bits are valid. If the timer is a 64-bit timer, only the
 * low 32-bits are returned.
 *
 * @return uint32_t The timer counter register.
 */
uint32_t hal_timer_read(void)
{
    return HW_READ_REG(BLE_TIMER0_CNT);
}

/**
 * hal timer delay
 *
 * Blocking delay for n ticks
 *
 * @param timer_num
 * @param ticks
 *
 * @return int 0 on success; error code otherwise.
 */
int hal_timer_delay(int timer_num, uint32_t ticks)
{
    uint32_t until;

    until = hal_timer_read() + ticks;
    while ((int32_t)(hal_timer_read() - until) <= 0) {
        /* Loop here till finished */
    }

    return 0;
}

/**
 *
 * Initialize the HAL timer structure with the callback and the callback
 * argument. Also initializes the HW specific timer pointer.
 *
 * @param cb_func
 *
 * @return int
 */
int hal_timer_set_cb(int timer_num, struct hal_timer *timer, hal_timer_cb cb_func,void *arg)
{
    timer->cb_func = cb_func;
    timer->cb_arg = arg;
    timer->link.tqe_prev = NULL;
    timer->bsp_timer = &atbm_ble_timer0;
	
    return 0;

}

int hal_timer_start_at(struct hal_timer *timer, uint32_t tick)
{
    int sr;
    struct hal_timer *entry;
    struct atbm_ble_timer *bsptimer;
	int32_t diffTime;
	uint32_t curTime;

    if ((timer == NULL) || (timer->link.tqe_prev != NULL) ||
        (timer->cb_func == NULL)) {
        return EINVAL;
    }
    bsptimer = (struct atbm_ble_timer *)timer->bsp_timer;
    timer->expiry = tick;

	curTime = hal_timer_read();
	diffTime = (int32_t)(timer->expiry - curTime);
	if(diffTime < 20){	//direct call in -100us ~ 10us
		hal_timer_call_direct(timer);
		return 0;
	}

    OS_ENTER_CRITICAL(sr);

    if (TAILQ_EMPTY(&bsptimer->hal_timer_q)) {
        TAILQ_INSERT_HEAD(&bsptimer->hal_timer_q, timer, link);
    } else {
        TAILQ_FOREACH(entry, &bsptimer->hal_timer_q, link) {
			if(((entry->expiry > curTime) && (timer->expiry > curTime))
				|| ((entry->expiry <= curTime) && (timer->expiry <= curTime))){
	            if ((int32_t)(timer->expiry - entry->expiry) < 0) {
	                TAILQ_INSERT_BEFORE(entry, timer, link);
	                break;
	            }				
			}else {
	            if ((int32_t)(timer->expiry - entry->expiry) > 0) {
	                TAILQ_INSERT_BEFORE(entry, timer, link);
	                break;
	            }
			}
        }
        if (!entry) {
            TAILQ_INSERT_TAIL(&bsptimer->hal_timer_q, timer, link);
        }
    }

    /* If this is the head, we need to set new OCMP */
    if (timer == TAILQ_FIRST(&bsptimer->hal_timer_q)) {
        hal_timer_set_ocmp(bsptimer, timer->expiry);
    }

    OS_EXIT_CRITICAL(sr);

    return 0;
}


/**
 * hal timer stop
 *
 * Stop a timer.
 *
 * @param timer
 *
 * @return int
 */
int hal_timer_stop(struct hal_timer *timer)
{

	int sr;
	int32_t reset_ocmp;
	int32_t diffTime;
    struct hal_timer *entry;
    struct atbm_ble_timer *bsptimer;

    if (timer == NULL) {
        return EINVAL;
    }

   	bsptimer = (struct atbm_ble_timer *)timer->bsp_timer;

   	OS_ENTER_CRITICAL(sr);

    if (timer->link.tqe_prev != NULL) {
        reset_ocmp = 0;
        if (timer == TAILQ_FIRST(&bsptimer->hal_timer_q)) {
            /* If first on queue, we will need to reset OCMP */
            entry = TAILQ_NEXT(timer, link);
            reset_ocmp = 1;
        }
        TAILQ_REMOVE(&bsptimer->hal_timer_q, timer, link);
        timer->link.tqe_prev = NULL;
        if (reset_ocmp) {
			while ((entry = TAILQ_FIRST(&bsptimer->hal_timer_q)) != NULL) {
				diffTime = (int32_t)(entry->expiry - hal_timer_read());
				if(diffTime < 20){
					TAILQ_REMOVE(&bsptimer->hal_timer_q, entry, link);
					entry->link.tqe_prev = NULL;
					entry->cb_func(entry->cb_arg);			
				}else{
					break;
				}
			}
            if (entry) {
				hal_timer_set_ocmp(entry->bsp_timer, entry->expiry);
            } else {
                hal_timer_set_stop();          
            }
        }
    }

    OS_EXIT_CRITICAL(sr);

    return 0;
}
