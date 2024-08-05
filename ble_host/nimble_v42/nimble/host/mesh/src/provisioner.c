#include "syscfg/syscfg.h"
#if MYNEWT_VAL(BLE_MESH_PROVISIONER) == 1



#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_PROVISIONER))
#include "host/ble_hs_log.h"

#include "mesh/mesh.h"
#include "mesh_priv.h"

#include <tinycrypt/ecc.h>

#include "beacon.h"
#include "crypto.h"
#include "mesh_atomic.h"
#include "adv.h"
#include "net.h"
#include "access.h"
#include "foundation.h"
#include "provisioner.h"
#include "provisioner_proxy.h"
#include "testing.h"

#include "nimble/ble.h"


/* 3 transmissions, 20ms interval */
#define PROV_XMIT              	BT_MESH_TRANSMIT(2, 20)

#define AUTH_METHOD_NO_OOB     	0x00
#define AUTH_METHOD_STATIC     	0x01
#define AUTH_METHOD_OUTPUT     	0x02
#define AUTH_METHOD_INPUT      	0x03

#define OUTPUT_OOB_BLINK       	0x00
#define OUTPUT_OOB_BEEP        	0x01
#define OUTPUT_OOB_VIBRATE     	0x02
#define OUTPUT_OOB_NUMBER      	0x03
#define OUTPUT_OOB_STRING      	0x04

#define INPUT_OOB_PUSH         	0x00
#define INPUT_OOB_TWIST        	0x01
#define INPUT_OOB_NUMBER       	0x02
#define INPUT_OOB_STRING       	0x03

#define PROV_ERR_NONE          	0x00
#define PROV_ERR_NVAL_PDU      	0x01
#define PROV_ERR_NVAL_FMT      	0x02
#define PROV_ERR_UNEXP_PDU     	0x03
#define PROV_ERR_CFM_FAILED    	0x04
#define PROV_ERR_RESOURCES     	0x05
#define PROV_ERR_DECRYPT       	0x06
#define PROV_ERR_UNEXP_ERR     	0x07
#define PROV_ERR_ADDR          	0x08

#define PROV_INVITE            	0x00
#define PROV_CAPABILITIES      	0x01
#define PROV_START             	0x02
#define PROV_PUB_KEY           	0x03
#define PROV_INPUT_COMPLETE    	0x04
#define PROV_CONFIRM           	0x05
#define PROV_RANDOM            	0x06
#define PROV_DATA              	0x07
#define PROV_COMPLETE          	0x08
#define PROV_FAILED            	0x09

#define PROV_ALG_P256          	0x00

#define GPCF(gpc)              	(gpc & 0x03)
#define GPC_START(last_seg)    	(((last_seg) << 2) | 0x00)
#define GPC_ACK                	0x01
#define GPC_CONT(seg_id)       	(((seg_id) << 2) | 0x02)
#define GPC_CTL(op)            	(((op) << 2) | 0x03)

#define START_PAYLOAD_MAX      	20
#define CONT_PAYLOAD_MAX       	23

#define START_LAST_SEG(gpc)    	(gpc >> 2)
#define CONT_SEG_INDEX(gpc)    	(gpc >> 2)

#define BEARER_CTL(gpc)        	(gpc >> 2)
#define LINK_OPEN              	0x00
#define LINK_ACK               	0x01
#define LINK_CLOSE             	0x02

#define CLOSE_REASON_SUCCESS   	0x00
#define CLOSE_REASON_TIMEOUT   	0x01
#define CLOSE_REASON_FAILED    	0x02

/* Service data length has minus 1 type length & 2 uuid length*/
#define PROV_SRV_DATA_LEN   	0x12
#define PROXY_SRV_DATA_LEN1 	0x09
#define PROXY_SRV_DATA_LEN2 	0x11

#define PROV_AUTH_VAL_SIZE     	0x10
#define PROV_CONF_SALT_SIZE    	0x10
#define PROV_CONF_KEY_SIZE     	0x10
#define PROV_DH_KEY_SIZE       	0x20
#define PROV_CONFIRM_SIZE      	0x10
#define PROV_PROV_SALT_SIZE    	0x10
#define PROV_CONF_INPUTS_SIZE  	0x91

#define XACT_SEG_DATA(_idx, _seg) (&link[_idx].rx.buf->om_data[20 + ((_seg - 1) * 23)])
#define XACT_SEG_RECV(_idx, _seg) (link[_idx].rx.seg &= ~(1 << (_seg)))
#define BIT_MASK(n) (BIT(n) - 1)

#define XACT_NVAL              	0xff
#define BT_UUID_MESH_PROXY_DATA_OUT_VAL   0x2ade



#define BEACON_TYPE_UNPROVISIONED  0x00
#define BEACON_TYPE_SECURE         0x01


enum {
    REMOTE_PUB_KEY,        /* Remote key has been received */
    LOCAL_PUB_KEY,         /* Local public key is available */
    LINK_ACTIVE,           /* Link has been opened */
    WAIT_GEN_DHKEY,        /* Waiting for remote public key to generate DHKey */
    HAVE_DHKEY,            /* DHKey has been calcualted */
    SEND_CONFIRM,          /* Waiting to send Confirm value */
    WAIT_NUMBER,           /* Waiting for number input from user */
    WAIT_STRING,           /* Waiting for string input from user */
    TIMEOUT_START,         /* Provision timeout timer has started */
    NUM_FLAGS,
};

/** Provisioner link structure allocation
 * |--------------------------------------------------------|
 * |            Link(PB-ADV)            |   Link(PB-GATT)   |
 * |--------------------------------------------------------|
 * |<----------------------Total Link---------------------->|
 */
struct prov_link {
    ATOMIC_DEFINE(flags, NUM_FLAGS);
    u8_t  uuid[16];          /* check if device is being provisioned*/
    u16_t oob_info;          /* oob info of this device */
    u8_t  element_num;       /* element num of device */
    u8_t  ki_flags;          /* Key refresh flag and iv update flag */
    u32_t iv_index;          /* IV Index */
    u8_t  auth_method;       /* choosed authentication method */
    u8_t  auth_action;       /* choosed authentication action */
    u8_t  auth_size;         /* choosed authentication size */
	u16_t assign_addr;       /* Application assigned address for the device */
    u16_t unicast_addr;      /* unicast address assigned for device */
    bt_addr_le_t addr;       /* Device address */
#if 1//IS_ENABLED(CONFIG_BT_MESH_PB_GATT)
    bool   connecting;       /* start connecting with device */
    u16_t conn_handle;    /* GATT connection */
#endif
    u8_t  expect;            /* Next expected PDU */

    u8_t dhkey[32];             /* Calculated DHKey */
    u8_t auth[16];              /* Authentication Value */

    u8_t conf_salt[16];         /* ConfirmationSalt */
    u8_t conf_key[16];          /* ConfirmationKey */
    u8_t conf_inputs[145];       /* ConfirmationInputs */

    u8_t *rand;              /* Local Random */
    u8_t conf[16];              /* Remote Confirmation */

    u8_t prov_salt[16];         /* Provisioning Salt */

#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV) || IS_ENABLED(CONFIG_BT_MESH_PB_GATT) 
    bool  linking;           /* Linking is being establishing */
    u16_t link_close;        /* Link close been sent flag */
    u32_t id;                /* Link ID */
    u8_t  pending_ack;       /* Decide which transaction id ack is pending */
    u8_t  expect_ack_for;    /* Transaction ACK expected for provisioning pdu */

    struct {
        u8_t  id;            /* Transaction ID */
        u8_t  prev_id;       /* Previous Transaction ID */
        u8_t  seg;           /* Bit-field of unreceived segments */
        u8_t  last_seg;      /* Last segment (to check length) */
        u8_t  fcs;           /* Expected FCS value */
        u8_t  adv_buf_id;    /* index of buf allocated in adv_buf_data */
        struct os_mbuf *buf;
    } rx;

    struct {
        /* Start timestamp of the transaction */
        s64_t start;

        /* Transaction id*/
        u8_t id;

        /* Pending outgoing buffer(s) */
        struct os_mbuf *buf[3];

        /* Retransmit timer */
        struct k_delayed_work retransmit;
    } tx;
#endif
    /** Provision timeout timer. Spec P259 says: The provisioning protocol
     *  shall have a minimum timeout of 60 seconds that is reset each time
     *  a provisioning protocol PDU is sent or received.
     */
    struct k_delayed_work timeout;
};

struct prov_rx {
    u32_t link_id;
    u8_t  xact_id;
    u8_t  gpc;
};

#define BT_MESH_ALREADY_PROV_NUM  (CONFIG_BT_MESH_PROV_NODES_MAX + 10)

struct prov_ctx_t {
    /** Provisioner public key and random have been generated
     *  Bit0 for public key and Bit1 for random
     */
    u8_t  pub_key_rand_done;
    bt_mesh_prov_bearer_t bearers;
    /* Provisioner public key */
    u8_t  public_key[64];

    /* Provisioner random */
    u8_t  random[16];

    /* Number of provisioned devices */
    u16_t node_count;

    /* Current number of PB-ADV provisioned devices simultaneously */
    u8_t  pba_count;

    /* Current number of PB-GATT provisioned devices simultaneously */
    u8_t  pbg_count;

    /* Current index of device being provisioned using PB-GATT or PB-ADV */
    int   pb_index;

    /* Current unicast address going to assigned */
    u16_t current_addr;

    /* Number of unprovisioned devices whose information has been added to queue */
    u8_t unprov_dev_num;

    /* Current net_idx going to be used in provisioning data */
    u16_t curr_net_idx;

    /* Current flags going to be used in provisioning data */
    u16_t curr_flags;

    /* Current iv_index going to be used in provisioning data */
    u16_t curr_iv_index;

    /* Offset of the device uuid to be matched, based on zero */
    u8_t  match_offset;

    /* Length of the device uuid to be matched (start from the match_offset) */
    u8_t  match_length;

    /* Value of the device uuid to be matched */
    u8_t  match_value[16];

    /* Indicate when received uuid_match adv_pkts, can provision it at once */
    bool prov_after_match;

    /** This structure is used to store the information of the device which
     *  provisioner has successfully sent provisioning data to. In this
     *  structure, we don't care if the device is currently in the mesh
     *  network, or has been removed, or failed to send provisioning
     *  complete pdu after receiving the provisioning data pdu.
     */
    struct already_prov_info {
        u8_t  uuid[16];     /* device uuid */
        u8_t  element_num;  /* element number of the deleted node */
        u16_t unicast_addr; /* Primary unicast address of the deleted node */
    } already_prov[BT_MESH_ALREADY_PROV_NUM];
};

struct unprov_dev_queue {
    bt_addr_le_t addr;
    u8_t         uuid[16];
    u16_t        oob_info;
    u8_t         bearer;
    u8_t         flags;
} __packed unprov_dev[CONFIG_BT_MESH_UNPROV_ADD_MAX] = {
    [0 ... (CONFIG_BT_MESH_UNPROV_ADD_MAX - 1)] = {
        .addr.type = 0xff,
        .bearer    = 0,
        .flags     = false,
    },
};



#define PROV_ADV  BIT(0)
#define PROV_GATT BIT(1)

#define RETRANSMIT_TIMEOUT   K_MSEC(500)
#define BUF_TIMEOUT          K_MSEC(400)
#define TRANSACTION_TIMEOUT  K_SECONDS(30)
#define PROVISION_TIMEOUT    K_SECONDS(60)

#if IS_ENABLED(CONFIG_BT_MESH_PB_GATT)
#define PROV_BUF_HEADROOM 5
#else
#define PROV_BUF_HEADROOM 0
#endif

#define PROV_BUF(len) NET_BUF_SIMPLE(PROV_BUF_HEADROOM + len)
#define BT_MESH_PROV_SAME_TIME (CONFIG_BT_MESH_PBA_SAME_TIME + CONFIG_BT_MESH_PBG_SAME_TIME)


static struct prov_link link[BT_MESH_PROV_SAME_TIME];
static struct prov_ctx_t prov_ctx;
static struct bt_mesh_prov_node_info prov_nodes[CONFIG_BT_MESH_PROV_NODES_MAX];

#if 0
static prov_adv_pkt_cb adv_pkt_notify;
#endif

//#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
static struct os_mbuf *rx_buf[CONFIG_BT_MESH_PBA_SAME_TIME];
//#endif

static const struct bt_mesh_prov        *prov;
static const struct bt_mesh_provisioner *provisioner;
static const struct bt_mesh_comp        *comp;

u8_t prov_addr;

static struct ble_npl_mutex pb_gatt_lock;
static inline void bt_mesh_pb_gatt_lock(void)
{
    ble_npl_mutex_pend(&pb_gatt_lock, BLE_NPL_TIME_FOREVER);
}

static inline void bt_mesh_pb_gatt_unlock(void)
{
    ble_npl_mutex_release(&pb_gatt_lock);
}

static void send_link_open(void);
static void send_pub_key(u8_t oob);
static void close_link(int i, u8_t reason);


void provisioner_pbg_count_dec(void)
{
    if (prov_ctx.pbg_count) {
        prov_ctx.pbg_count--;
    }
}

void provisioner_pbg_count_inc(void)
{
    prov_ctx.pbg_count++;
}

void provisioner_unprov_dev_num_dec(void)
{
    if (prov_ctx.unprov_dev_num) {
        prov_ctx.unprov_dev_num--;
    }
}

void provisioner_clear_connecting(int index)
{
    int i = index + CONFIG_BT_MESH_PBA_SAME_TIME;
#if IS_ENABLED(CONFIG_BT_MESH_PB_GATT)
    link[i].connecting = false;
    link[i].conn_handle = 0;
#endif
    link[i].oob_info = 0x0;
    memset(link[i].uuid, 0, 16);
    memset(&link[i].addr, 0, sizeof(link[i].addr));
    atomic_test_and_clear_bit(link[i].flags, LINK_ACTIVE);
}

const struct bt_mesh_provisioner *provisioner_get_prov_info(void)
{
    return provisioner;
}

void provisioner_link_connected(u16 handle,u16 index)
{
  u16 idx;
  idx = index;
  link[idx].conn_handle = handle;

}

static int provisioner_node_reset(int node_index)
{
    struct bt_mesh_prov_node_info *node = NULL;
    struct bt_mesh_rpl    *rpl  = NULL;
	int i;

	node = &prov_nodes[node_index];

	if(!node->provisioned){
		return -EINVAL;
	}
	
    /* Reset corresponding rpl when reset the node */
    for (i = 0; i < ARRAY_SIZE(bt_mesh.rpl); i++) {
        rpl = &bt_mesh.rpl[i];
        if (rpl->src >= node->unicast_addr &&
                rpl->src < node->unicast_addr + node->element_num) {
            memset(rpl, 0, sizeof(struct bt_mesh_rpl));
        }
    }

    memset(node, 0, sizeof(struct bt_mesh_prov_node_info));
    if (prov_ctx.node_count) {
        prov_ctx.node_count--;
    }

	return 0;
}

int provisioner_all_nodes_reset(void)
{
    int i;

    BT_DBG("Reset all nodes");

    for (i = 0; i < ARRAY_SIZE(prov_nodes); i++) {
		provisioner_node_reset(i);
    }

    return 0;
}

static int provisioner_dev_find(const bt_addr_le_t *addr, const u8_t uuid[16], int *index)
{
    bool uuid_match = false;
    bool addr_match = false;
    u8_t zero[6] = {0};
    int i = 0, j = 0, comp = 0;

    if (addr) {
        comp = memcmp(addr->val, zero, 6);
    }

    if ((!uuid && (!addr || (comp == 0) || (addr->type > BLE_ADDR_RANDOM))) || !index) {
        return -EINVAL;
    }

    /** Note: user may add a device into two unprov_dev array elements,
     *        one with device address, address type and another only
     *        with device UUID. We need to take this into consideration.
     */
    if (uuid) {
        for (i = 0; i < ARRAY_SIZE(unprov_dev); i++) {
            if (!memcmp(unprov_dev[i].uuid, uuid, 16)) {
                uuid_match = true;
                break;
            }
        }
    }

    if (addr && comp && (addr->type <= BLE_ADDR_RANDOM)) {
        for (j = 0; j < ARRAY_SIZE(unprov_dev); j++) {
            if (!memcmp(unprov_dev[j].addr.val, addr->val, 6) &&
                    unprov_dev[j].addr.type == addr->type) {
                addr_match = true;
                break;
            }
        }
    }

    if (!uuid_match && !addr_match) {
        BT_DBG("Device does not exist in queue");
        return -ENODEV;
    }

    if (uuid_match && addr_match && (i != j)) {
        /** In this situation, copy address & type into device
         *  uuid array element, reset another element, rm_flag
         *  will be decided by uuid element.
         *  Note: need to decrement the unprov_dev_num count
         */
        unprov_dev[i].addr.type = unprov_dev[j].addr.type;
        memcpy(unprov_dev[i].addr.val, unprov_dev[j].addr.val, 6);
        unprov_dev[i].bearer |= unprov_dev[j].bearer;
        memset(&unprov_dev[j], 0x0, sizeof(struct unprov_dev_queue));
        provisioner_unprov_dev_num_dec();
    }

    *index = uuid_match ? i : j;
    return 0;
}


