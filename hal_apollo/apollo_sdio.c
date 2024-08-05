/*
 * Mac80211 SDIO driver for altobeam APOLLO device
 * *
 * Copyright (c) 2016, altobeam
 * Author:
 *
 * Based on apollo code Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
 #define DEBUG 1
//#undef CONFIG_ATBM_APOLLO_USE_GPIO_IRQ 
#include <linux/version.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/spinlock.h>
#include <net/atbm_mac80211.h>
#include <linux/kthread.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/freezer.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>



#include "apollo.h"
#include "sbus.h"
#include "apollo_plat.h"
#include "debug.h"
#include "hwio.h"
#include "svn_version.h"
#include "module_fs.h"
#include "bh.h"
#include "mac80211/ieee80211_i.h"
#include "internal_cmd.h"

struct build_info{
	int ver;
	int dpll;
	char driver_info[64];
};
#define __PRINT_VALUE(x) #x
#define PRINT_VALUE(x) #x"="__PRINT_VALUE(x)

#ifdef CONFIG_ATBM_SUPPORT_SG
#pragma message("Support Network SG")
#endif

#ifdef CONFIG_MODDRVNAME
#define WIFI_MODDRVNAME CONFIG_MODDRVNAME
#pragma message(WIFI_MODDRVNAME)

#else
#define WIFI_MODDRVNAME "atbm_wlan"
#endif
#ifdef CONFIG_SDIOVID
#define WIFI_SDIO_VID CONFIG_SDIOVID
#pragma message(PRINT_VALUE(WIFI_SDIO_VID))
#else
#define WIFI_SDIO_VID 0x007a
#endif

#ifdef CONFIG_SDIOPID
#define WIFI_SDIO_PID CONFIG_SDIOPID
#pragma message(PRINT_VALUE(WIFI_SDIO_PID))

#else
#define WIFI_SDIO_PID 0x6011
#endif

#ifdef CONFIG_PLFDEVNAME
#define WIFI_PLFDEVNAME CONFIG_PLFDEVNAME
#pragma message(WIFI_PLFDEVNAME)

#else
#define WIFI_PLFDEVNAME "atbm_dev_wifi"
#endif

#ifdef CONFIG_ATBM_BLE
int atbm_ioctl_add(void);
void atbm_ioctl_free(void);
#endif

extern int atbm_bh_read_ctrl_reg_unlock(struct atbm_common *hw_priv,
					  u16 *ctrl_reg);
static void atbm_sdio_release_err_cmd(struct atbm_common	*hw_priv);
static void atbm_sdio_lock(struct sbus_priv *self);
static void atbm_sdio_unlock(struct sbus_priv *self);


//const char DRIVER_INFO[]={"[====="__DATE__" "__TIME__"""=====]"};
const char DRIVER_INFO[]={"[====="" """"=====]"};
static int driver_build_info(void)
{
	struct build_info build;
	build.ver=DRIVER_VER;
	if (DPLL_CLOCK==1)
		build.dpll=40;
	else if(DPLL_CLOCK==2)
		build.dpll=24;
	else
		build.dpll=26;
	memcpy(build.driver_info,(void*)DRIVER_INFO,sizeof(DRIVER_INFO));
	atbm_printk_init("SVN_VER=%d,DPLL_CLOCK=%d,BUILD_TIME=%s\n",build.ver,build.dpll,build.driver_info);

#if (PROJ_TYPE==CRONUS)
	atbm_printk_init("----drvier support chip Cronus \n");	
#endif

	return 0;
}
enum{
	THREAD_WAKEUP,
	THREAD_SHOULD_SUSPEND,
	THREAD_SUSPENED,
	THREAD_SHOULD_STOP,
};
struct atbm_sdio_thread
{
	const char *name;
	struct task_struct		__rcu *thread;
	unsigned long			flags;
	unsigned long           wakeup_period;
	struct completion 		suspended;
	int (*thread_fn)(void *priv);
	int (*period_handle)(struct atbm_sdio_thread *thread);
	struct sbus_priv *self;
};
#ifndef CONFIG_ATBM_SDIO_TX_PROCESS_NORMAL
enum sdio_encap_state
{
	SDIO_ENCAP_FREE,
	SDIO_ENCAP_TX_THREAD,
	SDIO_ENCAP_XMIT_PATH,
	SDIO_ENCAP_WSMC_PATH,
	SDIO_ENCAP_TRY_WAKEUP_THREAD,
	SDIO_ENCAP_WAITING_THREAD_RUN,
	SDIO_ENCAP_ENCAP_EXIT,
	SDIO_ENCAP_LIST_EMPTY,
	SDIO_ENCAP_FREE_LIST,
	SDIO_ENCAP_BH_WAKEUP,
	SDIO_ENCAP_SUSPEND,
};
#ifndef CONFIG_SDIO_TX_PATH_PREEMPT
#define SDIO_ENCAP_WAKEUP_TXTHREAD_PRO	SDIO_ENCAP_WAITING_THREAD_RUN
#else
#define SDIO_ENCAP_WAKEUP_TXTHREAD_PRO	SDIO_ENCAP_FREE
#endif
#define SDIO_SCATTERLIST_CAP			(16)
#define SDIO_SG_CAP						(20)
#define SDIO_SCATTERLIST_CACHE_SIZE		(24*1024)
#define SDIO_CACHE_SIZE					(SDIO_SCATTERLIST_CACHE_SIZE * SDIO_SCATTERLIST_CAP)

struct sdio_scatterlist
{
	int sglen[SDIO_SG_CAP];
	char *sgs[SDIO_SG_CAP];
	char *cnfsg;
	int num_sgs;
	int totallen;
	int sg_in;
	int sg_out;
	char *cache;
	struct list_head head;
};
struct sdio_cache
{
	char *cache;
	int cache_size;
};
#endif
struct sbus_priv {
	struct sdio_func	*func;
	struct atbm_common	*core;
	struct atbm_sdio_thread tx_thread;
	struct atbm_sdio_thread rx_thread;
	const struct atbm_platform_data *pdata;
	spinlock_t		lock;
	spinlock_t		bh_lock;
	sbus_irq_handler	irq_handler;
	sbus_irq_handler	irq_handler_suspend;
	int 			atbm_bgf_irq;
	int 			oob_irq_enabled;
	void			*irq_priv;
	void            *irq_priv_suspend;	
#ifndef CONFIG_ATBM_SDIO_TX_PROCESS_NORMAL
	struct atbm_sdio_thread sg_thread;
	struct list_head	sgs_free;
	struct list_head	sgs_pending;
	struct sdio_scatterlist *sgs_poll;
	struct sdio_cache cache;
	spinlock_t xmit_path_lock;
	bool   flushing_sg;
	enum sdio_encap_state encap_state;
#endif
};
static const struct sdio_device_id atbm_sdio_ids[] = {
	{ SDIO_DEVICE(WIFI_SDIO_VID, WIFI_SDIO_PID) },
	{ SDIO_DEVICE(WIFI_SDIO_VID, 0x6011) },
	{ /* end: all zeroes */			},
};

static int  atbm_sdio_init(void);
static void  atbm_sdio_exit(void);
extern 	int atbm_plat_request_gpio_irq(const struct atbm_platform_data *pdata,struct sbus_priv *self,int * atbm_bgf_irq);
extern 	void atbm_plat_free_gpio_irq(const struct atbm_platform_data *pdata,struct sbus_priv *self,int atbm_bgf_irq);
static int atbm_sdio_reset_chip(struct sbus_priv *self);
extern void atbm_sdio_rx_bh(struct atbm_common *hw_priv);
#ifdef CONFIG_ATBM_SDIO_TX_PROCESS_NORMAL
extern void atbm_sdio_tx_bh(struct atbm_common *hw_priv);
#endif
extern int atbm_bh_read_ctrl_reg(struct atbm_common *hw_priv,
					  u16 *ctrl_reg);
static void atbm_sdio_miss_irq(struct sbus_priv *self);

static struct task_struct *atbm_kthread_get(struct atbm_sdio_thread *thread)
{
	struct task_struct *bh = NULL;
	
	rcu_read_lock();
	bh = rcu_dereference(thread->thread);
	if(bh){
		get_task_struct(bh);
	}
	rcu_read_unlock();

	return bh;
}

static void atbm_kthread_put(struct task_struct *bh)
{
	put_task_struct(bh);
}
static int atbm_kthread_try_suspend(struct atbm_sdio_thread *thread)
{	
	struct task_struct *bh = atbm_kthread_get(thread);
	
	if(bh == NULL)
		goto exit;
	
	if(test_bit(THREAD_SHOULD_STOP,&thread->flags))
		goto exit;
	
	if (!test_bit(THREAD_SUSPENED, &thread->flags)) {
		
		set_bit(THREAD_SHOULD_SUSPEND, &thread->flags);
		
		if(bh != current){
			wake_up_process(bh);
			/*
			*set timeout is safe
			*/
			wait_for_completion_timeout(&thread->suspended,msecs_to_jiffies(1000));
		}
	}
exit:	
	if(bh)
		atbm_kthread_put(bh);
	return 0;
}
static void atbm_kthread_resume(struct atbm_sdio_thread *thread)
{
	struct task_struct *bh = atbm_kthread_get(thread);
	
	if(bh == NULL){
		return;
	}
	
	clear_bit(THREAD_SHOULD_SUSPEND, &thread->flags);
	if (test_and_clear_bit(THREAD_SUSPENED, &thread->flags)) {		
		wake_up_process(bh);
	}
	
	atbm_kthread_put(bh);
}

static int atbm_kthread_should_stop(struct atbm_sdio_thread *thread)
{
	if(!kthread_should_stop()){
		return 0;
	}
	set_bit(THREAD_SHOULD_STOP,&thread->flags);
	if(test_bit(THREAD_SHOULD_SUSPEND, &thread->flags)) {
		if (!test_and_set_bit(THREAD_SUSPENED, &thread->flags))
			complete(&thread->suspended);
	}

	return 1;
}

static void atbm_kthread_into_suspend(struct atbm_sdio_thread *thread)
{
	__set_current_state(TASK_INTERRUPTIBLE);
	while (test_bit(THREAD_SHOULD_SUSPEND, &thread->flags)) {
		if (!test_and_set_bit(THREAD_SUSPENED, &thread->flags))
			complete(&thread->suspended);	
		if(kthread_should_stop()){
			set_bit(THREAD_SHOULD_STOP,&thread->flags);
			clear_bit(THREAD_SHOULD_SUSPEND, &thread->flags);
			break;
		}else {
			schedule();
		}
		__set_current_state(TASK_INTERRUPTIBLE);
	}
	clear_bit(THREAD_SUSPENED, &thread->flags);
	__set_current_state(TASK_INTERRUPTIBLE);
}

static signed long atbm_schedule_timeout(struct atbm_sdio_thread *thread)
{
	signed long timeout = schedule_timeout(thread->wakeup_period);

	if (timeout == 0 && thread->period_handle){
		thread->period_handle(thread);
	}
	return timeout;
}


