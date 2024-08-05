

#include <assert.h>


#include "syscfg/syscfg.h"
#include "sysinit/sysinit.h"
#include "os/os.h"
#include "mem/mem.h"
#include "nimble/ble.h"
#include "nimble/ble_hci_trans.h"
#include "nimble/hci_common.h"
#include "ble_hci_ioctl.h"



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


static struct
{
    STAILQ_HEAD(, ble_hci_hif_pkt) hif_tx_pkt; /* Packet queue to send to UART */
}ble_hci_hif_state;



static struct os_mempool ble_hci_hif_pkt_pool;
static os_membuf_t ble_hci_hif_pkt_buf[
    OS_MEMPOOL_SIZE(BLE_HCI_HIF_EVT_COUNT + 1 + MYNEWT_VAL(BLE_ACL_BUF_COUNT),
        sizeof(struct ble_hci_hif_pkt))
];

static struct os_mbuf_pool ble_hci_acl_mbuf_pool;
static struct os_mempool_ext ble_hci_acl_pool;


static ble_hci_trans_rx_cmd_fn* ble_hci_ram_rx_cmd_hs_cb;
static void* ble_hci_ram_rx_cmd_hs_arg;

static ble_hci_trans_rx_cmd_fn* ble_hci_ram_rx_cmd_ll_cb;
static void* ble_hci_ram_rx_cmd_ll_arg;

static ble_hci_trans_rx_acl_fn* ble_hci_ram_rx_acl_hs_cb;
static void* ble_hci_ram_rx_acl_hs_arg;

static ble_hci_trans_rx_acl_fn* ble_hci_ram_rx_acl_ll_cb;
static void* ble_hci_ram_rx_acl_ll_arg;
void atbm_ble_dev_schedule_tx(void);
#if 0
extern struct atbmwifi_common g_hw_prv;
#endif


void ble_hci_trans_cfg_hs(ble_hci_trans_rx_cmd_fn* cmd_cb,
    void* cmd_arg,
    ble_hci_trans_rx_acl_fn* acl_cb,
    void* acl_arg)
{
    ble_hci_ram_rx_cmd_hs_cb = cmd_cb;
    ble_hci_ram_rx_cmd_hs_arg = cmd_arg;
    ble_hci_ram_rx_acl_hs_cb = acl_cb;
    ble_hci_ram_rx_acl_hs_arg = acl_arg;
}

void ble_hci_trans_cfg_ll(ble_hci_trans_rx_cmd_fn* cmd_cb,
    void* cmd_arg,
    ble_hci_trans_rx_acl_fn* acl_cb,
    void* acl_arg)
{
    ble_hci_ram_rx_cmd_ll_cb = cmd_cb;
    ble_hci_ram_rx_cmd_ll_arg = cmd_arg;
    ble_hci_ram_rx_acl_ll_cb = acl_cb;
    ble_hci_ram_rx_acl_ll_arg = acl_arg;
}

int ble_hci_trans_hs_cmd_tx(uint8_t* cmd)
{
    struct ble_hci_hif_pkt* pkt;
    os_sr_t sr;
#if 0
    struct atbmwifi_common* hw_priv = &g_hw_prv;
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

#if 0
    atbm_bh_schedule_tx(hw_priv);
#else
    atbm_ble_dev_schedule_tx();
#endif
    return BLE_ERR_SUCCESS;

}
int ble_hci_trans_ll_evt_tx(uint8_t* hci_ev)
{
    return 0;
}

int ble_hci_trans_hs_acl_tx(struct os_mbuf* om)
{
    struct ble_hci_hif_pkt* pkt;
    os_sr_t sr;
#if 0
    struct atbmwifi_common* hw_priv = &g_hw_prv;
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
#if 0
    atbm_bh_schedule_tx(hw_priv);
#else
    atbm_ble_dev_schedule_tx();
#endif
    return BLE_ERR_SUCCESS;
}

int ble_hci_trans_ll_acl_tx(struct os_mbuf* om)
{
    return BLE_ERR_SUCCESS;
}

