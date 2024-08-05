

#include "syscfg/syscfg.h"

#if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && MYNEWT_VAL(BLE_MESH_PB_GATT)) || \
		MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)


#include "mesh/mesh.h"
#include "provisioner.h"
#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG_PROXY))
#include "host/ble_hs_log.h"
#include "host/ble_att.h"
#include "services/gatt/ble_svc_gatt.h"
#include "../../host/src/ble_hs_priv.h"
#include "host/ble_gap.h"
#include "nimble/hci_common.h"

#include "mesh_priv.h"
#include "adv.h"
#include "net.h"
#include "beacon.h"
#include "foundation.h"
#include "access.h"
#include "provisioner.h"
#include "provisioner_proxy.h"

/** @def BT_UUID_MESH_PROV
 *  @brief Mesh Provisioning Service
 */
static ble_uuid16_t BT_UUID_MESH_PROV                 = BLE_UUID16_INIT(0x1827);
#define BT_UUID_MESH_PROV_VAL             0x1827
/** @def BT_UUID_MESH_PROXY
 *  @brief Mesh Proxy Service
 */
static ble_uuid16_t BT_UUID_MESH_PROXY                = BLE_UUID16_INIT(0x1828);
#define BT_UUID_MESH_PROXY_VAL            0x1828
/** @def BT_UUID_GATT_CCC
 *  @brief GATT Client Characteristic Configuration
 */
static ble_uuid16_t BT_UUID_GATT_CCC                  = BLE_UUID16_INIT(0x2902);
#define BT_UUID_GATT_CCC_VAL              0x2902
/** @def BT_UUID_MESH_PROV_DATA_IN
 *  @brief Mesh Provisioning Data In
 */
static ble_uuid16_t BT_UUID_MESH_PROV_DATA_IN         = BLE_UUID16_INIT(0x2adb);
#define BT_UUID_MESH_PROV_DATA_IN_VAL     0x2adb
/** @def BT_UUID_MESH_PROV_DATA_OUT
 *  @brief Mesh Provisioning Data Out
 */
static ble_uuid16_t BT_UUID_MESH_PROV_DATA_OUT        = BLE_UUID16_INIT(0x2adc);
#define BT_UUID_MESH_PROV_DATA_OUT_VAL    0x2adc
/** @def BT_UUID_MESH_PROXY_DATA_IN
 *  @brief Mesh Proxy Data In
 */
static ble_uuid16_t BT_UUID_MESH_PROXY_DATA_IN        = BLE_UUID16_INIT(0x2add);
#define BT_UUID_MESH_PROXY_DATA_IN_VAL    0x2add
/** @def BT_UUID_MESH_PROXY_DATA_OUT
 *  @brief Mesh Proxy Data Out
 */
static ble_uuid16_t BT_UUID_MESH_PROXY_DATA_OUT       = BLE_UUID16_INIT(0x2ade);
#define BT_UUID_MESH_PROXY_DATA_OUT_VAL   0x2ade

#define BLE_MESH_GATT_CHRC_BROADCAST              0x01
#define BLE_MESH_GATT_CHRC_READ                   0x02
#define BLE_MESH_GATT_CHRC_WRITE_WITHOUT_RESP     0x04
#define BLE_MESH_GATT_CHRC_WRITE                  0x08
#define BLE_MESH_GATT_CHRC_NOTIFY                 0x10
#define BLE_MESH_GATT_CHRC_INDICATE               0x20
#define BLE_MESH_GATT_CHRC_AUTH                   0x40
#define BLE_MESH_GATT_CHRC_EXT_PROP               0x80
#define PDU_TYPE(data)     (data[0] & MESH_BIT_MASK(6))
#define PDU_SAR(data)      (data[0] >> 6)

#define SAR_COMPLETE       0x00
#define SAR_FIRST          0x01
#define SAR_CONT           0x02
#define SAR_LAST           0x03

#define PROXY_SAR_TIMEOUT   K_SECONDS(20)
#define ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)
#define MESH_SCAN_INTERVAL_MS 10
#define MESH_SCAN_WINDOW_MS   10
#define MESH_SCAN_INTERVAL    ADV_SCAN_UNIT(MESH_SCAN_INTERVAL_MS)
#define MESH_SCAN_WINDOW      ADV_SCAN_UNIT(MESH_SCAN_WINDOW_MS)

#define PDU_HDR(sar, type) (sar << 6 | (type & MESH_BIT_MASK(6)))


#define SAR_COMPLETE       0x00
#define SAR_FIRST          0x01
#define SAR_CONT           0x02
#define SAR_LAST           0x03

struct bt_mesh_dev {
    /* Flags indicate which functionality is enabled */
    ATOMIC_DEFINE(flags, BLE_MESH_DEV_NUM_FLAGS);
};


struct bt_mesh_dev bt_mesh_dev;

static struct gattc_prov_info {
    u16 handle;
    bt_mesh_addr_t addr;
    u16_t service_uuid;
    u16_t mtu;
    bool  wr_desc_done;    /* Indicate if write char descriptor event is received */
    u16_t start_handle;    /* Service attribute start handle */
    u16_t end_handle;      /* Service attribute end handle */
    u16_t data_in_handle;  /* Data In Characteristic attribute handle */
    u16_t data_out_handle; /* Data Out Characteristic attribute handle */
    u16_t ccc_handle;      /* Data Out Characteristic CCC attribute handle */
} bt_mesh_gattc_info[MYNEWT_VAL_BLE_MAX_CONNECTIONS];

extern u8_t g_mesh_addr_type;

struct bt_mesh_prov_conn_cb {
    void (*connected)(bt_mesh_addr_t *addr, u16 handle, int id);

    void (*disconnected)(bt_mesh_addr_t *addr, u16 handle, u8_t reason);

    ssize_t (*prov_write_descr)(bt_mesh_addr_t *addr, u16 handle);

    ssize_t (*prov_notify)(u16 handle, u8_t *data, u16_t len);

    ssize_t (*proxy_write_descr)(bt_mesh_addr_t *addr, u16 handle);

    ssize_t (*proxy_notify)(u16 handle, u8_t *data, u16_t len);
};

static struct bt_mesh_prov_conn_cb *bt_mesh_gattc_conn_cb;

u8_t bt_mesh_gattc_get_free_conn_count(void)
{
	u8_t count = 0;
	u8_t i;

	for (i = 0U; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
		if (bt_mesh_gattc_info[i].handle == 0 &&
				bt_mesh_gattc_info[i].service_uuid == 0x0000) {
			++count;
		}
	}

	return count;
}

u16_t bt_mesh_gattc_get_mtu_info(u16 handle)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
		if (handle == bt_mesh_gattc_info[i].handle) {
			return bt_mesh_gattc_info[i].mtu;
		}
	}

	return 0;
}

#if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && MYNEWT_VAL(BLE_MESH_PB_GATT)) || \
		MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)

static struct bt_mesh_proxy_server {
    u16 handle ;
    enum __packed {
        NONE,
        PROV,
        PROXY,
    } conn_type;
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)
    u16_t net_idx;
#endif
    u8_t msg_type;
    struct k_delayed_work sar_timer;
	struct os_mbuf *buf;
} servers[MYNEWT_VAL_BLE_MAX_CONNECTIONS];

