#include <net/atbm_mac80211.h>
#include <linux/nl80211.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/freezer.h>
#include <linux/inetdevice.h>
#include <net/net_namespace.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/aio.h>
#include <linux/suspend.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
static void  ieee80211_ble_dump(const char *string,u8 *mem,size_t len);
#ifdef CONFIG_ATBM_SUPPORT_BLUEZ
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#endif
#define IEEE80211_BLE_SKB_HEADNEED	128

static char *ieee80211_ble_commb_ble_alloc_xmit(struct platform_device *pble_dev,size_t len);
static int ieee80211_ble_commb_unsubscribe(struct platform_device *pble_dev);
static int ieee80211_ble_commb_subscribe(struct platform_device *pble_dev,
			int (*recv)(struct platform_device *pble_dev, struct sk_buff *skb));
static int ieee80211_ble_commb_xmit(struct platform_device *pble_dev,u8 *xmit,size_t xmit_len);
static int ieee80211_ble_commb_stop(struct platform_device *pble_dev);
static int ieee80211_ble_commb_start(struct platform_device *pble_dev);

#ifdef CONFIG_ATBM_BLE_CDEV
#include "../dev_ioctl.h"
#include "../apollo.h"
#include "../wsm.h"
#include "../internal_cmd.h"

#define IEEE80211_BLE_IOCTL_DATA_SIZE		2048
struct ieee80211_ble_cdev;
struct ieee8021_ble_operations;

struct ieee80211_ble_file
{
	struct list_head head;
	struct device *dev;
	struct fasync_struct *connect_async;
	struct file *filp;
	struct inode *inode;
	struct ieee8021_ble_operations *ops;
	wait_queue_head_t read_wait;
	bool   exiting;
	bool   flushed;
	int    read_happens;
	struct sk_buff_head recv_queue;
	u8     ioctl_data[IEEE80211_BLE_IOCTL_DATA_SIZE];
};
struct ieee80211_ble_cdev
{
	struct list_head ble_files;
	spinlock_t lock;
	struct platform_device *pdev;
	struct device *ble_device;
	dev_t  ble_devt;
	struct notifier_block pm_notifier;
#ifdef CONFIG_ATBM_SUPPORT_BLUEZ
	/*
	*use for bluez
	*/
	struct hci_dev *hdev;
	struct ieee80211_ble_file hdev_file;
#endif
};

struct ieee8021_ble_operations {
	enum ieee80211_ble_dev_type dev_type;
	size_t  cache_limit;
	/*ops for file read*/
	ssize_t (*read) (struct ieee80211_ble_cdev *, struct ieee80211_ble_file *,
					 struct sk_buff *,char __user *, size_t);
	/*ops for file write or writev*/
	ssize_t (*write) (struct ieee80211_ble_cdev *, struct ieee80211_ble_file *,size_t);
	/*ops for file ioctrl*/
	ssize_t (*ioctl) (struct ieee80211_ble_cdev *, struct ieee80211_ble_file *,unsigned int, unsigned long);
	/*ops for receive*/
	int (*recv)(struct ieee80211_ble_file *,struct sk_buff *);
};
static struct cdev ble_device_cdev;
static struct class *ble_class;
static dev_t ble_devt;

#define ATBM_BLE_DEVICE_MAJOR   100
#define ATBM_BLE_DEVICE_DEV		MKDEV(ATBM_BLE_DEVICE_MAJOR, 0)
#define ATBM_BLE_MAX			1
static void ieee8021_ble_operations_init(struct ieee80211_ble_file *ble_file);

#ifdef CONFIG_ATBM_BLE_ADV_COEXIST

static int atbm_ioctl_ble_adv_coexit_start(struct ieee80211_local* local,u8* data)
{
	struct ioctl_ble_start* ble_start = (struct ioctl_ble_start*)data;
	struct wsm_ble_msg_coex_start ble_coex = { 0 };
	struct atbm_common *hw_priv=local->hw.priv;
	if ((ble_start->ble_adv == 0) && (ble_start->ble_scan == 0)) {
		atbm_printk_err("both adv and scan is close!\n");
		return -1;
	}

	if ((ble_start->ble_scan) && (ble_start->ble_scan_win == 0)) {
		atbm_printk_err("ble scan enable, but scan_win is 0!\n");
		return -1;
	}

	if ((ble_start->ble_adv_chan != 0) && (ble_start->ble_adv_chan >= 37)
		&& (ble_start->ble_adv_chan <= 39)) {
		ble_coex.chan_flag |= BIT(ble_start->ble_adv_chan - 37);
	}

	if ((ble_start->ble_scan_chan != 0) && (ble_start->ble_scan_chan >= 37)
		&& (ble_start->ble_scan_chan <= 39)) {
		ble_coex.chan_flag |= BIT(ble_start->ble_scan_chan - 37 + 3);
	}

	if (ble_start->ble_adv) {
		ble_coex.coex_flag |= BIT(0);
	}

	if (ble_start->ble_scan) {
		ble_coex.coex_flag |= BIT(1);
	}

	ble_coex.interval = ble_start->ble_interval;
	ble_coex.scan_win = ble_start->ble_scan_win;
	ble_coex.ble_id = BLE_MSG_COEXIST_START;
	atbm_printk_init("atbm_ioctl_ble_adv_coexit_start\n");
	return wsm_ble_msg_coexist_start(hw_priv, &ble_coex, 0);
}

static int atbm_ioctl_ble_adv_coexit_stop(struct ieee80211_local* local,u8* data)
{
	struct wsm_ble_msg ble_coex = { 0 };
	struct atbm_common *hw_priv=local->hw.priv;
	ble_coex.ble_id = BLE_MSG_COEXIST_STOP;
	return wsm_ble_msg_coexist_stop(hw_priv, &ble_coex, 0);
}
static int atbm_ioctl_ble_set_adv_data(struct ieee80211_local* local,u8* data)
{
	struct ioctl_ble_adv_data* adv_data = (struct ioctl_ble_adv_data*)data;
	struct wsm_ble_msg_adv_data ble_adv_data = { 0 };
	struct atbm_common *hw_priv=local->hw.priv;
	memcpy(&ble_adv_data.mac[0], adv_data, sizeof(struct ioctl_ble_adv_data));
	ble_adv_data.ble_id = BLE_MSG_SET_ADV_DATA;
	return wsm_ble_msg_set_adv_data(hw_priv, &ble_adv_data, 0);
}

static int atbm_ioctl_ble_adv_resp_start(struct ieee80211_local* local,u8* data)
{
	struct ioctl_ble_adv_resp_start* ble_start = (struct ioctl_ble_adv_resp_start*)data;
	struct wsm_ble_msg_adv_resp_start ble_adv_resp_msg = { 0 };
	struct atbm_common *hw_priv=local->hw.priv;

	ble_adv_resp_msg.interval = ble_start->ble_interval;
	ble_adv_resp_msg.ble_id = BLE_MSG_ADV_RESP_MODE_START;
	return wsm_ble_msg_set_adv_data(hw_priv, (struct wsm_ble_msg_adv_data*)&ble_adv_resp_msg, 0);
}

static int atbm_ioctl_ble_set_resp_data(struct ieee80211_local* local,u8* data)
{
	struct ioctl_ble_resp_data* resp_data = (struct ioctl_ble_resp_data*)data;
	struct wsm_ble_msg_resp_data ble_resp_data = { 0 };
	struct atbm_common *hw_priv=local->hw.priv;
	memcpy(&ble_resp_data.resp_data_len, resp_data, sizeof(struct ioctl_ble_resp_data));
	ble_resp_data.ble_id = BLE_MSG_SET_RESP_DATA;
	return wsm_ble_msg_set_adv_data(hw_priv, (struct wsm_ble_msg_adv_data*)&ble_resp_data, 0);
}

void atbm_ioctl_ble_adv_rpt_async(struct ieee80211_hw *hw,u8 *event_buffer, u16 event_len)
{
	struct sk_buff *skb;
	
	skb = atbm_dev_alloc_skb(sizeof(struct ioctl_status_async));
	if(skb){
		struct ioctl_status_async *async = (struct ioctl_status_async *)skb->data;
		struct ieee80211_ble_status *cb  = IEEE80211_BLE_SKB_CB(skb);
		cb->hw_hdr_size = sizeof(struct wsm_hdr);
		cb->size = event_len;
		BUG_ON(event_len > MAX_SYNC_EVENT_BUFFER_LEN);
		memcpy(async->event_buffer,event_buffer,event_len);
		async->driver_mode = 0;
		async->type        = 0;
		async->list_empty  = 0;
		atbm_skb_put(skb,sizeof(struct ioctl_status_async));
		ieee80211_ble_recv(hw,skb);
	}
}