static int atbm_sdio_wait_action(struct atbm_sdio_thread *thread)
{
	unsigned long idle_period = thread->wakeup_period;
	unsigned long period = idle_period;
wake:
	period = idle_period;
	set_current_state(TASK_INTERRUPTIBLE);	
	while (!atbm_kthread_should_stop(thread)) {
		if (test_and_clear_bit(THREAD_WAKEUP,
				       &thread->flags)) {
			__set_current_state(TASK_RUNNING);
			return 0;
		}
		/*
		*before calling schedule_timeout,do not change 
		*current thread state. if not and current thread
		*state has been change,please let your code goto
		*"wake" like the THREAD_SHOULD_SUSPEND process.
		*/
		else if(test_bit(THREAD_SHOULD_SUSPEND,&thread->flags)){
			
			atbm_printk_pm("%s: go to suspend...\n",__func__);
			atbm_kthread_into_suspend(thread);
			atbm_printk_pm("%s: exit from suspend...\n",__func__);
			goto wake;
		}
		if (!atbm_kthread_should_stop(thread))
			period = atbm_schedule_timeout(thread);
		set_current_state(TASK_INTERRUPTIBLE);
		
	}
	__set_current_state(TASK_RUNNING);
	return -1;
}

static int atbm_sdio_irq_period(struct atbm_sdio_thread *thread)
{
	int ret = 0;
	u16 ctrl_reg = 0;
	struct sbus_priv *self = (struct sbus_priv *)thread->self;
	struct atbm_common *hw_priv = self->core;

	atbm_printk_once("rx timeout\n");

#ifdef CONFIG_SDIO_IRQ_THREAD_PROCESS_DATA
#ifdef CONFIG_SDIO_THREAD_PROCESS_MAC80211_DATA
	if(!atbm_skb_queue_empty(&hw_priv->rx_frame_queue)){
		set_bit(THREAD_WAKEUP,&thread->flags);
		ret = 1;
		goto exit;
	}
#endif
#endif
	/*
	*check sdio irq thread has process the irq;
	*/
	if(test_bit(THREAD_WAKEUP,&thread->flags)){
		ret = 1;
		goto exit;
	}
	
	hw_priv->sbus_ops->lock(hw_priv->sbus_priv);
	if(atbm_bh_is_term(hw_priv)){
		ret = 1;
		hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);
		goto exit;
	}
	atbm_bh_read_ctrl_reg_unlock(hw_priv, &ctrl_reg);	
	
	if(ctrl_reg & ATBM_HIFREG_CONT_NEXT_LEN_MASK){
		__set_current_state(TASK_RUNNING);
		atbm_printk_limit("%s:Miss\n",__func__);
		atbm_sdio_miss_irq(hw_priv->sbus_priv);
		ret = 1;
	}
	hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);
exit:
	return ret;
}
static int atbm_sdio_rx_thread(void *priv)
{
	struct sbus_priv *self = (struct sbus_priv *)priv;
#ifdef CONFIG_ATBM_SDIO_IRQ_HIGH
	struct sched_param param = { .sched_priority = 1 };
#else
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO/2 };
#endif
	/*
	*the policy of the sheduler is same with the sdio irq thread
	*/
#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 9, 0))
    sched_set_fifo(current);
#else
    sched_setscheduler(current, SCHED_FIFO, &param);
#endif
	atbm_printk_init("%s\n",__func__);

	while(!atbm_sdio_wait_action(&self->rx_thread)){
		atbm_sdio_rx_bh(self->core);
	};
	atbm_printk_init("%s:exit\n",__func__);
	return 0;
}
static int atbm_sdio_tx_period(struct atbm_sdio_thread *thread)
{
#if 0
	struct sbus_priv *self = (struct sbus_priv *)thread->self;
	atbm_printk_err("%s:[%d][%d][%d][%d]\n",__func__,self->owner,self->flushing_sg,
					list_empty(&self->sgs_free),list_empty(&self->sgs_pending));
#endif
	return 1;
}
static int atbm_sdio_thread_init(struct atbm_sdio_thread *thread)
{
	void *bh;
	struct sbus_priv *self = thread->self;
	
	bh = kthread_create(thread->thread_fn,self, thread->name);
	if (IS_ERR(bh)){
		thread->thread = NULL;
		atbm_printk_err("sdio %s err\n",thread->name);
		return -1;
	}else {
		spin_lock_bh(&self->bh_lock);
		rcu_assign_pointer(thread->thread,bh);
		spin_unlock_bh(&self->bh_lock);
		init_completion(&thread->suspended);
	}

	return 0;
}

static int atbm_sdio_thread_deinit(struct atbm_sdio_thread *thread)
{
	void *bh;
	struct sbus_priv *self = thread->self;

	atbm_kthread_try_suspend(thread);
	
	set_bit(THREAD_SHOULD_STOP,&thread->flags);
	spin_lock_bh(&self->bh_lock);
	bh = rcu_dereference(thread->thread);
	rcu_assign_pointer(thread->thread,NULL);
	spin_unlock_bh(&self->bh_lock);
	if (bh){
		synchronize_rcu();
		kthread_stop(bh);
	}

	return 0;
}

static int atbm_sdio_thread_wakeup(struct atbm_sdio_thread *thread)
{
	void *bh;
	rcu_read_lock();
	if(test_and_set_bit(THREAD_WAKEUP, &thread->flags) == 0){
		bh = rcu_dereference(thread->thread);
		if(bh){			
			wake_up_process((struct task_struct *)bh);
		}
	}
	rcu_read_unlock();
	return 0;
}

#define MAX_POOL_BUFF_NUM	4
bool atbm_sdio_wait_enough_space(struct atbm_common	*hw_priv,u32 n_needs)
{
#define MAX_LOOP_POLL_CNT  (2*3000)
	u32 hw_xmited = 0;
	bool enough = false;
	int ret = 0;
	int loop = 0;
	u32 print = 0;

	spin_lock_bh(&hw_priv->tx_com_lock);
	enough = hw_priv->hw_bufs_free >= n_needs ? true : false;
	spin_unlock_bh(&hw_priv->tx_com_lock);
	
	while(enough == false){
		
		if(atbm_bh_is_term(hw_priv)){
			atbm_printk_err("%s:bh term\n",__func__);
			return false;
		}
#ifndef CONFIG_ATBM_SDIO_TX_HOLD
		hw_priv->sbus_ops->lock(hw_priv->sbus_priv);
#endif
		ret = atbm_direct_read_unlock(hw_priv,hw_priv->wsm_caps.NumOfHwXmitedAddr,&hw_xmited);
#ifndef CONFIG_ATBM_SDIO_TX_HOLD
		hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);
#endif

		if(ret){
			enough = false;
			break;
		}

		spin_lock_bh(&hw_priv->tx_com_lock);
		if((int)hw_priv->n_xmits < (int)hw_xmited ||
		   (int)(hw_priv->n_xmits - hw_xmited) > hw_priv->wsm_caps.numInpChBufs ||
		   (int)(hw_priv->n_xmits - hw_xmited)<0){
		   	enough = false;
		}else {
			hw_priv->hw_xmits = hw_xmited;
			hw_priv->hw_bufs_free =  (hw_priv->wsm_caps.numInpChBufs) - 
									 (hw_priv->n_xmits-hw_xmited);
			hw_priv->hw_bufs_free_init = hw_priv->hw_bufs_free;
			enough = hw_priv->hw_bufs_free >= n_needs ? true : false;
		}
		spin_unlock_bh(&hw_priv->tx_com_lock);

		if(enough == false){
			loop ++;
			if(loop % MAX_POOL_BUFF_NUM)
				continue;
			if(loop>=MAX_LOOP_POLL_CNT)
				break;
			if((loop >= 3)&&(print == 0)){			
				atbm_printk_debug("%s:n_xmits(%d),hw_xmited(%d),need(%d)\n",__func__,
					hw_priv->n_xmits,hw_xmited,n_needs);
				print = 1;
			}
#ifdef CONFIG_ATBM_SDIO_TX_HOLD
			hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);
#endif
			schedule_timeout_interruptible(msecs_to_jiffies(2));
#ifdef CONFIG_ATBM_SDIO_TX_HOLD
			hw_priv->sbus_ops->lock(hw_priv->sbus_priv);
#endif
		}
	}

	return enough;
}

bool atbm_sdio_have_enough_space(struct atbm_common	*hw_priv,u32 n_needs)
{
	u32 hw_xmited = 0;
	bool enough = false;
	int ret = 0;
	int n_pools = 0;
	
	spin_lock_bh(&hw_priv->tx_com_lock);
	enough = hw_priv->hw_bufs_free >= n_needs ? true : false;
	spin_unlock_bh(&hw_priv->tx_com_lock);

	if(enough == false){
		
		if(atbm_bh_is_term(hw_priv)){
			atbm_printk_err("%s:bh term\n",__func__);
			spin_lock_bh(&hw_priv->tx_com_lock);
			hw_priv->hw_bufs_free = n_needs;
			spin_unlock_bh(&hw_priv->tx_com_lock);
			return true;
		}
		
pool_buffs:
		n_pools ++;		
#ifndef CONFIG_ATBM_SDIO_TX_HOLD	
		hw_priv->sbus_ops->lock(hw_priv->sbus_priv);
#endif
		ret = atbm_direct_read_unlock(hw_priv,hw_priv->wsm_caps.NumOfHwXmitedAddr,&hw_xmited);
#ifndef CONFIG_ATBM_SDIO_TX_HOLD
		hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);
#endif
		if(ret){
			return false;			
		}
		
		spin_lock_bh(&hw_priv->tx_com_lock);
		if((int)hw_priv->n_xmits < (int)hw_xmited ||
		   (int)(hw_priv->n_xmits - hw_xmited) > hw_priv->wsm_caps.numInpChBufs ||
		   (int)(hw_priv->n_xmits - hw_xmited)<0){
		   	enough = false;
		}else {
			hw_priv->hw_xmits = hw_xmited;
			hw_priv->hw_bufs_free =  (hw_priv->wsm_caps.numInpChBufs) - 
									 (hw_priv->n_xmits-hw_xmited);
			hw_priv->hw_bufs_free_init = hw_priv->hw_bufs_free;
			enough = hw_priv->hw_bufs_free >= n_needs ? true : false;
		}
		spin_unlock_bh(&hw_priv->tx_com_lock);
		
		if((enough == false) && (n_pools%MAX_POOL_BUFF_NUM)){
			goto pool_buffs;
		}
	}

	return enough;
}

#ifdef CONFIG_ATBM_SDIO_TX_PROCESS_NORMAL
#pragma message("sdio normal process")
static int atbm_sdio_tx_thread(void *priv)
{
	struct sbus_priv *self = (struct sbus_priv *)priv;
#ifdef CONFIG_ATBM_SDIO_TX_THREAD_FIFO

#ifdef CONFIG_ATBM_SDIO_SMP
	struct sched_param param = { .sched_priority = 1 };
#else
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO/2 + 1 };
#endif

	atbm_printk_init("%s\n",__func__);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 9, 0))
    sched_set_fifo(current);
#else
    sched_setscheduler(current, SCHED_FIFO, &param);
#endif
#endif
	while(!atbm_sdio_wait_action(&self->tx_thread)){
#ifdef CONFIG_ATBM_SDIO_TX_HOLD	
		atbm_sdio_lock(self);
#endif
		atbm_sdio_tx_bh(self->core);
#ifdef CONFIG_ATBM_SDIO_TX_HOLD	
		atbm_sdio_unlock(self);
#endif
	}
	atbm_printk_init("%s:exit\n",__func__);
	atbm_sdio_release_err_cmd(self->core);
	return 0;
}

