/*
 * Device handling thread implementation for mac80211 altobeam APOLLO drivers
 *
 * Copyright (c) 2016, altobeam
 * Author:
 *
 * Based on:
 * Atbm UMAC CW1200 driver, which is
 * Copyright (c) 2010, ST-Ericsson
 * Author: Ajitpal Singh <ajitpal.singh@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
//#undef CONFIG_ATBM_APOLLO_USE_GPIO_IRQ
#include <net/atbm_mac80211.h>
#include <linux/kthread.h>
#include <linux/prefetch.h>


#include "apollo.h"
#include "bh.h"
#include "hwio.h"
#include "wsm.h"
#include "sbus.h"
#include "debug.h"
#include "apollo_plat.h"
#include "sta.h"
#include "ap.h"
#include "scan.h"
#if defined(CONFIG_ATBM_APOLLO_BH_DEBUG)
#define bh_printk  atbm_printk_always
#else
#define bh_printk(...)
#endif
#define IS_SMALL_MSG(wsm_id) (wsm_id & 0x1000)
static int atbm_bh(void *arg);
extern void atbm_monitor_pc(struct atbm_common *hw_priv);

int atbm_bh_read_ctrl_reg(struct atbm_common *hw_priv,
					  u16 *ctrl_reg);
int atbm_bh_read_ctrl_reg_unlock(struct atbm_common *hw_priv,
					  u16 *ctrl_reg);

typedef int (*reed_ctrl_handler_t)(struct atbm_common *hw_priv,u16 *ctrl_reg);
typedef int (*reed_data_handler_t)(struct atbm_common *hw_priv, void *buf, u32 buf_len);
extern  bool atbm_sdio_wait_enough_space(struct atbm_common	*hw_priv,u32 n_needs);
extern  bool atbm_sdio_have_enough_space(struct atbm_common	*hw_priv,u32 n_needs);

/* TODO: Verify these numbers with WSM specification. */
#define DOWNLOAD_BLOCK_SIZE_WR	(0x4000 - 4)
/* an SPI message cannot be bigger than (2"12-1)*2 bytes
 * "*2" to cvt to bytes */
#define MAX_SZ_RD_WR_BUFFERS	(DOWNLOAD_BLOCK_SIZE_WR*2)
#define PIGGYBACK_CTRL_REG	(2)
#define EFFECTIVE_BUF_SIZE	(MAX_SZ_RD_WR_BUFFERS - PIGGYBACK_CTRL_REG)
#define ATBM_MAX_OVERFLOW_SIZE	(64)
typedef int (*atbm_wsm_handler)(struct atbm_common *hw_priv,
	u8 *data, size_t size);

#ifdef MCAST_FWDING
int wsm_release_buffer_to_fw(struct atbm_vif *priv, int count);
#endif
/*Os wake lock*/
#define ATBM_OS_WAKE_LOCK(WAKELOCK)			atbm_os_wake_lock(WAKELOCK)
#define ATBM_OS_WAKE_UNLOCK(WAKELOCK)		atbm_os_wake_unlock(WAKELOCK)
/*BH wake lock*/
#define ATBM_BH_WAKE_LOCK(WAKELOCK)         atbm_bh_wake_lock(WAKELOCK)
#define ATBM_BH_WAKE_UNLOCK(WAKELOCK)         atbm_bh_wake_unlock(WAKELOCK)
int atbm_os_check_wakelock(struct atbm_common *hw_priv)
{
	if (!hw_priv)
		return 0;
#ifdef CONFIG_HAS_WAKELOCK
	/* Indicate to the SD Host to avoid going to suspend if internal locks are up */
	if (wake_lock_active(&hw_priv->hw_wake) ||
		(wake_lock_active(&hw_priv->bh_wake)))
		return 1;
#endif
	return 0;
}

int atbm_os_wake_lock(struct atbm_common *hw_priv)
{
	unsigned long flags;
	int ret = 0;
	ret=atbm_os_check_wakelock(hw_priv);
	if(ret){
		return 0;
	}
	if (hw_priv) {
		spin_lock_irqsave(&hw_priv->wakelock_spinlock, flags);
		if (hw_priv->wakelock_hw_counter == 0) {
#ifdef CONFIG_HAS_WAKELOCK
			wake_lock(&hw_priv->hw_wake);
#elif defined(SDIO_BUS) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
			//pm_stay_awake(hw_priv);
#endif
		}
		hw_priv->wakelock_hw_counter++;
		ret = hw_priv->wakelock_hw_counter;
		spin_unlock_irqrestore(&hw_priv->wakelock_spinlock, flags);
	}
	return ret;
}

int atbm_os_wake_unlock(struct atbm_common *hw_priv)
{
	unsigned long flags;
	int ret = 0;

	if (hw_priv) {
		spin_lock_irqsave(&hw_priv->wakelock_spinlock, flags);
		if (hw_priv->wakelock_hw_counter > 0) {
			hw_priv->wakelock_hw_counter--;
			if (hw_priv->wakelock_hw_counter == 0) {
#ifdef CONFIG_HAS_WAKELOCK
				wake_unlock(&hw_priv->hw_wake);
#elif defined(SDIO_BUS) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
				//pm_relax(hw_priv);
#endif
			}
			ret = hw_priv->wakelock_hw_counter;
		}
		spin_unlock_irqrestore(&hw_priv->wakelock_spinlock, flags);
	}
	return ret;
}
int atbm_bh_wake_lock(struct atbm_common *hw_priv)
{
	unsigned long flags;
	int ret;
	ret=atbm_os_check_wakelock(hw_priv);
	if(ret){
		return 0;
	}
	if (hw_priv) {
		spin_lock_irqsave(&hw_priv->wakelock_spinlock, flags);
		if (hw_priv->wakelock_bh_counter == 0) {
#ifdef CONFIG_HAS_WAKELOCK
			wake_lock(&hw_priv->bh_wake);
#elif defined(SDIO_BUS) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
			//pm_stay_awake(hw_priv);
#endif		
			hw_priv->wakelock_bh_counter++;
		}
		spin_unlock_irqrestore(&hw_priv->wakelock_spinlock, flags);
	}
	return 0;
}