struct bt_mesh_proxy_server *find_server(u16 handle)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(servers); i++) {
        if (servers[i].handle == handle) {
            return &servers[i];
        }
    }

    return NULL;
}


bool is_scanning(){

    if (atomic_test_bit(bt_mesh_dev.flags, BLE_MESH_DEV_SCANNING)) {
			BT_INFO("Scan is already started");
			return true;
	}
	else{
        return false;
	}

}
void set_scan_bit(){
	
    atomic_set_bit(bt_mesh_dev.flags, BLE_MESH_DEV_SCANNING);

}

void clear_scan_bit(){
	
    atomic_clear_bit(bt_mesh_dev.flags, BLE_MESH_DEV_SCANNING);

}

void bt_mesh_gattc_conn_cb_register(struct bt_mesh_prov_conn_cb *cb)
{
    bt_mesh_gattc_conn_cb = cb;
}

void bt_mesh_gattc_conn_cb_deregister(void)
{
    bt_mesh_gattc_conn_cb = NULL;
}

void bt_mesh_proxy_client_gatt_adv_recv(struct os_mbuf *buf,
                                        const bt_mesh_addr_t *addr, s8_t rssi)
{
    bt_mesh_proxy_adv_ctx_t ctx = {0};
    u8_t type = 0U;

    /* Check if connection reaches the maximum limitation */
    if (bt_mesh_gattc_get_free_conn_count() == 0) {
        BT_INFO("BLE connections for mesh reach max limit");
        return;
    }

    type = net_buf_simple_pull_u8(buf);

    switch (type) {
    case BLE_MESH_PROXY_ADV_NET_ID: {
        if (buf->om_len != sizeof(ctx.net_id.net_id)) {
            BT_DBG("Malformed Network ID,len:0x%x,data:%s ",buf->om_len,bt_hex(buf->om_data, buf->om_len));
            return;
        }

        struct bt_mesh_subnet *sub = NULL;
        //sub = bt_mesh_is_net_id_exist(buf->om_data);
        if (!sub) {
            return;
        }

        memcpy(ctx.net_id.net_id, buf->om_data, buf->om_len);
        ctx.net_id.net_idx = sub->net_idx;
        break;
    }
    case BLE_MESH_PROXY_ADV_NODE_ID:
        /* Gets node identity information.
         * hash = aes-ecb(identity key, 16 octets(padding + random + src)) mod 2^64,
         * If Proxy Client wants to get src, it may encrypts multiple times and compare
         * the hash value (8 octets) with the received one.
         */
        return;
    default:
        BT_INFO("Unknown Mesh Proxy adv type 0x%02x", type);
        return;
    }

   // if (proxy_client_adv_recv_cb) {
       // proxy_client_adv_recv_cb(addr, type, &ctx, rssi);
    //}
}

#endif

void bt_mesh_gattc_disconnect(u16         handle)
{

    int i;

    for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
        if (handle == bt_mesh_gattc_info[i].handle) {
            break;
        }
    }

    if (i == ARRAY_SIZE(bt_mesh_gattc_info)) {
        BT_ERR("Conn %d not found", handle);
        return;
    }
    ble_gap_terminate(bt_mesh_gattc_info[i].handle, BLE_ERR_REM_USER_CONN_TERM);
}

static int ble_on_subscribe(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr,
                            void *arg)
{
    u16 handle;
	uint8_t value[2] = {0x01, 0x00};
    int i = (int)arg, j, len;
    BT_INFO("Subscribe complete;i=%d status=%d conn_handle=%d "
                "attr_handle=%d\n",i,error->status, conn_handle, attr->handle);

    for (j = i + 1; j < ARRAY_SIZE(bt_mesh_gattc_info); j++) {
        if ((bt_mesh_gattc_info[j].handle == conn_handle) && bt_mesh_gattc_info[j].ccc_handle) {
            break;
        }
    }
    if (j == ARRAY_SIZE(bt_mesh_gattc_info)) {

        handle = bt_mesh_gattc_info[i].handle;

        if (bt_mesh_gattc_info[i].ccc_handle != attr->handle) {
            BT_WARN("gattc ccc_handle not matched");
            bt_mesh_gattc_disconnect(handle);
            return 0;
        }

        if (bt_mesh_gattc_info[i].service_uuid == BT_UUID_MESH_PROV_VAL) {
            if (bt_mesh_gattc_conn_cb != NULL && bt_mesh_gattc_conn_cb->prov_write_descr != NULL) {
                len = bt_mesh_gattc_conn_cb->prov_write_descr(&bt_mesh_gattc_info[i].addr, bt_mesh_gattc_info[i].handle);
                if (len < 0) {
                    BT_ERR("prov_write_descr failed");
                    bt_mesh_gattc_disconnect(handle);
                    return 0;
                }
                bt_mesh_gattc_info[i].wr_desc_done = true;
            }
        } else if (bt_mesh_gattc_info[i].service_uuid == BT_UUID_MESH_PROXY_VAL) {
            if (bt_mesh_gattc_conn_cb != NULL && bt_mesh_gattc_conn_cb->proxy_write_descr != NULL) {
                len = bt_mesh_gattc_conn_cb->proxy_write_descr(&bt_mesh_gattc_info[i].addr, bt_mesh_gattc_info[i].handle);
                if (len < 0) {
                    BT_ERR("proxy_write_descr failed");
                    bt_mesh_gattc_disconnect(handle);
                    return 0;
                }
                bt_mesh_gattc_info[i].wr_desc_done = true;
            }
        }


        return 0;
    }

    ble_gattc_write_flat(conn_handle, bt_mesh_gattc_info[i].ccc_handle, value, sizeof(value), ble_on_subscribe, (void *)j);
    return 0;
}


static int dsc_disced(uint16_t conn_handle, const struct ble_gatt_error *error,
                      uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                      void *arg)
{
    int rc = 0, j, i = (int)arg; /* char index */
    uint8_t value[2] = {0x01, 0x00};
    
	BT_DBG("error status:%x\n",error->status);
    switch (error->status) {
    case 0:
        if (bt_mesh_gattc_info[i].ccc_handle == 0 && dsc &&
            BLE_UUID16(&dsc->uuid)->value == BT_UUID_GATT_CCC_VAL) {//found cccd handle
            bt_mesh_gattc_info[i].ccc_handle = dsc->handle;
			BT_INFO("cccd handle:%x\n",dsc->handle);
        }
			
        break;

    case BLE_HS_EDONE:

        for (j = i + 1; j < ARRAY_SIZE(bt_mesh_gattc_info); j++) {
            if ((bt_mesh_gattc_info[j].handle == conn_handle) && bt_mesh_gattc_info[j].data_out_handle) {
                break;
            }
        }
        if (j == ARRAY_SIZE(bt_mesh_gattc_info)) {
            /* Register Notification for Mesh Provisioning/Proxy Data Out Characteristic */
            for (j = 0; j < ARRAY_SIZE(bt_mesh_gattc_info); j++) {
                if ((bt_mesh_gattc_info[j].handle == conn_handle) && bt_mesh_gattc_info[j].ccc_handle) {
                    break;
                }
            }
            if (j == ARRAY_SIZE(bt_mesh_gattc_info)) {
                return 0;
            }
            ble_gattc_write_flat(conn_handle, bt_mesh_gattc_info[i].ccc_handle, value, sizeof(value), ble_on_subscribe, (void *)j);
        } else {
            ble_gattc_disc_all_dscs(conn_handle, bt_mesh_gattc_info[j].data_out_handle, 0xffff, dsc_disced, (void *)j);
        }
        rc = 0;
        break;

    default:
        /* Error; abort discovery. */
        rc = error->status;
        break;
    }

    return rc;
}