static int provisioner_dev_uuid_match(const u8_t dev_uuid[16])
{
    if (!dev_uuid) {
        BT_ERR("Invalid parameters");
        return -EINVAL;
    }

    if (prov_ctx.match_length && prov_ctx.match_value) {
        if (memcmp(dev_uuid + prov_ctx.match_offset,
                   prov_ctx.match_value, prov_ctx.match_length)) {
            return -EAGAIN;
        }
    }

    return 0;
}

const u8_t *provisioner_net_key_get(u16_t net_idx)
{
    struct bt_mesh_subnet *sub = NULL;
    int i;

    BT_DBG("");

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
        sub = &bt_mesh.p_sub[i];
        if (!sub->subnet_active || (sub->net_idx != net_idx)) {
            BT_DBG("CONTINUE: %u, %u, %u",sub->subnet_active,
                	sub->net_idx, net_idx);
            continue;
        }
        if (sub->kr_flag) {
            BT_DBG("return kr_flag: %u", sub->kr_flag);
            return sub->keys[1].net;
        }

        BT_DBG("return netkey: %02x, %02x, %02x, %02x",
            sub->keys[0].net[0],
            sub->keys[0].net[1],
            sub->keys[0].net[2],
            sub->keys[0].net[3]);
        return sub->keys[0].net;
    }

    BT_DBG("return netkey NULL");

    return NULL;
}

struct bt_mesh_subnet *provisioner_subnet_get(u16_t net_idx)
{
    struct bt_mesh_subnet *sub = NULL;
    int i;

    BT_DBG("");

    if (net_idx == BT_MESH_KEY_ANY) {
        return &bt_mesh.p_sub[0];
    }

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
        sub = &bt_mesh.p_sub[i];
        if (!sub->subnet_active || (sub->net_idx != net_idx)) {
            continue;
        }
        return sub;
    }

    return NULL;
}

bool provisioner_check_msg_dst_addr(u16_t dst_addr)
{
    struct bt_mesh_prov_node_info *node = NULL;
    int i;

    BT_DBG("");

    if (!BT_MESH_ADDR_IS_UNICAST(dst_addr)) {
        return true;
    }

    for (i = 0; i < ARRAY_SIZE(prov_nodes); i++) {
        node = &prov_nodes[i];
        if (node->provisioned && dst_addr >= node->unicast_addr &&
                dst_addr < node->unicast_addr + node->element_num) {
            return true;
        }
    }

    return false;
}

const u8_t *provisioner_get_device_key(u16_t dst_addr)
{
    /* Device key is only used to encrypt configuration messages.
    *  Configuration model shall only be supported by the primary
    *  element which uses the primary unicast address.
    */
    struct bt_mesh_prov_node_info *node = NULL;
    int i;

    BT_DBG("");

    if (!BT_MESH_ADDR_IS_UNICAST(dst_addr)) {
        BT_ERR("%s: dst_addr is not unicast", __func__);
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(prov_nodes); i++) {
        node = &prov_nodes[i];
        if (node->provisioned && node->unicast_addr == dst_addr) {
            return node->dev_key;
        }
    }
	
    return NULL;
}

struct bt_mesh_app_key *provisioner_app_key_find(u16_t app_idx)
{
    struct bt_mesh_app_key *key = NULL;
    int i;

    BT_DBG("");

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_app_keys); i++) {
        key = &bt_mesh.p_app_keys[i];
        if (!key->appkey_active) {
            continue;
        }
        if (key->net_idx != BT_MESH_KEY_UNUSED &&
                key->app_idx == app_idx) {
            return key;
        }
    }

    return NULL;
}

u16_t provisioner_get_prov_node_count(void)
{
    return prov_ctx.node_count;
}

#if IS_ENABLED(CONFIG_BT_MESH_PB_GATT)
void bt_mesh_provisioner_clear_link_info(const u8_t addr[6])
{
    int i;

    if (!addr) {
        BT_ERR("%s, Invalid parameter", __func__);
        return;
    }

    BT_DBG("Clear device info, addr %s", bt_hex(addr, 6));

    for (i = CONFIG_BT_MESH_PBA_SAME_TIME; i < CONFIG_BT_MESH_PBA_SAME_TIME; i++) {
        if (!memcmp(link[i].addr.val, addr, 6)) {
            link[i].connecting = false;
            link[i].conn_handle = 0;
            link[i].oob_info = 0x0;
            memset(link[i].uuid, 0, 16);
            memset(&link[i].addr, 0, sizeof(bt_mesh_addr_t));
            atomic_test_and_clear_bit(link[i].flags, LINK_ACTIVE);
            if (atomic_test_and_clear_bit(link[i].flags, TIMEOUT_START)) {
                k_delayed_work_cancel(&link[i].timeout);
            }
            return;
        }
    }

    BT_WARN("Device not found, addr %s", bt_hex(addr, 6));
    return;
}

#endif

#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
static void buf_sent(int err, void *user_data)
{
    int i = (int)user_data;

    if (!link[i].tx.buf[0]) {
        return;
    }

    k_delayed_work_submit(&link[i].tx.retransmit, RETRANSMIT_TIMEOUT);
}

static struct bt_mesh_send_cb buf_sent_cb = {
    .end = buf_sent,
};

static void free_segments(int id)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(link[id].tx.buf); i++) {
        struct os_mbuf *buf = link[id].tx.buf[i];

        if (!buf) {
            break;
        }

        link[id].tx.buf[i] = NULL;
        /* Mark as canceled */
        BT_MESH_ADV(buf)->busy = 0;
        net_buf_unref(buf);
    }
}

static void prov_clear_tx(int i)
{
    BT_DBG("");

    k_delayed_work_cancel(&link[i].tx.retransmit);
    free_segments(i);
}

static void reset_link(int i, u8_t reason)
{
    bool pub_key;

    prov_clear_tx(i);

    if (provisioner->prov_link_close) {
        provisioner->prov_link_close(BT_MESH_PROV_ADV, reason);
    }

    if (atomic_test_and_clear_bit(link[i].flags, TIMEOUT_START)) {
        k_delayed_work_cancel(&link[i].timeout);
    }

    pub_key = atomic_test_bit(link[i].flags, LOCAL_PUB_KEY);

    /* Clear everything except the retransmit delayed work config */
    memset(&link[i], 0, offsetof(struct prov_link, tx.retransmit));

    link[i].pending_ack = XACT_NVAL;
    link[i].rx.prev_id  = XACT_NVAL;

    if (pub_key) {
        atomic_set_bit(link[i].flags, LOCAL_PUB_KEY);
    }
	
	net_buf_simple_init(rx_buf[i], 0);
	link[i].rx.buf = rx_buf[i];

    if (prov_ctx.pba_count) {
        prov_ctx.pba_count--;
    }
}

static struct os_mbuf *adv_buf_create(void)
{
    struct os_mbuf *buf;

    buf = bt_mesh_adv_create(BT_MESH_ADV_PROV, PROV_XMIT, BUF_TIMEOUT);
    if (!buf) {
        BT_ERR("Out of provisioning buffers");
        return NULL;
    }

    return buf;
}

static void ack_complete(u16_t duration, int err, void *user_data)
{
    int i = (int)user_data;

    BT_DBG("xact %u complete", (u8_t)link[i].pending_ack);

    link[i].pending_ack = XACT_NVAL;
}

static void gen_prov_ack_send(u8_t xact_id)
{
    static const struct bt_mesh_send_cb cb = {
        .start = ack_complete,
    };
    const struct bt_mesh_send_cb *complete;
    struct os_mbuf *buf;
    int i = prov_ctx.pb_index;

    BT_DBG("xact_id %u", xact_id);

    if (link[i].pending_ack == xact_id) {
        BT_DBG("Not sending duplicate ack");
        return;
    }

    buf = adv_buf_create();
    if (!buf) {
        return;
    }

    if (link[i].pending_ack == XACT_NVAL) {
        link[i].pending_ack = xact_id;
        complete = &cb;
    } else {
        complete = NULL;
    }

    net_buf_add_be32(buf, link[i].id);
    net_buf_add_u8(buf, xact_id);
    net_buf_add_u8(buf, GPC_ACK);

    bt_mesh_adv_send(buf, complete, (void *)i);
    net_buf_unref(buf);
}

static void send_reliable(int id)
{
    link[id].tx.start = k_uptime_get();

    for (int i = 0; i < ARRAY_SIZE(link[id].tx.buf); i++) {
        struct os_mbuf *buf = link[id].tx.buf[i];

        if (!buf) {
            break;
        }

        if (i + 1 < ARRAY_SIZE(link[id].tx.buf) && link[id].tx.buf[i + 1]) {
            bt_mesh_adv_send(buf, NULL, NULL);
        } else {
            bt_mesh_adv_send(buf, &buf_sent_cb, (void *)id);
        }
    }
}

static int bearer_ctl_send(int i, u8_t op, void *data, u8_t data_len)
{
    struct os_mbuf *buf;

    BT_DBG("op 0x%02x data_len %u", op, data_len);
    
    prov_clear_tx(i);

    buf = adv_buf_create();
    if (!buf) {
        return -ENOBUFS;
    }

    net_buf_add_be32(buf, link[i].id);
    /* Transaction ID, always 0 for Bearer messages */
    net_buf_add_u8(buf, 0x00);
    net_buf_add_u8(buf, GPC_CTL(op));
    net_buf_add_mem(buf, data, data_len);

    link[i].tx.buf[0] = buf;
    send_reliable(i);

    /** We can also use buf->ref and a flag to decide that
     *  link close has been sent 3 times.
     *  Here we use another way: use retransmit timer and need
     *  to make sure the timer is not cancelled during sending
     *  link close pdu, so we add link[i].tx.id = 0
     */
    if (op == LINK_CLOSE) {
        u8_t reason = *(u8_t *)data;
        link[i].link_close = (reason << 8 | BIT(0));
        link[i].tx.id = 0;
    }

    return 0;
}

static void send_link_open(void)
{
    int i = prov_ctx.pb_index, j;

    /** Generate link ID, and may need to check if this id is
     *  currently being used, which may will not happen ever.
     */
    bt_rand(&link[i].id, sizeof(u32_t));
    while (1) {
        for (j = 0; j < CONFIG_BT_MESH_PBA_SAME_TIME; j++) {
            if (atomic_test_bit(link[j].flags, LINK_ACTIVE) || link[j].linking) {
                if (link[i].id == link[j].id) {
                    bt_rand(&link[i].id, sizeof(u32_t));
                    break;
                }
            }
        }
        if (j == CONFIG_BT_MESH_PBA_SAME_TIME) {
            break;
        }
    }

    bearer_ctl_send(i, LINK_OPEN, link[i].uuid, 16);

    /* Set LINK_ACTIVE just to be in compatibility with  current Zephyr code */
    atomic_set_bit(link[i].flags, LINK_ACTIVE);

    prov_ctx.pba_count++;
}

static u8_t last_seg(u8_t len)
{
    if (len <= START_PAYLOAD_MAX) {
        return 0;
    }

    len -= START_PAYLOAD_MAX;

    return 1 + (len / CONT_PAYLOAD_MAX);
}

static inline u8_t next_transaction_id(void)
{
    int i = prov_ctx.pb_index;

    if (link[i].tx.id < 0x7F) {
        return link[i].tx.id++;
    }

    return 0x0;
}

static int prov_send_adv(struct os_mbuf *msg)
{
    struct os_mbuf *start, *buf;
    u8_t seg_len, seg_id;
    u8_t xact_id;
    int i = prov_ctx.pb_index;

    BT_DBG("len %u: %s", msg->om_len, bt_hex(msg->om_data, msg->om_len));

    prov_clear_tx(i);

    start = adv_buf_create();  
    if (!start) {
        return -ENOBUFS;
    }

    xact_id = next_transaction_id();
    net_buf_add_be32(start, link[i].id);
    net_buf_add_u8(start, xact_id);

    net_buf_add_u8(start, GPC_START(last_seg(msg->om_len)));
    net_buf_add_be16(start, msg->om_len);
    net_buf_add_u8(start, bt_mesh_fcs_calc(msg->om_data, msg->om_len));

    link[i].tx.buf[0] = start;

    seg_len = min(msg->om_len, START_PAYLOAD_MAX);
    BT_DBG("seg 0 len %u: %s", seg_len, bt_hex(msg->om_data, seg_len));
    net_buf_add_mem(start, msg->om_data, seg_len);
    net_buf_simple_pull(msg, seg_len);

    buf = start;
    for (seg_id = 1; msg->om_len > 0; seg_id++) {
        if (seg_id >= ARRAY_SIZE(link[i].tx.buf)) {
            BT_ERR("Too big message");
            free_segments(i);
            return -E2BIG;
        }

        buf = adv_buf_create();
        if (!buf) {
            free_segments(i);
            return -ENOBUFS;
        }

        link[i].tx.buf[seg_id] = buf;

        seg_len = min(msg->om_len, CONT_PAYLOAD_MAX);

        BT_DBG("seg_id %u len %u: %s", seg_id, seg_len,
               bt_hex(msg->om_data, seg_len));

        net_buf_add_be32(buf, link[i].id);
        net_buf_add_u8(buf, xact_id);
        net_buf_add_u8(buf, GPC_CONT(seg_id));
        net_buf_add_mem(buf, msg->om_data, seg_len);
        net_buf_simple_pull(msg, seg_len);
    }

    send_reliable(i);

    if (!atomic_test_and_set_bit(link[i].flags, TIMEOUT_START)) {
        k_delayed_work_submit(&link[i].timeout, PROVISION_TIMEOUT);
    }

    return 0;
}

#endif /* CONFIG_BT_MESH_PB_ADV */

#if IS_ENABLED(CONFIG_BT_MESH_PB_GATT)

static int prov_send_gatt(struct os_mbuf *msg)
{
    int i = prov_ctx.pb_index;
    int err;

    if (!link[i].conn_handle) {
        return -ENOTCONN;
    }

    err = bt_mesh_proxy_client_send(link[i].conn_handle, BT_MESH_PROXY_PROV, msg);
    if (err) {
        BT_ERR("Failed to send PB-GATT pdu");
        return err;
    }

    if (!atomic_test_and_set_bit(link[i].flags, TIMEOUT_START)) {
        k_delayed_work_submit(&link[i].timeout, PROVISION_TIMEOUT);
    }

    return 0;
}
#endif /* CONFIG_BT_MESH_PB_GATT */


static inline int prov_send(struct os_mbuf *buf)
{
    int i = prov_ctx.pb_index;
    if (i < CONFIG_BT_MESH_PBA_SAME_TIME) {
#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
        return prov_send_adv(buf);
#endif
    } else if (i <= CONFIG_BT_MESH_PBA_SAME_TIME &&
               i < BT_MESH_PROV_SAME_TIME) {
#if IS_ENABLED(CONFIG_BT_MESH_PB_GATT)
        return prov_send_gatt(buf);
#else
        return -EINVAL;
#endif
    } else {
        BT_ERR("Close link with link index exceeding upper limit");
        return -EINVAL;
    }
}

static void prov_buf_init(struct os_mbuf *buf, u8_t type)
{
    net_buf_simple_init(buf, PROV_BUF_HEADROOM);
    net_buf_simple_add_u8(buf, type);
}

static void prov_invite(const u8_t *data)
{
    BT_DBG("");
}

static void prov_start(const u8_t *data)
{
    BT_DBG("");
}

static void prov_data(const u8_t *data)
{
    BT_DBG("");
}