void atbm_bh_wake_unlock(struct atbm_common *hw_priv)
{
	unsigned long flags;
	if (hw_priv) {
		spin_lock_irqsave(&hw_priv->wakelock_spinlock, flags);
		if (hw_priv->wakelock_bh_counter > 0) {
			hw_priv->wakelock_bh_counter = 0;
#ifdef CONFIG_HAS_WAKELOCK
		wake_unlock(&hw_priv->bh_wake);
#elif defined(SDIO_BUS) && (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 36))
		//pm_relax(hw_priv);
#endif
		}
		spin_unlock_irqrestore(&hw_priv->wakelock_spinlock, flags);
	}

}
int atbm_register_bh(struct atbm_common *hw_priv)
{
	int err = 0;
	struct sched_param param = { .sched_priority = 1 };
	atbm_printk_init("[BH] register.\n");
	BUG_ON(hw_priv->bh_thread);
	atomic_set(&hw_priv->bh_rx, 0);
	atomic_set(&hw_priv->bh_tx, 0);
	atomic_set(&hw_priv->bh_term, 0);
	atomic_set(&hw_priv->bh_suspend, ATBM_APOLLO_BH_RESUMED);
	hw_priv->buf_id_tx = 0;
	hw_priv->buf_id_rx = 0;
	init_waitqueue_head(&hw_priv->bh_wq);
	init_waitqueue_head(&hw_priv->bh_evt_wq);
	hw_priv->bh_thread = kthread_create(&atbm_bh, hw_priv, ieee80211_alloc_name(hw_priv->hw,"atbm_bh"));
	if (IS_ERR(hw_priv->bh_thread)) {
		err = PTR_ERR(hw_priv->bh_thread);
		hw_priv->bh_thread = NULL;
	} else {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 9, 0))
        	sched_set_fifo_low(current);
#else
		WARN_ON(sched_setscheduler(hw_priv->bh_thread,
			SCHED_FIFO, &param));
#endif
#ifdef HAS_PUT_TASK_STRUCT
		get_task_struct(hw_priv->bh_thread);
#endif
		wake_up_process(hw_priv->bh_thread);
	}
	return err;
}

/*
static void atbm_hw_buff_reset(struct atbm_common *hw_priv)
{
	int i;
	hw_priv->wsm_tx_seq = 0;
	hw_priv->buf_id_tx = 0;
	hw_priv->wsm_rx_seq = 0;
	hw_priv->hw_bufs_used = 0;
	hw_priv->save_buf = NULL;
	hw_priv->save_buf_len = 0;
	hw_priv->save_buf_vif_selected = -1;
	hw_priv->buf_id_rx = 0;
	for (i = 0; i < ATBM_WIFI_MAX_VIFS; i++)
		hw_priv->hw_bufs_used_vif[i] = 0;
}
*/

