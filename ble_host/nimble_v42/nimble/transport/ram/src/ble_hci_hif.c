

#include <assert.h>


#include "syscfg/syscfg.h"
#include "sysinit/sysinit.h"
#include "os/os.h"
#include "mem/mem.h"
#include "nimble/ble.h"
#include "nimble/ble_hci_trans.h"
#include "nimble/hci_common.h"
#include "ble_hci_ram.h"


#define ACL_BLOCK_SIZE  OS_ALIGN(MYNEWT_VAL(BLE_ACL_BUF_SIZE) \
                                 + BLE_MBUF_MEMBLOCK_OVERHEAD \
                                 + BLE_HCI_DATA_HDR_SZ, OS_ALIGNMENT)

#define BLE_HCI_HIF_EVT_COUNT  \
    (MYNEWT_VAL(BLE_HCI_EVT_HI_BUF_COUNT) + MYNEWT_VAL(BLE_HCI_EVT_LO_BUF_COUNT))


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


static struct {
	STAILQ_HEAD(, ble_hci_hif_pkt) hif_tx_pkt; /* Packet queue to send to UART */	
}ble_hci_hif_state;



static struct os_mempool ble_hci_hif_pkt_pool;
static os_membuf_t ble_hci_hif_pkt_buf[
        OS_MEMPOOL_SIZE(BLE_HCI_HIF_EVT_COUNT + 1 + MYNEWT_VAL(BLE_ACL_BUF_COUNT),
                        sizeof (struct ble_hci_hif_pkt))
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

#ifndef CONFIG_ATBM_BLE_PLATFORM

extern struct atbmwifi_common g_hw_prv;
#endif


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
    struct ble_hci_hif_pkt *pkt;
    os_sr_t sr;
#ifndef CONFIG_ATBM_BLE_PLATFORM
	struct atbmwifi_common *hw_priv = &g_hw_prv;
#endif
    pkt = os_memblock_get(&ble_hci_hif_pkt_pool);
    if (pkt == NULL) {		
        ble_hci_trans_buf_free(cmd);
        return BLE_ERR_MEM_CAPACITY;
    }

    pkt->type = BLE_HCI_HIF_CMD;
    pkt->data = cmd;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&ble_hci_hif_state.hif_tx_pkt, pkt, next);
    OS_EXIT_CRITICAL(sr);
#ifndef CONFIG_ATBM_BLE_PLATFORM
    atbm_bh_schedule_tx(hw_priv);
#else
	atbm_ble_dev_schedule_tx();
#endif
    return BLE_ERR_SUCCESS;

}

int ble_hci_trans_ll_evt_tx(uint8_t *hci_ev)
{
	return 0;
}

int ble_hci_trans_hs_acl_tx(struct os_mbuf *om)
{
    struct ble_hci_hif_pkt *pkt;
    os_sr_t sr;
#ifndef CONFIG_ATBM_BLE_PLATFORM
	struct atbmwifi_common *hw_priv = &g_hw_prv;
#endif
    /* If this packet is zero length, just free it */
    if (OS_MBUF_PKTLEN(om) == 0) {
        os_mbuf_free_chain(om);
        return 0;
    }

    pkt = os_memblock_get(&ble_hci_hif_pkt_pool);
    if (pkt == NULL) {
        os_mbuf_free_chain(om);
        return BLE_ERR_MEM_CAPACITY;
    }

    pkt->type = BLE_HCI_HIF_ACL;
    pkt->data = om;

    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&ble_hci_hif_state.hif_tx_pkt, pkt, next);
    OS_EXIT_CRITICAL(sr);
#ifndef CONFIG_ATBM_BLE_PLATFORM
    atbm_bh_schedule_tx(hw_priv);
#else
	atbm_ble_dev_schedule_tx();
#endif
    return BLE_ERR_SUCCESS;
}

int ble_hci_trans_ll_acl_tx(struct os_mbuf *om)
{
    return BLE_ERR_SUCCESS;
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
	return 0;
}

void ble_hci_ram_init(void)
{
    int rc;

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

    /*
     * Create memory pool of packet list nodes. NOTE: the number of these
     * buffers should be, at least, the total number of event buffers (hi
     * and lo), the number of command buffers (currently 1) and the total
     * number of buffers that the controller could possibly hand to the host.
     */
    rc = os_mempool_init(&ble_hci_hif_pkt_pool,
                         BLE_HCI_HIF_EVT_COUNT + 1 +
                         MYNEWT_VAL(BLE_ACL_BUF_COUNT),
                         sizeof (struct ble_hci_hif_pkt),
                         ble_hci_hif_pkt_buf,
                         "ble_hci_hif_pkt_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);
	
    memset(&ble_hci_hif_state, 0, sizeof (ble_hci_hif_state));
    STAILQ_INIT(&ble_hci_hif_state.hif_tx_pkt);
}

struct ble_hci_hif_pkt *ble_hci_trans_tx_pkt_get(void)
{
    struct ble_hci_hif_pkt *pkt;
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);

    pkt = STAILQ_FIRST(&ble_hci_hif_state.hif_tx_pkt);
    if (!pkt) {
        OS_EXIT_CRITICAL(sr);
        return NULL;
    }

    STAILQ_REMOVE(&ble_hci_hif_state.hif_tx_pkt, pkt, ble_hci_hif_pkt, next);

    OS_EXIT_CRITICAL(sr);	

	return pkt;
}