static void send_invite(void)
{
    struct os_mbuf *buf = PROV_BUF(2);
    int i = prov_ctx.pb_index;
	
    prov_buf_init(buf, PROV_INVITE);

    net_buf_simple_add_u8(buf, provisioner->prov_attention);

    link[i].conf_inputs[0] = provisioner->prov_attention;

    if (prov_send(buf)) {
        BT_ERR("Failed to send invite");
        close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    link[i].expect = PROV_CAPABILITIES;
	
done:
	os_mbuf_free_chain(buf);
}

static void prov_capabilities(const u8_t *data)
{
    struct os_mbuf *buf = PROV_BUF(6);
    u16_t algorithms, output_action, input_action;
    u8_t  element_num, pub_key_oob, static_oob,
          output_size, input_size;
    u8_t  auth_method, auth_action, auth_size;
    int i = prov_ctx.pb_index, j;

    element_num = data[0];
    BT_DBG("Elements: %u", element_num);
    if (!element_num) {
        BT_ERR("Element number wrong");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }
    link[i].element_num = element_num;

    algorithms = sys_get_be16(&data[1]);
    BT_DBG("Algorithms:        %u", algorithms);
    if (algorithms != BIT(PROV_ALG_P256)) {
        BT_ERR("Algorithms wrong");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    pub_key_oob = data[3];
    BT_DBG("Public Key Type:   0x%02x", pub_key_oob);
    if (pub_key_oob > 0x01) {
        BT_ERR("Public key type wrong");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }
    pub_key_oob = ((provisioner->prov_pub_key_oob &&
                    provisioner->prov_pub_key_oob_cb) ? pub_key_oob : 0x00);

    static_oob = data[4];
    BT_DBG("Static OOB Type:   0x%02x", static_oob);
    if (static_oob > 0x01) {
        BT_ERR("Static OOB type wrong");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }
    static_oob = (provisioner->prov_static_oob_val ? static_oob : 0x00);

    output_size = data[5];
    BT_DBG("Output OOB Size:   %u", output_size);
    if (output_size > 0x08) {
        BT_ERR("Output OOB size wrong");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    output_action = sys_get_be16(&data[6]);
    BT_DBG("Output OOB Action: 0x%04x", output_action);
    if (output_action > 0x1f) {
        BT_ERR("Output OOB action wrong");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    /* Provisioner select output action */
    if (output_size) {
        for (j = 0; j < 5; j++) {
            if (output_action & BIT(j)) {
                //output_action = BIT(j);
                output_action = j;
                break;
            }
        }
    }

    input_size = data[8];
    BT_DBG("Input OOB Size: %u", input_size);
    if (input_size > 0x08) {
        BT_ERR("Input OOB size wrong");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    input_action = sys_get_be16(&data[9]);
    BT_DBG("Input OOB Action: 0x%04x", input_action);
    if (input_action > 0x0f) {
        BT_ERR("Input OOB action wrong");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    /* Make sure received pdu is ok and cancel the timeout timer */
    if (atomic_test_and_clear_bit(link[i].flags, TIMEOUT_START)) {
        k_delayed_work_cancel(&link[i].timeout);
    }

    /* Provisioner select input action */
    if (input_size) {
        for (j = 0; j < 4; j++) {
            if (input_action & BIT(j)) {
                //input_action = BIT(j);
                input_action = j;
                break;
            }
        }
    }

    if (static_oob) {
        /* if static oob is valid, just use static oob */
        auth_method = AUTH_METHOD_STATIC;
        auth_action = 0x00;
        auth_size   = 0x00;
    } else {
        if (!output_size && !input_size) {
            auth_method = AUTH_METHOD_NO_OOB;
            auth_action = 0x00;
            auth_size   = 0x00;
        } else if (!output_size && input_size) {
            auth_method = AUTH_METHOD_INPUT;
            auth_action = (u8_t)input_action;
            auth_size   = input_size;
        } else {
            auth_method = AUTH_METHOD_OUTPUT;
            auth_action = (u8_t)output_action;
            auth_size   = output_size;
        }
    }

    /* Store provisioning capbilities value in conf_inputs */
    memcpy(&link[i].conf_inputs[1], data, 11);

    prov_buf_init(buf, PROV_START);
    net_buf_simple_add_u8(buf, provisioner->prov_algorithm);
    net_buf_simple_add_u8(buf, pub_key_oob);
    net_buf_simple_add_u8(buf, auth_method);
    net_buf_simple_add_u8(buf, auth_action);
    net_buf_simple_add_u8(buf, auth_size);

    memcpy(&link[i].conf_inputs[12], &buf->om_data[1], 5);

    if (prov_send(buf)) {
        BT_ERR("Failed to send start");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    link[i].auth_method = auth_method;
    link[i].auth_action = auth_action;
    link[i].auth_size   = auth_size;

    /** After prov start sent, use OOB to get remote public key.
     *  And we just follow the procedure in Figure 5.15 of Section
     *  5.4.2.3 of Mesh Profile Spec.
     */
    if (pub_key_oob) {
        /** Because public key sent using provisioning pdu is
         *  big-endian, we may believe that device public key
         *  received using OOB is big-endian too.
         */
        if (provisioner->prov_pub_key_oob_cb()) {
            BT_ERR("Public Key OOB fail");
			close_link(i, CLOSE_REASON_FAILED);
			goto done;
        }
    }

    /** If using PB-ADV, need to listen for transaction ack,
     *  after ack is received, provisioner can send public key.
     */
#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
    if (i < CONFIG_BT_MESH_PBA_SAME_TIME) {
        link[i].expect_ack_for = PROV_START;
        return;
    }
#endif /* CONFIG_BT_MESH_PB_ADV */

    send_pub_key(pub_key_oob);

done:
	os_mbuf_free_chain(buf);
}

static bt_mesh_output_action_t output_action(u8_t action)
{
    switch (action) {
    case OUTPUT_OOB_BLINK:
        return BT_MESH_BLINK;
    case OUTPUT_OOB_BEEP:
        return BT_MESH_BEEP;
    case OUTPUT_OOB_VIBRATE:
        return BT_MESH_VIBRATE;
    case OUTPUT_OOB_NUMBER:
        return BT_MESH_DISPLAY_NUMBER;
    case OUTPUT_OOB_STRING:
        return BT_MESH_DISPLAY_STRING;
    default:
        return BT_MESH_NO_OUTPUT;
    }
}

static bt_mesh_input_action_t input_action(u8_t action)
{
    switch (action) {
    case INPUT_OOB_PUSH:
        return BT_MESH_PUSH;
    case INPUT_OOB_TWIST:
        return BT_MESH_TWIST;
    case INPUT_OOB_NUMBER:
        return BT_MESH_ENTER_NUMBER;
    case INPUT_OOB_STRING:
        return BT_MESH_ENTER_STRING;
    default:
        return BT_MESH_NO_INPUT;
    }
}

static int prov_auth(u8_t method, u8_t action, u8_t size)
{
    bt_mesh_output_action_t output;
    bt_mesh_input_action_t input;
    int i = prov_ctx.pb_index;

    switch (method) {
    case AUTH_METHOD_NO_OOB: {
        if (action || size) {
            return -EINVAL;
        }
        memset(link[i].auth, 0, 16);
        return 0;
	}

    case AUTH_METHOD_STATIC: {
        if (action || size) {
            return -EINVAL;
        }
		if(provisioner->prov_static_oob_val == NULL){
			BT_ERR("Static oob val is NULL");
			return -EINVAL;
		}
        memcpy(link[i].auth + 16 - provisioner->prov_static_oob_len,
               provisioner->prov_static_oob_val, provisioner->prov_static_oob_len);
        memset(link[i].auth, 0, 16 - provisioner->prov_static_oob_len);
        return 0;
	}

    case AUTH_METHOD_OUTPUT: {
        /* Use auth_action to get device output action */
        output = output_action(action);
        if (!output) {
            return -EINVAL;
        }
		link[i].expect = PROV_INPUT_COMPLETE;
        return provisioner->prov_input_num(output, size, i);
    }

    case AUTH_METHOD_INPUT: {
        /* Use auth_action to get device input action */
        input = input_action(action);
        if (!input) {
            return -EINVAL;
        }
        /* Provisioner ouputs number/string and wait for device's Provisioning Input Complete PDU */
        link[i].expect = PROV_INPUT_COMPLETE;

        if (input == BT_MESH_ENTER_STRING) {
            unsigned char str[9];
            u8_t j;

            bt_rand(str, size);
            /* Normalize to '0' .. '9' & 'A' .. 'Z' */
            for (j = 0U; j < size; j++) {
                str[j] %= 36;
                if (str[j] < 10) {
                    str[j] += '0';
                } else {
                    str[j] += 'A' - 10;
                }
            }
            str[size] = '\0';

            memcpy(link[i].auth, str, size);
            memset(link[i].auth + size, 0, PROV_AUTH_VAL_SIZE - size);
            return provisioner->prov_output_num(input, str, size, i);
        } else {
            u32_t div[8] = { 10, 100, 1000, 10000, 100000, 
							1000000, 10000000, 100000000 };
            u32_t num;

            bt_rand(&num, sizeof(num));
            num %= div[size - 1];

            sys_put_be32(num, &link[i].auth[12]);
            memset(link[i].auth, 0, 12);
            return provisioner->prov_output_num(input, &num, size, i);
        }		
    }

    default:
        return -EINVAL;
    }
}

static void send_confirm(void)
{
    struct os_mbuf *buf = PROV_BUF(17);
    int i = prov_ctx.pb_index;

    BT_INFO("ConfInputs[0]   %s", bt_hex(link[i].conf_inputs, 64));
    BT_INFO("ConfInputs[64]  %s", bt_hex(link[i].conf_inputs + 64, 64));
    BT_INFO("ConfInputs[128] %s", bt_hex(link[i].conf_inputs + 128, 17));

    if (bt_mesh_prov_conf_salt(link[i].conf_inputs, link[i].conf_salt)) {
        BT_ERR("Unable to generate confirmation salt");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    BT_INFO("ConfirmationSalt: %s", bt_hex(link[i].conf_salt, 16));

    if (bt_mesh_prov_conf_key(link[i].dhkey, link[i].conf_salt, link[i].conf_key)) {
        BT_ERR("Unable to generate confirmation key");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    BT_INFO("ConfirmationKey: %s", bt_hex(link[i].conf_key, 16));

    /** Provisioner use the same random number for each provisioning
     *  device, if different random need to be used, here provisioner
     *  should allocate memory for rand and call bt_rand() every time.
     */
    if (!(prov_ctx.pub_key_rand_done & BIT(1))) {
        if (bt_rand(prov_ctx.random, 16)) {
            BT_ERR("Unable to generate random number");
			close_link(i, CLOSE_REASON_FAILED);
			goto done;
        }
        link[i].rand = prov_ctx.random;
        prov_ctx.pub_key_rand_done |= BIT(1);
    } else {
        /* Provisioner random has already been generated. */
        link[i].rand = prov_ctx.random;
    }

    BT_INFO("LocalRandom: %s", bt_hex(link[i].rand, 16));

	BT_INFO("ConfAuth: %s", bt_hex(link[i].auth, 16));

    prov_buf_init(buf, PROV_CONFIRM);

	
    if (bt_mesh_prov_conf(link[i].conf_key, link[i].rand, link[i].auth,
                          net_buf_simple_add(buf, 16))) {
        BT_ERR("Unable to generate confirmation value");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    if (prov_send(buf)) {
        BT_ERR("Failed to send Provisioning Confirm");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    link[i].expect = PROV_CONFIRM;

done:
	os_mbuf_free_chain(buf);
}

static void prov_dh_key_cb(const u8_t key[32])
{
    int i = prov_ctx.pb_index;

    BT_DBG("%p", key);

    if (!key) {
        BT_ERR("DHKey generation failed");
		close_link(i, CLOSE_REASON_FAILED);
		return;
    }

    sys_memcpy_swap(link[i].dhkey, key, 32);

    BT_DBG("DHkey: %s", bt_hex(link[i].dhkey, 32));

    atomic_set_bit(link[i].flags, HAVE_DHKEY);

    /** After dhkey is generated, if auth_method is No OOB or
     *  Static OOB, provisioner can start to send confirmation.
     *  If output OOB is used by the device, provisioner need
     *  to watch out the output number and input it as auth_val.
     *  If input OOB is used by the device, provisioner need
     *  to output a value, and wait for prov input complete pdu.
     */
    if (prov_auth(link[i].auth_method,
                  link[i].auth_action, link[i].auth_size) < 0) {
		close_link(i, CLOSE_REASON_FAILED);
		return;
    }

    if (link[i].expect != PROV_INPUT_COMPLETE) {
        send_confirm();
    }
}

static void prov_gen_dh_key(void)
{
    u8_t pub_key[64];
    int i = prov_ctx.pb_index;

    /* Copy device public key in little-endian for bt_dh_key_gen().
     * X and Y halves are swapped independently.
     */
    sys_memcpy_swap(&pub_key[0], &link[i].conf_inputs[81], 32);
    sys_memcpy_swap(&pub_key[32], &link[i].conf_inputs[113], 32);

    if (bt_dh_key_gen(pub_key, prov_dh_key_cb)) {
        BT_ERR("Failed to generate DHKey");
        close_link(i, CLOSE_REASON_FAILED);
        return;
    }
}

static void send_pub_key(u8_t oob)
{
    struct os_mbuf *buf = PROV_BUF(65);
    const u8_t *key = NULL;
    int i = prov_ctx.pb_index;

    if (!(prov_ctx.pub_key_rand_done & BIT(0))) {
        key = bt_pub_key_get();
        if (!key) {
            BT_ERR("No public key available");
            close_link(i, CLOSE_REASON_FAILED);
            goto done;
        }

        /** For provisioner, once public key is generated, just store
         *  public key in prov_ctx, and no need to generate the public
         *  key during the provisioning of other devices.
         */
        memcpy(prov_ctx.public_key, key, 64);
        prov_ctx.pub_key_rand_done |= BIT(0);
    } else {
        /* Provisioner public key has already been generated */
        key = prov_ctx.public_key;
		BT_ERR("Local Public Key: %s", bt_hex(key, 64));
    }

    atomic_set_bit(link[i].flags, LOCAL_PUB_KEY);

    prov_buf_init(buf, PROV_PUB_KEY);

    /* Swap X and Y halves independently to big-endian */
    sys_memcpy_swap(net_buf_simple_add(buf, 32), key, 32);
    sys_memcpy_swap(net_buf_simple_add(buf, 32), &key[32], 32);

    /* Store provisioner public key value in conf_inputs */
    memcpy(&link[i].conf_inputs[17], &buf->om_data[1], 64);

    if (prov_send(buf)) {
        BT_ERR("Failed to send public key");
        close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    if (!oob) {
        link[i].expect = PROV_PUB_KEY;
    } else {
        /** Have already got device public key. If next is to
         *  send confirm(not wait for input complete), need to
         *  wait for transactiona ack for public key then send
         *  provisioning confirm pdu.
         */
#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
        if (i < CONFIG_BT_MESH_PBA_SAME_TIME) {
            link[i].expect_ack_for = PROV_PUB_KEY;
            goto done;
        }
#endif /* CONFIG_BT_MESH_PB_ADV */
		if(atomic_test_and_clear_bit(link[i].flags, REMOTE_PUB_KEY)){
			prov_gen_dh_key();
		}else{
			atomic_set_bit(link[i].flags, WAIT_GEN_DHKEY);
		}
    }

done:
	os_mbuf_free_chain(buf);
}

static void prov_pub_key(const u8_t *data)
{
    int i = prov_ctx.pb_index;

    BT_DBG("Remote Public Key: %s", bt_hex(data, 64));

    /* Make sure received pdu is ok and cancel the timeout timer */
    if (atomic_test_and_clear_bit(link[i].flags, TIMEOUT_START)) {
        k_delayed_work_cancel(&link[i].timeout);
    }

    memcpy(&link[i].conf_inputs[81], data, 64);

    if (!atomic_test_bit(link[i].flags, LOCAL_PUB_KEY)) {
        /* Clear retransmit timer */
#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
        prov_clear_tx(i);
#endif
        atomic_set_bit(link[i].flags, REMOTE_PUB_KEY);
        BT_WARN("Waiting for local public key");
        return;
    }

    prov_gen_dh_key();
}

static void prov_input_complete(const u8_t *data)
{
    int i = prov_ctx.pb_index;

    BT_DBG("input complete");

    /* Make sure received pdu is ok and cancel the timeout timer */
    if (atomic_test_and_clear_bit(link[i].flags, TIMEOUT_START)) {
        k_delayed_work_cancel(&link[i].timeout);
    }

    /* Provisioner receives input complete and send confirm */
    send_confirm();
}

static void prov_confirm(const u8_t *data)
{
    /** Here Zephyr uses PROV_BUF(16). Currently test with PROV_BUF(16)
     *  and PROV_BUF(17) on branch feature/btdm_ble_mesh_debug both
     *  work fine.
     */
    struct os_mbuf *buf = PROV_BUF(17);
    int i = prov_ctx.pb_index;

    BT_DBG("Remote Confirm: %s", bt_hex(data, 16));

    /* Make sure received pdu is ok and cancel the timeout timer */
    if (atomic_test_and_clear_bit(link[i].flags, TIMEOUT_START)) {
        k_delayed_work_cancel(&link[i].timeout);
    }

    memcpy(link[i].conf, data, 16);

    if (!atomic_test_bit(link[i].flags, HAVE_DHKEY)) {
#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
        prov_clear_tx(i);
#endif
        atomic_set_bit(link[i].flags, SEND_CONFIRM);
    }

    prov_buf_init(buf, PROV_RANDOM);

    net_buf_simple_add_mem(buf, link[i].rand, 16);

    if (prov_send(buf)) {
        BT_ERR("Failed to send Provisioning Random");
        close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    link[i].expect = PROV_RANDOM;

done:
	os_mbuf_free_chain(buf);

}

static void send_prov_data(void)
{
    struct os_mbuf *buf = PROV_BUF(34);
    const u8_t *netkey = NULL;
    int   i = prov_ctx.pb_index;
    int   j, err;
    bool  already_flag = false;
    u8_t  session_key[16];
    u8_t  nonce[13];
    u8_t  pdu[25];
    u16_t max_addr;

    err = bt_mesh_session_key(link[i].dhkey, link[i].prov_salt, session_key);
    if (err) {
        BT_ERR("Unable to generate session key");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }
    BT_DBG("SessionKey: %s", bt_hex(session_key, 16));

    err = bt_mesh_prov_nonce(link[i].dhkey, link[i].prov_salt, nonce);
    if (err) {
        BT_ERR("Unable to generate session nonce");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }
    BT_DBG("Nonce: %s", bt_hex(nonce, 13));

    /** Assign provisioning data for the device. Currently all provisioned devices
     *  will be added to primary subnet, and may add API to let users choose which
     *  subnet will the device be provisioned to later.
     */

    netkey = provisioner_net_key_get(prov_ctx.curr_net_idx);
    if (!netkey) {
        BT_ERR("Unable to get netkey for provisioning data");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }
    memcpy(pdu, netkey, 16);
    sys_put_be16(prov_ctx.curr_net_idx, &pdu[16]);
    pdu[18] = prov_ctx.curr_flags;
    sys_put_be32(prov_ctx.curr_iv_index, &pdu[19]);

    /** 1. Check if this device is a previously deleted device,
     *  or a device has bot been deleted but been reset, if
     *  so, reuse the unicast address assigned to this device
     *  before, see Mesh Spec Provisioning 5.4.2.5.
     *  2. The Provisioner must not reuse unicast addresses
     *  that have been allocated to a device and sent in a
     *  Provisioning Data PDU until the Provisioner receives
     *  an Unprovisioned Device beacon or Service Data for
     *  the Mesh Provisioning Service from that same device,
     *  identified using the Device UUID of the device.
     *  3. And once the provisioning data for the device has
     *  been sent, we will add the data sent to this device
     *  into the already_prov_info structure.
     *  4. Currently we don't deal with a situation which is:
     *  a device is re-provisioned, but the element num has
     *  changed.
     */
    /* Check if this device is a re-provisioned device */
    for (j = 0; j < BT_MESH_ALREADY_PROV_NUM; j++) {
        if (memcmp(link[i].uuid, prov_ctx.already_prov[j].uuid, 16) == 0) {
            already_flag = true;
            sys_put_be16(prov_ctx.already_prov[j].unicast_addr, &pdu[23]);
            link[i].unicast_addr = prov_ctx.already_prov[j].unicast_addr;
            break;
        }
    }

    max_addr = 0x7FFF;

    if (!already_flag) {
        /* If this device to be provisioned is a new device */
        if (!prov_ctx.current_addr) {
            BT_ERR("No unicast address can be assigned for this device");
			close_link(i, CLOSE_REASON_FAILED);
			goto done;
        }

        if (prov_ctx.current_addr + link[i].element_num - 1 > max_addr) {
            BT_ERR("Not enough unicast address for this device");
			close_link(i, CLOSE_REASON_FAILED);
			goto done;
        }

        sys_put_be16(prov_ctx.current_addr, &pdu[23]);
        link[i].unicast_addr = prov_ctx.current_addr;
    }

    prov_buf_init(buf, PROV_DATA);

    err = bt_mesh_prov_encrypt(session_key, nonce, pdu, net_buf_simple_add(buf, 33));
    if (err) {
        BT_ERR("Unable to encrypt provisioning data");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    }

    if (prov_send(buf)) {
        BT_ERR("Failed to send Provisioning Data");
		close_link(i, CLOSE_REASON_FAILED);
        goto done;
    } else {
        /** If provisioning data is sent successfully, add
         *  the assigned information into the already_prov_info
         *  structure if this device is new.
         *  Also, if send successfully, update the current_addr
         *  in prov_ctx structure.
         */
        if (!already_flag) {
            for (j = 0; j < BT_MESH_ALREADY_PROV_NUM; j++) {
                if (!prov_ctx.already_prov[j].element_num) {
                    memcpy(prov_ctx.already_prov[j].uuid, link[i].uuid, 16);
                    prov_ctx.already_prov[j].element_num  = link[i].element_num;
                    prov_ctx.already_prov[j].unicast_addr = link[i].unicast_addr;
                    break;
                }
            }

            /** We update the next unicast address to be assigned here because
             *  if provisioner is provisioning two devices at the same time, we
             *  need to assign the unicast address for them correctly. Hence we
             *  should not update the prov_ctx.current_addr after the proper
             *  provisioning complete pdu is received.
             */
            prov_ctx.current_addr += link[i].element_num;
            if (prov_ctx.current_addr > max_addr) {
                /* No unicast address will be used for further provisioning */
                prov_ctx.current_addr = 0x0000;
            }
        }
    }

    link[i].ki_flags = prov_ctx.curr_flags;
    link[i].iv_index = prov_ctx.curr_iv_index;
    link[i].expect = PROV_COMPLETE;
	
done:
	os_mbuf_free_chain(buf);
}

static void prov_random(const u8_t *data)
{
    u8_t conf_verify[16];
    int i = prov_ctx.pb_index;

    BT_DBG("Remote Random: %s", bt_hex(data, 16));

    if (bt_mesh_prov_conf(link[i].conf_key, data, link[i].auth, conf_verify)) {
        BT_ERR("Unable to calculate confirmation verification");
        goto fail;
    }

    if (memcmp(conf_verify, link[i].conf, 16)) {
        BT_ERR("Invalid confirmation value");
        BT_DBG("Received:   %s", bt_hex(link[i].conf, 16));
        BT_DBG("Calculated: %s",  bt_hex(conf_verify, 16));
        goto fail;
    }

    /*Verify received confirm is ok and cancel the timeout timer */
    if (atomic_test_and_clear_bit(link[i].flags, TIMEOUT_START)) {
        k_delayed_work_cancel(&link[i].timeout);
    }

    /** After provisioner receives provisioning random from device,
     *  and successfully check the confirmation, the following
     *  should be done:
     *  1. calculate prov_salt
     *  2. prepare provisioning data and send
     */
    if (bt_mesh_prov_salt(link[i].conf_salt, link[i].rand, data,
                          link[i].prov_salt)) {
        BT_ERR("Failed to generate provisioning salt");
        goto fail;
    }

    BT_DBG("ProvisioningSalt: %s", bt_hex(link[i].prov_salt, 16));

    send_prov_data();
    return;

fail:
    close_link(i, CLOSE_REASON_FAILED);
    return;
}

static void prov_complete(const u8_t *data)
{
    u8_t device_key[16];
    int i = prov_ctx.pb_index, j;
    int err, rm = 0;
    bool gatt_flag;

    /* Make sure received pdu is ok and cancel the timeout timer */
    if (atomic_test_and_clear_bit(link[i].flags, TIMEOUT_START)) {
        k_delayed_work_cancel(&link[i].timeout);
    }

    /** If provisioning complete is received, the provisioning device
     *  will be stored into the node_info structure and become a node
     *  within the mesh network
     */
    err = bt_mesh_dev_key(link[i].dhkey, link[i].prov_salt, device_key);
    if (err) {
        BT_ERR("Unable to generate device key");
        close_link(i, CLOSE_REASON_FAILED);
        return;
    }
	
    for (j = 0; j < CONFIG_BT_MESH_PROV_NODES_MAX; j++) {
        if (!prov_nodes[j].provisioned) {
            prov_nodes[j].provisioned  = true;
            prov_nodes[j].oob_info     = link[i].oob_info;
            prov_nodes[j].element_num  = link[i].element_num;
            prov_nodes[j].unicast_addr = link[i].unicast_addr;
            prov_nodes[j].net_idx  = prov_ctx.curr_net_idx;
            prov_nodes[j].flags        = link[i].ki_flags;
            prov_nodes[j].iv_index     = link[i].iv_index;
            prov_nodes[j].addr.type    = link[i].addr.type;
            memcpy(prov_nodes[j].addr.val, link[i].addr.val, 6);
            memcpy(prov_nodes[j].uuid, link[i].uuid, 16);
			memcpy(prov_nodes[j].dev_key, device_key, 16);
            break;
        }
    }

    prov_ctx.node_count++;

    if (i >= CONFIG_BT_MESH_PBG_SAME_TIME) {
        gatt_flag = true;
    } else {
        gatt_flag = false;
    }

    if (provisioner->prov_complete) {
        provisioner->prov_complete(j, prov_nodes[j].uuid, prov_nodes[j].unicast_addr,
                            prov_nodes[j].element_num, prov_nodes[j].net_idx, gatt_flag);
    }

    err = provisioner_dev_find(&link[i].addr, link[i].uuid, &rm);
    if (!err) {
        if (unprov_dev[rm].flags & RM_AFTER_PROV) {
            memset(&unprov_dev[rm], 0, sizeof(struct unprov_dev_queue));
            provisioner_unprov_dev_num_dec();
        }
    } else if (err == -ENODEV) {
        BT_DBG("Device is not found in queue");
    } else {
        BT_WARN("Remove device from queue failed");
    }

    close_link(i, CLOSE_REASON_SUCCESS);
}

static void prov_failed(const u8_t *data)
{
    int i =  prov_ctx.pb_index;

    BT_WARN("Error: 0x%02x", data[0]);

    close_link(i, CLOSE_REASON_FAILED);
}

static const struct {
    void (*func)(const u8_t *data);
    u16_t len;
} prov_handlers[] = {
    { prov_invite,         1  },
    { prov_capabilities,   11 },
    { prov_start,          5  },
    { prov_pub_key,        64 },
    { prov_input_complete, 0  },
    { prov_confirm,        16 },
    { prov_random,         16 },
    { prov_data,           33 },
    { prov_complete,       0  },
    { prov_failed,         1  },
};

static void close_link(int i, u8_t reason)
{
    if (i < CONFIG_BT_MESH_PBA_SAME_TIME) {
#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
        bearer_ctl_send(i, LINK_CLOSE, &reason, sizeof(reason));
#endif
    } else if (i >= CONFIG_BT_MESH_PBA_SAME_TIME &&
               i < BT_MESH_PROV_SAME_TIME) {
#if IS_ENABLED(CONFIG_BT_MESH_PB_GATT)
        if(link[i].conn_handle){
			//ble_gap_terminate(link[i].conn_handle, BLE_ERR_REM_USER_CONN_TERM);
			//GATTC CLOSE
		}
#endif
    } else {
        BT_ERR("Close link with unexpected link id");
    }
}

static void prov_send_fail_msg(u8_t err)
{
	struct os_mbuf *buf = PROV_BUF(2);

	prov_buf_init(buf, PROV_FAILED);
	net_buf_simple_add_u8(buf, err);
	prov_send(buf);
	os_mbuf_free_chain(buf);
}


static void prov_timeout(struct ble_npl_event *work)
{
    int i = (int)ble_npl_event_get_arg(work);

    BT_DBG("");

    close_link(i, CLOSE_REASON_TIMEOUT);
}


#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
static void prov_retransmit(struct ble_npl_event *work)
{
    int id = (int)ble_npl_event_get_arg(work);

    BT_DBG("");

    if (!atomic_test_bit(link[id].flags, LINK_ACTIVE)) {
        BT_WARN("Link not active");
        return;
    }

    if (k_uptime_get() - link[id].tx.start > TRANSACTION_TIMEOUT) {
        BT_WARN("Giving up transaction");
        close_link(id, CLOSE_REASON_TIMEOUT);
        return;
    }

    if (link[id].link_close & BIT(0)) {
        if (link[id].link_close >> 1 & 0x02) {
            reset_link(id, link[id].link_close >> 8);
            return;
        }
        link[id].link_close += BIT(1);
    }

    for (int i = 0; i < ARRAY_SIZE(link[id].tx.buf); i++) {
        struct os_mbuf *buf = link[id].tx.buf[i];

        if (!buf) {
            break;
        }

        if (BT_MESH_ADV(buf)->busy) {
            continue;
        }

        BT_DBG("%u bytes: %s", buf->om_len, bt_hex(buf->om_data, buf->om_len));

        if (i + 1 < ARRAY_SIZE(link[id].tx.buf) && link[id].tx.buf[i + 1]) {
            bt_mesh_adv_send(buf, NULL, NULL);
        } else {
            bt_mesh_adv_send(buf, &buf_sent_cb, (void *)id);
        }
    }
}

static void link_ack(struct prov_rx *rx, struct os_mbuf *buf)
{
    int i = prov_ctx.pb_index;

    BT_DBG("len %u", buf->om_len);

    if (buf->om_len) {
        BT_ERR("Link ack message length is wrong");
        close_link(i, CLOSE_REASON_FAILED);
        return;
    }

    if (link[i].expect == PROV_CAPABILITIES) {
        BT_DBG("Link ack already received");
        return;
    }

    /** After received link_ack, we don't call prov_clear_tx() to
     *  cancel retransmit timer, because retransmit timer will be
     *  cancelled after we send the provisioning invite pdu.
     */
    send_invite();
}

static void link_close(struct prov_rx *rx, struct os_mbuf *buf)
{
    u8_t reason;
    int i = prov_ctx.pb_index;

    BT_DBG("len %u", buf->om_len);

    reason = net_buf_simple_pull_u8(buf);

    reset_link(i, reason);
}

static void gen_prov_ctl(struct prov_rx *rx, struct os_mbuf *buf)
{
    int i = prov_ctx.pb_index;

    BT_DBG("op 0x%02x len %u i %u", BEARER_CTL(rx->gpc), buf->om_len, i);

    switch (BEARER_CTL(rx->gpc)) {
    case LINK_OPEN:
        break;

    case LINK_ACK:
        if (!atomic_test_bit(link[i].flags, LINK_ACTIVE)) {
            BT_INFO("flags return");
            return;
        }
        link_ack(rx, buf);
        break;

    case LINK_CLOSE:
        if (!atomic_test_bit(link[i].flags, LINK_ACTIVE)) {
            return;
        }
        link_close(rx, buf);
        break;

    default:
        BT_ERR("Unknown bearer opcode: 0x%02x", BEARER_CTL(rx->gpc));
        return;
    }
}

static void prov_msg_recv(void)
{
    int i = prov_ctx.pb_index;

    u8_t type = link[i].rx.buf->om_data[0];

    BT_DBG("type 0x%02x len %u", type, link[i].rx.buf->om_len);

    /** Provisioner first checks information of the received
     *  provisioing pdu, and once succeed, check the fcs
     */
    if (type != PROV_FAILED && type != link[i].expect) {
        BT_ERR("Unexpected msg 0x%02x != 0x%02x", type, link[i].expect);
		gen_prov_ack_send(link[i].rx.id);
		close_link(i, CLOSE_REASON_FAILED);
        return;
    }

    if (type >= 0x0A) {
        BT_ERR("Unknown provisioning PDU type 0x%02x", type);
		gen_prov_ack_send(link[i].rx.id);
		close_link(i, CLOSE_REASON_FAILED);
        return;
    }

    if (1 + prov_handlers[type].len != link[i].rx.buf->om_len) {
        BT_ERR("Invalid length %u for type 0x%02x", link[i].rx.buf->om_len, type);
		gen_prov_ack_send(link[i].rx.id);
		close_link(i, CLOSE_REASON_FAILED);
        return;
    }

    if (!bt_mesh_fcs_check(link[i].rx.buf, link[i].rx.fcs)) {
        BT_ERR("Incorrect FCS");
		close_link(i, CLOSE_REASON_FAILED);
        return;
    }

    gen_prov_ack_send(link[i].rx.id);
    link[i].rx.prev_id = link[i].rx.id;
    link[i].rx.id = 0;

    prov_handlers[type].func(&link[i].rx.buf->om_data[1]);
}

static void gen_prov_cont(struct prov_rx *rx, struct os_mbuf *buf)
{
    u8_t seg = CONT_SEG_INDEX(rx->gpc);
    int i = prov_ctx.pb_index;

    BT_DBG("len %u, seg_index %u", buf->om_len, seg);

    if (!link[i].rx.seg && link[i].rx.prev_id == rx->xact_id) {
        BT_WARN("Resending ack");
        gen_prov_ack_send(rx->xact_id);
        return;
    }

    if ((rx->xact_id != link[i].rx.id) && (link[i].rx.id != 0)) {
        BT_ERR("Data for unknown transaction (%u != %u)",
               rx->xact_id, link[i].rx.id);
        /** If provisioner receives a unknown tranaction id,
         *  currently we close the link.
         */
        goto fail;
    }
    else if (link[i].rx.id == 0) {
        BT_ERR("Not recv start packet, so not handle.");
        return;
    }

    if (seg > link[i].rx.last_seg) {
        BT_ERR("Invalid segment index %u", seg);
        goto fail;
    } else if (seg == link[i].rx.last_seg) {
        u8_t expect_len;

        expect_len = (link[i].rx.buf->om_len - 20 -
                      (23 * (link[i].rx.last_seg - 1)));
        if (expect_len != buf->om_len) {
            BT_ERR("Incorrect last seg len: %u != %u",
                   expect_len, buf->om_len);
            goto fail;
        }
    }

    if (!(link[i].rx.seg & BIT(seg))) {
        BT_WARN("Ignoring already received segment");
        return;
    }

    memcpy(XACT_SEG_DATA(i, seg), buf->om_data, buf->om_len);
    XACT_SEG_RECV(i, seg);

    if (!link[i].rx.seg) {
        prov_msg_recv();
    }
    return;

fail:
    close_link(i, CLOSE_REASON_FAILED);
    return;
}

static void gen_prov_ack(struct prov_rx *rx, struct os_mbuf *buf)
{
    int i = prov_ctx.pb_index;
    u8_t ack_type, pub_key_oob;

    BT_DBG("len %u", buf->om_len);

    if (!link[i].tx.buf[0]) {
        return;
    }

    if (!link[i].tx.id) {
        return;
    }

    if (rx->xact_id == (link[i].tx.id - 1)) {
        prov_clear_tx(i);

        ack_type = link[i].expect_ack_for;
		link[i].expect_ack_for = 0x00;
        switch (ack_type) {
        case PROV_START:
            pub_key_oob = link[i].conf_inputs[13];
            send_pub_key(pub_key_oob);
            break;
        case PROV_PUB_KEY:
			if(atomic_test_and_clear_bit(link[i].flags, REMOTE_PUB_KEY)){
				prov_gen_dh_key();
			}else{
				atomic_set_bit(link[i].flags, WAIT_GEN_DHKEY);
			}
            break;
        default:
            break;
        }
    }
}

static void gen_prov_start(struct prov_rx *rx, struct os_mbuf *buf)
{
    int i = prov_ctx.pb_index;
	u16_t trailing_space = 0;

    if (link[i].rx.seg) {
        BT_WARN("Got Start while there are unreceived segments");
        return;
    }

    if (link[i].rx.prev_id == rx->xact_id) {
        BT_WARN("Resending ack");
        gen_prov_ack_send(rx->xact_id);
        return;
    }
	
	trailing_space = OS_MBUF_TRAILINGSPACE(link[i].rx.buf);

    link[i].rx.buf->om_len = net_buf_simple_pull_be16(buf);
    link[i].rx.id  = rx->xact_id;
    link[i].rx.fcs = net_buf_simple_pull_u8(buf);

    BT_DBG("len %u last_seg %u total_len %u fcs 0x%02x", buf->om_len,
           START_LAST_SEG(rx->gpc), link[i].rx.buf->om_len, link[i].rx.fcs);

    /* Provisioner can not receive zero-length provisioning pdu */
    if (link[i].rx.buf->om_len < 1) {
        BT_ERR("Ignoring zero-length provisioning PDU");
        close_link(i, CLOSE_REASON_FAILED);
        return;
    }

    if (link[i].rx.buf->om_len > trailing_space) {
        BT_ERR("Too large provisioning PDU (%u bytes)", link[i].rx.buf->om_len);
        close_link(i, CLOSE_REASON_FAILED);
        return;
    }

    if (START_LAST_SEG(rx->gpc) > 0 && link[i].rx.buf->om_len <= 20) {
        BT_ERR("Too small total length for multi-segment PDU");
        close_link(i, CLOSE_REASON_FAILED);
        return;
    }

    link[i].rx.seg = (1 << (START_LAST_SEG(rx->gpc) + 1)) - 1;
    link[i].rx.last_seg = START_LAST_SEG(rx->gpc);
    memcpy(link[i].rx.buf->om_data, buf->om_data, buf->om_len);
    XACT_SEG_RECV(i, 0);

    if (!link[i].rx.seg) {
        prov_msg_recv();
    }
}

static const struct {
    void (*const func)(struct prov_rx *rx, struct os_mbuf *buf);
    const u8_t require_link;
    const u8_t min_len;
} gen_prov[] = {
    { gen_prov_start, true,  3 },
    { gen_prov_ack,   true,  0 },
    { gen_prov_cont,  true,  0 },
    { gen_prov_ctl,   true,  0 },
};

static void gen_prov_recv(struct prov_rx *rx, struct os_mbuf *buf)
{
    int i = prov_ctx.pb_index;

    if (buf->om_len < gen_prov[GPCF(rx->gpc)].min_len) {
        BT_ERR("Too short GPC message type %u", GPCF(rx->gpc));
        close_link(i, CLOSE_REASON_FAILED);
        return;
    }

    /** require_link flag can be used combined with link[].linking flag
     *  to set LINK_ACTIVE status after link_ack pdu is received.
     *  And if so, we shall not check LINK_ACTIVE status in the
     *  function find_link().
     */
    if (!atomic_test_bit(link[i].flags, LINK_ACTIVE) &&
            gen_prov[GPCF(rx->gpc)].require_link) {
        BT_DBG("Ignoring message that requires active link");
        return;
    }

    gen_prov[GPCF(rx->gpc)].func(rx, buf);
}

static int find_link(u32_t link_id, bool set)
{
    int i;

    /* link for PB-ADV is from 0 to CONFIG_BT_MESH_PBA_SAME_TIME */
    for (i = 0; i < CONFIG_BT_MESH_PBA_SAME_TIME; i++) {
        if (atomic_test_bit(link[i].flags, LINK_ACTIVE)) {
            if (link[i].id == link_id) {
                if (set) {
					prov_ctx.pb_index = i;
                }
                return 0;
            }
        }
    }

    return -1;
}

void provisioner_pb_adv_recv(struct os_mbuf *buf)
{
    struct prov_rx rx;
    int i;

    rx.link_id = net_buf_simple_pull_be32(buf);
    if (find_link(rx.link_id, true) < 0) {
        BT_ERR("Data for unexpected link");
        return;
    }

    i = prov_ctx.pb_index;

    if (buf->om_len < 2) {
        BT_ERR("Too short provisioning packet (len %u)", buf->om_len);
        close_link(i, CLOSE_REASON_FAILED);
        return;
    }

    rx.xact_id = net_buf_simple_pull_u8(buf);
    rx.gpc = net_buf_simple_pull_u8(buf);

    BT_DBG("link_id 0x%08x xact_id %u,gpc:%d", rx.link_id, rx.xact_id,rx.gpc);

    gen_prov_recv(&rx, buf);
}
#endif /* CONFIG_BT_MESH_PB_ADV */


#if IS_ENABLED(CONFIG_BT_MESH_PB_GATT)

static int find_conn(u16_t conn_handle, bool set)
{
    int i;

    /* link for PB-GATT is from CONFIG_BT_MESH_PBA_SAME_TIME to BT_MESH_PROV_SAME_TIME */
    for (i = CONFIG_BT_MESH_PBA_SAME_TIME; i < BT_MESH_PROV_SAME_TIME; i++) {
        if (atomic_test_bit(link[i].flags, LINK_ACTIVE)) {
            if (link[i].conn_handle == conn_handle) {
                if (set) {
                    prov_ctx.pb_index = i;
                }
                return true;
            }
        }
    }

    return false;
}

int provisioner_pb_gatt_recv(u16_t conn_handle, struct os_mbuf *buf)
{
    u8_t type;
    int i;

    BT_DBG("%u bytes: %s", buf->om_len, bt_hex(buf->om_data, buf->om_len));

    if (!find_conn(conn_handle, true)) {
        BT_ERR("Data for unexpected connection");
        return -ENOTCONN;
    }

    i = prov_ctx.pb_index;

    if (buf->om_len < 1) {
        BT_ERR("Too short provisioning packet (len %u)", buf->om_len);
        goto fail;
    }

    type = net_buf_simple_pull_u8(buf);
    if (type != PROV_FAILED && type != link[i].expect) {
        BT_ERR("Unexpected msg 0x%02x != 0x%02x", type, link[i].expect);
        goto fail;
    }

    if (type >= 0x0A) {
        BT_ERR("Unknown provisioning PDU type 0x%02x", type);
        goto fail;
    }

    if (prov_handlers[type].len != buf->om_len) {
        BT_ERR("Invalid length %u for type 0x%02x", buf->om_len, type);
        goto fail;
    }

    prov_handlers[type].func(buf->om_data);

    return 0;

fail:
    /* Mesh Spec Section 5.4.4 Provisioning errors */
    close_link(i, CLOSE_REASON_FAILED);
    return -EINVAL;
}

int provisioner_set_prov_conn(const u8_t addr[6], u16 handle)
{
    int i;

    if (!addr || !handle) {
        BT_ERR("%s, Invalid parameter", __func__);
        return -EINVAL;
    }

     for (i = 0; i < CONFIG_BT_MESH_PBG_SAME_TIME; i++) {
        if (!memcmp(link[i].addr.val, addr, 6)) {
            return 0;
        }
    }

    BT_ERR("Addr %s not found", bt_hex(addr, 6));
    return -ENOMEM;

}

int provisioner_pb_gatt_open(u16_t conn_handle, u8_t *addr)
{
    int i, id = 0;

    /** Double check if the device is currently being provisioned
     *  using PB-ADV.
     *  Provisioner binds conn with proper device when
     *  proxy_prov_connected() is invoked, and here after proper GATT
     *  procedures are completed, we just check if this conn already
     *  exists in the proxy servers array.
     */
    for (i = 0; i < BT_MESH_PROV_SAME_TIME; i++) {
        if (link[i].conn_handle == conn_handle) {
            id = i;
            break;
        }
    }
    //printf("provisioner_pb_gatt_open:i:%x,id:%x,link[i].conn_handle:%x\n",i,id,link[i].conn_handle);
    if (i == BT_MESH_PROV_SAME_TIME) {
        BT_ERR("No link found");
        return -ENOTCONN;
    }

	prov_ctx.pb_index = id;

    for (i = 0; i < CONFIG_BT_MESH_PBA_SAME_TIME; i++) {
        if (atomic_test_bit(link[i].flags, LINK_ACTIVE)) {
            if (!memcmp(link[i].uuid, link[id].uuid, 16)) {
                BT_ERR("Provision using PB-GATT & PB-ADV same time");
                close_link(id, CLOSE_REASON_FAILED);
                return -EALREADY;
            }
        }
    }

    atomic_set_bit(link[id].flags, LINK_ACTIVE);
    link[id].conn_handle = conn_handle;

    /* May use lcd to indicate starting provisioning each device */
    if (provisioner->prov_link_open) {
        provisioner->prov_link_open(BT_MESH_PROV_GATT);
    }

    send_invite();

    return 0;
}

static void prov_reset_pbg_link(int i)
{
    memset(&link[i], 0, offsetof(struct prov_link, timeout));
	BT_INFO(" link(%d) reset",i);
}

int provisioner_pb_gatt_close(u16_t conn_handle, u8_t reason)
{
    bool pub_key;
    int i;

    if (!find_conn(conn_handle, true)) {
        BT_ERR("Not connected");
        return -ENOTCONN;
    }

    if (provisioner->prov_link_close) {
        provisioner->prov_link_close(BT_MESH_PROV_GATT, reason);
    }

    i = prov_ctx.pb_index;
    //link[i].conn_handle = 0;//clear handle
    if (atomic_test_and_clear_bit(link[i].flags, TIMEOUT_START)) {
        k_delayed_work_cancel(&link[i].timeout);
    }

    pub_key = atomic_test_bit(link[i].flags, LOCAL_PUB_KEY);

    prov_reset_pbg_link(i);

    if (pub_key) {
        atomic_set_bit(link[i].flags, LOCAL_PUB_KEY);
    }

    return 0;
}
#endif /* CONFIG_BT_MESH_PB_GATT */

static int provisioner_prov_initd = false;
int provisioner_prov_init(const struct bt_mesh_provisioner *provisioner_info)
{
    int i,rc;
  
	if(provisioner_prov_initd){
		return 0;
	}


    if (!provisioner_info) {
        BT_ERR("No provisioning context provided");
        return -EINVAL;
    }

    if (CONFIG_BT_MESH_PBG_SAME_TIME > CONFIG_BT_MAX_CONN) {
        BT_ERR("PBG same time exceed max connection");
        return -EINVAL;
    }

    provisioner = provisioner_info;

#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
    for (i = 0; i < CONFIG_BT_MESH_PBA_SAME_TIME; i++) {
        link[i].pending_ack = XACT_NVAL;
        k_delayed_work_init(&link[i].tx.retransmit, prov_retransmit);
		k_delayed_work_add_arg(&link[i].tx.retransmit, (void *)i);
        link[i].rx.prev_id = XACT_NVAL;
		rx_buf[i] = NET_BUF_SIMPLE(65);
		net_buf_simple_init(rx_buf[i], 0);
        link[i].rx.buf = rx_buf[i];
    }
#endif

    for (i = 0; i < BT_MESH_PROV_SAME_TIME; i++) {
        k_delayed_work_init(&link[i].timeout, prov_timeout);
		k_delayed_work_add_arg(&link[i].timeout, (void *)i);
    }

    /* for PB-GATT, use servers[] array in proxy_provisioner.c */

    prov_ctx.current_addr = provisioner->prov_start_address;
    prov_ctx.curr_net_idx = BT_MESH_KEY_PRIMARY;
    prov_ctx.curr_flags = provisioner->flags;
    prov_ctx.curr_iv_index = provisioner->iv_index;

#if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && MYNEWT_VAL(BLE_MESH_PB_GATT)) || \
					MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)
		rc = ble_npl_mutex_init(&pb_gatt_lock);
		bt_mesh_gattc_init();
#endif

	provisioner_prov_initd = true;

    return 0;
}

int provisioner_prov_deinit(void)
{
	int i;
	
	provisioner = NULL;
	
	for(i=0; i<CONFIG_BT_MESH_PBA_SAME_TIME; i++){
		k_delayed_work_free(&link[i].tx.retransmit);
		free_segments(i);
		os_mbuf_free_chain(rx_buf[i]);	
	}
	
	for(i=0; i<BT_MESH_PROV_SAME_TIME; i++){
		k_delayed_work_free(&link[i].timeout);
		memset(&link[i], 0, sizeof(struct prov_link));
	}

#if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && MYNEWT_VAL(BLE_MESH_PB_GATT)) || \
						MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)
	ble_npl_mutex_free(&pb_gatt_lock);
	bt_mesh_gattc_deinit();
#endif

	memset(&prov_ctx, 0, sizeof(struct prov_ctx_t));
	memset(&unprov_dev, 0, sizeof(struct unprov_dev_queue));
	provisioner_all_nodes_reset();

	provisioner_prov_initd = false;
	return 0;
}


void bt_mesh_provisioner_reset_link(void) {
	int i;
#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
	for (i = 0; i < CONFIG_BT_MESH_PBA_SAME_TIME; i++) {
		net_buf_simple_init(rx_buf[i], 0);
		link[i].rx.buf = rx_buf[i];
	}
#endif
}


static void provisioner_unprov_beacon_recv(struct os_mbuf *buf, bt_addr_le_t *addr)
{
#if IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
    u8_t *dev_uuid = NULL;
    u16_t oob_info;
    int i, res;

    if (buf->om_len != 0x12 && buf->om_len != 0x16) {
        BT_ERR("Unprovisioned device beacon with wrong length");
        return;
    }

    if (prov_ctx.pba_count == CONFIG_BT_MESH_PBA_SAME_TIME) {
        BT_WARN("Current PB-ADV devices reach max limit");
        return;
    }

    dev_uuid = buf->om_data;
	BT_DBG("dev_uuid:%s", bt_hex(dev_uuid, 16));
    if (provisioner_dev_uuid_match(dev_uuid)) {
        BT_DBG("provisioner_unprov_beacon_recv");
        return;
    }

    /* Check if the device with this dev_uuid has already been provisioned. */
    for (i = 0; i < CONFIG_BT_MESH_PROV_NODES_MAX; i++) {
        if (prov_nodes[i].provisioned) {
            /* May also need to check device address and address type */
            if (!memcmp(prov_nodes[i].uuid, dev_uuid, 16)) {
                BT_WARN("Provisioned before, start to provision again");
                provisioner_node_reset(i);
                break;
            }
        }
    }

    if (prov_ctx.node_count == CONFIG_BT_MESH_PROV_NODES_MAX) {
        BT_WARN("Current provisioned devices reach max limit");
        return;
    }

    /* Check if this device is currently being provisioned */
    for (i = 0; i < BT_MESH_PROV_SAME_TIME; i++) {
        if (atomic_test_bit(link[i].flags, LINK_ACTIVE)) {
            if (!memcmp(link[i].uuid, dev_uuid, 16)) {
                //BT_DBG("This device is currently being provisioned");
                return;
            }
        }
    }

    if (prov_ctx.prov_after_match == false) {

        BT_ERR("RUN prov_ctx.prov_after_match == false 200811\n");  //
        prov_ctx.prov_after_match = true;                           //add_provisioner_supported

        res = provisioner_dev_find(addr, dev_uuid, &i);
        if (res) {
            BT_DBG("Device not found, notify to upper layer");
            /* Invoke callback and notify to upper layer */
            net_buf_simple_pull(buf, 16);
            oob_info = net_buf_simple_pull_be16(buf);
            #if 0
            if (adv_pkt_notify && addr) {
                adv_pkt_notify(addr->a.val, addr->type, BT_LE_ADV_NONCONN_IND, dev_uuid, oob_info, PROV_ADV);
            }
            #endif
            struct bt_mesh_unprov_dev_add add_dev;
            u8_t flags;
            memcpy(add_dev.addr, addr->val, 6);
            add_dev.addr_type   = addr->type;
            add_dev.bearer      = PROV_ADV;
            add_dev.oob_info    = oob_info;
            memcpy(add_dev.uuid, dev_uuid, 16);

            flags = RM_AFTER_PROV | START_PROV_NOW | FLUSHABLE_DEV;

            bt_mesh_provisioner_add_unprov_dev(&add_dev, flags);
            return;
        }

        if (!(unprov_dev[i].bearer & PROV_ADV)) {
            BT_DBG("Do not support pb-adv");
            return;
        }
    }

    /* Mesh beacon uses big-endian to send beacon data */
    for (i = 0; i < CONFIG_BT_MESH_PBA_SAME_TIME; i++) {
        if (!atomic_test_bit(link[i].flags, LINK_ACTIVE) && !link[i].linking) {
            memcpy(link[i].uuid, dev_uuid, 16);
            net_buf_simple_pull(buf, 16);
            link[i].oob_info = net_buf_simple_pull_be16(buf);
            if (addr) {
                link[i].addr.type = addr->type;
                memcpy(link[i].addr.val, addr->val, 6);
            }
            break;
        }
    }

    if (i == CONFIG_BT_MESH_PBA_SAME_TIME) {
        return;
    }

    /** Once unprovisioned device beacon is received, and previous
     *  checks are all passed, we can send link_open
     */
	prov_ctx.pb_index = i;

    send_link_open();

    if (provisioner->prov_link_open) {
        provisioner->prov_link_open(BT_MESH_PROV_ADV);
    }

    /** If provisioner sets LINK_ACTIVE bit once link_open is sent, here
     *  we may not need to use linking flag(like PB-GATT) to prevent
     *  the stored device information(like UUID, oob_info) been replaced
     *  by other received unprovisioned device beacon.
     *  But if provisioner sets LINK_ACTIVE bit after link_ack pdu is
     *  received, we need to use linking flag to prevent information-
     *  been replaced issue.
     *  Currently we set LINK_ACTIVE after we send link_open pdu
     */
    link[i].linking = 1;

#endif /* CONFIG_BT_MESH_PB_ADV */
}

bool provisioner_flags_match(struct os_mbuf *buf)
{
    u8_t flags;

    if (buf->om_len != 1) {
        BT_DBG("Unexpected flags length");
        return false;
    }

    flags = net_buf_simple_pull_u8(buf);
	
	BT_DBG("Received adv pkt with flags: 0x%02x", flags);

    return true;
}

static void provisioner_secure_beacon_recv(struct os_mbuf *buf, bt_addr_le_t *addr)
{
    // TODO: Provisioner receive and handle Secure Beacon
}

void provisioner_beacon_recv(struct os_mbuf *buf, bt_addr_le_t *addr)
{
    u8_t type;

    BT_DBG("%u bytes: %s", buf->om_len, bt_hex(buf->om_data, buf->om_len));

    if (buf->om_len < 1) {
        BT_ERR("Too short beacon");
        return;
    }

    type = net_buf_simple_pull_u8(buf);
    switch (type) {
    case BEACON_TYPE_UNPROVISIONED:
        BT_INFO("Unprovisioned device beacon received");
        provisioner_unprov_beacon_recv(buf, addr);
        break;
    case BEACON_TYPE_SECURE:
        secure_beacon_recv(buf);
        break;
    default:
        BT_WARN("Unknown beacon type 0x%02x", type);
        break;
    }
}

bool provisioner_is_node_provisioned(u8_t *dev_addr)
{
    int i;
    struct bt_mesh_prov_node_info *node = NULL;

    for (i = 0; i < ARRAY_SIZE(prov_nodes); i++) {
        node = &prov_nodes[i];
        if (!node->provisioned || !BT_MESH_ADDR_IS_UNICAST(node->unicast_addr)) {
            continue;
        }

        if (!memcmp(dev_addr, node->addr.val, 6)) {
            return true;
        }
    }

    return false;
}

int bt_mesh_prov_input_data(u8_t *num, u8_t size, bool num_flag)
{
    /** This function should be called in the prov_input_num
     *  callback, after the data output by device has been
     *  input by provisioner.
     *  Paramter size is used to indicate the length of data
     *  indicated by Pointer num, for example, if device output
     *  data is 12345678(decimal), the data in auth value will
     *  be 0xBC614E.
     *  Parameter num_flag is used to indicate whether the value
     *  input by provisioner is number or string.
     */
    int i = prov_ctx.pb_index;
	
	memset(link[i].auth, 0, 16);
    if (num_flag) {
        /* Provisioner input number */
        memcpy(link[i].auth + 12, num, sizeof(u32_t));
    } else {
        /* Provisioner input string */
        memcpy(link[i].auth, num, size);
    }
	
	send_confirm();
    return 0;
}

int bt_mesh_prov_oob_pub_key(const u8_t pub_key_x[32], const u8_t pub_key_y[32])
{
	int i = prov_ctx.pb_index;
	
    /* Swap X and Y halves independently to big-endian */
    sys_memcpy_swap(&link[i].conf_inputs[81], pub_key_x, 32);
    sys_memcpy_swap(&link[i].conf_inputs[81] + 32, pub_key_y, 32);

    atomic_set_bit(link[i].flags, REMOTE_PUB_KEY);
    if (atomic_test_and_clear_bit(link[i].flags, WAIT_GEN_DHKEY)) {
        prov_gen_dh_key();
    }

    return 0;
}

int bt_mesh_provisioner_init(void)
{
    struct bt_mesh_subnet *sub = NULL;
    u8_t p_key[16] = {0};

    BT_DBG("");

    comp = bt_mesh_comp_get();
    if (!comp) {
        BT_ERR("Provisioner comp is NULL");
        return -EINVAL;
    }

    provisioner = provisioner_get_prov_info();
    if (!provisioner) {
        BT_ERR("Provisioner context is NULL");
        return -EINVAL;
    }

    /* If the device only acts as a Provisioner, need to initialize
       each element's address. */
    bt_mesh_comp_provision(provisioner->prov_unicast_addr);

    /* Generate the primary netkey */
    if (bt_rand(p_key, 16)) {
        BT_ERR("Create primary netkey fail");
        return -EIO;
    }

    sub = &bt_mesh.p_sub[0];

    sub->kr_flag = BT_MESH_KEY_REFRESH(provisioner->flags);
    if (sub->kr_flag) {
        if (bt_mesh_net_keys_create(&sub->keys[1], p_key)) {
            BT_ERR("Create net_keys fail");
            sub->subnet_active = false;
            return -EIO;
        }
        sub->kr_phase = BT_MESH_KR_PHASE_2;
    } else {
        if (bt_mesh_net_keys_create(&sub->keys[0], p_key)) {
            BT_ERR("Create net_keys fail");
            sub->subnet_active = false;
            return -EIO;
        }
        sub->kr_phase = BT_MESH_KR_NORMAL;
    }
    sub->net_idx = BT_MESH_KEY_PRIMARY;
    sub->node_id = BT_MESH_NODE_IDENTITY_NOT_SUPPORTED;

    sub->subnet_active = true;

    /* Dynamically added appkey & netkey will use these key_idx */
    bt_mesh.p_app_idx_next = 0x0000;
    bt_mesh.p_net_idx_next = 0x0001;

    /* In this function, we use the values of struct bt_mesh_prov
       which has been initialized in the application layer */
    bt_mesh.iv_index  = provisioner->iv_index;
    bt_mesh.iv_update = BT_MESH_IV_UPDATE(provisioner->flags);

    /* Set initial IV Update procedure state time-stamp */
    bt_mesh.last_update = k_uptime_get();

    BT_DBG("kr_flag: %d, kr_phase: %d, net_idx: 0x%02x, node_id %d",
           sub->kr_flag, sub->kr_phase, sub->net_idx, sub->node_id);
    BT_DBG("netkey:     %s, nid: 0x%x", bt_hex(sub->keys[0].net, 16), sub->keys[0].nid);
    BT_DBG("enckey:     %s", bt_hex(sub->keys[0].enc, 16));
    BT_DBG("network id: %s", bt_hex(sub->keys[0].net_id, 8));
#if IS_ENABLED(CONFIG_BT_MESH_GATT_PROXY)
    BT_DBG("identity:   %s", bt_hex(sub->keys[0].identity, 16));
#endif
    BT_DBG("privacy:    %s", bt_hex(sub->keys[0].privacy, 16));
    BT_DBG("beacon:     %s", bt_hex(sub->keys[0].beacon, 16));

    return 0;
}

int bt_mesh_provisioner_add_unprov_dev(struct bt_mesh_unprov_dev_add *add_dev, u8_t flags)
{
    bt_addr_le_t add_addr = {0};
    u8_t zero[16] = {0};
    int addr_cmp = 0, uuid_cmp = 0;
    int i, err = 0;

    if (!add_dev) {
        BT_ERR("add_dev is NULL");
        return -EINVAL;
    }

    addr_cmp = memcmp(add_dev->addr, zero, 6);
    uuid_cmp = memcmp(add_dev->uuid, zero, 16);

    if (add_dev->bearer == 0x0 || ((uuid_cmp == 0) &&
                                   ((addr_cmp == 0) || add_dev->addr_type > BLE_ADDR_RANDOM))) {
        BT_ERR("Invalid parameters");
        return -EINVAL;
    }

	BT_DBG("add_dev->bearer add_dev->bearer add_dev->bearer: %02x", add_dev->bearer);

    if ((add_dev->bearer & BT_MESH_PROV_ADV) && (add_dev->bearer & BT_MESH_PROV_GATT) &&
            (flags & START_PROV_NOW)) {
        BT_ERR("Can not start both PB-ADV & PB-GATT provision");
        return -EINVAL;
    }

    if ((uuid_cmp == 0) && (flags & START_PROV_NOW)) {
        BT_ERR("Can not start provisioning with zero uuid");
        return -EINVAL;
    }

    if ((add_dev->bearer & BT_MESH_PROV_GATT) && (flags & START_PROV_NOW) &&
            ((addr_cmp == 0) || add_dev->addr_type > BLE_ADDR_RANDOM)) {
        BT_ERR("PB-GATT with invalid device address");
        return -EINVAL;
    }

    if (add_dev->bearer & BT_MESH_PROV_GATT) {
#if !IS_ENABLED(CONFIG_BT_MESH_PB_GATT)
        BT_ERR("Do not support PB-GATT");
        return -EINVAL;
#endif
    }

    if (add_dev->bearer & BT_MESH_PROV_ADV) {
#if !IS_ENABLED(CONFIG_BT_MESH_PB_ADV)
        BT_ERR("Do not support PB-ADV");
        return -EINVAL;
#endif
    }

    add_addr.type = add_dev->addr_type;
    memcpy(add_addr.val, add_dev->addr, 6);

    err = provisioner_dev_find(&add_addr, add_dev->uuid, &i);
    if (err == -EINVAL) {
        BT_ERR("Invalid parameters");
        return err;
    } else if (err == 0) {
        if (!(add_dev->bearer & unprov_dev[i].bearer)) {
            BT_WARN("Add device with only bearer updated");
            unprov_dev[i].bearer |= add_dev->bearer;
        } else {
            BT_WARN("Device already exists");
        }
        goto start;
    }

    for (i = 0; i < ARRAY_SIZE(unprov_dev); i++) {
        if (unprov_dev[i].bearer) {
            continue;
        }
        if (addr_cmp && (add_dev->addr_type <= BLE_ADDR_RANDOM)) {
            unprov_dev[i].addr.type = add_dev->addr_type;
            memcpy(unprov_dev[i].addr.val, add_dev->addr, 6);
        }
        if (uuid_cmp) {
            memcpy(unprov_dev[i].uuid, add_dev->uuid, 16);
        }
        unprov_dev[i].bearer = add_dev->bearer & BIT_MASK(2);
        unprov_dev[i].flags  = flags & BIT_MASK(3);
        goto start;
    }

    /* If queue is full, find flushable device and replace it */
    for (i = 0; i < ARRAY_SIZE(unprov_dev); i++) {
        if (unprov_dev[i].flags & FLUSHABLE_DEV) {
            memset(&unprov_dev[i], 0, sizeof(struct unprov_dev_queue));
            if (addr_cmp && (add_dev->addr_type <= 0x01)) {
                unprov_dev[i].addr.type = add_dev->addr_type;
                memcpy(unprov_dev[i].addr.val, add_dev->addr, 6);
            }
            if (uuid_cmp) {
                memcpy(unprov_dev[i].uuid, add_dev->uuid, 16);
            }
            unprov_dev[i].bearer = add_dev->bearer & BIT_MASK(2);
            unprov_dev[i].flags  = flags & BIT_MASK(3);
            goto start;
        }
    }

    BT_ERR("Unprov_dev queue is full");
    return -ENOMEM;

start:
    if (!(flags & START_PROV_NOW)) {
        return 0;
    }

    /* Check if the device has already been provisioned */
    for (i = 0; i < ARRAY_SIZE(prov_nodes); i++) {
        if (prov_nodes[i].provisioned) {
            if (!memcmp(prov_nodes[i].uuid, add_dev->uuid, 16)) {
                BT_WARN("Provisioned before, start to provision again");
                provisioner_node_reset(i);
                break;
            }
        }
    }

    if (prov_ctx.node_count == CONFIG_BT_MESH_PROV_NODES_MAX) {
        BT_WARN("Node count reachs max limit");
        return -ENOMEM;
    }

    /* Check if the device is being provisioned now */
    for (i = 0; i < ARRAY_SIZE(link); i++) {
        if (atomic_test_bit(link[i].flags, LINK_ACTIVE) || link[i].connecting) {
            if (!memcmp(link[i].uuid, add_dev->uuid, 16)) {
                BT_WARN("The device is being provisioned");
                return -EALREADY;
            }
        }
    }

    /* Check if current provisioned node count + active link reach max limit */
    if (prov_ctx.node_count + prov_ctx.pba_count + prov_ctx.pbg_count >=
            CONFIG_BT_MESH_PROV_NODES_MAX) {
        BT_WARN("Node count + active link count reach max limit");
        return -EIO;
    }

    if (add_dev->bearer & BT_MESH_PROV_ADV) {
        if (prov_ctx.pba_count == CONFIG_BT_MESH_PBA_SAME_TIME) {
            BT_WARN("Current PB-ADV links reach max limit");
            return -EIO;
        }
        for (i = 0; i < CONFIG_BT_MESH_PBA_SAME_TIME; i++) {
            if (!atomic_test_bit(link[i].flags, LINK_ACTIVE) && !link[i].linking) {
                memcpy(link[i].uuid, add_dev->uuid, 16);
                link[i].oob_info = add_dev->oob_info;
                if (addr_cmp && (add_dev->addr_type <= 0x01)) {
                    link[i].addr.type = add_dev->addr_type;
                    memcpy(link[i].addr.val, add_dev->addr, 6);
                }
                break;
            }
        }
        if (i == CONFIG_BT_MESH_PBA_SAME_TIME) {
            BT_ERR("%s: no PB-ADV link available", __func__);
            return -ENOMEM;
        }
		prov_ctx.pb_index = i;
        send_link_open();
        link[i].linking = 1;
        if (provisioner->prov_link_open) {
            provisioner->prov_link_open(BT_MESH_PROV_ADV);
        }
        prov_addr = add_dev->addr[0];
    }
#if ( MYNEWT_VAL(BLE_MESH_PB_GATT))
	else if (add_dev->bearer & BT_MESH_PROV_GATT) {
        if (prov_ctx.pbg_count == CONFIG_BT_MESH_PBG_SAME_TIME) {
            BT_WARN("Current PB-GATT links reach max limit");
            return -EIO;
        }
        for (i = CONFIG_BT_MESH_PBA_SAME_TIME; i < BT_MESH_PROV_SAME_TIME; i++) {
            if (!atomic_test_bit(link[i].flags, LINK_ACTIVE) && !link[i].connecting) {
                memcpy(link[i].uuid, add_dev->uuid, 16);
                link[i].oob_info = add_dev->oob_info;
                if (addr_cmp && (add_dev->addr_type <= BLE_ADDR_RANDOM)) {
                    link[i].addr.type = add_dev->addr_type;
                    memcpy(link[i].addr.val, add_dev->addr, 6);
                }
                break;
            }
        }
        if (i == BT_MESH_PROV_SAME_TIME) {
            BT_ERR("No PB-GATT link available");
            return -ENOMEM;
        }
		
        if (bt_mesh_gattc_conn_create(&link[i].addr,0x1827)){//for compile
			memset(link[i].uuid, 0, 16);
			link[i].oob_info = 0x0;
			memset(&link[i].addr, 0, sizeof(link[i].addr));   
			return -ENOMEM;
        } else {
			link[i].connecting = true;
        }
    }
#endif
    return 0;
}

int bt_mesh_provisioner_delete_device(struct bt_mesh_device_delete *del_dev)
{
    /**
     * Three Situations:
     * 1. device is not being/been provisioned, just remove from device queue.
     * 2. device is being provisioned, need to close link & remove from device queue.
     * 3. device is been provisioned, need to send config_node_reset and may need to
     *    remove from device queue. config _node_reset can be added in function
     *    provisioner_node_reset() in provisioner_main.c.
     */
    bt_addr_le_t del_addr = {0};
    u8_t zero[16] = {0};
    int addr_cmp = 0, uuid_cmp = 0;
    bool addr_match = false;
    bool uuid_match = false;
    int i, err = 0;

    if (!del_dev) {
        BT_ERR("del_dev is NULL");
        return -EINVAL;
    }

    addr_cmp = memcmp(del_dev->addr, zero, 6);
    uuid_cmp = memcmp(del_dev->uuid, zero, 16);

    if ((uuid_cmp == 0) && ((addr_cmp == 0) ||
                            del_dev->addr_type > 0x01)) {
        BT_ERR("Invalid parameters");
        return -EINVAL;
    }

    del_addr.type = del_dev->addr_type;
    memcpy(del_addr.val, del_dev->addr, 6);

    /* First: find if the device is in the device queue */
    err = provisioner_dev_find(&del_addr, del_dev->uuid, &i);
    if (err) {
        BT_DBG("Device not in the queue");
    } else {
        memset(&unprov_dev[i], 0x0, sizeof(struct unprov_dev_queue));
        provisioner_unprov_dev_num_dec();
    }

    /* Second: find if the device is being provisioned */
    for (i = 0; i < ARRAY_SIZE(link); i++) {
        if (addr_cmp && (del_dev->addr_type <= BLE_ADDR_RANDOM)) {
            if (!memcmp(link[i].addr.val, del_dev->addr, 6) &&
                    link[i].addr.type == del_dev->addr_type) {
                addr_match = true;
            }
        }
        if (uuid_cmp) {
            if (!memcmp(link[i].uuid, del_dev->uuid, 16)) {
                uuid_match = true;
            }
        }
        if (addr_match || uuid_match) {
            close_link(i, CLOSE_REASON_FAILED);
            break;
        }
    }

    /* Third: find if the device is been provisioned */
    for (i = 0; i < ARRAY_SIZE(prov_nodes); i++) {
		if(!prov_nodes[i].provisioned){
			continue;
		}
        if (addr_cmp && (del_dev->addr_type <= 0x01)) {
            if (!memcmp(prov_nodes[i].addr.val, del_dev->addr, 6) &&
                    prov_nodes[i].addr.type == del_dev->addr_type) {
                addr_match = true;
            }
        }
        if (uuid_cmp) {
            if (!memcmp(prov_nodes[i].uuid, del_dev->uuid, 16)) {
                uuid_match = true;
            }
        }
        if (addr_match || uuid_match) {
            provisioner_node_reset(i);
            break;
        }
    }

    return 0;
}

int bt_mesh_provisioner_set_dev_uuid_match(u8_t offset, u8_t length,
        									const u8_t *match, bool prov_flag)
{
    if (length && (!match || (offset + length > 16))) {
        BT_ERR("Invalid parameters");
        return -EINVAL;
    }

    prov_ctx.match_offset = offset;
    prov_ctx.match_length = length;
    if (length) {
        memcpy(prov_ctx.match_value, match, length);
    }
    prov_ctx.prov_after_match = prov_flag;

    return 0;
}
#if 0
int bt_mesh_prov_adv_pkt_cb_register(prov_adv_pkt_cb cb)
{
    if (!cb) {
        BT_ERR("cb is NULL");
        return -EINVAL;
    }

    adv_pkt_notify = cb;
    return 0;
}
#endif
int bt_mesh_provisioner_set_prov_data_info(struct bt_mesh_prov_data_info *info)
{
    const u8_t *key = NULL;

    if (!info || info->flag == 0) {
        return -EINVAL;
    }

    if (info->flag & NET_IDX_FLAG) {
        key = provisioner_net_key_get(info->net_idx);
        if (!key) {
            BT_ERR("Add local netkey first");
            return -EINVAL;
        }
        prov_ctx.curr_net_idx = info->net_idx;
    } else if (info->flag & FLAGS_FLAG) {
        prov_ctx.curr_flags = info->flags;
    } else if (info->flag & IV_INDEX_FLAG) {
        prov_ctx.curr_iv_index = info->iv_index;
    }

    return 0;
}

int bt_mesh_provisioner_get_all_node_unicast_addr(struct os_mbuf *buf)
{
    struct bt_mesh_prov_node_info *node = NULL;
    int i;

    if (!buf) {
        BT_ERR("Invalid parameter");
        return -EINVAL;
    }

    for (i = 0; i < ARRAY_SIZE(prov_nodes); i++) {
        node = &prov_nodes[i];
        if (!node->provisioned || !BT_MESH_ADDR_IS_UNICAST(node->unicast_addr)) {
            continue;
        }
        net_buf_simple_add_le16(buf, node->unicast_addr);
    }

    return 0;
}

static int provisioner_index_check(int node_index)
{
    struct bt_mesh_prov_node_info *node = NULL;

    BT_DBG("");

    if (node_index < 0) {
        BT_ERR("node_index < 0");
        return -EINVAL;
    }

    if (node_index >= ARRAY_SIZE(prov_nodes)) {
        BT_ERR("Too big node_index");
        return -EINVAL;
    }

    node = &prov_nodes[node_index];
    if (!node->provisioned) {
        BT_ERR("Node not found");
        return -EINVAL;
    }

    return 0;
}

int bt_mesh_provisioner_set_node_name(int node_index, const char *name)
{
    size_t length, name_len;
    int i;

    BT_DBG("");

    if (!name) {
        BT_ERR("%s, Invalid parameter", __func__);
        return -EINVAL;
    }

    if (provisioner_index_check(node_index)) {
        BT_ERR("%s, Failed to check node index", __func__);
        return -EINVAL;
    }

    BT_DBG("name len is %d, name is %s", strlen(name), name);

    length = (strlen(name) <= MESH_NAME_SIZE) ? strlen(name) : MESH_NAME_SIZE;
    for (i = 0; i < ARRAY_SIZE(prov_nodes); i++) {
        if (!prov_nodes[i].provisioned) {
            continue;
        }
        name_len = strlen(prov_nodes[i].node_name);
        if (length != name_len) {
            continue;
        }
        if (!strncmp(prov_nodes[i].node_name, name, length)) {
            BT_WARN("Name %s already exists", name);
            return -EEXIST;
        }
    }

    strncpy(prov_nodes[node_index].node_name, name, length);

    return 0;
}

const char *bt_mesh_provisioner_get_node_name(int node_index)
{
    BT_DBG("");

    if (provisioner_index_check(node_index)) {
        BT_ERR("Check node_index fail");
        return NULL;
    }

    return prov_nodes[node_index].node_name;
}

int bt_mesh_provisioner_get_node_index(const char *name)
{
    size_t length, name_len;
    int i;

    BT_DBG("");

    if (!name) {
        return -EINVAL;
    }

    length = (strlen(name) <= MESH_NAME_SIZE) ? strlen(name) : MESH_NAME_SIZE;
    for (i = 0; i < ARRAY_SIZE(prov_nodes); i++) {
        if (!prov_nodes[i].provisioned) {
            continue;
        }
        name_len = strlen(prov_nodes[i].node_name);
        if (length != name_len) {
            continue;
        }
        if (!strncmp(prov_nodes[i].node_name, name, length)) {
            return i;
        }
    }

    return -ENODEV;
}

struct bt_mesh_prov_node_info *bt_mesh_provisioner_get_node_info(u16_t unicast_addr)
{
    struct bt_mesh_prov_node_info *node = NULL;
    int i;

    BT_DBG("");

    if (!BT_MESH_ADDR_IS_UNICAST(unicast_addr)) {
        BT_ERR("%s: not a unicast address", __func__);
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(prov_nodes); i++) {
        node = &prov_nodes[i];
        if (!node->provisioned) {
            continue;
        }
        if (unicast_addr >= node->unicast_addr &&
                unicast_addr < (node->unicast_addr + node->element_num)) {
            return node;
        }
    }

    return NULL;
}

u32_t bt_mesh_provisioner_get_net_key_count(void)
{
    return ARRAY_SIZE(bt_mesh.p_sub);
}

u32_t bt_mesh_provisioner_get_app_key_count(void)
{
    return ARRAY_SIZE(bt_mesh.p_app_keys);
}

static int provisioner_check_app_key(const u8_t app_key[16], u16_t *app_idx)
{
    struct bt_mesh_app_key *key = NULL;
    int i;

    if (!app_key) {
        return 0;
    }

    /* Check if app_key is already existed */
    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_app_keys); i++) {
        key = &bt_mesh.p_app_keys[i];
        if (key->appkey_active && (!memcmp(key->keys[0].val, app_key, 16) ||
                    !memcmp(key->keys[1].val, app_key, 16))) {
            *app_idx = key->app_idx;
            return -EEXIST;
        }
    }

    return 0;
}

static int provisioner_check_app_idx(u16_t app_idx, bool exist)
{
    struct bt_mesh_app_key *key = NULL;
    int i;

    if (exist) {
        /* Check if app_idx is already existed */
        for (i = 0; i < ARRAY_SIZE(bt_mesh.p_app_keys); i++) {
            key = &bt_mesh.p_app_keys[i];
            if (key->appkey_active && (key->app_idx == app_idx)) {
                return -EEXIST;
            }
        }
        return 0;
    }

    /* Check if app_idx is not existed */
    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_app_keys); i++) {
        key = &bt_mesh.p_app_keys[i];
        if (key->appkey_active && (key->app_idx == app_idx)) {
            return 0;
        }
    }

    return -ENODEV;
}

static int provisioner_check_app_key_full(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_app_keys); i++) {
        if (!bt_mesh.p_app_keys[i].appkey_active) {
            return i;
        }
    }

    return -ENOMEM;
}

static int provisioner_check_net_key(const u8_t net_key[16], u16_t *net_idx)
{
    struct bt_mesh_subnet *sub = NULL;
    int i;

    if (!net_key) {
        return 0;
    }

    /* Check if net_key is already existed */
    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
        sub = &bt_mesh.p_sub[i];
        if (sub->subnet_active && (!memcmp(sub->keys[0].net, net_key, 16) ||
                    !memcmp(sub->keys[1].net, net_key, 16))) {
            *net_idx = sub->net_idx;
            return -EEXIST;
        }
    }

    return 0;
}

static int provisioner_check_net_idx(u16_t net_idx, bool exist)
{
    struct bt_mesh_subnet *sub = NULL;
    int i;

    if (exist) {
        /* Check if net_idx is already existed */
        for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
            sub = &bt_mesh.p_sub[i];
            if (sub->subnet_active && (sub->net_idx == net_idx)) {
                return -EEXIST;
            }
        }
        return 0;
    }

    /* Check if net_idx is not existed */
    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
        sub = &bt_mesh.p_sub[i];
        if (sub->subnet_active && (sub->net_idx == net_idx)) {
            return 0;
        }
    }

    return -ENODEV;
}

static int provisioner_check_net_key_full(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
        if (!bt_mesh.p_sub[i].subnet_active) {
            return i;
        }
    }

    return -ENOMEM;
}

int bt_mesh_provisioner_local_app_key_add(const u8_t app_key[16], u16_t net_idx, u16_t *app_idx)
{
    struct bt_mesh_app_key  *key  = NULL;
    struct bt_mesh_app_keys *keys = NULL;
    u8_t p_key[16] = {0};
    int add = -1;

    if (bt_mesh.p_app_idx_next >= 0x1000) {
        BT_ERR("No app_idx available");
        return -EIO;
    }

    if (!app_idx || (*app_idx != 0xFFFF && *app_idx >= 0x1000)) {
        BT_ERR("Invalid parameters");
        return -EINVAL;
    }

    /* Check if the same application key already exists */
    if (provisioner_check_app_key(app_key, app_idx)) {
        BT_WARN("app_key already exists, app_idx updated");
        return 0;
    }

    /* Check if the net_idx exists */
    if (provisioner_check_net_idx(net_idx, false)) {
        BT_ERR("net_idx does not exist");
        return -ENODEV;
    }

    /* Check if the same app_idx already exists */
    if (provisioner_check_app_idx(*app_idx, true)) {
        BT_ERR("app_idx already exists");
        return -EEXIST;
    }

    add = provisioner_check_app_key_full();
    if (add < 0) {
        BT_ERR("app_key full");
        return -ENOMEM;
    }

    if (!app_key) {
        if (bt_rand(p_key, 16)) {
            BT_ERR("%s: generate app_key fail", __func__);
            return -EIO;
        }
    } else {
        memcpy(p_key, app_key, 16);
    }

    key = &(bt_mesh.p_app_keys[add]);
    keys = &key->keys[0];
	
    if (bt_mesh_app_id(p_key, &keys->id)) {
        BT_ERR("Generate aid fail");
        key->appkey_active = false;
        return -EIO;
    }

    memcpy(keys->val, p_key, 16);
    key->net_idx = net_idx;
    if (*app_idx != 0xFFFF) {
    	key->app_idx = *app_idx;
    } else {
        key->app_idx = bt_mesh.p_app_idx_next;
        while (1) {
            if (provisioner_check_app_idx(key->app_idx, true)) {
                key->app_idx = (++bt_mesh.p_app_idx_next);
                if (key->app_idx >= 0x1000) {
                    BT_ERR("No app_idx available for key");
                    key->appkey_active = false;
                    return -EIO;
                }
            } else {
                break;
            }
        }
    	*app_idx = key->app_idx;
    }
    key->updated = false;
    key->appkey_active = true;

    return 0;
}

const u8_t *bt_mesh_provisioner_local_app_key_get(u16_t net_idx, u16_t app_idx)
{
    struct bt_mesh_app_key *key = NULL;
    int i;

    BT_DBG("");

    if (provisioner_check_net_idx(net_idx, false)) {
        BT_ERR("net_idx does not exist");
        return NULL;
    }

    if (provisioner_check_app_idx(app_idx, false)) {
        BT_ERR("app_idx does not exist");
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_app_keys); i++) {
        key = &bt_mesh.p_app_keys[i];
        if (key->appkey_active && key->net_idx == net_idx &&
                key->app_idx == app_idx) {
            if (key->updated) {
                return key->keys[1].val;
            }
            return key->keys[0].val;
        }
    }

    return NULL;
}