void atbm_unregister_bh(struct atbm_common *hw_priv)
{
	struct task_struct *thread = hw_priv->bh_thread;
	if (WARN_ON(!thread))
		return;

	hw_priv->bh_thread = NULL;
	atbm_printk_exit( "[BH] unregister.\n");
	atomic_add(1, &hw_priv->bh_term);
	wake_up(&hw_priv->bh_wq);
	kthread_stop(thread);
#ifdef HAS_PUT_TASK_STRUCT
	put_task_struct(thread);
#endif
}
static void atbm_sdio_napi_sched(struct wsm_rx_encap *encap)
{
	if(encap->gro_flush){
		ieee80211_napi_sched(encap->hw_priv->hw);
	}else {
		struct sk_buff *skb_flush;

		skb_flush = atbm_alloc_skb(0,GFP_KERNEL);

		if(unlikely(!skb_flush)){
			WARN_ON(1);
			return;
		}
		skb_flush->pkt_type = ATBM_RX_WSM_GRO_FLUSH;
		
		encap->rx_func(encap,skb_flush);
	}
}
static int atbm_sdio_process_read_data(struct wsm_rx_encap *encap)
{
	#define LMAC_MAX_RX_BUFF	(24)
	u32 read_len_lsb = 0;
	u32 read_len_msb = 0;
	u32 read_len;
	struct sk_buff *skb_rx = NULL;
	u16 ctrl_reg = 0;
	u32 alloc_len;
	u8 *data;
	u8 rx_continue_cnt = 0;
	struct atbm_common *hw_priv = encap->hw_priv;
	
rx_check:
	if (WARN_ON(atbm_bh_read_ctrl_reg_unlock(
			hw_priv, &ctrl_reg))){
			goto err;
	}
rx_continue:
	read_len_lsb = (ctrl_reg & ATBM_HIFREG_CONT_NEXT_LEN_LSB_MASK)*2;
	read_len_msb = (ctrl_reg & ATBM_HIFREG_CONT_NEXT_LEN_MSB_MASK)*2;
	read_len=((read_len_msb>>2)+read_len_lsb);
	
	if (!read_len) {
		goto exit;
	}

	if (WARN_ON((read_len < sizeof(struct wsm_hdr)) ||
			(read_len > EFFECTIVE_BUF_SIZE))) {
			atbm_printk_err("Invalid read len: %d,read_cnt(%d)\n",
				read_len,rx_continue_cnt);
		
		atbm_bh_halt(hw_priv);
		goto err;
	}
	/* Add SIZE of PIGGYBACK reg (CONTROL Reg)
	 * to the NEXT Message length + 2 Bytes for SKB */
	read_len = read_len + 2;
	alloc_len = read_len;
	if (alloc_len % SDIO_BLOCK_SIZE ) {
		alloc_len -= (alloc_len % SDIO_BLOCK_SIZE );
		alloc_len += SDIO_BLOCK_SIZE;
	}
	/* Check if not exceeding CW1200 capabilities */
	if (WARN_ON_ONCE(alloc_len > EFFECTIVE_BUF_SIZE)) {
		atbm_printk_err("Read aligned len: %d\n",
			alloc_len);
	}
	
	skb_rx = hw_priv->recv_skb;
	
	if (WARN_ON(!skb_rx)){
		goto err;
	}

	atbm_skb_trim(skb_rx, 0);
	data = skb_rx->data;
	
	if (WARN_ON(!data)){
		goto err;
	}
	
	if (WARN_ON(atbm_data_read_unlock(hw_priv, data, alloc_len))){
		atbm_bh_halt(hw_priv);
		goto err;
	}
	
	/* Piggyback */
	ctrl_reg = __le16_to_cpu(
		((__le16 *)data)[alloc_len / 2 - 1]);
	
	if(atbm_bh_is_term(hw_priv) || atomic_read(&hw_priv->bh_term)){
		goto err;
	}

	atbm_skb_put(skb_rx, read_len);
	
	rx_continue_cnt++;
	
	skb_rx->pkt_type = ATBM_RX_RAW_FRAME;
	
	encap->check_seq = true;
	encap->wsm = (struct wsm_hdr *)skb_rx->data;
	encap->wsm_id = (__le32_to_cpu(encap->wsm->id) & 0xFFF)  & (~WSM_TX_LINK_ID(WSM_TX_LINK_ID_MAX));
	
	if (unlikely(WARN_ON(atbm_rx_serial(encap) == false))) {			
		atbm_hif_status_set(1);
		atbm_bh_halt(encap->hw_priv);
		goto err;
	}
	
	encap->check_seq = false;
	WARN_ON(atbm_rx_tasklet_process_encap(encap,skb_rx) == true);
	
	read_len_lsb = (ctrl_reg & ATBM_HIFREG_CONT_NEXT_LEN_LSB_MASK)*2;
	read_len_msb = (ctrl_reg & ATBM_HIFREG_CONT_NEXT_LEN_MSB_MASK)*2;
	
	read_len = ((read_len_msb>>2)+read_len_lsb);
	
	if(read_len)
		goto rx_continue;
	goto rx_check;
	
exit:
	if(rx_continue_cnt)
		atbm_sdio_napi_sched(encap);
	return rx_continue_cnt;
err:
	return -1;
}
#ifdef CONFIG_ATBM_SDIO_TX_PROCESS_NORMAL
void atbm_sdio_tx_bh(struct atbm_common *hw_priv)
{
#define WSM_SDIO_TX_MULT_BLOCK_SIZE	(SDIO_BLOCK_SIZE)
#define ATBM_SDIO_FREE_BUFF_ERR(condition,free,prev_free,xmiteds,hw_xmiteds)	\
	do{																			\
		if(condition)	{																\
			atbm_printk_err("%s[%d]:free(%x),prev_free(%x),xmiteds(%x),hw_xmiteds(%x)\n",__func__,__LINE__,free,prev_free,xmiteds,hw_xmiteds);	\
			BUG_ON(1);			\
		}\
	}while(0)
	struct hif_tx_encap encap;
	static u8 loop = 1;
	struct wsm_hdr_tx *wsm_tx;
	u32 putLen=0;
	u8 *need_confirm = NULL;
	int ret=0;
	int txMutiFrameCount=0;
#if (PROJ_TYPE>=ARES_A)
	u16 wsm_len_u16[2];
	u16 wsm_len_sum;
#endif	//	(PROJ_TYPE==ARES_A)	
	bool enough = false;

	prefetchw(hw_priv->xmit_buff);
xmit_continue:

	txMutiFrameCount = 0;
	putLen = 0;
	enough = false;
	need_confirm = NULL;
	do {
		
		enough = atbm_sdio_have_enough_space(hw_priv,1);
		
		if(enough == false){
			if(txMutiFrameCount > 0)
				break;
			else
				goto xmit_wait;
		}
		
		encap.data = hw_priv->xmit_buff + putLen;
		ret = wsm_get_tx(hw_priv,&encap);
		
		if (ret <= 0) {
			if(txMutiFrameCount > 0)
				break;
			else
				goto xmit_finished;
		}
		
		txMutiFrameCount++;
		wsm_tx = (struct wsm_hdr_tx*)(hw_priv->xmit_buff + putLen);

        
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
//          printk("cipher wsm_len:%x %x\n", wsm_len_u16[0], wsm_len_u16[1]);
#endif
#endif //(PROJ_TYPE==ARES_A)


		if (encap.tx_len <= 8)
			encap.tx_len = 16;

		if (encap.tx_len % (WSM_SDIO_TX_MULT_BLOCK_SIZE) ) {
			encap.tx_len -= (encap.tx_len % (WSM_SDIO_TX_MULT_BLOCK_SIZE) );
			encap.tx_len += WSM_SDIO_TX_MULT_BLOCK_SIZE;
		}

		/* Check if not exceeding atbm
		capabilities */
		if (WARN_ON_ONCE(encap.tx_len > EFFECTIVE_BUF_SIZE)) {
			atbm_printk_err("Write aligned len:"
			" %d\n", encap.tx_len);
		}
		
		wsm_tx->id &= __cpu_to_le32(~WSM_TX_SEQ(WSM_TX_SEQ_MAX));
		wsm_tx->id |= cpu_to_le32(WSM_TX_SEQ(hw_priv->wsm_tx_seq));

		spin_lock_bh(&hw_priv->tx_com_lock);
		ATBM_SDIO_FREE_BUFF_ERR(hw_priv->hw_bufs_free <= 0,hw_priv->hw_bufs_free,hw_priv->hw_bufs_free_init,hw_priv->n_xmits,hw_priv->hw_xmits);
		hw_priv->n_xmits ++;
		hw_priv->hw_bufs_free --;		
		ATBM_SDIO_FREE_BUFF_ERR(hw_priv->hw_bufs_free < 0,hw_priv->hw_bufs_free,hw_priv->hw_bufs_free_init,hw_priv->n_xmits,hw_priv->hw_xmits);
		spin_unlock_bh(&hw_priv->tx_com_lock);
		
		putLen += encap.tx_len;
		hw_priv->wsm_tx_seq = (hw_priv->wsm_tx_seq + 1) & WSM_TX_SEQ_MAX;

		if(encap.source){
			need_confirm = encap.source;
			atbm_printk_debug("%s:cmd free(%d),used(%d)\n",__func__,hw_priv->hw_bufs_free,hw_priv->hw_bufs_used);
			break;
		}
		
		hw_priv->wsm_txframe_num++;
		
		if (putLen+hw_priv->wsm_caps.sizeInpChBuf>SDIO_TX_MAXLEN){
			break;
		}
	}while(loop);
	BUG_ON(putLen == 0);
	hw_priv->buf_id_offset = txMutiFrameCount;
	atomic_add(1, &hw_priv->bh_tx);

	if(atbm_bh_is_term(hw_priv)){
		atbm_printk_err("%s:bh term\n",__func__);
		wsm_force_free_tx(hw_priv,(struct wsm_tx *)need_confirm);
		goto xmit_continue;
	}
#ifdef CONFIG_ATBM_SDIO_TX_HOLD	
	if (WARN_ON(atbm_data_write_unlock(hw_priv,hw_priv->xmit_buff, putLen))) {		
		atbm_printk_err("%s: xmit data err\n",__func__);
		goto xmit_err;
	}
#else
	if (WARN_ON(atbm_data_write(hw_priv,hw_priv->xmit_buff, putLen))) {		
		atbm_printk_err("%s: xmit data err\n",__func__);
		goto xmit_err;
	}
#endif
xmit_wait:	
	if((enough == false)&&(atbm_sdio_wait_enough_space(hw_priv,1) == false)){
		atbm_printk_err("%s: wait space timeout\n",__func__);
		goto xmit_err;
	}
	
	goto xmit_continue;
	
xmit_finished:	
	return;
xmit_err:
	wsm_force_free_tx(hw_priv,(struct wsm_tx *)need_confirm);
	atbm_bh_halt(hw_priv);
	goto xmit_continue;
}
#endif
void atbm_sdio_rx_bh(struct atbm_common *hw_priv)
{
	struct wsm_rx_encap encap;
	
	if(hw_priv->hard_irq == false){
#ifdef CONFIG_SDIO_IRQ_THREAD_PROCESS_DATA
#ifdef CONFIG_SDIO_THREAD_PROCESS_MAC80211_DATA
		struct sk_buff *skb ;
		struct sk_buff_head local_list;
		unsigned long flags;
		struct wsm_rx_encap encap;
		
		__atbm_skb_queue_head_init(&local_list);

		spin_lock_irqsave(&hw_priv->rx_frame_queue.lock,flags);
		hw_priv->bh_running  = true;
	restart:	
		bh_printk("%s: restart\n",__func__);
		atbm_skb_queue_splice_tail_init(&hw_priv->rx_frame_queue, &local_list);
		spin_unlock_irqrestore(&hw_priv->rx_frame_queue.lock,flags);

		encap.hw_priv = hw_priv;
		encap.check_seq = false;
		
		while ((skb = __atbm_skb_dequeue(&local_list)) != NULL) {
			if(unlikely(atomic_read(&hw_priv->bh_term)|| hw_priv->bh_error || (hw_priv->bh_thread == NULL)||
			   (atomic_read(&hw_priv->atbm_pluged)==0)))
			{
				atbm_dev_kfree_skb(skb);
				continue;
			}
			
			if(atbm_rx_tasklet_process_encap(&encap,skb) == false){
				BUG_ON(skb->pkt_type != ATBM_RX_WSM_GRO_FLUSH);
				atbm_dev_kfree_skb(skb);
			}
		}	
		
		spin_lock_irqsave(&hw_priv->rx_frame_queue.lock,flags);
		if(!atbm_skb_queue_empty(&hw_priv->rx_frame_queue))
			goto restart;
		hw_priv->bh_running = false;
		spin_unlock_irqrestore(&hw_priv->rx_frame_queue.lock,flags);
#endif
		return;		
#endif
	}
	hw_priv->sbus_ops->lock(hw_priv->sbus_priv);
	encap.hw_priv = hw_priv;
	encap.priv = NULL;
	encap.rx_func = atbm_rx_tasklet_process_encap;
	atbm_sdio_process_read_data(&encap);
	hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);
}
void atbm_irq_handler(void *priv)
{
	/* To force the device to be always-on, the host sets WLAN_UP to 1 */
	struct wsm_rx_encap *encap = (struct wsm_rx_encap *)priv;
	struct atbm_common *hw_priv = encap->priv;
	
	if(!hw_priv->init_done){
		atbm_printk_err("[BH] irq. init_done =0 drop\n");
		return;
	}

	if (atbm_bh_is_term(hw_priv))
		return;
	
	hw_priv->hard_irq = !in_interrupt() ? false : true;
	if(hw_priv->hard_irq == false)	{
#ifdef CONFIG_SDIO_IRQ_THREAD_PROCESS_DATA
		int rx_counter = 0;		
		__atbm_irq_enable(hw_priv,0);
		encap->hw_priv = hw_priv;
rx_continue:
		rx_counter = atbm_sdio_process_read_data(encap);
		if(rx_counter >= ATBM_MAX_OVERFLOW_SIZE){
			hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);
			atbm_printk_debug("%s:over flow\n",__func__);
			schedule_timeout_interruptible(msecs_to_jiffies(10));			
			hw_priv->sbus_ops->lock(hw_priv->sbus_priv);
			goto rx_continue;
		}
		if (atbm_bh_is_term(hw_priv) || atomic_read(&hw_priv->bh_term)){
			wake_up(&hw_priv->bh_wq);
		}
		__atbm_irq_enable(hw_priv,1);
		return;
#endif
	}
	if(hw_priv->sbus_ops->sbus_rev_schedule)
		hw_priv->sbus_ops->sbus_rev_schedule(hw_priv->sbus_priv);
	else if (atomic_add_return(1, &hw_priv->bh_rx) == 1){
		wake_up(&hw_priv->bh_wq);
	}
	return;
}