void atbm_ioctl_ble_conn_rpt_async(struct ieee80211_hw *hw,u8 *event_buffer, u16 event_len)
{
	struct sk_buff *skb;
	
	skb = atbm_dev_alloc_skb(sizeof(struct ioctl_status_async));
	if(skb){
		struct ioctl_status_async *async = (struct ioctl_status_async *)skb->data;
		struct ieee80211_ble_status *cb  = IEEE80211_BLE_SKB_CB(skb);
		cb->hw_hdr_size = sizeof(struct wsm_hdr);
		cb->size = event_len;
		BUG_ON(event_len > MAX_SYNC_EVENT_BUFFER_LEN);
		memcpy(async->event_buffer,event_buffer,event_len);
		async->driver_mode = 0;
		async->type        = 1;
		async->list_empty  = 0;
		atbm_skb_put(skb,sizeof(struct ioctl_status_async));
		
		ieee80211_ble_recv(hw,skb);
	}
	
}

#endif//#ifdefCONFIG_ATBM_BLE_ADV_COEXISTstatic int atbm_ioctl_notify_add(u8 type, u8 driver_mode, u8 *event_buffer, u16 event_len)

static void ieee80211_ble_cdev_lock(struct device *dev)
{
	device_lock(dev);
}

static void ieee80211_ble_cdev_unlock(struct device *dev)
{
	device_unlock(dev);
}
static void ieee80211_ble_cdev_set_priv(struct device *dev,void *priv)
{
	dev_set_drvdata(dev,priv);
}
static void *ieee80211_ble_cdev_get_priv(struct device *dev)
{
	void *priv;
	priv = dev_get_drvdata(dev);

	return priv;
}
static struct ieee80211_ble_cdev *ieee80211_get_ble_dev(struct file *filp)
{
	struct ieee80211_ble_file *ble_file = filp->private_data;
	struct ieee80211_ble_cdev *ble_cdev = NULL;
	
	BUG_ON(ble_file == NULL);
	BUG_ON(ble_file->dev ==  NULL);
	ieee80211_ble_cdev_lock(ble_file->dev);
	
	ble_cdev = ieee80211_ble_cdev_get_priv(ble_file->dev);
	if(ble_cdev == NULL){
		ieee80211_ble_cdev_unlock(ble_file->dev);
		return NULL;
	}
	return ble_cdev;
}
static void ieee80211_put_ble_dev(struct file *filp)
{
	struct ieee80211_ble_file *ble_file = filp->private_data;

	BUG_ON(ble_file == NULL);
	BUG_ON(ble_file->dev ==  NULL);
	
	ieee80211_ble_cdev_unlock(ble_file->dev);
}
static int __match_devt(struct device *dev, const void *data)
{
	const dev_t *devt = data;

	return dev->devt == *devt;
}

struct device *ieee80211_ble_device_find_by_devt(dev_t devt)
{
	struct device *dev;

	dev = class_find_device(ble_class, NULL, &devt, __match_devt);

	return dev;
}

static int ieee80211_ble_file_flush(struct ieee80211_ble_file *ble_file)
{
	unsigned long flags;

	BUG_ON(ble_file == NULL);
	if(ble_file->flushed == false){
		spin_lock_irqsave(&ble_file->recv_queue.lock,flags);
		__atbm_skb_queue_purge(&ble_file->recv_queue);
		spin_unlock_irqrestore(&ble_file->recv_queue.lock,flags);
		list_del(&ble_file->head);
		ble_file->flushed = true;
	}
	return atbm_skb_queue_len(&ble_file->recv_queue);
}

static int ieee80211_ble_ioctl_flush (struct file *filp, fl_owner_t id)
{
	struct ieee80211_ble_cdev *ble_cdev = ieee80211_get_ble_dev(filp);

	atbm_printk_ble("ioctl_flush(%p),%d,[%s]\n",ble_cdev, file_count(filp), current->comm);

	if(ble_cdev == NULL){
		goto exit;
	}
	
	if(file_count(filp) > 1){
		ieee80211_put_ble_dev(filp);
		goto exit;
	}
	
	if(ble_cdev){
		spin_lock_bh(&ble_cdev->lock);
		ieee80211_ble_file_flush((struct ieee80211_ble_file *)filp->private_data);
		spin_unlock_bh(&ble_cdev->lock);
		ieee80211_put_ble_dev(filp);
	}
	
exit:	
	return 0;
}

static ssize_t ieee80211_ble_ioctl_write(struct file *filp, const char __user *buff, size_t len, loff_t *off)
{
	struct ieee80211_ble_cdev *ble_cdev = ieee80211_get_ble_dev(filp);
	struct ieee80211_ble_file *ble_file = filp->private_data;
	
	atbm_printk_ble("ioctl_write(%zu)\n",len);
	
	if(ble_cdev == NULL){
		return -1;
	}

	if(ble_file->ops->write == NULL){
		goto err;
	}
	
	if(len > IEEE80211_BLE_IOCTL_DATA_SIZE){
		atbm_printk_err("ble_ioctl_write len err (%zu)\n",len);
		goto err;
	}
		
	if (0 != copy_from_user(&ble_file->ioctl_data[0], buff, len)) {
		atbm_printk_err("%s: copy_from_user err.\n", __func__);
		goto err;
	}

	len = ble_file->ops->write(ble_cdev,ble_file,len);
	ieee80211_put_ble_dev(filp);
	return len;
err:
	ieee80211_put_ble_dev(filp);
	return -1;
}
static ssize_t ieee80211_ble_ioctl_writev(struct kiocb *iocb
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
		, const struct iovec *iov
		,unsigned long nr_segs, loff_t o
#else
		,struct iov_iter * iov
#endif
		)

{
	struct file *filp = iocb->ki_filp;
	struct ieee80211_ble_cdev *ble_cdev = ieee80211_get_ble_dev(filp);
	struct ieee80211_ble_file *ble_file = filp->private_data;
	size_t len = 0;
	u8 *xmit;
	
	if(ble_cdev == NULL){
		atbm_printk_err("writev: ble_dev err\n");
		return -1;
	}
	
	if(ble_file->ops->write == NULL){
		goto err;
	}
	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	len = iov_length(iov, nr_segs);
#else
	len = iov_iter_count(iov);
#endif
	
	atbm_printk_ble("ioctl_writev(%zu)\n",len);

	if(len > IEEE80211_BLE_IOCTL_DATA_SIZE){
		atbm_printk_err("writev: len err(%zu)\n",len);
		goto err;
	}

	xmit = ble_file->ioctl_data;

	memset(xmit,0,IEEE80211_BLE_IOCTL_DATA_SIZE);
	
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	{
		int i = 0;
		for (i = 0; i < nr_segs; i++) {
			
			if (copy_from_user(xmit, iov[i].iov_base, iov[i].iov_len)){
				atbm_printk_err("writev: copy err\n");
				goto err;
			}
			xmit += iov[i].iov_len;
		}
	}
#else
	if (!copy_from_iter(xmit, len, iov)) {
		atbm_printk_err("writev: copy err\n");
		goto err;
	}
#endif

	len = ble_file->ops->write(ble_cdev,ble_file,len);;
	
	ieee80211_put_ble_dev(filp);
	return len;
err:
	ieee80211_put_ble_dev(filp);
	return -1;	
}
static unsigned int ieee80211_ble_ioctl_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct ieee80211_ble_file *ble_file = filp->private_data;

	poll_wait(filp, &ble_file->read_wait, wait);

	if (!skb_queue_empty(&ble_file->recv_queue))
		return POLLIN | POLLRDNORM;

	return POLLOUT | POLLWRNORM;
}

static int ieee80211_ble_ioctl_open(struct inode *inode, struct file *filp)
{
	struct device *dev = NULL;
	struct ieee80211_ble_file *ble_file;
	struct ieee80211_ble_cdev *ble_cdev;
	
	ble_file = atbm_kzalloc(sizeof(struct ieee80211_ble_file), GFP_KERNEL);

	if(ble_file == NULL){
		atbm_printk_err("ioctl_open malloc err\n");
		goto err;
	}
	
	if (imajor(inode) == MAJOR(ble_devt)){
		dev = ieee80211_ble_device_find_by_devt(inode->i_rdev);
	}
	if (!dev)
		goto err;

	ieee80211_ble_cdev_lock(dev);
	ble_cdev = ieee80211_ble_cdev_get_priv(dev);
	
	if(ble_cdev == NULL){
		atbm_printk_err("ble_cdev is null ,can not open\n");
		goto err_ble;
	}
	
	ble_file->dev   = dev;
	ble_file->filp  = filp;
	ble_file->inode = inode;
	
	atbm_skb_queue_head_init(&ble_file->recv_queue);
	init_waitqueue_head(&ble_file->read_wait);
	spin_lock_bh(&ble_cdev->lock);
	list_add_tail(&ble_file->head,&ble_cdev->ble_files);
	spin_unlock_bh(&ble_cdev->lock);
	
	ieee8021_ble_operations_init(ble_file);
	atbm_printk_always("ioctl_open(%p)(%p)\n",dev,dev_get_drvdata(dev));
	filp->private_data = ble_file;
	ieee80211_ble_cdev_unlock(dev);
	return 0;
err_ble:
	ieee80211_ble_cdev_unlock(dev);
	put_device(dev);
err:
	if(ble_file)
		atbm_kfree(ble_file);
	atbm_printk_err("open err dev(%d)(%d)\n",MAJOR(ble_devt),imajor(inode));
	return -1;
}
static int ieee80211_ble_ioctl_fasync(int fd, struct file *filp, int on)
{
	struct ieee80211_ble_file *ble_file = (struct ieee80211_ble_file *)filp->private_data;

	BUG_ON(ble_file == NULL);
	
	return fasync_helper(fd, filp, on, &ble_file->connect_async);
}

