

#include <assert.h>


#include "syscfg/syscfg.h"
#include "sysinit/sysinit.h"
#include "os/os.h"
#include "mem/mem.h"
#include "nimble/ble.h"
#include "nimble/ble_hci_trans.h"
#include "nimble/hci_common.h"
#include "dev_int.h"
#include "hw_defs.h"
#include "transport/ram/ble_hci_ram.h"



#define ACL_BLOCK_SIZE  OS_ALIGN(MYNEWT_VAL(BLE_ACL_BUF_SIZE) \
                                 + BLE_MBUF_MEMBLOCK_OVERHEAD \
                                 + BLE_HCI_DATA_HDR_SZ, OS_ALIGNMENT)


#define HCI_CMD_SHARE_NUM	2
#define HCI_ACL_SHARE_NUM	4

#define HCI_CMD_SHARE_SIZE	256
#define HCI_ACL_SHARE_SIZE	512

struct HCI_CMD_SHARE_PARAM{
	volatile u32 host_cnt;
	volatile u32 ll_cnt;
	volatile u8 hci_cmd[HCI_CMD_SHARE_NUM][HCI_CMD_SHARE_SIZE];
};

struct HCI_ACL_SHARE_PARAM{
	volatile u32 host_cnt;
	volatile u32 ll_cnt;
	volatile u8 hci_acl[HCI_ACL_SHARE_NUM][HCI_ACL_SHARE_SIZE];	
};

struct BLE_TIME_SYNC_PARAM{
	volatile u32 ll_sleep_expiry;
	volatile u32 host_time_base;
	volatile u32 delta_time;
	volatile u32 reserve;
};

struct HCI_CMD_SHARE_PARAM *ble_hci_cmd_h2l_share;
struct HCI_ACL_SHARE_PARAM *ble_hci_acl_h2l_share;

struct HCI_CMD_SHARE_PARAM *ble_hci_cmd_l2h_share;
struct HCI_ACL_SHARE_PARAM *ble_hci_acl_l2h_share;
struct HCI_CMD_SHARE_PARAM *ble_hci_ack_l2h_share;

struct BLE_TIME_SYNC_PARAM *ble_time_sync_share;


static struct os_mempool ble_hci_ram_cmd_pool;
static os_membuf_t ble_hci_ram_cmd_buf[
        OS_MEMPOOL_SIZE(1, BLE_HCI_TRANS_CMD_SZ)
];

static struct os_mempool ble_hci_ram_evt_hi_pool;
static os_membuf_t ble_hci_ram_evt_hi_buf[
    OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_HCI_EVT_HI_BUF_COUNT),
                    MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE))
];

static struct os_mempool ble_hci_ram_evt_lo_pool;
static os_membuf_t ble_hci_ram_evt_lo_buf[
        OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_HCI_EVT_LO_BUF_COUNT),
                        MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE))
];

static os_membuf_t ble_hci_acl_buf[
	OS_MEMPOOL_SIZE(MYNEWT_VAL(BLE_ACL_BUF_COUNT),
                        ACL_BLOCK_SIZE)
];

static struct os_mbuf_pool ble_hci_acl_mbuf_pool;
static struct os_mempool_ext ble_hci_acl_pool;



static ble_hci_trans_rx_cmd_fn *ble_hci_ram_rx_cmd_hs_cb;
static void *ble_hci_ram_rx_cmd_hs_arg;

static ble_hci_trans_rx_cmd_fn *ble_hci_ram_rx_cmd_ll_cb;
static void *ble_hci_ram_rx_cmd_ll_arg;

static ble_hci_trans_rx_acl_fn *ble_hci_ram_rx_acl_hs_cb;
static void *ble_hci_ram_rx_acl_hs_arg;

static ble_hci_trans_rx_acl_fn *ble_hci_ram_rx_acl_ll_cb;
static void *ble_hci_ram_rx_acl_ll_arg;