uint8_t* ble_hci_trans_buf_alloc(int type)
{
    uint8_t* buf;

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
struct os_mbuf* ble_hci_trans_acl_buf_alloc(void)
{
    struct os_mbuf* m;
    uint8_t usrhdr_len;

    usrhdr_len = sizeof(struct ble_mbuf_hdr);
    m = os_mbuf_get_pkthdr(&ble_hci_acl_mbuf_pool, usrhdr_len);

    return m;
}


void ble_hci_trans_buf_free(uint8_t* buf)
{
    int rc;

    /* XXX: this may look a bit odd, but the controller uses the command
    * buffer to send back the command complete/g_blehifRxdata as an immediate
    * response to the command. This was done to insure that the controller
    * could always send back one of these events when a command was received.
    * Thus, we check to see which pool the buffer came from so we can free
    * it to the appropriate pool
    */
    if (os_memblock_from(&ble_hci_ram_evt_hi_pool, buf)) {
        rc = os_memblock_put(&ble_hci_ram_evt_hi_pool, buf);
        assert(rc == 0);
    }
    else if (os_memblock_from(&ble_hci_ram_evt_lo_pool, buf)) {
        rc = os_memblock_put(&ble_hci_ram_evt_lo_pool, buf);
        assert(rc == 0);
    }
    else {
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
        sizeof(struct ble_hci_hif_pkt),
        ble_hci_hif_pkt_buf,
        "ble_hci_hif_pkt_pool");
    SYSINIT_PANIC_ASSERT(rc == 0);

    memset(&ble_hci_hif_state, 0, sizeof(ble_hci_hif_state));
    STAILQ_INIT(&ble_hci_hif_state.hif_tx_pkt);
    //atbm_ioct_init();
}

struct ble_hci_hif_pkt* ble_hci_trans_tx_pkt_get(void)
{
    struct ble_hci_hif_pkt* pkt;
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

void ble_hci_trans_copy_data(struct ble_hci_hif_pkt* tx_pkt, uint8_t* output, uint32_t* putLen)
{
    uint32_t tx_len = 0;
    uint8_t* data;
    struct os_mbuf* om, * om_next;

    output[*putLen] = tx_pkt->type;
    (*putLen)++;
    if (tx_pkt->type == BLE_HCI_HIF_CMD) {
        data = (atbm_uint8*)tx_pkt->data;
        tx_len = data[2] + BLE_HCI_CMD_HDR_LEN;
        memcpy(&output[*putLen], data, tx_len);
        (*putLen) += tx_len;
        ble_hci_trans_buf_free(data);
    }
    else if (tx_pkt->type == BLE_HCI_HIF_ACL) {
        om = (struct os_mbuf*)tx_pkt->data;
        om = os_mbuf_trim_front(om);
        tx_len = 0;
        while (om != NULL) {
            tx_len += om->om_len;
            if (tx_len > HCI_ACL_SHARE_SIZE) {
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
    else {
        assert(0);
    }
    os_memblock_put(&ble_hci_hif_pkt_pool, tx_pkt);
}

void _ble_hci_trans_copy_data(struct ble_hci_hif_pkt* tx_pkt, uint8_t* output, uint32_t* putLen)
{
    uint32_t tx_len = 0;
    uint8_t* data;
    struct os_mbuf* om, * om_next;

    output[*putLen] = tx_pkt->type;
    (*putLen)++;
    if (tx_pkt->type == BLE_HCI_HIF_CMD) {
        data = (atbm_uint8*)tx_pkt->data;
        tx_len = data[2] + BLE_HCI_CMD_HDR_LEN;
        memcpy(&output[*putLen], data, tx_len);
        (*putLen) += tx_len;
    }
    else if (tx_pkt->type == BLE_HCI_HIF_ACL) {
        om = (struct os_mbuf*)tx_pkt->data;
        om = os_mbuf_trim_front(om);
        tx_len = 0;
        while (om != NULL) {
            tx_len += om->om_len;
            if (tx_len > HCI_ACL_SHARE_SIZE) {
                ATBM_BUG_ON(1);
            }
            memcpy(&output[*putLen], om->om_data, om->om_len);
            (*putLen) += om->om_len;
            om_next = SLIST_NEXT(om, om_next);
            om = om_next;
        }
    }
    else {
        assert(0);
    }
}
void ble_hci_trans_free_hif_pkt(struct ble_hci_hif_pkt* tx_pkt)
{
    switch (tx_pkt->type) {
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
int ble_hci_trans_hs_rx(uint8_t ack, uint8_t* data, uint16_t data_len)
{
    int len;
    uint8_t* evtbuf = NULL;
    struct os_mbuf* om = NULL;
    int ret = -1;

#if 0
    int i;
    iot_printf("hs_rx:");
    for (i = 0;i < 32;i++) {
        iot_printf("%02X ", data[i]);
    }
    iot_printf("\n");
#endif
    switch (data[0]) {
    case BLE_HCI_HIF_EVT:
        len = data[2] + BLE_HCI_EVENT_HDR_LEN;
        assert(len <= MYNEWT_VAL(BLE_HCI_EVT_BUF_SIZE));
        if (ack) {
            evtbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_HI);
        }
        else {
            evtbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_EVT_LO);
        }
        if (evtbuf == NULL) {
			iot_printf("ack evt alloc err\n");
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
        if (om == NULL) {
			iot_printf("acl alloc err\n");
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


////////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>
#include <asm/ioctl.h>
//#include <arpa/inet.h>
//#include <netinet/in.h>
#include <sys/types.h>
//#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
//#include <linux/smp.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>

enum ieee80211_ble_msg_type
{
    BLE_MSG_TYPE_UNKOWN,
    BLE_MSG_TYPE_ACK,
    BLE_MSG_TYPE_EVT,
};

#define SER_SOCKET_PATH          	"/tmp/server_socket"
#define ATBM_IOCTL          			(121)

#define ATBM_BLE_COEXIST_START        	_IOW(ATBM_IOCTL,0, unsigned int)
#define ATBM_BLE_COEXIST_STOP        	_IOW(ATBM_IOCTL,1, unsigned int)
#define ATBM_BLE_SET_ADV_DATA        	_IOW(ATBM_IOCTL,2, unsigned int)
#define ATBM_BLE_ADV_RESP_MODE_START    _IOW(ATBM_IOCTL,3, unsigned int)
#define ATBM_BLE_SET_RESP_DATA     		_IOW(ATBM_IOCTL,4, unsigned int)
#define ATBM_BLE_HIF_TXDATA     		_IOW(ATBM_IOCTL,5, unsigned int)
#define MAX_SYNC_EVENT_BUFFER_LEN 	512
#define CMD_LINE_LEN             	1600
struct ioctl_status_async
{
    u8 type; /* 0: connect msg, 1: driver msg, 2:scan complete, 3:wakeup host reason, 4:disconnect reason, 5:connect fail reason, 6:customer event*/
    u8 driver_mode; /* TYPE1 0: rmmod, 1: insmod; TYPE3\4\5 reason */
    u8 list_empty;
    u8 event_buffer[MAX_SYNC_EVENT_BUFFER_LEN];
};
struct wsm_hdr
{
    u16 len;
    u16 id;
};
static sem_t sem_ioctl_stat;
sem_t sem_sock_sync;
static sem_t sem_status;
static int atbm_fp = -1;
#ifdef CONFIG_BLE_RECV_DATA_HIGHT_LAYER

static int atbm_net_sock = -1;
#endif
static u8 ioctl_data[1024];
char cmd_line[CMD_LINE_LEN];
static struct ioctl_status_async g_blehifRxdata;
bool g_is_quit = 0;
static  int read_happens = 0;
pthread_t fsread_tid;
bool atbm_ble_is_quit;


int open_atbm_ioctl(void)
{
    unsigned long flags = 0;
	
    atbm_fp = open("/dev/atbm_ioctl", O_RDWR);
    if (atbm_fp < 0) {
        fprintf(stdout, "open /dev/atbm_ioctl fail.\n");
        return -1;
    }
	
    fcntl(atbm_fp, F_SETOWN, getpid());
    flags = fcntl(atbm_fp, F_GETFL);
    fcntl(atbm_fp, F_SETFL, flags | FASYNC);

	flags = fcntl(atbm_fp, F_GETFD, 0);
	fcntl(atbm_fp, F_SETFD, flags | FD_CLOEXEC);
	iot_printf("FD_CLOEXEC set\n");

    return 0;
}


void ioctl_msg_func(int sig_num)
{
	sem_post(&sem_status);
//	iot_printf("ioctl_msg_func\n");
#if 0
    int len = 0;
	sem_wait(&sem_status);
    while (!g_is_quit) {
        memset(&g_blehifRxdata, 0, sizeof(struct ioctl_status_async));
		iot_printf("read_happens ++ (%d)\n",read_happens);
        len = read(atbm_fp, &g_blehifRxdata, sizeof(struct ioctl_status_async));
		iot_printf("read_happens --0 (%d)\n",read_happens);
        if (len < (int)(sizeof(g_blehifRxdata))) {
            fprintf(stdout, "Line:%d read connect stat error.\n", __LINE__);
            break;
        }
        else {
            struct wsm_hdr* wsm = (struct wsm_hdr*)g_blehifRxdata.event_buffer;
            unsigned char* data = (unsigned char*)(wsm + 1);
			iot_printf("read_happens --1 (%d)(%d)\n",read_happens,wsm->id);
            //iot_printf("ble hif rx data %d\n", wsm->id);
            //dump_mem(g_blehifRxdata.event_buffer, 16);
            if (wsm->id == BLE_MSG_TYPE_ACK) {
                ble_hci_trans_hs_rx(1, data, 1);
            }
            else if (wsm->id == BLE_MSG_TYPE_EVT) {
                ble_hci_trans_hs_rx(0, data, 1);
            }
            else {
                iot_printf("ble rx data error!!!!\n");
            }
			iot_printf("read_happens --2 (%d)\n",read_happens);
			read_happens++;
        }

        if (g_blehifRxdata.list_empty) {
            break;
        }
    }
	sem_post(&sem_status);
#endif
}
#ifdef CONFIG_BLE_RECV_DATA_HIGHT_LAYER
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/rtnetlink.h>
#include <netpacket/packet.h>
#include <linux/filter.h>
#include <linux/errqueue.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
//#include <linux/skbuff.h>


struct ethhdr {
        unsigned char   h_dest[6];       /* destination eth addr */
        unsigned char   h_source[6];     /* source ether addr    */
        short          h_proto;                /* packet type ID field */
} __attribute__((packed));


#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]


/*      0800    IP
 *      8100    802.1Q VLAN
 *      0001    802.3
 *      0002    AX.25
 *      0004    802.2
 *      8035    RARP
 *      0005    SNAP
 *      0805    X.25
 *      0806    ARP
 *      8137    IPX
 *      0009    Localtalk
 *      86DD    IPv6
 */
#define ETH_P_ALL 0x0003
#define ETH_P_CUSTOM 0x88ff


int atbm_cmd_system(const char* command,char * buf,int buff_len)
{
    char* result = NULL;
    FILE *fpRead = NULL;
	int ret = 0;
    fpRead = popen(command, "r");
	if(!fpRead){
		printf("command : %d  fail!!\n",command);
		return -1;
	}
	if(fpRead && buf && (buff_len > 1)){
		memset(buf,0,buff_len);
		if(fgets(buf,buff_len-1,fpRead)!=NULL)
		{ 
			printf(" %s,%d \n",buf,strlen(buf));			
			ret = 0;
		}else{
			ret = -1;
		}
	}
		
    return ret;
}



int creat_br_socket(char * ifname)
{
	int sockfd;
	struct sockaddr_ll ll;
	int optlen,optval;
	char cmd_buff[64] = {0};
	int i = 0;
	while(1){
		snprintf(cmd_buff,63,"cat /proc/net/wireless  | grep %s",ifname);
		if(atbm_cmd_system(cmd_buff,cmd_buff,63) == 0){
			
			snprintf(cmd_buff,63,"ifconfig %s up",ifname);
			atbm_cmd_system(cmd_buff,NULL,0);
			break;
		}else{
			printf("WIFI driver is not ready!\n");
			i++;
			if(i == 3)
				return -1;
		}
		sleep(1);
	}
	
	struct sock_filter ethertype_sock_filter_insns[] = {
		/* Load ethertype */
		BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 2 * 6),
		/* Jump over next statement if ethertype does not match */
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, ETH_P_CUSTOM, 0, 1),
		/* Ethertype match - return all */
		BPF_STMT(BPF_RET | BPF_K, ~0),
		/* No match - drop */
		BPF_STMT(BPF_RET | BPF_K, 0)
	};
	const struct sock_fprog ethertype_sock_filter = {
		.len = ARRAY_SIZE(ethertype_sock_filter_insns),
		.filter = ethertype_sock_filter_insns,
	};

	
	memset(&ll, 0, sizeof(ll));
	ll.sll_family = PF_PACKET;
	ll.sll_ifindex = if_nametoindex(ifname);
	ll.sll_protocol = htons(ETH_P_ALL);
	//ll.sll_pkttype = PACKET_OTHERHOST;

	sockfd = socket(PF_PACKET, SOCK_RAW , htons(ETH_P_ALL));
	if (sockfd < 0) {
		printf( "%s: socket(PF_PACKET): %s",__func__, strerror(errno));
		/* try to continue without the workaround RX socket */
		return -1;
	}

	if (bind(sockfd, (struct sockaddr *) &ll, sizeof(ll)) < 0) {
		printf( "%s: bind[PF_PACKET]: %s",__func__, strerror(errno));
		/* try to continue without the workaround RX socket */
		close(sockfd);
		return -1;
	}
	
	if(setsockopt(sockfd, SOL_SOCKET, SO_ATTACH_FILTER,
		       &ethertype_sock_filter, sizeof(struct sock_fprog))){
				   
		printf("creat_br_socket : setsockopt fail! \n");
		return -1;
	}
	
	printf("create socket = %d \n",sockfd);
	return sockfd;
}

#endif
static void* fs_read_func(void* arg)
{
#ifdef CONFIG_BLE_RECV_DATA_HIGHT_LAYER
	int res = -1;
	unsigned char buf[1024];
	struct sockaddr_ll ll;
	socklen_t fromlen;
	int ether_len = 0;
	
	if(atbm_net_sock < 0){
		printf("atbm_net_sock is init fail! \n");
		return;
	}
	memset(&ll, 0, sizeof(ll));
	fromlen = sizeof(ll);
	ether_len = sizeof(struct ethhdr);
	
	while(!g_is_quit){
		res = recvfrom(atbm_net_sock, buf, sizeof(buf), 0,  (struct sockaddr *) &ll,&fromlen);
		if (res < 0) {
			printf("recv_data - recvfrom: %s \n",strerror(errno));
			continue;
		}

				
		if (res < (int)(sizeof(g_blehifRxdata) + ether_len)) {
            fprintf(stdout, "Line:%d read connect stat error.\n", __LINE__);
            break;
        }
        else {
			memcpy(&g_blehifRxdata,buf+ether_len,sizeof(g_blehifRxdata));
            struct wsm_hdr* wsm = (struct wsm_hdr*)g_blehifRxdata.event_buffer;
            unsigned char* data = (unsigned char*)(wsm + 1);
//				iot_printf("read_happens --1 (%d)(%d)\n",read_happens,wsm->id);
            //iot_printf("ble hif rx data %d\n", wsm->id);
            //dump_mem(g_blehifRxdata.event_buffer, 16);
            if (wsm->id == BLE_MSG_TYPE_ACK) {
                ble_hci_trans_hs_rx(1, data, 1);
            }
            else if (wsm->id == BLE_MSG_TYPE_EVT) {
                ble_hci_trans_hs_rx(0, data, 1);
            }
            else {
                iot_printf("ble rx data error!!!!\n");
            }
//				iot_printf("read_happens --2 (%d)\n",read_happens);
			read_happens++;
			
        }
	
		
	}
	
#else
	int len = 0;
	
	while (!g_is_quit) {
//		iot_printf("fs_read_func\n");
		sem_wait(&sem_status);

		do{
//			iot_printf("read_happens ++ (%d)\n",read_happens); 
			memset(&g_blehifRxdata, 0, sizeof(struct ioctl_status_async));
			len = read(atbm_fp, &g_blehifRxdata, sizeof(struct ioctl_status_async));
//			iot_printf("read_happens --0 (%d)\n",read_happens);
			
			if (len < (int)(sizeof(g_blehifRxdata))) {
	            fprintf(stdout, "Line:%d read connect stat error.\n", __LINE__);
	            break;
	        }
	        else {
	            struct wsm_hdr* wsm = (struct wsm_hdr*)g_blehifRxdata.event_buffer;
	            unsigned char* data = (unsigned char*)(wsm + 1);
//				iot_printf("read_happens --1 (%d)(%d)\n",read_happens,wsm->id);
	            //iot_printf("ble hif rx data %d\n", wsm->id);
	            //dump_mem(g_blehifRxdata.event_buffer, 16);
	            if (wsm->id == BLE_MSG_TYPE_ACK) {
	                ble_hci_trans_hs_rx(1, data, 1);
	            }
	            else if (wsm->id == BLE_MSG_TYPE_EVT) {
	                ble_hci_trans_hs_rx(0, data, 1);
	            }
	            else {
	                iot_printf("ble rx data error!!!!\n");
	            }
//				iot_printf("read_happens --2 (%d)\n",read_happens);
				read_happens++;
	        }

	        if (g_blehifRxdata.list_empty) {
	            break;
	        }
		}while(1);
	}
#endif
}
int ble_ioctl_tx(u8 * buffer,int len)
{
    return ioctl(atbm_fp, ATBM_BLE_HIF_TXDATA, (unsigned long)(buffer));
}

u8 ble_xmit_buff[2048];
void atbm_ble_dev_schedule_tx(void)
{   
    struct ble_hci_hif_pkt* tx_pkt;
    u32 tx_len;
    
flush:

    tx_len = 0;
   // xmit_buff = NULL;
    sem_wait(&sem_ioctl_stat);

    tx_pkt = ble_hci_trans_tx_pkt_get();

    if (tx_pkt == NULL) {
        goto pkt_null;
    }
	memset(ble_xmit_buff, 0, 2048);
    _ble_hci_trans_copy_data(tx_pkt, &ble_xmit_buff[2], &tx_len);
    *(u16*)ble_xmit_buff = tx_len;
    //dump_mem(xmit_buff, tx_len+2);
    //iot_printf("hif schedule_tx %d\n", tx_len + 2);
    ble_ioctl_tx(ble_xmit_buff, tx_len + 2);

    //goto pkt_free;
   // sem_post(&sem_ioctl_stat);
pkt_free:
    sem_post(&sem_ioctl_stat);
    ble_hci_trans_free_hif_pkt(tx_pkt);
    goto flush;
pkt_null:
    sem_post(&sem_ioctl_stat);
    return;

}
int ble_coexist_start(void)
{
   // struct ioctl_ble_start ble_start;
    return ioctl(atbm_fp, ATBM_BLE_COEXIST_START, (unsigned long)(&ioctl_data));
}

int ble_coexist_stop(void)
{
   // struct ioctl_ble_start ble_start;
    return ioctl(atbm_fp, ATBM_BLE_COEXIST_STOP, (unsigned long)(&ioctl_data));
}


static int socket_accept(int serv_fd) 
{
    struct timeval tv;
    fd_set fds;
    int fd, rc;
 
    /* Wait 20 seconds for a connection, then give up. */
    tv.tv_sec = 0;
    tv.tv_usec = 10000;
    FD_ZERO(&fds);
    FD_SET(serv_fd, &fds);

    rc = select(serv_fd + 1, &fds, NULL, NULL, &tv);
    if (rc < 1) {
        return -1;
    }
    if(FD_ISSET(serv_fd, &fds)){
        fd = accept(serv_fd, NULL, NULL);
	}
    if (fd < 0) {
        return -1;
    }
 
    return fd;
}

int hif_cmd_socket_fd = 0;
void* get_command_func(void* arg)
{
    struct sockaddr_un ser_un;
    int connect_fd = 0;
    int ret = 0;
    char recall[] = "OK";

    hif_cmd_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (hif_cmd_socket_fd <= 0) {
        fprintf(stdout, "open socket err\n");
        return;
    }

    unlink(SER_SOCKET_PATH);

    memset(&ser_un, 0, sizeof(ser_un));
    ser_un.sun_family = AF_UNIX;
    strcpy(ser_un.sun_path, SER_SOCKET_PATH);
    ret = bind(hif_cmd_socket_fd, (struct sockaddr*)&ser_un, sizeof(struct sockaddr_un));
    if (ret < 0) {
        fprintf(stdout, "bind err\n");
        return;
    }

    ret = listen(hif_cmd_socket_fd, 5);
    if (ret < 0) {
        fprintf(stdout, "listen err\n");
        return;
    }

    while (!g_is_quit) {
        //connect_fd = accept(hif_cmd_socket_fd, NULL, NULL);
		connect_fd = socket_accept(hif_cmd_socket_fd);
        if (connect_fd < 0) {
            continue;
        }

        memset(cmd_line, 0, sizeof(cmd_line));
        while (1) {
            ret = read(connect_fd, cmd_line, sizeof(cmd_line));
            if (ret > 0) {
                break;
            }
        }

        write(connect_fd, recall, strlen(recall) + 1);
        close(connect_fd);
        fprintf(stdout, "cmd_line: %s\n", cmd_line);
        sem_post(&sem_sock_sync);

        if (!strcmp(cmd_line, "quit")) {
            break;
        }
    }
	
    close(hif_cmd_socket_fd);
}
#ifdef CONFIG_BLE_RECV_DATA_HIGHT_LAYER

int atbm_creat_netif_sock(void)
{
	atbm_net_sock = creat_br_socket("wlan0");
	if(atbm_net_sock < 0){
		printf("fs_read_func : creat_br_socket err ! \n ");
		return -1;
	}
	return 0;
}
#endif
int hif_ioctl_init()
{
    atbm_fp = -1;
    sem_init(&sem_ioctl_stat, 0, 1);
    sem_init(&sem_sock_sync, 0, 0);
	sem_init(&sem_status, 0, 0);	
	g_is_quit = 0;
#ifdef CONFIG_BLE_RECV_DATA_HIGHT_LAYER	
	if(atbm_creat_netif_sock() < 0){
		printf("atbm_creat_netif_sock fail! \n");
		//return -1;
	}
#endif
	pthread_create(&fsread_tid, NULL, fs_read_func, NULL);
    signal(SIGIO, ioctl_msg_func);
    //iot_printf("open_atbm_ioctl\n");
    open_atbm_ioctl();
    //iot_printf("ble_coexist_start\n");
    ble_coexist_start();
    iot_printf("ble_coexist_start--\n");
}

int hif_ioctl_loop()
{
    char* pStr;
    int ret = -1;
    pthread_t sock_tid;
    iot_printf("get_command_func\n");
    pthread_create(&sock_tid, NULL, get_command_func, NULL);
    while (!g_is_quit) {
        sem_wait(&sem_sock_sync);
        if (!strcmp(cmd_line, "quit")) {
            atbm_ble_is_quit=1;
		    while(!g_is_quit)
		    	{
				usleep(100);
				
		    	}
            break;
        }
        else {
            cli_set_event(cmd_line, strlen(cmd_line));
        }
    }
	
config_err:
    pthread_join(sock_tid, NULL);
	pthread_detach(fsread_tid);
	pthread_join(fsread_tid,NULL);
    pthread_cancel(sock_tid);
	pthread_cancel(fsread_tid);
    sem_destroy(&sem_ioctl_stat);
    sem_destroy(&sem_sock_sync);
	sem_destroy(&sem_status);
    if (atbm_fp > 0) {
        close(atbm_fp);
		atbm_fp = -1;
    }
#ifdef CONFIG_BLE_RECV_DATA_HIGHT_LAYER

	if(atbm_net_sock > 0){
		close(atbm_net_sock);
		atbm_net_sock = -1;
	}
#endif
    fprintf(stdout, "exit ble_smt process\n");

    return 0;
}

int atbm_ble_exit(void)
{
	while(!g_is_quit)
	{
		sleep(1);
	}
    pthread_cancel(fsread_tid);
    sem_destroy(&sem_ioctl_stat);
    sem_destroy(&sem_sock_sync);
	sem_destroy(&sem_status);
     if (atbm_fp > 0) {
        close(atbm_fp);
		atbm_fp = -1;
    }
#ifdef CONFIG_BLE_RECV_DATA_HIGHT_LAYER
	if(atbm_net_sock > 0){
		close(atbm_net_sock);
		atbm_net_sock = -1;
	}
#endif
    fprintf(stdout, "exit ble process\n");
}

