
#ifndef __PROVISIONER_PROXY_H__
#define __PROVISIONER_PROXY_H__

#define BLE_MESH_PROXY_ADV_NET_ID           0x00
#define BLE_MESH_PROXY_ADV_NODE_ID          0x01

#define BT_MESH_PROXY_NET_PDU   0x00
#define BT_MESH_PROXY_BEACON    0x01
#define BT_MESH_PROXY_CONFIG    0x02
#define BT_MESH_PROXY_PROV      0x03

#define BLE_MESH_PROXY_CFG_FILTER_SET       0x00
#define BLE_MESH_PROXY_CFG_FILTER_ADD       0x01
#define BLE_MESH_PROXY_CFG_FILTER_REMOVE    0x02
#define BLE_MESH_PROXY_CFG_FILTER_STATUS    0x03

enum {
    BLE_MESH_DEV_ENABLE,
    BLE_MESH_DEV_READY,
    BLE_MESH_DEV_ID_STATIC_RANDOM,
    BLE_MESH_DEV_HAS_PUB_KEY,
    BLE_MESH_DEV_PUB_KEY_BUSY,

    BLE_MESH_DEV_ADVERTISING,
    BLE_MESH_DEV_KEEP_ADVERTISING,
    BLE_MESH_DEV_SCANNING,
    BLE_MESH_DEV_EXPLICIT_SCAN,
    BLE_MESH_DEV_ACTIVE_SCAN,
    BLE_MESH_DEV_SCAN_FILTER_DUP,

    BLE_MESH_DEV_RPA_VALID,

    BLE_MESH_DEV_ID_PENDING,

    /* Total number of flags - must be at the end of the enum */
    BLE_MESH_DEV_NUM_FLAGS,
};

struct bt_mesh_uuid {
    u8_t type;
};

struct bt_mesh_gatt_attr {
    /** Attribute UUID */
    const struct bt_mesh_uuid *uuid;

    /** Attribute read callback
     *
     *  @param conn   The connection that is requesting to read
     *  @param attr   The attribute that's being read
     *  @param buf    Buffer to place the read result in
     *  @param len    Length of data to read
     *  @param offset Offset to start reading from
     *
     *  @return Number fo bytes read, or in case of an error
     *          BLE_MESH_GATT_ERR() with a specific ATT error code.
     */
    ssize_t (*read)(u16 handle,
                    const struct bt_mesh_gatt_attr *attr,
                    void *buf, u16_t len,
                    u16_t offset);

    /** Attribute write callback
     *
     *  @param conn   The connection that is requesting to write
     *  @param attr   The attribute that's being written
     *  @param buf    Buffer with the data to write
     *  @param len    Number of bytes in the buffer
     *  @param offset Offset to start writing from
     *  @param flags  Flags (BT_GATT_WRITE_*)
     *
     *  @return Number of bytes written, or in case of an error
     *          BLE_MESH_GATT_ERR() with a specific ATT error code.
     */
    ssize_t (*write)(u16_t handle,
                     const struct bt_mesh_gatt_attr *attr,
                     const void *buf, u16_t len,
                     u16_t offset, u8_t flags);

    /** Attribute user data */
    void *user_data;
    /** Attribute handle */
    u16_t handle;
    /** Attribute permissions */
    u8_t  perm;
};

typedef union {
    struct {
        u8_t  net_id[8];
        u16_t net_idx;
    } net_id;
    struct {
        u16_t src;
    } node_id;
} bt_mesh_proxy_adv_ctx_t;

struct bt_mesh_proxy_cfg_pdu {
    u8_t opcode;
    union {
        struct cfg_filter_set {
            u8_t filter_type;
        } set;
        struct cfg_addr_add {
            u16_t addr[10];
            u16_t  addr_num;
        } add;
        struct cfg_addr_remove {
            u16_t addr[10];
            u16_t  addr_num;
        } remove;
    };
};

int bt_mesh_gattc_conn_create(bt_addr_le_t *peer_addr, u16_t service_uuid);
void bt_mesh_gattc_init(void);
void bt_mesh_gattc_deinit(void);
u8_t bt_mesh_gattc_get_free_conn_count(void);
u16_t bt_mesh_gattc_get_mtu_info(u16_t handle);
int bt_mesh_gattc_write_no_rsp(u16_t handle,
                               const struct bt_mesh_gatt_attr *attr,
                               const void *data, u16_t len);
int provisioner_proxy_event(struct ble_gap_event *event, void *arg);
int bt_mesh_proxy_client_send(u16 handle, u8_t type,struct os_mbuf *msg);
int bt_mesh_proxy_client_connect(const u8_t addr[6], u8_t addr_type, u16_t net_idx);

bool is_scanning();
void set_scan_bit();
void clear_scan_bit();
bool bt_mesh_proxy_client_relay(struct os_mbuf *buf, u16_t dst);
bool bt_mesh_proxy_client_beacon_send(struct bt_mesh_subnet *sub);

struct bt_mesh_proxy_server *find_server(u16 handle);
int bt_mesh_proxy_client_cfg_send(u8_t conn_handle, u16_t net_idx,
                                  struct bt_mesh_proxy_cfg_pdu *pdu);



#endif