extern struct os_mbuf *ble_hci_trans_acl_buf_alloc(void);
extern void ble_hci_trans_buf_free(uint8_t *buf);


void wifi_notify_ble_int(void)
{
	uint32_t val;
	val = HW_READ_REG(0x161000cc);
	val |= BIT(15);
	HW_WRITE_REG(0x161000cc, val);
	//printf("%s:%d\n",__FUNCTION__,__LINE__);
}


void ble_hci_evt_check(void)
{
	u8 *data;
	int i, len;
	u8 *evtbuf;

	if((int)(ble_hci_cmd_l2h_share->ll_cnt - ble_hci_cmd_l2h_share->host_cnt) < 0){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);
	}	

	if((int)(ble_hci_cmd_l2h_share->ll_cnt - ble_hci_cmd_l2h_share->host_cnt) > HCI_CMD_SHARE_NUM){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);		
	}

//	printf("cmd l2h ll_cnt:%d, host_cnt:%d\n", 
//			ble_hci_cmd_l2h_share->ll_cnt, ble_hci_cmd_l2h_share->host_cnt);
	if(ble_hci_cmd_l2h_share->ll_cnt == ble_hci_cmd_l2h_share->host_cnt){
		return;
	}

	while((int)(ble_hci_cmd_l2h_share->ll_cnt - ble_hci_cmd_l2h_share->host_cnt) > 0){
		data = &ble_hci_cmd_l2h_share->hci_cmd[ble_hci_cmd_l2h_share->host_cnt%HCI_CMD_SHARE_NUM][0];
		len = data[1] + 2;
		if(len > HCI_CMD_SHARE_SIZE){
			assert(0);
		}
		
		evtbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_LO);
		if(evtbuf == NULL){
			//printf("evtbuf alloc err(%d)\n",__LINE__);
			break;
		}
#if 0		
		printf("host evt rx:");
		for(i=0; i<len; i++){
			printf("%02X ", data[i]);
		}
		printf("\n");
#endif
		memcpy(evtbuf, data, len);
		assert(ble_hci_ram_rx_cmd_hs_cb != NULL);
		ble_hci_ram_rx_cmd_hs_cb(evtbuf, ble_hci_ram_rx_cmd_hs_arg);
		ble_hci_cmd_l2h_share->host_cnt ++;
	}
}

void ble_hci_ack_check(void)
{
	u8 *data;
	int i, len;
	u8 *evtbuf;

	if((int)(ble_hci_ack_l2h_share->ll_cnt - ble_hci_ack_l2h_share->host_cnt) < 0){
		printf("ble_hci_ack_l2h_share cnt err\n");
		assert(0);
	}	

	if((int)(ble_hci_ack_l2h_share->ll_cnt - ble_hci_ack_l2h_share->host_cnt) > HCI_CMD_SHARE_NUM){
		printf("ble_hci_ack_l2h_share cnt err\n");
		assert(0);		
	}

//	printf("cmd l2h ll_cnt:%d, host_cnt:%d\n", 
//			ble_hci_cmd_l2h_share->ll_cnt, ble_hci_cmd_l2h_share->host_cnt);
	if(ble_hci_ack_l2h_share->ll_cnt == ble_hci_ack_l2h_share->host_cnt){
		return;
	}

	while((int)(ble_hci_ack_l2h_share->ll_cnt - ble_hci_ack_l2h_share->host_cnt) > 0){
		data = &ble_hci_ack_l2h_share->hci_cmd[ble_hci_ack_l2h_share->host_cnt%HCI_CMD_SHARE_NUM][0];
		len = data[1] + 2;
		if(len > HCI_CMD_SHARE_SIZE){
			assert(0);
		}
		
		evtbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
		if(evtbuf == NULL){
			//printf("evtbuf alloc err(%d)\n",__LINE__);
			break;
		}
#if 0		
		printf("host evt rx:");
		for(i=0; i<len; i++){
			printf("%02X ", data[i]);
		}
		printf("\n");
#endif
		memcpy(evtbuf, data, len);
		assert(ble_hci_ram_rx_cmd_hs_cb != NULL);
		ble_hci_ram_rx_cmd_hs_cb(evtbuf, ble_hci_ram_rx_cmd_hs_arg);
		ble_hci_ack_l2h_share->host_cnt ++;
	}
	
}