static int atbm_sdio_xmit_init(struct sbus_priv *self)
{
	struct atbm_common *hw_priv = self->core;	
	struct atbm_sdio_thread *thread = &self->tx_thread;
	atbm_printk_init("atbmwifi INIT_WORK enable\n");
	
	thread->flags = 0;
	thread->name  = ieee80211_alloc_name(hw_priv->hw,"sdio_tx");
	thread->period_handle = atbm_sdio_tx_period;
	thread->thread_fn = atbm_sdio_tx_thread;
	thread->self = self;
	thread->wakeup_period = msecs_to_jiffies(1000);

	if(atbm_sdio_thread_init(thread)){
		return -1;
	}
	
	hw_priv->xmit_buff = atbm_kzalloc(SDIO_TX_MAXLEN, GFP_KERNEL);

	if(hw_priv->xmit_buff == NULL){
		return -1;
	}
	
	return 0;
}
static int atbm_sdio_xmit_deinit(struct sbus_priv *self)
{
	atbm_printk_exit("atbm_sdio_xmit_deinit\n");

	atbm_sdio_thread_deinit(&self->tx_thread);
	
	if(self->core->xmit_buff){
		atbm_kfree(self->core->xmit_buff);
		self->core->xmit_buff = NULL;
	}	
	return 0;
}

static int atbm_sdio_xmit_schedule(struct sbus_priv *self)
{
	return atbm_sdio_thread_wakeup(&self->tx_thread);

}
#else

void atbm_sdio_thread_state(struct sbus_priv *self)
{
	int n_pendings = 0;
	int n_frees = 0;
	struct sdio_scatterlist *sgl;
	
	spin_lock_bh(&self->xmit_path_lock);
	atbm_printk_err("%s:[%d][%d][%d][%d]\n",__func__,list_empty(&self->sgs_free),
					list_empty(&self->sgs_pending),self->flushing_sg,self->encap_state);
	atbm_printk_err("[%s]:[%lx]\n",self->tx_thread.name,self->tx_thread.flags);
	atbm_printk_err("[%s]:[%lx]\n",self->sg_thread.name,self->sg_thread.flags);
	list_for_each_entry(sgl, &self->sgs_pending, head){
		n_pendings ++;
	}
	atbm_printk_err("sgs_pending(%d)\n",n_pendings);
	list_for_each_entry(sgl, &self->sgs_free, head){
		n_frees ++;
	}
	atbm_printk_err("sgs_free(%d)\n",n_frees);
	atbm_printk_err("queue pending[%d][%d]\n",self->core->tx_queue_stats.num_queued[0],self->core->tx_queue_stats.num_queued[1]);
	spin_unlock_bh(&self->xmit_path_lock);
}
static void  atbm_sdio_sched_sg_pending(struct sbus_priv *self,struct sdio_scatterlist *sgl)
{
	
	spin_lock_bh(&self->xmit_path_lock);
	/*
	*move to pending queue;
	*/
	list_move_tail(&sgl->head, &self->sgs_pending);
	
	if(self->flushing_sg == false){
		/*
		*wakeup sg thread
		*/
		atbm_sdio_thread_wakeup(&self->sg_thread);
	}
	spin_unlock_bh(&self->xmit_path_lock);
}

static int atbm_sdio_cook_sg(struct sbus_priv *self,struct sdio_scatterlist *sgl)
{
	struct hif_tx_encap encap;
	int  ret;
	struct atbm_common *hw_priv =  self->core;
	struct wsm_hdr_tx *wsm_tx;
#if (PROJ_TYPE>=ARES_A)
	u16 wsm_len_u16[2];
	u16 wsm_len_sum;
#endif	//	(PROJ_TYPE==ARES_A)

	do {
		
		encap.data = sgl->cache + sgl->totallen;
		
		ret = wsm_get_tx(hw_priv,&encap);
		
		if (ret <= 0) {
			if(sgl->num_sgs > 0)
				break;
			else{
				return 1;
			}
		}
		
		wsm_tx = (struct wsm_hdr_tx*)encap.data;
#if (PROJ_TYPE>=ARES_A)
#ifdef CIPHER_HIF_COMBINED
        wsm_len_u16[0] = wsm_tx->u.common.total_len & 0xff;
        wsm_len_u16[1] = (wsm_tx->u.common.total_len >> 8) & 0xff;
        wsm_len_sum = wsm_len_u16[0] + wsm_len_u16[1];
        if (wsm_len_sum & BIT(8))
        {
            wsm_tx->u.common.mark = __cpu_to_le16(((wsm_len_sum + 1) & 0xff) << 8);
        }else{
            wsm_tx->u.common.mark = __cpu_to_le16((wsm_len_sum & 0xff) << 8);
        }
#endif
#endif //(PROJ_TYPE==ARES_A)
		if (encap.tx_len % (SDIO_BLOCK_SIZE) ) {
			encap.tx_len -= (encap.tx_len % (SDIO_BLOCK_SIZE) );
			encap.tx_len += SDIO_BLOCK_SIZE;
		}
		
		wsm_tx->id &= __cpu_to_le32(~WSM_TX_SEQ(WSM_TX_SEQ_MAX));
		wsm_tx->id |= cpu_to_le32(WSM_TX_SEQ(hw_priv->wsm_tx_seq));

		sgl->totallen += encap.tx_len;
		hw_priv->wsm_tx_seq = (hw_priv->wsm_tx_seq + 1) & WSM_TX_SEQ_MAX;
		
		sgl->sgs[sgl->sg_in] 	= encap.data;
		sgl->sglen[sgl->sg_in]  = encap.tx_len;
		
		sgl->num_sgs ++;
		sgl->sg_in ++;

		if(encap.source){
			sgl->cnfsg = encap.source;
			atbm_printk_debug("%s:cmd free(%d),used(%d)\n",__func__,hw_priv->hw_bufs_free,hw_priv->hw_bufs_used);
			break;
		}
		
		if(sgl->num_sgs >= SDIO_SG_CAP){
			break;
		}
		/*
		*2048 is safe
		*/
		if (sgl->totallen + 2048 > SDIO_SCATTERLIST_CACHE_SIZE){
			break;
		}
	}while(1);

	BUG_ON(sgl->num_sgs == 0);
	
	return 0;
}
static void encap_state_set(struct sbus_priv *self,enum sdio_encap_state state)
{
	if(unlikely(self->encap_state == SDIO_ENCAP_SUSPEND)){
		return;
	}
	self->encap_state = state;
}
static void encap_state_reset(struct sbus_priv *self)
{
	self->encap_state = SDIO_ENCAP_FREE;
}
static void atbm_sdio_tx_encap_state(struct sbus_priv *self,enum sdio_encap_state state)
{
	switch(state){
	case SDIO_ENCAP_FREE:
		if(self->encap_state ==  SDIO_ENCAP_SUSPEND){
			atbm_sdio_thread_wakeup(&self->tx_thread);
		}
		encap_state_reset(self);
		return;
	case SDIO_ENCAP_TX_THREAD:
		encap_state_set(self,SDIO_ENCAP_TX_THREAD);
		return;
	case SDIO_ENCAP_XMIT_PATH:
		atbm_fallthrough;
	case SDIO_ENCAP_WSMC_PATH:
		atbm_fallthrough;
	case SDIO_ENCAP_TRY_WAKEUP_THREAD:
		if(self->encap_state != SDIO_ENCAP_WAITING_THREAD_RUN)
			encap_state_set(self,state);
		return;
	case SDIO_ENCAP_BH_WAKEUP:
		if(self->encap_state == SDIO_ENCAP_FREE){
			encap_state_set(self,SDIO_ENCAP_WAKEUP_TXTHREAD_PRO);
			atbm_sdio_thread_wakeup(&self->tx_thread);
		}else {
			encap_state_set(self,SDIO_ENCAP_TRY_WAKEUP_THREAD);
		}
		return;
	case SDIO_ENCAP_LIST_EMPTY:
		encap_state_set(self,SDIO_ENCAP_FREE);
		return;
	case SDIO_ENCAP_ENCAP_EXIT:
		if(self->encap_state == SDIO_ENCAP_TRY_WAKEUP_THREAD){
			encap_state_set(self,SDIO_ENCAP_WAKEUP_TXTHREAD_PRO);
			atbm_sdio_thread_wakeup(&self->tx_thread);
			return;
		}
		encap_state_set(self,SDIO_ENCAP_FREE);
		return;
	case SDIO_ENCAP_FREE_LIST:
		if(self->encap_state == SDIO_ENCAP_FREE){
			encap_state_set(self,SDIO_ENCAP_WAKEUP_TXTHREAD_PRO);
			atbm_sdio_thread_wakeup(&self->tx_thread);
		}
		return;
	case SDIO_ENCAP_SUSPEND:
		encap_state_set(self,SDIO_ENCAP_SUSPEND);
		return;
	default:
		BUG_ON(1);
		return;
	}
}
static bool atbm_sdio_tx_encap_hold(struct sbus_priv *self,enum sdio_encap_state state)
{
	bool empty = list_empty(&self->sgs_free);
	
	switch(self->encap_state){
	case SDIO_ENCAP_FREE:
		if(empty){
			return false;
		}
		atbm_sdio_tx_encap_state(self,state);
		return true;
	case SDIO_ENCAP_WAITING_THREAD_RUN:
		WARN_ON(empty);
		if(state == SDIO_ENCAP_TX_THREAD){
			atbm_sdio_tx_encap_state(self,SDIO_ENCAP_TX_THREAD);
			return true;
		}
		return false;
	case SDIO_ENCAP_SUSPEND:
		atbm_printk_err("txencap suspend\n");
		return false;
	default:
		atbm_sdio_tx_encap_state(self,SDIO_ENCAP_TRY_WAKEUP_THREAD);
		return false;
	}
}
static int atbm_sdio_xmit_schedule(struct sbus_priv *self)
{
	spin_lock_bh(&self->xmit_path_lock);
	atbm_sdio_tx_encap_state(self,SDIO_ENCAP_BH_WAKEUP);
	spin_unlock_bh(&self->xmit_path_lock);
	return 0;
}

/*
*May be called by xxsdio_tx thread,the process of sending wsm cmd ,or the network tx path.but
at the same time ,only one of them can be the owner of the atbm_sdio_tx_encap.if not ,ASSERT.
*/
static int atbm_sdio_tx_encap(struct sbus_priv *self,enum sdio_encap_state path)
{
	struct sdio_scatterlist *sgl;
	struct list_head sgs_free;
	
	spin_lock_bh(&self->xmit_path_lock);
	if(atbm_sdio_tx_encap_hold(self,path) == false){
		goto exit_sched;
	}
splice_continue:	
	INIT_LIST_HEAD(&sgs_free);
	list_splice_tail_init(&self->sgs_free,&sgs_free);
	spin_unlock_bh(&self->xmit_path_lock);
encap_continue:	
	sgl = list_first_entry(&sgs_free, struct sdio_scatterlist, head);
	
	if(atbm_sdio_cook_sg(self,sgl)){
		BUG_ON(sgl->num_sgs != 0);
		BUG_ON(sgl->totallen != 0);
		goto encap_exit;
	}
	
	atbm_sdio_sched_sg_pending(self,sgl);
	
	if(!list_empty(&sgs_free))
		goto encap_continue;

	spin_lock_bh(&self->xmit_path_lock);
	if(!list_empty(&self->sgs_free)){
		goto splice_continue;
	}
	atbm_sdio_tx_encap_state(self,SDIO_ENCAP_LIST_EMPTY);
	spin_unlock_bh(&self->xmit_path_lock);

	return 0;
	
encap_exit:
	spin_lock_bh(&self->xmit_path_lock);
	list_splice_init(&sgs_free,&self->sgs_free);
	atbm_sdio_tx_encap_state(self,SDIO_ENCAP_ENCAP_EXIT);
	spin_unlock_bh(&self->xmit_path_lock);
	return 1;
exit_sched:
	spin_unlock_bh(&self->xmit_path_lock);
	return 1;
}