int bt_mesh_provisioner_local_app_key_delete(u16_t net_idx, u16_t app_idx)
{
    struct bt_mesh_app_key *key = NULL;
    int i;

    BT_DBG("");

    if (provisioner_check_net_idx(net_idx, false)) {
        BT_ERR("net_idx does not exist");
        return -ENODEV;
    }

    if (provisioner_check_app_idx(app_idx, false)) {
        BT_ERR("app_idx does not exist");
        return -ENODEV;
    }

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_app_keys); i++) {
        key = &bt_mesh.p_app_keys[i];
        if (key->appkey_active && key->net_idx == net_idx &&
                key->app_idx == app_idx) {
            key->appkey_active = false;
            return 0;
        }
    }

    /* Shall never reach here */
    return -ENODEV;
}

struct bt_mesh_app_key *bt_mesh_provisioner_app_key_find(u16_t app_idx)
{
    struct bt_mesh_app_key *key = NULL;
    int i;

    BT_DBG("%s", __func__);

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_app_keys); i++) {
        key = &bt_mesh.p_app_keys[i];
        if (key && key->net_idx != BT_MESH_KEY_UNUSED &&
                key->app_idx == app_idx) {
            return key;
        }
    }

    return NULL;
}


int bt_mesh_provisioner_local_app_key_update(const u8_t app_key[16],
                                             u16_t net_idx, u16_t app_idx)
{
    struct bt_mesh_app_keys *keys = NULL;
    struct bt_mesh_app_key *key = NULL;

    if (app_key == NULL) {
        BT_ERR("Invalid AppKey");
        return -EINVAL;
    }

    BT_INFO("AppKey %s, net_idx 0x%03x, app_idx 0x%03x", bt_hex(app_key, 16), net_idx, app_idx);

    /* Check if the net_idx exists */
    if (provisioner_check_net_idx(net_idx, false)) {
        BT_ERR("Invalid NetKeyIndex 0x%04x", net_idx);
        return -ENODEV;
    }

    key = bt_mesh_provisioner_app_key_find(app_idx);
    if (key == NULL) {
        BT_ERR("Invalid AppKeyIndex 0x%04x", app_idx);
        return -ENODEV;
    }

    keys = &key->keys[0];
    if (bt_mesh_app_id(app_key, &keys->id)) {
        BT_ERR("Failed to generate AID");
        return -EIO;
    }

    memset(keys->val, 0, 16);
    memcpy(keys->val, app_key, 16);

    key->updated = false;


    return 0;
}