void ble_hci_acl_check(void)
{
	struct os_mbuf *m;
	uint16_t pktlen;
	u8 *data;
	int i;

	if((int)(ble_hci_acl_l2h_share->ll_cnt - ble_hci_acl_l2h_share->host_cnt) < 0){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);
	}
	
	if((int)(ble_hci_acl_l2h_share->ll_cnt - ble_hci_acl_l2h_share->host_cnt) > HCI_ACL_SHARE_NUM){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);		
	}
	
	//printf("acl l2h ll_cnt:%d, host_cnt:%d\n", 
	//		ble_hci_acl_l2h_share->ll_cnt, ble_hci_acl_l2h_share->host_cnt);

	if(ble_hci_acl_l2h_share->ll_cnt == ble_hci_acl_l2h_share->host_cnt){
		return;
	}	
	
	while((int)(ble_hci_acl_l2h_share->ll_cnt - ble_hci_acl_l2h_share->host_cnt) > 0){
		m = ble_hci_trans_acl_buf_alloc();
		if(m){
			data = &ble_hci_acl_l2h_share->hci_acl[ble_hci_acl_l2h_share->host_cnt%HCI_ACL_SHARE_NUM][0];
			pktlen = get_le16(&data[2]);
			//printf("acl l2h pktlen:%d\n", pktlen + BLE_HCI_DATA_HDR_SZ);
#if 0
			for(i=0; i<pktlen+BLE_HCI_DATA_HDR_SZ; i++){
				printf("%02X ", data[i]);
				if(0 == ((i+1)%16)){
					printf("\n");
					printf("data:");
				}
			}
			printf("\n");
#endif

			if (pktlen + BLE_HCI_DATA_HDR_SZ > ACL_BLOCK_SIZE){
				os_mbuf_free_chain(m);
				assert(0);
			}else{
				memcpy(m->om_data, data, pktlen + BLE_HCI_DATA_HDR_SZ);	
				m->om_len = pktlen + BLE_HCI_DATA_HDR_SZ;
				OS_MBUF_PKTLEN(m) = pktlen + BLE_HCI_DATA_HDR_SZ;
#if 0
				printf("acl l2h om:%X, m->om_len:%d\n", m, m->om_len);
				for(i=0; i<m->om_len; i++){
					printf("%02X ", m->om_data[i]);
					if(0 == ((i+1)%16)){
						printf("\n");
						printf("data:");
					}
				}
				printf("\n");
#endif
				assert(ble_hci_ram_rx_acl_hs_cb != NULL);
				ble_hci_ram_rx_acl_hs_cb(m, ble_hci_ram_rx_acl_hs_arg);
			}
			
			ble_hci_acl_l2h_share->host_cnt ++;			
		}else{
			printf("ble acl buff has no mem\n");
			break;
		}
	}
}


void ble_host_hci_handle_isr(void)
{
	uint32_t val;
	
	val = HW_READ_REG(0x161000c4);
	if(val & BIT(15)){
		ble_hci_ack_check();
		ble_hci_evt_check();
		ble_hci_acl_check();
		val = HW_READ_REG(0x161000c8);
		val |= BIT(15);
		HW_WRITE_REG(0x161000c8, val);	
	}
}