int atbm_bh_suspend(struct atbm_common *hw_priv)
{
	int i =0;
	int ret = 0;
	struct atbm_vif *priv = NULL;
	atbm_printk_pm("[BH] try suspend.\n");
	if (hw_priv->bh_error) {
		atbm_printk_warn("BH error -- can't suspend\n");
		return -EINVAL;
	}
#ifdef MCAST_FWDING

 	atbm_for_each_vif(hw_priv, priv, i) {
		if (!priv)
			continue;
		if ( (priv->multicast_filter.enable)
			&& (priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP) ) {
			wsm_release_buffer_to_fw(priv,
				(hw_priv->wsm_caps.numInpChBufs - 1));
			break;
		}
	}
#endif
	atomic_set(&hw_priv->bh_suspend, ATBM_APOLLO_BH_SUSPEND);
	wake_up(&hw_priv->bh_wq);
	ret = atbm_wait_event_timeout_stay_awake(hw_priv,hw_priv->bh_evt_wq, hw_priv->bh_error ||
		(ATBM_APOLLO_BH_SUSPENDED == atomic_read(&hw_priv->bh_suspend)),
		 60 * HZ,false) ? 0 : -ETIMEDOUT;
	
	if((ret == 0)&&(hw_priv->sbus_ops->sbus_bh_suspend))
		ret = hw_priv->sbus_ops->sbus_bh_suspend(hw_priv->sbus_priv);

	return ret;
}