static int chr_disced(uint16_t conn_handle, const struct ble_gatt_error *error,
                      const struct ble_gatt_chr *chr, void *arg)
{
    int rc = 0, j;
    uint16_t uuid16 = 0;
    int i = (int)arg; /* service index */
    u16 handle = bt_mesh_gattc_info[i].handle;
    const ble_uuid_any_t *uuid = &chr->uuid;
    if (chr) {
        uuid16 = (uint16_t) BLE_UUID16(uuid)->value;
    }
	BT_DBG("error status:%x\n",error->status);
    switch (error->status) {
    case 0:
        /* Get Mesh Provisioning/Proxy Data In/Out Characteristic */
        if ((uuid16 == BT_UUID_MESH_PROV_DATA_IN_VAL) || (uuid16 == BT_UUID_MESH_PROXY_DATA_IN_VAL)) {
            if (!(chr->properties & BLE_MESH_GATT_CHRC_WRITE_WITHOUT_RESP)) {
                bt_mesh_gattc_disconnect(handle);
                BT_ERR("Write without response is not set for Data In characteristic");
                return 0;
            }
            bt_mesh_gattc_info[i].data_in_handle = chr->val_handle;
        } else if ((uuid16 == 0x2adc) || (uuid16 == 0x2ade)) {
            if (!(chr->properties & BLE_MESH_GATT_CHRC_NOTIFY)) {
                bt_mesh_gattc_disconnect(handle);
                BT_ERR("Notify is not set for Data Out characteristic");
                return 0;
            }
            bt_mesh_gattc_info[i].data_out_handle = chr->val_handle;
        }
        break;
    case BLE_HS_EDONE:
        /* All characteristics in this service discovered; start discovering
         * characteristics in the next service.
         */
        for (j = i + 1; j < ARRAY_SIZE(bt_mesh_gattc_info); j++) {
            if ((bt_mesh_gattc_info[j].handle == conn_handle) && (bt_mesh_gattc_info[j].start_handle > bt_mesh_gattc_info[j].end_handle)) {
                break;
            }
        }
        if (j == ARRAY_SIZE(bt_mesh_gattc_info)) {
            for (j = 0; j < ARRAY_SIZE(bt_mesh_gattc_info); j++) {
                if ((bt_mesh_gattc_info[j].handle == conn_handle) && bt_mesh_gattc_info[j].data_out_handle) {
                    break;
                }
            }
            ble_gattc_disc_all_dscs(conn_handle, bt_mesh_gattc_info[j].data_out_handle, bt_mesh_gattc_info[j].end_handle,
                                    dsc_disced, (void *)j);
        } else {
            ble_gattc_disc_all_chrs(conn_handle, bt_mesh_gattc_info[j].start_handle, bt_mesh_gattc_info[j].end_handle,
                                    chr_disced, (void *)j);
        }
        break;

    default:
        rc = error->status;
        break;
    }

    return rc;
}


static int svc_disced(uint16_t conn_handle, const struct ble_gatt_error *error,
                      const struct ble_gatt_svc *service, void *arg)
{
    u16 handle;
	int rc = 0, i;
    const ble_uuid_any_t *uuid;
    uint8_t uuid_length;
	BT_DBG("error status:%x\n",error->status);
    switch (error->status) {
    case 0:
        if (!service) {
            return 0;
        }
        uuid = &service->uuid;
        uuid_length = (uint8_t) (uuid->u.type == BLE_UUID_TYPE_16 ? 2 : 16);
        if (uuid_length != 2) {
            return 0;
        }
        for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
            if (bt_mesh_gattc_info[i].service_uuid == (uint16_t)BLE_UUID16(uuid)->value) {
                bt_mesh_gattc_info[i].start_handle = service->start_handle;
                bt_mesh_gattc_info[i].end_handle   = service->end_handle;
                break;
            }
        }

        break;
    case BLE_HS_EDONE:
        for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
            if (bt_mesh_gattc_info[i].handle == conn_handle) {
                break;
            }
        }

        if (i == ARRAY_SIZE(bt_mesh_gattc_info)) {
            BT_ERR("Conn handle 0x%04x not found", conn_handle);
            return 0;
        }
        handle = bt_mesh_gattc_info[i].handle;
        if (bt_mesh_gattc_info[i].start_handle == 0x00 ||
                bt_mesh_gattc_info[i].end_handle   == 0x00 ||
                (bt_mesh_gattc_info[i].start_handle > bt_mesh_gattc_info[i].end_handle)) {
            bt_mesh_gattc_disconnect(handle);
            return 0;
        }

        /* Get the characteristic num within Mesh Provisioning/Proxy Service */
        ble_gattc_disc_all_chrs(conn_handle, bt_mesh_gattc_info[i].start_handle, bt_mesh_gattc_info[i].end_handle,
                                chr_disced, (void *)i);
        break;

    default:
        rc = error->status;
        break;
    }

    return rc;
}