static int ieee80211_ble_ioctl_release(struct inode *inode, struct file *filp)
{
	struct ieee80211_ble_file *ble_file = (struct ieee80211_ble_file *)filp->private_data;
	struct ieee80211_ble_cdev *ble_cdev;
	
	BUG_ON(ble_file->dev == NULL);
	ieee80211_ble_cdev_lock(ble_file->dev);
	ble_cdev = ieee80211_ble_cdev_get_priv(ble_file->dev);
	
	if(ble_cdev){
		spin_lock_bh(&ble_cdev->lock);
		ieee80211_ble_file_flush(ble_file);
		spin_unlock_bh(&ble_cdev->lock);
	}
	atbm_printk_always("ioctl_release(%p)(%p)\n",ble_file->dev,ble_cdev);
	filp->private_data = NULL;
	ieee80211_ble_cdev_unlock(ble_file->dev);
	put_device(ble_file->dev);
	atbm_kfree(ble_file);
	
	return 0;
}
static int ieee80211_ble_file_submit_skb(struct ieee80211_ble_file *ble_file,struct sk_buff *skb)
{
	struct ieee80211_ble_status *cb;
	int ret = -1;
	
	cb = IEEE80211_BLE_SKB_CB(skb);
	cb->nr   = 0;

	if(atbm_skb_queue_len(&ble_file->recv_queue) < ble_file->ops->cache_limit){
		atbm_skb_queue_tail(&ble_file->recv_queue,skb);
		ret = 0;
	}else {
		atbm_printk_always("blerx cache overflow(%zu)(%d)\n",ble_file->ops->cache_limit,atbm_skb_queue_len(&ble_file->recv_queue));
	}

	kill_fasync(&ble_file->connect_async, SIGIO, POLL_IN);
	wake_up_interruptible(&ble_file->read_wait);

	return ret;
}
static int ieee80211_ble_cdev_rx(struct platform_device *pdev, struct sk_buff *skb)
{
	struct ieee80211_ble_cdev *ble_cdev = dev_get_drvdata(&pdev->dev);
	struct ieee80211_ble_file *ble_file = NULL;
	struct ieee80211_ble_file *ble_file_prev = NULL;
	int ret = -1;
	int n_files = 0;
	
	BUG_ON(ble_cdev == NULL);
	
	spin_lock_bh(&ble_cdev->lock);

	/*
	*submit skb to each file 
	*/
	list_for_each_entry(ble_file, &ble_cdev->ble_files, head){
		struct sk_buff *new;
		
		if(ble_file_prev == NULL){
			ble_file_prev = ble_file;
			continue;
		}
		
		new = atbm_skb_copy(skb, GFP_ATOMIC);

		if(new){
			n_files ++ ;
			if(ble_file_prev->ops->recv(ble_file_prev,new) != 0){
				atbm_kfree_skb(new);
			}
		}
		
		ble_file_prev = ble_file;
	}
	
	if(ble_file_prev){
		n_files ++ ;
		ret = ble_file_prev->ops->recv(ble_file_prev,skb);
	}
	
	spin_unlock_bh(&ble_cdev->lock);

	return ret;
}
static ssize_t ieee80211_ble_read(struct file *filp, char __user *buff, size_t len, loff_t *off)
{
	struct ieee80211_ble_file *ble_file = filp->private_data;
	struct ieee80211_ble_cdev *ble_cdev = NULL;
	unsigned long flags;
	struct sk_buff *skb;
	int ret = 0;
	struct sk_buff_head list;
	
	//atbm_printk_ble("ble_read task[%p/%s],len[%zu][%d]\n",current,current->comm,len,ble_file->read_happens);
	
	while (len) {
		struct ieee80211_ble_status *cb;
		
		ble_cdev =  ieee80211_get_ble_dev(filp);
		
		if(ble_cdev == NULL){
			atbm_printk_err("ble_cdev has been flushed\n");
			ret = -1;
			goto err_dev;
		}
		
		if(ble_file->flushed == true){
			atbm_printk_err("ble_file has been flushed\n");
			goto err_file;
		}
		
		__atbm_skb_queue_head_init(&list);
		
		spin_lock_irqsave(&ble_file->recv_queue.lock,flags);

		if (atbm_skb_queue_empty(&ble_file->recv_queue)){
			atbm_printk_err("%s: recv_queue list is empty.\n", __func__);
			goto try_wait;
		}
		atbm_skb_queue_splice_tail_init(&ble_file->recv_queue, &list);
		spin_unlock_irqrestore(&ble_file->recv_queue.lock,flags);

		skb = atbm_skb_peek(&list);

		cb  = IEEE80211_BLE_SKB_CB(skb);

		if(cb->nr == 0){
			struct ioctl_status_async *status;

			status = (struct ioctl_status_async *)skb->data;

			if(atbm_skb_queue_len(&list) >= 2){
				status->list_empty = 0;
			}else {
				status->list_empty = 1;
			}
		}
		
		if(ble_file->ops->read){
			ret = ble_file->ops->read(ble_cdev,ble_file,skb,buff,len);
		}else {
			ret = skb->len;
		}
		
		if(ret >= skb->len){
			__atbm_skb_unlink(skb,&list);
			atbm_kfree_skb(skb);
		}else if(ret > 0){
			cb->nr ++;
			atbm_skb_pull(skb,ret);
		}else {
			atbm_printk_err("%s:copy err\n",__func__);
		}
		spin_lock_irqsave(&ble_file->recv_queue.lock,flags);
		atbm_skb_queue_splice_init(&list,&ble_file->recv_queue);
		spin_unlock_irqrestore(&ble_file->recv_queue.lock,flags);
		ieee80211_put_ble_dev(filp);
		
//		atbm_printk_ble("ble_read finished[%p/%s],len[%d][%d]\n",current,current->comm,ret,ble_file->read_happens);
		ble_file->read_happens ++;
		
		break;
		
try_wait:		
		spin_unlock_irqrestore(&ble_file->recv_queue.lock,flags);
		ieee80211_put_ble_dev(filp);
		
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			atbm_printk_debug("read not need block\n");
			break;
		}

		if(ble_file->exiting == true){
			break;
		}
		//atbm_printk_ble("read waiting\n");
		
		ret = wait_event_interruptible(ble_file->read_wait,
					       !atbm_skb_queue_empty(&ble_file->recv_queue));
		if (ret < 0){
			
			break;
		}
		//atbm_printk_ble("wait_event up(%d)\n",ret);
	}
	
err_dev:
	return ret;
err_file:
	ieee80211_put_ble_dev(filp);
	return -1;
}
int ieee80211_ble_ioctl_start(struct ieee80211_ble_cdev *ble_cdev,u8 *data)
{
#ifdef CONFIG_WIFI_BT_COMB
	atbm_printk_init("atbm_ioctl_ble_start\n");
	return 0;
#else
	return atbm_ioctl_ble_adv_coexit_start(ble_cdev->pdev->dev.platform_data,data);
#endif
}
int ieee80211_ble_ioctl_stop(struct ieee80211_ble_cdev *ble_cdev, u8* data)
{
#ifdef CONFIG_WIFI_BT_COMB
	atbm_printk_init("atbm_ioctl_ble_stop\n");
	return 0;
#else
//ble adv/scan comb
	return atbm_ioctl_ble_adv_coexit_stop(ble_cdev->pdev->dev.platform_data,data);
#endif
}
static long ieee80211_ble_unlock_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ieee80211_ble_cdev *ble_cdev = ieee80211_get_ble_dev(filp);
	struct ieee80211_ble_file *ble_file = filp->private_data;
	int ret = -1;
	
	if(ble_cdev == NULL){
		return -1;
	}
	
	if(ble_file->flushed == true){
		atbm_printk_err("ble_file has been flushed\n");
		goto err;
	}
	
	if(ble_file->ops->ioctl == NULL){
		goto err;
	}

	
	ret = ble_file->ops->ioctl(ble_cdev,ble_file,cmd,arg);