void ble_hci_trans_cfg_hs(ble_hci_trans_rx_cmd_fn *cmd_cb,
                     void *cmd_arg,
                     ble_hci_trans_rx_acl_fn *acl_cb,
                     void *acl_arg)
{
    ble_hci_ram_rx_cmd_hs_cb = cmd_cb;
    ble_hci_ram_rx_cmd_hs_arg = cmd_arg;
    ble_hci_ram_rx_acl_hs_cb = acl_cb;
    ble_hci_ram_rx_acl_hs_arg = acl_arg;
}

void ble_hci_trans_cfg_ll(ble_hci_trans_rx_cmd_fn *cmd_cb,
                     void *cmd_arg,
                     ble_hci_trans_rx_acl_fn *acl_cb,
                     void *acl_arg)
{
    ble_hci_ram_rx_cmd_ll_cb = cmd_cb;
    ble_hci_ram_rx_cmd_ll_arg = cmd_arg;
    ble_hci_ram_rx_acl_ll_cb = acl_cb;
    ble_hci_ram_rx_acl_ll_arg = acl_arg;
}

int ble_hci_trans_hs_cmd_tx(uint8_t *cmd)
{
	os_sr_t sr;
	u8 *data;
	int len;
	u8 cnt = 0;
	
	if((int)(ble_hci_cmd_h2l_share->host_cnt - ble_hci_cmd_h2l_share->ll_cnt) < 0){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);
	}

	if((int)(ble_hci_cmd_h2l_share->host_cnt - ble_hci_cmd_h2l_share->ll_cnt) > HCI_CMD_SHARE_NUM){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);		
	}

	while((int)(ble_hci_cmd_h2l_share->host_cnt - ble_hci_cmd_h2l_share->ll_cnt) == HCI_CMD_SHARE_NUM){
		wifi_notify_ble_int();
		hal_sleep(10);
		if(cnt ++ > 5){
			ble_hci_trans_buf_free(cmd);
			printf("ble_hci_cmd_share no free mem\n");
			return -1;			
		}
	}

	data = &ble_hci_cmd_h2l_share->hci_cmd[ble_hci_cmd_h2l_share->host_cnt%HCI_CMD_SHARE_NUM][0];
	len = cmd[2] + BLE_HCI_CMD_HDR_LEN;
	if(len > HCI_CMD_SHARE_SIZE){
		assert(0);
	}
	OS_ENTER_CRITICAL(sr);
	memset(data, 0, HCI_CMD_SHARE_SIZE);
	memcpy(data, cmd, len);
	
#if 0
	printf("host cmd send:");
	for(i=0; i<len; i++){
		printf("%02X ", data[i]);
	}
	printf("\n");
#endif
	
	ble_hci_trans_buf_free(cmd);
	ble_hci_cmd_h2l_share->host_cnt ++;
	wifi_notify_ble_int();
	OS_EXIT_CRITICAL(sr);
	return 0;
}

int ble_hci_trans_ll_evt_tx(uint8_t *hci_ev)
{
	return 0;
}

int ble_hci_trans_hs_acl_tx(struct os_mbuf *om)
{
	os_sr_t sr;
	u8 *data;
	uint16_t rem_tx_len;
	struct os_mbuf *om_next;
	u8 cnt = 0;
	uint16_t handle;
    uint16_t length;
	
	if((int)(ble_hci_acl_h2l_share->host_cnt - ble_hci_acl_h2l_share->ll_cnt) < 0){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);
	}
	
	if((int)(ble_hci_acl_h2l_share->host_cnt - ble_hci_acl_h2l_share->ll_cnt) > HCI_ACL_SHARE_NUM){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);		
	}

	while((int)(ble_hci_acl_h2l_share->host_cnt - ble_hci_acl_h2l_share->ll_cnt) == HCI_ACL_SHARE_NUM){
		wifi_notify_ble_int();
		hal_sleep(10);
		if(cnt ++ > 5){
			os_mbuf_free_chain(om);
			printf("ble_hci_acl_share no free mem\n");
			return -1;			
		}
	}

	data = &ble_hci_acl_h2l_share->hci_acl[ble_hci_acl_h2l_share->host_cnt%HCI_ACL_SHARE_NUM][0];

	om = os_mbuf_trim_front(om);
	//printf("acl tx:%d, om:%X, len:%d\n", __LINE__, om, om->om_len);
	handle = get_le16(om->om_data);
	length = get_le16(om->om_data + 2);
	//printf("acl tx, handle:%d, length:%d\n", handle, length);

	OS_ENTER_CRITICAL(sr);
	rem_tx_len = 0;
	while (om != NULL){
		rem_tx_len += om->om_len;
		if(rem_tx_len > HCI_ACL_SHARE_SIZE){
			os_mbuf_free_chain(om);
			assert(0);
		}
		memcpy(data, om->om_data, om->om_len);
		data += om->om_len;
		om_next = SLIST_NEXT(om, om_next);
		os_mbuf_free(om);
		om = om_next;
	}