int provisioner_proxy_event(struct ble_gap_event *event, void *arg)
{

	 struct ble_gap_disc_desc *desc;
	 struct ble_gap_disc_params scan_param =
		{ .passive = 1, .filter_duplicates = 0, .itvl =
		  MESH_SCAN_INTERVAL, .window = MESH_SCAN_WINDOW };
		int rc, i;
		uint8_t notif_data[100];
		uint16_t notif_len;
		ssize_t len;
		struct ble_gap_conn_desc conn_desc;

	switch (event->type){
        case BLE_GAP_EVENT_DISC:
        ble_adv_gap_mesh_cb(event, arg);
        break;
	
		case BLE_GAP_EVENT_CONNECT:
			if (event->connect.status == 0) {
                BT_INFO("Connection established");
			    rc = ble_gap_conn_find(event->connect.conn_handle, &conn_desc);
                assert(rc == 0);

                if (bt_mesh_gattc_conn_cb != NULL && bt_mesh_gattc_conn_cb->connected != NULL) {
	                for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
	                    if (!memcmp(bt_mesh_gattc_info[i].addr.val, conn_desc.peer_id_addr.val, 6)) {
							provisioner_link_connected(event->connect.conn_handle,i);
	                        bt_mesh_gattc_info[i].handle = event->connect.conn_handle;
	                        (bt_mesh_gattc_conn_cb->connected)(&bt_mesh_gattc_info[i].addr, bt_mesh_gattc_info[i].handle, i);
	                        break;
	                    }
	                }
               }
            }

			if (!atomic_test_bit(bt_mesh_dev.flags, BLE_MESH_DEV_SCANNING)) {
	            rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &scan_param, provisioner_proxy_event, NULL);
	            if (rc != 0) {
	                BT_ERR("Invalid scan status %d", rc);
	                break;
	            }

	            atomic_set_bit(bt_mesh_dev.flags, BLE_MESH_DEV_SCANNING);
            }

                break;
		case BLE_GAP_EVENT_DISCONNECT:
		    if (bt_mesh_gattc_conn_cb != NULL && bt_mesh_gattc_conn_cb->disconnected != NULL) {
            for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
                memcpy(&conn_desc, &event->disconnect.conn, sizeof(conn_desc));
                if (!memcmp(bt_mesh_gattc_info[i].addr.val, conn_desc.peer_ota_addr.val, 6)) {
                    if (bt_mesh_gattc_info[i].handle == event->disconnect.conn.conn_handle) {
                        (bt_mesh_gattc_conn_cb->disconnected)(&bt_mesh_gattc_info[i].addr, bt_mesh_gattc_info[i].handle, event->disconnect.reason);
                        if (!bt_mesh_gattc_info[i].wr_desc_done) {
                            /* Add this in case connection is established, connected event comes, but
                             * connection is terminated before server->filter_type is set to PROV.
                             */
#if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && MYNEWT_VAL(BLE_MESH_PB_GATT))
                            if (bt_mesh_gattc_info[i].service_uuid == BT_UUID_MESH_PROV_VAL) {
                                bt_mesh_provisioner_clear_link_info(bt_mesh_gattc_info[i].addr.val);
                            }
#endif
                        }
                    } else {
                        /* Add this in case connection is failed to be established, and here we
                         * need to clear some provision link info, like connecting flag, device
                         * uuid, address info, etc.
                         */
#if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && MYNEWT_VAL(BLE_MESH_PB_GATT))
                        if (bt_mesh_gattc_info[i].service_uuid == BT_UUID_MESH_PROV_VAL) {
                            bt_mesh_provisioner_clear_link_info(bt_mesh_gattc_info[i].addr.val);
                        }
#endif
                    }
#if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && MYNEWT_VAL(BLE_MESH_PB_GATT))
                    if (bt_mesh_gattc_info[i].service_uuid == BT_UUID_MESH_PROV_VAL) {
                        /* Decrease prov pbg_count */
                        provisioner_pbg_count_dec();
                    }
#endif
                    /* Reset corresponding gattc info */
                    memset(&bt_mesh_gattc_info[i], 0, sizeof(bt_mesh_gattc_info[i]));
                    bt_mesh_gattc_info[i].handle = 0;
                    bt_mesh_gattc_info[i].mtu = 23;
                    bt_mesh_gattc_info[i].wr_desc_done = false;
                    break;
                }
            }
        }
        break;

		case BLE_GAP_EVENT_MTU:
            for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
	            if (bt_mesh_gattc_info[i].handle == event->mtu.conn_handle) {
	                bt_mesh_gattc_info[i].mtu = event->mtu.value;
	                break;
	            }
            }
        /** Once mtu exchanged accomplished, start to find services, and here
         *  need a flag to indicate which service to find(Mesh Prov Service or
         *  Mesh Proxy Service)
         */
            ble_uuid_any_t bt_uuid;
            if (i != ARRAY_SIZE(bt_mesh_gattc_info)) {
	            //service_uuid.len       = sizeof(bt_mesh_gattc_info[i].service_uuid);
	            if (sizeof(bt_mesh_gattc_info[i].service_uuid) == 0x02) {
	                bt_uuid.u16.u.type = BLE_UUID_TYPE_16;
	                bt_uuid.u16.value = bt_mesh_gattc_info[i].service_uuid;

	            } else if (sizeof(bt_mesh_gattc_info[i].service_uuid) == 0x10) {
	                bt_uuid.u128.u.type = BLE_UUID_TYPE_128;
	                memcpy(bt_uuid.u128.value, &bt_mesh_gattc_info[i].service_uuid, 16);
	            }
	            /* Search Mesh Provisioning Service or Mesh Proxy Service */
	            ble_gattc_disc_all_svcs(bt_mesh_gattc_info[i].handle, svc_disced, NULL);
            }
		        break;
		case BLE_GAP_EVENT_NOTIFY_RX:
	        for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
	            if (bt_mesh_gattc_info[i].handle == event->notify_rx.conn_handle) {
	                break;
	            }
	        }

	        if (i == ARRAY_SIZE(bt_mesh_gattc_info)) {
	            BT_ERR("Conn handle 0x%04x not found", event->notify_rx.conn_handle);
	            return 0;
	        }

	        u16 handle = bt_mesh_gattc_info[i].handle;
	        ble_gap_conn_find(event->notify_rx.conn_handle, &conn_desc);

	        if (bt_mesh_gattc_info[i].data_out_handle != event->notify_rx.attr_handle) {
	            /* Data isn't populated yet */
	            return 0;
	        }

	        if (memcmp(bt_mesh_gattc_info[i].addr.val, conn_desc.peer_id_addr.val, 6) ||
	                (bt_mesh_gattc_info[i].data_out_handle != event->notify_rx.attr_handle) ||
	                (event->notify_rx.indication != 0)) {
	            BT_ERR("Notification error");
	            bt_mesh_gattc_disconnect(handle);
	            return 0;
	        }

	        notif_len = OS_MBUF_PKTLEN(event->notify_rx.om);
	        rc = os_mbuf_copydata(event->notify_rx.om, 0, notif_len, notif_data);
            BT_INFO(" notif data:%s ,len:%d",bt_hex(notif_data, notif_len),notif_len);
	        if (bt_mesh_gattc_info[i].service_uuid == BT_UUID_MESH_PROV_VAL) {
	            if (bt_mesh_gattc_conn_cb != NULL && bt_mesh_gattc_conn_cb->prov_notify != NULL) {
	                len = bt_mesh_gattc_conn_cb->prov_notify(bt_mesh_gattc_info[i].handle,
	                        notif_data, notif_len);
	                if (len < 0) {
	                    BT_ERR("prov_notify failed");
	                    bt_mesh_gattc_disconnect(handle);
	                    return 0;
	                }
	            }
	        } else if (bt_mesh_gattc_info[i].service_uuid == BT_UUID_MESH_PROXY_VAL) {
	            if (bt_mesh_gattc_conn_cb != NULL && bt_mesh_gattc_conn_cb->proxy_notify != NULL) {
	                len = bt_mesh_gattc_conn_cb->proxy_notify(bt_mesh_gattc_info[i].handle,
	                        notif_data, notif_len);
	                if (len < 0) {
	                    BT_ERR("proxy_notify failed");
	                    bt_mesh_gattc_disconnect(handle);
	                    return 0;
	                }
	            }
	       }
           break;
       default:
           break;

	}	
	return 0;
}

static int mtu_cb(uint16_t conn_handle,
                  const struct ble_gatt_error *error,
                  uint16_t mtu, void *arg)
{
    int i;
    if (error->status == 0) {
        BT_INFO("conn_handle:%x,mtu:%x",conn_handle,mtu);
        for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
            if (bt_mesh_gattc_info[i].handle == conn_handle) {
                bt_mesh_gattc_info[i].mtu = mtu;
                break;
            }
        }
    }
	else{
        BT_INFO("error:%x",error->status);
		bt_mesh_gattc_disconnect(conn_handle);
	}
    return 0;
}