err:
	ieee80211_put_ble_dev(filp);
	return ret;
}
#ifdef CONFIG_ATBM_SUPPORT_BLUEZ
static void ieee80211_ble_bluez_recv(struct hci_dev *hdev,struct sk_buff *skb)
{
	/* Ensure LE is enabled for incoming connections */
	set_bit(HCI_LE_ENABLED, &hdev->dev_flags);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
	hci_recv_frame(hdev,skb);
#else
	skb->dev = (void *)hdev;
	hci_recv_frame(skb);
#endif
}
static int ieee80211_ble_bluez_submit_skb(struct ieee80211_ble_file *ble_file,struct sk_buff *skb)
{
	struct ieee80211_ble_status *cb = IEEE80211_BLE_SKB_CB(skb);
	struct hci_dev *hdev = container_of(ble_file->dev, struct hci_dev, dev);
	struct sk_buff *bt_skb = NULL;
	u8 pkt_type;
	
	atbm_printk_ble("bluezsubmit in (%zu)(%d)(%zu)\n",cb->size,skb->len,cb->nr);
	
	BUG_ON(cb->nr != 0);
	/*
	*remove type,driver_mode,list_empty;
	*/
	atbm_skb_pull(skb,offsetof(struct ioctl_status_async, event_buffer));
	/*
	*remove hw hdr
	*/
	atbm_skb_pull(skb,cb->hw_hdr_size);

	cb->size -= cb->hw_hdr_size;

	atbm_skb_trim(skb, cb->size);

	pkt_type = skb->data[0];

	atbm_skb_pull(skb,1);
	
	atbm_printk_ble("bluezsubmit out(%zu)(%d)(%d)\n",cb->size,skb->len,pkt_type);

	bt_skb = bt_skb_alloc(skb->len,GFP_ATOMIC);

	if(bt_skb == NULL){
		goto exit;
	}

	WARN_ON(skb_copy_bits(skb,0,skb_put(bt_skb,skb->len),skb->len));

	memset(bt_skb->cb,0,sizeof(bt_skb->cb));
	
	bt_cb((bt_skb))->expect = 0;
	bt_cb((bt_skb))->pkt_type = pkt_type;

	
	ieee80211_ble_bluez_recv(hdev,bt_skb);
exit:
	atbm_kfree_skb(skb);
	return 0;
}

static struct ieee8021_ble_operations ieee880211_blez_ops = {
	.dev_type    = BLE_DEV_TYPE_BLUEZ,
	.cache_limit = 256,
	.recv  = ieee80211_ble_bluez_submit_skb,
};

static int ieee80211_ble_bluez_open(struct hci_dev *hdev)
{
	struct ieee80211_ble_cdev *ble_cdev = dev_get_drvdata(&hdev->dev);
	struct ieee80211_ble_file *ble_file;
	
	if(ble_cdev == NULL){
		goto err_cdev;
	}
	
	ble_file = &ble_cdev->hdev_file;
	memset(ble_file,0,sizeof(struct ieee80211_ble_file));
	
	ble_file->dev = &hdev->dev;
	ble_file->ops = &ieee880211_blez_ops;

	atbm_skb_queue_head_init(&ble_file->recv_queue);
	init_waitqueue_head(&ble_file->read_wait);
	
	spin_lock_bh(&ble_cdev->lock);
	list_add_tail(&ble_file->head,&ble_cdev->ble_files);
	spin_unlock_bh(&ble_cdev->lock);

	atbm_printk_ble("bluez open\n");
	
	return 0;
err_cdev:
	return -1;
}

static int ieee80211_ble_bluez_close(struct hci_dev *hdev)
{
	struct ieee80211_ble_cdev *ble_cdev = dev_get_drvdata(&hdev->dev);
	struct ieee80211_ble_file *ble_file;
	
	
	if(ble_cdev ==  NULL){
		goto exit;
	}
	
	ble_file = &ble_cdev->hdev_file;
	
	spin_lock_bh(&ble_cdev->lock);
	ieee80211_ble_file_flush(ble_file);
	spin_unlock_bh(&ble_cdev->lock);
	atbm_printk_ble("bluez close\n");
	return 0;
exit:
	return -1;
}

static int ieee80211_ble_bluez_flush(struct hci_dev *hdev)
{
	struct ieee80211_ble_cdev *ble_cdev = dev_get_drvdata(&hdev->dev);
	struct ieee80211_ble_file *ble_file;
	unsigned long flags;
	
	if(ble_cdev == NULL){
		goto exit;	
	}

	ble_file = &ble_cdev->hdev_file;
	
	spin_lock_irqsave(&ble_file->recv_queue.lock,flags);
	__atbm_skb_queue_purge(&ble_file->recv_queue);
	spin_unlock_irqrestore(&ble_file->recv_queue.lock,flags);
exit:
	return 0;
}

/* Fake a Command Complete event for unsupported commands */
static int fake_command_complete(struct hci_dev *hdev, u16 opcode, u8 status, const u8 *params, u8 params_len)
{
	struct sk_buff *skb;
	u8 *data;
	u8 total_len = 3 + params_len; /* status + ncmd + params */

	skb = bt_skb_alloc(total_len + 3, GFP_ATOMIC); /* +3 for event header */
	if (!skb)
		return -ENOMEM;

	data = skb_put(skb, total_len + 3);

	/* Event header */
	data[0] = 0x0e; /* Command Complete event */
	data[1] = total_len; /* Length */
	data[2] = 1; /* ncmd = 1 */

	/* Opcode (little endian) */
	data[3] = opcode & 0xff;
	data[4] = (opcode >> 8) & 0xff;

	/* Status */
	data[5] = status;

	/* Parameters */
	if (params_len > 0)
		memcpy(&data[6], params, params_len);

	memset(skb->cb, 0, sizeof(skb->cb));
	bt_cb(skb)->pkt_type = 4; /* HCI_EVENT_PKT */

	atbm_printk_ble("Faking Command Complete for opcode 0x%04x\n", opcode);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
	hci_recv_frame(hdev, skb);
#else
	skb->dev = (void *)hdev;
	hci_recv_frame(skb);
#endif

	return 0;
}

/* Check if we need to fake a response for unsupported commands */
static int handle_unsupported_command(struct hci_dev *hdev, struct sk_buff *skb)
{
	u16 opcode;
	u8 accept_list_size;

	if (skb->len < 3)
		return 0; /* Not a valid command, let firmware handle it */

	opcode = skb->data[0] | (skb->data[1] << 8);

	switch (opcode) {
	case 0x200f: /* LE Read Accept List Size (0x08|0x000f) */
		atbm_printk_ble("Intercepting unsupported LE Read Accept List Size\n");
		/* Consume the command SKB */
		kfree_skb(skb);
		/* Send fake response: accept list size = 8 */
		accept_list_size = 8;
		fake_command_complete(hdev, opcode, 0x00, &accept_list_size, 1);
		return 1; /* Command handled */
	}

	return 0; /* Not handled, proceed normally */
}
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3, 4, 0))
static void ieee80211_ble_bluez_destruct(struct hci_dev *hdev)
{
	
}
#endif
static int ieee80211_ble_bluez_send_frame(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0))
			struct hci_dev *hdev,
#endif
			struct sk_buff *skb)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;
#endif
	char *xmit_buff;
	struct ieee80211_ble_cdev *ble_cdev = dev_get_drvdata(&hdev->dev);
	struct platform_device *pdev;
	
	if(ble_cdev == NULL){
		goto exit;
	}

	pdev = ble_cdev->pdev;

	if(pdev == NULL){
		goto exit;
	}
	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	};
	atbm_printk_ble("bluez send(%d)(%d)\n",bt_cb(skb)->pkt_type,skb->len);

	/* Check if this is an unsupported command that we need to fake */
	if (bt_cb(skb)->pkt_type == HCI_COMMAND_PKT) {
		if (handle_unsupported_command(hdev, skb)) {
			/* Command was handled, don't send to firmware */
			return 0;
		}
	}

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);

	xmit_buff = ieee80211_ble_commb_ble_alloc_xmit(ble_cdev->pdev,skb->len);

	if (xmit_buff == NULL) {
		goto exit;
	}
	
	
	WARN_ON(skb_copy_bits(skb,0,xmit_buff,skb->len));

	ieee80211_ble_commb_xmit(pdev, xmit_buff, skb->len);