#if 0	
	printf("acl h2l rem_tx_len:%d\n", rem_tx_len);
	data = &ble_hci_acl_h2l_share->hci_acl[ble_hci_acl_h2l_share->host_cnt%HCI_ACL_SHARE_NUM][0];
	printf("data:");
	for(i=0;i<rem_tx_len; i++){
		printf("%02X ", data[i]);
		if(0 == ((i+1)%16)){
			printf("\n");
			printf("data:");
		}
	}
	printf("\n");
#endif

	ble_hci_acl_h2l_share->host_cnt ++;
	wifi_notify_ble_int();
	OS_EXIT_CRITICAL(sr);

    return 0;
}

int ble_hci_trans_ll_acl_tx(struct os_mbuf *om)
{
    return 0;
}

uint8_t *ble_hci_trans_buf_alloc(int type)
{
    uint8_t *buf;

    switch (type) {
    case BLE_HCI_TRANS_BUF_CMD:
        buf = os_memblock_get(&ble_hci_ram_cmd_pool);
        break;

    case BLE_HCI_TRANS_BUF_EVT_HI:
        buf = os_memblock_get(&ble_hci_ram_evt_hi_pool);
        if (buf == NULL) {
            /* If no high-priority event buffers remain, try to grab a
             * low-priority one.
             */
            buf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_LO);
        }
        break;

    case BLE_HCI_TRANS_BUF_EVT_LO:
        buf = os_memblock_get(&ble_hci_ram_evt_lo_pool);
        break;

    default:
        assert(0);
        buf = NULL;
    }

    return buf;
}

/**
 * Allocates a buffer (mbuf) for ACL operation.
 *
 * @return                      The allocated buffer on success;
 *                              NULL on buffer exhaustion.
 */
struct os_mbuf *ble_hci_trans_acl_buf_alloc(void)
{
    struct os_mbuf *m;
    uint8_t usrhdr_len;

    usrhdr_len = sizeof(struct ble_mbuf_hdr);
    m = os_mbuf_get_pkthdr(&ble_hci_acl_mbuf_pool, usrhdr_len);
	
    return m;
}


void ble_hci_trans_buf_free(uint8_t *buf)
{
    int rc;

    /* XXX: this may look a bit odd, but the controller uses the command
    * buffer to send back the command complete/status as an immediate
    * response to the command. This was done to insure that the controller
    * could always send back one of these events when a command was received.
    * Thus, we check to see which pool the buffer came from so we can free
    * it to the appropriate pool
    */
    if (os_memblock_from(&ble_hci_ram_evt_hi_pool, buf)) {
        rc = os_memblock_put(&ble_hci_ram_evt_hi_pool, buf);
        assert(rc == 0);
    } else if (os_memblock_from(&ble_hci_ram_evt_lo_pool, buf)) {
        rc = os_memblock_put(&ble_hci_ram_evt_lo_pool, buf);
        assert(rc == 0);
    } else {
        assert(os_memblock_from(&ble_hci_ram_cmd_pool, buf));
        rc = os_memblock_put(&ble_hci_ram_cmd_pool, buf);
        assert(rc == 0);
    }
}