int bt_mesh_provisioner_local_net_key_add(const u8_t net_key[16], u16_t *net_idx)
{
    struct bt_mesh_subnet *sub = NULL;
    u8_t p_key[16] = {0};
    int add = -1;

    if (bt_mesh.p_net_idx_next >= 0x1000) {
        BT_ERR("No net_idx available");
        return -EIO;
    }

    if (!net_idx || (*net_idx != 0xFFFF && *net_idx >= 0x1000)) {
        BT_ERR("Invalid parameters");
        return -EINVAL;
    }

    /* Check if the same network key already exists */
    if (provisioner_check_net_key(net_key, net_idx)) {
        BT_WARN("net_key already exists, net_idx updated");
        return 0;
    }

    /* Check if the same net_idx already exists */
    if (provisioner_check_net_idx(*net_idx, true)) {
        BT_ERR("net_idx already exists");
        return -EEXIST;
    }

    add = provisioner_check_net_key_full();
    if (add < 0) {
        BT_ERR("net_key is full");
        return -ENOMEM;
    }

    if (!net_key) {
        if (bt_rand(p_key, 16)) {
            BT_ERR("Generate net_key fail");
            return -EIO;
        }
    } else {
        memcpy(p_key, net_key, 16);
    }

    sub = &bt_mesh.p_sub[add];

    if (bt_mesh_net_keys_create(&sub->keys[0], p_key)) {
        BT_ERR("Generate nid fail");
        sub->subnet_active = false;
        return -EIO;
    }

    if (*net_idx != 0xFFFF) {
        sub->net_idx = *net_idx;
    } else {
        sub->net_idx = bt_mesh.p_net_idx_next;
        while (1) {
            if (provisioner_check_net_idx(sub->net_idx, true)) {
                sub->net_idx = (++bt_mesh.p_net_idx_next);
                if (sub->net_idx >= 0x1000) {
                    BT_ERR("No net_idx available for sub");
                    sub->subnet_active = false;
                    return -EIO;
                }
            } else {
                break;
            }
        }
        *net_idx = sub->net_idx;
    }
    sub->kr_phase = BT_MESH_KR_NORMAL;
    sub->kr_flag  = false;
    sub->node_id  = BT_MESH_NODE_IDENTITY_NOT_SUPPORTED;

    sub->subnet_active = true;

    return 0;
}