exit:
	kfree_skb(skb);
	return 0;
}
static int ieee80211_ble_bluez_init(struct ieee80211_ble_cdev *ble_cdev)
{
	struct hci_dev *hdev = NULL;
	
	hdev = hci_alloc_dev();

	if(hdev == NULL){
		goto hdev_err;
	}

	ble_cdev->hdev = hdev;
#if ATBM_USB_BUS
	hdev->type = HCI_USB;
#else
	hdev->bus = HCI_SDIO;
#endif
	SET_HCIDEV_DEV(hdev, &ble_cdev->pdev->dev);
	dev_set_drvdata(&hdev->dev, ble_cdev);

	hdev->open     = ieee80211_ble_bluez_open;
	hdev->close    = ieee80211_ble_bluez_close;
	hdev->flush    = ieee80211_ble_bluez_flush;
	hdev->send     = ieee80211_ble_bluez_send_frame;
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3, 4, 0))
	hdev->destruct = ieee80211_ble_bluez_destruct;
#endif
	if(hci_register_dev(hdev) < 0){
		atbm_printk_err("register hdev err\n");
		goto hdev_err;
	}
	return 0;
hdev_err:
	ble_cdev->hdev = NULL;
	if(hdev)
		hci_free_dev(hdev);
	return -ENOMEM;
}
static int ieee80211_ble_bluez_deinit(struct ieee80211_ble_cdev *ble_cdev)
{
	struct hci_dev *hdev = ble_cdev->hdev;

	if(hdev == NULL){
		goto exit;
	}

	dev_set_drvdata(&hdev->dev, NULL);
	
	ble_cdev->hdev = NULL;
	

	hci_unregister_dev(hdev);

	
	hci_free_dev(hdev);
exit:
	return 0;
}
#endif
static int ieee80211_ble_cdev_init(struct ieee80211_ble_cdev *ble_cdev)
{	
	spin_lock_init(&ble_cdev->lock);
	INIT_LIST_HEAD(&ble_cdev->ble_files);
	return 0;
}

static int ieee80211_ble_cdev_deinit(struct ieee80211_ble_cdev *ble_cdev)
{
	struct ieee80211_ble_file *ble_file;
	spin_lock_bh(&ble_cdev->lock);
	while(!list_empty(&ble_cdev->ble_files)){
		ble_file = list_first_entry(&ble_cdev->ble_files, struct ieee80211_ble_file, head);
		/*
		*del list
		*/
		ieee80211_ble_file_flush(ble_file);	
	}
	spin_unlock_bh(&ble_cdev->lock);
	return 0;
}

static void	ieee80211_ble_file_prepare_skb_destructor(struct sk_buff *skb)
{
	struct ieee80211_ble_status *cb  = IEEE80211_BLE_SKB_CB(skb);
	struct completion	*done  = cb->context2;

	if(done){
		complete(done);
		atbm_printk_err("ble_file skb destructor\n");
	}
}

static void ieee80211_ble_file_prepare_exit(struct ieee80211_ble_cdev *ble_cdev)
{
	struct ieee80211_ble_file *ble_file;
	struct sk_buff *new = NULL;
	struct completion	done;
	
	init_completion(&done);
	
	spin_lock_bh(&ble_cdev->lock);
	
	list_for_each_entry(ble_file, &ble_cdev->ble_files, head){
		
		
		if(ble_file->ops->dev_type != BLE_DEV_TYPE_BLUEDROID){
			continue;
		}

		atbm_printk_ble("ble_file bluedroid exit\n");
		
		new = atbm_dev_alloc_skb(sizeof(struct ioctl_status_async));
		
		if(new){
			int idx = 0;
			struct ioctl_status_async *async = (struct ioctl_status_async *)new->data;
			struct ieee80211_ble_status *cb  = IEEE80211_BLE_SKB_CB(new);

			cb->hw_hdr_size = sizeof(struct wsm_hdr);
			cb->size        = 8;
			cb->context1    = ble_file;
			cb->context2    = &done;
			
			async->event_buffer[idx++] = 0;
			async->event_buffer[idx++] = 0;
			async->event_buffer[idx++] = 0;
			async->event_buffer[idx++] = 0;
			async->event_buffer[idx++] = 0x04;
			async->event_buffer[idx++] = 0x10;
			async->event_buffer[idx++] = 0x01;
			async->event_buffer[idx++] = 0xff;
			async->driver_mode = 10;
			async->type        = 0;
			async->list_empty  = 0;
			ble_file->exiting  = true;
			atbm_skb_put(new,sizeof(struct ioctl_status_async));
			
			new->destructor = ieee80211_ble_file_prepare_skb_destructor;
			
			if(ble_file->ops->recv(ble_file,new) != 0){
				atbm_kfree_skb(new);
			}

			
		}

		break;
	}
	
	if(new == NULL)
		complete(&done);
	
	spin_unlock_bh(&ble_cdev->lock);

	if (!wait_for_completion_timeout(&done, 10*HZ)){
		atbm_printk_err("%s:waiting read timeout\n",__func__);
		ieee80211_ble_cdev_deinit(ble_cdev);
	}
}

static int wakeup_reason_pm_event(struct notifier_block *notifier,
        unsigned long pm_event, void *unused)
{
	struct ieee80211_ble_cdev *ble_cdev;
	int ret = NOTIFY_DONE;

	ble_cdev = container_of(notifier, struct ieee80211_ble_cdev, pm_notifier);
    switch (pm_event) {
    case PM_SUSPEND_PREPARE:
		atbm_printk_err("ble suspend\n");
		ieee80211_ble_file_prepare_exit(ble_cdev);
		ret = NOTIFY_OK;
        break;
	default:
		break;
    }

	return ret;
}

static int atbm_ble_platform_probe(struct platform_device *pdev)
{
	
	struct ieee80211_ble_cdev *ble_cdev;

	ble_cdev = atbm_kzalloc(sizeof(struct ieee80211_ble_cdev),GFP_KERNEL);

	if(ble_cdev == NULL){
		goto err;
	}
		
	if(ieee80211_ble_commb_start(pdev)){
		goto err;
	}
	
	ieee80211_ble_commb_subscribe(pdev, ieee80211_ble_cdev_rx);
	ble_cdev->pdev = pdev;
	ieee80211_ble_cdev_init(ble_cdev);
	dev_set_drvdata(&pdev->dev,ble_cdev);
	ble_cdev->ble_devt = ble_devt;
	ble_cdev->ble_device = device_create(ble_class, &pdev->dev,ble_cdev->ble_devt, ble_cdev, "atbm_ioctl");

	if(ble_cdev->ble_device == NULL){
		goto err;
	}
	ble_cdev->pm_notifier.notifier_call = wakeup_reason_pm_event;
	register_pm_notifier(&ble_cdev->pm_notifier);

	ieee80211_ble_cdev_lock(ble_cdev->ble_device);
	ieee80211_ble_cdev_set_priv(ble_cdev->ble_device,ble_cdev);
	ieee80211_ble_cdev_unlock(ble_cdev->ble_device);
	atbm_printk_always("ble_platform_probe(%p)\n",ble_cdev);
#ifdef CONFIG_ATBM_SUPPORT_BLUEZ
	if(ieee80211_ble_bluez_init(ble_cdev)){
		goto err;
	}
#endif
	return 0;
err:
	if(ble_cdev){
		ieee80211_ble_cdev_deinit(ble_cdev);
		atbm_kfree(ble_cdev);
	}
	ieee80211_ble_commb_unsubscribe(pdev);
	return -1;
}

static int atbm_ble_platform_remove(struct platform_device *pdev)
{
	struct ieee80211_ble_cdev *ble_cdev = dev_get_drvdata(&pdev->dev);

	ieee80211_ble_file_prepare_exit(ble_cdev);
	
	ieee80211_ble_commb_stop(pdev);
	ieee80211_ble_commb_unsubscribe(pdev);

	ieee80211_ble_cdev_lock(ble_cdev->ble_device);
#ifdef CONFIG_ATBM_SUPPORT_BLUEZ
	ieee80211_ble_bluez_deinit(ble_cdev);
#endif
	ieee80211_ble_cdev_set_priv(ble_cdev->ble_device,NULL);
	ieee80211_ble_cdev_deinit(ble_cdev);
	ieee80211_ble_cdev_unlock(ble_cdev->ble_device);
	synchronize_rcu();
	unregister_pm_notifier(&ble_cdev->pm_notifier);
	device_destroy(ble_class,ble_cdev->ble_devt);
	
	dev_set_drvdata(&pdev->dev,NULL);
	atbm_printk_always("ble_platform_remove(%p)\n",ble_cdev);
	atbm_kfree(ble_cdev);
	return 0;
}