void bt_mesh_gattc_exchange_mtu(u8_t index)
{
    /** Set local MTU and exchange with GATT server.
     *  ATT_MTU >= 69 for Mesh GATT Prov Service
     *  ATT_NTU >= 33 for Mesh GATT Proxy Service
    */
    ble_gattc_exchange_mtu(bt_mesh_gattc_info[index].handle, mtu_cb, NULL);
}



int bt_mesh_proxy_client_connect(const u8_t addr[6], u8_t addr_type, u16_t net_idx)
{
    bt_addr_le_t remote_addr = {0};
    int result = 0;

    if (!addr || addr_type > BLE_ADDR_RANDOM) {
        BT_ERR("%s, Invalid parameter", __func__);
        return -EINVAL;
    }

    memcpy(remote_addr.val, addr, 6);
    remote_addr.type = addr_type;

    result = bt_mesh_gattc_conn_create(&remote_addr, BT_UUID_MESH_PROXY_VAL);
    if (result < 0) {
        return result;
    }

    /* Store corresponding net_idx which can be used for sending Proxy Configuration */
    servers[result].net_idx = net_idx;
    return 0;
}


int bt_mesh_gattc_conn_create(bt_addr_le_t *peer_addr, u16_t service_uuid)
{
	int i, rc;
	u8_t zero[6] = {0};
	struct ble_gap_conn_params conn_params;

    if (!peer_addr || !memcmp(peer_addr->val, zero, 6) || (peer_addr->type > BLE_ADDR_RANDOM)) {
        return false;
    }

    if (service_uuid != BT_UUID_MESH_PROV_VAL &&
            service_uuid != BT_UUID_MESH_PROXY_VAL) {
        BT_ERR("Invalid service uuid 0x%04x", service_uuid);
        return -EINVAL;
    }
	
    /* Check if already creating connection with the device */
    for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
        if (!memcmp(bt_mesh_gattc_info[i].addr.val, peer_addr->val, 6)) {
            BT_WARN("Already create connection with %s", bt_hex(peer_addr->val, 6));
            return -EALREADY;
        }
    }	

    /* Find empty element in queue to store device info */
    for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
        if ((bt_mesh_gattc_info[i].handle == 0) &&
            (bt_mesh_gattc_info[i].service_uuid == 0x0000)) {
            memcpy(bt_mesh_gattc_info[i].addr.val, peer_addr->val, 6);
            bt_mesh_gattc_info[i].addr.type = peer_addr->type;
            /* Service to be found after exhanging mtu size */
            bt_mesh_gattc_info[i].service_uuid = service_uuid;
            break;
        }
    }
	
    if (i == ARRAY_SIZE(bt_mesh_gattc_info)) {
        BT_WARN("gattc info is full");
        return -ENOMEM;
    }	

	conn_params.itvl_min = 0x18;
    conn_params.itvl_max = 0x18;
    conn_params.latency = 0;
    conn_params.supervision_timeout = 0x64;
    conn_params.scan_itvl = 0x0020;
    conn_params.scan_window = 0x0020;
    conn_params.min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
    conn_params.max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;
    rc = ble_gap_disc_cancel();
	if(rc){
        BT_ERR("disc cancel error");
	}
	rc = ble_gap_connect(g_mesh_addr_type, peer_addr, BLE_HS_FOREVER, &conn_params, provisioner_proxy_event, NULL);
	if(rc == 0){
		provisioner_pbg_count_inc();
	}else{
		memset(&bt_mesh_gattc_info[i], 0, sizeof(struct gattc_prov_info));
        bt_mesh_gattc_info[i].mtu = 23; 			/* Default MTU_SIZE 23 */
        bt_mesh_gattc_info[i].wr_desc_done = false;	
		BT_ERR("ble gap connect err, rc:%d", rc);
	}
	
	return rc;
}


static void proxy_connected(bt_mesh_addr_t *addr, u16 handle, int id)
{
    struct bt_mesh_proxy_server *server = NULL;

    if (!servers[id].handle) {
        server = &servers[id];
    }

    if (!server) {
        BT_ERR("No free Proxy Server objects");
        /** Disconnect current connection, clear part of prov_link
         *  information, like uuid, dev_addr, linking flag, etc.
         */
        bt_mesh_gattc_disconnect(handle);
        return;
    }

    server->handle = handle;
    server->conn_type = NONE;

    bt_mesh_gattc_exchange_mtu(id);//发起交换MTU
    return;
}

int bt_mesh_gattc_write_no_rsp(u16 handle,
                               const struct bt_mesh_gatt_attr *attr,
                               const void *data, u16_t len)
{
    u16_t conn_id;
    int i;

    for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
        if (handle == bt_mesh_gattc_info[i].handle) {
            break;
        }
    }

    if (i == ARRAY_SIZE(bt_mesh_gattc_info)) {
        BT_ERR("Conn %d not found", handle);
        /** Here we return 0 for prov_send() return value check in provisioner.c
         */
        return 0;
    }

    conn_id = bt_mesh_gattc_info[i].handle;

    struct os_mbuf *om;
    int rc;

    om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return -1;
    }

    rc = ble_gattc_write_no_rsp(conn_id, bt_mesh_gattc_info[i].data_in_handle, om);
    if (rc != 0) {
        return -1;
    }

    return 0;
}

static void proxy_disconnected(bt_mesh_addr_t *addr, u16 handle, u8_t reason)
{
    struct bt_mesh_proxy_server *server = find_server(handle);

    BT_DBG("handle is %d, reason 0x%02x", handle,reason);

    if (!server) {
        BT_ERR("No Proxy Server object found");
        return;
    }

#if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && MYNEWT_VAL(BLE_MESH_PB_GATT))
    if (server->conn_type == PROV) {
        provisioner_pb_gatt_close(handle, reason);
    }
#endif


/*
#if CONFIG_BLE_MESH_GATT_PROXY_CLIENT
    if (server->conn_type == PROXY) {
        if (proxy_client_disconnect_cb) {
            proxy_client_disconnect_cb(addr, server - servers, server->net_idx, reason);
        }
    }
#endif
*/
    k_delayed_work_cancel(&server->sar_timer);
    server->handle = 0;
    server->conn_type = NONE;
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)
    server->net_idx = BT_MESH_KEY_UNUSED;
#endif

    return;
}

static ssize_t proxy_write_ccc(bt_mesh_addr_t *addr, u16 handle)
{
    struct bt_mesh_proxy_server *server = find_server(handle);

    if (!server) {
        BT_ERR("No Proxy Server object found");
        return -ENOTCONN;
    }

    if (server->conn_type == NONE) {
        server->conn_type = PROXY;

        //if (proxy_client_connect_cb) {
            //proxy_client_connect_cb(addr, server - servers, server->net_idx);
       // }
        return 0;
    }
    printf("server->conn_type:%d\n",server->conn_type);
    return -EINVAL;
}

u16_t bt_mesh_gattc_get_service_uuid(u16 handle)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(bt_mesh_gattc_info); i++) {
        if (handle == bt_mesh_gattc_info[i].handle) {
            break;
        }
    }

    if (i == ARRAY_SIZE(bt_mesh_gattc_info)) {
        return 0;
    }

    return bt_mesh_gattc_info[i].service_uuid;
}

static void filter_status(struct bt_mesh_proxy_server *server,
                          struct bt_mesh_net_rx *rx,
                          struct os_mbuf *buf)
{
    u8_t filter_type = 0U;
    u16_t list_size = 0U;