const u8_t *bt_mesh_provisioner_local_net_key_get(u16_t net_idx)
{
    struct bt_mesh_subnet *sub = NULL;
    int i;

    BT_DBG("");

    if (provisioner_check_net_idx(net_idx, false)) {
        BT_ERR("net_idx does not exist");
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
        sub = &bt_mesh.p_sub[i];
        if (sub->subnet_active && sub->net_idx == net_idx) {
            if (sub->kr_flag) {
                return sub->keys[1].net;
            }
            return sub->keys[0].net;
        }
    }

    return NULL;
}

int bt_mesh_provisioner_local_net_key_delete(u16_t net_idx)
{
    struct bt_mesh_subnet *sub = NULL;
    int i;

    BT_DBG("");

    if (provisioner_check_net_idx(net_idx, false)) {
        BT_ERR("net_idx does not exist");
        return -ENODEV;
    }

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
        sub = &bt_mesh.p_sub[i];
        if (sub->subnet_active && sub->net_idx == net_idx) {
            sub->subnet_active = false;
            return 0;
        }
    }

    /* Shall never reach here */
    return -ENODEV;
}

struct bt_mesh_subnet *bt_mesh_provisioner_subnet_get(u16_t net_idx)
{
    struct bt_mesh_subnet *sub = NULL;
    int i;