int atbm_bh_resume(struct atbm_common *hw_priv)
{
	int i =0;
	int ret;
#ifdef MCAST_FWDING
	struct atbm_vif *priv =NULL;
#endif

	atbm_printk_pm("[BH] try resume.\n");
	if (hw_priv->bh_error) {
		atbm_printk_warn("BH error -- can't resume\n");
		return -EINVAL;
	}

	atomic_set(&hw_priv->bh_suspend, ATBM_APOLLO_BH_RESUME);
	wake_up(&hw_priv->bh_wq);
    atbm_printk_pm("wakeup bh,wait evt_wq\n");
#ifdef MCAST_FWDING
	ret = atbm_wait_event_timeout_stay_awake(hw_priv,hw_priv->bh_evt_wq, hw_priv->bh_error ||
				(ATBM_APOLLO_BH_RESUMED == atomic_read(&hw_priv->bh_suspend)),
				1 * HZ,false) ? 0 : -ETIMEDOUT;

	atbm_for_each_vif(hw_priv, priv, i) {
		if (!priv)
			continue;
		if ((priv->join_status == ATBM_APOLLO_JOIN_STATUS_AP)
				&& (priv->multicast_filter.enable)) {
			u8 count = 0;
			WARN_ON(wsm_request_buffer_request(priv, &count));
			bh_printk(
				"[BH] BH resume. Reclaim Buff %d \n",count);
			break;
		}
	}
#else
	ret = atbm_wait_event_timeout_stay_awake(hw_priv,hw_priv->bh_evt_wq,hw_priv->bh_error ||
		(ATBM_APOLLO_BH_RESUMED == atomic_read(&hw_priv->bh_suspend)),
		100 * HZ,false) ? 0 : -ETIMEDOUT;
#endif
	if((ret == 0)&&(hw_priv->sbus_ops->sbus_bh_resume))
		ret = hw_priv->sbus_ops->sbus_bh_resume(hw_priv->sbus_priv);
	return ret;
}
#ifdef MCAST_FWDING
#ifndef USB_BUS

//just for sdio
int wsm_release_buffer_to_fw(struct atbm_vif *priv, int count)
{
	int i;
	u8 flags;
	struct wsm_buf *buf;
	u32 buf_len;
	struct wsm_hdr_tx *wsm;
	struct atbm_common *hw_priv = priv->hw_priv;


	if (priv->join_status != ATBM_APOLLO_JOIN_STATUS_AP) {
		return 0;
	}

	bh_printk( "Rel buffer to FW %d, %d\n", count, hw_priv->hw_bufs_used);

	for (i = 0; i < count; i++) {
		if ((hw_priv->hw_bufs_used + 1) < hw_priv->wsm_caps.numInpChBufs) {
			flags = i ? 0: 0x1;

			wsm_alloc_tx_buffer(hw_priv);

			buf = &hw_priv->wsm_release_buf[i];
			buf_len = buf->data - buf->begin;

			/* Add sequence number */
			wsm = (struct wsm_hdr_tx *)buf->begin;
			BUG_ON(buf_len < sizeof(*wsm));

			wsm->id &= __cpu_to_le32(
				~WSM_TX_SEQ(WSM_TX_SEQ_MAX));
			wsm->id |= cpu_to_le32(
				WSM_TX_SEQ(hw_priv->wsm_tx_seq));

			atbm_printk_bh("REL %d\n", hw_priv->wsm_tx_seq);

			if (WARN_ON(atbm_data_write(hw_priv,
				buf->begin, buf_len))) {
				break;
			}


			hw_priv->buf_released = 1;
			hw_priv->wsm_tx_seq = (hw_priv->wsm_tx_seq + 1) & WSM_TX_SEQ_MAX;
		} else
			break;
	}

	if (i == count) {
		return 0;
	}

	/* Should not be here */
	atbm_printk_err("[BH] Less HW buf %d,%d.\n", hw_priv->hw_bufs_used,
			hw_priv->wsm_caps.numInpChBufs);
	WARN_ON(1);

	return -1;
}
#endif //USB_BUS
#endif