static int atbm_sdio_network_path_encap(struct sbus_priv *self)
{
	atbm_sdio_tx_encap(self,SDIO_ENCAP_XMIT_PATH);
	return 0;
}
static int atbm_sdio_wsm_cmd_encap(struct sbus_priv *self)
{
	atbm_sdio_tx_encap(self,SDIO_ENCAP_WSMC_PATH);
	return 0;
}

static int atbm_sdio_tx_thread(void *priv)
{
	struct sbus_priv *self = (struct sbus_priv *)priv;

#ifdef CONFIG_ATBM_SDIO_IRQ_HIGH
	struct sched_param param = { .sched_priority = 3 };
#else
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO/2 + 2};
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 9, 0))
    sched_set_fifo(current);
#else
    sched_setscheduler(current, SCHED_FIFO, &param);
#endif
	atbm_printk_init("%s\n",__func__);

	while(!atbm_sdio_wait_action(&self->tx_thread)){
		atbm_sdio_tx_encap(self,SDIO_ENCAP_TX_THREAD);
	}
	atbm_printk_init("%s:exit\n",__func__);
	atbm_sdio_release_err_cmd(self->core);
	return 0;
}
static void atbm_sdio_drv_flush_sg(struct sbus_priv *self,struct sdio_scatterlist *sgl)
{
#define ATBM_SDIO_FREE_BUFF_ERR(condition,free,prev_free,xmiteds,hw_xmiteds)	\
		do{ 																		\
			if(condition)	{																\
				atbm_printk_err("%s[%d]:free(%x),prev_free(%x),xmiteds(%x),hw_xmiteds(%x)\n",__func__,__LINE__,free,prev_free,xmiteds,hw_xmiteds);	\
				BUG_ON(1);			\
			}\
		}while(0)

	bool enough;
	int nsgs;
	int sglen;
	int totallen = 0;
	char *sg;
	char *cnf = NULL;
	struct atbm_common *hw_priv =  self->core;

flush_continue:
	
	if(sgl->num_sgs == 0){
		BUG_ON(totallen != sgl->totallen);
		BUG_ON(sgl->sg_in != sgl->sg_out);
		return;
	}
	
	enough = false;
	nsgs   = 0;
	sglen  = 0;
	sg     = sgl->sgs[sgl->sg_out];
	BUG_ON(sgl->sg_in == sgl->sg_out);
	
	do {
		char *cnfsg;
		
		enough = atbm_sdio_have_enough_space(hw_priv,1);
		
		if(enough == false){
			break;
		}
		
		totallen += sgl->sglen[sgl->sg_out];
		sglen    += sgl->sglen[sgl->sg_out];
		cnfsg = sgl->sgs[sgl->sg_out];
		sgl->sg_out ++;
		nsgs  ++;
		sgl->num_sgs --;

		spin_lock_bh(&hw_priv->tx_com_lock);
		ATBM_SDIO_FREE_BUFF_ERR(hw_priv->hw_bufs_free <= 0,hw_priv->hw_bufs_free,hw_priv->hw_bufs_free_init,hw_priv->n_xmits,hw_priv->hw_xmits);
		hw_priv->n_xmits ++;
		hw_priv->hw_bufs_free --;		
		ATBM_SDIO_FREE_BUFF_ERR(hw_priv->hw_bufs_free < 0,hw_priv->hw_bufs_free,hw_priv->hw_bufs_free_init,hw_priv->n_xmits,hw_priv->hw_xmits);
		spin_unlock_bh(&hw_priv->tx_com_lock);

		if(sgl->cnfsg == cnfsg){
			cnf = cnfsg;
			break;
		}
		
	}while(sgl->num_sgs);
	
	if(atbm_bh_is_term(hw_priv)){
		atbm_printk_err("%s:bh term\n",__func__);
		wsm_force_free_tx(hw_priv,(struct wsm_tx *)cnf);
		goto flush_continue;
	}
	
	hw_priv->buf_id_offset = nsgs;
	
	if (sglen && WARN_ON(atbm_data_write(hw_priv,sg, sglen))) {		
		atbm_printk_err("%s: xmit data err\n",__func__);
		goto xmit_err;
	}
	
	cnf = NULL;
	
	if((enough == false)&&(atbm_sdio_wait_enough_space(hw_priv,1) == false)){
		atbm_printk_err("%s: wait space timeout\n",__func__);
		goto xmit_err;
	}

	goto  flush_continue;	
xmit_err:
	wsm_force_free_tx(hw_priv,(struct wsm_tx *)cnf);
	atbm_bh_halt(hw_priv);
	goto flush_continue;
}
static void atbm_sdio_release_sg(struct sbus_priv *self,struct sdio_scatterlist *sg)
{	
	sg->totallen = 0;
	sg->num_sgs  = 0;
	sg->sg_in    = 0;
	sg->sg_out   = 0;
	sg->cnfsg    = NULL;
	
	spin_lock_bh(&self->xmit_path_lock);
	list_move_tail(&sg->head,&self->sgs_free);
	atbm_sdio_tx_encap_state(self,SDIO_ENCAP_FREE_LIST);
	spin_unlock_bh(&self->xmit_path_lock);
	
}
static void atbm_sdio_process_flush_sg(struct sbus_priv *self)
{
	struct list_head sg_list;
	struct sdio_scatterlist *sgl;
	
	INIT_LIST_HEAD(&sg_list);
#ifdef CONFIG_ATBM_SDIO_TX_HOLD
	atbm_sdio_lock(self);
#endif	
	spin_lock_bh(&self->xmit_path_lock);
flush_continue:
	self->flushing_sg = true;
	list_splice_tail_init(&self->sgs_pending,&sg_list);
	spin_unlock_bh(&self->xmit_path_lock);

	while (!list_empty(&sg_list)) {
		sgl = list_first_entry(&sg_list, struct sdio_scatterlist, head);
		
		atbm_sdio_drv_flush_sg(self,sgl);
		atbm_sdio_release_sg(self,sgl);
	}	

	spin_lock_bh(&self->xmit_path_lock);
	if(!list_empty(&self->sgs_pending)){
		goto flush_continue;
	}
	self->flushing_sg = false;
	spin_unlock_bh(&self->xmit_path_lock);
	
#ifdef CONFIG_ATBM_SDIO_TX_HOLD
	atbm_sdio_unlock(self);
#endif	
	
}
static int atbm_sdio_sg_thread(void *priv)
{
	struct sbus_priv *self = (struct sbus_priv *)priv;
#ifdef CONFIG_ATBM_SDIO_IRQ_HIGH
	struct sched_param param = { .sched_priority = 2};
#else
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO/2+1};
#endif	
#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 9, 0))
    sched_set_fifo(current);
#else
    sched_setscheduler(current, SCHED_FIFO, &param);
#endif
	atbm_printk_init("%s\n",__func__);
	
	while(!atbm_sdio_wait_action(&self->sg_thread)){
		atbm_sdio_process_flush_sg(self);
	}
	
	atbm_printk_init("%s:exit\n",__func__);
	atbm_sdio_process_flush_sg(self);
	atbm_sdio_release_err_cmd(self->core);

	return 0;
}

static int atbm_sdio_tx_init(struct sbus_priv *self)
{
	struct atbm_common *hw_priv = self->core;	
	struct atbm_sdio_thread *thread = &self->tx_thread;

	thread->flags = 0;
	thread->name  = ieee80211_alloc_name(hw_priv->hw,"sdio_tx");
	thread->period_handle = NULL;/*atbm_sdio_tx_period*/;
	thread->thread_fn = atbm_sdio_tx_thread;
	thread->self = self;
	thread->wakeup_period = MAX_SCHEDULE_TIMEOUT;

	if(atbm_sdio_thread_init(thread)){
		return -1;
	}

	return 0;
}

static int atbm_sdio_tx_sg_init(struct sbus_priv *self)
{
	struct atbm_common *hw_priv = self->core;	
	struct atbm_sdio_thread *thread = &self->sg_thread;

	thread->flags = 0;
	thread->name  = ieee80211_alloc_name(hw_priv->hw,"sdio_sg");
	thread->period_handle = NULL/*atbm_sdio_sg_period*/;
	thread->thread_fn = atbm_sdio_sg_thread;
	thread->self = self;
	thread->wakeup_period = MAX_SCHEDULE_TIMEOUT;

	if(atbm_sdio_thread_init(thread)){
		return -1;
	}

	return 0;
}

static int atbm_sdio_xmit_init(struct sbus_priv *self)
{
	int i = 0;
	atbm_printk_init("atbmwifi INIT_WORK enable\n");
	spin_lock_init(&self->xmit_path_lock);
	
	if(atbm_sdio_tx_init(self)){
		return  -1;
	}

	if(atbm_sdio_tx_sg_init(self)){
		return  -1;
	}
	
	INIT_LIST_HEAD(&self->sgs_free);
	INIT_LIST_HEAD(&self->sgs_pending);
	

	self->sgs_poll = atbm_kzalloc(sizeof(struct sdio_scatterlist) * SDIO_SCATTERLIST_CAP,GFP_KERNEL);
	
	if (!self->sgs_poll)
		return -ENOMEM;

	self->cache.cache = atbm_kzalloc(SDIO_CACHE_SIZE,GFP_KERNEL);
	
	if(!self->cache.cache){
		return -ENOMEM;
	}
	
	self->cache.cache_size = SDIO_CACHE_SIZE;
	
	for (i = 0; i < SDIO_SCATTERLIST_CAP; ++i){
		list_add_tail(&self->sgs_poll[i].head, &self->sgs_free);
		self->sgs_poll[i].cache = self->cache.cache + i*SDIO_SCATTERLIST_CACHE_SIZE;
	}	

	spin_lock_bh(&self->xmit_path_lock);
	encap_state_reset(self);
	spin_unlock_bh(&self->xmit_path_lock);
	return 0;
}
static int atbm_sdio_xmit_deinit(struct sbus_priv *self)
{
	atbm_sdio_thread_state(self);
	/*
	*let encap goto suspend.
	*/
	spin_lock_bh(&self->xmit_path_lock);
	atbm_sdio_tx_encap_state(self,SDIO_ENCAP_SUSPEND);
	spin_unlock_bh(&self->xmit_path_lock);

	/*
	*exit thread
	*/
	atbm_sdio_thread_deinit(&self->tx_thread);
	atbm_sdio_thread_deinit(&self->sg_thread);

	/*
	*let sgs_free,sgs_pending empty
	*/
	spin_lock_bh(&self->xmit_path_lock);
	INIT_LIST_HEAD(&self->sgs_free);
	INIT_LIST_HEAD(&self->sgs_pending);
	/*
	*reset state
	*/
	encap_state_reset(self);
	spin_unlock_bh(&self->xmit_path_lock);

	
	if(self->sgs_poll){
		atbm_kfree(self->sgs_poll);
		self->sgs_poll = NULL;
	}

	if(self->cache.cache){
		atbm_kfree(self->cache.cache);
		self->cache.cache = NULL;
	}
	return 0;
}
#endif
static int atbm_sdio_rev_init(struct sbus_priv *self)
{
	struct atbm_common *hw_priv = self->core;
	struct atbm_sdio_thread *thread = &self->rx_thread;
	
	atbm_printk_init("atbmwifi INIT_WORK enable\n");
	
	thread->flags = 0;
	thread->name  = ieee80211_alloc_name(hw_priv->hw,"sdio_rx");
	thread->period_handle = atbm_sdio_irq_period;
	thread->thread_fn = atbm_sdio_rx_thread;
	thread->wakeup_period = msecs_to_jiffies(30);
	thread->self = self;
	if(atbm_sdio_thread_init(thread))
		return -1;
	
	hw_priv->recv_skb = __atbm_dev_alloc_skb(SDIO_RX_MAXLEN,GFP_KERNEL);
	if(hw_priv->recv_skb == NULL){
		atbm_printk_err("recv_skb alloc err\n");
		return -1;
	}
    return 0;
}

