
#ifndef __PROVISIONER_H__
#define __PROVISIONER_H__

#include "mesh/mesh.h"


#if (MYNEWT_VAL(BLE_MESH_PB_ADV))
#define CONFIG_BT_MESH_PBA_SAME_TIME 2 
#else
#define CONFIG_BT_MESH_PBA_SAME_TIME 0
#endif

#if (MYNEWT_VAL(BLE_MESH_PB_GATT))
#define CONFIG_BT_MESH_PBG_SAME_TIME 2
#else
#define CONFIG_BT_MESH_PBG_SAME_TIME 0
#endif

#define RM_AFTER_PROV  BIT(0)
#define START_PROV_NOW BIT(1)
#define FLUSHABLE_DEV  BIT(2)


struct bt_mesh_unprov_dev_add {
    u8_t  addr[6];
    u8_t  addr_type;
    u8_t  uuid[16];
    u16_t oob_info;
    u8_t  bearer;
};

struct bt_mesh_device_delete {
    u8_t addr[6];
    u8_t addr_type;
    u8_t uuid[16];
};

#define MESH_NAME_SIZE  31

struct bt_mesh_prov_node_info {
    bool  provisioned;      /* device provisioned flag */
    bt_addr_le_t addr;      /* device address */
    u8_t  uuid[16];         /* node uuid */
    u16_t oob_info;         /* oob info contained in adv pkt */
    u8_t  element_num;      /* element contained in this node */
    u16_t unicast_addr;     /* primary unicast address of this node */
    u16_t net_idx;          /* Netkey index got during provisioning */
    u8_t  flags;            /* Key refresh flag and iv update flag */
    u32_t iv_index;         /* IV Index */
	u8_t  dev_key[16];		
	char  node_name[MESH_NAME_SIZE];
};


#define NET_IDX_FLAG  BIT(0)
#define FLAGS_FLAG    BIT(1)
#define IV_INDEX_FLAG BIT(2)

struct bt_mesh_prov_data_info {
    union {
        u16_t net_idx;
        u8_t  flags;
        u32_t iv_index;
    };
    u8_t flag;
};

typedef struct {
    u8_t type;
    u8_t val[6];
} bt_mesh_addr_t;

int provisioner_pb_gatt_recv(u16_t conn_handle, struct os_mbuf *buf);

void provisioner_pbg_count_inc(void);

void provisioner_unprov_dev_num_dec(void);

void provisioner_pbg_count_dec(void);

const struct bt_mesh_provisioner *provisioner_get_prov_info(void);

int provisioner_all_nodes_reset(void);

int provisioner_pb_gatt_open(u16_t conn_handle, u8_t *addr);

int provisioner_set_prov_conn(const u8_t addr[6], u16 handle);

void bt_mesh_provisioner_clear_link_info(const u8_t addr[6]);

const u8_t *provisioner_net_key_get(u16_t net_idx);

struct bt_mesh_subnet *provisioner_subnet_get(u16_t net_idx);

bool provisioner_check_msg_dst_addr(u16_t dst_addr);

const u8_t *provisioner_get_device_key(u16_t dst_addr);
int provisioner_pb_gatt_close(u16_t conn_handle, u8_t reason);

struct bt_mesh_app_key *provisioner_app_key_find(u16_t app_idx);

u16_t provisioner_get_prov_node_count(void);

int provisioner_prov_init(const struct bt_mesh_provisioner *provisioner_info);
void provisioner_link_connected(u16 handle,u16 index);

int provisioner_prov_deinit(void);

bool provisioner_flags_match(struct os_mbuf *buf);

u16_t provisioner_srv_uuid_recv(struct os_mbuf *buf);

void provisioner_srv_data_recv(struct os_mbuf *buf, const bt_addr_le_t *addr, u16_t uuid);

void provisioner_beacon_recv(struct os_mbuf *buf, bt_addr_le_t *addr);

bool provisioner_is_node_provisioned(u8_t *dev_addr);

int bt_mesh_prov_input_data(u8_t *num, u8_t size, bool num_flag);

int bt_mesh_prov_oob_pub_key(const u8_t pub_key_x[32], const u8_t pub_key_y[32]);

int bt_mesh_provisioner_init(void);

int bt_mesh_provisioner_add_unprov_dev(struct bt_mesh_unprov_dev_add *add_dev, u8_t flags);

int bt_mesh_provisioner_delete_device(struct bt_mesh_device_delete *del_dev);

int bt_mesh_provisioner_set_dev_uuid_match(u8_t offset, u8_t length,
        									const u8_t *match, bool prov_flag);

void bt_mesh_provisioner_prov_adv_recv(struct os_mbuf *buf,
                                       const bt_mesh_addr_t *addr, s8_t rssi);
#if 0
typedef void (*prov_adv_pkt_cb)(const u8_t addr[6], const u8_t addr_type,	
                                const u8_t adv_type, const u8_t dev_uuid[16],
                                u16_t oob_info, bt_mesh_prov_bearer_t bearer);

int bt_mesh_prov_adv_pkt_cb_register(prov_adv_pkt_cb cb);
#endif

void bt_mesh_provisioner_set_prov_bearer(bt_mesh_prov_bearer_t bearers, bool clear);

int bt_mesh_provisioner_set_prov_data_info(struct bt_mesh_prov_data_info *info);

int bt_mesh_provisioner_get_all_node_unicast_addr(struct os_mbuf *buf);

int bt_mesh_provisioner_set_node_name(int node_index, const char *name);

const char *bt_mesh_provisioner_get_node_name(int node_index);

int bt_mesh_provisioner_get_node_index(const char *name);

struct bt_mesh_prov_node_info *bt_mesh_provisioner_get_node_info(u16_t unicast_addr);

u32_t bt_mesh_provisioner_get_net_key_count(void);

u32_t bt_mesh_provisioner_get_app_key_count(void);

int bt_mesh_provisioner_local_app_key_add(const u8_t app_key[16], u16_t net_idx, u16_t *app_idx);

const u8_t *bt_mesh_provisioner_local_app_key_get(u16_t net_idx, u16_t app_idx);

int bt_mesh_provisioner_local_app_key_delete(u16_t net_idx, u16_t app_idx);

int bt_mesh_provisioner_local_net_key_add(const u8_t net_key[16], u16_t *net_idx);

const u8_t *bt_mesh_provisioner_local_net_key_get(u16_t net_idx);

int bt_mesh_provisioner_local_net_key_delete(u16_t net_idx);

int bt_mesh_provisioner_local_net_key_update(const u8_t net_key[16], u16_t net_idx);

int bt_mesh_provisioner_local_app_key_update(const u8_t app_key[16], u16_t net_idx, u16_t app_idx);

int bt_mesh_provisioner_get_own_unicast_addr(u16_t *addr, u8_t *elem_num);

int bt_mesh_provisioner_bind_local_model_app_idx(u16_t elem_addr, u16_t mod_id,
        										u16_t cid, u16_t app_idx);

int bt_mesh_provisioner_bind_local_app_net_idx(u16_t net_idx, u16_t app_idx);

int bt_mesh_provisioner_print_local_element_info(void);


#endif