void atbm_put_skb(struct atbm_common *hw_priv, struct sk_buff *skb)
{
	if (hw_priv->skb_cache){
		//printk("%s atbm_kfree_skb skb=%p\n",__func__,skb);
		atbm_dev_kfree_skb(skb);
	}
	else
		hw_priv->skb_cache = skb;
}

int atbm_bh_read_ctrl_reg(struct atbm_common *hw_priv,
					  u16 *ctrl_reg)
{
	int ret=0,retry=0;
	while (retry <= MAX_RETRY) {
		ret = atbm_reg_read_16(hw_priv,
				ATBM_HIFREG_CONTROL_REG_ID, ctrl_reg);
		if(!ret){
				break;
		}else{
			/*reset sdio internel reg by send cmd52 to abort*/
			WARN_ON(hw_priv->sbus_ops->abort(hw_priv->sbus_priv));
			retry++;
			mdelay(retry);
			atbm_printk_err(
				"[BH] Failed to read control register.ret=%x\n",ret);
		}
	}
	return ret;
}
int atbm_bh_read_ctrl_reg_unlock(struct atbm_common *hw_priv,
					  u16 *ctrl_reg)
{
	int ret=0,retry=0;
	while (retry <= MAX_RETRY) {
		ret = atbm_reg_read_16_unlock(hw_priv,
				ATBM_HIFREG_CONTROL_REG_ID, ctrl_reg);
		if(!ret){
				break;
		}else{
			/*reset sdio internel reg by send cmd52 to abort*/
			WARN_ON(hw_priv->sbus_ops->abort(hw_priv->sbus_priv));
			retry++;
			mdelay(retry);
			atbm_printk_err(
				"[BH] Failed to read control register.ret=%x\n",ret);
		}
	}
	return ret;
}

//just ARESB have this function
//used this function to clear sdio rtl bug register
// if not do this sdio direct mode (wr/read reigster) will not work
// this function is the same to atbm_data_force_write (used queue mode clear bit to clear)
// 
int atbm_powerave_sdio_sync(struct atbm_common *hw_priv)
{
	int ret=0;
	//int retry=0;
	u32 config_reg;
	ret = atbm_reg_read_unlock_32(hw_priv, ATBM_HIFREG_CONFIG_REG_ID, &config_reg);
	if (ret < 0) {
		atbm_printk_err("%s: enable_irq: can't read config register.\n", __func__);
	}

	if(config_reg & ATBM_HIFREG_PS_SYNC_SDIO_FLAG)
	{
		atbm_printk_err("%s:%d\n",__func__,__LINE__);
		//atbm_hw_buff_reset(hw_priv);
		config_reg |= ATBM_HIFREG_PS_SYNC_SDIO_CLEAN;
		atbm_reg_write_unlock_32(hw_priv,ATBM_HIFREG_CONFIG_REG_ID,config_reg);
	}
	return ret;
}
int atbm_device_wakeup(struct atbm_common *hw_priv)
{
	u16 ctrl_reg;
	int ret=0;
	int loop = 1;

#ifdef PS_SETUP

	/* To force the device to be always-on, the host sets WLAN_UP to 1 */
	ret = atbm_reg_write_16(hw_priv, ATBM_HIFREG_CONTROL_REG_ID,
			ATBM_HIFREG_CONT_WUP_BIT);
	if (WARN_ON(ret))
		return ret;
#endif
	while(1){
		mdelay(5);
		ret = atbm_bh_read_ctrl_reg(hw_priv, &ctrl_reg);
		if (WARN_ON(ret)){
		}
		/* If the device returns WLAN_RDY as 1, the device is active and will
		 * remain active. */
		atbm_printk_bh("Rdy =%x\n",ctrl_reg);
		if (ctrl_reg & ATBM_HIFREG_CONT_RDY_BIT) {
			atbm_printk_bh("[BH] Device awake.<%d>\n",loop);
			ret= 1;
			break;
		}
	}
	return ret;
}