static int atbm_sdio_rev_deinit(struct sbus_priv *self)
{
	atbm_printk_exit("atbm_sdio_rev_deinit\n");
	
	atbm_sdio_thread_deinit(&self->rx_thread);
	
	if(self->core->recv_skb){
		atbm_dev_kfree_skb(self->core->recv_skb);
		self->core->recv_skb = NULL;
	}
	return  0;
}
static int atbm_sdio_rev_schedule(struct sbus_priv *self)
{
	return atbm_sdio_thread_wakeup(&self->rx_thread);
}
static int atbm_sdio_bh_suspend(struct sbus_priv *self)
{
	int ret = 0;
	ret = atbm_kthread_try_suspend(&self->tx_thread);
	ret |= atbm_kthread_try_suspend(&self->rx_thread);
#ifndef CONFIG_ATBM_SDIO_TX_PROCESS_NORMAL
	ret |= atbm_kthread_try_suspend(&self->sg_thread);
#endif
	return ret;
}
static int atbm_sdio_bh_resume(struct sbus_priv *self)
{
	int ret = 0;
	atbm_kthread_resume(&self->tx_thread);
	atbm_kthread_resume(&self->rx_thread);
#ifndef CONFIG_ATBM_SDIO_TX_PROCESS_NORMAL
	atbm_kthread_resume(&self->sg_thread);
#endif
	return ret;
}
#if 0
static int atbm_sdio_rev_giveback(struct sbus_priv *self,void *giveback)
{
	struct atbm_common *hw_priv = self->core;
	struct wsm_rx *rx = (struct wsm_rx *)giveback;
	u32 hw_xmited = rx->channel_type;
	int hw_free;
	
	spin_lock_bh(&hw_priv->tx_com_lock);
	BUG_ON((int)hw_xmited > (int)hw_priv->n_xmits);
	if(hw_priv->n_xmits - hw_xmited <= hw_priv->wsm_caps.numInpChBufs){
		hw_free =  (hw_priv->wsm_caps.numInpChBufs-hw_priv->hw_bufs_used) - (hw_priv->n_xmits-hw_xmited);
		if(hw_priv->hw_bufs_free < hw_free)
			hw_priv->hw_bufs_free = hw_free;
	}
	spin_unlock_bh(&hw_priv->tx_com_lock);

	return 0;
}
#endif
/* sbus_ops implemetation */

static int atbm_sdio_memcpy_fromio(struct sbus_priv *self,
				     unsigned int addr,
				     void *dst, int count)
{
	return sdio_memcpy_fromio(self->func, dst, addr, count);
}

static int atbm_sdio_memcpy_toio(struct sbus_priv *self,
				   unsigned int addr,
				   const void *src, int count)
{
	return sdio_memcpy_toio(self->func, addr, (void *)src, count);
}

static int atbm_sdio_read_sync(struct sbus_priv *self,
				     unsigned int addr,
				     void *dst, int count)
{
	int ret = -EINVAL;
	
	switch(count){
	case sizeof(u16):
		*(u16 *)dst = sdio_readw(self->func, addr, &ret);		
		break;
	case sizeof(u32):
		*(u32 *)dst = sdio_readl(self->func, addr, &ret);
		break;
	default:
		WARN_ON(count == 8);
		ret = atbm_sdio_memcpy_fromio(self,addr,dst,count);
	}
	
	return ret;
}

static int atbm_sdio_write_sync(struct sbus_priv *self,
				   unsigned int addr,
				   const void *src, int count)
{
	int ret = -EINVAL;

	switch(count){
	case sizeof(u16):
		sdio_writew(self->func, *(u16 *)src, addr, &ret);
		break;
	case sizeof(u32):
		sdio_writel(self->func, *(u32 *)src, addr, &ret);
		break;
	default:
		WARN_ON(count == 8);
		ret = atbm_sdio_memcpy_toio(self,addr,src,count);
		break;
	}

	return ret;
}
int  atbm_readb_func(struct atbm_common *hw_priv,unsigned int addr,u8 *dst)
{
	int   ret = -EINVAL;
	hw_priv->sbus_ops->lock(hw_priv->sbus_priv);
	*dst = sdio_readb(hw_priv->sbus_priv->func, addr, &ret);
	hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);

	return ret;
}

int  atbm_writeb_func(struct atbm_common *hw_priv,unsigned int addr,u8 b)
{
	int   ret = -EINVAL;
	hw_priv->sbus_ops->lock(hw_priv->sbus_priv);
	sdio_writeb(hw_priv->sbus_priv->func,b,addr,&ret);
	hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);

	return ret;
}

int atbm_readb_func0(struct sbus_priv *self,
						 unsigned int addr,int *ret_err)
{
	u8 data;
	sdio_claim_host(self->func);
	data = sdio_f0_readb(self->func,addr,ret_err);
	sdio_release_host(self->func);
	return data;
}

int atbm_writeb_func0(struct sbus_priv *self,
						 unsigned int addr,u8 data)
{
	int ret_err;
	sdio_claim_host(self->func);
	//data = sdio_f0_writeb(self->func,addr,ret_err);
	sdio_f0_writeb(self->func, data, addr, &ret_err);
	sdio_release_host(self->func);
	return ret_err;
}

static void atbm_sdio_lock(struct sbus_priv *self)
{
	sdio_claim_host(self->func);
}

static void atbm_sdio_unlock(struct sbus_priv *self)
{
	sdio_release_host(self->func);
}

#ifdef CONFIG_SDIO_THREAD_PROCESS_MAC80211_DATA
static bool atbm_sdio_submit_recv_skb(struct wsm_rx_encap *encap,struct sk_buff *skb)
{
	if(likely(skb->pkt_type != ATBM_RX_WSM_CMD_FRAME)){
		bool bh_running = false;
		struct atbm_common *hw_priv = encap->hw_priv;
		
		spin_lock_bh(&hw_priv->rx_frame_queue.lock);
		__atbm_skb_queue_tail(&hw_priv->rx_frame_queue, skb);
		bh_running = hw_priv->bh_running;
		spin_unlock_bh(&hw_priv->rx_frame_queue.lock);

		if (bh_running == true)
			;
		else
			atbm_sdio_rev_schedule(hw_priv->sbus_priv);

	}else {
		encap->skb = skb;
		atbm_process_wsm_cmd_frame(encap);
	}
	return true;
}
#endif
#ifndef CONFIG_ATBM_APOLLO_USE_GPIO_IRQ
static void atbm_sdio_irq_handler(struct sdio_func *func)
{
	struct sbus_priv *self = sdio_get_drvdata(func);
	struct wsm_rx_encap encap;
	
	encap.priv = self->irq_priv;
#ifdef CONFIG_SDIO_IRQ_THREAD_PROCESS_DATA
#ifdef CONFIG_SDIO_THREAD_PROCESS_MAC80211_DATA
	encap.rx_func = atbm_sdio_submit_recv_skb;
	encap.gro_flush = false;
#else
	encap.rx_func = atbm_rx_tasklet_process_encap;
	encap.gro_flush = true;
#endif
	BUG_ON(!self);
#endif
	if (self->irq_handler)
		self->irq_handler(&encap);
#ifdef CONFIG_SDIO_IRQ_THREAD_PROCESS_DATA
#ifdef CONFIG_SDIO_THREAD_PROCESS_MAC80211_DATA
	
#endif
#endif
}
#endif
#ifdef CONFIG_ATBM_APOLLO_USE_GPIO_IRQ

irqreturn_t atbm_gpio_hardirq(int irq, void *dev_id)
{
	return IRQ_WAKE_THREAD;
}
void atbm_oob_intr_set(struct sbus_priv *self, bool enable)
{
	unsigned long flags;

	if (!self)
		return;

	spin_lock_irqsave(&self->lock, flags);
	if (self->oob_irq_enabled != enable) {
		if (enable)
			enable_irq(self->atbm_bgf_irq);
		else
			disable_irq_nosync(self->atbm_bgf_irq);
		self->oob_irq_enabled = enable;
	}
	spin_unlock_irqrestore(&self->lock, flags);
}