    BT_INFO("%s", __func__);

    if (net_idx == 0xffff) {
        return &bt_mesh.p_sub[0];
    }

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
        sub = &bt_mesh.p_sub[i];
        if (sub && sub->net_idx == net_idx) {
            return sub;
        }
    }

    return NULL;
}

int bt_mesh_provisioner_local_net_key_update(const u8_t net_key[16], u16_t net_idx)
{
    struct bt_mesh_subnet *sub = NULL;
    int err = 0;

    if (net_key == NULL) {
        BT_ERR("Invalid NetKey");
        return -EINVAL;
    }

    BT_INFO("NetKey %s, net_idx 0x%03x", bt_hex(net_key, 16), net_idx);

    sub = bt_mesh_provisioner_subnet_get(net_idx);
    if (sub == NULL) {
        BT_ERR("Invalid NetKeyIndex 0x%04x", net_idx);
        return -ENODEV;
    }

    err = bt_mesh_net_keys_create(&sub->keys[0], net_key);
    if (err) {
        BT_ERR("Failed to generate NID");
        return -EIO;
    }

    memset(sub->keys[0].net, 0, 16);
    memcpy(sub->keys[0].net, net_key, 16);

    sub->kr_phase = BT_MESH_KR_NORMAL;
    sub->kr_flag = false;
    sub->node_id = BT_MESH_NODE_IDENTITY_NOT_SUPPORTED;

    err = bt_mesh_net_beacon_update(sub);
    if (err) {
        BT_ERR("Failed to update secure beacon");
        return -EIO;
    }

    return 0;
}