int ble_hci_trans_reset(void)
{
	
}


void ble_hci_ram_init(void)
{
    int rc;
	extern char PASRAM_BLE_SHARE;

    /* Ensure this function only gets called by sysinit. */
    SYSINIT_ASSERT_ACTIVE();

    rc = os_mempool_ext_init(&ble_hci_acl_pool,
                             MYNEWT_VAL(BLE_ACL_BUF_COUNT),
                             ACL_BLOCK_SIZE,
                             ble_hci_acl_buf,
                             "ble_hci_acl_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = os_mbuf_pool_init(&ble_hci_acl_mbuf_pool,
                           &ble_hci_acl_pool.mpe_mp,
                           ACL_BLOCK_SIZE,
                           MYNEWT_VAL(BLE_ACL_BUF_COUNT));
    SYSINIT_PANIC_ASSERT(rc == 0);

    /*
     * Create memory pool of HCI command buffers. NOTE: we currently dont
     * allow this to be configured. The controller will only allow one
     * outstanding command. We decided to keep this a pool in case we allow
     * allow the controller to handle more than one outstanding command.
     */
    rc = os_mempool_init(&ble_hci_ram_cmd_pool,
                         1,
                         BLE_HCI_TRANS_CMD_SZ,
                         ble_hci_ram_cmd_buf,
                         "ble_hci_ram_cmd_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);
	

    rc = os_mempool_init(&ble_hci_ram_evt_hi_pool,
                         MYNEWT_VAL(BLE_HCI_EVT_HI_BUF_COUNT),
                         MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE),
                         ble_hci_ram_evt_hi_buf,
                         "ble_hci_ram_evt_hi_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = os_mempool_init(&ble_hci_ram_evt_lo_pool,
                         MYNEWT_VAL(BLE_HCI_EVT_LO_BUF_COUNT),
                         MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE),
                         ble_hci_ram_evt_lo_buf,
                         "ble_hci_ram_evt_lo_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

	ble_hci_cmd_h2l_share = (u32)&PASRAM_BLE_SHARE;
	ble_hci_cmd_l2h_share = (u8 *)ble_hci_cmd_h2l_share + sizeof(struct HCI_CMD_SHARE_PARAM);
	ble_hci_ack_l2h_share = (u8 *)ble_hci_cmd_l2h_share + sizeof(struct HCI_CMD_SHARE_PARAM);
	ble_hci_acl_h2l_share = (u8 *)ble_hci_ack_l2h_share + sizeof(struct HCI_CMD_SHARE_PARAM);
	ble_hci_acl_l2h_share = (u8 *)ble_hci_acl_h2l_share + sizeof(struct HCI_ACL_SHARE_PARAM);
	ble_time_sync_share = (u8 *)ble_hci_acl_l2h_share + sizeof(struct HCI_ACL_SHARE_PARAM);

	memset((u8 *)ble_hci_cmd_h2l_share, 0, sizeof(struct HCI_CMD_SHARE_PARAM));
	memset((u8 *)ble_hci_cmd_l2h_share, 0, sizeof(struct HCI_CMD_SHARE_PARAM));
	memset((u8 *)ble_hci_ack_l2h_share, 0, sizeof(struct HCI_CMD_SHARE_PARAM));
	memset((u8 *)ble_hci_acl_h2l_share, 0, sizeof(struct HCI_ACL_SHARE_PARAM));
	memset((u8 *)ble_hci_acl_l2h_share, 0, sizeof(struct HCI_ACL_SHARE_PARAM));
	memset((u8 *)ble_time_sync_share, 0, sizeof(struct BLE_TIME_SYNC_PARAM));

	IRQ_RegisterHandler(DEV_INT_NUM_WIFI_AP_CPU, ble_host_hci_handle_isr);
}