irqreturn_t atbm_gpio_irq(int irq, void *dev_id)
{
	struct sbus_priv *self = dev_id;
	struct wsm_rx_encap encap;
	
	if (self) {
		bool sdio_hold = false;
		encap.priv = self->irq_priv;
		if(!in_interrupt()){
			sdio_hold = true;
			atbm_sdio_lock(self);
#ifdef CONFIG_SDIO_IRQ_THREAD_PROCESS_DATA
#ifdef CONFIG_SDIO_THREAD_PROCESS_MAC80211_DATA
			encap.rx_func = atbm_sdio_submit_recv_skb;
			encap.gro_flush = false;
#else
			encap.rx_func = atbm_rx_tasklet_process_encap;
			encap.gro_flush = true;
#endif
#endif
		}		
		atbm_oob_intr_set(self, 0);
		self->irq_handler(&encap);		
		if(sdio_hold == true){
			WARN_ON(in_interrupt());
			sdio_hold = false;
#ifdef CONFIG_SDIO_IRQ_THREAD_PROCESS_DATA
			atbm_oob_intr_set(self,true);
#ifdef CONFIG_SDIO_THREAD_PROCESS_MAC80211_DATA
								
#endif
#endif
			atbm_sdio_unlock(self);
		}
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static int atbm_request_irq(struct sbus_priv *self)
{
	int ret = 0;
	int func_num;
	u8 cccr;
//	int bgf_irq;
	
	/* Hack to access Fuction-0 */
	func_num = self->func->num;
	self->func->num = 0;

	cccr = sdio_readb(self->func, SDIO_CCCR_IENx, &ret);
	if (WARN_ON(ret))
		goto err;

	/* Master interrupt enable ... */
	cccr |= BIT(0);

	/* ... for our function */
	cccr |= BIT(func_num);

	sdio_writeb(self->func, cccr, SDIO_CCCR_IENx, &ret);
	if (WARN_ON(ret))
		goto err;

	/* back to	Fuction-1 */
	self->func->num = func_num;

	ret = atbm_plat_request_gpio_irq(self->pdata,self,&self->atbm_bgf_irq);
	//printk("========================bgf_irq=%d\n",bgf_irq);

	if (WARN_ON(ret))
		goto err;
	self->oob_irq_enabled = 1;

	return 0;

err:
	atbm_plat_free_gpio_irq(self->pdata,self,self->atbm_bgf_irq);
	atbm_printk_bus("[%s]  fail exiting sw_gpio_irq_request..   :%d\n",__func__, ret);
	return ret;
}
#endif
static void atbm_sdio_miss_irq(struct sbus_priv *self) 
{	
	struct wsm_rx_encap encap;
	
#ifdef CONFIG_ATBM_APOLLO_USE_GPIO_IRQ
	atbm_oob_intr_set(self, 0);
#endif
	encap.priv = self->irq_priv;
	encap.rx_func = atbm_rx_tasklet_process_encap;
	encap.gro_flush = true;
	if (self->irq_handler)
		self->irq_handler(&encap);
}
static int atbm_sdio_irq_subscribe(struct sbus_priv *self,
				     sbus_irq_handler handler,
				     void *priv)
{
	int ret;
	unsigned long flags;

	if (!handler)
		return -EINVAL;

	spin_lock_irqsave(&self->lock, flags);
	self->irq_priv = priv;
	self->irq_handler = handler;
	spin_unlock_irqrestore(&self->lock, flags);

	atbm_printk_bus("[ATBM_WIFI]SW IRQ subscribe\n");
	sdio_claim_host(self->func);
#ifndef CONFIG_ATBM_APOLLO_USE_GPIO_IRQ
	#pragma message("atbm wifi SDIO_IRQ")
	atbm_printk_bus("[ATBM_WIFI] used SDIO Irq \n");
	ret = sdio_claim_irq(self->func, atbm_sdio_irq_handler);
	if (ret)
		atbm_printk_err("Failed to claim sdio Irq :%d\n",ret);
#else
	#pragma message("atbm wifi GPIO_IRQ")
	atbm_printk_bus("[ATBM_WIFI] used GPIO Irq \n");
	ret = atbm_request_irq(self);
#endif
	sdio_release_host(self->func);
	return ret;
}

static int atbm_sdio_irq_unsubscribe(struct sbus_priv *self)
{
	int ret = 0;
	unsigned long flags;
	//const struct resource *irq = self->pdata->irq;

	WARN_ON(!self->irq_handler);
	if (!self->irq_handler)
		return 0;

	atbm_printk_bus("[ATBM_WIFI]:SW IRQ unsubscribe\n");

#ifndef CONFIG_ATBM_APOLLO_USE_GPIO_IRQ
	sdio_claim_host(self->func);
	ret = sdio_release_irq(self->func);
	sdio_release_host(self->func);
#else
    atbm_plat_free_gpio_irq(self->pdata,self,self->atbm_bgf_irq);
	//free_irq(self->atbm_bgf_irq,self);
	//gpio_free(self->pdata->irq_gpio);
#endif  //CONFIG_ATBM_APOLLO_USE_GPIO_IRQ

	spin_lock_irqsave(&self->lock, flags);
	self->irq_priv = NULL;
	self->irq_handler = NULL;
	spin_unlock_irqrestore(&self->lock, flags);

	return ret;
}


#if ((ATBM_WIFI_PLATFORM != 10) && (ATBM_WIFI_PLATFORM != PLATFORM_AMLOGIC_S805) \
	&& (ATBM_WIFI_PLATFORM != PLATFORM_AMLOGIC_905))
static int atbm_detect_card(const struct atbm_platform_data *pdata)
{
	/* HACK!!!
	 * Rely on mmc->class_dev.class set in mmc_alloc_host
	 * Tricky part: a new mmc hook is being (temporary) created
	 * to discover mmc_host class.
	 * Do you know more elegant way how to enumerate mmc_hosts?
	 */

	struct mmc_host *mmc = NULL;
	struct class_dev_iter iter;
	struct device *dev;
	static struct platform_device *sdio_platform_dev = NULL;
	int status = 0;
	
	sdio_platform_dev = platform_device_alloc(WIFI_PLFDEVNAME,0);
	if(sdio_platform_dev == NULL){
		status = -ENOMEM;
		goto platform_dev_err;
	}

	if(platform_device_add(sdio_platform_dev) != 0){
		status = -ENOMEM;
		goto platform_dev_err;
	}
	
	mmc = mmc_alloc_host(0, &sdio_platform_dev->dev);
	
	if (!mmc){
		status = -ENOMEM;
		goto exit;
	}

	BUG_ON(!mmc->class_dev.class);
	class_dev_iter_init(&iter, mmc->class_dev.class, NULL, NULL);
	for (;;) {
		dev = class_dev_iter_next(&iter);
		if (!dev) {
			atbm_printk_err( "atbm: %s is not found.\n",
				pdata->mmc_id);
			break;
		} else {
			struct mmc_host *host = container_of(dev,
				struct mmc_host, class_dev);

			atbm_printk_bus("apollo:  found. %s\n",
				dev_name(&host->class_dev));

			if (dev_name(&host->class_dev) &&
				strcmp(dev_name(&host->class_dev),
					pdata->mmc_id))
				continue;

			if(host->card == NULL)
				mmc_detect_change(host, 10);
			else
				atbm_printk_err("%s has been attached\n",pdata->mmc_id);
			break;
		}
	}
	mmc_free_host(mmc);
exit:
	if(sdio_platform_dev)
		platform_device_unregister(sdio_platform_dev);
	return status;
platform_dev_err:
	if(sdio_platform_dev)
		platform_device_put(sdio_platform_dev);
	return status;
}
#endif //PLATFORM_AMLOGIC_S805

static int atbm_sdio_off(const struct atbm_platform_data *pdata)
{
	int ret = 0;

	if (pdata->insert_ctrl)
		ret = pdata->insert_ctrl(pdata, false);
	return ret;
}
#if ((ATBM_WIFI_PLATFORM != 10) && (ATBM_WIFI_PLATFORM != PLATFORM_AMLOGIC_S805) \
	&& (ATBM_WIFI_PLATFORM != PLATFORM_AMLOGIC_905))

static int atbm_sdio_on(const struct atbm_platform_data *pdata)
{
	int ret = 0;
    if (pdata->insert_ctrl)
		ret = pdata->insert_ctrl(pdata, true);
	msleep(200);
	atbm_detect_card(pdata);
	return ret;
}
#endif //#if ((ATBM_WIFI_PLATFORM != 10) && (ATBM_WIFI_PLATFORM != PLATFORM_AMLOGIC_S805))

static int atbm_cmd52_abort(struct sbus_priv *self)
{
	int ret;
	int regdata;
	sdio_claim_host(self->func);

	/* SDIO Simplified Specification V2.0, 4.4 Reset for SDIO */
	regdata = sdio_f0_readb(self->func, SDIO_CCCR_ABORT, &ret);
	atbm_printk_err("%s,%d ret %d\n",__func__,__LINE__,ret);
	if (ret)
		regdata = 0x08;
	else
		regdata |= 0x01;
	sdio_f0_writeb(self->func, regdata, SDIO_CCCR_ABORT, &ret);
//	msleep(1500);
	atbm_printk_err("%s,%d ret %d\n",__func__,__LINE__,ret);
	sdio_release_host(self->func);
	return ret;
}

static int atbm_sdio_reset(struct sbus_priv *self)
{
	int ret;
	int regdata;
	int func_num;

	return 0;
	atbm_printk_bus("atbm_sdio_reset++\n");
	sdio_claim_host(self->func);
	/* Hack to access Fuction-0 */
	func_num = self->func->num;

	self->func->num = 0;

	/**********************/
	atbm_printk_bus("SDIO_RESET++\n");
	/* SDIO Simplified Specification V2.0, 4.4 Reset for SDIO */
	regdata = sdio_readb(self->func, SDIO_CCCR_ABORT, &ret);
	if (ret)
		regdata = 0x08;
	else
		regdata |= 0x08;
	sdio_writeb(self->func, regdata, SDIO_CCCR_ABORT, &ret);
	if (WARN_ON(ret))
		goto set_func0_err;
	msleep(1500);
	regdata = sdio_readb(self->func, SDIO_CCCR_ABORT, &ret);
	atbm_printk_bus("SDIO_RESET-- 0x%x\n",regdata);

	/**********************/
	atbm_printk_bus("SDIO_SPEED_EHS++\n");
	regdata = sdio_readb(self->func, SDIO_CCCR_SPEED, &ret);
	if (WARN_ON(ret))
		goto set_func0_err;

	regdata |= SDIO_SPEED_EHS;
	sdio_writeb(self->func, regdata, SDIO_CCCR_SPEED, &ret);
	if (WARN_ON(ret))
		goto set_func0_err;

	regdata = sdio_readb(self->func, SDIO_CCCR_SPEED, &ret);
	atbm_printk_bus("SDIO_SPEED_EHS -- 0x%x:0x%x\n",regdata,SDIO_SPEED_EHS);

	/**********************/
	atbm_printk_bus("SDIO_BUS_WIDTH_4BIT++\n");
	regdata = sdio_readb(self->func, SDIO_CCCR_IF, &ret);
	if (WARN_ON(ret))
		goto set_func0_err;

	//regdata |= SDIO_BUS_WIDTH_4BIT;
	regdata = 0xff;
	sdio_writeb(self->func, regdata, SDIO_CCCR_IF, &ret);
	if (WARN_ON(ret))
		goto set_func0_err;
	regdata = sdio_readb(self->func, SDIO_CCCR_IF, &ret);
	atbm_printk_bus("SDIO_BUS_WIDTH_4BIT -- 0x%x:0x%x\n",regdata,SDIO_BUS_WIDTH_4BIT);
	/**********************/
	atbm_printk_bus("SDIO_BUS_ENABLE_FUNC++\n");
	regdata = sdio_readb(self->func, SDIO_CCCR_IOEx, &ret);
	if (WARN_ON(ret))
		goto set_func0_err;
	regdata |= BIT(func_num);
	atbm_printk_bus("SDIO_BUS_ENABLE_FUNC regdata %x\n",regdata);
	sdio_writeb(self->func, regdata, SDIO_CCCR_IOEx, &ret);
	if (WARN_ON(ret))
		goto set_func0_err;
	regdata = sdio_readb(self->func, SDIO_CCCR_IOEx, &ret);
	atbm_printk_bus("SDIO_BUS_ENABLE_FUNC -- 0x%x\n",regdata);
	/**********************/

set_func0_err:
	self->func->num = func_num;
	sdio_set_block_size(self->func,512);
	/* Restore the WLAN function number */
	sdio_release_host(self->func);

	return 0;
}

static u32 atbm_sdio_align_size(struct sbus_priv *self, u32 size)
{
	u32 aligned = sdio_align_size(self->func, size);
	return aligned;
}