    if (buf->om_len != 3) {
        BT_ERR("Invalid Proxy Filter Status length %d", buf->om_len);
        return;
    }

    filter_type = net_buf_simple_pull_u8(buf);
    if (filter_type > 0x01) {
        BT_ERR("Invalid proxy filter type 0x%02x", filter_type);
        return;
    }

    list_size = net_buf_simple_pull_be16(buf);

    BT_INFO("filter_type 0x%02x, list_size %d", filter_type, list_size);

    return;
}


static void proxy_cfg(struct bt_mesh_proxy_server *server)
{
    struct os_mbuf *buf = NET_BUF_SIMPLE(29);
    struct bt_mesh_net_rx rx = {0};
    u8_t opcode = 0U;
    int err = 0;

    if (server->buf->om_len > 29) {
        BT_ERR("Too large proxy cfg pdu (len %d)", server->buf->om_len);
        return;
    }

    err = bt_mesh_net_decode(server->buf, BT_MESH_NET_IF_PROXY_CFG,
                             &rx, buf);
    if (err) {
        BT_ERR("Failed to decode Proxy Configuration (err %d)", err);
        return;
    }

    if (!BT_MESH_ADDR_IS_UNICAST(rx.ctx.addr)) {
        BT_ERR("Proxy Configuration from non-unicast addr 0x%04x", rx.ctx.addr);
        return;
    }

    rx.local_match = 1U;
/*
    if (bt_mesh_rpl_check(&rx, NULL)) {
        BT_WARN("Replay: src 0x%04x dst 0x%04x seq 0x%06x",
                rx.ctx.addr, rx.ctx.recv_dst, rx.seq);
        return;
    }
*/
    /* Remove network headers */
    net_buf_simple_pull(buf, BT_MESH_NET_HDR_LEN);

    BT_DBG("%u bytes: %s", buf->om_len, bt_hex(buf->om_data, buf->om_len));

    if (buf->om_len < 3) {
        BT_WARN("Too short proxy configuration PDU");
        return;
    }

    opcode = net_buf_simple_pull_u8(buf);

    switch (opcode) {
    case BLE_MESH_PROXY_CFG_FILTER_STATUS:
        filter_status(server, &rx, buf);
        break;
    default:
        BT_WARN("Unknown Proxy Configuration OpCode 0x%02x", opcode);
        break;
    }
}

static void proxy_complete_pdu(struct bt_mesh_proxy_server *server)
{
    //printf("complete_pdu len:%d:data:%s\n",server->buf->om_len,bt_hex(server->buf->om_data, server->buf->om_len));
	switch (server->msg_type) {
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)

    case BT_MESH_PROXY_NET_PDU:
        BT_INFO("Mesh Network PDU");
        bt_mesh_net_recv(server->buf, 0, BT_MESH_NET_IF_PROXY);
        break;
    case BT_MESH_PROXY_BEACON:
        BT_INFO("Mesh Beacon PDU");
        bt_mesh_beacon_recv(server->buf);
        break;
    case BT_MESH_PROXY_CONFIG:
        BT_INFO("Mesh Configuration PDU");
        proxy_cfg(server);
        break;
#endif /* CONFIG_BLE_MESH_GATT_PROXY_CLIENT */
#if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && MYNEWT_VAL(BLE_MESH_PB_GATT))
    case BT_MESH_PROXY_PROV:
        BT_INFO("Mesh Provisioning PDU");
        provisioner_pb_gatt_recv(server->handle, server->buf);
        break;
#endif
    default:
        BT_WARN("Unhandled Message Type 0x%02x", server->msg_type);
        break;
    }

    net_buf_simple_init(server->buf, 0);

}


static int proxy_send(u16 handle, const void *data, u16_t len)
{
    BT_DBG("%u bytes: %s", len, bt_hex(data, len));

    return bt_mesh_gattc_write_no_rsp(handle, NULL, data, len);
}


static int proxy_segment_and_send(uint16_t conn_handle, u8_t type,
                                  struct os_mbuf *msg)
{
    u16_t mtu = 0U;
    int err = 0;

    if (conn_handle == 0xffff || conn_handle == 0) {
        BT_ERR("%s, Invalid parameter", __func__);
        return -EINVAL;
    }

    BT_DBG("conn %d type 0x%02x len %u: %s", conn_handle, type, msg->om_len,
           bt_hex(msg->om_data, msg->om_len));

    mtu = bt_mesh_gattc_get_mtu_info(conn_handle);
    if (!mtu) {
        BT_ERR("Conn %d used to get mtu not exists", conn_handle);
        return -ENOTCONN;
    }

    /* ATT_MTU - OpCode (1 byte) - Handle (2 bytes) */
    mtu -= 3;
	printf("mtu:%d,data len:%d\n",mtu,msg->om_len);
    if (mtu > msg->om_len) {
        net_buf_simple_push_u8(msg, PDU_HDR(SAR_COMPLETE, type));
        return proxy_send(conn_handle, msg->om_data, msg->om_len);
    }

    net_buf_simple_push_u8(msg, PDU_HDR(SAR_FIRST, type));
    err = proxy_send(conn_handle, msg->om_data, mtu);
    net_buf_simple_pull(msg, mtu);

    while (msg->om_len) {
        if (msg->om_len + 1 < mtu) {
            net_buf_simple_push_u8(msg, PDU_HDR(SAR_LAST, type));
            err = proxy_send(conn_handle, msg->om_data, msg->om_len);
            break;
        }

        net_buf_simple_push_u8(msg, PDU_HDR(SAR_CONT, type));
        err = proxy_send(conn_handle, msg->om_data, mtu);
        net_buf_simple_pull(msg, mtu);
    }

    return err;
}


int bt_mesh_proxy_client_send(u16 handle, u8_t type,
                              struct os_mbuf *msg)
{
    struct bt_mesh_proxy_server *server = find_server(handle);

    if (!server) {
        BT_ERR("No Proxy Server object found");
        return -ENOTCONN;
    }

    if ((server->conn_type == 1) != (type == BT_MESH_PROXY_PROV)) {
        BT_ERR("Invalid PDU type for Proxy Server");
        return -EINVAL;
    }

    return proxy_segment_and_send(handle, type, msg);
}

bool bt_mesh_proxy_client_relay(struct os_mbuf *buf, u16_t dst)
{
    bool send = false;
    int err = 0;
    int i;

    for (i = 0; i < ARRAY_SIZE(servers); i++) {
        struct bt_mesh_proxy_server *server = &servers[i];
        struct os_mbuf *msg = NET_BUF_SIMPLE(32);

        if (!server->handle || server->conn_type != PROXY) {
			os_mbuf_free_chain(msg);
            continue;
        }

        /* Proxy PDU sending modifies the original buffer,
         * so we need to make a copy.
         */
        net_buf_simple_init(msg, 1);
        net_buf_simple_add_mem(msg, buf->om_data, buf->om_len);

        err = bt_mesh_proxy_client_send(server->handle, BT_MESH_PROXY_NET_PDU, msg);
        if (err) {
            BT_ERR("Failed to send proxy network message (err %d)", err);
        } else {
            BT_INFO("%u bytes to dst 0x%04x", buf->om_len, dst);
            send = true;
        }

	    os_mbuf_free_chain(msg);
    }

    return send;
}