void ble_hci_trans_copy_data(struct ble_hci_hif_pkt *tx_pkt, uint8_t *output, uint32_t *putLen)
{
	uint32_t tx_len=0;
	uint8_t *data;
	struct os_mbuf *om, *om_next;
	
	output[*putLen] = tx_pkt->type;
	(*putLen) ++;
	if(tx_pkt->type == BLE_HCI_HIF_CMD){
		data = (atbm_uint8 *)tx_pkt->data;
		tx_len = data[2] + BLE_HCI_CMD_HDR_LEN;
		memcpy(&output[*putLen], data, tx_len);
		(*putLen) += tx_len;
		ble_hci_trans_buf_free(data);
	}
	else if(tx_pkt->type == BLE_HCI_HIF_ACL){
		om = (struct os_mbuf *)tx_pkt->data;
		om = os_mbuf_trim_front(om);
		tx_len = 0;
		while (om != NULL){
			tx_len += om->om_len;
			if(tx_len > HCI_ACL_SHARE_SIZE){
				os_mbuf_free_chain(om);
				ATBM_BUG_ON(1);
			}
			memcpy(&output[*putLen], om->om_data, om->om_len);
			(*putLen) += om->om_len;
			om_next = SLIST_NEXT(om, om_next);
			os_mbuf_free(om);
			om = om_next;
		}				
	}
	else{
		assert(0);
	}
	os_memblock_put(&ble_hci_hif_pkt_pool, tx_pkt);
}

void _ble_hci_trans_copy_data(struct ble_hci_hif_pkt *tx_pkt, uint8_t *output, uint32_t *putLen)
{
	uint32_t tx_len=0;
	uint8_t *data;
	struct os_mbuf *om, *om_next;
	
	output[*putLen] = tx_pkt->type;
	(*putLen) ++;
	if(tx_pkt->type == BLE_HCI_HIF_CMD){
		data = (atbm_uint8 *)tx_pkt->data;
		tx_len = data[2] + BLE_HCI_CMD_HDR_LEN;
		memcpy(&output[*putLen], data, tx_len);
		(*putLen) += tx_len;
	}
	else if(tx_pkt->type == BLE_HCI_HIF_ACL){
		om = (struct os_mbuf *)tx_pkt->data;
		om = os_mbuf_trim_front(om);
		tx_len = 0;
		while (om != NULL){
			tx_len += om->om_len;
			if(tx_len > HCI_ACL_SHARE_SIZE){
				ATBM_BUG_ON(1);
			}
			memcpy(&output[*putLen], om->om_data, om->om_len);
			(*putLen) += om->om_len;
			om_next = SLIST_NEXT(om, om_next);
			om = om_next;
		}				
	}
	else{
		assert(0);
	}
}
void ble_hci_trans_free_hif_pkt(struct ble_hci_hif_pkt *tx_pkt)
{
	switch(tx_pkt->type){
	case  BLE_HCI_HIF_CMD:
		ble_hci_trans_buf_free(tx_pkt->data);
		break;
	case BLE_HCI_HIF_ACL:
		os_mbuf_free_chain(tx_pkt->data);
		break;
	default:
		assert(0);
	}
	
	os_memblock_put(&ble_hci_hif_pkt_pool, tx_pkt);
}
int ble_hci_trans_hs_rx(uint8_t ack, uint8_t *data, uint16_t data_len)
{
	int len;
	uint8_t *evtbuf = NULL;
	struct os_mbuf *om = NULL;
	int ret = -1;
	
#if 0
	int i;
	iot_printf("hs_rx:");
	for(i=0;i<32;i++){
		iot_printf("%02X ", data[i]);
	}
	iot_printf("\n");
#endif
	switch(data[0]){
		case BLE_HCI_HIF_EVT:
			len = data[2]+BLE_HCI_EVENT_HDR_LEN;
			assert(len <= MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE));
			if(ack){
				evtbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
			}
			else{
				evtbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_LO);
			}
			if(evtbuf == NULL){
				break;
			}
			memcpy(evtbuf, &data[1], len);
			assert(ble_hci_ram_rx_cmd_hs_cb != NULL);
			ble_hci_ram_rx_cmd_hs_cb(evtbuf, ble_hci_ram_rx_cmd_hs_arg);
			ret = 0;
			break;
	
		case BLE_HCI_HIF_ACL:
			len = get_le16(&data[3]) + BLE_HCI_DATA_HDR_SZ;
			assert(len <= ACL_BLOCK_SIZE);
			om = ble_hci_trans_acl_buf_alloc();
			if(om == NULL){
				break;
			}
			memcpy(om->om_data, &data[1], len);	
			om->om_len = len;
			OS_MBUF_PKTLEN(om) = len;
			assert(ble_hci_ram_rx_acl_hs_cb != NULL);
			ble_hci_ram_rx_acl_hs_cb(om, ble_hci_ram_rx_acl_hs_arg);
			ret = 0;
			break;
			
		default:
			break;
	}

	return ret;
}