int atbm_sdio_set_block_size(struct sbus_priv *self, u32 size)
{
	//return sdio_set_block_size(self->func, size);
	 u32 retries = 0;
	 int ret = 0;
	 do{
		  ret = sdio_set_block_size(self->func, size);

		  if(ret == 0){
		   	break;
		  }
		  retries ++;

		  atbm_printk_err("%s: set block size err(%d)\n",__func__,retries);
	 }while(retries <= 10);
	 
 	return ret;
}

static int atbm_sdio_pm(struct sbus_priv *self, bool  suspend)
{
	int ret = 0;
	return ret;
}

int atbm_reset_lmc_cpu(struct atbm_common *hw_priv)
{
	u32 val32;
	int ret=0;
	int retry=0;
	if(hw_priv == NULL)
	{
		return -1;
	}
	while (retry <= MAX_RETRY) {
		ret = atbm_reg_read_32(hw_priv, ATBM_HIFREG_CONFIG_REG_ID, &val32);
		if(!ret){
			retry=0;
			break;
		}else{
			/*reset sdio internel reg by send cmd52 to abort*/
			WARN_ON(hw_priv->sbus_ops->abort(hw_priv->sbus_priv));
			retry++;
			mdelay(1);
			atbm_printk_err(
				"%s:%d: enable_irq: can't read " \
				"config register.\n", __func__,__LINE__);
		}
	}
	val32 |= ATBM_HIFREG_CONFIG_CPU_RESET_BIT_2;
	val32 |= ATBM_HIFREG_CONFIG_CPU_RESET_BIT;	
	
	while (retry <= MAX_RETRY) {
		ret = atbm_reg_write_32(hw_priv, ATBM_HIFREG_CONFIG_REG_ID,val32);
		if(!ret){
		    retry=0;
			break;
		}else{
			/*reset sdio internel reg by send cmd52 to abort*/
			WARN_ON(hw_priv->sbus_ops->abort(hw_priv->sbus_priv));
			retry++;
			mdelay(1);
			atbm_printk_err(
				"%s:%d: enable_irq: can't write " \
				"config register.\n", __func__,__LINE__);
		}
	}
	while (retry <= MAX_RETRY) {
		ret = atbm_reg_read_32(hw_priv, ATBM_HIFREG_CONFIG_REG_ID, &val32);
		if(!ret){
			retry=0;
			break;
		}else{
			/*reset sdio internel reg by send cmd52 to abort*/
			WARN_ON(hw_priv->sbus_ops->abort(hw_priv->sbus_priv));
			retry++;
			mdelay(1);
			atbm_printk_err(
				"%s:%d: enable_irq: can't read " \
				"config register.\n", __func__,__LINE__);
		}
	}
	val32 &= ~ATBM_HIFREG_CONFIG_CPU_RESET_BIT_2;

	while (retry <= MAX_RETRY) {
		ret = atbm_reg_write_32(hw_priv, ATBM_HIFREG_CONFIG_REG_ID,val32);
		if(!ret){
			retry=0;
			break;
		}else{
			/*reset sdio internel reg by send cmd52 to abort*/
			WARN_ON(hw_priv->sbus_ops->abort(hw_priv->sbus_priv));
			retry++;
			mdelay(1);
			atbm_printk_err(
				"%s:%d: enable_irq: can't write " \
				"config register.\n", __func__,__LINE__);
		}
	}

	while (retry <= MAX_RETRY) {
		ret = atbm_reg_read_32(hw_priv, ATBM_HIFREG_CONFIG_REG_ID, &val32);
		if(!ret){
			retry=0;
			break;
		}else{
			/*reset sdio internel reg by send cmd52 to abort*/
			WARN_ON(hw_priv->sbus_ops->abort(hw_priv->sbus_priv));
			retry++;
			mdelay(1);
			atbm_printk_err( "%s:%d: set_mode: can't read config register.\n",__func__,__LINE__);
		}
	}
	val32 |= ATBM_HIFREG_CONFIG_ACCESS_MODE_BIT;

	while (retry <= MAX_RETRY) {
		ret = atbm_reg_write_32(hw_priv, ATBM_HIFREG_CONFIG_REG_ID,val32);
		if(!ret){
			retry=0;
			break;
		}else{
			/*reset sdio internel reg by send cmd52 to abort*/
			WARN_ON(hw_priv->sbus_ops->abort(hw_priv->sbus_priv));
			retry++;
			mdelay(1);
			atbm_printk_err("%s:%d: set_mode: can't write config register.\n",__func__,__LINE__);
		}
	}
	return ret;
}
static void atbm_sdio_release_err_cmd(struct atbm_common	*hw_priv)
{
	spin_lock_bh(&hw_priv->wsm_cmd.lock);
	if(hw_priv->wsm_cmd.cmd != 0XFFFF){
		hw_priv->wsm_cmd.ret = -1;
		hw_priv->wsm_cmd.done = 1;
		hw_priv->wsm_cmd.cmd = 0xFFFF;
		hw_priv->wsm_cmd.ptr = NULL;
		hw_priv->wsm_cmd.arg = NULL;
		atbm_printk_once("%s:release wsm_cmd.lock\n",__func__);		
		wake_up(&hw_priv->wsm_cmd_wq);		
	}	
	spin_unlock_bh(&hw_priv->wsm_cmd.lock);
}

static int __atbm_sdio_lmac_restart(struct sbus_priv *self)
{
	struct atbm_common *hw_priv = self->core;
	int ret = 0;
	int i = 0;
	/*
	*lock tx queues
	*/
	wsm_lock_tx_async(hw_priv);
	atbm_tx_queues_lock(hw_priv);

	atbm_printk_init("%s: Prepare Restart\n",__func__);
	/*
	*disable sdio irq ,and stop tx/rx thread
	*/
	hw_priv->sbus_ops->irq_unsubscribe(hw_priv->sbus_priv);
	if(hw_priv->sbus_ops->sbus_xmit_func_deinit)
		hw_priv->sbus_ops->sbus_xmit_func_deinit(hw_priv->sbus_priv);
	if(hw_priv->sbus_ops->sbus_rev_func_deinit)	
		hw_priv->sbus_ops->sbus_rev_func_deinit(hw_priv->sbus_priv);
	/*
	*clear cmd
	*/
	atbm_sdio_release_err_cmd(hw_priv);
	atbm_destroy_wsm_cmd(hw_priv);
	/*
	*clear tx queues
	*/
	for (i = 0; i < 4; i++)
		atbm_queue_clear(&hw_priv->tx_queue[i], ATBM_WIFI_ALL_IFS);
	/*
	*hold rtnl_lock,make sure that when down load fw,network layer cant not 
	*send pkg and cmd
	*/
	rtnl_lock();
	
	ieee80211_pre_restart_hw_sync(hw_priv->hw);
	
	hw_priv->init_done = 0;

	ret = atbm_reset_lmc_cpu(hw_priv);

	if(ret){
		atbm_printk_err("%s:reset cpu err\n",__func__);
		goto exit;
	}
	
	atbm_printk_init("%s: Flush Rx\n",__func__);
	atbm_rx_bh_flush(hw_priv);
	atbm_printk_init("%s: Flush Running cmd\n",__func__);
	atbm_destroy_wsm_cmd(hw_priv);
	atbm_printk_init("Flush iee80211 hw\n");
	hw_priv->bh_error = 0;
	/*
	*release hw buff
	*/
	hw_priv->wsm_tx_seq = 0;
	hw_priv->buf_id_tx = 0;
	hw_priv->wsm_rx_seq = 0;
	hw_priv->hw_bufs_used = 0;
	hw_priv->buf_id_rx = 0;
	/*
	*for sdio no tx confirm ,n_xmits must be zero
	*/
	hw_priv->n_xmits = 0;
	hw_priv->hw_xmits = 0;
	hw_priv->hw_bufs_free = 0;
	hw_priv->hw_bufs_free_init = 0;
	for (i = 0; i < ATBM_WIFI_MAX_VIFS; i++)
		hw_priv->hw_bufs_used_vif[i] = 0;
	
	atomic_set(&hw_priv->atbm_pluged,1);

	/*
	*reinit bus tx/rx
	*/
	if(hw_priv->sbus_ops->sbus_xmit_func_init)
		ret = hw_priv->sbus_ops->sbus_xmit_func_init(hw_priv->sbus_priv);
	if(hw_priv->sbus_ops->sbus_rev_func_init)
		ret |= hw_priv->sbus_ops->sbus_rev_func_init(hw_priv->sbus_priv);

	if(ret){
		atbm_printk_init("%s:init tx/rx err\n",__func__);
		goto exit;
	}

	/*
	*load firmware
	*/
	ret = atbm_reinit_firmware(hw_priv);
	
	if(ret){
		atbm_printk_init("%s:reload fw err\n",__func__);
		goto exit;
	}
	atbm_Default_driver_configuration(hw_priv);
	
	/*
	*restart ap and sta
	*/
	ret = ieee80211_restart_hw_sync(hw_priv->hw);
exit:
	atbm_tx_queues_unlock(hw_priv);
	wsm_unlock_tx(hw_priv);
	rtnl_unlock();
	return ret;
}
static int atbm_sdio_lmac_restart(struct sbus_priv *self)
{
	int ret = -1;
	/*
	*it's safe to try device lock here when rmmod is running
	*here should not use device_lock,if so lock may be dead.
	*/
	if(device_trylock(&self->func->dev)){
		get_device(&self->func->dev);
		ret = __atbm_sdio_lmac_restart(self);
		put_device(&self->func->dev);
		device_unlock(&self->func->dev);
	}else {
		atbm_printk_err("%s:maybe sdio is disconneting\n",__func__);
	}

	return ret;
}

static struct sbus_ops atbm_sdio_sbus_ops = {
	.sbus_memcpy_fromio	= atbm_sdio_memcpy_fromio,
	.sbus_memcpy_toio	= atbm_sdio_memcpy_toio,
	.sbus_read_sync 	= atbm_sdio_read_sync,//atbm_sdio_memcpy_fromio,
	.sbus_write_sync	= atbm_sdio_write_sync,//atbm_sdio_memcpy_toio,
	.lock				= atbm_sdio_lock,
	.unlock				= atbm_sdio_unlock,
	.irq_subscribe		= atbm_sdio_irq_subscribe,
	.irq_unsubscribe	= atbm_sdio_irq_unsubscribe,
	.reset				= atbm_sdio_reset,
	.align_size			= atbm_sdio_align_size,
	.power_mgmt			= atbm_sdio_pm,
	.set_block_size		= atbm_sdio_set_block_size,
	.sbus_reset_chip    = atbm_sdio_reset_chip,
	.abort				= atbm_cmd52_abort,
	.lmac_restart   	= atbm_sdio_lmac_restart,
	//.sbus_cmd52_fromio =atbm_cmd52_fromio,
	//.sbus_cmd52_toio =atbm_cmd52_toio,
	.sbus_xmit_func_init   = atbm_sdio_xmit_init,
	.sbus_xmit_func_deinit  = atbm_sdio_xmit_deinit,
	.sbus_rev_func_init    = atbm_sdio_rev_init,
	.sbus_rev_func_deinit  = atbm_sdio_rev_deinit,
	.sbus_xmit_schedule    = atbm_sdio_xmit_schedule,
	.sbus_rev_schedule     = atbm_sdio_rev_schedule,
	.sbus_bh_suspend       = atbm_sdio_bh_suspend,
	.sbus_bh_resume        = atbm_sdio_bh_resume,
#ifndef CONFIG_ATBM_SDIO_TX_PROCESS_NORMAL
	.sbus_data_write = atbm_sdio_network_path_encap,
	.sbus_wsm_write  = atbm_sdio_wsm_cmd_encap,
#endif
	.sbus_wait_data_xmited = atbm_sdio_rev_schedule,
};