static int beacon_send(u16 conn_handle, struct bt_mesh_subnet *sub)
{
    int rc;
    struct os_mbuf *buf = NET_BUF_SIMPLE(23);

	net_buf_simple_init(buf, 1);
	bt_mesh_beacon_create(sub, buf);


    net_buf_simple_init(buf, 1);
    bt_mesh_beacon_create(sub, buf);

    rc = bt_mesh_proxy_client_send(conn_handle, BT_MESH_PROXY_BEACON, buf);
	os_mbuf_free_chain(buf);
	return rc;
}


bool bt_mesh_proxy_client_beacon_send(struct bt_mesh_subnet *sub)
{
    bool send = false;
    int err = 0;
    int i;

    /* NULL means we send Secure Network Beacon on all subnets */
    if (!sub) {

#if (!MYNEWT_VAL(BLE_MESH_PROVISIONER))
        if (bt_mesh_is_provisioned()) {
            for (i = 0; i < ARRAY_SIZE(bt_mesh.sub); i++) {
                if (bt_mesh.sub[i].net_idx != BT_MESH_KEY_UNUSED) {
                    send = bt_mesh_proxy_client_beacon_send(&bt_mesh.sub[i]);
                }
            }
            return send;
        }
#endif /* CONFIG_BLE_MESH_NODE */


#if MYNEWT_VAL(BLE_MESH_PROVISIONER)
        if (bt_mesh_is_provisioner_en()) {
            for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
                if ( bt_mesh.p_sub[i].net_idx != BT_MESH_KEY_UNUSED) {
                    send = bt_mesh_proxy_client_beacon_send(&bt_mesh.p_sub[i]);
                }
            }
            return send;
        }
#endif
        return send;
    }

    for (i = 0; i < ARRAY_SIZE(servers); i++) {
        if (servers[i].handle && servers[i].conn_type == PROXY) {
            err = beacon_send(servers[i].handle, sub);
            if (err) {
                BT_ERR("Failed to send proxy beacon message (err %d)", err);
            } else {
                send = true;
            }
        }
    }

    return send;
}


#define ATTR_IS_PROV(uuid) (uuid == BT_UUID_MESH_PROV_VAL)

static ssize_t proxy_recv(u16 handle,
                          const struct bt_mesh_gatt_attr *attr, const void *buf,
                          u16_t len, u16_t offset, u8_t flags)
{
    struct bt_mesh_proxy_server *server = find_server(handle);
    const u8_t *data = buf;
    u16_t srvc_uuid = 0U;

    if (!server) {
        BT_ERR("No Proxy Server object found");
        return -ENOTCONN;
    }

    if (len < 1) {
        BT_WARN("Too small Proxy PDU");
        return -EINVAL;
    }

    srvc_uuid = bt_mesh_gattc_get_service_uuid(handle);
    if (!srvc_uuid) {
        BT_ERR("No service uuid found");
        return -ENOTCONN;
    }

    if (ATTR_IS_PROV(srvc_uuid) != (PDU_TYPE(data) == BT_MESH_PROXY_PROV)) {
        BT_WARN("Proxy PDU type doesn't match GATT service uuid");
        return -EINVAL;
    }

    if (len - 1 > net_buf_simple_tailroom(server->buf)) {
        BT_WARN("Too big proxy PDU");
        return -EINVAL;
    }

    switch (PDU_SAR(data)) {
    case SAR_COMPLETE:
        if (server->buf->om_len) {
            BT_WARN("Complete PDU while a pending incomplete one");
            return -EINVAL;
        }

        server->msg_type = PDU_TYPE(data);
        net_buf_simple_add_mem(server->buf, data + 1, len - 1);
        proxy_complete_pdu(server);
        break;

    case SAR_FIRST:
        if (server->buf->om_len) {
            BT_WARN("First PDU while a pending incomplete one");
            return -EINVAL;
        }

        k_delayed_work_submit(&server->sar_timer, PROXY_SAR_TIMEOUT);
        server->msg_type = PDU_TYPE(data);
        net_buf_simple_add_mem(server->buf, data + 1, len - 1);
        break;

    case SAR_CONT:
        if (!server->buf->om_len) {
            BT_WARN("Continuation with no prior data");
            return -EINVAL;
        }

        if (server->msg_type != PDU_TYPE(data)) {
            BT_WARN("Unexpected message type in continuation");
            return -EINVAL;
        }

        k_delayed_work_submit(&server->sar_timer, PROXY_SAR_TIMEOUT);
        net_buf_simple_add_mem(server->buf, data + 1, len - 1);
        break;

    case SAR_LAST:
        if (!server->buf->om_len) {
            BT_WARN("Last SAR PDU with no prior data");
            return -EINVAL;
        }

        if (server->msg_type != PDU_TYPE(data)) {
            BT_WARN("Unexpected message type in last SAR PDU");
            return -EINVAL;
        }

        k_delayed_work_cancel(&server->sar_timer);
        net_buf_simple_add_mem(server->buf, data + 1, len - 1);
        proxy_complete_pdu(server);
        break;
    }

    return len;
}


static int send_proxy_cfg(uint16 handle, u16_t net_idx, struct bt_mesh_proxy_cfg_pdu *cfg)
{
    struct bt_mesh_msg_ctx ctx = {
        .net_idx  = net_idx,
        .app_idx  = BT_MESH_KEY_UNUSED,        /* CTL shall be set to 1 */
        .addr     = BT_MESH_ADDR_UNASSIGNED,   /* DST shall be set to the unassigned address */
        .send_ttl = 0U,                         /* TTL shall be set to 0 */
    };
    struct bt_mesh_net_tx tx = {
        .ctx = &ctx,
        .src = bt_mesh_primary_addr(),
    };
    struct os_mbuf *buf = NULL;
    u16_t alloc_len = 0U;
    int err = 0;


if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && bt_mesh_is_provisioner_en()) 
    tx.sub = provisioner_subnet_get(net_idx);