static struct platform_driver atbm_ble_platform_driver = {
	.probe = atbm_ble_platform_probe,
	.remove = atbm_ble_platform_remove,
	.driver = {
		.name = "atbm_ble",
	},
};
#ifdef BLUEDROID
ssize_t ieee8021_ble_operations_bluedriod_read (struct ieee80211_ble_cdev *cdev, 
					struct ieee80211_ble_file *ble_file,struct sk_buff *skb,
					char __user *buff, size_t len)
{
	struct ieee80211_ble_status *cb = IEEE80211_BLE_SKB_CB(skb);
	ssize_t ret = 0;
	ssize_t copy_len = 0;

	atbm_printk_ble("read_bluedroid in (%zu)(%d)(%zu)(%zu)\n",cb->size,skb->len,len,cb->nr);
	if(cb->nr == 0){
		/*
		*remove type,driver_mode,list_empty;
		*/
		atbm_skb_pull(skb,offsetof(struct ioctl_status_async, event_buffer));
		/*
		*remove hw hdr
		*/
		atbm_skb_pull(skb,cb->hw_hdr_size);

		cb->size -= cb->hw_hdr_size;

		atbm_skb_trim(skb, cb->size);
	}
	atbm_printk_ble("read_bluedroid out(%zu)(%d)(%zu)\n",cb->size,skb->len,len);
	ieee80211_ble_dump(__func__,skb->data,skb->len);
	copy_len = min(len,(size_t)skb->len);
		
	ret = copy_to_user(buff, skb->data,copy_len);
		
	if(ret == 0){
		ret = copy_len;		
	}else {
		ret = -1;
	}

	return ret;
}
static ssize_t ieee8021_ble_operations_bluedriod_write (struct ieee80211_ble_cdev *cdev, 
					struct ieee80211_ble_file *ble_file,size_t len)
{
	char* xmit_buff;

	xmit_buff = ieee80211_ble_commb_ble_alloc_xmit(cdev->pdev,len);

	if (xmit_buff == NULL) {
		goto err;
	}
	
	atbm_printk_ble("bluedriod_write (%zu)\n",len);
	
	memcpy(xmit_buff, ble_file->ioctl_data, len);

	ieee80211_ble_commb_xmit(cdev->pdev, xmit_buff, len);

	return len;
err:
	return -1;
}
static ssize_t ieee8021_ble_operations_bluedriod_ioctl(struct ieee80211_ble_cdev *ble_cdev, 
					struct ieee80211_ble_file *ble_file,unsigned int cmd, unsigned long arg)
{
	atbm_printk_ble("bluedroid not support ioctl\n");
	return -1;
}
static struct ieee8021_ble_operations ieee880211_ble_ops = {
	.dev_type	 = BLE_DEV_TYPE_BLUEDROID,
	.cache_limit = 128,
	.read  = ieee8021_ble_operations_bluedriod_read,
	.write = ieee8021_ble_operations_bluedriod_write,
	.ioctl = ieee8021_ble_operations_bluedriod_ioctl,
	.recv  = ieee80211_ble_file_submit_skb,
};
#else
ssize_t ieee8021_ble_operations_default_read (struct ieee80211_ble_cdev *cdev, 
					struct ieee80211_ble_file *ble_file,struct sk_buff *skb,
					char __user *buff, size_t len)
{
	ssize_t copy_len = min(len,sizeof(struct ioctl_status_async));
	ssize_t ret;
	struct ieee80211_ble_status *cb = IEEE80211_BLE_SKB_CB(skb);

	BUG_ON(cb->nr != 0);
	
	ret = copy_to_user(buff, skb->data,copy_len);
		
	if(ret == 0){
		ret = copy_len;
	}else {
		ret = -1;
	}

	return ret;	
}
static void ieee80211_ble_ioctl_tx(struct ieee80211_ble_cdev *ble_cdev,uint8_t* buf)
{
#ifdef CONFIG_WIFI_BT_COMB
	char* xmit_buff;
	uint8_t* tx_pkt = &buf[2];
	u16 tx_len = *(u16*)buf;
	xmit_buff = NULL;
	rcu_read_lock();

	xmit_buff = ieee80211_ble_commb_ble_alloc_xmit(ble_cdev->pdev,HCI_ACL_SHARE_SIZE);

	if (xmit_buff == NULL) {
		goto pkt_free;
	}
	memcpy(xmit_buff, tx_pkt, tx_len);
	ieee80211_ble_commb_xmit(ble_cdev->pdev, xmit_buff, tx_len);
pkt_free:
	rcu_read_unlock();
	return;
#else
	atbm_printk_always("unsupport ble mode\n");
#endif //#ifdef CONFIG_WIFI_BT_COMB
}
static ssize_t ieee8021_ble_operations_default_write (struct ieee80211_ble_cdev *cdev, 
					struct ieee80211_ble_file *ble_file,size_t len)
{
	ieee80211_ble_ioctl_tx(cdev,ble_file->ioctl_data);

	return len;
}
static ssize_t ieee8021_ble_operations_default_ioctl(struct ieee80211_ble_cdev *ble_cdev, 
					struct ieee80211_ble_file *ble_file,unsigned int cmd, unsigned long arg)
{
	if (copy_from_user(ble_file->ioctl_data, (struct at_cmd_direct *)arg,IEEE80211_BLE_IOCTL_DATA_SIZE)){
		atbm_printk_err("%s: copy_from_user err.\n", __func__);
		goto err;
	}
	
	switch(cmd){
	case ATBM_BLE_COEXIST_START:
		ieee80211_ble_ioctl_start(ble_cdev,ble_file->ioctl_data);
		break;
	case ATBM_BLE_COEXIST_STOP:
		ieee80211_ble_ioctl_stop(ble_cdev,ble_file->ioctl_data);
		break;
#ifdef CONFIG_ATBM_BLE_ADV_COEXIST
	case ATBM_BLE_SET_ADV_DATA:
		atbm_ioctl_ble_set_adv_data(ble_cdev->pdev->dev.platform_data,ble_file->ioctl_data);		
		break;
	case ATBM_BLE_ADV_RESP_MODE_START:
		atbm_ioctl_ble_adv_resp_start(ble_cdev->pdev->dev.platform_data,ble_file->ioctl_data); 
		break;
	case ATBM_BLE_SET_RESP_DATA:
		atbm_ioctl_ble_set_resp_data(ble_cdev->pdev->dev.platform_data,ble_file->ioctl_data);	
		break;
#endif  //#ifdef CONFIG_ATBM_BLE_ADV_COEXIST
	case ATBM_BLE_HIF_TXDATA:
		ieee80211_ble_ioctl_tx(ble_cdev,ble_file->ioctl_data);
		break;
	default:
		atbm_printk_err("%s cmd %d invalid.\n", __func__, cmd);
		goto err;
	}
	return 0;
err:
	return -1;
}
static struct ieee8021_ble_operations ieee880211_ble_ops = {
	.dev_type    = BLE_DEV_TYPE_NBLE,
	.cache_limit = 32,
	.read  = ieee8021_ble_operations_default_read,
	.write = ieee8021_ble_operations_default_write,
	.ioctl = ieee8021_ble_operations_default_ioctl,
	.recv  = ieee80211_ble_file_submit_skb,
};
#endif
static void ieee8021_ble_operations_init(struct ieee80211_ble_file *ble_file)
{
	ble_file->ops = &ieee880211_ble_ops;
}
static struct file_operations ieee880211_ble_ioctl_fops = {
    .owner 			= THIS_MODULE,
    .open 			= ieee80211_ble_ioctl_open,
    .release 		= ieee80211_ble_ioctl_release,
    .read 			= ieee80211_ble_read,
    .unlocked_ioctl = ieee80211_ble_unlock_ioctl,
    .fasync 		= ieee80211_ble_ioctl_fasync,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0))
	.aio_write 		= ieee80211_ble_ioctl_writev,
#else
	.write_iter 	= ieee80211_ble_ioctl_writev,
#endif
	.write 			= ieee80211_ble_ioctl_write,
	.poll 			= ieee80211_ble_ioctl_poll,
	.flush 			= ieee80211_ble_ioctl_flush,
};

int  ieee80211_ble_platform_init(void)
{
	int ret = -1;

	ret = platform_driver_register(&atbm_ble_platform_driver);
	
	if (ret){
		goto err;
	}
	
	ret = alloc_chrdev_region(&ble_devt, 0, ATBM_BLE_MAX, "atbm_ioctl");
	
	if (ret) {
		atbm_printk_err("Unable to lock minors for atbm_ioctl\n");
		goto error_region;
	}
	
	ble_class = class_create(THIS_MODULE, "atbm_ioctl_class");

	if(ble_class == NULL){
		ret = -1;
		atbm_printk_err("fail to alloc class\n");
		goto error_class;
	}
	
	ble_device_cdev.owner = THIS_MODULE;
	cdev_init(&ble_device_cdev, &ieee880211_ble_ioctl_fops);
	
	ret = cdev_add(&ble_device_cdev, ble_devt, ATBM_BLE_MAX);
	
	if (ret) {
		atbm_printk_err("Unable to get ble dev major %d\n",ble_devt);
		goto error_cdev;
	}
	atbm_printk_always("platform_init(%d)\n",MAJOR(ble_devt));
	return ret;
error_cdev:
	class_destroy(ble_class);
error_class:
	unregister_chrdev_region(ble_devt, ATBM_BLE_MAX);
error_region:
	platform_driver_unregister(&atbm_ble_platform_driver);
err:
	return ret;
}

