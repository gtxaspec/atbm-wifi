/*
 * Mac80211 USB driver for altobeam APOLLO device
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
#ifdef CONFIG_SUPPORT_HIF_TRACE
#undef CONFIG_USB_AGGR_URB_TX
#define CONFIG_USB_AGGR_URB_TX
#endif

#ifdef CONFIG_USB_AGGR_URB_TX
#undef CONFIG_USE_DMA_ADDR_BUFFER
#define CONFIG_USE_DMA_ADDR_BUFFER
#endif //ATBM_NEW_USB_AGGR_TX

 #define DEBUG 1
#include <linux/version.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <net/atbm_mac80211.h>
#include <linux/kthread.h>
#include <linux/device.h>
#include <linux/usb.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/kref.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>

#include "mac80211/ieee80211_i.h"
#include "mac80211/driver-ops.h"
#include "hwio.h"
#include "apollo.h"
#include "sbus.h"
#include "apollo_plat.h"
#include "debug.h"
#include "bh.h"
#include "svn_version.h"
#include "module_fs.h"
#include "internal_cmd.h"
#if defined(CONFIG_HAS_EARLYSUSPEND)
#ifdef ATBM_PM_USE_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#endif

#ifdef ATBM_PM_USE_EARLYSUSPEND
#pragma message("Suspend Remove Interface")
#endif


#ifdef CONFIG_USB_AGGR_URB_TX
#pragma message("Usb Aggr Tx")
#endif

#ifdef CONFIG_USE_DMA_ADDR_BUFFER
#pragma message("Usb Dma Buff")
#endif

#ifdef CONFIG_WSM_CMD_XMIT_DIRECTLY
#pragma message("Cmd Xmit Directly")
#endif

#ifdef CONFIG_USB_DATA_XMIT_DIRECTLY
#pragma message("Date Xmit Directly")
#endif

#ifdef CONFIG_MODDRVNAME
#define WIFI_MODDRVNAME CONFIG_MODDRVNAME
#pragma message(WIFI_MODDRVNAME)

#else
#define WIFI_MODDRVNAME "atbm_wlan"
#endif

#ifdef CONFIG_PLFDEVNAME
#define WIFI_PLFDEVNAME CONFIG_PLFDEVNAME
#pragma message(WIFI_PLFDEVNAME)

#else
#define WIFI_PLFDEVNAME "atbmusbwifi"
#endif


#define __PRINT_VALUE(x) #x
#define PRINT_VALUE(x) #x"="__PRINT_VALUE(x)


#ifdef CONFIG_USBVID
#define WIFI_USB_VID CONFIG_USBVID
#pragma message(PRINT_VALUE(WIFI_USB_VID))
#else
#define WIFI_USB_VID 0x007a
#endif

#ifdef CONFIG_USBPID
#define WIFI_USB_PID CONFIG_USBPID
#pragma message(PRINT_VALUE(WIFI_USB_PID))

#else
#define WIFI_USB_PID 0x8888
#endif



//#define DBG_EVENT_LOG
#include "dbg_event.h"
MODULE_ALIAS(WIFI_MODDRVNAME);
#define WSM_TX_SKB 1

#define ATBM_USB_EP0_MAX_SIZE 64
#define ATBM_USB_EP1_MAX_RX_SIZE 512
#define ATBM_USB_EP2_MAX_TX_SIZE 512

#define ATBM_USB_VENQT_WRITE  0x40
#define ATBM_USB_VENQT_READ 0xc0

#define usb_printk(...)
/*usb vendor define type, EP0, bRequest*/
enum {
	VENDOR_HW_READ=0,
	VENDOR_HW_WRITE=1,
	VENDOR_HW_RESVER=2,
	VENDOR_SW_CPU_JUMP=3,/*cpu jump to real lmac code,after fw download*/
	VENDOR_SW_READ=4,
	VENDOR_SW_WRITE=5,	
	VENDOR_DBG_SWITCH=6,
	VENDOR_HW_RESET	=6,
	VENDOR_EP0_CMD=7,
};

 enum atbm_system_action{
 	ATBM_SYSTEM_REBOOT,
	ATBM_SYSTEM_RMMOD,
	ATBM_SYSTEM_NORMAL,
	ATBM_SYSTEM_MAX,
 };

extern void atbm_wifi_run_status_set(int status);
 int atbm_usb_pm(struct sbus_priv *self, bool  auto_suspend);
struct sbus_priv* atbm_wifi_comb_buspriv_get(void);
 int atbm_usb_pm_async(struct sbus_priv *self, bool  auto_suspend);
 void atbm_usb_urb_put(struct sbus_priv *self,unsigned long *bitmap,int id,int tx);
 int atbm_usb_urb_get(struct sbus_priv *self,unsigned long *bitmap,int max_urb,int tx);
 static void atbm_usb_lock(struct sbus_priv *self);
 static void atbm_usb_unlock(struct sbus_priv *self);
 static void atbm_usb_receive_data_complete(struct urb *urb);
 static void atbm_usb_xmit_data_complete(struct urb *urb);
#ifdef CONFIG_ATBM_BLE
int atbm_ioctl_add(void);
void atbm_ioctl_free(void);
#endif


#ifdef CONFIG_USE_DMA_ADDR_BUFFER
#define PER_PACKET_LEN 2048 //must pow 512
 char * atbm_usb_get_txDMABuf(struct sbus_priv *self); 
 char * atbm_usb_pick_txDMABuf(struct sbus_priv *self); 
 void atbm_usb_free_txDMABuf(struct sbus_priv *self);
 void atbm_usb_free_txDMABuf_all(struct sbus_priv *self,u8 * buffer,int cnt);
 

 #define ATBM_USB_ALLOC_URB(iso)		usb_alloc_urb(iso, GFP_ATOMIC)
#define ATBM_USB_SUBMIT_URB(pUrb)		usb_submit_urb(pUrb, GFP_ATOMIC)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0))

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
#define atbm_usb_buffer_alloc(dev, size, dma) usb_alloc_coherent((dev), (size), (in_interrupt() ? GFP_ATOMIC : GFP_KERNEL), (dma))
#define atbm_usb_buffer_free(dev, size, addr, dma) usb_free_coherent((dev), (size), (addr), (dma))
#else //(LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
#define atbm_usb_buffer_alloc(dev, size, dma) usb_buffer_alloc((dev), (size), (in_interrupt() ? GFP_ATOMIC : GFP_KERNEL), (dma))
#define atbm_usb_buffer_free(dev, size, addr, dma) usb_buffer_free((dev), (size), (addr), (dma))
#endif

#else
#define atbm_usb_buffer_alloc(_dev, _size, _dma)	atbm_kmalloc(_size, GFP_ATOMIC)
#define atbm_usb_buffer_free(_dev, _size, _addr, _dma)	atbm_kfree(_addr) 
#endif //#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0)



#define atbm_dma_addr_t					dma_addr_t
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 0))
#define ATBM_USB_FILL_HTTX_BULK_URB(pUrb,	\
				pUsb_Dev,	\
				uEndpointAddress,		\
				pTransferBuf,			\
				BufSize,				\
				Complete,	\
				pContext,				\
				TransferDma)				\
  				do{	\
					usb_fill_bulk_urb(pUrb, pUsb_Dev, usb_sndbulkpipe(pUsb_Dev, uEndpointAddress),	\
								pTransferBuf, BufSize, Complete, pContext);	\
					pUrb->transfer_dma	= TransferDma; \
					pUrb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;	\
				}while(0)
#else
#define ATBM_USB_FILL_HTTX_BULK_URB(pUrb,	\
				pUsb_Dev,				\
				uEndpointAddress,		\
				pTransferBuf,			\
				BufSize,				\
				Complete,				\
			       pContext,	\
					TransferDma)	\
  				do{	\
					FILL_BULK_URB(pUrb, pUsb_Dev, usb_sndbulkpipe(pUsb_Dev, uEndpointAddress),	\
								pTransferBuf, BufSize, Complete, pContext);	\
				}while(0)
#endif	

#endif //#ifdef CONFIG_USE_DMA_ADDR_BUFFER
#ifdef CONFIG_NFRAC_40M
#define DPLL_CLOCK 40
#elif defined (CONFIG_NFRAC_26M)
#define DPLL_CLOCK 26
#endif
struct build_info{
	int ver;
	int dpll;
	char driver_info[64];
};
typedef int (*sbus_complete_handler)(struct urb *urb);
static int atbm_usb_receive_data(struct sbus_priv *self,unsigned int addr,void *dst, int count,sbus_callback_handler hander);
static int atbm_usb_xmit_data(struct sbus_priv *self,unsigned int addr,const void *pdata, int len,sbus_callback_handler hander);