else
    tx.sub = bt_mesh_subnet_get(net_idx);


    if (!tx.sub) {
        BT_ERR("Invalid NetKeyIndex 0x%04x", net_idx);
        return -EIO;
    }

    switch (cfg->opcode) {
    case BLE_MESH_PROXY_CFG_FILTER_SET:
        if (cfg->set.filter_type > 0x01) {
            BT_ERR("Invalid proxy filter type 0x%02x", cfg->set.filter_type);
            return -EINVAL;
        }

        alloc_len = sizeof(cfg->opcode) + sizeof(cfg->set.filter_type);
        break;
    case BLE_MESH_PROXY_CFG_FILTER_ADD:
        if (cfg->add.addr == NULL || cfg->add.addr_num == 0) {
            BT_ERR("Empty proxy addr list to add");
            return -EINVAL;
        }

        alloc_len = sizeof(cfg->opcode) + (cfg->add.addr_num << 1);
        break;
    case BLE_MESH_PROXY_CFG_FILTER_REMOVE:
        if (cfg->remove.addr == NULL || cfg->remove.addr_num == 0) {
            BT_ERR("Empty proxy addr list to remove");
            return -EINVAL;
        }

        alloc_len = sizeof(cfg->opcode) + (cfg->remove.addr_num << 1);
        break;
    default:
        BT_ERR("Unknown Proxy Configuration opcode 0x%02x", cfg->opcode);
        return -EINVAL;
    }

    /**
     * For Proxy Configuration PDU:
     * 1 octet Proxy PDU type + 9 octets network pdu header + Tranport PDU + 8 octets NetMIC
     */
    buf = NET_BUF_SIMPLE(1 + BT_MESH_NET_HDR_LEN + alloc_len + 8);
    if (!buf) {
        return -ENOMEM;
    }

    net_buf_reserve(buf, 10);

    net_buf_simple_add_u8(buf, cfg->opcode);
    switch (cfg->opcode) {
    case BLE_MESH_PROXY_CFG_FILTER_SET:
        net_buf_simple_add_u8(buf, cfg->set.filter_type);
        break;
    case BLE_MESH_PROXY_CFG_FILTER_ADD:
        for (u16_t i = 0U; i < cfg->add.addr_num; i++) {
            net_buf_simple_add_le16(buf, cfg->add.addr[i]);
        }
        break;
    case BLE_MESH_PROXY_CFG_FILTER_REMOVE:
        for (u16_t i = 0U; i < cfg->remove.addr_num; i++) {
            net_buf_simple_add_le16(buf, cfg->remove.addr[i]);
        }
        break;
    }

    BT_INFO("len %u bytes: %s", buf->om_len, bt_hex(buf->om_data, buf->om_len));

    err = bt_mesh_net_encode(&tx, buf, true);
    if (err) {
        BT_ERR("Encoding proxy message failed (err %d)", err);
        os_mbuf_free(buf);
        return err;
    }

    err = bt_mesh_proxy_client_send(handle, BT_MESH_PROXY_CONFIG, buf);
    if (err) {
        BT_ERR("Failed to send proxy cfg message (err %d)", err);
    }

    os_mbuf_free_chain(buf);
    return err;
}


int bt_mesh_proxy_client_cfg_send(u8_t conn_handle, u16_t net_idx,
                                  struct bt_mesh_proxy_cfg_pdu *pdu)
{

    if (conn_handle == 0 || !pdu || pdu->opcode > BLE_MESH_PROXY_CFG_FILTER_REMOVE) {
        BT_ERR("%s, Invalid parameter", __func__);
        return -EINVAL;
    }

    BT_DBG("conn_handle %d, net_idx 0x%04x", conn_handle, net_idx);

    struct bt_mesh_proxy_server *server = find_server(conn_handle);
    if (!server) {
        BT_ERR("No Proxy Server object found");
        return -ENOTCONN;
    }

    /**
     * Check if net_idx used to encrypt Proxy Configuration are the same
     * with the one added when creating proxy connection.
     */

    if (server->net_idx != net_idx) {
        BT_ERR("NetKeyIndex 0x%04x mismatch, expect 0x%04x",
                net_idx, servers[conn_handle].net_idx);
        return -EIO;
    }

    return send_proxy_cfg(conn_handle, net_idx, pdu);
}


static ssize_t prov_write_ccc(bt_mesh_addr_t *addr, u16 handle)
{
    struct bt_mesh_proxy_server *server = find_server(handle);

    if (!server) {
        BT_ERR("No Proxy Server object found");
        return -ENOTCONN;
    }

    if (server->conn_type == NONE) {
        server->conn_type = PROV;

        if (provisioner_set_prov_conn(addr->val, handle)) {
            bt_mesh_gattc_disconnect(handle);
            return -EIO;
        }
        return provisioner_pb_gatt_open(handle, addr->val);
    }

    return -ENOMEM;
}


static ssize_t prov_recv_ntf(u16 handle, u8_t *data, u16_t len)
{
    struct bt_mesh_proxy_server *server = find_server(handle);

    if (!server) {
        BT_ERR("No Proxy Server object found");
        return -ENOTCONN;
    }

    if (server->conn_type == PROV) {
        return proxy_recv(handle, NULL, data, len, 0, 0);
    }

    return -EINVAL;
}


static ssize_t proxy_recv_ntf(u16 handle, u8_t *data, u16_t len)
{
    struct bt_mesh_proxy_server *server = find_server(handle);

    if (!server) {
        BT_ERR("No Proxy Server object found");
        return -ENOTCONN;
    }

    if (server->conn_type == PROXY) {
        return proxy_recv(handle, NULL, data, len, 0, 0);
    }

    return -EINVAL;
}

static struct bt_mesh_prov_conn_cb conn_callbacks = {
    .connected = proxy_connected,
    .disconnected = proxy_disconnected,
#if (MYNEWT_VAL(BLE_MESH_PROVISIONER) && MYNEWT_VAL(BLE_MESH_PB_GATT))
    .prov_write_descr = prov_write_ccc,
    .prov_notify = prov_recv_ntf,
#endif /* CONFIG_BLE_MESH_PROVISIONER && CONFIG_BLE_MESH_PB_GATT */
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)
    .proxy_write_descr = proxy_write_ccc,
    .proxy_notify = proxy_recv_ntf,
#endif /* CONFIG_BLE_MESH_GATT_PROXY_CLIENT */
};

static void proxy_sar_timeout(struct ble_npl_event *work)
{
    struct bt_mesh_proxy_server *server ;
    int rc;
	
    BT_WARN("%s", __func__);

    server = ble_npl_event_get_arg(work);

    assert(server != NULL);

	if ((server->handle != 0)){

	     bt_mesh_gattc_disconnect(server->handle);
	} 
   
}


void bt_mesh_gattc_init(void)
{
	int i;
    for (i=0; i<ARRAY_SIZE(bt_mesh_gattc_info); i++) {
		memset(&bt_mesh_gattc_info[i], 0, sizeof(struct gattc_prov_info));
		bt_mesh_gattc_info[i].handle = 0;
        bt_mesh_gattc_info[i].mtu = 23; 			/* Default MTU_SIZE 23 */
        bt_mesh_gattc_info[i].wr_desc_done = false;
    }

    for (i = 0; i < ARRAY_SIZE(servers); i++) {
        struct bt_mesh_proxy_server *server = &servers[i];
		server->handle = 0x0;
        k_delayed_work_init(&server->sar_timer, proxy_sar_timeout);
		k_delayed_work_add_arg(&server->sar_timer, &servers[i]);
        //server->buf.size = SERVER_BUF_SIZE;
        //server->buf.__buf = server_buf_data + (i * SERVER_BUF_SIZE);
        server->buf = NET_BUF_SIMPLE(68);
		
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)
        server->net_idx = BT_MESH_KEY_UNUSED;
#endif
    }
    bt_mesh_gattc_conn_cb_register(&conn_callbacks);
}


void bt_mesh_gattc_deinit(void){

	 int i;

    /* Initialize the server receive buffers */
    for (i = 0; i < ARRAY_SIZE(servers); i++) {
        struct bt_mesh_proxy_server *server = &servers[i];
        k_delayed_work_free(&server->sar_timer);
        if(server->buf){
            os_mbuf_free_chain(server->buf);
		}
        memset(server, 0, sizeof(struct bt_mesh_proxy_server));

    }

    bt_mesh_gattc_conn_cb_deregister();

    return 0;

}



#endif