void  ieee80211_ble_platform_exit(void)
{
	platform_driver_unregister(&atbm_ble_platform_driver);
	class_destroy(ble_class);
	cdev_del(&ble_device_cdev);
	unregister_chrdev_region(ble_devt, ATBM_BLE_MAX);
}
#endif
void ieee80211_ble_dev_recv(struct ieee80211_hw *hw,u8 *event_buffer, u16 event_len)
{
	struct sk_buff *skb;

	skb = atbm_dev_alloc_skb(sizeof(struct ioctl_status_async));

	if(skb){
		struct ioctl_status_async *async = (struct ioctl_status_async *)skb->data;
		struct ieee80211_ble_status *cb  = IEEE80211_BLE_SKB_CB(skb);
		
		cb->hw_hdr_size = sizeof(struct wsm_hdr);
		cb->size = event_len;
		cb->context1 = NULL;
		cb->context2 = NULL;
		
		BUG_ON(event_len > MAX_SYNC_EVENT_BUFFER_LEN);
		memcpy(async->event_buffer,event_buffer,event_len);
		async->driver_mode = 0;
		async->type        = 0;
		async->list_empty  = 0;
		atbm_skb_put(skb,sizeof(struct ioctl_status_async));
		
		ieee80211_ble_recv(hw,skb);
	}
}
static void  ieee80211_ble_dump(const char *string,u8 *mem,size_t len)
{
#if 0
	size_t i = 0;
	atbm_printk_err("[%s]:\n",string);

	for(i = 0; i< len ; i++){
		if(!(i % 16)){
			atbm_printk_err("\n");
		}
		atbm_printk_err("[%x]",mem[i]);
	}
#endif	
}
static int ieee80211_ble_thread_wakeup(struct ieee80211_ble_thread *thread)
{

	void *bh;
	rcu_read_lock();
	if(test_and_set_bit(THREAD_ACTION_WAKEUP, &thread->flags) == 0){
		bh = rcu_dereference(thread->thread);
		if(bh){			
			wake_up_process((struct task_struct *)bh);
		}
	}
	rcu_read_unlock();
	return 0;
}

static int ieee80211_ble_thread_deinit(struct ieee80211_ble_thread *thread)
{
	void *bh;
	struct ieee80211_local *local = thread->local;
	struct ieee80211_ble_local *ble_local = &local->ble_local;
	
	set_bit(THREAD_ACTION_SHOULD_STOP,&thread->flags);
	spin_lock_bh(&ble_local->ble_spin_lock);
	bh = rcu_dereference(thread->thread);
	rcu_assign_pointer(thread->thread,NULL);
	spin_unlock_bh(&ble_local->ble_spin_lock);
	if (bh){
		synchronize_rcu();
		kthread_stop(bh);
	}

	return 0;
}

static int ieee80211_ble_kthread_should_stop(struct ieee80211_ble_thread *thread)
{
	if(!kthread_should_stop()){
		return 0;
	}
	
	set_bit(THREAD_ACTION_SHOULD_STOP,&thread->flags);
	if(test_bit(THREAD_ACTION_SHOULD_SUSPEND, &thread->flags)) {
		if (!test_and_set_bit(THREAD_ACTION_SUSPENED, &thread->flags))
			complete(&thread->suspended);
	}

	return 1;
}
static void ieee80211_ble_schedule_timeout(struct ieee80211_ble_thread *thread)
{
	signed long timeout = schedule_timeout(thread->wakeup_period);

	if (timeout == 0 && thread->period_handle){
		thread->period_handle(thread);
	}
}

static int ieee80211_ble_wait_action(struct ieee80211_ble_thread *thread)
{
	set_current_state(TASK_INTERRUPTIBLE);	
	while (!ieee80211_ble_kthread_should_stop(thread)) {
		if (test_and_clear_bit(THREAD_ACTION_WAKEUP,
				       &thread->flags)) {
			__set_current_state(TASK_RUNNING);
			return 0;
		}
		if (!ieee80211_ble_kthread_should_stop(thread))
			ieee80211_ble_schedule_timeout(thread);
		set_current_state(TASK_INTERRUPTIBLE);
		
	}
	__set_current_state(TASK_RUNNING);
	return -1;
}

static int ieee80211_ble_thread_process(void *val)
{
	struct ieee80211_ble_thread *thread = (struct ieee80211_ble_thread *)val;
	atbm_printk_init("[%s] start\n",thread->name);
	while(!ieee80211_ble_wait_action(thread)){
		thread->thread_fn(thread);
	}
	atbm_printk_init("[%s] stop\n",thread->name);
	return 0;
}

static int ieee80211_ble_thread_init(struct ieee80211_ble_thread *thread)
{	
	thread->thread = kthread_create(ieee80211_ble_thread_process,thread, thread->name);
	
	if (IS_ERR(thread->thread)){
		thread->thread = NULL;
		atbm_printk_err("sdio %s err\n",thread->name);
		return -1;
	}
	init_completion(&thread->suspended);
	return  0;
}
static int ieee80211_ble_xmit_thread(struct ieee80211_ble_thread *thread)
{
	struct sk_buff *skb;
	struct ieee80211_local *local = thread->local;
	struct ieee80211_ble_local *ble_local = &local->ble_local;

	while((skb  =  atbm_skb_dequeue(&ble_local->ble_xmit_queue))){
		
		/*
		*start tx
		*/
		BUG_ON(local->ops->do_ble_xmit == NULL);
		//printk("[ble xmit]:len [%d]\n",skb->len);
		ieee80211_ble_dump(__func__,skb->data,skb->len);
		local->ops->do_ble_xmit(&local->hw,skb->data,skb->len);
		/*
		*free skb
		*/
		atbm_dev_kfree_skb(skb);
	}

	return 0;
}
static int ieee80211_ble_xmit_init(struct ieee80211_local *local)
{
	struct ieee80211_ble_local *ble_local = &local->ble_local;
	struct ieee80211_ble_thread *thread = &ble_local->xmit_thread;
	
	atbm_skb_queue_head_init(&ble_local->ble_xmit_queue);

	thread->flags = 0;
	thread->name  = ieee80211_alloc_name(&local->hw,"ble_xmit");
	thread->period_handle = NULL;
	thread->thread_fn = ieee80211_ble_xmit_thread;
	thread->local = local;
	thread->wakeup_period = MAX_SCHEDULE_TIMEOUT;

	if(ieee80211_ble_thread_init(thread)){
		atbm_printk_err("ble_xmit thread err\n");
		return -1;
	}
	ieee80211_ble_thread_wakeup(thread);
	return 0;
}
void  ieee80211_ble_recv(struct ieee80211_hw *hw,struct sk_buff *skb)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_ble_local *ble_local = &local->ble_local;

	spin_lock_bh(&ble_local->ble_spin_lock);
	
	if(ble_local->ble_started == true){
		atbm_skb_queue_tail(&ble_local->ble_recv_queue,skb);
		ieee80211_ble_thread_wakeup(&ble_local->recv_thread);
	}else {
		atbm_dev_kfree_skb(skb);
	}
	
	spin_unlock_bh(&ble_local->ble_spin_lock);
}
static int ieee80211_ble_recv_thread(struct ieee80211_ble_thread *thread)
{
	struct sk_buff *skb;
	struct ieee80211_local *local = thread->local;
	struct ieee80211_ble_local *ble_local = &local->ble_local;
	int  (*ble_cb)(struct platform_device *pble_dev, struct sk_buff *skb);
	
	mutex_lock(&ble_local->ble_mutex_lock);

	ble_cb = rcu_dereference(ble_local->ble_recv_callback);
	
	while((skb  =  atbm_skb_dequeue(&ble_local->ble_recv_queue))){
		int ret = -1;
		atbm_printk_debug("%s:ble(%d)\n",__func__,skb->len);
		if(ble_cb) 
			ret = ble_cb(&local->ble_dev,skb);
		if(ret)
			atbm_dev_kfree_skb(skb);
	}
	
	mutex_unlock(&ble_local->ble_mutex_lock);

	return 0;
}
static int ieee80211_ble_recv_init(struct ieee80211_local *local)
{
	struct ieee80211_ble_local *ble_local = &local->ble_local;
	struct ieee80211_ble_thread *thread = &ble_local->recv_thread;
	
	atbm_skb_queue_head_init(&ble_local->ble_recv_queue);

	thread->flags = 0;
	thread->name  = ieee80211_alloc_name(&local->hw,"ble_recv");
	thread->period_handle = NULL;
	thread->thread_fn = ieee80211_ble_recv_thread;
	thread->local = local;
	thread->wakeup_period = MAX_SCHEDULE_TIMEOUT;

	if(ieee80211_ble_thread_init(thread)){
		atbm_printk_err("ble_recv thread err\n");
		return -1;
	}
	ieee80211_ble_thread_wakeup(thread);
	return 0;
}
static int ieee80211_ble_xmit_exit(struct ieee80211_local *local)
{
	struct ieee80211_ble_local *ble_local = &local->ble_local;
	struct ieee80211_ble_thread *thread = &ble_local->xmit_thread;
	
	ieee80211_ble_thread_deinit(thread);

	atbm_skb_queue_purge(&ble_local->ble_xmit_queue);
	return  0;
}