#if 0
const char DRIVER_INFO[]={"[===USB-APOLLO=="__DATE__" "__TIME__"""=====]"};
#else
#if (PROJ_TYPE==CRONUS)
#define PROJ_TYPE_STR "[===USB-CRONUS=="
#else
#define PROJ_TYPE_STR "[===USB-OTHER=="
#endif  // (PROJ_TYPE==CRONUS)
#endif
#pragma	message(PROJ_TYPE_STR)
const char DRIVER_INFO[]={PROJ_TYPE_STR};

#ifdef CONFIG_ATBM_BUILD_TIME
char *buildtime = CONFIG_ATBM_BUILD_TIME;
#else
char *buildtime = "";
#endif

static int driver_build_info(void)
{
	struct build_info build;
	build.ver=DRIVER_VER;
	build.dpll=DPLL_CLOCK;
	memset(build.driver_info,0,sizeof(build.driver_info));
	atbm_printk_init("SVN_VER=%d,DPLL_CLOCK=%d,BUILD_TIME=[%s]\n",build.ver,build.dpll,buildtime);
	return 0;
}
int wifi_module_exit =0;
#define atbm_wifi_get_status()			ATBM_VOLATILE_GET(&wifi_module_exit)
#define atbm_wifi_set_status(status)	ATBM_VOLATILE_SET(&wifi_module_exit,status)
//in atbm_usb_probe set 1, exit atbm_usb_probe set 0, 
//when  wifi_usb_probe_doing =1 ,can't not wifi_module_exit, must wait until wifi_usb_probe_doing==0
static int wifi_usb_probe_doing =0;
static int wifi_tx_urb_pending =0;
#define TEST_URB_NUM 5
#ifdef CONFIG_USB_AGGR_URB_TX
#define URB_AGGR_NUM 128
#define PER_BUFF_AGGR_NUM 32
#ifdef CONFIG_SUPPORT_HIF_TRACE
#define TX_URB_NUM 1
#define RX_URB_NUM 1
#else
#define TX_URB_NUM 4
#define RX_URB_NUM 16
#endif
#define BUFF_ALLOC_LEN (PER_BUFF_AGGR_NUM*PER_PACKET_LEN)
#else
#define TX_URB_NUM 128
#define RX_URB_NUM 64
#define TX_URB_INTR_INTERVAL	(TX_URB_NUM/2)
#endif
struct sbus_urb {
	struct sbus_priv* obj;
	struct urb *test_urb;
	struct sk_buff *test_skb;
	void *data;
	int urb_id;
	int offset;
	int retries;
#ifdef CONFIG_USE_DMA_ADDR_BUFFER
	atbm_dma_addr_t dma_transfer_addr;	/* (in) dma addr for transfer_buffer */
	
	int pallocated_buf_len;
	int frame_cnt;
	int dma_buff_alloced;
#endif
	u8 *pallocated_buf;
};
struct dvobj_priv{
	struct usb_device *pusbdev;
	struct usb_interface *pusbintf;
	//struct sk_buff *rx_skb;
#ifdef CONFIG_USE_DMA_ADDR_BUFFER
	atbm_dma_addr_t tx_dma_addr;
	char * tx_dma_addr_buffer;
	unsigned long tx_dma_addr_buffer_len;
	struct sbus_urb *tx_save_urb;
	int tx_save_urb_data_len;
#ifdef CONFIG_USB_AGGR_URB_TX
	char * NextAllocPost;
	char * NextFreePost;
	char * tx_dma_addr_buffer_end;
	bool tx_dma_addr_buffer_full;
	int  free_dma_buffer_cnt;
	int  total_dma_buffer_cnt;
#endif  //CONFIG_USB_AGGR_URB_TX
#endif //CONFIG_USE_DMA_ADDR_BUFFER
	struct sbus_urb rx_urb[RX_URB_NUM];
	struct sbus_urb tx_urb[TX_URB_NUM];
	int    n_submitteds;
	struct usb_anchor tx_submitted;
	struct usb_anchor rx_submitted;
	struct usb_anchor xmit_err;
	struct usb_anchor recv_err;
	struct usb_anchor comp_err;
#ifdef CONDIF_ATBM_CTRL_REQUEST_ASYNC
	struct usb_anchor ctrl_submitted;
	atomic_t		  ctrl_err;
#endif
	unsigned long	 rx_urb_map[BITS_TO_LONGS(RX_URB_NUM)];
	unsigned long	 tx_urb_map[BITS_TO_LONGS(TX_URB_NUM)];
	struct urb *cmd_urb;
	struct sbus_priv *self;
	struct net_device *netdev;
	u8	usb_speed; // 1.1, 2.0 or 3.0
	u8	nr_endpoint;
	int ep_in;
	int ep_in_size;
	int ep_out;
	int ep_out_size;
	int	ep_num[6]; //endpoint number
};
struct sbus_priv {
	struct dvobj_priv *drvobj;
	struct atbm_common	*core;
	struct atbm_platform_data *pdata;
	//struct sk_buff *rx_skb;
	//struct sk_buff *tx_skb;
	
#ifdef USB_USE_TASTLET_TXRX
	struct tasklet_struct tx_cmp_tasklet;
	struct tasklet_struct rx_cmp_tasklet;
#else
	struct atbm_workqueue_struct 	*tx_workqueue;
	struct atbm_workqueue_struct 	*rx_workqueue;
	struct atbm_work_struct rx_complete_work;
	struct atbm_work_struct tx_complete_work;
#endif
	void 	__rcu * urb_wkq;
	struct mutex	urb_wkq_mutex;

	struct atbm_work_struct urb_wk;
	bool            rx_running;
	unsigned int   	tx_hwChanId;
	//sbus_callback_handler	tx_callback_handler;
	//sbus_callback_handler	rx_callback_handler;
	spinlock_t		lock;
	spinlock_t      xmit_path_lock;
	struct mutex 	sbus_mutex;
	int 			auto_suspend;
	int 			suspend;
	u8 *usb_data;
	u8 *usb_req_data;
#if defined(CONFIG_HAS_EARLYSUSPEND)
#ifdef ATBM_PM_USE_EARLYSUSPEND
	struct early_suspend atbm_early_suspend;
	struct semaphore early_suspend_lock;
	atomic_t early_suspend_state;
#endif
#endif /* CONFIG_HAS_EARLYSUSPEND && DHD_USE_EARLYSUSPEND */
	struct mutex	sys_mutex;
	int syshold;
};
static const struct usb_device_id atbm_usb_ids[] = {
	/* Asus */
	{USB_DEVICE(WIFI_USB_VID, WIFI_USB_PID)},
    {USB_DEVICE(0x1b20, 0x6052)},
	{USB_DEVICE(WIFI_USB_VID, 0x6052)},
	{ /* end: all zeroes */}
};
struct atbm_usb_driver_ref {
	struct kref ref;
	struct usb_driver *atbm_driver;
};

struct atbm_usb_driver_ref atbm_usb_kref;

MODULE_DEVICE_TABLE(usb, atbm_usb_ids);
int atbm_usb_free_tx_wsm(struct sbus_priv *self,struct sbus_urb *tx_urb);
static int  atbm_usb_init(void);
static void  atbm_usb_exit(void);
struct mutex atbm_usb_mod_lock;	
struct dvobj_priv *g_dvobj= NULL;
#define atbm_usb_module_lock_int()		mutex_init(&atbm_usb_mod_lock)
#define atbm_usb_module_lock()			mutex_lock(&atbm_usb_mod_lock)
#define atbm_usb_module_unlock()		mutex_unlock(&atbm_usb_mod_lock)
#define atbm_usb_module_trylock() 			mutex_trylock(&atbm_usb_mod_lock)
#define atbm_usb_module_lock_release()	mutex_destroy(&atbm_usb_mod_lock)
#define atbm_usb_module_lock_check()	lockdep_assert_held(&atbm_usb_mod_lock)
#define atbm_usb_dvobj_assign_pointer(p)	ATBM_VOLATILE_SET(&g_dvobj,p)
#define atbm_usb_dvobj_dereference()		ATBM_VOLATILE_GET(&g_dvobj)
static void  atbm_usb_fw_sync(struct atbm_common *hw_priv,struct dvobj_priv *dvobj);
extern struct atbm_common *g_hw_priv;

extern void atbm_tx_tasklet(unsigned long priv);
extern void atbm_rx_tasklet(unsigned long priv);

#ifndef USB_USE_TASTLET_TXRX

void atbm_tx_complete_work(struct atbm_work_struct *work)
{
	struct sbus_priv *self =
			container_of(work, struct sbus_priv , tx_complete_work);
	atbm_tx_tasklet((unsigned long)self->core);
}

void atbm_rx_complete_work(struct atbm_work_struct *work)
{
	struct sbus_priv *self =
			container_of(work, struct sbus_priv , rx_complete_work);
	atbm_rx_tasklet((unsigned long)self->core);
}
#endif
static int atbm_urb_halt(struct sbus_priv *self)
{
	struct atbm_common	*hw_priv	= self->core;
	
	return (atomic_read(&hw_priv->atbm_pluged) == 0 )|| 
		   (self->drvobj->pusbdev->state < USB_STATE_UNAUTHENTICATED) || 
		   atbm_wifi_get_status() || self->suspend;
}
static void atbm_urb_work_process_ximt_err_urb(struct sbus_priv *self)
{
	struct sbus_urb *tx_urb;
	struct urb *err_urb;
	struct atbm_common	*hw_priv	= self->core;
//	int index = 0;
	
	atbm_printk_err("atbm_urb_work_process_ximt_err_urb\n");	
	if(!usb_anchor_empty(&self->drvobj->xmit_err)){
		atbm_printk_err("urb xmit_err\n");
		/*
		*cancle all tx urb
		*/
		usb_kill_anchored_urbs(&self->drvobj->tx_submitted);
	}
#ifdef  CONFIG_SUPPORT_HIF_TRACE

	while((err_urb = usb_get_from_anchor(&self->drvobj->comp_err)) || 
		  (err_urb = usb_get_from_anchor(&self->drvobj->xmit_err))){
		
		tx_urb = (struct sbus_urb*)err_urb->context;
		usb_free_urb(err_urb);
		
		if(!atbm_urb_halt(self)){
//			atbm_bh_halt(hw_priv);
			int status;
again:		
			if(tx_urb->test_urb->actual_length != tx_urb->test_urb->transfer_buffer_length){

				atbm_printk_err("tx urb retry(%d)(%d)(%d)(%d)\n",tx_urb->test_urb->actual_length,
								tx_urb->test_urb->transfer_buffer_length,tx_urb->retries,tx_urb->offset);
				tx_urb->retries ++;
				tx_urb->offset += tx_urb->test_urb->actual_length;
				tx_urb->test_urb->actual_length = 0;
#ifdef CONFIG_USE_DMA_ADDR_BUFFER
				ATBM_USB_FILL_HTTX_BULK_URB(tx_urb->test_urb,
				self->drvobj->pusbdev, self->drvobj->ep_out,tx_urb->pallocated_buf+tx_urb->offset,tx_urb->test_urb->transfer_buffer_length - tx_urb->offset,
				atbm_usb_xmit_data_complete,tx_urb,tx_urb->dma_transfer_addr + tx_urb->offset);
#else //CONFIG_USE_DMA_ADDR_BUFFER
				usb_fill_bulk_urb(tx_urb->test_urb,
				self->drvobj->pusbdev, usb_sndbulkpipe(self->drvobj->pusbdev, self->drvobj->ep_out),
				tx_urb->pallocated_buf+tx_urb->offset,tx_urb->test_urb->transfer_buffer_length - tx_urb->offset,
				atbm_usb_xmit_data_complete,tx_urb);
#endif //CONFIG_USE_DMA_ADDR_BUFFER
				usb_anchor_urb(tx_urb->test_urb, &self->drvobj->tx_submitted);

				status = usb_submit_urb(tx_urb->test_urb, GFP_ATOMIC);

				if(status){
					usb_unanchor_urb(tx_urb->test_urb);
					atbm_printk_err("usb_submit_urb tx err %d %d\n",status,tx_urb->retries);

					if(tx_urb->retries <= 1000){
						goto  again; 
					}

					atbm_bh_halt(hw_priv);
				}else {
					continue;
				}
			}else {
				tx_urb->test_urb->status = 0;
				atbm_usb_xmit_data_complete(tx_urb->test_urb);
				continue;
				
			}
		}
		
		wsm_force_free_tx(hw_priv,tx_urb->data);
		tx_urb->test_urb->actual_length = tx_urb->test_urb->transfer_buffer_length;
		tx_urb->test_urb->status = 0;
		atbm_usb_xmit_data_complete(tx_urb->test_urb);
	}
#else
	while((err_urb = usb_get_from_anchor(&self->drvobj->comp_err)) || 
	  	  (err_urb = usb_get_from_anchor(&self->drvobj->xmit_err))){
		
		tx_urb = (struct sbus_urb*)err_urb->context;
		usb_free_urb(err_urb);

		wsm_force_free_tx(hw_priv,tx_urb->data);
		tx_urb->test_urb->actual_length = tx_urb->test_urb->transfer_buffer_length;
		tx_urb->test_urb->status = 0;
		atbm_usb_xmit_data_complete(tx_urb->test_urb);
		
		if(!atbm_urb_halt(self)){
			atbm_bh_halt(hw_priv);
		}
	}	
#endif
//	for(index = 0; index < BITS_TO_LONGS(TX_URB_NUM) ; index++){
//		atbm_printk_err("tx_urb_map[%d]=[%lx]\n",index,self->drvobj->tx_urb_map[index]);
//	}
}
static void atbm_urb_work_process_recv_err_urb(struct sbus_priv *self)
{
	struct urb *err_urb;
	struct atbm_common	*hw_priv	= self->core;
//	int index = 0;
	
	if(!usb_anchor_empty(&self->drvobj->recv_err)){
		/*
		*cancle all rx urb
		*/
		usb_kill_anchored_urbs(&self->drvobj->rx_submitted);
	}
#ifdef  CONFIG_SUPPORT_HIF_TRACE
	while((err_urb = usb_get_from_anchor(&self->drvobj->recv_err))){

		struct sbus_urb *rx_urb = (struct sbus_urb*)err_urb->context;
		usb_free_urb(err_urb);

		if(rx_urb->test_urb->status == -EOVERFLOW){
			atbm_printk_err("urb rx overflow[%d][%d]\n",rx_urb->test_urb->actual_length,rx_urb->test_urb->transfer_buffer_length);
			rx_urb->retries = 0;
			rx_urb->offset  = 0;
			rx_urb->test_urb->actual_length = 0;
		}

		if(atbm_hif_trace_running(hw_priv)){
			rx_urb->retries = 0;
			rx_urb->offset  = 0;
			rx_urb->test_urb->actual_length = 0;
			atbm_usb_urb_put(self,self->drvobj->rx_urb_map,rx_urb->urb_id,0);
			continue;
		}
		
		if(!atbm_urb_halt(self)){
			int status;
again:
			rx_urb->retries ++;
			rx_urb->offset += rx_urb->test_urb->actual_length;
			rx_urb->test_urb->actual_length = 0;
			usb_fill_bulk_urb(rx_urb->test_urb, 
							  self->drvobj->pusbdev, 
							  usb_rcvbulkpipe(self->drvobj->pusbdev, self->drvobj->ep_in),
							  rx_urb->test_skb->data + rx_urb->offset,RX_BUFFER_SIZE - rx_urb->offset,atbm_usb_receive_data_complete,rx_urb);
			usb_anchor_urb(rx_urb->test_urb, &self->drvobj->rx_submitted);
			status = usb_submit_urb(rx_urb->test_urb, GFP_ATOMIC);
			if (status) {
				usb_unanchor_urb(rx_urb->test_urb);
				atbm_printk_err("usb_submit_urb rx err %d %d\n",status,rx_urb->retries);

				if(rx_urb->retries <= 1000){
					goto  again; 
				}
				atbm_usb_urb_put(self,self->drvobj->rx_urb_map,rx_urb->urb_id,0);
				atbm_bh_halt(hw_priv);
			}
		}else {
			atbm_usb_urb_put(self,self->drvobj->rx_urb_map,rx_urb->urb_id,0);
		}
	}
#else
	while((err_urb = usb_get_from_anchor(&self->drvobj->recv_err))){
		
		struct sbus_urb *rx_urb = (struct sbus_urb*)err_urb->context;
		usb_free_urb(err_urb);

		if(!atbm_urb_halt(self)){
			atbm_bh_halt(hw_priv);
		}else {
			atbm_usb_urb_put(self,self->drvobj->rx_urb_map,rx_urb->urb_id,0);
		}
	}
#endif

//	for(index = 0; index < BITS_TO_LONGS(RX_URB_NUM) ; index++){
//		atbm_printk_err("rx_urb_map[%d]=[%lx]\n",index,self->drvobj->rx_urb_map[index]);
//	}
}
static void atbm_urb_work(struct atbm_work_struct *work)
{
	struct sbus_priv *self = container_of(work, struct sbus_priv , urb_wk);
	
	atbm_urb_work_process_recv_err_urb(self);
	atbm_urb_work_process_ximt_err_urb(self);
}
static int  atbm_usb_alloc_urb_work(struct sbus_priv *self)
{
	int  ret = 0;
	struct atbm_workqueue_struct 	*urb_wkq;
	
	ATBM_INIT_WORK(&self->urb_wk, atbm_urb_work);
	
	
	urb_wkq = atbm_alloc_ordered_workqueue(ieee80211_alloc_name(self->core->hw,"usb-work"),0);
	
	if(urb_wkq == NULL){
		ret = -ENOMEM;
	}
	atbm_printk_init("alloc urb work\n");
	mutex_lock(&self->urb_wkq_mutex);
	rcu_assign_pointer(self->urb_wkq,(void *)urb_wkq);
	mutex_unlock(&self->urb_wkq_mutex);
	return ret;
}
static void  atbm_usb_destroy_urb_work(struct sbus_priv *self)
{
	struct atbm_workqueue_struct 	*urb_wkq;
	
	mutex_lock(&self->urb_wkq_mutex);
	urb_wkq = self->urb_wkq;
	rcu_assign_pointer(self->urb_wkq,NULL);
	mutex_unlock(&self->urb_wkq_mutex);

	synchronize_rcu();
	
	if(urb_wkq){
		atbm_flush_workqueue(urb_wkq);
		atbm_destroy_workqueue(urb_wkq);
	}
	atbm_urb_work(&self->urb_wk);
	atbm_printk_init("destroy urb work\n");
	
	usb_kill_anchored_urbs(&self->drvobj->rx_submitted);
	usb_kill_anchored_urbs(&self->drvobj->tx_submitted);
	
	if(self->drvobj->pusbdev->state < USB_STATE_UNAUTHENTICATED){
		atbm_printk_err("usb disconnecting,can not try anything\n");
	}else do{
		char *flush_buff;
		int  flush_len = 0;
		int  status = 0;
		
		atbm_printk_exit("usb:try to flush rx....\n");

		flush_buff = atbm_kzalloc(RX_BUFFER_SIZE,GFP_KERNEL);

		if(flush_buff == NULL){
			break;
		}

		do{
			flush_len = 0;
			
			status = usb_bulk_msg(self->drvobj->pusbdev,
						 usb_rcvbulkpipe(self->drvobj->pusbdev, self->drvobj->ep_in),
						 flush_buff,RX_BUFFER_SIZE,&flush_len,100);
			atbm_printk_err("flush rx[%d][%d]\n",status,flush_len);
			
		}while(flush_len);

		atbm_kfree(flush_buff);
	}while(0);

}
static void  atbm_usb_flush_urb_work(struct sbus_priv *self)
{
	mutex_lock(&self->urb_wkq_mutex);
	if(self->urb_wkq){
		atbm_flush_workqueue(self->urb_wkq);
	}
	mutex_unlock(&self->urb_wkq_mutex);
}
static void atbm_usb_schedule_urb_work(struct sbus_priv *self,
				struct sbus_urb *submit_urb,struct usb_anchor *anchor)
{
	struct atbm_workqueue_struct 	*urb_wkq;
	
//	atbm_printk_err("atbm_usb_schedule_urb_work\n");	
	rcu_read_lock();
	urb_wkq = (struct atbm_workqueue_struct *)rcu_dereference(self->urb_wkq);
	if(urb_wkq){
		usb_anchor_urb(submit_urb->test_urb, anchor);
		atbm_queue_work(urb_wkq,&self->urb_wk);
	}
	rcu_read_unlock();
}

static int atbm_usb_xmit_init(struct sbus_priv *self)
{
	
#ifdef USB_USE_TASTLET_TXRX
	atbm_printk_init("atbmwifi USB_USE_TASTLET_TXRX enable (%p)\n",self->core);
	tasklet_init(&self->tx_cmp_tasklet, atbm_tx_tasklet, (unsigned long)self->core);
#else
	struct atbm_common *hw_priv = self->core;
	atbm_printk_init("atbmwifi INIT_WORK enable\n");
	ATBM_INIT_WORK(&self->tx_complete_work, atbm_tx_complete_work);
	self->tx_workqueue= atbm_create_singlethread_workqueue(ieee80211_alloc_name(hw_priv->hw,"tx_workqueue")/*"tx_workqueue"*/);

	if(self->tx_workqueue == NULL)
		return -1;
#endif
	return 0;
}

static int atbm_usb_xmit_deinit(struct sbus_priv *self)
{
	
#ifdef USB_USE_TASTLET_TXRX
	tasklet_kill(&self->tx_cmp_tasklet);
#else
	if(self->tx_workqueue){
		atbm_flush_workqueue(self->tx_workqueue);
		atbm_destroy_workqueue(self->tx_workqueue);
	}
	self->tx_workqueue = NULL;	
#endif
	return 0;
}
static int atbm_usb_rev_init(struct sbus_priv *self)
{
#ifdef USB_USE_TASTLET_TXRX
	atbm_printk_init("atbmwifi USB_USE_TASTLET_TXRX enable (%p)\n",self->core);
	tasklet_init(&self->rx_cmp_tasklet, atbm_rx_tasklet, (unsigned long)self->core);
#else
	struct atbm_common *hw_priv = self->core;
	atbm_printk_init("atbmwifi INIT_WORK enable\n");
	ATBM_INIT_WORK(&self->rx_complete_work, atbm_rx_complete_work);
	self->rx_workqueue= atbm_create_singlethread_workqueue(ieee80211_alloc_name(hw_priv->hw,"rx_workqueue")/*"rx_workqueue"*/);
	if(self->rx_workqueue == NULL)
		return -1;
#endif
    return 0;
}

static int atbm_usb_rev_deinit(struct sbus_priv *self)
{
	
#ifdef USB_USE_TASTLET_TXRX
	tasklet_kill(&self->rx_cmp_tasklet); 
#else
	if(self->rx_workqueue){
		atbm_flush_workqueue(self->rx_workqueue);
		atbm_destroy_workqueue(self->rx_workqueue);
	}
	self->rx_workqueue = NULL;	
#endif
	return 0;
}

static int atbm_usb_xmit_schedule(struct sbus_priv *self)
{
	struct atbm_common *hw_priv = self->core;

	if(atomic_read(&hw_priv->bh_term)|| hw_priv->bh_error || (hw_priv->bh_thread == NULL))
		return -1;
	
	if(atomic_xchg(&hw_priv->bh_suspend_usb, 0)){
		atbm_usb_pm_async(hw_priv->sbus_priv, false);
	}
#ifdef USB_USE_TASTLET_TXRX	
	tasklet_schedule(&self->tx_cmp_tasklet);
#else
	if(self->tx_workqueue==NULL)
	{
		atbm_printk_err("atbm_bh_schedule_tx term ERROR\n");
		return -1;
	}
	atbm_queue_work(self->tx_workqueue,&self->tx_complete_work);
#endif
	return 0;

}
static int atbm_usb_rev_schedule(struct sbus_priv *self)
{
	struct atbm_common *hw_priv = self->core;
	
	if(atomic_read(&hw_priv->bh_term)|| hw_priv->bh_error || (hw_priv->bh_thread == NULL))
		return -1;
	
#ifdef USB_USE_TASTLET_TXRX
	tasklet_schedule(&self->rx_cmp_tasklet);
#else
	if((self->rx_workqueue == NULL))
	{
		return -1;
	}
	atbm_queue_work(self->rx_workqueue,&self->rx_complete_work);
#endif
	return 0;
}
/*
*lock for probe dan disconnect
*/
void atbm_usb_module_muxlock(void)
{
	atbm_usb_module_lock();
}

void atbm_usb_module_muxunlock(void)
{
	atbm_usb_module_unlock();
}

/* sbus_ops implemetation */
int atbm_usbctrl_vendorreq_sync(struct sbus_priv *self, u8 request,u8 b_write,
					u16 value, u16 index, void *pdata,u16 len)
{
	unsigned int pipe;
	int status;
	u8 reqtype;
	int vendorreq_times = 0;
	struct usb_device *udev = self->drvobj->pusbdev;
	static int count;
	u8 * reqdata=self->usb_req_data;

	if(udev->state < USB_STATE_UNAUTHENTICATED){
		atbm_printk_err("usb not configed\n");
		return -1;
	}
	//printk("atbm_usbctrl_vendorreq_sync++ reqtype=%d\n",reqtype);
	if (!reqdata){
		atbm_printk_err("regdata is Null\n");
	}
	if(len > ATBM_USB_EP0_MAX_SIZE){
		atbm_dbg(ATBM_APOLLO_DBG_MSG,"usbctrl_vendorreq request 0x%x, b_write %d! len:%d >%d too long \n",
		       request, b_write, len,ATBM_USB_EP0_MAX_SIZE);
		return -1;
	}
	if(b_write){
		pipe = usb_sndctrlpipe(udev, 0); /* write_out */
		reqtype =  ATBM_USB_VENQT_WRITE;//host to device
		// reqdata must used dma data
		memcpy(reqdata,pdata,len);
	}
	else {
		pipe = usb_rcvctrlpipe(udev, 0); /* read_in */
		reqtype =  ATBM_USB_VENQT_READ;//device to host
	}
	do {
		status = usb_control_msg(udev, pipe, request, reqtype, value,
						 index, reqdata, len, 500); /*500 ms. timeout*/
		if (status < 0) {
			atbm_printk_err("vendorreq:err(%d)addr[%x] len[%d],b_write %d request %d\n",status,value|(index<<16),len,b_write, request);
		} else if(status != len) {
			atbm_printk_err("vendorreq:len err(%d)\n",status);
		}
		else{
			break;
		}
	} while (++vendorreq_times < 3);

	if((b_write==0) && (status>0)){
		memcpy(pdata,reqdata,len);
	}
	if (status < 0 && count++ < 4)
		atbm_dbg(ATBM_APOLLO_DBG_MSG,"reg 0x%x, usbctrl_vendorreq TimeOut! status:0x%x value=0x%x\n",
		       value, status, *(u32 *)pdata);
	return status;
}
EXPORT_SYMBOL(atbm_usbctrl_vendorreq_sync);

					
#ifdef HW_DOWN_FW
static int atbm_usb_hw_read_port(struct sbus_priv *self, u32 addr, void *pdata,int len)
{
	int ret = 0;
	u8 request = VENDOR_HW_READ; //HW
	u16 wvalue = (u16)(addr & 0x0000ffff);
	u16 index = addr >> 16;

	//printk("ERR,read addr %x,len %x\n",addr,len);

	//hardware just support len=4
	WARN_ON((len != 4) && (request== VENDOR_HW_READ));
	ret = atbm_usbctrl_vendorreq_sync(self,request,0,wvalue, index, pdata,len);
	if (ret < 0)
	{
		atbm_printk_err("ERR read addr %x,len %x\n",addr,len);
	}
	return ret;
}
#ifndef CONDIF_ATBM_CTRL_REQUEST_ASYNC
static int atbm_usb_hw_write_port(struct sbus_priv *self, u32 addr, const void *pdata,int len)
{

	u8 request = VENDOR_HW_WRITE; //HW
	u16 wvalue = (u16)(addr & 0x0000ffff);
	u16 index = addr >> 16;
	int ret =0;

	atbm_usb_pm(self,0);

	//printk(KERN_ERR "%s:addr(%x)\n",__func__,addr);
	//hardware just support len=4
	//WARN_ON((len != 4) && (request== VENDOR_HW_WRITE));
	ret =  atbm_usbctrl_vendorreq_sync(self,request,1,wvalue, index, (void *)pdata,len);
	if (ret < 0)
	{
		atbm_printk_err("ERR write addr %x,len %x\n",addr,len);
	}
	atbm_usb_pm(self,1);
	return ret;
}
#else
struct atbm_usb_ctrlrequest{
		struct sbus_priv *self;
		struct usb_ctrlrequest ctrl;
		u8 mem[0] __attribute__((__aligned__(64)));
};
static void atbm_usb_ctrlwrite_async_cb(struct urb *urb)
{
	struct atbm_usb_ctrlrequest *ctrl = (struct atbm_usb_ctrlrequest *)urb->context;
	struct sbus_priv *self = ctrl->self;
	
	BUG_ON(self == NULL);
	
	if(urb->status){
		atbm_printk_err("ctrl req urb err[%d]\n",urb->status);
		atomic_set(&self->drvobj->ctrl_err,1);
	}
	atbm_kfree(urb->context);
}

static int atbm_usb_hw_write_port(struct sbus_priv *self, u32 addr, const void *pdata,int len)
{
	struct atbm_usb_ctrlrequest *ctrl = NULL;
	
	struct urb *urb = NULL;	
	u16 wvalue = (u16)(addr & 0x0000ffff);
	u16 index = addr >> 16;
	int ret =0;
	u8* buff = NULL;
	int resubmitted = 0;
	
	atbm_usb_pm(self,0);

	if(atomic_read(&self->drvobj->ctrl_err)){
		ret = -ENODEV;		
		goto exit;
	}
	
	if((!!pdata)^(!!len)){
		ret = -EINVAL;
		goto exit;
	}
	
	if(len > ATBM_USB_EP0_MAX_SIZE){
		ret = -EINVAL;
		goto exit;
	}
	
	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		ret = -ENOMEM;
		goto exit;
	}

	ctrl = atbm_kzalloc(sizeof(*ctrl)+len,GFP_KERNEL);
	
	if(ctrl == NULL){		
		ret = -ENOMEM;
		goto exit;
	}

	buff = ctrl->mem;
	ctrl->self = self;
	
	if(pdata)
		memcpy(buff,pdata,len);

	ctrl->ctrl.bRequestType = ATBM_USB_VENQT_WRITE;
	ctrl->ctrl.bRequest = VENDOR_HW_WRITE;
	ctrl->ctrl.wValue = cpu_to_le16(wvalue);
	ctrl->ctrl.wIndex = cpu_to_le16(index);
	ctrl->ctrl.wLength = cpu_to_le16(len);

	usb_fill_control_urb(urb, self->drvobj->pusbdev, usb_sndctrlpipe(self->drvobj->pusbdev, 0),
			     (unsigned char *)(&ctrl->ctrl), buff, len,
			     atbm_usb_ctrlwrite_async_cb, ctrl);
xmit:
	usb_anchor_urb(urb, &self->drvobj->ctrl_submitted);
	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (ret < 0) {		
		atbm_printk_err("%s: submit_urb err (%d)\n",__func__,ret);		
		usb_unanchor_urb(urb);
		if(resubmitted == 0){
			usb_wait_anchor_empty_timeout(&self->drvobj->ctrl_submitted,40);
			resubmitted = 1;
			goto xmit;
		}
		atbm_kfree(ctrl);
	}
exit:
	atbm_usb_pm(self,1);
	if(urb)
		usb_free_urb(urb);
	return ret;
}
#endif
int atbm_usb_sw_read_port(struct sbus_priv *self, u32 addr, void *pdata,int len)
{
	u8 request = VENDOR_SW_READ; //SW
	u16 wvalue = (u16)(addr & 0x0000ffff);
	u16 index = addr >> 16;

	WARN_ON(len > ATBM_USB_EP0_MAX_SIZE);
	return atbm_usbctrl_vendorreq_sync(self,request,0,wvalue, index, pdata,len);
}

#else
static int atbm_usb_sw_read_port(struct sbus_priv *self, u32 addr, void *pdata,int len)
{
	u8 request = VENDOR_SW_READ; //SW
	u16 wvalue = (u16)(addr & 0x0000ffff);
	u16 index = addr >> 16;

	WARN_ON(len > ATBM_USB_EP0_MAX_SIZE);
	return atbm_usbctrl_vendorreq_sync(self,request,0,wvalue, index, pdata,len);
}
//#ifndef HW_DOWN_FW
static int atbm_usb_sw_write_port(struct sbus_priv *self, u32 addr,const void *pdata,int len)
{
	u8 request = VENDOR_SW_WRITE; //SW
	u16 wvalue = (u16)(addr & 0x0000ffff);
	u16 index = addr >> 16;

	WARN_ON(len > ATBM_USB_EP0_MAX_SIZE);
	return atbm_usbctrl_vendorreq_sync(self,request,1,wvalue, index, (void *)pdata,len);
}
#endif
int atbm_lmac_start(struct sbus_priv *self)
{
	u8 request = VENDOR_SW_CPU_JUMP;
	static int tmpdata =0;
	return atbm_usbctrl_vendorreq_sync(self,request,1,0, 0, &tmpdata,0);
}
int atbm_usb_ep0_cmd(struct sbus_priv *self)
{
	u8 request = VENDOR_EP0_CMD; //SW
	
	static int tmpdata =0;
	return atbm_usbctrl_vendorreq_sync(self,request,1,0, 0, &tmpdata,0);
}
#if 0
static int atbm_usb_device_reset(struct sbus_priv *self)
{
	int ret = 0;
	/*
	*reset usb
	*/
	ret = usb_lock_device_for_reset(self->drvobj->pusbdev, self->drvobj->pusbintf);
	if (ret == 0) {
		ret = usb_reset_device(self->drvobj->pusbdev);
		usb_unlock_device(self->drvobj->pusbdev);
	}
	
	if(ret != 0){
		goto error;
	}
	
	if(self->drvobj->pusbdev->state != USB_STATE_CONFIGURED){
		goto error;
	}
	
	return 0;
error:
	return -1;
}
#endif
static void atbm_usb_block_urbs(struct sbus_priv *self)
{
	int urb_index = 0;
	unsigned long flags = 0;
	struct sbus_urb *urb_unlink;
	
	spin_lock_irqsave(&self->lock, flags);
	urb_unlink = self->drvobj->rx_urb;
	for(urb_index=0;urb_index<RX_URB_NUM;urb_index++){
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
		usb_block_urb(urb_unlink[urb_index].test_urb);
		#else
		atomic_inc(&urb_unlink[urb_index].test_urb->reject);
		#endif
	}
	urb_unlink = self->drvobj->tx_urb;
	for(urb_index=0;urb_index<TX_URB_NUM;urb_index++){
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
		usb_block_urb(urb_unlink[urb_index].test_urb);
		#else
		atomic_inc(&urb_unlink[urb_index].test_urb->reject);
		#endif
	}
	spin_unlock_irqrestore(&self->lock, flags);
}

static void atbm_usb_unblock_urbs(struct sbus_priv *self)
{
	int urb_index = 0;
	unsigned long flags = 0;
	struct sbus_urb *urb_unlink;
	
	spin_lock_irqsave(&self->lock, flags);
	urb_unlink = self->drvobj->rx_urb;
	for(urb_index=0;urb_index<RX_URB_NUM;urb_index++){
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
		usb_unblock_urb(urb_unlink[urb_index].test_urb);
		#else
		usb_unpoison_urb(urb_unlink[urb_index].test_urb);
		#endif
	}
	urb_unlink = self->drvobj->tx_urb;
	for(urb_index=0;urb_index<TX_URB_NUM;urb_index++){
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
		usb_unblock_urb(urb_unlink[urb_index].test_urb);
		#else
		usb_unpoison_urb(urb_unlink[urb_index].test_urb);
		#endif
	}
	spin_unlock_irqrestore(&self->lock, flags);
}

static int atbm_usb_wait_anchor_empty_timeout(struct sbus_priv *self,int timeout)
{
	int urb_index = 0;
	struct atbm_common *hw_priv = self->core;
	struct sbus_urb *urb_unlink;
	int ret = 0;

	atbm_printk_bus( "Unlink Rx Urb\n");
	urb_unlink = self->drvobj->rx_urb;
	for(urb_index=0;urb_index<RX_URB_NUM;urb_index++){
		ret = usb_unlink_urb(urb_unlink[urb_index].test_urb);
		//atbm_printk_bus( "usb_unlink_urb[%d][%d]\n",urb_index,ret);
	}
	ret = usb_wait_anchor_empty_timeout(&hw_priv->sbus_priv->drvobj->rx_submitted,timeout);
	if(ret == 0){
		atbm_printk_err("rx_submitted cancle timeout (%d)\n",usb_anchor_empty(&hw_priv->sbus_priv->drvobj->rx_submitted));
		goto exit;
	}
	atbm_printk_bus("Unlink Rx Urb (%d)\n",ret);
	atbm_printk_bus( "Unlink Tx Urb\n");
	urb_unlink = self->drvobj->tx_urb;
	for(urb_index=0;urb_index<TX_URB_NUM;urb_index++){
		ret = usb_unlink_urb(urb_unlink[urb_index].test_urb);
	//	atbm_printk_err("usb_unlink_urb[%d][%d]\n",urb_index,ret);
	}
	ret = usb_wait_anchor_empty_timeout(&hw_priv->sbus_priv->drvobj->tx_submitted,timeout);
	if(ret == 0){
		atbm_printk_err("tx_submitted cancle timeout(%d) \n",usb_anchor_empty(&hw_priv->sbus_priv->drvobj->tx_submitted));
	}
	atbm_printk_bus("Unlink Tx Urb(%d)\n",ret);
exit:
	return ret;
}
void atbm_lmac_ebm_debug(struct atbm_common *hw_priv,const char *func,int line)
{
	#define LEN0_OFFSET (0x1C)
	#define LEN1_OFFSET	(0x20)
	#define STS_OFFSET	(0x84)
	
	static const int ebm_base[10] = {0xa560000,0xa560200,0xa560400,0xa560600,0xa560800,0xa560a00,0xa560c00,0xa560e00,0xa561000,0xa561200};
	static const int ebm_offset[3] = {LEN0_OFFSET,LEN1_OFFSET,STS_OFFSET};
	int ii = 0;
	int jj = 0;
	u32 val32 = 0;
	int ret = 0;
	
	for(ii = 0;ii<ARRAY_SIZE(ebm_base);ii++){
		for(jj = 0;jj < ARRAY_SIZE(ebm_offset);jj++){
			ret = atbm_direct_read_reg_32(hw_priv,ebm_base[ii]+ebm_offset[jj],&val32);
			if(ret < 0){
				atbm_printk_err("[%s][%d]:read[%x] err\n",func,line,ebm_base[ii]+ebm_offset[jj]);
			}else if(val32){
				atbm_printk_err("[%s][%d]:read[%x]=[%x]\n",func,line,ebm_base[ii]+ebm_offset[jj],val32);
				BUG_ON(1);
			}
		}
	}
}
static int _atbm_lmac_restart(struct sbus_priv *self)
{
	int ret = 0;
	struct atbm_common *hw_priv = self->core;
	int i;
	/*
	*kill all tx and rx urb and release tx pkg and cmd
	*/
	atbm_printk_init("%s\n",__func__);
	
	if(self->drvobj->pusbdev->state < USB_STATE_UNAUTHENTICATED){
		atbm_printk_err("usb has been disconnect\n");
		return -1;
	}
#ifdef USB_USE_TASTLET_TXRX
	tasklet_disable(&self->tx_cmp_tasklet);
	tasklet_disable(&self->rx_cmp_tasklet);
#endif

	wsm_lock_tx_async(hw_priv);
	atbm_wifi_set_status(2);
	/*
	*from now ,rx_urb and tx_urb can not be submitted again until
	*urb unblock.
	*/
	atbm_usb_block_urbs(self);
	synchronize_rcu();
	/*
	*cpu hold
	*/
	atbm_usb_ep0_hw_reset_cmd(self,HW_HOLD_CPU,0);
	atbm_usb_ep0_hw_reset_cmd(self,HW_RESET_HIF,0);
	msleep(10);
//	atbm_lmac_ebm_debug(hw_priv,__func__,__LINE__);
	/*
	*reset mac
	*/
	{
		u32 val32;
		/*reset mac*/
		ret = atbm_direct_read_reg_32(hw_priv,0x16100074,&val32);
		if(ret<0)
			atbm_printk_err("read 0xab0016c err\n");
		val32 |= BIT(1);
		ret = atbm_direct_write_reg_32(hw_priv,0x16100074,val32);
		if(ret<0)
			atbm_printk_err("write 0xab0016c err\n");
		
		ret = atbm_direct_read_reg_32(hw_priv,0x16100074,&val32);
		atbm_printk_init("reset val(%x)\n",val32);
		val32 &= ~BIT(1);
		ret = atbm_direct_write_reg_32(hw_priv,0x16100074,val32);
		if(ret<0)
			atbm_printk_err("write 0xab0016c err\n");

		ret = atbm_direct_read_reg_32(hw_priv,0x16100074,&val32);
		
		atbm_printk_init("after reset(%x)\n",val32);
	}
	/*
	*
	*/
	//atbm_lmac_ebm_debug(hw_priv,__func__,__LINE__);
	/*
	ret = atbm_usb_device_reset(self);
	if(ret){
		atbm_printk_err("Reset Usb Err\n");
		goto error_prepare;
	}
	atbm_printk_init("Release All Tx Urb\n");
	*/
	ret = atbm_usb_wait_anchor_empty_timeout(self,1000);
	/*
	if(ret == 0){
		ret = atbm_usb_device_reset(self);
		if(ret){
			atbm_printk_err("Reset Usb Err\n");
			goto error_prepare;
		}
		ret = atbm_usb_wait_anchor_empty_timeout(self,1000);

		if(ret == 0){
			atbm_printk_err("Cancle Usb Err\n");
			goto error_prepare;
		}
	}
	*/
	atbm_rx_bh_flush(hw_priv);	
	atbm_usb_flush_urb_work(self);
	synchronize_rcu();
	/*
	ret = atbm_usb_device_reset(self);
	if(ret){
		atbm_printk_err("Reset Usb Err\n");
		goto error_prepare;
	}
	*/

	atbm_printk_init("Flush txrx urb\n");
	/*
	*waitting all wsm cmd destory
	*/
	atbm_destroy_wsm_cmd(hw_priv);
	
	hw_priv->bh_error = 0;
	smp_mb();
	ieee80211_pre_restart_hw_sync(hw_priv->hw);
	atbm_printk_init("Flush iee80211 hw\n");
	atbm_tx_queues_lock(hw_priv);
	/*
	*hold rtnl_lock,make sure that when down load fw,network layer cant not 
	*send pkg and cmd
	*/
	rtnl_lock();
	/*
	*release hw buff
	*/
	hw_priv->wsm_tx_seq = 0;
	hw_priv->buf_id_tx = 0;
	hw_priv->wsm_rx_seq = 0;
	hw_priv->hw_bufs_used = 0;
	hw_priv->buf_id_rx = 0;
	for (i = 0; i < ATBM_WIFI_MAX_VIFS; i++)
		hw_priv->hw_bufs_used_vif[i] = 0;
	
	atomic_set(&hw_priv->atbm_pluged,1);
	atbm_usb_unblock_urbs(self);
	atbm_wifi_set_status(0);
	atbm_hif_trace_restart(hw_priv);
	/*
	*disable urb monitor irq ,and stop tx/rx thread
	*/
	hw_priv->sbus_ops->irq_unsubscribe(hw_priv->sbus_priv);
	
#ifdef USB_USE_TASTLET_TXRX
	tasklet_enable(&self->rx_cmp_tasklet);
	tasklet_enable(&self->tx_cmp_tasklet);
#endif
	/*
	*load firmware
	*/
	ret = atbm_reinit_firmware(hw_priv);
	
	if(ret){
		/*
		*disable urb monitor irq ,and stop tx/rx thread
		*/
		hw_priv->sbus_ops->irq_unsubscribe(hw_priv->sbus_priv);
		atbm_printk_init("%s:reload fw err\n",__func__);
		goto error_reload;
	}
	atbm_Default_driver_configuration(hw_priv);
	/*
	*restart ap and sta
	*/
	ret = ieee80211_restart_hw_sync(hw_priv->hw);
	
	rtnl_unlock();
	atbm_tx_queues_unlock(hw_priv);
	wsm_unlock_tx(hw_priv);
	return ret;
error_reload:
	rtnl_unlock();
	atbm_tx_queues_unlock(hw_priv);
//error_prepare:
	wsm_unlock_tx_async(hw_priv);
	return -1;
}
int atbm_lmac_restart(struct sbus_priv *self)
{
	int ret  = -1;
	/*
	*hold atbm_usb_module_lock,means that during reload firmware
	*disconnect and probe will be locked
	*/
	if(atbm_usb_module_trylock()){ 
		if(atbm_hw_priv_dereference()){
			ret = _atbm_lmac_restart(self);
		}
		atbm_usb_module_unlock();
	}else {
		/*disconnecting*/
		atbm_printk_init("%s:disconnecting\n",__func__);
		
	}
	return ret;
}

#define HW_RESET_REG_CPU   				BIT(16)
#define HW_RESET_REG_HIF   				BIT(17)
#define HW_RESET_REG_SYS   				BIT(18)
#define HW_RESRT_REG_CHIP  				BIT(19)
#define HW_RESET_REG_NEED_IRQ_TO_LMAC	BIT(20)
int atbm_usb_ep0_hw_reset_cmd(struct sbus_priv *self,enum HW_RESET_TYPE type,bool irq_lmac)
{
	u8 request = VENDOR_HW_RESET; //SW
	u16 wvalue ;
	u16 index ;
	u32 val = 0;
	int ret = 0;

	mutex_lock(&self->sys_mutex);
	
	atbm_direct_read_reg_32(self->core,0x161010cc,&val);
	val |= BIT(15);
	atbm_printk_init("0x161010cc %d\n",val);
	atbm_direct_write_reg_32(self->core,0x161010cc,val);
	
	if(type == HW_RESET_HIF){
		self->syshold |= HW_RESET_REG_HIF;
	}
	else if(type == HW_RESET_HIF_SYSTEM){
		self->syshold |= HW_RESET_REG_HIF|HW_RESET_REG_SYS;
	}
	else if(type == HW_RESET_HIF_SYSTEM_USB){
		self->syshold |= HW_RESRT_REG_CHIP;
	}
	else if(type == HW_HOLD_CPU){
		self->syshold |= HW_RESET_REG_CPU;
	}
	else if(type == HW_RUN_CPU){
		self->syshold = 0;
	}else if (type == HW_RESET_HIF_SYSTEM_CPU)
	{
		self->syshold |= HW_RESET_REG_CPU|HW_RESET_REG_HIF|HW_RESET_REG_SYS;
	}
	if(irq_lmac){
		self->syshold |= HW_RESET_REG_NEED_IRQ_TO_LMAC;
	}
	//tmpdata |= 0x40;
	//tmpdata |= VENDOR_HW_RESET<<8;
	wvalue = (self->syshold>>16)&0xff;
	wvalue |= ((request + 0x40 + ((self->syshold>>16)&0xff))<<8)&0xff00;
	index = wvalue;
	atbm_printk_bus("ep0_hw_reset request %d wvalue %x\n",request,wvalue);
	ret = atbm_usbctrl_vendorreq_sync(self,request,1,wvalue, index, &self->syshold,0);

	self->syshold &= ~(HW_RESET_REG_HIF | HW_RESET_REG_SYS | HW_RESET_REG_NEED_IRQ_TO_LMAC | HW_RESRT_REG_CHIP);
	
	mutex_unlock(&self->sys_mutex);

	return ret;
}
/*
wvalue=1 : open uart debug;
 wvalue=0 : close uart debug;
 */
int atbm_usb_debug_config(struct sbus_priv *self,u16 wvalue)
{
#if (PROJ_TYPE<ARES_B)
	u8 request = VENDOR_DBG_SWITCH;
	u16 index = 0;

	usb_printk( "atbm_usb_debug_config\n");

	return atbm_usbctrl_vendorreq_sync(self,request,1,wvalue, index, &wvalue,0);
#else //#if (PROJ_TYPE>=ARES_B)
	return 0;
#endif  //#if (PROJ_TYPE<ARES_B)
}

static void atbm_usb_xmit_data_complete(struct urb *urb)
{
	struct sbus_urb *tx_urb=(struct sbus_urb*)urb->context;
	struct sbus_priv *self = tx_urb->obj;

	switch(urb->status){
		case 0:
			break;
		case -ENOENT:
		case -ECONNRESET:
		case -ENODEV:
		case -ESHUTDOWN:
			atbm_printk_err("WARNING>%s %d status %d %d\n",__func__,__LINE__,urb->status,urb->actual_length);
			goto __free;
		default:
			atbm_printk_err( "WARNING> %s %d status %d %d\n",__func__,__LINE__,urb->status,urb->actual_length);
			goto __free;
	}

	if(!self->core->init_done){
		atbm_printk_err("[BH] irq. init_done =0 drop\n");
		goto __free;
	}
	if (/* WARN_ON */(self->core->bh_error)){
		atbm_printk_err( "[BH] irq. bh_error =0 drop\n");
		goto __free;
	}

	/*
	*
	*/
	if(urb->actual_length != urb->transfer_buffer_length){
		atbm_printk_err("%s:tx not complete[%d][%d][%d]\n",__func__,urb->status,urb->actual_length,urb->transfer_buffer_length);
		goto __free;
	}
#ifdef CONFIG_USB_AGGR_URB_TX
	atbm_usb_free_txDMABuf_all(self,tx_urb->pallocated_buf,tx_urb->dma_buff_alloced);
#endif //CONFIG_USB_AGGR_URB_TX

	atbm_usb_urb_put(self,self->drvobj->tx_urb_map,tx_urb->urb_id,1);

	atbm_usb_xmit_schedule(self);
	
	
	return ;

__free:
	atbm_printk_err("<Warning> usb drop 1 frame txend:_urb_put %d\n",tx_urb->urb_id);

	atbm_usb_schedule_urb_work(self,tx_urb,&self->drvobj->comp_err);
	return ;
}
#ifdef CONFIG_USB_AGGR_URB_TX
static int atbm_usb_xmit_data(struct sbus_priv *self,
				   unsigned int addr,
				   const void *pdata, int len,sbus_callback_handler hander)
{
	struct hif_tx_encap encap;
	int status=0;
	struct wsm_hdr_tx *wsm;
	struct atbm_common *hw_priv=self->core;
	char * txdmabuff = NULL;
	char * usb_aggr_buff = NULL;
	int ret = 0;
	int urb_id = -1;
	struct sbus_urb *tx_urb = NULL;
	
	usb_printk( "atbm_usb_xmit_data++\n");
	
	spin_lock_bh(&self->xmit_path_lock);
		
	urb_id = atbm_usb_urb_get(self,self->drvobj->tx_urb_map,TX_URB_NUM,1);
	if(urb_id<0){
		usb_printk( "atbm_usb_xmit_data:urb_id<0\n");
		status=-4;
		goto error;
	}
	usb_printk( "atbm_usb_xmit_data++ %d\n",__LINE__);

	/*if (atomic_read(&self->tx_lock)==0)*/
	if (hw_priv->device_can_sleep) {
		hw_priv->device_can_sleep = false;
	}
	
	tx_urb = &self->drvobj->tx_urb[urb_id];
	tx_urb->pallocated_buf_len = 0;
	tx_urb->frame_cnt=0;
	tx_urb->dma_buff_alloced = 0;
	tx_urb->data=NULL;
	usb_aggr_buff = NULL;
	tx_urb->pallocated_buf = atbm_usb_pick_txDMABuf(self);
	
	if(tx_urb->pallocated_buf==NULL){
		//usb_printk( "atbm_usb_pick_txDMABuf:err\n");
		atbm_usb_urb_put(self,self->drvobj->tx_urb_map,urb_id,1);
		status=-7;
		goto error;
	}	
	do {
		encap.data = tx_urb->pallocated_buf + tx_urb->pallocated_buf_len;
		ret = wsm_get_tx(hw_priv,&encap);
		if (ret <= 0) {
			status=-3;
			break;
		}

		if(usb_aggr_buff == NULL){
			usb_aggr_buff= atbm_usb_get_txDMABuf(self);
			BUG_ON(usb_aggr_buff == NULL);
			tx_urb->dma_buff_alloced ++;
		}
		txdmabuff = usb_aggr_buff; 
		wsm = (struct wsm_hdr_tx *)txdmabuff;
		wsm->enc_hdr.total_len = wsm->u.common.total_len;
		wsm->enc_hdr.flag = wsm->u.common.flag;
		wsm->u.usb_hdr.usb_len = wsm->u.common.total_len;
		if (le16_to_cpu(wsm->u.usb_hdr.usb_len) < 1538)
			wsm->u.usb_hdr.usb_len = __cpu_to_le16(1538);
		tx_urb->frame_cnt++;
		tx_urb->data = encap.source;

		//wsm->enc_hdr.SourceAddr = __cpu_to_le32(wsm->u.common.total_len | (wsm->u.common.flag << 16));
		//wsm->enc_hdr.SourceAddr = __cpu_to_le32(wsm->u.common.total_len | ((wsm->u.common.flag) << 16));
		wsm->u.usb_hdr.flag = __cpu_to_le16(0xe569)<<16;
		wsm->u.usb_hdr.flag |= __cpu_to_le32(BIT(6));
		wsm->u.usb_hdr.flag |= __cpu_to_le32((self->tx_hwChanId & 0x1f));
		wsm->u.usb_hdr.usb_id = wsm->id;
		wsm->id &= __cpu_to_le32(~WSM_TX_SEQ(WSM_TX_SEQ_MAX));
		wsm->id |= cpu_to_le32(WSM_TX_SEQ(hw_priv->wsm_tx_seq));
		
		self->tx_hwChanId++;
		usb_printk( "tx_seq %d\n",self->tx_hwChanId);
		hw_priv->wsm_tx_seq = (hw_priv->wsm_tx_seq + 1) & WSM_TX_SEQ_MAX;
	
		
		tx_urb->pallocated_buf_len += PER_PACKET_LEN;
		/*
		*if the data is cmd ,keep it last in the aggr buff
		*/
		if(tx_urb->data){
			break;
		}
		
		hw_priv->wsm_txframe_num++;
		//the last dma buffer
		usb_aggr_buff += PER_PACKET_LEN;
		if(tx_urb->frame_cnt>=(PER_BUFF_AGGR_NUM*tx_urb->dma_buff_alloced)){
			if(usb_aggr_buff >= self->drvobj->tx_dma_addr_buffer_end)
				break;
			usb_aggr_buff = atbm_usb_pick_txDMABuf(self);
			if(usb_aggr_buff == NULL)
				break;
			usb_aggr_buff = NULL;
		}
	}while(1);

	if(tx_urb->pallocated_buf_len == 0){
		atbm_usb_urb_put(self,self->drvobj->tx_urb_map,urb_id,1);
		status= -6;
		goto error;
	}

	if(tx_urb->frame_cnt ==0){
		WARN_ON(1);
	}
	
	usb_anchor_urb(tx_urb->test_urb, &self->drvobj->tx_submitted);

	tx_urb->dma_transfer_addr = self->drvobj->tx_dma_addr +((atbm_dma_addr_t)tx_urb->pallocated_buf - (atbm_dma_addr_t)self->drvobj->tx_dma_addr_buffer);
	ATBM_USB_FILL_HTTX_BULK_URB(tx_urb->test_urb,
		self->drvobj->pusbdev, self->drvobj->ep_out,tx_urb->pallocated_buf,tx_urb->pallocated_buf_len,
		atbm_usb_xmit_data_complete,tx_urb,tx_urb->dma_transfer_addr);
	
	tx_urb->offset  = 0;
	tx_urb->retries = 0;
	if(!atbm_wifi_get_status()){
		//frame_hexdump("usb_tx\n",tx_urb->pallocated_buf,64);
		atomic_add(1, &hw_priv->bh_tx);
		
		//usb_anchor_urb(self->drvobj->tx_urb,
		status = usb_submit_urb(tx_urb->test_urb, GFP_ATOMIC);
	}else {
		status = -1;
	}
	
	if (status && tx_urb->frame_cnt) {
		tx_urb->test_urb->status = status;
		tx_urb->test_urb->actual_length = 0;
		usb_unanchor_urb(tx_urb->test_urb);
		atbm_usb_schedule_urb_work(self,tx_urb,&self->drvobj->xmit_err);
		status = 1;		
		atbm_printk_err("release all data finished\n");	
		goto error;
	}
		
	if(status==0){
		spin_unlock_bh(&self->xmit_path_lock);
		return 1;
	}
error:
	spin_unlock_bh(&self->xmit_path_lock);
	return status;
}
#else
extern int atbm_wifi_bt_comb_get(void);
static int atbm_usb_xmit_data(struct sbus_priv *self,
				   unsigned int addr,
				   const void *pdata, int len,sbus_callback_handler hander)
{
	struct hif_tx_encap encap;
	int status=0;
	struct wsm_hdr_tx *wsm;
	struct atbm_common *hw_priv=self->core;
	int tx_len=0;
	int ret = 0;
	int urb_id =-1;
	struct sbus_urb *tx_urb = NULL;
	
	spin_lock_bh(&self->xmit_path_lock);
	
	urb_id = atbm_usb_urb_get(self,self->drvobj->tx_urb_map,TX_URB_NUM,1);
	if(urb_id<0){
		usb_printk( "atbm_usb_xmit_data:urb_id<0\n");
		status=-4;
		goto error;
	}
	tx_urb = &self->drvobj->tx_urb[urb_id];
	
	encap.data  = tx_urb->pallocated_buf;
	encap.tx_len =0;
	if (hw_priv->device_can_sleep) {
		hw_priv->device_can_sleep = false;
	}
	
	ret = wsm_get_tx(hw_priv,&encap);
	
	if (ret <= 0) {
		  atbm_usb_urb_put(self,self->drvobj->tx_urb_map,urb_id,1);
		 // printk("tx:atbm_usb_urb_put NULL %d\n",urb_id);
		  status =-3;
		  goto error;
	} else {
		wsm = (struct wsm_hdr_tx*)tx_urb->pallocated_buf;
		//printk("actual_len:%d totallen:%d datalen:%d\n", actual_len, wsm->u.common.total_len, wsm->u.common.flag & 0x7fff);
		wsm->enc_hdr.total_len = wsm->u.common.total_len;
		wsm->enc_hdr.flag = wsm->u.common.flag;
		//wsm->enc_hdr.SourceAddr = __cpu_to_le32(wsm->u.common.total_len | ((wsm->u.common.flag) << 16));
		wsm->u.usb_hdr.usb_len = wsm->u.common.total_len;
		if (le16_to_cpu(wsm->u.usb_hdr.usb_len) <1538)
			wsm->u.usb_hdr.usb_len =__cpu_to_le16(1538);
		
		tx_len = le16_to_cpu(wsm->u.usb_hdr.usb_len);

		atomic_add(1, &hw_priv->bh_tx);
		
		tx_urb->data = encap.source;
		
		wsm->u.usb_hdr.flag = __cpu_to_le16(0xe569)<<16;
		wsm->u.usb_hdr.flag |= __cpu_to_le32(BIT(6));
		wsm->u.usb_hdr.flag |= __cpu_to_le32((self->tx_hwChanId & 0x1f));
		wsm->id &= __cpu_to_le32(~WSM_TX_SEQ(WSM_TX_SEQ_MAX));
		wsm->id |= cpu_to_le32(WSM_TX_SEQ(hw_priv->wsm_tx_seq));
		
#ifdef 	CONFIG_ATBM_USB_INTERVAL		
		if(atbm_wifi_bt_comb_get() == 0){
			if(self->drvobj->n_submitteds % TX_URB_INTR_INTERVAL){
				tx_urb->test_urb->transfer_flags |= URB_NO_INTERRUPT;
			}else {
				tx_urb->test_urb->transfer_flags &= ~URB_NO_INTERRUPT;
			}
			
			self->drvobj->n_submitteds ++;
			
			if(tx_urb->data){
				//atbm_printk_tx("xmit need cfn(%p)(%p)(%d)\n",tx_urb->data,tx_urb->pallocated_buf,hw_priv->wsm_cmd.cmd);
				tx_urb->test_urb->transfer_flags &= ~URB_NO_INTERRUPT;
			}
		}
#endif		
		usb_anchor_urb(tx_urb->test_urb, &self->drvobj->tx_submitted);
		
#ifdef CONFIG_USE_DMA_ADDR_BUFFER
		ATBM_USB_FILL_HTTX_BULK_URB(tx_urb->test_urb,
			self->drvobj->pusbdev, self->drvobj->ep_out,tx_urb->pallocated_buf,tx_len,
			atbm_usb_xmit_data_complete,tx_urb,tx_urb->dma_transfer_addr);
#else //CONFIG_USE_DMA_ADDR_BUFFER
		usb_fill_bulk_urb(tx_urb->test_urb,
			self->drvobj->pusbdev, usb_sndbulkpipe(self->drvobj->pusbdev, self->drvobj->ep_out),
			tx_urb->pallocated_buf,tx_len,
			atbm_usb_xmit_data_complete,tx_urb);
#endif //CONFIG_USE_DMA_ADDR_BUFFER
		tx_urb->offset  = 0;
		tx_urb->retries = 0;
		if(!atbm_wifi_get_status()){
			status = usb_submit_urb(tx_urb->test_urb, GFP_ATOMIC);
		} else {
			status = -1;
		}
		if (status) {
			tx_urb->test_urb->status = status;
			tx_urb->test_urb->actual_length = 0;
			usb_unanchor_urb(tx_urb->test_urb);
			atbm_usb_schedule_urb_work(self,tx_urb,&self->drvobj->xmit_err);
			status = 1;
			atbm_printk_once("tx:atbm_usb_urb_put %d\n",urb_id);			
			goto error;
		}
		
		self->tx_hwChanId++;
		usb_printk( "tx_seq %d\n",self->tx_hwChanId);
		hw_priv->wsm_tx_seq = (hw_priv->wsm_tx_seq + 1) & WSM_TX_SEQ_MAX;
	
	}
	if(status==0){
		spin_unlock_bh(&self->xmit_path_lock);
		return 1;
	}
error:
	spin_unlock_bh(&self->xmit_path_lock);
	return status;
}
#endif //CONFIG_USB_AGGR_URB_TX

void atbm_usb_kill_all_txurb(struct sbus_priv *self)
{
	int i=0;
	for(i=0;i<TX_URB_NUM;i++){
		usb_kill_urb(self->drvobj->tx_urb[i].test_urb);
	}

}
void atbm_usb_kill_all_rxurb(struct sbus_priv *self)
{
	int i=0;
	for(i=0;i<RX_URB_NUM;i++){
		usb_kill_urb(self->drvobj->rx_urb[i].test_urb);
	}

}

void atbm_usb_urb_free(struct sbus_priv *self,struct sbus_urb * pUrb,int max_num)
{
	int i=0;
#ifdef CONFIG_USE_DMA_ADDR_BUFFER
		if(self->drvobj->tx_dma_addr){
			atbm_usb_buffer_free(self->drvobj->pusbdev,self->drvobj->tx_dma_addr_buffer_len,self->drvobj->tx_dma_addr_buffer, self->drvobj->tx_dma_addr);
			self->drvobj->tx_dma_addr = 0;
		}
#endif //CONFIG_USE_DMA_ADDR_BUFFER
	for(i=0;i<max_num;i++){
		usb_kill_urb(pUrb[i].test_urb);
		usb_free_urb(pUrb[i].test_urb);
		if(pUrb[i].test_skb)
			atbm_dev_kfree_skb(pUrb[i].test_skb);
		pUrb[i].test_skb =NULL;
		pUrb[i].test_urb =NULL;
		#ifndef CONFIG_USE_DMA_ADDR_BUFFER
		if(pUrb[i].pallocated_buf){
			atbm_kfree(pUrb[i].pallocated_buf);
			pUrb[i].pallocated_buf = NULL;
		}
		#endif
	}
}


void atbm_usb_urb_map_show(struct sbus_priv *self)
{
	atbm_printk_err("tx_urb_map %lx\n",self->drvobj->tx_urb_map[0]);

}
#ifdef CONFIG_USE_DMA_ADDR_BUFFER


#ifndef ALIGN
#define ALIGN(a,b)			(((a) + ((b) - 1)) & (~((b)-1)))
#endif
#ifdef CONFIG_USB_AGGR_URB_TX
char * atbm_usb_pick_txDMABuf(struct sbus_priv *self)
{
	unsigned long flags=0;
	char * buf;
	spin_lock_irqsave(&self->lock, flags);
	if(self->drvobj->NextAllocPost == self->drvobj->NextFreePost){
		if(self->drvobj->tx_dma_addr_buffer_full){
			usb_printk( "atbm_usb_pick_txDMABuf:tx_dma_addr_buffer_full %d \n",self->drvobj->tx_dma_addr_buffer_full);
			spin_unlock_irqrestore(&self->lock, flags);
			return NULL;
		}
	}
	buf = self->drvobj->NextAllocPost;
	spin_unlock_irqrestore(&self->lock, flags);
	return buf;
}

char * atbm_usb_get_txDMABuf(struct sbus_priv *self)
{
	char * buf;
	unsigned long flags=0;
	spin_lock_irqsave(&self->lock, flags);
	if(self->drvobj->NextAllocPost == self->drvobj->NextFreePost){
		if(self->drvobj->tx_dma_addr_buffer_full){
			spin_unlock_irqrestore(&self->lock, flags);
			return NULL;
		}
	}
	else {
		
	}
	
	self->drvobj->free_dma_buffer_cnt--;
	if(self->drvobj->free_dma_buffer_cnt <0){
		self->drvobj->free_dma_buffer_cnt=0;
		atbm_printk_err("free_dma_buffer_cnt ERR,NextAllocPost %p drvobj->NextFreePost %p\n",self->drvobj->NextAllocPost, self->drvobj->NextFreePost);
		
		BUG_ON(1);
	}
	
	buf = self->drvobj->NextAllocPost;
	self->drvobj->NextAllocPost += BUFF_ALLOC_LEN;
	if(self->drvobj->NextAllocPost >= self->drvobj->tx_dma_addr_buffer_end){
		self->drvobj->NextAllocPost = self->drvobj->tx_dma_addr_buffer;
	}
	if(self->drvobj->NextAllocPost == self->drvobj->NextFreePost){
		self->drvobj->tx_dma_addr_buffer_full =1;
	}
	spin_unlock_irqrestore(&self->lock, flags);

	return buf;
}


void atbm_usb_free_txDMABuf(struct sbus_priv *self)
{
	if(self->drvobj->NextAllocPost == self->drvobj->NextFreePost){
		if(self->drvobj->tx_dma_addr_buffer_full==0){
			atbm_printk_err("self->drvobj->free_dma_buffer_cnt %d\n",self->drvobj->free_dma_buffer_cnt);
			WARN_ON(self->drvobj->tx_dma_addr_buffer_full==0);
			return;
		}
		self->drvobj->tx_dma_addr_buffer_full = 0;
	}
	else {
		
	}
	self->drvobj->free_dma_buffer_cnt++;
	if(self->drvobj->total_dma_buffer_cnt < self->drvobj->free_dma_buffer_cnt){
		atbm_printk_err("<WARNING> atbm_usb_free_txDMABuf (buffer(%p)  NextFreePost(%p))free_dma_buffer_cnt %d \n",
			self->drvobj->NextAllocPost, 
			self->drvobj->NextFreePost,
			self->drvobj->free_dma_buffer_cnt);
		
		BUG_ON(1);
		
	}
	self->drvobj->NextFreePost += BUFF_ALLOC_LEN;
	if(self->drvobj->NextFreePost == self->drvobj->tx_dma_addr_buffer_end){
		self->drvobj->NextFreePost = self->drvobj->tx_dma_addr_buffer;
	}
}
void atbm_usb_free_txDMABuf_all(struct sbus_priv *self,u8 * buffer,int cnt)
{
	unsigned long flags=0;
	spin_lock_irqsave(&self->lock, flags);
	WARN_ON(cnt==0);
	if((char *)buffer != self->drvobj->NextFreePost){			
		atbm_printk_err("<WARNING> atbm_usb_free_txDMABuf_all (buffer(%p) != NextFreePost(%p))free_dma_buffer_cnt %d cnt %d\n",buffer, self->drvobj->NextFreePost,self->drvobj->free_dma_buffer_cnt,cnt);
		BUG_ON(1);
		self->drvobj->NextFreePost = buffer;
	}
	while(cnt--){
		atbm_usb_free_txDMABuf(self);
	}
	spin_unlock_irqrestore(&self->lock, flags);
}
#endif
#endif //#ifdef CONFIG_USE_DMA_ADDR_BUFFER
int atbm_usb_urb_malloc(struct sbus_priv *self,struct sbus_urb * pUrb,int max_num,int len,int b_skb,int b_dma_buffer)
{
	int i=0;
#ifdef CONFIG_USE_DMA_ADDR_BUFFER
	len = ALIGN(len,PER_PACKET_LEN);
	self->drvobj->tx_dma_addr = 0;
	if(b_dma_buffer){
#ifdef CONFIG_USB_AGGR_URB_TX
		self->drvobj->tx_dma_addr_buffer_len=len*max_num*URB_AGGR_NUM;//ALIGN(len*max_num,4096);
#else
	    self->drvobj->tx_dma_addr_buffer_len=len*max_num;//ALIGN(len*max_num,4096);
#endif  //CONFIG_USB_AGGR_URB_TX
		atbm_printk_init("atbm_usb_urb_malloc CONFIG_USE_DMA_ADDR_BUFFER max_num %d, total %d\n",max_num,(int)self->drvobj->tx_dma_addr_buffer_len);
		self->drvobj->tx_dma_addr_buffer = atbm_usb_buffer_alloc(self->drvobj->pusbdev,self->drvobj->tx_dma_addr_buffer_len, &self->drvobj->tx_dma_addr);
		if(self->drvobj->tx_dma_addr_buffer==NULL){
			atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Can't allocate tx_dma_addr_buffer.");
			goto __free_urb;
		}
		usb_printk( "tx_dma_addr_buffer %x dma %x\n",self->drvobj->tx_dma_addr_buffer ,self->drvobj->tx_dma_addr);
	}
	
#ifdef CONFIG_USB_AGGR_URB_TX
	self->drvobj->NextAllocPost = self->drvobj->tx_dma_addr_buffer;
	self->drvobj->NextFreePost = self->drvobj->tx_dma_addr_buffer;
	self->drvobj->tx_dma_addr_buffer_end = self->drvobj->tx_dma_addr_buffer+self->drvobj->tx_dma_addr_buffer_len;
	self->drvobj->tx_dma_addr_buffer_full = 0;
	self->drvobj->free_dma_buffer_cnt= self->drvobj->tx_dma_addr_buffer_len/BUFF_ALLOC_LEN;
	self->drvobj->total_dma_buffer_cnt= self->drvobj->tx_dma_addr_buffer_len/BUFF_ALLOC_LEN;
	atbm_printk_init("CONFIG_USB_AGGR_URB_TX enable cnt tx_dma_addr_buffer_end(%p)tx_dma_addr_buffer(%p),%d\n",
		self->drvobj->tx_dma_addr_buffer,self->drvobj->tx_dma_addr_buffer_end,(int)self->drvobj->tx_dma_addr_buffer_len/BUFF_ALLOC_LEN);
#endif  //CONFIG_USB_AGGR_URB_TX
#endif //#ifdef CONFIG_USE_DMA_ADDR_BUFFER
	
	for(i=0;i<max_num;i++){
		pUrb[i].test_urb=usb_alloc_urb(0,GFP_KERNEL);
		if (!pUrb[i].test_urb){
			atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Can't allocate test_urb.");
			goto __free_urb;
		}
#ifdef CONFIG_USE_DMA_ADDR_BUFFER
		
		if(b_dma_buffer){		
#ifdef CONFIG_USB_AGGR_URB_TX
			pUrb[i].pallocated_buf =NULL;
			pUrb[i].test_skb = NULL;
			pUrb[i].pallocated_buf_len = 0;
			pUrb[i].dma_transfer_addr = 0;
#else
			pUrb[i].pallocated_buf =self->drvobj->tx_dma_addr_buffer+i*len;
			pUrb[i].test_skb = NULL;
			pUrb[i].pallocated_buf_len = len;
			pUrb[i].dma_transfer_addr = self->drvobj->tx_dma_addr+i*len;
#endif //USB_ATHENAB_AGGR
		}
		else 
#endif //#ifdef CONFIG_USE_DMA_ADDR_BUFFER
		if(b_skb){
			pUrb[i].test_skb = __atbm_dev_alloc_skb(len,GFP_KERNEL);
			if (!pUrb[i].test_skb){
				atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Can't allocate test_skb.");
				goto __free_skb;
			}
			pUrb[i].pallocated_buf = NULL;
		}else {
#ifndef CONFIG_USE_DMA_ADDR_BUFFER
			pUrb[i].pallocated_buf = atbm_kzalloc(len,GFP_KERNEL);
			if(pUrb[i].pallocated_buf == NULL){
				atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Can't allocate pallocated_buf.");
				goto __free_skb;
			}
#endif 
			pUrb[i].test_skb = NULL;
		}
		pUrb[i].urb_id = i;
		pUrb[i].obj = self;
		pUrb[i].data = NULL;
	}
	return 0;
__free_skb:
#ifdef CONFIG_USE_DMA_ADDR_BUFFER
	if(self->drvobj->tx_dma_addr){
		atbm_usb_buffer_free(self->drvobj->pusbdev,self->drvobj->tx_dma_addr_buffer_len,self->drvobj->tx_dma_addr_buffer, self->drvobj->tx_dma_addr);
		self->drvobj->tx_dma_addr = 0;
	}
#endif //CONFIG_USE_DMA_ADDR_BUFFER
#ifndef CONFIG_USE_DMA_ADDR_BUFFER
	for( ;i>=0;--i){
		if(pUrb[i].pallocated_buf){
			atbm_kfree(pUrb[i].pallocated_buf);
			pUrb[i].pallocated_buf = NULL;
		}
	}
#endif
	for( ;i>=0;--i){
		#ifndef CONFIG_USE_DMA_ADDR_BUFFER
		if(pUrb[i].pallocated_buf){
			atbm_kfree(pUrb[i].pallocated_buf);
			pUrb[i].pallocated_buf = NULL;
		}
		#endif
		atbm_dev_kfree_skb(pUrb[i].test_skb);
	}
	i = max_num;
__free_urb:
	for( ;i>=0;--i){
		usb_free_urb(pUrb[i].test_urb);
	}

	return -ENOMEM;
}
#ifdef CONFIG_USB_DATA_XMIT_DIRECTLY
static int atbm_usb_data_send(struct sbus_priv *self)
{
	int status;
	struct atbm_common *hw_priv = self->core;
	hw_priv->sbus_ops->lock(hw_priv->sbus_priv);
	do{
		status = hw_priv->sbus_ops->sbus_memcpy_toio(hw_priv->sbus_priv,0x1,NULL,TX_BUFFER_SIZE);
	}while(status > 0);
	hw_priv->sbus_ops->unlock(hw_priv->sbus_priv);
	
	return 0;
}
#endif
static int atbm_usb_wait_submitted_xmited(struct sbus_priv *self)
{
	int status = 0;

	status = usb_wait_anchor_empty_timeout(&self->drvobj->tx_submitted,10000);

	if(status == 0)
		return -1;
#ifdef CONDIF_ATBM_CTRL_REQUEST_ASYNC
	status = usb_wait_anchor_empty_timeout(&self->drvobj->ctrl_submitted,10000);	
	if(status == 0)
		return -1;
	status = atomic_read(&self->drvobj->ctrl_err);
#endif
	return status;
}
int atbm_usb_urb_get(struct sbus_priv *self,unsigned long *bitmap,int max_urb,int tx)
{
	int id = 0;
	unsigned long flags=0;

	spin_lock_irqsave(&self->lock, flags);
	id= find_first_zero_bit(bitmap,max_urb);
	if((id>=max_urb)||(id<0)){
		spin_unlock_irqrestore(&self->lock, flags);
		return -1;
	}
	__set_bit(id,bitmap);
	if(tx){
		wifi_tx_urb_pending++;
		WARN_ON(wifi_tx_urb_pending>TX_URB_NUM);
	}
	spin_unlock_irqrestore(&self->lock, flags);

	return id;
}
void atbm_usb_urb_put(struct sbus_priv *self,unsigned long *bitmap,int id,int tx)
{
	unsigned long flags=0;
	int release = 1;
	spin_lock_irqsave(&self->lock, flags);
	if(tx){
		wifi_tx_urb_pending--;
		WARN_ON(wifi_tx_urb_pending<0);
	}
	//WARN_ON((*bitmap & BIT(id))==0);
	
	if(release)
		__clear_bit(id,bitmap);
	spin_unlock_irqrestore(&self->lock, flags);
}

static void atbm_usb_submit_rev_skb(struct sbus_priv *self,struct sk_buff *skb,int urb_id)
{
	struct atbm_common *hw_priv = self->core;
	unsigned long flags;
	bool rx_running;

	spin_lock_irqsave(&hw_priv->rx_frame_queue.lock, flags);
	__atbm_skb_queue_tail(&hw_priv->rx_frame_queue, skb);
	rx_running = hw_priv->bh_running;
	spin_unlock_irqrestore(&hw_priv->rx_frame_queue.lock, flags);

	if(urb_id != -1)
		atbm_usb_urb_put(self,self->drvobj->rx_urb_map,urb_id,0);
	
	if(rx_running == false)
		atbm_usb_rev_schedule(self);
}
#ifdef CONFIG_USB_URB_RX_SUBMIT_DIRECTLY
static bool atbm_usb_giveback_rev_skb_cb(struct wsm_rx_encap *encap,struct sk_buff *skb)
{
	if(likely(skb->pkt_type != ATBM_RX_WSM_CMD_FRAME)){
		atbm_usb_submit_rev_skb(encap->hw_priv->sbus_priv,skb,-1);
	}else {
		struct sbus_urb *rx_urb = (struct sbus_urb *)encap->priv;
		rx_urb->test_skb = NULL;
		atbm_usb_submit_rev_skb(rx_urb->obj,skb,rx_urb->urb_id);
	}
	return true;
}
#endif
static void atbm_usb_receive_data_complete(struct urb *urb)
{
	struct sbus_urb *rx_urb=(struct sbus_urb*)urb->context;
	struct sbus_priv *self = rx_urb->obj;
	struct sk_buff *skb = rx_urb->test_skb;
	struct atbm_common *hw_priv=self->core;
	int RecvLength = urb->actual_length + rx_urb->offset;
	struct wsm_hdr *wsm;
	
	usb_printk( "rxend  Len %d\n",RecvLength);

	switch(urb->status){
		case 0:
			break;
		case -ENOENT:
		case -ECONNRESET:
		case -ENODEV:
		case -ESHUTDOWN:
			atbm_printk_err("atbm_usb_rx_complete1 error status=%d len %d\n",urb->status,RecvLength);
			goto usb_wk;
		case -EPROTO:
			atbm_printk_err( "atbm_usb_rx_complete3 error status=%d len %d\n",urb->status,RecvLength);
			if(RecvLength !=0){
				break;
			}
			atbm_fallthrough;
		case -EOVERFLOW:
			atbm_printk_err("<ERROR>:EOVERFLOW,len=%d\n",RecvLength);
			if(RecvLength)
				break;
			atbm_printk_err("<ERROR>:atbm_usb_rx_complete status=%d len %d\n",urb->status,RecvLength);
			goto usb_wk;
		default:
			atbm_printk_err("atbm_usb_rx_complete2 error status=%d len %d\n",urb->status,RecvLength);
			goto usb_wk;
	}
	
	wsm = (struct wsm_hdr *)skb->data;

	do{
		if((RecvLength % self->drvobj->ep_in_size) == 0){
			atbm_printk_err("atbm_usb_rx_complete len not enough[%x][%d][%d]\n",wsm->id,le16_to_cpu(wsm->len),RecvLength);
			goto usb_wk;
		}

		if(RecvLength < le16_to_cpu(wsm->len)){
			atbm_printk_err("atbm_usb_rx_complete len err(%d)(%d)(%x)\n",RecvLength,le16_to_cpu(wsm->len),le16_to_cpu(wsm->id));
			goto drop_current;
		}

		switch(atbm_hif_trace_check(wsm,RecvLength)){
		case HIF_TRACE_RESULT_IGNORE:
			if((le16_to_cpu(wsm->len) != RecvLength) && (le16_to_cpu(wsm->len) + 1 != RecvLength)){
				atbm_printk_err("atbm_usb_rx_complete wsm len [%x][%d][%d]\n",wsm->id,le16_to_cpu(wsm->len),RecvLength);
				/*drop the err package*/
				goto drop_current;
			}
			break;
		case HIF_TRACE_RESULT_FAIL:
			goto  drop_current;
		case HIF_TRACE_RESULT_COMP:
			break;
		}
		
		break;
drop_current:
		/*drop the err package*/
		urb->actual_length = 0;
		rx_urb->offset 	= 0;
		rx_urb->retries = 0;
		goto usb_wk;
	}while(0);
	
	
	if (WARN_ON(4 > RecvLength)){
		frame_hexdump("atbm_usb_receive_data_complete",(u8 *)wsm,32);
		goto usb_wk;
	}

	BUG_ON(RecvLength > RX_BUFFER_SIZE);
	
	skb->pkt_type = ATBM_RX_RAW_FRAME;
	
	if(!hw_priv->init_done){
		atbm_printk_err( "[BH] irq. init_done =0 drop\n");
		goto free_urb;
	}
	if (/* WARN_ON */(hw_priv->bh_error))
		goto free_urb;

#ifdef CONFIG_USB_URB_RX_SUBMIT_DIRECTLY
	if(0){
	}else {
		struct wsm_rx_encap encap;
		encap.hw_priv = hw_priv;
		encap.skb = skb;
		encap.rx_func = atbm_usb_giveback_rev_skb_cb;
		encap.check_seq = true;
		encap.wsm = (struct wsm_hdr *)skb->data;
		encap.priv = rx_urb;
		
		if(atbm_rx_directly(&encap) == true){
			int status=0;		
			rx_urb->test_skb=skb;
			atbm_skb_trim(rx_urb->test_skb,0);

			usb_fill_bulk_urb(rx_urb->test_urb, 
							  self->drvobj->pusbdev, 
							  usb_rcvbulkpipe(self->drvobj->pusbdev, self->drvobj->ep_in),
							  skb->data,RX_BUFFER_SIZE,atbm_usb_receive_data_complete,rx_urb);
			usb_anchor_urb(rx_urb->test_urb, &self->drvobj->rx_submitted);
			status = usb_submit_urb(rx_urb->test_urb, GFP_ATOMIC);
			if (status) {
				usb_unanchor_urb(rx_urb->test_urb);
				atbm_printk_err("receive_data usb_submit_urb ++ ERR %d\n",status);
				goto usb_wk;
			}
		}
		return;
	}
#else
	rx_urb->test_skb = NULL;
	atbm_usb_submit_rev_skb(self,skb,rx_urb->urb_id);
#endif
	return;
free_urb:
	atbm_printk_err("free rx urb(%d)\n",rx_urb->urb_id);
	atbm_usb_urb_put(self,self->drvobj->rx_urb_map,rx_urb->urb_id,0);
	return;
usb_wk:
	atbm_usb_schedule_urb_work(self,rx_urb,&self->drvobj->recv_err);
	return;

}




static int atbm_usb_receive_data(struct sbus_priv *self,unsigned int addr,void *dst, int count,sbus_callback_handler hander)
{
	unsigned int pipe;
	int status=0;
	struct sk_buff *skb;
	struct sbus_urb *rx_urb;
	int urb_id;

	if(unlikely(self->suspend == 1)){
		atbm_printk_err("%s:usb suspend\n",__func__);
		status =  2;
		goto system_err;
	}
	if(unlikely(atbm_wifi_get_status()>=2)){
		atbm_printk_err("atbm_usb_receive_data drop urb req because rmmod driver\n");
		status = 3;
		goto system_err;
	}

	urb_id = atbm_usb_urb_get(self,self->drvobj->rx_urb_map,RX_URB_NUM,0);
	if(urb_id<0){
		status=-4;
		goto system_err;
	}
	rx_urb = &self->drvobj->rx_urb[urb_id];
	//if not rxdata complete
	//initial new rxdata
	if(rx_urb->test_skb == NULL){
		if(dst ){
			rx_urb->test_skb=dst;
			atbm_skb_trim(rx_urb->test_skb,0);

		}
		else {
			skb=atbm_dev_alloc_skb(count+64);
			if (!skb){
				status=-1;
				atbm_printk_err("atbm_usb_receive_data++ atbm_dev_alloc_skb %p ERROR\n",skb);
				goto __err_skb;
			}
			atbm_skb_reserve(skb, 64);
			rx_urb->test_skb=skb;
		}
	}
	else {
		if(dst ){	
			atbm_dev_kfree_skb(dst);		
		}

	}

	if(unlikely(atbm_hif_trace_running(self->core) == true)){
		status = -3;
		goto __err_skb;
	}

	skb = rx_urb->test_skb;
	rx_urb->offset  = 0;
	rx_urb->retries = 0;
	rx_urb->test_urb->actual_length = 0;
	
	pipe = usb_rcvbulkpipe(self->drvobj->pusbdev, self->drvobj->ep_in);
	usb_fill_bulk_urb(rx_urb->test_urb, self->drvobj->pusbdev, pipe,skb->data,count,atbm_usb_receive_data_complete,rx_urb);
	usb_anchor_urb(rx_urb->test_urb, &self->drvobj->rx_submitted);
	status = usb_submit_urb(rx_urb->test_urb, GFP_ATOMIC);
	usb_printk( "usb_rx urb_id %d\n",urb_id);
	if (status) {
		usb_unanchor_urb(rx_urb->test_urb);
		status = -2;
		//atomic_xchg(&self->rx_lock, 0);
		atbm_printk_err("receive_data usb_submit_urb ++ ERR %d\n",status);
		goto __err_skb;
	}

__err_skb:
	if(status < 0){
		atbm_usb_urb_put(self,self->drvobj->rx_urb_map,urb_id,0);
	}
	return status;
system_err:
	if(dst)
		atbm_dev_kfree_skb(dst);
	return status;
}


static int atbm_usb_memcpy_fromio(struct sbus_priv *self,
				     unsigned int addr,
				     void *dst, int count)
{
	int i=0;
	for(i=0;i<RX_URB_NUM;i++){
	 	atbm_usb_receive_data(self,addr,dst,count,NULL);
	}
	return 0;
}
#if 0
void atbm_usb_rxlock(struct sbus_priv *self)
{
	atomic_add(1, &self->rx_lock);
}
void atbm_usb_rxunlock(struct sbus_priv *self)
{
	atomic_sub(1, &self->rx_lock);
}
#endif
static int atbm_usb_memcpy_toio(struct sbus_priv *self,
				   unsigned int addr,
				   const void *src, int count)
{
	return atbm_usb_xmit_data(self, addr,(void *)src,count,NULL);
}

static int atbm_usb_memcpy_fromio_async(struct sbus_priv *self,
				     unsigned int addr,
				     void *dst, int count,sbus_callback_handler func)
{
	 int ret = 0;

	//  self->rx_callback_handler = func;

	// if(atomic_add_return(0, &self->rx_lock)){
	//	 return -1;
	 //}

	 atbm_usb_receive_data(self,addr,dst,count,func);

	 return ret;
}

static int atbm_usb_memcpy_toio_async(struct sbus_priv *self,
				   unsigned int addr,
				   const void *src, int count ,sbus_callback_handler func)
{
	 int ret =  0;
	 
	 if(unlikely(self->suspend == 1)){
	 	return 0;
	 }
	 atbm_usb_xmit_data(self, addr,(void *)src, count,func);

	 return ret;
}

static void atbm_usb_lock(struct sbus_priv *self)
{
	//mutex_lock(&self->sbus_mutex);
	atbm_usb_pm_async(self,0);

}

static void atbm_usb_unlock(struct sbus_priv *self)
{
	//mutex_unlock(&self->sbus_mutex);
	atbm_usb_pm_async(self,1);
}

static int atbm_usb_off(const struct atbm_platform_data *pdata)
{
	int ret = 0;

	//if (pdata->insert_ctrl)
	//	ret = pdata->insert_ctrl(pdata, false);
	return ret;
}

static int atbm_usb_on(const struct atbm_platform_data *pdata)
{
	int ret = 0;

   // if (pdata->insert_ctrl)
	//	ret = pdata->insert_ctrl(pdata, true);

	return ret;
}


static int atbm_usb_reset(struct sbus_priv *self)
{
//	u32 regdata = 1;
	atbm_printk_bus(" %s\n",__func__);
	usb_reset_device(interface_to_usbdev(self->drvobj->pusbintf));
	//#ifdef HW_DOWN_FW
	//atbm_usb_hw_write_port(self,0x16100074,&regdata,4);
	//#else
	//atbm_usb_sw_write_port(self,0x16100074,&regdata,4);
	//#endif
	return 0;
}
static int atbm_usb_lock_reset(struct sbus_priv *self){
	int result;
	result=usb_lock_device_for_reset(interface_to_usbdev(self->drvobj->pusbintf),self->drvobj->pusbintf);
	if(result<0){
		atbm_printk_err("unable to lock device for reset :%d\n",result);
	}else{
		result=usb_reset_device(interface_to_usbdev(self->drvobj->pusbintf));
		usb_unlock_device(interface_to_usbdev(self->drvobj->pusbintf));
	}
	return result;
}
static u32 atbm_usb_align_size(struct sbus_priv *self, u32 size)
{
	size_t aligned = size;
	return aligned;
}

int atbm_usb_set_block_size(struct sbus_priv *self, u32 size)
{
	return 0;
}
#ifdef CONFIG_PM
int atbm_usb_pm(struct sbus_priv *self, bool  auto_suspend)
{
	int ret = 0;

	//printk("atbm_usb_pm %d -> %d\n",self->auto_suspend ,auto_suspend);
	//if(self->auto_suspend == auto_suspend){
		//printk("***********func=%s,usage_count=%d\n",__func__,self->drvobj->pusbdev->dev.power.usage_count);
	//	return;
	//}
	self->auto_suspend  = auto_suspend;
#if USB_AUTO_WAKEUP
	if(auto_suspend){
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
			usb_autopm_put_interface(self->drvobj->pusbintf);
#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,20))
			usb_autopm_enable(self->drvobj->pusbintf);
#else
			usb_autosuspend_device(self->drvobj->pusbdev, 1);
#endif //(LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
	}
	else { //if(auto_suspend)
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
		usb_autopm_get_interface(self->drvobj->pusbintf);
#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,20))
			usb_autopm_disable(self->drvobj->pusbintf);
#else //(LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
			usb_autoresume_device(self->drvobj->pusbdev, 1);
#endif //(LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
	}
#endif  //#if USB_AUTO_WAKEUP
	//printk("***********func=%s,usage_count=%d\n",__func__,self->drvobj->pusbdev->dev.power.usage_count);
	return ret;
}
int atbm_usb_pm_async(struct sbus_priv *self, bool  auto_suspend)
{
	int ret = 0;

	self->auto_suspend  = auto_suspend;
	
#if USB_AUTO_WAKEUP
	if(auto_suspend){
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
		usb_autopm_put_interface_async(self->drvobj->pusbintf);
#else
#endif
	}
	else {
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
		usb_autopm_get_interface_async(self->drvobj->pusbintf);
#else
#endif
	}
#endif  //#if USB_AUTO_WAKEUP

	return ret;
}
#else
int atbm_usb_pm(struct sbus_priv *self, bool  auto_suspend)
{
	return 0;
}
int atbm_usb_pm_async(struct sbus_priv *self, bool  auto_suspend)
{

	return 0;
}
#endif
static int atbm_usb_irq_subscribe(struct sbus_priv *self, sbus_irq_handler handler,void *priv)
{
	int ret = atbm_usb_alloc_urb_work(self);

	if(self->drvobj->pusbdev->state < USB_STATE_UNAUTHENTICATED){
		atbm_printk_err("usb disconnecting,can not try anything\n");
	}else do{
		char *flush_buff;
		int  flush_len = 0;
		int  status = 0;
		
		atbm_printk_exit("usb:try to flush rx....\n");

		flush_buff = atbm_kzalloc(RX_BUFFER_SIZE,GFP_KERNEL);

		if(flush_buff == NULL){
			break;
		}

		do{
			extern void atbm_monitor_pc(struct atbm_common *hw_priv);
			
			flush_len = 0;
			
			status = usb_bulk_msg(self->drvobj->pusbdev,
						 usb_rcvbulkpipe(self->drvobj->pusbdev, self->drvobj->ep_in),
						 flush_buff,RX_BUFFER_SIZE,&flush_len,100);
			atbm_printk_err("flush rx[%d][%d]\n",status,flush_len);

			if(flush_len){
				struct wsm_hdr *wsm = (struct wsm_hdr *)flush_buff;

				atbm_printk_err("flush wsm[%x][%x]\n",__le16_to_cpu(wsm->len),__le16_to_cpu(wsm->id));
				atbm_monitor_pc(self->core);
			}
			
		}while(flush_len);

		atbm_kfree(flush_buff);
	}while(0);
	
	if(self->syshold)
		atbm_usb_ep0_hw_reset_cmd(self,HW_RUN_CPU,0);
	return ret;
}
static int atbm_usb_irq_unsubscribe(struct sbus_priv *self)
{
	atbm_usb_destroy_urb_work(self);
	return 0;
}
#ifdef  CONFIG_SUPPORT_HIF_TRACE
static int atbm_usb_pre_hif_trace(struct sbus_priv *self)
{
	
#ifdef USB_USE_TASTLET_TXRX
	tasklet_disable(&self->tx_cmp_tasklet);
	tasklet_disable(&self->rx_cmp_tasklet);
#endif
	atbm_usb_flush_urb_work(self);
	return 0;
}

static int atbm_usb_post_hif_trace(struct sbus_priv *self)
{
	
#ifdef USB_USE_TASTLET_TXRX
	tasklet_enable(&self->rx_cmp_tasklet);
	tasklet_enable(&self->tx_cmp_tasklet);
#endif
	atbm_usb_memcpy_fromio(self,0x2,NULL,RX_BUFFER_SIZE);
	return 0;
}
#endif
static struct sbus_ops atbm_usb_sbus_ops = {
	.sbus_memcpy_fromio	= atbm_usb_memcpy_fromio,
	.sbus_memcpy_toio	= atbm_usb_memcpy_toio,
#ifdef HW_DOWN_FW
	.sbus_read_sync		= atbm_usb_hw_read_port,
	.sbus_write_sync	= atbm_usb_hw_write_port,
#else //SW
	.sbus_read_sync		= atbm_usb_sw_read_port,
	.sbus_write_sync	= atbm_usb_sw_write_port,
#endif
	.sbus_read_async	= atbm_usb_memcpy_fromio_async,
	.sbus_write_async	= atbm_usb_memcpy_toio_async,
	.lock				= atbm_usb_lock,
	.unlock				= atbm_usb_unlock,
	.reset				= atbm_usb_reset,
	.usb_lock_reset     = atbm_usb_lock_reset,
	.align_size			= atbm_usb_align_size,
	.power_mgmt			= atbm_usb_pm,
	.set_block_size		= atbm_usb_set_block_size,
#ifdef ATBM_USB_RESET
	.usb_reset			= atbm_usb_reset,
#endif
	.bootloader_debug_config = atbm_usb_debug_config,
	.lmac_start			=atbm_lmac_start,
#ifdef USB_CMD_UES_EP0	
	.ep0_cmd			=atbm_usb_ep0_cmd,
#endif
	.irq_unsubscribe	= atbm_usb_irq_unsubscribe,
	.irq_subscribe	= atbm_usb_irq_subscribe,
	.lmac_restart   = atbm_lmac_restart,
#ifdef CONFIG_USB_DATA_XMIT_DIRECTLY
	.sbus_data_write = atbm_usb_data_send,
#endif
	.sbus_wait_data_xmited = atbm_usb_wait_submitted_xmited,
	.sbus_xmit_func_init   = atbm_usb_xmit_init,
	.sbus_xmit_func_deinit  = atbm_usb_xmit_deinit,
	.sbus_rev_func_init    = atbm_usb_rev_init,
	.sbus_rev_func_deinit  = atbm_usb_rev_deinit,
	.sbus_xmit_schedule    = atbm_usb_xmit_schedule,
	.sbus_rev_schedule     = atbm_usb_rev_schedule,
#ifdef  CONFIG_SUPPORT_HIF_TRACE
	.sbus_pre_hif_trace    = atbm_usb_pre_hif_trace,
	.sbus_post_hif_trace   = atbm_usb_post_hif_trace,
#endif
};

static struct dvobj_priv *usb_dvobj_init(struct usb_interface *usb_intf)
{
	int	i;
	struct dvobj_priv *pdvobjpriv=NULL;
	struct usb_device_descriptor 	*pdev_desc;
	struct usb_host_config			*phost_conf;
	struct usb_config_descriptor		*pconf_desc;
	struct usb_host_interface		*phost_iface;
	struct usb_interface_descriptor	*piface_desc;
	struct usb_host_endpoint		*phost_endp;
	struct usb_endpoint_descriptor	*pendp_desc;
	struct usb_device				*pusbd;
	
	pdvobjpriv = atbm_kzalloc(sizeof(*pdvobjpriv), GFP_KERNEL);
	if (!pdvobjpriv){
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Can't allocate USB dvobj.");
		goto exit;
	}
	pdvobjpriv->pusbintf = usb_intf ;
	pusbd = pdvobjpriv->pusbdev = interface_to_usbdev(usb_intf);
	usb_set_intfdata(usb_intf, pdvobjpriv);
#ifdef CONFIG_PM
#ifdef ATBM_SUSPEND_REMOVE_INTERFACE
	pusbd->reset_resume = 1;
#endif
#endif
	//pdvobjpriv->RtNumInPipes = 0;
	//pdvobjpriv->RtNumOutPipes = 0;
	pdev_desc = &pusbd->descriptor;
#if 1
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"\natbm_usb_device_descriptor:\n");
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bLength=%x\n", pdev_desc->bLength);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bDescriptorType=%x\n", pdev_desc->bDescriptorType);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bcdUSB=%x\n", pdev_desc->bcdUSB);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bDeviceClass=%x\n", pdev_desc->bDeviceClass);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bDeviceSubClass=%x\n", pdev_desc->bDeviceSubClass);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bDeviceProtocol=%x\n", pdev_desc->bDeviceProtocol);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bMaxPacketSize0=%x\n", pdev_desc->bMaxPacketSize0);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"idVendor=%x\n", pdev_desc->idVendor);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"idProduct=%x\n", pdev_desc->idProduct);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bcdDevice=%x\n", pdev_desc->bcdDevice);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"iManufacturer=%x\n", pdev_desc->iManufacturer);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"iProduct=%x\n", pdev_desc->iProduct);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"iSerialNumber=%x\n", pdev_desc->iSerialNumber);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bNumConfigurations=%x\n", pdev_desc->bNumConfigurations);
#endif

	phost_conf = pusbd->actconfig;
	pconf_desc = &phost_conf->desc;
#if 1
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"\natbm_usb_configuration_descriptor:\n");
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bLength=%x\n", pconf_desc->bLength);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bDescriptorType=%x\n", pconf_desc->bDescriptorType);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"wTotalLength=%x\n", pconf_desc->wTotalLength);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bNumInterfaces=%x\n", pconf_desc->bNumInterfaces);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bConfigurationValue=%x\n", pconf_desc->bConfigurationValue);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"iConfiguration=%x\n", pconf_desc->iConfiguration);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bmAttributes=%x\n", pconf_desc->bmAttributes);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bMaxPower=%x\n", pconf_desc->bMaxPower);
#endif

	phost_iface = &usb_intf->altsetting[0];
	piface_desc = &phost_iface->desc;
#if 1
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"\natbm_usb_interface_descriptor:\n");
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bLength=%x\n", piface_desc->bLength);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bDescriptorType=%x\n", piface_desc->bDescriptorType);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bInterfaceNumber=%x\n", piface_desc->bInterfaceNumber);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bAlternateSetting=%x\n", piface_desc->bAlternateSetting);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bNumEndpoints=%x\n", piface_desc->bNumEndpoints);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bInterfaceClass=%x\n", piface_desc->bInterfaceClass);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bInterfaceSubClass=%x\n", piface_desc->bInterfaceSubClass);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"bInterfaceProtocol=%x\n", piface_desc->bInterfaceProtocol);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"iInterface=%x\n", piface_desc->iInterface);
#endif

	//pdvobjpriv->NumInterfaces = pconf_desc->bNumInterfaces;
	//pdvobjpriv->InterfaceNumber = piface_desc->bInterfaceNumber;
	pdvobjpriv->nr_endpoint = piface_desc->bNumEndpoints;


	for (i = 0; i < pdvobjpriv->nr_endpoint; i++)
	{
		phost_endp = phost_iface->endpoint + i;
		if (phost_endp)
		{
			pendp_desc = &phost_endp->desc;

			atbm_dbg(ATBM_APOLLO_DBG_MSG,"\nusb_endpoint_descriptor(%d):\n", i);
			atbm_dbg(ATBM_APOLLO_DBG_MSG,"bLength=%x\n",pendp_desc->bLength);
			atbm_dbg(ATBM_APOLLO_DBG_MSG,"bDescriptorType=%x\n",pendp_desc->bDescriptorType);
			atbm_dbg(ATBM_APOLLO_DBG_MSG,"bEndpointAddress=%x\n",pendp_desc->bEndpointAddress);
			atbm_dbg(ATBM_APOLLO_DBG_MSG,"wMaxPacketSize=%d\n",le16_to_cpu(pendp_desc->wMaxPacketSize));
			atbm_dbg(ATBM_APOLLO_DBG_MSG,"bInterval=%x\n",pendp_desc->bInterval);

			if (usb_endpoint_is_bulk_in(pendp_desc))
			{
				atbm_dbg(ATBM_APOLLO_DBG_MSG,"usb_endpoint_is_bulk_in = %x\n",usb_endpoint_num(pendp_desc));
				pdvobjpriv->ep_in_size=le16_to_cpu(pendp_desc->wMaxPacketSize);
				pdvobjpriv->ep_in=usb_endpoint_num(pendp_desc);
			}
			else if (usb_endpoint_is_bulk_out(pendp_desc))
			{
				atbm_dbg(ATBM_APOLLO_DBG_MSG,"usb_endpoint_is_bulk_out = %x\n",usb_endpoint_num(pendp_desc));
				pdvobjpriv->ep_out_size=le16_to_cpu(pendp_desc->wMaxPacketSize);
				pdvobjpriv->ep_out=usb_endpoint_num(pendp_desc);
			}
			pdvobjpriv->ep_num[i] = usb_endpoint_num(pendp_desc);
		}
	}
	usb_get_dev(pusbd);
	atbm_dbg(ATBM_APOLLO_DBG_MSG,"nr_endpoint=%d, in_num=%d, out_num=%d\n\n", pdvobjpriv->nr_endpoint, pdvobjpriv->ep_in, pdvobjpriv->ep_out);
exit:
	return pdvobjpriv;
}

/*
*you can modify this function,but NOTIFY THAT:
*in this funciton you can not SLEEP!!!
*/
static void atbm_usb_sta_deauthen_iterate_atomic_cb(void *data, u8 *mac,
			 struct ieee80211_vif *vif)
{
	struct atbm_common *hw_priv = (struct atbm_common *)data;
	struct atbm_vif *priv = ABwifi_get_vif_from_ieee80211(vif);

	if(hw_priv != priv->hw_priv){
		return;
	}

	if(priv->join_status != ATBM_APOLLO_JOIN_STATUS_STA){
		return;
	}

	atbm_printk_exit( "[%pM] force deauthen\n",mac);
	ieee80211_connection_loss(vif);
}
static void atbm_process_system_action(struct atbm_common *hw_priv,
														enum atbm_system_action action)
{
	struct ieee80211_local *local = hw_to_local(hw_priv->hw);

	switch(action){
		
		case ATBM_SYSTEM_REBOOT:
		case ATBM_SYSTEM_RMMOD:
			atomic_set(&local->resume_timer_start,1);
			atbm_mod_timer(&local->resume_timer, round_jiffies(jiffies + 5*HZ));
			break;
		case ATBM_SYSTEM_NORMAL:
		case ATBM_SYSTEM_MAX:
		default:
			break;
	}

	
}
static void atbm_usb_station_diconnect_sync(struct atbm_common *hw_priv,
														 enum atbm_system_action action)
{	
	struct ieee80211_hw *hw = NULL;
	struct ieee80211_local *local = NULL;
	struct ieee80211_sub_if_data *sdata;
	
	if(hw_priv == NULL){
		goto sync_end;
	}
	hw = hw_priv->hw;

	if(hw_priv != hw->priv){
		goto sync_end;
	}
	/*
	*according to system action,do some job:
	*reboot or rmmod:disable supplicant authen
	*/
	atbm_process_system_action(hw_priv,action);
	/*
	*send deauthen to all connected ap
	*/
	local = hw_to_local(hw);
	ieee80211_iterate_active_interfaces_atomic(hw,atbm_usb_sta_deauthen_iterate_atomic_cb,
											   (void*)hw_priv);
	/*
	*make sure beacon_connection_loss_work has been processed
	*/
	atbm_flush_workqueue(local->workqueue);
	/*
	*make sure deauthen has been send;
	*/
	
	mutex_lock(&local->mtx);
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list)
		drv_flush(local, sdata, false);
	mutex_unlock(&local->iflist_mtx);
	mutex_unlock(&local->mtx);
	/*
	*make sure unjoin work complete
	*/
	atbm_flush_workqueue(hw_priv->workqueue);	
sync_end:
	return;
}
#ifdef ANDROID
static int g_rebootSystem = 0;

static void atbm_usb_reboot_sync(void)
{
	atbm_usb_module_lock();
	atbm_usb_station_diconnect_sync(atbm_hw_priv_dereference(),ATBM_SYSTEM_REBOOT);	
	atbm_usb_fw_sync(atbm_hw_priv_dereference(),atbm_usb_dvobj_dereference());
	atbm_usb_module_unlock();
}


static int atbm_reboot_notifier(struct notifier_block *nb,
				unsigned long action, void *unused)
{
	atbm_printk_exit("atbm_reboot_notifier(%ld)\n",action);
	g_rebootSystem =0;
	atbm_usb_reboot_sync();
	if(action == SYS_POWER_OFF){
		return NOTIFY_DONE;
	}
	atbm_usb_exit();
	return NOTIFY_DONE;
}


/* Probe Function to be called by USB stack when device is discovered */
static struct notifier_block atbm_reboot_nb = {
	.notifier_call = atbm_reboot_notifier,
	.priority=1,
};
#endif
#if defined(CONFIG_HAS_EARLYSUSPEND)
#ifdef ATBM_PM_USE_EARLYSUSPEND
static void atbm_early_suspend(struct early_suspend *h)
{
	struct sbus_priv *self = container_of(h, struct sbus_priv, atbm_early_suspend);
	
	if (unlikely(!down_trylock(&self->early_suspend_lock))) {
		atbm_printk_bus("zezer:early suspend--------------------\n");
		atomic_set(&self->early_suspend_state, 1);	
		wiphy_rfkill_set_hw_state(self->core->hw->wiphy,true);
		up(&self->early_suspend_lock);
	}
}
static void atbm_late_resume(struct early_suspend *h)
{
	struct sbus_priv *self = container_of(h, struct sbus_priv, atbm_early_suspend);
	down(&self->early_suspend_lock);
	if(atomic_read(&self->early_suspend_state)){
		atomic_set(&self->early_suspend_state, 0);
		wiphy_rfkill_set_hw_state(self->core->hw->wiphy,false);
	}
	up(&self->early_suspend_lock);
}

static void atbm_enable_early_suspend(struct sbus_priv *self)
{
	sema_init(&self->early_suspend_lock, 1);
	
	down(&self->early_suspend_lock);
	atomic_set(&self->early_suspend_state, 0);	
	self->atbm_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 100;
	self->atbm_early_suspend.suspend = atbm_early_suspend;
	self->atbm_early_suspend.resume = atbm_late_resume;
	register_early_suspend(&self->atbm_early_suspend);
	up(&self->early_suspend_lock);
}

static void atbm_disable_early_suspend(struct sbus_priv *self)
{
	unregister_early_suspend(&self->atbm_early_suspend);
}
#endif
#endif

static int __atbm_usb_probe(struct usb_interface *intf,
				   const struct usb_device_id *id)
{
	struct sbus_priv *self;
	struct dvobj_priv *dvobj;
	int status;
	
	if(atbm_wifi_get_status()){
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "[atbm]Can't probe USB. when wifi_module_exit");
		status = -ETIMEDOUT;
		goto err_exit;
	}
	
	wifi_usb_probe_doing= 1;
	
	atbm_dbg(ATBM_APOLLO_DBG_INIT, "Probe called %d v1\n",intf->minor);

	self = atbm_kzalloc(sizeof(*self), GFP_KERNEL);
	if (!self) {
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Can't allocate USB sbus_priv.");
		status = -ENOMEM;
		goto err_exit;
	}
	mutex_init(&self->sbus_mutex);

	spin_lock_init(&self->lock);
	spin_lock_init(&self->xmit_path_lock);
	self->pdata = atbm_get_platform_data();
	/* 1--- Initialize dvobj_priv */
	dvobj = usb_dvobj_init(intf);
	if (!dvobj){
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Can't allocate USB dvobj.");
		status = -ENOMEM;
		goto err_self;
	}
	
	dvobj->self =self;
	self->drvobj=dvobj;

	/*2---alloc rx_urb*/
#ifdef CONFIG_USE_DMA_ADDR_BUFFER	
	atbm_printk_init("CONFIG_USE_DMA_ADDR_BUFFER TX_BUFFER_SIZE %x\n",TX_BUFFER_SIZE);
	dvobj->tx_save_urb=NULL;
#else
	atbm_printk_init("not CONFIG_USE_DMA_ADDR_BUFFER\n");
#endif //CONFIG_USE_DMA_ADDR_BUFFER
	status = atbm_usb_urb_malloc(self,dvobj->rx_urb,RX_URB_NUM,RX_BUFFER_SIZE, 1,0);
	if (status != 0){
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Can't allocate rx_urb.");
		goto err_dvobj;
	}
	memset(dvobj->rx_urb_map,0,sizeof(dvobj->rx_urb_map));
	/*3---alloc tx_urb*/
	status = atbm_usb_urb_malloc(self,dvobj->tx_urb,TX_URB_NUM,TX_BUFFER_SIZE, 0,1);

	if (status){
		atbm_dbg(ATBM_APOLLO_DBG_ERROR, "Can't allocate tx_urb.");
		goto err_rx_urb;
	}	
	memset(dvobj->tx_urb_map,0,sizeof(dvobj->tx_urb_map));
	atbm_printk_init("CONFIG_TX_NO_CONFIRM\n");

	/*4---alloc cmd_urb*/
	dvobj->cmd_urb=usb_alloc_urb(0,GFP_KERNEL);
	if (!dvobj->cmd_urb){
		goto err_tx_urb;
	}
	/*5---alloc rx data buffer*/
	self->usb_data = atbm_kzalloc(ATBM_USB_EP0_MAX_SIZE+16,GFP_KERNEL);
	if (!self->usb_data){
		status = -ENOMEM;
		goto err_cmd_urb;
	}
	//self->rx_skb = NULL;
//#define ALIGN(a,b)  (((a)+((b)-1))&(~((b)-1)))
	self->usb_req_data = (u8 *)ALIGN((unsigned long)self->usb_data,16);

	init_usb_anchor(&dvobj->tx_submitted);
	init_usb_anchor(&dvobj->rx_submitted);
	init_usb_anchor(&dvobj->xmit_err);
	init_usb_anchor(&dvobj->recv_err);
	init_usb_anchor(&dvobj->comp_err);
#ifdef CONDIF_ATBM_CTRL_REQUEST_ASYNC
	init_usb_anchor(&dvobj->ctrl_submitted);
#endif
	mutex_init(&self->sys_mutex);
	mutex_init(&self->urb_wkq_mutex);
	self->syshold = 0;
	self->tx_hwChanId = 0;
	wifi_tx_urb_pending = 0;
	//
	//usb auto-suspend init
	self->suspend=0;	
	self->auto_suspend=0;	

#ifdef CONFIG_PM
#if USB_AUTO_WAKEUP

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	usb_enable_autosuspend(self->drvobj->pusbdev);
#else
	self->drvobj->pusbdev->autosuspend_disabled = 0;//autosuspend disabled by the user
#endif
#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
	{
		dvobj->pusbdev->do_remote_wakeup=1;
		dvobj->pusbintf->needs_remote_wakeup = 1;
		device_init_wakeup(&dvobj->pusbintf->dev,1);
		pm_runtime_set_autosuspend_delay(&dvobj->pusbdev->dev,15000);
	}
#endif //(LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
#endif //CONFIG_PM

#else //USB_AUTO_WAKEUP
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	usb_disable_autosuspend(self->drvobj->pusbdev);
#else //(LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
	self->drvobj->pusbdev->autosuspend_disabled = 1;//autosuspend disabled by the user
#endif //(LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
	dvobj->pusbdev->do_remote_wakeup=0;
	dvobj->pusbintf->needs_remote_wakeup = 0;
	device_init_wakeup(&dvobj->pusbintf->dev,1);
#endif //(LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))

#endif //#if USB_AUTO_WAKEUP
#endif //CONFIG_PM


	//initial wifi mac
	status = atbm_core_probe(&atbm_usb_sbus_ops,
			      self, &intf->dev, &self->core);

	if (status) {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3, 6, 0))
		status = -EPROBE_DEFER;
#endif
		goto err_probe;
	}else {
		atbm_usb_dvobj_assign_pointer(dvobj);
#if defined(CONFIG_HAS_EARLYSUSPEND)
#ifdef ATBM_PM_USE_EARLYSUSPEND
		atbm_enable_early_suspend(self);
#endif
#endif
		atbm_printk_err("[atbm_wtd]:set wtd_probe = 1\n");
	}
	wifi_usb_probe_doing=0;

	return 0;
err_probe:
#ifdef CONDIF_ATBM_CTRL_REQUEST_ASYNC
	usb_kill_anchored_urbs(&dvobj->ctrl_submitted);
#endif
	mutex_destroy(&self->sys_mutex);
	atbm_usb_dvobj_assign_pointer(NULL);
	atbm_hw_priv_assign_pointer(NULL);
	atbm_kfree(self->usb_data);
err_cmd_urb:
	usb_free_urb(dvobj->cmd_urb);
	dvobj->cmd_urb = NULL;
err_tx_urb:
	atbm_usb_urb_free(self,self->drvobj->tx_urb,TX_URB_NUM);
err_rx_urb:
	atbm_usb_urb_free(self,self->drvobj->rx_urb,RX_URB_NUM);
err_dvobj:
	dvobj->self = NULL;
	self->drvobj = NULL;
	usb_put_dev(dvobj->pusbdev);
	atbm_kfree(dvobj);
err_self:
	atbm_kfree(self);
	usb_set_intfdata(intf, NULL);
	atbm_usb_dvobj_assign_pointer(NULL);
err_exit:
	wifi_usb_probe_doing = 0;

	return status;
}

static int atbm_usb_probe(struct usb_interface *intf,
				   const struct usb_device_id *id)
{
	int ret = 0;
	
	atbm_usb_module_lock();
	ret = __atbm_usb_probe(intf,id);
	atbm_usb_module_unlock();

	return ret;
}
/* Disconnect Function to be called by USB stack when
 * device is disconnected */
static void __atbm_usb_disconnect(struct usb_interface *intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(intf);
	struct sbus_priv *self  = NULL;
	struct usb_device *pdev=NULL;
#ifdef ANDROID
	//mdelay(2000);
#endif
	if (dvobj) {
		self  = dvobj->self;
		pdev = dvobj->pusbdev;
		if (self->core) {
			atbm_printk_exit(" %s %d core %p\n",__func__,__LINE__,self->core);
			atbm_core_release(self->core);
			self->core = NULL;
		}
#if defined(CONFIG_HAS_EARLYSUSPEND)
#ifdef ATBM_PM_USE_EARLYSUSPEND
		atbm_disable_early_suspend(self);
#endif
#endif
#ifdef ANDROID
		//mdelay(2000);
#endif
#if (EXIT_MODULE_RESET_USB==0)
		if(g_rebootSystem)
#endif /*EXIT_MODULE_RESET_USB*/
#if 0
		if (interface_to_usbdev(intf)->state != USB_STATE_NOTATTACHED) 
		{
			atbm_printk_exit("usb_reset_device\n");
			mutex_unlock(&intf->dev.mutex);
			usb_reset_device(interface_to_usbdev(intf));
			mutex_lock(&intf->dev.mutex);
			//usb_reset_device(interface_to_usbdev(intf));
		}
#endif
		self->suspend = 1;
		atbm_usb_urb_free(self,self->drvobj->rx_urb,RX_URB_NUM);
		atbm_usb_urb_free(self,self->drvobj->tx_urb,TX_URB_NUM);
		
		if(pdev->state < USB_STATE_UNAUTHENTICATED){
			atbm_printk_err("usb disconnecting,can not try anything\n");
		}else do{
			char *flush_buff;
			int  flush_len = 0;
			int  status = 0;
			
			atbm_printk_exit("usb:try to flush rx....\n");

			flush_buff = atbm_kzalloc(RX_BUFFER_SIZE,GFP_KERNEL);

			if(flush_buff == NULL){
				break;
			}

			do{
				flush_len = 0;
				
				status = usb_bulk_msg(self->drvobj->pusbdev,
							 usb_rcvbulkpipe(self->drvobj->pusbdev, self->drvobj->ep_in),
							 flush_buff,RX_BUFFER_SIZE,&flush_len,100);
				atbm_printk_err("flush rx[%d][%d]\n",status,flush_len);
				
			}while(flush_len);

			atbm_kfree(flush_buff);
		}while(0);
#ifdef CONDIF_ATBM_CTRL_REQUEST_ASYNC
		usb_kill_anchored_urbs(&dvobj->ctrl_submitted);
#endif
		usb_kill_urb(self->drvobj->cmd_urb);
		usb_free_urb(self->drvobj->cmd_urb);
		usb_set_intfdata(intf, NULL);
		mutex_destroy(&self->sbus_mutex);
		mutex_destroy(&self->sys_mutex);
		mutex_destroy(&self->urb_wkq_mutex);
		atbm_kfree(self->usb_data);
		atbm_kfree(self);
		atbm_kfree(dvobj);
		atbm_usb_dvobj_assign_pointer(NULL);
		atbm_hw_priv_assign_pointer(NULL);
		if(pdev)
		{
			BUG_ON((pdev!=interface_to_usbdev(intf)));
			usb_put_dev(pdev);
		}
		atbm_printk_exit("atbm_usb_disconnect---->oK\n");
	}
}
static void atbm_usb_disconnect_flush(struct atbm_common *hw_priv)
{
	struct sbus_priv *self  = hw_priv->sbus_priv;
	/*
	*flush txrx queue
	*/
	synchronize_net();
	/*
	*cancle all tx urb
	*/
	usb_kill_anchored_urbs(&self->drvobj->tx_submitted);
	/*
	*cancle all rx urb
	*/
	usb_kill_anchored_urbs(&self->drvobj->rx_submitted);
	/*
	*flush urb monitor
	*/
	atbm_usb_flush_urb_work(self);
#ifndef USB_USE_TASTLET_TXRX
	{
		atbm_flush_workqueue(self->tx_workqueue);
		atbm_flush_workqueue(self->rx_workqueue);		
	}
#else
	{
		tasklet_disable(&self->tx_cmp_tasklet);
		tasklet_disable(&self->rx_cmp_tasklet);
		atbm_tx_tasklet((unsigned long)hw_priv);
		atbm_rx_tasklet((unsigned long)hw_priv);
		tasklet_enable(&self->rx_cmp_tasklet);
		tasklet_enable(&self->tx_cmp_tasklet);
	}
#endif
	atbm_destroy_wsm_cmd(hw_priv);
}
static void atbm_usb_prepare_disconnect(struct atbm_common *hw_priv)
{
	int i = 0;
	/*
	*usb has been disconnect,flush all package and cmd
	*/
	if(hw_priv == NULL)
		return;

	atomic_set(&hw_priv->atbm_pluged,0);
	
	atbm_printk_err("%s\n",__func__);
	/*
	*before clear queue ,lock tx
	*/
	wsm_lock_tx_async(hw_priv);
	/*
	*flush out all packets
	*/
	atbm_usb_disconnect_flush(hw_priv);
	/*
	*flush all pkg in the hmac,because we can not receive pkg confirm
	*/
	for (i = 0; i < 4; i++)
		atbm_queue_clear(&hw_priv->tx_queue[i], ATBM_WIFI_ALL_IFS);
	/*
	*atbm_wait_scan_complete_sync must be called before wsm_wait_pm_sync.
	*wait scan complete,maybe wait scan cmd confirm,or scan complicaton
	*/

	//end the atbm_iw test and release the lock when the USB device is removed
	if(hw_priv->etf_test_v2)
	{
		hw_priv->bStartTx = 0;
		hw_priv->bStartTxWantCancel  = 0;
		hw_priv->etf_test_v2 = 0;
		//call etf_v2_scan_end function when the USB device is removed
		atbm_hw_priv_queue_work(hw_priv, &hw_priv->etf_tx_end_work);
		//release the lock in wsm_scan function when the USB device is removed
		wsm_oper_unlock(hw_priv);
	}
	
	atbm_wait_scan_complete_sync(hw_priv);
	/*
	*wait pm complete.
	*/
	wsm_wait_pm_sync(hw_priv);
	/*
	*unlock tx
	*/
	wsm_unlock_tx(hw_priv);

}
static void atbm_usb_disconnect(struct usb_interface *intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(intf);
	
	if(dvobj && (interface_to_usbdev(intf)->state < USB_STATE_UNAUTHENTICATED)){
		/*
		*prepare for disconnect: 
		*1.lock tx , flush and clear all queue
		*2.clear pending cmd
		*3.wait scan complete
		*4.wait pm unlock
		*/
		atbm_usb_prepare_disconnect(dvobj->self->core);
	}
	/*
	*call atbm_usb_module_lock to protect other process
	*from getting the hw_priv of NULL;
	*/
	atbm_usb_module_lock();
	__atbm_usb_disconnect(intf);	
	atbm_usb_module_unlock();

	//debug fs, set error flag.
	atbm_wifi_run_status_set(1);
}

int __atbm_usb_suspend(struct sbus_priv *self)
{
	atbm_printk_pm("usb suspend\n");
	self->suspend=1;
	return 0;

}

int __atbm_usb_resume(struct sbus_priv *self)
{
	atbm_printk_pm("usb resume\n");
	self->suspend=0;
	atbm_usb_memcpy_fromio(self,0,NULL,RX_BUFFER_SIZE);
	return 0;
	
}
#ifdef CONFIG_PM
#ifdef ATBM_SUSPEND_REMOVE_INTERFACE
static int __atbm_usb_hw_suspend(struct sbus_priv *self)
{
	int ret = 0;
	struct atbm_common *hw_priv = self->core;
	struct atbm_ep0_cmd{
			u32 cmd_id;
			u32 lmac_seq;
			u32 hmac_seq;
			u32 data[32];
	};
	u32 buf[DOWNLOAD_BLOCK_SIZE/4];
	int tx_size=12;
	struct atbm_ep0_cmd *cmd = (struct atbm_ep0_cmd *)buf;
	u32 addr = hw_priv->wsm_caps.HiHwCnfBufaddr;
	
	cmd->cmd_id=0x17690125;
	cmd->lmac_seq=11;
	cmd->hmac_seq=12;

	ret = atbm_ep0_write(hw_priv,addr,buf, tx_size);
	if (ret < 0) {
		atbm_printk_pm("%s:err\n",__func__);
		goto error;
	}
	ret = atbm_usb_ep0_cmd(hw_priv->sbus_priv);
	if(ret < 0){
		atbm_printk_pm("%s:err\n",__func__);
		goto error;
	}

	ret = atbm_direct_write_reg_32(hw_priv,0x0b000548,1);

	if(ret < 0){
		atbm_printk_pm("%s:0x0b000548 err\n",__func__);
		goto error;
	}
	ret  = 0;
error:
	return ret;
}
static int __atbm_usb_suspend_comm(struct usb_interface *intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(intf);
	struct sbus_priv *self =  dvobj->self;
	struct atbm_common *hw_priv = self->core;
	struct ieee80211_local *local = hw_to_local(hw_priv->hw);
	int ret = 0;
	
	atbm_usb_module_lock_check();

	if(local->wowlan == true){
		return 0;
	}
	wsm_lock_tx_async(hw_priv);
	atbm_wifi_set_status(2);
	/*
	*from now ,rx_urb and tx_urb can not be submitted again until
	*urb unblock.
	*unblock urbs after resume 
	*/
	atbm_usb_block_urbs(self);
	synchronize_rcu();
	ret = atbm_usb_wait_anchor_empty_timeout(self,1000);
	if(ret == 0){
		/*
		*try again
		*/
		ret = atbm_usb_wait_anchor_empty_timeout(self,10000);
		if(ret == 0){
			ret = -EBUSY;
			atbm_printk_pm("%s:wait urb timeout\n",__func__);
			goto urbs_sync_err;
		}
	}	
	atbm_rx_bh_flush(hw_priv);	

	ret = __atbm_usb_hw_suspend(self);

	if(ret != 0){
		atbm_printk_pm("%s:fw prepare suspend err\n",__func__);
		goto urbs_sync_err;
	}
	synchronize_rcu();
	/*
	*waitting all wsm cmd destory
	*/
	atbm_destroy_wsm_cmd(hw_priv);
	highlight_debug("[atbm usb wifi]:suspend done");
	return ret;
urbs_sync_err:
	highlight_debug("[atbm usb wifi]:suspend fail,watting retry");
	atbm_usb_unblock_urbs(self);
	atbm_wifi_set_status(0);
	wsm_unlock_tx(hw_priv);
	atbm_usb_memcpy_fromio(dvobj->self,0,NULL,RX_BUFFER_SIZE);
	return ret;
}
static int __atbm_usb_resume_comm(struct usb_interface *intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(intf);
	struct sbus_priv *self =  dvobj->self;
	struct atbm_common *hw_priv = self->core;
	struct ieee80211_local *local = hw_to_local(hw_priv->hw);
	int ret = -1;
	int i = 0;
	
	atbm_usb_module_lock_check();
	/*
	*hold rtnl_lock,make sure that when down load fw,network layer cant not 
	*send pkg and cmd
	*/
	rtnl_lock();
	/*
	*suspend fail ,can not to do resume.waiting disconnect...
	*/
	if(dvobj->self->suspend==0){
		ret = -1;
		goto wow_resume;
	}
	dvobj->self->suspend = 0;
	smp_mb();
	if(local->wowlan == true){
		atbm_printk_pm("wowlan support\n");
		atbm_usb_memcpy_fromio(dvobj->self,0,NULL,RX_BUFFER_SIZE);
		ret = 0;
		goto wow_resume;
	}
	/*
	*release hw buff
	*/
	hw_priv->wsm_tx_seq = 0;
	hw_priv->buf_id_tx = 0;
	hw_priv->wsm_rx_seq = 0;
	hw_priv->hw_bufs_used = 0;
	hw_priv->buf_id_rx = 0;
	for (i = 0; i < ATBM_WIFI_MAX_VIFS; i++)
		hw_priv->hw_bufs_used_vif[i] = 0;
	
	atomic_set(&hw_priv->atbm_pluged,1);
	/*
	*block usbs at suspend.
	*here unblock urbs for later using
	*/
	atbm_usb_unblock_urbs(self);
	atbm_wifi_set_status(0);
	/*
	*reload firmware and reinit lmac
	*/
	ret = atbm_reinit_firmware(hw_priv);
	
	wsm_unlock_tx(hw_priv);
wow_resume:
	if(ret != 0){
		/*
		*usb resume fail,so disable tx task
		*/
		atomic_set(&hw_priv->atbm_pluged,0);
		/*
		*notify usb kernel we need to reprobe.
		*/
		intf->needs_binding = 1;
		highlight_debug("[atbm usb wifi] resume fail,waitting disconnect.....");
	}else {
		highlight_debug("[atbm usb wifi] resume done");
	}
	rtnl_unlock();	
	return ret;
}
static int atbm_usb_reset_resume(struct usb_interface *intf)
{
	int ret = 0;
	highlight_debug("[atbm usb wifi]:try to reset resume");
	atbm_usb_module_lock();
	if(atbm_hw_priv_dereference()){
		ret = __atbm_usb_resume_comm(intf);
	}
	atbm_usb_module_unlock();
	return ret;
}
#endif
static int atbm_usb_suspend(struct usb_interface *intf,pm_message_t message)
{
#ifndef ATBM_SUSPEND_REMOVE_INTERFACE
	struct dvobj_priv *dvobj = usb_get_intfdata(intf);
	atbm_printk_pm("usb_suspend\n");
	dvobj->self->suspend=1;
	//atbm_usb_suspend_start(dvobj->self);
	//msleep(20);
	//atbm_usb_urb_kill(dvobj->self,dvobj->rx_urb,RX_URB_NUM);
	return 0;
#else
	struct dvobj_priv *dvobj = usb_get_intfdata(intf);
	int ret = 0;
	highlight_debug("[atbm usb wifi]:try to suspend");
	atbm_usb_module_lock();
	if(atbm_hw_priv_dereference()){
		ret = __atbm_usb_suspend_comm(intf);
	}	
	dvobj->self->suspend = (ret==0);
	atbm_usb_module_unlock();
	return ret;
#endif

}
static int atbm_usb_resume(struct usb_interface *intf)
{
#ifndef ATBM_SUSPEND_REMOVE_INTERFACE
	struct dvobj_priv *dvobj = usb_get_intfdata(intf);
	atbm_printk_pm("usb_resume\n");
	dvobj->self->suspend=0;
	atbm_usb_memcpy_fromio(dvobj->self,0,NULL,RX_BUFFER_SIZE);
	return 0;
#else
	int ret = 0;
	highlight_debug("[atbm usb wifi]:try to resume");
	atbm_usb_module_lock();
	if(atbm_hw_priv_dereference()){
		ret = __atbm_usb_resume_comm(intf);
	}
	atbm_usb_module_unlock();
	return ret;
#endif
	
}
#endif
static int atbm_usb_pre_reset(struct usb_interface *intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(intf);
	if(dvobj == NULL)
		return 1;
	if(dvobj->self == NULL)
		return 1;
	if(dvobj->self->core == NULL)
		return 1;
	atbm_printk_init("%s:%d\n",__func__,interface_to_usbdev(intf)->state);
	return 0;
}
static int atbm_usb_post_reset(struct usb_interface *intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(intf);
	atbm_printk_init("%s:%d\n",__func__,interface_to_usbdev(intf)->state);
	if(dvobj == NULL)
		return 1;
	if(dvobj->self == NULL)
		return 1;
	if(dvobj->self->core == NULL)
		return 1;
	return 0;
}

static struct usb_driver apollod_driver = {
	.name		= WIFI_MODDRVNAME,
	.id_table	= atbm_usb_ids,
	.probe		= atbm_usb_probe,
	.disconnect	= atbm_usb_disconnect,
#ifdef CONFIG_PM
	.suspend	= atbm_usb_suspend,
	.resume		= atbm_usb_resume,
#ifdef ATBM_SUSPEND_REMOVE_INTERFACE
	.reset_resume = atbm_usb_reset_resume,
#endif
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0))
	.disable_hub_initiated_lpm = 1,
#endif  //(LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0))
#if USB_AUTO_WAKEUP
	.supports_autosuspend = 1,
#else //USB_AUTO_WAKEUP
	.supports_autosuspend = 0,
	
#endif  //USB_AUTO_WAKEUP
	.pre_reset = atbm_usb_pre_reset,
	.post_reset = atbm_usb_post_reset,
	.soft_unbind = 1,
};
static int atbm_usb_register(void)
{
	struct atbm_usb_driver_ref *driver_ref = &atbm_usb_kref;
	int ret = 0;
	
	kref_init(&driver_ref->ref);
	
	ret = usb_register(&apollod_driver);

	driver_ref->atbm_driver = ret?NULL:&apollod_driver;

	return ret;
}
static void atbm_usb_driver_release(struct kref *kref)
{
	struct atbm_usb_driver_ref *driver_ref = container_of(kref, struct atbm_usb_driver_ref, ref);

	if(driver_ref->atbm_driver== NULL){
		WARN_ON(1);
		return;
	}
	
	usb_deregister(driver_ref->atbm_driver);
	
	driver_ref->atbm_driver = NULL;
}
/*
*return 1 ,driver removed success
*return 0 ,others
*/
static int atbm_usb_deregister(void)
{
	struct atbm_usb_driver_ref *driver_ref = &atbm_usb_kref;	
	return kref_put(&driver_ref->ref, atbm_usb_driver_release);
}
/* Init Module function -> Called by insmod */
static int  atbm_usb_init(void)
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
	if (pdata->power_ctrl) {
		ret = pdata->power_ctrl(pdata, true);
		if (ret)
			goto err_power;
	}
	ret = atbm_usb_register();
	if (ret)
		goto err_reg;

	ret = atbm_usb_on(pdata);
	if (ret)
		goto err_on;

	return 0;

err_on:
	if (pdata->power_ctrl)
		pdata->power_ctrl(pdata, false);
err_power:
	if (pdata->clk_ctrl)
		pdata->clk_ctrl(pdata, false);
err_clk:
	atbm_usb_deregister();
err_reg:
	return ret;
}

/* Called at Driver Unloading */
static void  atbm_usb_exit(void)
{
	struct atbm_platform_data *pdata;
	atbm_printk_exit("atbm_usb_exit+++++++\n");
	pdata = atbm_get_platform_data();
	atbm_usb_deregister();
	atbm_printk_exit("atbm_usb_exit:usb_deregister\n");
	atbm_usb_off(pdata);
	if (pdata->power_ctrl)
		pdata->power_ctrl(pdata, false);
	if (pdata->clk_ctrl)
		pdata->clk_ctrl(pdata, false);
	atbm_printk_exit("atbm_usb_exit---------\n");
}
#ifdef ATBM_USE_SAVED_FW
static struct platform_device *usb_platform_dev = NULL;
#ifdef CONFIG_PM_SLEEP
static int atbm_usb_pm_notifier(struct notifier_block *b, unsigned long pm_event, void *d)
{
	switch (pm_event) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		if(usb_platform_dev)
			atbm_cache_fw_before_suspend(&usb_platform_dev->dev);
		break;

	case PM_POST_RESTORE:
		/* Restore from hibernation failed. We need to clean
		 * up in exactly the same way, so fall through. */
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		
		break;

	case PM_RESTORE_PREPARE:
	default:
		break;
	}

	return NOTIFY_DONE;
}
static struct notifier_block atbm_usb_pm_nb = {
	.notifier_call = atbm_usb_pm_notifier,
	.priority = 0,
};
#endif
static int atbm_usb_platform_probe(struct platform_device *pdev)
{
#ifdef CONFIG_PM_SLEEP
	atbm_printk_init("%s\n",__func__);
	register_pm_notifier(&atbm_usb_pm_nb);
#endif
	return 0;
}
static int atbm_usb_platform_remove(struct platform_device *pdev)
{
#ifdef CONFIG_PM_SLEEP
	atbm_printk_exit("%s\n",__func__);
	unregister_pm_notifier(&atbm_usb_pm_nb);
#endif
	return 0;
}

static struct platform_driver atbm_usb_platform_driver = {
	.probe = atbm_usb_platform_probe,
	.remove = atbm_usb_platform_remove,
	.driver = {
		.name = WIFI_PLFDEVNAME,
	},
};

static void atbm_usb_platform_init(void)
{
	int ret;
	usb_platform_dev = platform_device_alloc(WIFI_PLFDEVNAME,0);

	if(!usb_platform_dev){
		atbm_printk_init( "alloc platform device err\n");
		goto unreg_plate;
	}
	ret = platform_device_add(usb_platform_dev);
	if (ret){
		atbm_printk_init("platform_device_add err\n");
		goto put_dev;
	}
	ret = platform_driver_register(&atbm_usb_platform_driver);
	if (ret)
		goto put_dev;
	return;
put_dev:
	platform_device_put(usb_platform_dev);
unreg_plate:
	usb_platform_dev = NULL;
	return;
}
static void atbm_usb_platform_exit(void)
{
	if(usb_platform_dev){
		platform_driver_unregister(&atbm_usb_platform_driver);
		platform_device_unregister(usb_platform_dev);
		usb_platform_dev = NULL;
	}
}
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0))
static int atbm_process_each_dev(struct usb_device *usb_dev, void *data)
{
	struct usb_interface *intf = NULL;
	int probe_wait = 0;
	bool found = false;
	
	if((usb_dev->descriptor.idVendor != WIFI_USB_VID) || (usb_dev->descriptor.idProduct != WIFI_USB_PID)){
		goto exit;
	}
retry:
	
	intf = usb_find_interface(&apollod_driver,-1);

	if(intf&&(interface_to_usbdev(intf) == usb_dev)){
		struct net_device *dev;

		rtnl_lock();
		for_each_netdev(&init_net, dev){
			struct ieee80211_sub_if_data *sdata;
			
			if (!dev->ieee80211_ptr || !dev->ieee80211_ptr->wiphy)
				continue;
		
			if (dev->ieee80211_ptr->wiphy->privid != mac80211_wiphy_privid)
				continue;

			sdata = IEEE80211_DEV_TO_SUB_IF(dev);

			if(wiphy_dev(sdata->local->hw.wiphy) != &intf->dev){
				continue;
			}
			found = true;
			break;
		}	
		rtnl_unlock();
		
	} 
	
	if(found == false){
		probe_wait ++;		
		if(probe_wait >= 10){
			goto exit;
		}
		schedule_timeout_interruptible(msecs_to_jiffies(300));
		goto retry;
	}
exit:
	return 0;
}

static void abtm_usb_enum_each_interface(void)
{
	usb_for_each_dev(NULL,atbm_process_each_dev);
}
#else
static void abtm_usb_enum_each_interface(void)
{
	
}
#endif
static int __init atbm_usb_module_init(void)
{
	atbm_printk_init("atbm_usb_module_init %x\n",wifi_module_exit);
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
	atbm_init_firmware();
	atbm_usb_module_lock_int();
	atbm_wifi_set_status(0);
	atbm_usb_module_lock();
#ifdef ATBM_USE_SAVED_FW
	atbm_usb_platform_init();
#endif
	atbm_usb_dvobj_assign_pointer(NULL);
	atbm_module_attribute_init();
	atbm_usb_module_unlock();
	atbm_ieee80211_init();
	atbm_usb_init();
#ifdef ANDROID
	register_reboot_notifier(&atbm_reboot_nb);
#endif
	abtm_usb_enum_each_interface();
	return 0;
}
//wifi_usb_probe_doing
//inorder to wait usb probe  function done ,
//when we running in usb_probe function, can't atbm_usb_module_exit
static void atbm_wait_usb_probe_end(void)
{	
	int loop=0;
	while(wifi_usb_probe_doing){
		atbm_printk_exit("atbm_wait_usb_probe_end\n");	
		mdelay(10);
		loop++;
		if(loop>200)
			break;
	}
}
static void atbm_usb_wait_tx_pending_complete(void)
{
	int loop =0;
	atbm_printk_exit("wifi_tx_urb_pending(%d)\n",wifi_tx_urb_pending);
	while(wifi_tx_urb_pending !=0){
		mdelay(10);
		if(loop++ > 100){
			break;
		}
	}	
	atbm_printk_exit("wifi_tx_urb_pending(%d)\n",wifi_tx_urb_pending);
}

static void  atbm_usb_fw_sync(struct atbm_common *hw_priv,struct dvobj_priv *dvobj)
{
	int loop =0;
	u32 regdata = 0;
	atbm_usb_module_lock_check();
	atbm_wifi_set_status(1);
	atbm_usb_wait_tx_pending_complete();
	if((dvobj == NULL) || (hw_priv == NULL)){
		atbm_printk_exit("%s %d \n",__func__,__LINE__);
		goto exit;
	}

	if((dvobj->self == NULL) ||  (dvobj->self->core != hw_priv)){		
		goto exit;
	}
	atbm_wait_scan_complete_sync(hw_priv);
	wsm_wait_pm_sync(hw_priv);
	/*
	*must make sure that there are no pkgs in the lmc.
	*/
	wsm_lock_tx_async(hw_priv);
	wsm_flush_tx(hw_priv);

	#if 0//(PROJ_TYPE>=ARES_B)
	//reboot system not call reset cmd here, will call usb_reset cmd later
	if(g_rebootSystem==0){
		atbm_printk_exit("atbm_usb_ep0_hw_reset_cmd +++\n");
		atbm_usb_ep0_hw_reset_cmd(dvobj->self,HW_RESET_HIF_SYSTEM_CPU,0);
	}
	#else
	atbm_printk_exit("HiHwCnfBufaddr  %x\n",hw_priv->wsm_caps.HiHwCnfBufaddr);
	if((hw_priv->wsm_caps.HiHwCnfBufaddr  & 0xFFF00000) == 0x9000000){
		atbm_printk_exit("atbm_usb_hw_write_port  0x87690121\n");
	
		regdata = 0x87690121;
		atbm_usb_hw_write_port(dvobj->self,hw_priv->wsm_caps.HiHwCnfBufaddr,&regdata,4);
		loop = 0;
		atbm_usb_ep0_cmd(dvobj->self);
		while(regdata != 0x87690122) {						
			mdelay(10);						
			atbm_usb_hw_read_port(dvobj->self,hw_priv->wsm_caps.HiHwCnfBufaddr+4,&regdata,4);
			if(regdata == 0x87690122){
				atbm_printk_exit("wait usb lmac rmmod ok !!!!!!!!\n");
				break;
			}
			if(loop++ > 100){
				atbm_printk_exit("wait usb lmac rmmod fail !!!!!!!!\n");
				break;
			}
		}
	}
	#endif
	wsm_unlock_tx(hw_priv);
exit:
	/*
	*cpu hold
	*/
	
	//atbm_usb_ep0_hw_reset_cmd(hw_priv->sbus_priv, HW_HOLD_CPU, 0);
	if(hw_priv){
		atbm_usb_ep0_hw_reset_cmd(hw_priv->sbus_priv, HW_RESET_HIF, 0);
		/*
		*reset mac
		*/
		{
			u32 val32;
			int ret;
			//close all intr	
			//atbm_direct_write_reg_32(hw_priv, 0x16100008, 0xffffffff);
		
			//reset cpu
			//atbm_direct_read_reg_32(hw_priv, 0x16101000, &val32);
			//val32 |= BIT(8);
			//atbm_direct_write_reg_32(hw_priv, 0x16101000, val32);
			//val32 &= ~BIT(8);
			//atbm_direct_write_reg_32(hw_priv, 0x16101000, val32);
			//atbm_usb_ep0_hw_reset_cmd(hw_priv->sbus_priv, HW_RESET_HIF_SYSTEM_CPU, 0);
			/*reset mac*/
			ret = atbm_direct_read_reg_32(hw_priv, 0x16100074, &val32);
			if (ret < 0)
				atbm_printk_err("read 0x16100074 err\n");
			val32 |= BIT(1);
			ret = atbm_direct_write_reg_32(hw_priv, 0x16100074, val32);
			if (ret < 0)
				atbm_printk_err("write 0x16100074 err\n");

			ret = atbm_direct_read_reg_32(hw_priv, 0x16100074, &val32);
			atbm_printk_init("reset val(%x)\n", val32);
			val32 &= ~BIT(1);
			ret = atbm_direct_write_reg_32(hw_priv, 0x16100074, val32);
			if (ret < 0)
				atbm_printk_err("write 0x16100074 err\n");

			ret = atbm_direct_read_reg_32(hw_priv, 0x16100074, &val32);

			atbm_printk_init("after reset(%x)\n", val32);
		}
	}
	atbm_wifi_set_status(2);
	atbm_printk_exit("atbm_usb_rmmod_sync loop  %d!!!!!!!!\n",loop);
}

static void atbm_usb_rmmod_sync(void)
{
	atbm_usb_module_lock();
	atbm_usb_station_diconnect_sync(atbm_hw_priv_dereference(),ATBM_SYSTEM_RMMOD);	
	atbm_usb_fw_sync(atbm_hw_priv_dereference(),atbm_usb_dvobj_dereference());
	atbm_usb_module_unlock();
}
static void  atbm_usb_module_exit(void)
{
	atbm_printk_exit("atbm_usb_module_exit g_dvobj %p wifi_usb_probe_doing %d\n",g_dvobj ,wifi_usb_probe_doing);
	atbm_wait_usb_probe_end();
	atbm_usb_rmmod_sync();
	atbm_printk_exit("atbm_usb_exit!!!\n");
	atbm_usb_exit();
#ifdef ANDROID
	unregister_reboot_notifier(&atbm_reboot_nb);
#endif
	atbm_ieee80211_exit();
	atbm_release_firmware();
	atbm_usb_module_lock();
	atbm_usb_dvobj_assign_pointer(NULL);
#ifdef ATBM_USE_SAVED_FW
	atbm_usb_platform_exit();
#endif
	atbm_usb_module_unlock();
	atbm_module_attribute_exit();
	atbm_wifi_set_status(0);
	atbm_usb_module_lock_release();
	atbm_wq_exit();
	ieee80211_atbm_mem_exit();
	ieee80211_atbm_skb_exit();
#ifdef CONFIG_ATBM_BLE
#ifdef CONFIG_ATBM_BLE_CDEV
	ieee80211_ble_platform_exit();
#endif
#endif
	atbm_printk_exit("atbm_usb_module_exit--%d\n",wifi_module_exit);
	return ;
}


module_init(atbm_usb_module_init);
module_exit(atbm_usb_module_exit);