int bt_mesh_provisioner_get_own_unicast_addr(u16_t *addr, u8_t *elem_num)
{
    if (!addr || !elem_num || !provisioner || !comp) {
        BT_ERR("Parameter is NULL");
        return -EINVAL;
    }

    *addr     = provisioner->prov_unicast_addr;
    *elem_num = comp->elem_count;

    return 0;
}

int bt_mesh_provisioner_bind_local_model_app_idx(u16_t elem_addr, u16_t mod_id,
        										u16_t cid, u16_t app_idx)
{
    struct bt_mesh_elem  *elem  = NULL;
    struct bt_mesh_model *model = NULL;
    int i;

    if (!comp) {
        BT_ERR("Comp is NULL");
        return -EINVAL;
    }

    for (i = 0; i < comp->elem_count; i++) {
        elem = &comp->elem[i];
        if (elem->addr == elem_addr) {
            break;
        }
    }
    if (i == comp->elem_count) {
        BT_ERR("No element found");
        return -ENODEV;
    }

    if (cid == 0xFFFF) {
        model = bt_mesh_model_find(elem, mod_id);
    } else {
        model = bt_mesh_model_find_vnd(elem, cid, mod_id);
    }
    if (!model) {
        BT_ERR("No model found");
        return -ENODEV;
    }

    if (provisioner_check_app_idx(app_idx, false)) {
        BT_ERR("app_idx does not exist");
        return -ENODEV;
    }

    for (i = 0; i < ARRAY_SIZE(model->keys); i++) {
        if (model->keys[i] == app_idx) {
            BT_WARN("%s: app_idx already bind with model", __func__);
            return 0;
        }
    }

    for (i = 0; i < ARRAY_SIZE(model->keys); i++) {
        if (model->keys[i] == BT_MESH_KEY_UNUSED) {
            model->keys[i] = app_idx;
            return 0;
        }
    }

    BT_ERR("model->keys is full");
    return -ENOMEM;
}

int bt_mesh_provisioner_bind_local_app_net_idx(u16_t net_idx, u16_t app_idx)
{
    struct bt_mesh_app_key *key = NULL;
    int i;

    BT_DBG("");

    if (provisioner_check_net_idx(net_idx, false)) {
        BT_ERR("net_idx does not exist");
        return -ENODEV;
    }

    if (provisioner_check_app_idx(app_idx, false)) {
        BT_ERR("app_idx does not exist");
        return -ENODEV;
    }

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_app_keys); i++) {
        key = &bt_mesh.p_app_keys[i];
        if (!key->appkey_active || (key->app_idx != app_idx)) {
            continue;
        }
        key->net_idx = net_idx;
        return 0;
    }

    return -ENODEV;
}

int bt_mesh_provisioner_print_local_element_info(void)
{
    struct bt_mesh_elem  *elem  = NULL;
    struct bt_mesh_model *model = NULL;
    int i, j;

    if (!comp) {
        BT_ERR("Comp is NULL");
        return -EINVAL;
    }

    iot_printf("************************************************");
    iot_printf("* cid: 0x%04x    pid: 0x%04x    vid: 0x%04x    *", comp->cid, comp->pid, comp->vid);
    iot_printf("* Element Number: 0x%02x                         *", comp->elem_count);
    for (i = 0; i < comp->elem_count; i++) {
        elem = &comp->elem[i];
        iot_printf("* Element %d: 0x%04x                            *", i, elem->addr);
        iot_printf("*     Loc: 0x%04x   NumS: 0x%02x   NumV: 0x%02x    *", elem->loc, elem->model_count, elem->vnd_model_count);
        for (j = 0; j < elem->model_count; j++) {
            model = &elem->models[j];
            iot_printf("*     sig_model %d: id - 0x%04x                 *", j, model->id);
        }
        for (j = 0; j < elem->vnd_model_count; j++) {
            model = &elem->vnd_models[j];
            iot_printf("*     vnd_model %d: id - 0x%04x, cid - 0x%04x   *", j, model->vnd.id, model->vnd.company);
        }
    }
    iot_printf("************************************************");

    return 0;
}

void bt_mesh_provisioner_set_prov_bearer(bt_mesh_prov_bearer_t bearers, bool clear)
{
    if (clear == false) {
        prov_ctx.bearers |= bearers;
    } else {
        prov_ctx.bearers &= ~bearers;
    }
}

bool bt_mesh_provisioner_get_node_with_uuid(const u8_t uuid[16]){

    int i=0;
	for (i = 0; i < ARRAY_SIZE(prov_nodes); i++){
        if (memcmp(prov_nodes[i].uuid, uuid, 16) == 0) {
            return true;
        }
	}
	return false;

}

static bool is_unprov_dev_being_provision(const u8_t uuid[16])
{
    int i;

    for (i = 0; i < CONFIG_BT_MESH_PBA_SAME_TIME; i++) {

        if (link[i].connecting || atomic_test_bit(link[i].flags, LINK_ACTIVE)) {

            if (!memcmp(link[i].uuid, uuid, 16)) {
                BT_INFO("Device is being provisioning");
                return true;
            }
        }
    }

    return false;
}

static int provisioner_check_unprov_dev_info(const u8_t uuid[16], bt_mesh_prov_bearer_t bearer)
{
    if (!uuid) {
        BT_ERR("%s, Invalid parameter", __func__);
        return -EINVAL;
    }

    /* Check if the device uuid matches configured value */
    if (provisioner_dev_uuid_match(uuid)) {//需要加入UUID匹配过程
        BT_DBG("Device uuid mismatch");
        return -EIO;
    }

    /* Check if this device is currently being provisioned.
     * According to Zephyr's device code, if we connect with
     * one device and start to provision it, we may still can
     * receive the connectable prov adv pkt from this device.
     * Here we check both PB-GATT and PB-ADV link status.
     */
    if (is_unprov_dev_being_provision(uuid)) {
        return -EALREADY;
    }

    /* Check if the current PB-ADV link is full */
    if (bearer == BT_MESH_PROV_ADV && prov_ctx.pba_count == CONFIG_BT_MESH_PBA_SAME_TIME) {
        BT_INFO("Current PB-ADV links reach max limit");
        return -ENOMEM;
    }

    /* Check if the current PB-GATT link is full */
    if (bearer == BT_MESH_PROV_GATT && prov_ctx.pbg_count == CONFIG_BT_MESH_PBG_SAME_TIME) {
        BT_INFO("Current PB-GATT links reach max limit");
        return -ENOMEM;
    }

    /* Check if the device has already been provisioned */
    if (bt_mesh_provisioner_get_node_with_uuid(uuid)) {
        BT_INFO("Provisioned before, start to provision again");
        return 0;
    }

    return 0;
}

#if (MYNEWT_VAL(BLE_MESH_PB_GATT))
static int provisioner_start_prov_pb_gatt(const u8_t uuid[16], const bt_mesh_addr_t *addr,
                                          u16_t oob_info, u16_t assign_addr)
{
    u8_t zero[6] = {0};
    int addr_cmp = 0;
    int i;

    if (!uuid || !addr) {
        BT_ERR("%s, Invalid parameter", __func__);
        return -EINVAL;
    }

    bt_mesh_pb_gatt_lock();

    /* If the unicast address of the node is going to be allocated internally,
     * then we need to check if there are addresses can be allocated.
     */

     /*
    if (assign_addr == BT_MESH_ADDR_UNASSIGNED &&
            prov_ctx.curr_alloc_addr == BT_MESH_ADDR_UNASSIGNED) {
        BT_ERR("No available unicast address to assign");
        bt_mesh_pb_gatt_unlock();
        return -EIO;
    }
    */

    if (is_unprov_dev_being_provision(uuid)) {
        bt_mesh_pb_gatt_unlock();
        return -EALREADY;
    }

    addr_cmp = memcmp(addr->val, zero, 6);
    for (i = CONFIG_BT_MESH_PBA_SAME_TIME; i < BT_MESH_PROV_SAME_TIME; i++) {
        if (!link[i].connecting && !atomic_test_bit(link[i].flags, LINK_ACTIVE)) {
            memcpy(link[i].uuid, uuid, 16);
            link[i].oob_info = oob_info;
            if (addr_cmp && (addr->type <= 1)) {
                link[i].addr.type = addr->type;
                memcpy(link[i].addr.val, addr->val, 6);
            }
            if (bt_mesh_gattc_conn_create(&link[i].addr, 0x1827) < 0) {
				BT_INFO("%s, gattc create connection failed", __func__);
                memset(link[i].uuid, 0, 16);
                link[i].oob_info = 0x0;
				link[i].connecting = false;
                memset(&link[i].addr, 0, sizeof(bt_mesh_addr_t));
                bt_mesh_pb_gatt_unlock();
                return -EIO;
            }
            /* If the application layer assigned a specific unicast address for the device,
             * then Provisioner will use this address in the Provisioning Data PDU.
             */
            if (BT_MESH_ADDR_IS_UNICAST(assign_addr)) {
                link[i].assign_addr = assign_addr;
            }
            /* If creating connection successfully, set connecting flag to 1 */
            link[i].connecting = true;
            //provisioner_pbg_count_inc();
            bt_mesh_pb_gatt_unlock();
            return 0;
        }
    }

    BT_ERR("No PB-GATT link available");
    bt_mesh_pb_gatt_unlock();

    return -ENOMEM;
}
#endif /* CONFIG_BLE_MESH_PB_GATT */


void bt_mesh_provisioner_prov_adv_recv(struct os_mbuf *buf,
                                       const bt_mesh_addr_t *addr, s8_t rssi)
{
#if (MYNEWT_VAL(BLE_MESH_PB_GATT))
    const u8_t *uuid = NULL;
    u16_t oob_info = 0U;

    if (!(prov_ctx.bearers & BT_MESH_PROV_GATT)) {
        BT_WARN("Not support PB-GATT bearer");
        return;
    }

    if (bt_mesh_gattc_get_free_conn_count() == 0) {
        BT_INFO("BLE connections for mesh reach max limit");
        return;
    }

    uuid = buf->om_data;
    net_buf_simple_pull(buf, 16);
    /* Mesh beacon uses big-endian to send beacon data */
    oob_info = net_buf_simple_pull_be16(buf);

    if (provisioner_check_unprov_dev_info(uuid, BT_MESH_PROV_GATT)) {
        return;
    }

    /*
    if (is_unprov_dev_info_callback_to_app(
                BLE_MESH_PROV_GATT, uuid, addr, oob_info, rssi)) {
        return;
    }
    */

    /* Provisioner will copy the device uuid, oob info, etc. into an unused link
     * struct, and at this moment the link has not been activated. Even if we
     * receive an Unprovisioned Device Beacon and a Connectable Provisioning adv
     * pkt from the same device, and store the device info received within each
     * adv pkt into two link structs which will has no impact on the provisioning
     * of this device, because no matter which link among PB-GATT and PB-ADV is
     * activated first, the other one will be dropped finally and the link struct
     * occupied by the dropped link will be used by other devices (because the link
     * is not activated).
     * Use connecting flag to prevent if two devices's adv pkts are both received,
     * the previous one info will be replaced by the second one.
     */
    provisioner_start_prov_pb_gatt(uuid, addr, oob_info, BT_MESH_ADDR_UNASSIGNED);
#endif /* CONFIG_BLE_MESH_PB_GATT */
}


#endif