/* Probe Function to be called by SDIO stack when device is discovered */
static int atbm_sdio_probe(struct sdio_func *func,
			      const struct sdio_device_id *id)
{
	struct sbus_priv *self;
	int status;

	atbm_dbg(ATBM_APOLLO_DBG_INIT, "Probe called\n");
	
	func->card->quirks|=MMC_QUIRK_LENIENT_FN0;
	func->card->quirks |= MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;

	self = atbm_kzalloc(sizeof(*self), GFP_KERNEL);
	if (!self) {
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Can't allocate SDIO sbus_priv.");
		return -ENOMEM;
	}

	spin_lock_init(&self->lock);
	spin_lock_init(&self->bh_lock);
	self->pdata = atbm_get_platform_data();
	self->func = func;
	sdio_set_drvdata(func, self);
	sdio_claim_host(func);
	sdio_enable_func(func);
	sdio_release_host(func);

	//reset test start
	//atbm_sdio_reset(self);
	//reset test end

	atbm_printk_init("%s:v12\n",__func__);
	status = atbm_core_probe(&atbm_sdio_sbus_ops,
			      self, &func->dev, &self->core);
	if (status) {
		sdio_claim_host(func);
		sdio_disable_func(func);
		sdio_release_host(func);
		sdio_set_drvdata(func, NULL);
		atbm_kfree(self);
		//printk("[atbm_wtd]:set wtd_probe = -1\n");
	}
	else {
		atbm_printk_exit("[atbm_wtd]:set wtd_probe = 1\n");
	}
	return status;
}

/* Disconnect Function to be called by SDIO stack when
 * device is disconnected */
static int atbm_sdio_reset_chip(struct sbus_priv *self)
{
	atbm_printk_bus("%s\n",__func__);
	atbm_reset_lmc_cpu(self->core);
	return 0;
}
static void atbm_sdio_disconnect(struct sdio_func *func)
{
	struct sbus_priv *self = sdio_get_drvdata(func);
	atbm_printk_exit("atbm_sdio_disconnect");
	if (self) {
		if (self->core) {
#ifdef RESET_CHIP
			atbm_reset_chip((struct atbm_common *)self->core->hw->priv);
#else
			/*
			*should not rest cpu here,we will do it at function atbm_unregister_common
			*/
//			atbm_reset_lmc_cpu((struct atbm_common *)self->core->hw->priv);
#endif
			atbm_core_release(self->core);
			self->core = NULL;
		}
		sdio_claim_host(func);
#if 0
		/*
		*	reset sdio
		*/
		{
			int ret;
			int regdata;
			/**********************/
			atbm_printk_exit("[%s]:SDIO_RESET++\n",dev_name(&func->card->host->class_dev));
			/* SDIO Simplified Specification V2.0, 4.4 Reset for SDIO */
			regdata = sdio_f0_readb(func, SDIO_CCCR_ABORT, &ret);
			if (ret)
				regdata = 0x08;
			else
				regdata |= 0x08;
			sdio_f0_writeb(func, regdata, SDIO_CCCR_ABORT, &ret);
			WARN_ON(ret);
			msleep(50);
			regdata = sdio_f0_readb(func, SDIO_CCCR_ABORT, &ret);
			atbm_printk_exit("[%s]:SDIO_RESET-- 0x%x\n",dev_name(&func->card->host->class_dev),regdata);

			/**********************/
		}
#endif
		sdio_disable_func(func);
		sdio_release_host(func);
		sdio_set_drvdata(func, NULL);
		atbm_kfree(self);
	}
}

static int atbm_suspend(struct device *dev)
{
	int ret;
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sbus_priv *self = sdio_get_drvdata(func);
	/* Notify SDIO that CW1200 will remain powered during suspend */
	mmc_pm_flag_t flags=sdio_get_host_pm_caps(func);
	//printk("mmc_pm_flag=%x\n",flags);
	if(!(flags&MMC_PM_KEEP_POWER)){
		atbm_dbg(ATBM_APOLLO_DBG_ERROR,
				"cant remain alive while host is suspended\n");
		return -ENOSYS;
	}
	ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (ret)
		atbm_dbg(ATBM_APOLLO_DBG_ERROR,
			   "set sdio keep pwr flag failed:%d\n", ret);
	/*sdio irq wakes up host*/
	if (flags&MMC_PM_WAKE_SDIO_IRQ){
		ret = sdio_set_host_pm_flags(func, MMC_PM_WAKE_SDIO_IRQ);
	}
	if (ret)
		atbm_dbg(ATBM_APOLLO_DBG_ERROR,
			   "set sdio wake up irq flag failed:%d\n", ret);
	atbm_printk_err("sdio suspend\n");
	if(hw_to_local(self->core->hw)->wowlan == false){
		atbm_printk_err("sdio no wowlan suspend\n");
		ret = atbm_bh_suspend(self->core);
	}
	if(ret == 0){
		self->irq_handler_suspend = self->irq_handler;
		self->irq_priv_suspend    = self->irq_priv;
		atbm_sdio_irq_unsubscribe(self);
	}
	return ret;
}

static int atbm_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sbus_priv *self = sdio_get_drvdata(func);
	int ret = 0;
	
	atbm_printk_err("sdio resume\n");
	atbm_sdio_lock(self);
	atbm_printk_err("%s:disable irq\n",__func__);
	__atbm_irq_enable(self->core,0);
	atbm_sdio_unlock(self);
	if(self->irq_handler_suspend && self->irq_priv_suspend){
		atbm_sdio_irq_subscribe(self,self->irq_handler_suspend,self->irq_priv_suspend);
		self->irq_handler_suspend = NULL;
		self->irq_priv_suspend = NULL;
	}
	atbm_sdio_lock(self);
	atbm_printk_err("%s:enable irq\n",__func__);
	__atbm_irq_enable(self->core,1);
	atbm_sdio_unlock(self);
	
	if(hw_to_local(self->core->hw)->wowlan == false){
		atbm_printk_err("sdio no wowlan resume\n");
		ret = atbm_bh_resume(self->core);
	}
	return 0;
}

static const struct dev_pm_ops atbm_pm_ops = {
	.suspend = atbm_suspend,
	.resume = atbm_resume,
};

static struct sdio_driver sdio_driver = {
	.name		= WIFI_MODDRVNAME,
	.id_table	= atbm_sdio_ids,
	.probe		= atbm_sdio_probe,
	.remove		= atbm_sdio_disconnect,
	.drv = {
		.pm = &atbm_pm_ops,
	}
};
static int atbm_reboot_notifier(struct notifier_block *nb,
				unsigned long action, void *unused)
{
	atbm_printk_exit("atbm_reboot_notifier\n");
	atbm_sdio_exit();
	atbm_ieee80211_exit();
	atbm_release_firmware();
	return NOTIFY_DONE;
}

/* Probe Function to be called by USB stack when device is discovered */
static struct notifier_block atbm_reboot_nb = {
	.notifier_call = atbm_reboot_notifier,
	.priority=1,
};


/* Init Module function -> Called by insmod */
static int  atbm_sdio_init(void)
{
	const struct atbm_platform_data *pdata;
	int ret;
	pdata = atbm_get_platform_data();

	ret=driver_build_info();
	if (pdata->clk_ctrl) {
		ret = pdata->clk_ctrl(pdata, true);
		if (ret)
			goto err_clk;
	}
/*
* modify for rockchip platform
*/
#if (ATBM_WIFI_PLATFORM == 10)
	if (pdata->insert_ctrl&&pdata->power_ctrl)
	{
		ret = pdata->insert_ctrl(pdata, false);
		ret = pdata->power_ctrl(pdata, false);
		if (ret)
			goto err_power;
		ret = pdata->power_ctrl(pdata, true);
		if (ret)
			goto err_power;
		ret = pdata->insert_ctrl(pdata, true);
	}
	else
	{
		goto err_power;
	}
#else
	if (pdata->power_ctrl) {
		ret = pdata->power_ctrl(pdata, true);
		if (ret)
			goto err_power;
	}
#endif
	ret = sdio_register_driver(&sdio_driver);
	if (ret)
		goto err_reg;
#if ((ATBM_WIFI_PLATFORM != 10) && (ATBM_WIFI_PLATFORM != PLATFORM_AMLOGIC_S805)\
	&& (ATBM_WIFI_PLATFORM != PLATFORM_AMLOGIC_905))

	ret = atbm_sdio_on(pdata);
	if (ret)
		goto err_on;
#endif
	return 0;

#if ((ATBM_WIFI_PLATFORM != 10) && (ATBM_WIFI_PLATFORM != PLATFORM_AMLOGIC_S805)\
	&& (ATBM_WIFI_PLATFORM != PLATFORM_AMLOGIC_905))

err_on:
	if (pdata->power_ctrl)
		pdata->power_ctrl(pdata, false);
#endif
err_power:
	if (pdata->clk_ctrl)
		pdata->clk_ctrl(pdata, false);
err_clk:
	sdio_unregister_driver(&sdio_driver);
err_reg:
	return ret;
}

/* Called at Driver Unloading */
static void  atbm_sdio_exit(void)
{
	const struct atbm_platform_data *pdata;
	pdata = atbm_get_platform_data();
	sdio_unregister_driver(&sdio_driver);
	atbm_sdio_off(pdata);
	if (pdata->power_ctrl)
		pdata->power_ctrl(pdata, false);
	if (pdata->clk_ctrl)
		pdata->clk_ctrl(pdata, false);
}


static int __init apollo_sdio_module_init(void)
{
#ifdef CONFIG_ATBM_BLE
#ifdef CONFIG_ATBM_BLE_CDEV
	if(ieee80211_ble_platform_init()){
		return -1;
	}
#endif
#endif	
	ieee80211_atbm_mem_int();
	ieee80211_atbm_skb_int();
	atbm_wq_init();
#ifdef ANDROID
	register_reboot_notifier(&atbm_reboot_nb);
#endif
	atbm_init_firmware();
	atbm_ieee80211_init();
	atbm_module_attribute_init();
	return atbm_sdio_init();
}
static void  apollo_sdio_module_exit(void)
{	
	atbm_sdio_exit();
	atbm_ieee80211_exit();
	atbm_release_firmware();
#ifdef ANDROID
	unregister_reboot_notifier(&atbm_reboot_nb);
#endif
	atbm_module_attribute_exit();
	atbm_wq_exit();
	ieee80211_atbm_mem_exit();
	ieee80211_atbm_skb_exit();
#ifdef CONFIG_ATBM_BLE
#ifdef CONFIG_ATBM_BLE_CDEV
	ieee80211_ble_platform_exit();
#endif
#endif

}


module_init(apollo_sdio_module_init);
module_exit(apollo_sdio_module_exit);