static int atbm_bh(void *arg)
{
	struct atbm_common *hw_priv = arg;
	struct atbm_vif *priv = NULL;
	int rx, tx=0, term, suspend;
	u16 ctrl_reg = 0;
	int pending_tx = 0;
	long status;
	bool powersave_enabled;
	int i;
	
#define __ALL_HW_BUFS_USED (hw_priv->hw_bufs_used)
	while (1) {
		powersave_enabled = 1;
		atbm_hw_vif_read_lock(&hw_priv->vif_list_lock);
		atbm_for_each_vif_safe(hw_priv, priv, i) 
		{
			if (!priv)
				continue;
			powersave_enabled &= !!priv->powersave_enabled;
		}
		atbm_hw_vif_read_unlock(&hw_priv->vif_list_lock);
		if (!__ALL_HW_BUFS_USED
				&& powersave_enabled
				&& !hw_priv->device_can_sleep
				&& !atomic_read(&hw_priv->recent_scan)) {
			status = 4*HZ;
			bh_printk( "[BH] No Device wakedown.\n");
#ifdef PS_SETUP
			WARN_ON(atbm_reg_write_16(hw_priv,
						ATBM_HIFREG_CONTROL_REG_ID, 0));
			hw_priv->device_can_sleep = true;
#endif
		} else if (__ALL_HW_BUFS_USED)
			/* Interrupt loss detection */
			status = 4*HZ;
		else
			status = 4*HZ;
		/* If a packet has already been txed to the device then read the
		   control register for a probable interrupt miss before going
		   further to wait for interrupt; if the read length is non-zero
		   then it means there is some data to be received */
		status = wait_event_interruptible_timeout(hw_priv->bh_wq, ({
				rx = atomic_xchg(&hw_priv->bh_rx, 0);
				tx = atomic_xchg(&hw_priv->bh_tx, 0);
				term = atomic_xchg(&hw_priv->bh_term, 0);
				suspend = pending_tx ?
					0 : atomic_read(&hw_priv->bh_suspend);
				(rx || tx || term || suspend || hw_priv->bh_error || atomic_read(&hw_priv->bh_halt));
			}), status);
		
		if (status < 0 || term || hw_priv->bh_error){
			atbm_bh_read_ctrl_reg(hw_priv, &ctrl_reg);
			//printk(" ++ctrl_reg= %x,\n",ctrl_reg);
			atbm_printk_err("%s BH thread break %ld %d %d ctrl_reg=%x\n",__func__,status,term,hw_priv->bh_error,ctrl_reg);
			break;
		}
		if(atomic_read(&hw_priv->bh_halt)){
			atomic_set(&hw_priv->atbm_pluged,0);
			atbm_printk_err("%s:bh_halt\n",__func__);
			if(hw_priv->sbus_ops->lmac_restart(hw_priv->sbus_priv) != 0){
				atomic_xchg(&hw_priv->bh_halt,0);
				atomic_set(&hw_priv->bh_term,1);
				hw_priv->bh_error = 1;
				break;
			}
		}
		if (0)
		{
			unsigned long timestamp = jiffies;
			long timeout;
			bool pending = false;
			int i;
			
			atbm_printk_warn("Missed interrupt Status =%d, buffused=%d\n",(int)status,(int)__ALL_HW_BUFS_USED);
			rx = 1;
			atbm_printk_debug("[bh] next wsm_rx_seq %d wsm_tx_seq %d\n",hw_priv->wsm_rx_seq,hw_priv->wsm_tx_seq);
			atbm_printk_debug("[bh] wsm_hiftx_cmd_num %d wsm_hif_cmd_conf_num %d\n",hw_priv->wsm_hiftx_num,hw_priv->wsm_hifconfirm_num);
			atbm_printk_debug("[bh] wsm_txframe_num %d wsm_txconfirm_num %d\n",hw_priv->wsm_txframe_num,hw_priv->wsm_txconfirm_num);
			atbm_printk_debug("[bh] num_pending[0]=%d num_pending[1]=%d pending[2]=%d pending[3]=%d\n",
															hw_priv->tx_queue[0].num_pending,
															hw_priv->tx_queue[1].num_pending,
															hw_priv->tx_queue[2].num_pending,
															hw_priv->tx_queue[3].num_pending);
			//atbm_monitor_pc(hw_priv);

			atbm_bh_read_ctrl_reg(hw_priv, &ctrl_reg);
			atbm_printk_err(" ++ctrl_reg= %x,\n",ctrl_reg);

			/* Get a timestamp of "oldest" frame */
			for (i = 0; i < 4; ++i)
				pending |= atbm_queue_get_xmit_timestamp(
						&hw_priv->tx_queue[i],
						&timestamp, -1,
						hw_priv->pending_frame_id);

			/* Check if frame transmission is timed out.
			 * Add an extra second with respect to possible
			 * interrupt loss. */
			timeout = timestamp +
					WSM_CMD_LAST_CHANCE_TIMEOUT +
					1 * HZ  -
					jiffies;

			/* And terminate BH tread if the frame is "stuck" */
			if (pending && timeout < 0) {
			}
		} else if (!status) {
			if (!hw_priv->device_can_sleep
					&& !atomic_read(&hw_priv->recent_scan)) {
			        bh_printk(KERN_ERR "[BH] Device wakedown. Timeout.\n");
#ifdef PS_SETUP
				WARN_ON(atbm_reg_write_16(hw_priv,
						ATBM_HIFREG_CONTROL_REG_ID, 0));
				hw_priv->device_can_sleep = true;
#endif//#ifdef PS_SETUP
			}
			continue;
		} else if (suspend) {
			atbm_printk_err("[BH] Device suspend.\n");
			powersave_enabled = 1;
			atbm_hw_vif_read_lock(&hw_priv->vif_list_lock);
			atbm_for_each_vif_safe(hw_priv, priv, i) {
				if (!priv)
					continue;
				powersave_enabled &= !!priv->powersave_enabled;
			}
			atbm_hw_vif_read_unlock(&hw_priv->vif_list_lock);
			if (powersave_enabled) {
				bh_printk( "[BH] No Device wakedown. Suspend.\n");
			}

			atomic_set(&hw_priv->bh_suspend, ATBM_APOLLO_BH_SUSPENDED);
			wake_up(&hw_priv->bh_evt_wq);
			atbm_printk_err("[BH] wait resume..\n");
			status = wait_event_interruptible(hw_priv->bh_wq,
					ATBM_APOLLO_BH_RESUME == atomic_read(
						&hw_priv->bh_suspend));
			if (status < 0) {
				atbm_printk_err("%s: Failed to wait for resume: %ld.\n",
					__func__, status);
				break;
			}
			atbm_printk_err("[BH] Device resume.\n");
			atomic_set(&hw_priv->bh_suspend, ATBM_APOLLO_BH_RESUMED);
			wake_up(&hw_priv->bh_evt_wq);
			atomic_add(1, &hw_priv->bh_rx);
			continue;
		}
	}

	if (!term) {
		int loop = 3;
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "[BH] Fatal error, exitting.\n");
		hw_priv->bh_error = 1;
		while(loop-->0){
			atbm_monitor_pc(hw_priv);
			msleep(10);
		}

//		hw_priv->sbus_ops->wtd_wakeup(hw_priv->sbus_priv);
		atbm_hw_vif_read_lock(&hw_priv->vif_list_lock);
		atbm_for_each_vif_safe(hw_priv, priv, i) {
			if (!priv)
				continue;
//			ieee80211_driver_hang_notify(priv->vif, GFP_KERNEL);
		}
		atbm_hw_vif_read_unlock(&hw_priv->vif_list_lock);
#ifdef CONFIG_PM
		atbm_pm_stay_awake(&hw_priv->pm_state, 3*HZ);
#endif
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "[BH] Fatal error, exitting.1\n");
		/* TODO: schedule_work(recovery) */