static int ieee80211_ble_recv_exit(struct ieee80211_local *local)
{
	struct ieee80211_ble_local *ble_local = &local->ble_local;
	struct ieee80211_ble_thread *thread = &ble_local->recv_thread;
	
	ieee80211_ble_thread_deinit(thread);

	atbm_skb_queue_purge(&ble_local->ble_recv_queue);
	return  0;
}


static struct ieee80211_local *ble_to_local(struct platform_device *pble_dev) 
{
	return container_of(pble_dev, struct ieee80211_local, ble_dev);
}
static int ieee80211_ble_commb_start(struct platform_device *pble_dev)
{
	struct ieee80211_local *local = ble_to_local(pble_dev);
	struct ieee80211_ble_local *ble_local = &local->ble_local;

	atbm_printk_init("ble start\n");
	if(ieee80211_ble_recv_init(local)){
		goto fail_recv;
	}

	if(ieee80211_ble_xmit_init(local)){
		goto fail_xmit;
	}
	/*
	*start sucess
	*/
	spin_lock_bh(&ble_local->ble_spin_lock);
	ble_local->ble_started = true;
	spin_unlock_bh(&ble_local->ble_spin_lock);
	return 0;
fail_xmit:
	ieee80211_ble_recv_exit(local);
fail_recv:
	atbm_printk_init("ble start err\n");
	return -1;

}

static int ieee80211_ble_commb_stop(struct platform_device *pble_dev)
{
	struct ieee80211_local *local = ble_to_local(pble_dev);
	struct ieee80211_ble_local *ble_local = &local->ble_local;

	spin_lock_bh(&ble_local->ble_spin_lock);
	ble_local->ble_started = false;
	spin_unlock_bh(&ble_local->ble_spin_lock);
	
	synchronize_rcu();
	
	ieee80211_ble_xmit_exit(local);
	ieee80211_ble_recv_exit(local);
	atbm_printk_init("ble stop\n");
	return 0;
}

static int ieee80211_ble_commb_xmit(struct platform_device *pble_dev,u8 *xmit,size_t xmit_len)
{
	struct ieee80211_local *local = ble_to_local(pble_dev);
	struct ieee80211_ble_buff *ble_buff;
	struct ieee80211_ble_local *ble_local = &local->ble_local;
	
	struct sk_buff *skb;
	ieee80211_ble_dump(__func__,xmit,xmit_len);
	ble_buff = container_of((void *)xmit, struct ieee80211_ble_buff, mem);

	skb = ble_buff->skb;

	BUG_ON((skb->data + IEEE80211_BLE_SKB_HEADNEED) != (u8*)ble_buff);
	
	atbm_skb_reserve(skb, IEEE80211_BLE_SKB_HEADNEED+sizeof(struct ieee80211_ble_buff));
	atbm_skb_put(skb,xmit_len);
	
	spin_lock_bh(&ble_local->ble_spin_lock);
	
	if(ble_local->ble_started == true){
		atbm_printk_debug("[%s]:len [%d]\n",__func__,skb->len);
		atbm_skb_queue_tail(&ble_local->ble_xmit_queue,skb);
		ieee80211_ble_thread_wakeup(&ble_local->xmit_thread);
	}else {
		atbm_dev_kfree_skb(skb);
	}
	
	spin_unlock_bh(&ble_local->ble_spin_lock);
	return 0;
}

static int ieee80211_ble_commb_subscribe(struct platform_device *pble_dev,
			int (*recv)(struct platform_device *pble_dev, struct sk_buff *skb))
{
	struct ieee80211_local *local = ble_to_local(pble_dev);
	struct ieee80211_ble_local *ble_local = &local->ble_local;

	atbm_printk_init("ble subscribe\n");
	mutex_lock(&ble_local->ble_mutex_lock);
	rcu_assign_pointer(ble_local->ble_recv_callback,recv);
	mutex_unlock(&ble_local->ble_mutex_lock);
	
	return 0;
}
static int ieee80211_ble_commb_unsubscribe(struct platform_device *pble_dev)
{
	struct ieee80211_local *local = ble_to_local(pble_dev);
	struct ieee80211_ble_local *ble_local = &local->ble_local;
	atbm_printk_init("ble unsubscribe\n");
	mutex_lock(&ble_local->ble_mutex_lock);
	rcu_assign_pointer(ble_local->ble_recv_callback,NULL);
	mutex_unlock(&ble_local->ble_mutex_lock);

	synchronize_rcu();
	return 0;
}
static char *ieee80211_ble_commb_ble_alloc_xmit(struct platform_device *pble_dev,size_t len)
{
	struct sk_buff *skb;
	struct  ieee80211_ble_buff *ble_buff;
	
	skb = atbm_dev_alloc_skb(len +  IEEE80211_BLE_SKB_HEADNEED + sizeof(struct  ieee80211_ble_buff));

	if(skb == NULL){
		return  NULL;
	}

	ble_buff = (struct  ieee80211_ble_buff *)(skb->data + IEEE80211_BLE_SKB_HEADNEED);
	ble_buff->skb = skb;

	return (char *)ble_buff->mem;
}
static void ieee80211_ble_device_release(struct device *dev)
{
	atbm_printk_exit("ble_device_release\n");
}
int ieee80211_ble_dev_int(struct ieee80211_local *local)
{
	struct ieee80211_ble_local *ble_local = &local->ble_local;
	
	struct platform_device *pble_dev = &local->ble_dev;

	pble_dev->name = "atbm_ble";
	pble_dev->id   = 0;
	pble_dev->dev.platform_data = local;
	pble_dev->dev.release = ieee80211_ble_device_release;
	
	ble_local->ble_recv_callback = 0;
	atbm_printk_err("ble_spin_lock init \n");
	spin_lock_init(&ble_local->ble_spin_lock);
	mutex_init(&ble_local->ble_mutex_lock);
	return 0;
	
}
int ieee80211_ble_dev_register(struct ieee80211_local *local)
{	
	struct atbm_common *hw_priv=local->hw.priv;
	if(hw_priv->loader_ble == 1){
		atbm_printk_err("ieee80211_ble_dev_register\n");
		if(platform_device_register(&local->ble_dev)){
			goto fail_dev;
		}
	}
	return 0;
fail_dev:
	return -1;
}
void ieee80211_ble_dev_deregister(struct ieee80211_local *local)
	{
	struct ieee80211_ble_local *ble_local = &local->ble_local;
	struct atbm_common *hw_priv=local->hw.priv;
	if(hw_priv->loader_ble == 1){
		atbm_printk_err("ieee80211_ble_dev_deregister\n");
		platform_device_unregister(&local->ble_dev);
		
	}
	mutex_destroy(&ble_local->ble_mutex_lock);
}


int atbm_ble_init(struct ieee80211_local *local)
{
	int result;
	struct atbm_common *hw_priv=local->hw.priv;
	if(hw_priv->loader_ble == 1){

#ifdef CONFIG_ATBM_BLE
#ifdef CONFIG_ATBM_BLE_CDEV
		if(ieee80211_ble_platform_init()){
			return -1;
		}
#endif
#endif

#ifdef CONFIG_ATBM_BLE	
		ieee80211_ble_dev_int(local);
#endif

		
#ifdef CONFIG_ATBM_BLE	

		result = ieee80211_ble_dev_register(local);
		if(result){
			atbm_ble_exit(local);
			atbm_printk_err("ble_dev register err\n");
			return -2;
		}
#endif
	}
	return 0;

}


int atbm_ble_exit(struct ieee80211_local *local)
{
	struct atbm_common *hw_priv=local->hw.priv;
	atbm_printk_err("atbm_ble_exit ++++++++++ \n");
	if(hw_priv->loader_ble == 1){
		
#ifdef CONFIG_ATBM_BLE
	ieee80211_ble_dev_deregister(local);
#endif


#ifdef CONFIG_ATBM_BLE
#ifdef CONFIG_ATBM_BLE_CDEV
	ieee80211_ble_platform_exit();
#endif
#endif
	}
	atbm_printk_err("atbm_ble_exit --------- \n");
	return 0;
}