#ifndef HAS_PUT_TASK_STRUCT
		/* The only reason of having this stupid code here is
		 * that __put_task_struct is not exported by kernel. */
		for (;;) {
			int status = wait_event_interruptible(hw_priv->bh_wq, ({
				term = atomic_xchg(&hw_priv->bh_term, 0);
				(term);
				}));
			
			atbm_dbg(ATBM_APOLLO_DBG_ERROR, "[BH] Fatal error, exitting.2\n");
			if (status || term)
				break;
		}
#endif
	}
	atbm_printk_exit("atbm_wifi_BH_thread stop ++\n");
	/*
	add this code just because 'linux kernel' need kthread not exit ,
	before kthread_stop func call,
	*/
	if(term)
	{
		//clear pendding cmd
		if(!mutex_trylock(&hw_priv->wsm_cmd_mux))
		{
			spin_lock_bh(&hw_priv->wsm_cmd.lock);
			if(hw_priv->wsm_cmd.cmd != 0xFFFF)
			{
				hw_priv->wsm_cmd.ret = -1;
				hw_priv->wsm_cmd.done = 1;
				spin_unlock_bh(&hw_priv->wsm_cmd.lock);
				atbm_printk_exit("cancle current pendding cmd,release wsm_cmd.lock\n");
				wake_up(&hw_priv->wsm_cmd_wq);
				msleep(2);
				spin_lock_bh(&hw_priv->wsm_cmd.lock);
			}
			spin_unlock_bh(&hw_priv->wsm_cmd.lock);
		}
		else
		{
			mutex_unlock(&hw_priv->wsm_cmd_mux);
		}

		/*
		*cancle the current scanning process
		*/
		mutex_lock(&hw_priv->conf_mutex);
		if(atomic_read(&hw_priv->scan.in_progress))
		{
			struct atbm_vif *scan_priv = ABwifi_hwpriv_to_vifpriv(hw_priv,
						hw_priv->scan.if_id);
			bool scanto_running = false;
			atbm_priv_vif_list_read_unlock(&scan_priv->vif_lock);
			mutex_unlock(&hw_priv->conf_mutex);
			scanto_running = atbm_hw_cancel_delayed_work(&hw_priv->scan.timeout,true);
			mutex_lock(&hw_priv->conf_mutex);
			if(scanto_running>0)
			{
				hw_priv->scan.curr = hw_priv->scan.end;
				mutex_unlock(&hw_priv->conf_mutex);
				atbm_scan_timeout(&hw_priv->scan.timeout.work);
				mutex_lock(&hw_priv->conf_mutex);
			}
		}
		mutex_unlock(&hw_priv->conf_mutex);
		{
			u8 i = 0;
			//cancel pendding work
			#define ATBM_CANCEL_PENDDING_WORK(work,work_func)			\
				do{														\
					if(atbm_hw_cancel_queue_work(work,true)==true)			\
					{													\
						work_func(work);								\
					}													\
				}														\
				while(0)
					
#ifdef CONFIG_ATBM_SUPPORT_P2P
			if(atbm_hw_cancel_delayed_work(&hw_priv->rem_chan_timeout,true))
				atbm_rem_chan_timeout(&hw_priv->rem_chan_timeout.work);
#endif
			ATBM_CANCEL_PENDDING_WORK(&hw_priv->scan.work,atbm_scan_work);
#ifdef ATBM_SUPPORT_SMARTCONFIG
			ATBM_CANCEL_PENDDING_WORK(&hw_priv->scan.smartwork,atbm_smart_scan_work);
			ATBM_CANCEL_PENDDING_WORK(&hw_priv->scan.smartsetChanwork,atbm_smart_setchan_work);
			ATBM_CANCEL_PENDDING_WORK(&hw_priv->scan.smartstopwork,atbm_smart_stop_work);
			atbm_del_timer_sync(&hw_priv->smartconfig_expire_timer);
			#endif
			atbm_for_each_vif(hw_priv, priv, i) {
				if(priv == NULL)
					continue;
				ATBM_CANCEL_PENDDING_WORK(&priv->set_tim_work, atbm_set_tim_work);
				ATBM_CANCEL_PENDDING_WORK(&priv->multicast_start_work,atbm_multicast_start_work);
				ATBM_CANCEL_PENDDING_WORK(&priv->multicast_stop_work, atbm_multicast_stop_work);
				atbm_del_timer_sync(&priv->mcast_timeout);
			}
		}
	}
	while(term){
		if(kthread_should_stop()){
			break;
		}
		schedule_timeout_uninterruptible(msecs_to_jiffies(100));
	}
	atbm_printk_exit("atbm_wifi_BH_thread stop --\n");
	return 0;
}
