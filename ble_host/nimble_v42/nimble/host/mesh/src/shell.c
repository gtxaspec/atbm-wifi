/** @file
 *  @brief Bluetooth Mesh shell
 *
 */

/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "syscfg/syscfg.h"

#if MYNEWT_VAL(BLE_MESH_SHELL)


#include <ctype.h>

#include "shell.h"
//#include "console/console.h"
#include "mesh/mesh.h"
#include "mesh/main.h"
#include "mesh/glue.h"
#include "mesh/testing.h"
#include "provisioner_proxy.h"
/* Private includes for raw Network & Transport layer access */
#include "net.h"
#include "access.h"
#include "mesh_priv.h"
#include "lpn.h"
#include "transport.h"
#include "foundation.h"
#include "testing.h"
#include "settings.h"
#include "cli.h"
#if MYNEWT_VAL(BLE_MESH_SHELL_MODELS)
#include "mesh/model_srv.h"
#include "mesh/model_cli.h"
#include "light_model.h"
#endif
#include "prov.h"
#if (MYNEWT_VAL(BLE_MESH_PROXY))
#include "proxy.h"
#endif
#if MYNEWT_VAL(BLE_MESH_PROVISIONER)
#include "provisioner.h"
#endif
#include "provisioner_proxy.h"


/* This should be higher priority (lower value) than main task priority */
#define BLE_MESH_SHELL_TASK_PRIO 126
#define BLE_MESH_SHELL_STACK_SIZE 768

//OS_TASK_STACK_DEFINE(g_blemesh_shell_stack, BLE_MESH_SHELL_STACK_SIZE);

//struct os_task mesh_shell_task;
static struct os_eventq mesh_shell_queue;

#define CID_NVAL   0xffff
#define CID_VENDOR  0x05C3

/* Vendor Model data */
#define VND_MODEL_ID_1 0x1234

struct kv_pair {
    char *key;
    int val;
};


/* Default net, app & dev key values, unless otherwise specified */
static const u8_t default_key[16] = {
	0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
	0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};

static struct {
	u16_t local;
	u16_t dst;
	u16_t net_idx;
	u16_t app_idx;
} net = {
	.local = BT_MESH_ADDR_UNASSIGNED,
	.dst = BT_MESH_ADDR_UNASSIGNED,
};

static struct bt_mesh_cfg_srv cfg_srv = {
	.relay = BT_MESH_RELAY_DISABLED,
	.beacon = BT_MESH_BEACON_ENABLED,
#if MYNEWT_VAL(BLE_MESH_FRIEND)
	.frnd = BT_MESH_FRIEND_ENABLED,
#else
	.frnd = BT_MESH_FRIEND_NOT_SUPPORTED,
#endif
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY)
	.gatt_proxy = BT_MESH_GATT_PROXY_DISABLED,
#else
	.gatt_proxy = BT_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif

	.default_ttl = 7,

	/* 3 transmissions with 20ms interval */
	.net_transmit = BT_MESH_TRANSMIT(2, 20),
	.relay_retransmit = BT_MESH_TRANSMIT(0, 10),
};

#define CUR_FAULTS_MAX 4

static u8_t cur_faults[CUR_FAULTS_MAX];
static u8_t reg_faults[CUR_FAULTS_MAX * 2];


extern int parse_arg_all(int argc, char **argv);
extern uint8_t parse_arg_uint8(char *name, int *out_status);
extern uint16_t parse_arg_uint16(char *name, int *out_status);
extern int parse_arg_mac(char *name, uint8_t *dst);
extern int parse_arg_byte_stream(char *name, int max_len, uint8_t *dst, int *out_len);
extern uint8_t parse_arg_uint8_dflt(char *name, uint8_t dflt, int *out_status);
extern int parse_arg_kv_dflt(char *name, const struct kv_pair *kvs, 
							int def_val, int *out_status);


static u8_t str2u8(const char *str)
{
	if (isdigit(str[0])) {
		return strtoul(str, NULL, 0);
	}

	return (!strcmp(str, "on") || !strcmp(str, "enable"));
}

static bool str2bool(const char *str)
{
	return str2u8(str);
}

static void get_faults(u8_t *faults, u8_t faults_size, u8_t *dst, u8_t *count)
{
	u8_t i, limit = *count;

	for (i = 0, *count = 0; i < faults_size && *count < limit; i++) {
		if (faults[i]) {
			*dst++ = faults[i];
			(*count)++;
		}
	}
}

static int fault_get_cur(struct bt_mesh_model *model, u8_t *test_id,
			 u16_t *company_id, u8_t *faults, u8_t *fault_count)
{
	printk("Sending current faults\n");

	*test_id = 0x00;
	*company_id = CID_VENDOR;

	get_faults(cur_faults, sizeof(cur_faults), faults, fault_count);

	return 0;
}

static int fault_get_reg(struct bt_mesh_model *model, u16_t cid,
			 u8_t *test_id, u8_t *faults, u8_t *fault_count)
{
	if (cid != CID_VENDOR) {
		printk("Faults requested for unknown Company ID 0x%04x\n", cid);
		return -EINVAL;
	}

	printk("Sending registered faults\n");

	*test_id = 0x00;

	get_faults(reg_faults, sizeof(reg_faults), faults, fault_count);

	return 0;
}

static int fault_clear(struct bt_mesh_model *model, uint16_t cid)
{
	if (cid != CID_VENDOR) {
		return -EINVAL;
	}

	memset(reg_faults, 0, sizeof(reg_faults));

	return 0;
}

static int fault_test(struct bt_mesh_model *model, uint8_t test_id,
		      uint16_t cid)
{
	if (cid != CID_VENDOR) {
		return -EINVAL;
	}

	if (test_id != 0x00) {
		return -EINVAL;
	}

	return 0;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
	.fault_get_cur = fault_get_cur,
	.fault_get_reg = fault_get_reg,
	.fault_clear = fault_clear,
	.fault_test = fault_test,
};

static struct bt_mesh_health_srv health_srv = {
	.cb = &health_srv_cb,
};

static struct bt_mesh_model_pub health_pub;

static void
health_pub_init(void)
{
	health_pub.msg  = BT_MESH_HEALTH_FAULT_MSG(CUR_FAULTS_MAX);
}
#if MYNEWT_VAL(BLE_MESH_CFG_CLI)

static struct bt_mesh_cfg_cli cfg_cli = {
};

#endif /* MYNEWT_VAL(BLE_MESH_CFG_CLI) */

#if MYNEWT_VAL(BLE_MESH_HEALTH_CLI)
void show_faults(u8_t test_id, u16_t cid, u8_t *faults, size_t fault_count)
{
	size_t i;

	if (!fault_count) {
		printk("Health Test ID 0x%02x Company ID 0x%04x: no faults\n",
		       test_id, cid);
		return;
	}

	printk("Health Test ID 0x%02x Company ID 0x%04x Fault Count %zu:\n",
	       test_id, cid, fault_count);

	for (i = 0; i < fault_count; i++) {
		printk("\t0x%02x\n", faults[i]);
	}
}

static void health_current_status(struct bt_mesh_health_cli *cli, u16_t addr,
				  u8_t test_id, u16_t cid, u8_t *faults,
				  size_t fault_count)
{
	printk("Health Current Status from 0x%04x\n", addr);
	show_faults(test_id, cid, faults, fault_count);
}

static struct bt_mesh_health_cli health_cli = {
	.current_status = health_current_status,
};

#endif /* MYNEWT_VAL(BLE_MESH_HEALTH_CLI) */

#if MYNEWT_VAL(BLE_MESH_SHELL_MODELS)
static struct bt_mesh_model_pub gen_onoff_pub;
static struct bt_mesh_model_pub gen_level_pub;
static struct bt_mesh_model_pub light_lightness_pub;
static struct bt_mesh_gen_onoff_srv_cb gen_onoff_srv_cb = {
	.get = light_model_gen_onoff_get,
	.set = light_model_gen_onoff_set,
};
static struct bt_mesh_gen_level_srv_cb gen_level_srv_cb = {
	.get = light_model_gen_level_get,
	.set = light_model_gen_level_set,
};
static struct bt_mesh_light_lightness_srv_cb light_lightness_srv_cb = {
	.get = light_model_light_lightness_get,
	.set = light_model_light_lightness_set,
};

void bt_mesh_set_gen_onoff_srv_cb(struct bt_mesh_gen_onoff_srv_cb *gen_onoff_cb)
{
	gen_onoff_srv_cb = *gen_onoff_cb;
}

void bt_mesh_set_gen_level_srv_cb(struct bt_mesh_gen_level_srv_cb *gen_level_cb)
{
	gen_level_srv_cb = *gen_level_cb;
}

void bt_mesh_set_light_lightness_srv_cb(struct bt_mesh_light_lightness_srv_cb *light_lightness_cb)
{
	light_lightness_srv_cb = *light_lightness_cb;
}

#endif

static struct bt_mesh_model root_models[] = {
	BT_MESH_MODEL_CFG_SRV(&cfg_srv),
	BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
#if MYNEWT_VAL(BLE_MESH_CFG_CLI)
	BT_MESH_MODEL_CFG_CLI(&cfg_cli),
#endif
#if MYNEWT_VAL(BLE_MESH_HEALTH_CLI)
	BT_MESH_MODEL_HEALTH_CLI(&health_cli),
#endif
#if MYNEWT_VAL(BLE_MESH_SHELL_MODELS)
	BT_MESH_MODEL_GEN_ONOFF_SRV(&gen_onoff_srv_cb, &gen_onoff_pub),
	BT_MESH_MODEL_GEN_ONOFF_CLI(),
	BT_MESH_MODEL_GEN_LEVEL_SRV(&gen_level_srv_cb, &gen_level_pub),
	BT_MESH_MODEL_GEN_LEVEL_CLI(),
	BT_MESH_MODEL_LIGHT_LIGHTNESS_SRV(&light_lightness_srv_cb, &light_lightness_pub),
#endif
};

static struct bt_mesh_model vnd_models[] = {
	BT_MESH_MODEL_VND(CID_VENDOR, VND_MODEL_ID_1,
			  BT_MESH_MODEL_NO_OPS, NULL, NULL),
};

static struct bt_mesh_elem elements[] = {
	BT_MESH_ELEM(0, root_models, vnd_models),
};

static const struct bt_mesh_comp comp = {
	.cid = CID_VENDOR,
	.elem = elements,
	.elem_count = ARRAY_SIZE(elements),
};

static u8_t hex2val(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	} else if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	} else if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	} else {
		return 0;
	}
}

static size_t hex2bin(const char *hex, u8_t *bin, size_t bin_len)
{
	size_t len = 0;

	while (*hex && len < bin_len) {
		bin[len] = hex2val(*hex++) << 4;

		if (!*hex) {
			len++;
			break;
		}

		bin[len++] |= hex2val(*hex++);
	}

	return len;
}

static void prov_complete(u16_t net_idx, u16_t addr)
{
	printk("Local node provisioned, net_idx 0x%04x address 0x%04x\n",
	       net_idx, addr);
	net.net_idx = net_idx,
	net.local = addr;
	net.dst = addr;
}

static void prov_reset(void)
{
	printk("The local node has been reset and needs reprovisioning\n");
}

static int output_number(bt_mesh_output_action_t action, uint32_t number)
{
	printk("OOB Number: %lu\n", number);
	return 0;
}

static int output_string(const char *str)
{
	printk("OOB String: %s\n", str);
	return 0;
}

static bt_mesh_input_action_t input_act;
static u8_t input_size;

static int cmd_input_num(int argc, char *argv[])
{
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	if (input_act != BT_MESH_ENTER_NUMBER) {
		printk("A number hasn't been requested!\n");
		return 0;
	}

	if (strlen(argv[1]) < input_size) {
		printk("Too short input (%u digits required)\n",
		       input_size);
		return 0;
	}

	err = bt_mesh_input_number(strtoul(argv[1], NULL, 10));
	if (err) {
		printk("Numeric input failed (err %d)\n", err);
		return 0;
	}

	input_act = BT_MESH_NO_INPUT;
	return 0;
}

struct shell_cmd_help cmd_input_num_help = {
	NULL, "<number>", NULL
};

static int cmd_input_str(int argc, char *argv[])
{
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	if (input_act != BT_MESH_ENTER_STRING) {
		printk("A string hasn't been requested!\n");
		return 0;
	}

	if (strlen(argv[1]) < input_size) {
		printk("Too short input (%u characters required)\n",
		       input_size);
		return 0;
	}

	err = bt_mesh_input_string(argv[1]);
	if (err) {
		printk("String input failed (err %d)\n", err);
		return 0;
	}

	input_act = BT_MESH_NO_INPUT;
	return 0;
}

struct shell_cmd_help cmd_input_str_help = {
	NULL, "<string>", NULL
};

static int input(bt_mesh_input_action_t act, u8_t size)
{
	switch (act) {
	case BT_MESH_ENTER_NUMBER:
		printk("Enter a number (max %u digits) with: input-num <num>\n",
		       size);
		break;
	case BT_MESH_ENTER_STRING:
		printk("Enter a string (max %u chars) with: input-str <str>\n",
		       size);
		break;
	default:
		printk("Unknown input action %u (size %u) requested!\n",
		       act, size);
		return -EINVAL;
	}

	input_act = act;
	input_size = size;
	return 0;
}

static const char *bearer2str(bt_mesh_prov_bearer_t bearer)
{
	switch (bearer) {
	case BT_MESH_PROV_ADV:
		return "PB-ADV";
	case BT_MESH_PROV_GATT:
		return "PB-GATT";
	default:
		return "unknown";
	}
}

static void link_open(bt_mesh_prov_bearer_t bearer)
{
	printk("Provisioning link opened on %s\n", bearer2str(bearer));
}

static void link_close(bt_mesh_prov_bearer_t bearer)
{
	printk("Provisioning link closed on %s\n", bearer2str(bearer));
}

#if 1
/* based on Core Specification 4.2 Vol 3. Part H 2.3.5.6.1 */
static const uint8_t oob_pri_key_test[32] = {
    0x52, 0x9A, 0xA0, 0x67, 0x0D, 0x72, 0xCD, 0x64, 0x97, 0x50, 0x2E, 0xD4,
    0x73, 0x50, 0x2B, 0x03, 0x7E, 0x88, 0x03, 0xB5, 0xC6, 0x08, 0x29, 0xA5,
    0xA3, 0xCA, 0xA2, 0x19, 0x50, 0x55, 0x30, 0xBA
};

static const uint8_t oob_pub_key_test[64] = {
    /* X */
	0xF4, 0x65, 0xE4, 0x3F, 0xF2, 0x3D, 0x3F, 0x1B, 0x9D, 0xC7, 0xDF, 0xC0,
	0x4D, 0xA8, 0x75, 0x81, 0x84, 0xDB, 0xC9, 0x66, 0x20, 0x47, 0x96, 0xEC,
	0xCF, 0x0D, 0x6C, 0xF5, 0xE1, 0x65, 0x00, 0xCC, 
    /* Y */
	0x02, 0x01, 0xD0, 0x48, 0xBC, 0xBB, 0xD8, 0x99, 0xEE, 0xEF, 0xC4, 0x24,
	0x16, 0x4E, 0x33, 0xC2, 0x01, 0xC2, 0xB0, 0x10, 0xCA, 0x6B, 0x4D, 0x43,
	0xA8, 0xA1, 0x55, 0xCA, 0xD8, 0xEC, 0xB2, 0x79
};
#else
/* based on Core Specification 4.2 Vol 3. Part H 2.3.5.6.1 */
static const uint8_t oob_pri_key_test[32] = {
    0x3f, 0x49, 0xf6, 0xd4, 0xa3, 0xc5, 0x5f, 0x38, 0x74, 0xc9, 0xb3, 0xe3,
    0xd2, 0x10, 0x3f, 0x50, 0x4a, 0xff, 0x60, 0x7b, 0xeb, 0x40, 0xb7, 0x99,
    0x58, 0x99, 0xb8, 0xa6, 0xcd, 0x3c, 0x1a, 0xbd
};

static const uint8_t oob_pub_key_test[64] = {
    /* X */
    0x20, 0xb0, 0x03, 0xd2, 0xf2, 0x97, 0xbe, 0x2c, 0x5e, 0x2c, 0x83, 0xa7,
    0xe9, 0xf9, 0xa5, 0xb9, 0xef, 0xf4, 0x91, 0x11, 0xac, 0xf4, 0xfd, 0xdb,
    0xcc, 0x03, 0x01, 0x48, 0x0e, 0x35, 0x9d, 0xe6,
    /* Y */
    0xdc, 0x80, 0x9c, 0x49, 0x65, 0x2a, 0xeb, 0x6d, 0x63, 0x32, 0x9a, 0xbf,
    0x5a, 0x52, 0x15, 0x5c, 0x76, 0x63, 0x45, 0xc2, 0x8f, 0xed, 0x30, 0x24,
    0x74, 0x1c, 0x8e, 0xd0, 0x15, 0x89, 0xd2, 0x8b,
};
#endif

static uint8_t oob_pri_key_swap[32];
static uint8_t oob_pub_key_swap[64];


static void oob_pub_key_cb(void)
{
	printk("Provisioning oob_pub_key input\n");
	memcpy(oob_pri_key_swap, oob_pri_key_test, 32);
	memcpy(oob_pub_key_swap, oob_pub_key_test, 64);

	//if key input in little-endian need swap it, PTS
	sys_memcpy_swap(oob_pri_key_swap, oob_pri_key_test, 32);
	sys_memcpy_swap(&oob_pub_key_swap[0], &oob_pub_key_test[0], 32);
	sys_memcpy_swap(&oob_pub_key_swap[32], &oob_pub_key_test[32], 32);
	
	bt_mesh_set_oob_pub_key(&oob_pub_key_swap[0], &oob_pub_key_swap[32], oob_pri_key_swap);
}


static u8_t dev_uuid[16] = {0x55, 0x66, 0};//MYNEWT_VAL(BLE_MESH_DEV_UUID);

static u8_t static_val[16];

static struct bt_mesh_prov prov = {
	.uuid = dev_uuid,
	.link_open = link_open,
	.link_close = link_close,
	.complete = prov_complete,
	.reset = prov_reset,
	.oob_pub_key = 0,
	.oob_pub_key_cb = oob_pub_key_cb,
	.static_val = NULL,
	.static_val_len = 0,
	.output_size = MYNEWT_VAL(BLE_MESH_OOB_OUTPUT_SIZE),
	.output_actions = MYNEWT_VAL(BLE_MESH_OOB_OUTPUT_ACTIONS),
	.output_number = output_number,
	.output_string = output_string,
	.input_size = MYNEWT_VAL(BLE_MESH_OOB_INPUT_SIZE),
	.input_actions = MYNEWT_VAL(BLE_MESH_OOB_INPUT_ACTIONS),
	.input = input,
};


#if MYNEWT_VAL(BLE_MESH_PROVISIONER)

static bt_mesh_output_action_t prov_input_act = 0;
static u8_t prov_input_size = 0;

static void provisioner_link_open(bt_mesh_prov_bearer_t bearer)
{
	printk("Provisioner link opened on %s\n", bearer2str(bearer));
}

static void provisioner_link_close(bt_mesh_prov_bearer_t bearer, u8_t reason)
{
	printk("Provisioner link closed on %s, reason:%d\n", bearer2str(bearer), reason);
}

static void provisioner_complete(int node_idx, const u8_t device_uuid[16],
                                 u16_t unicast_addr, u8_t element_num,
                                 u16_t netkey_idx, bool gatt_flag)
{
    u16_t addr;
	u8_t elem_num;

	bt_mesh_provisioner_get_own_unicast_addr(&addr,&elem_num);
	net.net_idx = netkey_idx,
	net.local = addr;
	net.dst = unicast_addr;
    
	printk("provisioner_complete\r\n");
	printk("node_idx: %d\r\n", node_idx);
	printk("device_uuid: %02x, %02x, %02x, %02x, %02x, %02x\r\n",
				device_uuid[0], device_uuid[1], device_uuid[2], 
				device_uuid[3], device_uuid[4], device_uuid[5]);
	printk("unicast_addr: %u\r\n", unicast_addr);
	printk("element_num: %u\r\n", element_num);
	printk("netkey_idx: %u\r\n", netkey_idx);
	printk("gatt_flag: %u\r\n", gatt_flag);
	printk("local addr:%x\r\n",net.local);
}

static int provisioner_input_num(bt_mesh_output_action_t act, u8_t size, u8_t link_idx)
{
	if(act == BT_MESH_DISPLAY_STRING){
		printk("Enter a string (max %u chars) with: pinput-str <str>\n", size);
	}else{
		printk("Enter a number (max %u digits) with: pinput-num <num>\n", size);
	}

	prov_input_act = act;
	prov_input_size = size;
	
	return 0;
}

static int provisioner_output_num(bt_mesh_input_action_t act, void *data, u8_t size, u8_t link_idx)
{
	if(act == BT_MESH_ENTER_STRING){
		printk("Display a string (%s)\n", data);
	}else{
		printk("Enter a number (%lu) size (%u)\n", *(u32_t *)data, size);
	}
	
	return 0;
}

static int provisioner_oob_pub_key_cb(void)
{
	printk("Provisioner oob_pub_key input\n");
	memcpy(oob_pri_key_swap, oob_pri_key_test, 32);
	memcpy(oob_pub_key_swap, oob_pub_key_test, 64);

	//if key input in little-endian need swap it, PTS
	sys_memcpy_swap(oob_pri_key_swap, oob_pri_key_test, 32);
	sys_memcpy_swap(&oob_pub_key_swap[0], &oob_pub_key_test[0], 32);
	sys_memcpy_swap(&oob_pub_key_swap[32], &oob_pub_key_test[32], 32);

	bt_mesh_prov_oob_pub_key(&oob_pub_key_swap[0], &oob_pub_key_swap[32]);
	return 0;
}


static struct bt_mesh_provisioner provisioner = {
	.prov_uuid              = dev_uuid,
	.prov_unicast_addr      = 0x20,
	.prov_start_address     = 2,
	.prov_attention         = 0,
	.prov_algorithm         = 0,
	.prov_pub_key_oob       = 0,
	.prov_pub_key_oob_cb    = provisioner_oob_pub_key_cb,
	.prov_static_oob_val    = NULL,
	.prov_static_oob_len    = 0,
	.prov_input_num         = provisioner_input_num,
	.prov_output_num        = provisioner_output_num,
	.flags                  = 0,
	.iv_index               = 0,
	.prov_link_open         = provisioner_link_open,
	.prov_link_close        = provisioner_link_close,
	.prov_complete          = provisioner_complete,
};

static int cmd_provisioner(int argc, char *argv[])
{
	int gatt = 0;
	
	if (argc < 3) {
		return -EINVAL;
	}

	if (!strcmp(argv[1], "gatt")) {
		gatt = 1;
	}else{
		gatt = 0;
	}

	if (str2bool(argv[2])) {
		printk("provisioner on (%s)\n", gatt? "gatt":"adv");
		if(gatt){
			bt_mesh_provisioner_enable(BT_MESH_PROV_GATT);
		}else{
			bt_mesh_provisioner_enable(BT_MESH_PROV_ADV);
		}
	}else{
		printk("provisioner off (%s)\n", gatt? "gatt":"adv");
		if(gatt){
			bt_mesh_provisioner_disable(BT_MESH_PROV_GATT);
		}else{
			bt_mesh_provisioner_disable(BT_MESH_PROV_ADV);
		}
	}
	
	return 0;
}

struct shell_cmd_help cmd_provisioner_help = {
	NULL, "[adv/gatt] [on/off]", NULL
};


static int cmd_provisioner_input_num(int argc, char *argv[])
{
	int i, err;
	u32_t num = 0;
	u8_t  prov_input[8];

	if (argc < 2) {
		return -EINVAL;
	}

	if (prov_input_act == BT_MESH_DISPLAY_STRING) {
		printk("A number hasn't been requested!\n");
		return 0;
	}

	if (strlen(argv[1]) < prov_input_size) {
		printk("Too short input (%u digits required)\n", prov_input_size);
		return 0;
	}
	num = strtoul(argv[1], NULL, 10);

	sys_put_be32(num, prov_input);

	err = bt_mesh_prov_input_data(prov_input, prov_input_size, 1);
	if (err) {
		printk("Numeric input failed (err %d)\n", err);
		return 0;
	}

	prov_input_act = 0;
	return 0;
}

struct shell_cmd_help cmd_provisioner_input_num_help = {
	NULL, "<number>", NULL
};

static int cmd_provisioner_input_str(int argc, char *argv[])
{
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	if (prov_input_act != BT_MESH_DISPLAY_STRING) {
		printk("A string hasn't been requested!\n");
		return 0;
	}

	if (strlen(argv[1]) < prov_input_size) {
		printk("Too short input (%u characters required)\n", prov_input_size);
		return 0;
	}

	err = bt_mesh_prov_input_data((u8_t *)argv[1], prov_input_size, 0);
	if (err) {
		printk("String input failed (err %d)\n", err);
		return 0;
	}

	prov_input_act = 0;
	return 0;
}

struct shell_cmd_help cmd_provisioner_input_str_help = {
	NULL, "<string>", NULL
};

static int cmd_provisioner_static_oob(int argc, char *argv[])
{
	if (argc < 2) {
		provisioner.prov_static_oob_val = NULL;
		provisioner.prov_static_oob_len = 0;
	} else {
		provisioner.prov_static_oob_len = hex2bin(argv[1], static_val, 16);
		if (provisioner.prov_static_oob_len) {
			provisioner.prov_static_oob_val = static_val;
		} else {
			provisioner.prov_static_oob_val = NULL;
		}
	}

	if (provisioner.prov_static_oob_val) {
		printk("Static OOB value set (length %u)\n",
		       provisioner.prov_static_oob_len);
	} else {
		printk("Static OOB value cleared\n");
	}

	return 0;
}

struct shell_cmd_help cmd_provisioner_static_oob_help = {
	NULL, "[val: 1-16 hex values]", NULL
};

static int cmd_provisioner_pub_key_oob(int argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	if (str2bool(argv[1])) {
		provisioner.prov_pub_key_oob = 1;
	} else{
		provisioner.prov_pub_key_oob = 0;
	}
	
	return 0;
}

struct shell_cmd_help cmd_provisioner_pub_key_oob_help = {
	NULL, "<val: off, on>", NULL
};

#endif

static int cmd_static_oob(int argc, char *argv[])
{
	if (argc < 2) {
		prov.static_val = NULL;
		prov.static_val_len = 0;
	} else {
		prov.static_val_len = hex2bin(argv[1], static_val, 16);
		if (prov.static_val_len) {
			prov.static_val = static_val;
		} else {
			prov.static_val = NULL;
		}
	}

	if (prov.static_val) {
		printk("Static OOB value set (length %u)\n",
		       prov.static_val_len);
	} else {
		printk("Static OOB value cleared\n");
	}

	return 0;
}

struct shell_cmd_help cmd_static_oob_help = {
	NULL, "[val: 1-16 hex values]", NULL
};


static const struct kv_pair cmd_output_action[] = {
    { "noout", 		BT_MESH_NO_OUTPUT },
	{ "blink", 		BT_MESH_BLINK },
	{ "beep", 		BT_MESH_BEEP },
	{ "vibrate", 	BT_MESH_VIBRATE },
	{ "disnum", 	BT_MESH_DISPLAY_NUMBER },
	{ "disstr", 	BT_MESH_DISPLAY_STRING },
    { NULL }
};

static const struct kv_pair cmd_input_action[] = {
    { "noin", 		BT_MESH_NO_INPUT },
	{ "push", 		BT_MESH_PUSH },
	{ "twist", 		BT_MESH_TWIST },
	{ "ennum", 		BT_MESH_ENTER_NUMBER },
	{ "enstr", 		BT_MESH_ENTER_STRING },
    { NULL }
};

static int cmd_auth_oob_set(int argc, char *argv[])
{
	int rc;
	
    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    prov.output_size = parse_arg_uint8_dflt("output_size",
                                         MYNEWT_VAL(BLE_MESH_OOB_OUTPUT_SIZE), &rc);	

	prov.output_actions = parse_arg_kv_dflt("output_act", cmd_output_action,
                                         MYNEWT_VAL(BLE_MESH_OOB_OUTPUT_ACTIONS), &rc);

    prov.input_size = parse_arg_uint8_dflt("input_size",
                                         MYNEWT_VAL(BLE_MESH_OOB_INPUT_SIZE), &rc);

	prov.input_actions = parse_arg_kv_dflt("input_act", cmd_input_action,
									 	MYNEWT_VAL(BLE_MESH_OOB_INPUT_ACTIONS), &rc);

	return 0;
}

static const struct shell_param auth_oob_params[] = {
    {"output_size", "mesh oob output size, default:4"},
    {"output_act", "usage: =[noout|blink|beep|vibrate|disnum|disstr], default: disnum"},
	{"input_size", "mesh oob input size, default:4"},
	{"input_act", "usage: =[noin|push|twist|ennum|enstr], default: noin"},
    {NULL, NULL}
};

static const struct shell_cmd_help cmd_auth_oob_help = {
    .summary = "set prov oob with specific parameters",
    .usage = NULL,
    .params = auth_oob_params,
};



static int cmd_pub_key_oob(int argc, char *argv[])
{
	if (argc < 2) {
		return -EINVAL;
	}

	if (str2bool(argv[1])) {
		prov.oob_pub_key = 1;
	} else{
		prov.oob_pub_key = 0;
	}
	
	return 0;
}

struct shell_cmd_help cmd_pub_key_oob_help = {
	NULL, "<val: off, on>", NULL
};


static int cmd_uuid(int argc, char *argv[])
{
	u8_t uuid[16];
	size_t len;

	if (argc < 2) {
		return -EINVAL;
	}

	len = hex2bin(argv[1], uuid, sizeof(uuid));
	if (len < 1) {
		return -EINVAL;
	}

	memcpy(dev_uuid, uuid, len);
	memset(dev_uuid + len, 0, sizeof(dev_uuid) - len);

	printk("Device UUID set\n");

	return 0;
}

struct shell_cmd_help cmd_uuid_help = {
	NULL, "<UUID: 1-16 hex values>", NULL
};

static int cmd_reset(int argc, char *argv[])
{
    uint16_t addr;
	
	if(argc == 2){
		addr = strtoul(argv[1], NULL, 0);
	}

	if ((argc == 1) || (addr == net.local)) {
		bt_mesh_reset();
		printk("Local node reset complete");
	} else {
		int err;
		bool reset = false;

		err = bt_mesh_cfg_node_reset(net.net_idx, net.dst, &reset);
		if (err) {
			printk("Unable to send "
					"Remote Node Reset (err %d)", err);
			return 0;
		}

		printk("Remote node reset complete");
	}

	return 0;
}
struct shell_cmd_help cmd_reset_help = {
	NULL, "<addr>", NULL
};

#if MYNEWT_VAL(BLE_MESH_LOW_POWER)
static int cmd_lpn(int argc, char *argv[])
{
	static bool enabled;
	int err;

	if (argc < 2) {
		printk("%s\n", enabled ? "enabled" : "disabled");
		return 0;
	}

	if (str2bool(argv[1])) {
		if (enabled) {
			printk("LPN already enabled\n");
			return 0;
		}

		err = bt_mesh_lpn_set(true);
		if (err) {
			printk("Enabling LPN failed (err %d)\n", err);
		} else {
			enabled = true;
		}
	} else {
		if (!enabled) {
			printk("LPN already disabled\n");
			return 0;
		}

		err = bt_mesh_lpn_set(false);
		if (err) {
			printk("Enabling LPN failed (err %d)\n", err);
		} else {
			enabled = false;
		}
	}

	return 0;
}

static int cmd_poll(int argc, char *argv[])
{
	int err;

	err = bt_mesh_lpn_poll();
	if (err) {
		printk("Friend Poll failed (err %d)\n", err);
	}

	return 0;
}

static void lpn_cb(u16_t friend_addr, bool established)
{
	if (established) {
		printk("Friendship (as LPN) established to Friend 0x%04x\n",
		       friend_addr);
	} else {
		printk("Friendship (as LPN) lost with Friend 0x%04x\n",
		       friend_addr);
	}
}

struct shell_cmd_help cmd_lpn_help = {
	NULL, "<value: off, on>", NULL
};

#endif /* MESH_LOW_POWER */

static int check_pub_addr_unassigned(void)
{
#ifdef ARCH_sim
	return 0;
#else
	uint8_t zero_addr[BLE_DEV_ADDR_LEN] = { 0 };

	return memcmp(MYNEWT_VAL(BLE_PUBLIC_DEV_ADDR),
		      zero_addr, BLE_DEV_ADDR_LEN) == 0;
#endif
}

int cmd_mesh_init(int argc, char *argv[])
{
	int err;
	ble_addr_t addr;
	struct bt_mesh_provisioner *pvnr = NULL;

#if (MYNEWT_VAL(BLE_MESH_PROXY))
	bt_mesh_proxy_adv_stop();
#endif

#if (MYNEWT_VAL(BLE_MESH_PROVISIONER))
	pvnr = &provisioner;
#endif

	if (0){ //(check_pub_addr_unassigned()) {
		/* Use NRPA */
		err = ble_hs_id_gen_rnd(1, &addr);
		if(err != 0){
			printk("cmd_mesh_init, ble_hs_id_gen_rnd, err(%d)\n", err);
			return -1;
		}
		err = ble_hs_id_set_rnd(addr.val);
		if(err != 0){
			printk("cmd_mesh_init, ble_hs_id_set_rnd, err(%d)\n", err);
			return -1;
		}

		err = bt_mesh_init(addr.type, &prov, &comp, pvnr);
	}
	else {
		err = bt_mesh_init(0, &prov, &comp, pvnr);
	}

	if (err) {
		printk("Mesh initialization failed (err %d)\n", err);
	}

	printk("Mesh initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	if (bt_mesh_is_provisioned()) {
		printk("Mesh network restored from flash\n");
	} else {
		printk("Use \"pb-adv on\" or \"pb-gatt on\" to enable"
		       " advertising\n");
	}

#if MYNEWT_VAL(BLE_MESH_LOW_POWER)
	bt_mesh_lpn_set_cb(lpn_cb);
#endif

	return 0;
}

#if MYNEWT_VAL(BLE_MESH_GATT_PROXY)
static int cmd_ident(int argc, char *argv[])
{
	int err;

	err = bt_mesh_proxy_identity_enable();
	if (err) {
		printk("Failed advertise using Node Identity (err %d)\n", err);
	}

	return 0;
}
#endif /* MESH_GATT_PROXY */
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)

struct shell_cmd_help cmd_proxy_client_connect_help = {
	NULL, "<addr=XX:XX:XX:XX:XX:XX type=uint8" , NULL
};

static int cmd_proxy_client_connect(int argc, char *argv[]){

    int rc;
    uint8_t addr[6];
	uint8_t type;
	uint16_t net_idx;
    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

	rc = parse_arg_mac("addr", addr);
    if (rc != 0) {
        iot_printf("err 'addr'\n");
        return rc;
    }

    type =  parse_arg_uint8("type", &rc);
	if (rc != 0) {
        printf("invalid 'type' parameter\n");
        return rc;
    }
	
	if((rc = bt_mesh_proxy_client_connect(addr,type,net.net_idx)) !=0)
    {
        printf("bt_mesh_proxy_client_connect err:%d\n",rc);
    }
	

}



void cmd_proxy_client_cfg_send(int argc, char *argv[]){

    int rc;
    uint16 addr;
	uint8_t type;
	uint8_t opcode;
	uint16_t net_idx;
    uint16_t handle;
	uint8_t act;
	struct bt_mesh_proxy_cfg_pdu pdu;
    rc = parse_arg_all(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }
	
	handle = parse_arg_uint8("handle", &rc);
    if (rc != 0) {
        iot_printf("err 'handle'\n");
        return rc;
    }

    opcode = parse_arg_uint8("opcode", &rc);
    if (rc != 0) {
        iot_printf("err 'opcode'\n");
        return rc;
    }
    

    type =  parse_arg_uint8("type", &rc);
	if (rc != 0) {
        printf(" 'type' parameter null\n");
        //return rc;
    }

	act =  parse_arg_uint8("act", &rc);
	if (rc != 0) {
        printf(" 'type' act null\n");
        //return rc;
    }

	if(act == 0){
        pdu.opcode = opcode;
		//pdu.add.addr_num = 1 ;
		//pdu.add.addr[0] = net.dst;
		pdu.set.filter_type = type;
	}else if(act == 1){
        pdu.opcode = opcode;
		pdu.add.addr_num = 1 ;
		pdu.add.addr[0] = net.dst;
		pdu.set.filter_type = type;
	}else if(act == 2){
        pdu.opcode = opcode;
		pdu.remove.addr_num = 1 ;
		pdu.remove.addr[0] = net.dst;
		pdu.set.filter_type = type;
	}
   

    bt_mesh_proxy_client_cfg_send(handle,net.net_idx,&pdu);

}



#endif
static int cmd_dst(int argc, char *argv[])
{
	if (argc < 2) {
		printk("Destination address: 0x%04x%s\n", net.dst,
		       net.dst == net.local ? " (local)" : "");
		return 0;
	}

	if (!strcmp(argv[1], "local")) {
		net.dst = net.local;
	} else {
		net.dst = strtoul(argv[1], NULL, 0);
	}

	printk("Destination address set to 0x%04x%s\n", net.dst,
	       net.dst == net.local ? " (local)" : "");
	return 0;
}

struct shell_cmd_help cmd_dst_help = {
	NULL, "[destination address]", NULL
};

static int cmd_netidx(int argc, char *argv[])
{
	if (argc < 2) {
		printk("NetIdx: 0x%04x\n", net.net_idx);
		return 0;
	}

	net.net_idx = strtoul(argv[1], NULL, 0);
	printk("NetIdx set to 0x%04x\n", net.net_idx);
	return 0;
}

struct shell_cmd_help cmd_netidx_help = {
	NULL, "[NetIdx]", NULL
};

static int cmd_appidx(int argc, char *argv[])
{
	if (argc < 2) {
		printk("AppIdx: 0x%04x\n", net.app_idx);
		return 0;
	}

	net.app_idx = strtoul(argv[1], NULL, 0);
	printk("AppIdx set to 0x%04x\n", net.app_idx);
	return 0;
}

struct shell_cmd_help cmd_appidx_help = {
	NULL, "[AppIdx]", NULL
};

static int cmd_net_send(int argc, char *argv[])
{	
	struct os_mbuf *msg = NET_BUF_SIMPLE(32);
	struct bt_mesh_msg_ctx ctx = {
		.send_ttl = BT_MESH_TTL_DEFAULT,
		.net_idx = net.net_idx,
		.addr = net.dst,
		.app_idx = net.app_idx,

	};
	struct bt_mesh_net_tx tx = {
		.ctx = &ctx,
		.src = net.local,
		.xmit = bt_mesh_net_transmit_get(),
		.sub = bt_mesh_subnet_get(net.net_idx),
	};
	size_t len;
	int err = 0;
	u8_t send_data[16];
	u8_t appkey = 1;

	if (argc < 2) {
		err = -EINVAL;
		goto done;
	}

	if (!tx.sub) {
		printk("No matching subnet for NetKey Index 0x%04x\n",
		       net.net_idx);
		goto done;
	}

    err = parse_arg_all(argc - 1, argv + 1);
    if (err != 0) {
        goto done;
    }

	ctx.send_ttl = parse_arg_uint8("ttl", &err);
	if(err != 0){
		ctx.send_ttl = BT_MESH_TTL_DEFAULT;
	}
	
	tx.src = parse_arg_uint16("src", &err);
	if(err != 0){
		tx.src = net.local;
	}	

	ctx.addr = parse_arg_uint16("dst", &err);
	if(err != 0){
		ctx.addr = net.dst;
	}
	
	appkey = parse_arg_uint8("appkey", &err);
	if(err != 0){
		appkey = 1;
	}
	
    err = parse_arg_byte_stream("data", 16, send_data, &len);
	if(err != 0){
		iot_printf("invalid 'data' parameter\n");
        goto done;
	}

	if(appkey == 0){
		ctx.app_idx = BT_MESH_KEY_DEV;
	}
	
	net_buf_simple_init(msg, 0);
	memcpy(msg->om_data, send_data, len);
	net_buf_simple_add(msg, len);

	err = bt_mesh_trans_send(&tx, msg, NULL, NULL);
	if (err) {
		printk("Failed to send (err %d)\n", err);
	}

done:
	os_mbuf_free_chain(msg);
	return err;
}

static const struct shell_param cmd_net_send_params[] = {
    {"ttl", "usage: =[0-UINT8_MAX], default: 255"},
	{"src", "usage: =[UINT16]"},	
	{"dst", "usage: =[UINT16]"},
	{"appkey", "usage: =[0-1], =0 appkey use BT_MESH_KEY_DEV"},
	{"data", "usage: =[XX:XX...]"},
	{NULL, NULL}	
};

struct shell_cmd_help cmd_net_send_help = {
    .summary = "net send data",
    .usage = NULL,
    .params = cmd_net_send_params,
};

static int cmd_iv_update(int argc, char *argv[])
{
	if (bt_mesh_iv_update()) {
		printk("Transitioned to IV Update In Progress state\n");
	} else {
		printk("Transitioned to IV Update Normal state\n");
	}

	printk("IV Index is 0x%08lx\n", bt_mesh.iv_index);

	return 0;
}

static int cmd_rpl_clear(int argc, char *argv[])
{
	bt_mesh_rpl_clear();
	return 0;
}

#if MYNEWT_VAL(BLE_MESH_LOW_POWER)
static int cmd_lpn_subscribe(int argc, char *argv[])
{
	u16_t address;

	if (argc < 2) {
		return -EINVAL;
	}

	address = strtoul(argv[1], NULL, 0);

	printk("address 0x%04x", address);

	bt_mesh_lpn_group_add(address);

	return 0;
}

struct shell_cmd_help cmd_lpn_subscribe_help = {
	NULL, "<addr>", NULL
};

static int cmd_lpn_unsubscribe(int argc, char *argv[])
{
	u16_t address;

	if (argc < 2) {
		return -EINVAL;
	}

	address = strtoul(argv[1], NULL, 0);

	printk("address 0x%04x", address);

	bt_mesh_lpn_group_del(&address, 1);

	return 0;
}

struct shell_cmd_help cmd_lpn_unsubscribe_help = {
	NULL, "<addr>", NULL
};
#endif

static int cmd_iv_update_test(int argc, char *argv[])
{
	bool enable;

	if (argc < 2) {
		return -EINVAL;
	}

	enable = str2bool(argv[1]);
	if (enable) {
		printk("Enabling IV Update test mode\n");
	} else {
		printk("Disabling IV Update test mode\n");
	}

	bt_mesh_iv_update_test(enable);

	return 0;
}

struct shell_cmd_help cmd_iv_update_test_help = {
	NULL, "<value: off, on>", NULL
};

#if MYNEWT_VAL(BLE_MESH_CFG_CLI)

int cmd_timeout(int argc, char *argv[])
{
	s32_t timeout;

	if (argc < 2) {
		timeout = bt_mesh_cfg_cli_timeout_get();
		if (timeout == K_FOREVER) {
			printk("Message timeout: forever\n");
		} else {
			printk("Message timeout: %lu seconds\n",
			       timeout / 1000);
		}

		return 0;
	}

	timeout = strtol(argv[1], NULL, 0);
	if (timeout < 0 || timeout > (INT32_MAX / 1000)) {
		timeout = K_FOREVER;
	} else {
		timeout = timeout * 1000;
	}

	bt_mesh_cfg_cli_timeout_set(timeout);
	if (timeout == K_FOREVER) {
		printk("Message timeout: forever\n");
	} else {
		printk("Message timeout: %lu seconds\n",
		       timeout / 1000);
	}

	return 0;
}

struct shell_cmd_help cmd_timeout_help = {
	NULL, "[timeout in seconds]", NULL
};


static int cmd_get_comp(int argc, char *argv[])
{
	struct os_mbuf *comp = NET_BUF_SIMPLE(32);
	u8_t status, page = 0x00;
	int err = 0;

	if (argc > 1) {
		page = strtol(argv[1], NULL, 0);
	}

	net_buf_simple_init(comp, 0);
	err = bt_mesh_cfg_comp_data_get(net.net_idx, net.dst, page,
					&status, comp);
	if (err) {
		printk("Getting composition failed (err %d)\n", err);
		goto done;
	}

	if (status != 0x00) {
		printk("Got non-success status 0x%02x\n", status);
		goto done;
	}

	printk("Got Composition Data for 0x%04x:\n", net.dst);
	printk("\tCID      0x%04x\n", net_buf_simple_pull_le16(comp));
	printk("\tPID      0x%04x\n", net_buf_simple_pull_le16(comp));
	printk("\tVID      0x%04x\n", net_buf_simple_pull_le16(comp));
	printk("\tCRPL     0x%04x\n", net_buf_simple_pull_le16(comp));
	printk("\tFeatures 0x%04x\n", net_buf_simple_pull_le16(comp));

	while (comp->om_len > 4) {
		u8_t sig, vnd;
		u16_t loc;
		int i;

		loc = net_buf_simple_pull_le16(comp);
		sig = net_buf_simple_pull_u8(comp);
		vnd = net_buf_simple_pull_u8(comp);

		printk("\n\tElement @ 0x%04x:\n", loc);

		if (comp->om_len < ((sig * 2) + (vnd * 4))) {
			printk("\t\t...truncated data!\n");
			break;
		}

		if (sig) {
			printk("\t\tSIG Models:\n");
		} else {
			printk("\t\tNo SIG Models\n");
		}

		for (i = 0; i < sig; i++) {
			u16_t mod_id = net_buf_simple_pull_le16(comp);

			printk("\t\t\t0x%04x\n", mod_id);
		}

		if (vnd) {
			printk("\t\tVendor Models:\n");
		} else {
			printk("\t\tNo Vendor Models\n");
		}

		for (i = 0; i < vnd; i++) {
			u16_t cid = net_buf_simple_pull_le16(comp);
			u16_t mod_id = net_buf_simple_pull_le16(comp);

			printk("\t\t\tCompany 0x%04x: 0x%04x\n", cid, mod_id);
		}
	}

done:
	os_mbuf_free_chain(comp);
	return err;
}

struct shell_cmd_help cmd_get_comp_help = {
	NULL, "[page]", NULL
};

static int cmd_beacon(int argc, char *argv[])
{
	u8_t status;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_beacon_get(net.net_idx, net.dst, &status);
	} else {
		u8_t val = str2u8(argv[1]);

		err = bt_mesh_cfg_beacon_set(net.net_idx, net.dst, val,
					     &status);
	}

	if (err) {
		printk("Unable to send Beacon Get/Set message (err %d)\n", err);
		return 0;
	}

	printk("Beacon state is 0x%02x\n", status);

	return 0;
}

struct shell_cmd_help cmd_beacon_help = {
	NULL, "[val: off, on]", NULL
};

static int cmd_ttl(int argc, char *argv[])
{
	u8_t ttl;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_ttl_get(net.net_idx, net.dst, &ttl);
	} else {
		u8_t val = strtoul(argv[1], NULL, 0);

		err = bt_mesh_cfg_ttl_set(net.net_idx, net.dst, val, &ttl);
	}

	if (err) {
		printk("Unable to send Default TTL Get/Set (err %d)\n", err);
		return 0;
	}

	printk("Default TTL is 0x%02x\n", ttl);

	return 0;
}

struct shell_cmd_help cmd_ttl_help = {
	NULL, "[ttl: 0x00, 0x02-0x7f]", NULL
};

static int cmd_friend(int argc, char *argv[])
{
	u8_t frnd;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_friend_get(net.net_idx, net.dst, &frnd);
	} else {
		u8_t val = str2u8(argv[1]);

		err = bt_mesh_cfg_friend_set(net.net_idx, net.dst, val, &frnd);
	}

	if (err) {
		printk("Unable to send Friend Get/Set (err %d)\n", err);
		return 0;
	}

	printk("Friend is set to 0x%02x\n", frnd);

	return 0;
}

struct shell_cmd_help cmd_friend_help = {
	NULL, "[val: off, on]", NULL
};

static int cmd_gatt_proxy(int argc, char *argv[])
{
	u8_t proxy;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_gatt_proxy_get(net.net_idx, net.dst, &proxy);
	} else {
		u8_t val = str2u8(argv[1]);

		err = bt_mesh_cfg_gatt_proxy_set(net.net_idx, net.dst, val,
						 &proxy);
	}

	if (err) {
		printk("Unable to send GATT Proxy Get/Set (err %d)\n", err);
		return 0;
	}

	printk("GATT Proxy is set to 0x%02x\n", proxy);

	return 0;
}

struct shell_cmd_help cmd_gatt_proxy_help = {
	NULL, "[val: off, on]", NULL
};

static int cmd_relay(int argc, char *argv[])
{
	u8_t relay, transmit;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_relay_get(net.net_idx, net.dst, &relay,
					    &transmit);
	} else {
		u8_t val = str2u8(argv[1]);
		u8_t count, interval, new_transmit;

		if (val) {
			if (argc > 2) {
				count = strtoul(argv[2], NULL, 0);
			} else {
				count = 2;
			}

			if (argc > 3) {
				interval = strtoul(argv[3], NULL, 0);
			} else {
				interval = 20;
			}

			new_transmit = BT_MESH_TRANSMIT(count, interval);
		} else {
			new_transmit = 0;
		}

		err = bt_mesh_cfg_relay_set(net.net_idx, net.dst, val,
					    new_transmit, &relay, &transmit);
	}

	if (err) {
		printk("Unable to send Relay Get/Set (err %d)\n", err);
		return 0;
	}

	printk("Relay is 0x%02x, Transmit 0x%02x (count %u interval %ums)\n",
	       relay, transmit, BT_MESH_TRANSMIT_COUNT(transmit),
	       BT_MESH_TRANSMIT_INT(transmit));

	return 0;
}

struct shell_cmd_help cmd_relay_help = {
	NULL, "[val: off, on] [count: 0-7] [interval: 0-32]", NULL
};

static int cmd_net_key_add(int argc, char *argv[])
{
	u8_t key_val[16];
	u16_t key_net_idx;
	u8_t status;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);

	if (argc > 2) {
		size_t len;

		len = hex2bin(argv[3], key_val, sizeof(key_val));
		memset(key_val, 0, sizeof(key_val) - len);
	} else {
		memcpy(key_val, default_key, sizeof(key_val));
	}

	err = bt_mesh_cfg_net_key_add(net.net_idx, net.dst, key_net_idx,
				      key_val, &status);
	if (err) {
		printk("Unable to send NetKey Add (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("NetKey Add failed with status 0x%02x\n", status);
	} else {
		printk("NetKey Add with NetKey Index 0x%03x\n", key_net_idx);
	}

	return 0;
}

struct shell_cmd_help cmd_net_key_add_help = {
	NULL, "<NetKeyIndex> [val]", NULL
};

static int cmd_net_key_update(int argc, char *argv[])
{
	u8_t key_val[16];
	u16_t key_net_idx;
	u8_t status;
	int err;
	
	if (argc < 2) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);

	if (argc > 2) {
		size_t len;

		len = hex2bin(argv[3], key_val, sizeof(key_val));
		memset(key_val, 0, sizeof(key_val) - len);
	} else {
		memcpy(key_val, default_key, sizeof(key_val));
	}

	err = bt_mesh_cfg_net_key_update(net.net_idx, net.dst, key_net_idx,
				      key_val, &status);
	if (err) {
		printk("Unable to send NetKey update (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("NetKey update failed with status 0x%02x\n", status);
	} else {
		printk("NetKey update with NetKey Index 0x%03x\n", key_net_idx);
	}

	return 0;
}

struct shell_cmd_help cmd_net_key_update_help = {
	NULL, "<NetKeyIndex> [val]", NULL
};

static int cmd_net_key_get(int argc, char *argv[])
{
	uint16_t keys[16];
	size_t cnt;
	int err, i;

	cnt = ARRAY_SIZE(keys);

	err = bt_mesh_cfg_net_key_get(net.net_idx, net.dst, keys, &cnt);
	if (err) {
		printk("Unable to send NetKeyGet (err %d)", err);
		return 0;
	}

	printk("NetKeys known by 0x%04x:", net.dst);
	for (i = 0; i < cnt; i++) {
		printk("\t0x%03x", keys[i]);
	}

	return 0;
}


static int cmd_net_key_del(int argc, char *argv[])
{
	u16_t key_net_idx;
	u8_t status;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);

	err = bt_mesh_cfg_net_key_del(net.net_idx, net.dst, key_net_idx, &status);
	if (err) {
		printk("Unable to send NET Key Del (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("NetKey Delete failed with status 0x%02x\n", status);
	} else {
		printk("NetKey Delete, NetKeyIndex 0x%04x \n", key_net_idx);
	}

	return 0;	
}

struct shell_cmd_help cmd_net_key_del_help = {
	NULL, "<NetKeyIndex>", NULL
};

static int cmd_app_key_add(int argc, char *argv[])
{
	u8_t key_val[16];
	u16_t key_net_idx, key_app_idx;
	u8_t status;
	int err;

	if (argc < 3) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);
	key_app_idx = strtoul(argv[2], NULL, 0);

	if (argc > 3) {
		size_t len;

		len = hex2bin(argv[3], key_val, sizeof(key_val));
		memset(key_val, 0, sizeof(key_val) - len);
	} else {
		memcpy(key_val, default_key, sizeof(key_val));
	}

	err = bt_mesh_cfg_app_key_add(net.net_idx, net.dst, key_net_idx,
				      key_app_idx, key_val, &status);
	if (err) {
		printk("Unable to send App Key Add (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("AppKey Add failed with status 0x%02x\n", status);
	} else {
		printk("AppKey Add, NetKeyIndex 0x%04x AppKeyIndex 0x%04x\n",
		       key_net_idx, key_app_idx);
	}

	return 0;
}

struct shell_cmd_help cmd_app_key_add_help = {
	NULL, "<NetKeyIndex> <AppKeyIndex> [val]", NULL
};

static int cmd_local_app_key_update(int argc, char *argv[])
{
	u8_t key_val[16];
	u16_t key_net_idx, key_app_idx;
	u8_t status;
	int err;

	if (argc < 3) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);
	key_app_idx = strtoul(argv[2], NULL, 0);

	if (argc > 3) {
		size_t len;

		len = hex2bin(argv[3], key_val, sizeof(key_val));
		memset(key_val, 0, sizeof(key_val) - len);
	} else {
		memcpy(key_val, default_key, sizeof(key_val));
	}

	err = bt_mesh_provisioner_local_app_key_update(key_val,  key_net_idx, key_app_idx);
	if (err) {
		printk("Unable to send cmd_local_app_key_update (err %d)\n", err);
		return 0;
	}

	return 0;
}

struct shell_cmd_help cmd_local_app_key_update_help = {
	NULL, "<NetKeyIndex> <AppKeyIndex> [val]", NULL
};


static int cmd_local_app_key_add(int argc, char *argv[])
{
	u8_t key_val[16];
	u16_t key_net_idx, key_app_idx;
	u8_t status;
	int err;

	if (argc < 3) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);
	key_app_idx = strtoul(argv[2], NULL, 0);

	if (argc > 3) {
		size_t len;

		len = hex2bin(argv[3], key_val, sizeof(key_val));
		memset(key_val, 0, sizeof(key_val) - len);
	} else {
		memcpy(key_val, default_key, sizeof(key_val));
	}

	err = bt_mesh_provisioner_local_app_key_add(key_val,  key_net_idx,&key_app_idx);
	if (err) {
		printk("Unable to send cmd_local_app_key_add (err %d)\n", err);
		return 0;
	}

	return 0;
}

struct shell_cmd_help cmd_local_app_key_add_help = {
	NULL, "<NetKeyIndex> <AppKeyIndex> [val]", NULL
};


static int cmd_local_net_key_update(int argc, char *argv[])
{
	u8_t key_val[16];
	u16_t key_net_idx, key_app_idx;
	u8_t status;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);

	if (argc > 2) {
		size_t len;

		len = hex2bin(argv[2], key_val, sizeof(key_val));
		memset(key_val, 0, sizeof(key_val) - len);
	} else {
		memcpy(key_val, default_key, sizeof(key_val));
	}

	err = bt_mesh_provisioner_local_net_key_update(key_val,key_net_idx);
	if (err) {
		printk("Unable to send cmd_local_net_key_update (err %d)\n", err);
		return 0;
	}

	return 0;
}

struct shell_cmd_help cmd_local_net_key_update_help = {
	NULL, "<NetKeyIndex> [val]", NULL
};

static int cmd_local_net_key_add(int argc, char *argv[])
{
	u8_t key_val[16];
	u16_t key_net_idx, key_app_idx;
	u8_t status;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);

	if (argc > 2) {
		size_t len;

		len = hex2bin(argv[2], key_val, sizeof(key_val));
		memset(key_val, 0, sizeof(key_val) - len);
	} else {
		memcpy(key_val, default_key, sizeof(key_val));
	}

	err = bt_mesh_provisioner_local_net_key_add(key_val,&key_net_idx);
	if (err) {
		printk("Unable to send cmd_local_net_key_add (err %d)\n", err);
		return 0;
	}

	return 0;
}

struct shell_cmd_help cmd_local_net_key_add_help = {
	NULL, "<NetKeyIndex> [val]", NULL
};

static int cmd_provisioner_beacon(int argc, char *argv[])
{ 
    int err;
	bool kr_flag;
	u8_t key_val[16];
	struct bt_mesh_subnet *sub;

    if (argc != 3) {
		printf("argc should be 3\n");
		return -EINVAL;
	}

	kr_flag = str2bool(argv[1]);

    if (argc > 2) {
		size_t len;

		len = hex2bin(argv[2], key_val, sizeof(key_val));
		memset(key_val, 0, sizeof(key_val) - len);
	} else {
		memcpy(key_val, default_key, sizeof(key_val));
	}

	sub = bt_mesh_provisioner_subnet_get(0);
    if (sub == NULL) {
        BT_ERR("Invalid NetKeyIndex ");
        return -ENODEV;
    }

    sub->kr_flag = kr_flag;

    if (sub->kr_flag) {
        if (bt_mesh_net_keys_create(&sub->keys[1], key_val)) {
            BT_ERR("Create net_keys fail");
            sub->subnet_active = false;
            return -EIO;
        }
        sub->kr_phase = BT_MESH_KR_PHASE_2;
    } else{
        if (bt_mesh_net_keys_create(&sub->keys[0], key_val)) {
            BT_ERR("Create net_keys fail");
            sub->subnet_active = false;
            return -EIO;
        }
        sub->kr_phase = BT_MESH_KR_NORMAL;
    }

	err = bt_mesh_net_beacon_update(sub);
    if (err) {
        BT_ERR("Failed to update secure beacon");
        return -EIO;
    }
    return 0;
}

struct shell_cmd_help cmd_provisioner_beacon_help = {
	NULL, "[on/off/enable/disable] [netkeyval]" , NULL
};

static int cmd_app_key_update(int argc, char *argv[])
{
	u8_t key_val[16];
	u16_t key_net_idx, key_app_idx;
	u8_t status;
	int err;

	if (argc < 3) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);
	key_app_idx = strtoul(argv[2], NULL, 0);

	if (argc > 3) {
		size_t len;

		len = hex2bin(argv[3], key_val, sizeof(key_val));
		memset(key_val, 0, sizeof(key_val) - len);
	} else {
		memcpy(key_val, default_key, sizeof(key_val));
	}

	err = bt_mesh_cfg_app_key_update(net.net_idx, net.dst, key_net_idx,
				      key_app_idx, key_val, &status);
	if (err) {
		printk("Unable to send App Key Add (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("AppKey update failed with status 0x%02x\n", status);
	} else {
		printk("AppKey update, NetKeyIndex 0x%04x AppKeyIndex 0x%04x\n",
		       key_net_idx, key_app_idx);
	}

	return 0;
}

struct shell_cmd_help cmd_app_key_update_help = {
	NULL, "<NetKeyIndex> <AppKeyIndex> [val]", NULL
};

static int cmd_app_key_get(int argc, char *argv[])
{
	u8_t *data = NULL;
	u16_t len;
	u16_t idx1, idx2;
	u16_t key_net_idx;
	u8_t status;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);	

	err = bt_mesh_cfg_app_key_get(net.net_idx, net.dst, key_net_idx, &data, &len, &status);
	if (err) {
		printk("Unable to send AppKey get (err %d)\n", err);
		return 0;
	}
	
	if (status) {
		printk("AppKey get failed with status 0x%02x\n", status);
	} else {
		printk("AppKey get with NetKey Index 0x%03x\n", key_net_idx);
	}

	if(data && len){
		printk("NetKey idx list:");
		while(len > 0){
			if(len >= 3){
				key_idx_unpack(data, &idx1, &idx2);
				data += 3;
				len -= 3;
				printk("0x%03X, 0x%03X, ", idx1, idx2);
			}else if(len >= 2){
				idx1 = sys_get_le16(data) & 0xfff;
				data += 2;
				len -= 2;
				printk("0x%03X ", idx1);
			}else{
				break;
			}
		}
		printk("\n");		
	}else{
		printk("NetKey get none\n");
	}

	if(data){
		free(data);
	}

	return 0;
}

struct shell_cmd_help cmd_app_key_get_help = {
	NULL, "<NetKeyIndex>", NULL
};

static int cmd_app_key_del(int argc, char *argv[])
{
	u16_t key_net_idx, key_app_idx;
	u8_t status;
	int err;
	
	if (argc < 3) {
		return -EINVAL;
	}

	key_net_idx = strtoul(argv[1], NULL, 0);
	key_app_idx = strtoul(argv[2], NULL, 0);

	err = bt_mesh_cfg_app_key_del(net.net_idx, net.dst, key_net_idx,
									key_app_idx, &status);

	if (err) {
		printk("Unable to send NET Key Del (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("AppKey Delete failed with status 0x%02x\n", status);
	} else {
		printk("AppKey Delete, NetKeyIndex 0x%04x, AppKeyIndex 0x%04x\n", 
				key_net_idx, key_app_idx);
	}

	return 0;		
}

struct shell_cmd_help cmd_app_key_del_help = {
	NULL, "<NetKeyIndex> <AppKeyIndex>", NULL
};

static int cmd_key_refresh_get(int argc, char *argv[])
{
	u8_t status, phase;
	int err;

	err = bt_mesh_cfg_kr_phase_get(net.net_idx, net.dst, &status, &phase);
	if (err) {
		printk("Unable to send Key Refresh Get (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Key Refresh Get failed with status 0x%02x\n", status);
	} else {
		printk("Key Refresh Get, phase 0x%02x\n", phase);
	}

	return 0;
}

static int cmd_key_refresh_set(int argc, char *argv[])
{
	u8_t status, phase;
	u8_t transition;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	transition = strtoul(argv[1], NULL, 0);

	err = bt_mesh_cfg_kr_phase_set(net.net_idx, net.dst, transition, &status, &phase);
	if (err) {
		printk("Unable to send Key Refresh Set (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Key Refresh Set failed with status 0x%02x\n", status);
	} else {
		printk("Key Refresh Set, phase 0x%02x\n", phase);
	}

	return 0;
}

struct shell_cmd_help cmd_key_refresh_set_help = {
	NULL, "<transition>", NULL
};


static int cmd_mod_app_bind(int argc, char *argv[])
{
	u16_t elem_addr, mod_app_idx, mod_id, cid;
	u8_t status;
	int err;

	if (argc < 4) {
		return -EINVAL;
	}

	elem_addr = strtoul(argv[1], NULL, 0);
	mod_app_idx = strtoul(argv[2], NULL, 0);
	mod_id = strtoul(argv[3], NULL, 0);

	if (argc > 4) {
		cid = strtoul(argv[4], NULL, 0);
		err = bt_mesh_cfg_mod_app_bind_vnd(net.net_idx, net.dst,
						   elem_addr, mod_app_idx,
						   mod_id, cid, &status);
	} else {
		err = bt_mesh_cfg_mod_app_bind(net.net_idx, net.dst, elem_addr,
					       mod_app_idx, mod_id, &status);
	}

	if (err) {
		printk("Unable to send Model App Bind (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Model App Bind failed with status 0x%02x\n", status);
	} else {
		printk("AppKey successfully bound\n");
	}

	return 0;
}

struct shell_cmd_help cmd_mod_app_bind_help = {
	NULL, "<addr> <AppIndex> <Model ID> [Company ID]", NULL
};

static int cmd_mod_app_unbind(int argc, char *argv[])
{
	uint16_t elem_addr, mod_app_idx, mod_id, cid;
	uint8_t status;
	int err;

	if (argc < 4) {
		return -EINVAL;
	}

	elem_addr = strtoul(argv[1], NULL, 0);
	mod_app_idx = strtoul(argv[2], NULL, 0);
	mod_id = strtoul(argv[3], NULL, 0);

	if (argc > 4) {
		cid = strtoul(argv[4], NULL, 0);
		err = bt_mesh_cfg_mod_app_unbind_vnd(net.net_idx, net.dst,
						   elem_addr, mod_app_idx,
						   mod_id, cid, &status);
	} else {
		err = bt_mesh_cfg_mod_app_unbind(net.net_idx, net.dst,
				elem_addr, mod_app_idx, mod_id, &status);
	}

	if (err) {
		printk("Unable to send Model App Unbind (err %d)",
			    err);
		return 0;
	}

	if (status) {
		printk("Model App Unbind failed with status 0x%02x",
			    status);
	} else {
		printk("AppKey successfully unbound");
	}

	return 0;
}

struct shell_cmd_help cmd_mod_app_unbind_help = {
	NULL, "<addr> <AppIndex> <Model ID> [Company ID]", NULL
};

static int cmd_mod_app_get(int argc,char *argv[])
{
	uint16_t elem_addr, mod_id, cid;
	uint16_t apps[16];
	uint8_t status;
	size_t cnt;
	int err, i;

	elem_addr = strtoul(argv[1], NULL, 0);
	mod_id = strtoul(argv[2], NULL, 0);
	cnt = ARRAY_SIZE(apps);

	if (argc > 3) {
		cid = strtoul(argv[3], NULL, 0);
		err = bt_mesh_cfg_mod_app_get_vnd(net.net_idx, net.dst,
						  elem_addr, mod_id, cid,
						  &status, apps, &cnt);
	} else {
		err = bt_mesh_cfg_mod_app_get(net.net_idx, net.dst, elem_addr,
					      mod_id, &status, apps, &cnt);
	}

	if (err) {
		printk("Unable to send Model App Get (err %d)",
			    err);
		return 0;
	}

	if (status) {
		printk("Model App Get failed with status 0x%02x",
			    status);
	} else {
		printk(
			"Apps bound to Element 0x%04x, Model 0x%04x %s:",
			elem_addr, mod_id, argc > 3 ? argv[3] : "(SIG)");

		if (!cnt) {
			printk("\tNone.");
		}

		for (i = 0; i < cnt; i++) {
			printk("\t0x%04x", apps[i]);
		}
	}

	return 0;
}

struct shell_cmd_help cmd_mod_app_get_help = {
	NULL, "<elem addr> <Model ID> [Company ID]", NULL
};


static int cmd_mod_sub_add(int argc, char *argv[])
{
	u16_t elem_addr, sub_addr, mod_id, cid;
	u8_t status;
	int err;

	if (argc < 4) {
		return -EINVAL;
	}

	elem_addr = strtoul(argv[1], NULL, 0);
	sub_addr = strtoul(argv[2], NULL, 0);
	mod_id = strtoul(argv[3], NULL, 0);

	if (argc > 4) {
		cid = strtoul(argv[4], NULL, 0);
		err = bt_mesh_cfg_mod_sub_add_vnd(net.net_idx, net.dst,
						  elem_addr, sub_addr, mod_id,
						  cid, &status);
	} else {
		err = bt_mesh_cfg_mod_sub_add(net.net_idx, net.dst, elem_addr,
					      sub_addr, mod_id, &status);
	}

	if (err) {
		printk("Unable to send Model Subscription Add (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Model Subscription Add failed with status 0x%02x\n",
		       status);
	} else {
		printk("Model subscription was successful\n");
	}

	return 0;
}

struct shell_cmd_help cmd_mod_sub_add_help = {
	NULL, "<elem addr> <sub addr> <Model ID> [Company ID]", NULL
};

static int cmd_mod_sub_del(int argc, char *argv[])
{
	u16_t elem_addr, sub_addr, mod_id, cid;
	u8_t status;
	int err;

	if (argc < 4) {
		return -EINVAL;
	}

	elem_addr = strtoul(argv[1], NULL, 0);
	sub_addr = strtoul(argv[2], NULL, 0);
	mod_id = strtoul(argv[3], NULL, 0);

	if (argc > 4) {
		cid = strtoul(argv[4], NULL, 0);
		err = bt_mesh_cfg_mod_sub_del_vnd(net.net_idx, net.dst,
						  elem_addr, sub_addr, mod_id,
						  cid, &status);
	} else {
		err = bt_mesh_cfg_mod_sub_del(net.net_idx, net.dst, elem_addr,
					      sub_addr, mod_id, &status);
	}

	if (err) {
		printk("Unable to send Model Subscription Delete (err %d)\n",
		       err);
		return 0;
	}

	if (status) {
		printk("Model Subscription Delete failed with status 0x%02x\n",
		       status);
	} else {
		printk("Model subscription deltion was successful\n");
	}

	return 0;
}

struct shell_cmd_help cmd_mod_sub_del_help = {
	NULL, "<elem addr> <sub addr> <Model ID> [Company ID]", NULL
};

static int cmd_mod_sub_get(int argc,
			      char *argv[])
{
	uint16_t elem_addr, mod_id, cid;
	uint16_t subs[16];
	uint8_t status;
	size_t cnt;
	int err, i;

	elem_addr = strtoul(argv[1], NULL, 0);
	mod_id = strtoul(argv[2], NULL, 0);
	cnt = ARRAY_SIZE(subs);

	if (argc > 3) {
		cid = strtoul(argv[3], NULL, 0);
		err = bt_mesh_cfg_mod_sub_get_vnd(net.net_idx, net.dst,
						  elem_addr, mod_id, cid,
						  &status, subs, &cnt);
	} else {
		err = bt_mesh_cfg_mod_sub_get(net.net_idx, net.dst, elem_addr,
					      mod_id, &status, subs, &cnt);
	}

	if (err) {
		printk("Unable to send Model Subscription Get "
			    "(err %d)", err);
		return 0;
	}

	if (status) {
		printk("Model Subscription Get failed with "
			    "status 0x%02x", status);
	} else {
		printk(
			"Model Subscriptions for Element 0x%04x, "
			"Model 0x%04x %s:",
			elem_addr, mod_id, argc > 3 ? argv[3] : "(SIG)");

		if (!cnt) {
			printk("\tNone.");
		}

		for (i = 0; i < cnt; i++) {
			printk("\t0x%04x", subs[i]);
		}
	}

	return 0;
}

struct shell_cmd_help cmd_mod_sub_get_help = {
	NULL, "<elem addr> <Model ID> [Company ID]", NULL
};


static int cmd_node_id_get(int argc,
			      char *argv[])
{
	struct bt_mesh_cfg_node_id_status status;
	int err;
	err = bt_mesh_cfg_node_identity_get(net.net_idx, net.dst,
					      &status);
	if (status.status == 0) 
	{
		printf("cmd_node_id_get identity 0x%02x,nix:%x\n", status.identity,status.net_idx);
	    return 0;
	}
	else
	{
        printf("cmd_node_id_get fail status:%x\n",status.status);
	    return -1;
	}
}

struct shell_cmd_help cmd_node_id_get_help = {
	NULL, " NULL", NULL
};

static int cmd_node_id_set(int argc,
			      char *argv[])
{
	uint8_t identity;
	int err;
    struct bt_mesh_cfg_node_id_status status;
    identity = strtoul(argv[1], NULL, 0);

	
	err = bt_mesh_cfg_node_identity_set(net.net_idx, net.dst,
					     identity, &status);
	if (status.status == 0) 
	{
		printf("cmd_node_id_set identity: 0x%02x\n", status.identity);
	    return 0;
	}
	else
	{
        printf("cmd_node_id_set failed :%x\n",status.status);
	    return -1;
	}
}

struct shell_cmd_help cmd_node_id_set_help = {
	NULL, " identity", NULL
};

static int cmd_lpn_timeout_get(int argc,
			      char *argv[])
{
	struct bt_mesh_cfg_lpn_pollto_status status;
	int err;
    u16_t lpn;

    lpn = strtoul(argv[1], NULL, 0);

	err = lpn_client_timeout_get(net.net_idx, net.dst,
					     lpn, &status);

	if (status.timeout != 0) 
	{
		printf("cmd_lpn_timeout_get addr: 0x%02x,timeout:%x\n", status.lpn_addr,status.timeout);
	    return 0;
	}
	else
	{
        printf("cmd_lpn_timeout_get failed\n");
	    return -1;
	}
}

struct shell_cmd_help cmd_lpn_timeout_get_help = {
	NULL, " lpn", NULL
};

static int cmd_net_transmit(int argc, char *argv[])
{
	uint8_t transmit;
	int err;

	if (argc < 2) {
		err = bt_mesh_cfg_net_transmit_get(net.net_idx,
				net.dst, &transmit);
	} else {
		if (argc != 3) {
			printk("Wrong number of input arguments"
						"(2 arguments are required)");
			return -EINVAL;
		}

		uint8_t count, interval, new_transmit;

		count = strtoul(argv[1], NULL, 0);
		interval = strtoul(argv[2], NULL, 0);

		new_transmit = BT_MESH_TRANSMIT(count, interval);

		err = bt_mesh_cfg_net_transmit_set(net.net_idx, net.dst,
				new_transmit, &transmit);
	}

	if (err) {
		printk("Unable to send network transmit"
				" Get/Set (err %d)", err);
		return 0;
	}

	printk("Transmit 0x%02x (count %u interval %ums)",
			transmit, BT_MESH_TRANSMIT_COUNT(transmit),
			BT_MESH_TRANSMIT_INT(transmit));

	return 0;
}

struct shell_cmd_help cmd_net_transmit_help = {
	NULL, "[<count: 0-7> <interval: 10-320>]", NULL
};

static int cmd_mod_sub_overwrite(int argc, char *argv[])
{
	u16_t elem_addr, sub_addr, mod_id, cid;
	u8_t status;
	int err;

	if (argc < 4) {
		return -EINVAL;
	}

	elem_addr = strtoul(argv[1], NULL, 0);
	sub_addr = strtoul(argv[2], NULL, 0);
	mod_id = strtoul(argv[3], NULL, 0);


		err = bt_mesh_cfg_mod_sub_overwrite(net.net_idx, net.dst, elem_addr,
					      sub_addr, mod_id, &status);


	if (err) {
		printk("Unable to send Model Subscription Overwrite (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Model Subscription Overwrite failed with status 0x%02x\n",
		       status);
	} else {
		printk("Model subscription was successful\n");
	}

	return 0;
}

struct shell_cmd_help cmd_mod_sub_overwrite_help = {
	NULL, "<elem addr> <sub addr> <Model ID> ", NULL
};

static int cmd_mod_sub_del_all(int argc, char *argv[])
{
	u16_t elem_addr, mod_id, cid;
	u8_t status;
	int err;

	if (argc < 3) {
		return -EINVAL;
	}

	elem_addr = strtoul(argv[1], NULL, 0);
	mod_id = strtoul(argv[2], NULL, 0);


		err = bt_mesh_cfg_mod_sub_del_all(net.net_idx, net.dst, elem_addr,
					       mod_id, &status);


	if (err) {
		printk("Unable to send Model Subscription del all (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Model Subscription del all failed with status 0x%02x\n",
		       status);
	} else {
		printk("Model subscription was successful\n");
	}

	return 0;
}

struct shell_cmd_help cmd_mod_sub_del_all_help = {
	NULL, "<elem addr>  <Model ID>", NULL
};

static int cmd_mod_sub_overwrite_va(int argc, char *argv[])
{
	u16_t elem_addr, sub_addr, mod_id, cid;
	u8_t label[16];
	u8_t status;
	size_t len;
	int err;

	if (argc < 3) {
		return -EINVAL;
	}
	
	memset(label, 0, sizeof(label));
    elem_addr = strtoul(argv[1], NULL, 0);
    sub_addr =  strtoul(argv[2], NULL, 0);
	memcpy(label,&sub_addr,2);
	mod_id = strtoul(argv[3], NULL, 0);


	err = bt_mesh_cfg_mod_sub_va_overwrite(net.net_idx, net.dst,
						 elem_addr, label, mod_id,
						 &sub_addr, &status);
	if (err) {
		printk("Unable to send Mod Sub VA overwrite (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Mod Sub VA overwrite failed with status 0x%02x\n",
		       status);
	} else {
		printk("0x%04x subscribed to Label UUID %s (va 0x%04x)\n",
		       elem_addr, argv[2], sub_addr);
	}

	return 0;
}

struct shell_cmd_help cmd_mod_sub_overwrite_va_help = {
	NULL, "<elem addr> <Label UUID> <Model ID>", NULL
};


static int cmd_mod_sub_add_va(int argc, char *argv[])
{
	u16_t elem_addr, sub_addr, mod_id, cid;
	u8_t label[16];
	u8_t status;
	size_t len;
	int err;

	if (argc < 4) {
		return -EINVAL;
	}

	elem_addr = strtoul(argv[1], NULL, 0);

	len = hex2bin(argv[2], label, sizeof(label));
	memset(label + len, 0, sizeof(label) - len);

	mod_id = strtoul(argv[3], NULL, 0);

	if (argc > 4) {
		cid = strtoul(argv[4], NULL, 0);
		err = bt_mesh_cfg_mod_sub_va_add_vnd(net.net_idx, net.dst,
						     elem_addr, label, mod_id,
						     cid, &sub_addr, &status);
	} else {
		err = bt_mesh_cfg_mod_sub_va_add(net.net_idx, net.dst,
						 elem_addr, label, mod_id,
						 &sub_addr, &status);
	}

	if (err) {
		printk("Unable to send Mod Sub VA Add (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Mod Sub VA Add failed with status 0x%02x\n",
		       status);
	} else {
		printk("0x%04x subscribed to Label UUID %s (va 0x%04x)\n",
		       elem_addr, argv[2], sub_addr);
	}

	return 0;
}

struct shell_cmd_help cmd_mod_sub_add_va_help = {
	NULL, "<elem addr> <Label UUID> <Model ID> [Company ID]", NULL
};

static int cmd_mod_sub_del_va(int argc, char *argv[])
{
	u16_t elem_addr, sub_addr, mod_id, cid;
	u8_t label[16];
	u8_t status;
	size_t len;
	int err;

	if (argc < 4) {
		return -EINVAL;
	}

	elem_addr = strtoul(argv[1], NULL, 0);

	len = hex2bin(argv[2], label, sizeof(label));
	memset(label + len, 0, sizeof(label) - len);

	mod_id = strtoul(argv[3], NULL, 0);

	if (argc > 4) {
		cid = strtoul(argv[4], NULL, 0);
		err = bt_mesh_cfg_mod_sub_va_del_vnd(net.net_idx, net.dst,
						     elem_addr, label, mod_id,
						     cid, &sub_addr, &status);
	} else {
		err = bt_mesh_cfg_mod_sub_va_del(net.net_idx, net.dst,
						 elem_addr, label, mod_id,
						 &sub_addr, &status);
	}

	if (err) {
		printk("Unable to send Model Subscription Delete (err %d)\n",
		       err);
		return 0;
	}

	if (status) {
		printk("Model Subscription Delete failed with status 0x%02x\n",
		       status);
	} else {
		printk("0x%04x unsubscribed from Label UUID %s (va 0x%04x)\n",
		       elem_addr, argv[2], sub_addr);
	}

	return 0;
}

struct shell_cmd_help cmd_mod_sub_del_va_help = {
	NULL, "<elem addr> <Label UUID> <Model ID> [Company ID]", NULL
};

static int mod_pub_get(u16_t addr, u16_t mod_id, u16_t cid)
{
	struct bt_mesh_cfg_mod_pub pub;
	u8_t status;
	int err;

	if (cid == CID_NVAL) {
		err = bt_mesh_cfg_mod_pub_get(net.net_idx, net.dst, addr,
					      mod_id, &pub, &status);
	} else {
		err = bt_mesh_cfg_mod_pub_get_vnd(net.net_idx, net.dst, addr,
						  mod_id, cid, &pub, &status);
	}

	if (err) {
		printk("Model Publication Get failed (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Model Publication Get failed (status 0x%02x)\n",
		       status);
		return 0;
	}

	printk("Model Publication for Element 0x%04x, Model 0x%04x:\n"
	       "\tPublish Address:                0x%04x\n"
	       "\tAppKeyIndex:                    0x%04x\n"
	       "\tCredential Flag:                %u\n"
	       "\tPublishTTL:                     %u\n"
	       "\tPublishPeriod:                  0x%02x\n"
	       "\tPublishRetransmitCount:         %u\n"
	       "\tPublishRetransmitInterval:      %ums\n",
	       addr, mod_id, pub.addr, pub.app_idx, pub.cred_flag, pub.ttl,
	       pub.period, BT_MESH_PUB_TRANSMIT_COUNT(pub.transmit),
	       BT_MESH_PUB_TRANSMIT_INT(pub.transmit));

	return 0;
}

static int mod_pub_va_set(u16_t addr, u16_t mod_id, u16_t cid, char *argv[])
{
	struct bt_mesh_cfg_mod_pub_va pub;
	u8_t status, count;
	u16_t interval;
	u16_t  pub_addr;
	int err;

    memset(pub.addr,0,16);
	pub_addr = strtoul(argv[0], NULL, 0);
	memcpy(pub.addr,&pub_addr,2);

	pub.app_idx = strtoul(argv[1], NULL, 0);
	pub.cred_flag = str2bool(argv[2]);
	pub.ttl = strtoul(argv[3], NULL, 0);
	pub.period = strtoul(argv[4], NULL, 0);

	count = strtoul(argv[5], NULL, 0);
	if (count > 7) {
		printk("Invalid retransmit count\n");
		return -EINVAL;
	}

	interval = strtoul(argv[6], NULL, 0);
	if (interval > (31 * 50) || (interval % 50)) {
		printk("Invalid retransmit interval %u\n", interval);
		return -EINVAL;
	}

	pub.transmit = BT_MESH_PUB_TRANSMIT(count, interval);

	if (cid == CID_NVAL) {
		err = bt_mesh_cfg_mod_pub_va_set(net.net_idx, net.dst, addr,
					      mod_id, &pub, &status);
	} 

	if (err) {
		printk("Model Publication va Set failed (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Model Publication Set failed (status 0x%02x)\n",
		       status);
	} else {
		printk("Model Publication successfully set\n");
	}

	return 0;
}



static int mod_pub_set(u16_t addr, u16_t mod_id, u16_t cid, char *argv[])
{
	struct bt_mesh_cfg_mod_pub pub;
	u8_t status, count;
	u16_t interval;
	int err;

	pub.addr = strtoul(argv[0], NULL, 0);
	pub.app_idx = strtoul(argv[1], NULL, 0);
	pub.cred_flag = str2bool(argv[2]);
	pub.ttl = strtoul(argv[3], NULL, 0);
	pub.period = strtoul(argv[4], NULL, 0);

	count = strtoul(argv[5], NULL, 0);
	if (count > 7) {
		printk("Invalid retransmit count\n");
		return -EINVAL;
	}

	interval = strtoul(argv[6], NULL, 0);
	if (interval > (31 * 50) || (interval % 50)) {
		printk("Invalid retransmit interval %u\n", interval);
		return -EINVAL;
	}

	pub.transmit = BT_MESH_PUB_TRANSMIT(count, interval);

	if (cid == CID_NVAL) {
		err = bt_mesh_cfg_mod_pub_set(net.net_idx, net.dst, addr,
					      mod_id, &pub, &status);
	} else {
		err = bt_mesh_cfg_mod_pub_set_vnd(net.net_idx, net.dst, addr,
						  mod_id, cid, &pub, &status);
	}

	if (err) {
		printk("Model Publication Set failed (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Model Publication Set failed (status 0x%02x)\n",
		       status);
	} else {
		printk("Model Publication successfully set\n");
	}

	return 0;
}

//add model pub virtual address cmd

static int cmd_mod_pub_va(int argc, char *argv[])
{
	u16_t addr, mod_id, cid;

	if (argc < 3) {
		return -EINVAL;
	}

	addr = strtoul(argv[1], NULL, 0);
	mod_id = strtoul(argv[2], NULL, 0);

	argc -= 3;
	argv += 3;

	if (argc == 1 || argc == 8) {
		cid = strtoul(argv[0], NULL, 0);
		argc--;
		argv++;
	} else {
		cid = CID_NVAL;
	}

	if (argc > 0) {
		if (argc < 7) {
			return -EINVAL;
		}

		return mod_pub_va_set(addr, mod_id, cid, argv);
	} 
	return 0;
}

struct shell_cmd_help cmd_mod_pub_va_help = {
	NULL, "<addr> <mod id> [cid] [<PubAddr> "
	"<AppKeyIndex> <cred> <ttl> <period> <count> <interval>]" , NULL
};


static int cmd_mod_pub(int argc, char *argv[])
{
	u16_t addr, mod_id, cid;

	if (argc < 3) {
		return -EINVAL;
	}

	addr = strtoul(argv[1], NULL, 0);
	mod_id = strtoul(argv[2], NULL, 0);

	argc -= 3;
	argv += 3;

	if (argc == 1 || argc == 8) {
		cid = strtoul(argv[0], NULL, 0);
		argc--;
		argv++;
	} else {
		cid = CID_NVAL;
	}

	if (argc > 0) {
		if (argc < 7) {
			return -EINVAL;
		}

		return mod_pub_set(addr, mod_id, cid, argv);
	} else {
		return mod_pub_get(addr, mod_id, cid);
	}
}

struct shell_cmd_help cmd_mod_pub_help = {
	NULL, "<addr> <mod id> [cid] [<PubAddr> "
	"<AppKeyIndex> <cred> <ttl> <period> <count> <interval>]" , NULL
};

static void hb_sub_print(struct bt_mesh_cfg_hb_sub *sub)
{
	printk("Heartbeat Subscription:\n"
	       "\tSource:      0x%04x\n"
	       "\tDestination: 0x%04x\n"
	       "\tPeriodLog:   0x%02x\n"
	       "\tCountLog:    0x%02x\n"
	       "\tMinHops:     %u\n"
	       "\tMaxHops:     %u\n",
	       sub->src, sub->dst, sub->period, sub->count,
	       sub->min, sub->max);
}

static int hb_sub_get(int argc, char *argv[])
{
	struct bt_mesh_cfg_hb_sub sub;
	u8_t status;
	int err;

	err = bt_mesh_cfg_hb_sub_get(net.net_idx, net.dst, &sub, &status);
	if (err) {
		printk("Heartbeat Subscription Get failed (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Heartbeat Subscription Get failed (status 0x%02x)\n",
		       status);
	} else {
		hb_sub_print(&sub);
	}

	return 0;
}

static int hb_sub_set(int argc, char *argv[])
{
	struct bt_mesh_cfg_hb_sub sub;
	u8_t status;
	int err;

	sub.src = strtoul(argv[1], NULL, 0);
	sub.dst = strtoul(argv[2], NULL, 0);
	sub.period = strtoul(argv[3], NULL, 0);

	err = bt_mesh_cfg_hb_sub_set(net.net_idx, net.dst, &sub, &status);
	if (err) {
		printk("Heartbeat Subscription Set failed (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Heartbeat Subscription Set failed (status 0x%02x)\n",
		       status);
	} else {
		hb_sub_print(&sub);
	}

	return 0;
}

static int cmd_hb_sub(int argc, char *argv[])
{
	if (argc > 1) {
		if (argc < 4) {
			return -EINVAL;
		}

		return hb_sub_set(argc, argv);
	} else {
		return hb_sub_get(argc, argv);
	}
}

struct shell_cmd_help cmd_hb_sub_help = {
	NULL, "<src> <dst> <period>", NULL
};

static int hb_pub_get(int argc, char *argv[])
{
	struct bt_mesh_cfg_hb_pub pub;
	u8_t status;
	int err;

	err = bt_mesh_cfg_hb_pub_get(net.net_idx, net.dst, &pub, &status);
	if (err) {
		printk("Heartbeat Publication Get failed (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Heartbeat Publication Get failed (status 0x%02x)\n",
		       status);
		return 0;
	}

	printk("Heartbeat publication:\n");
	printk("\tdst 0x%04x count 0x%02x period 0x%02x\n",
	       pub.dst, pub.count, pub.period);
	printk("\tttl 0x%02x feat 0x%04x net_idx 0x%04x\n",
	       pub.ttl, pub.feat, pub.net_idx);

	return 0;
}

static int hb_pub_set(int argc, char *argv[])
{
	struct bt_mesh_cfg_hb_pub pub;
	u8_t status;
	int err;

	pub.dst = strtoul(argv[1], NULL, 0);
	pub.count = strtoul(argv[2], NULL, 0);
	pub.period = strtoul(argv[3], NULL, 0);
	pub.ttl = strtoul(argv[4], NULL, 0);
	pub.feat = strtoul(argv[5], NULL, 0);
	pub.net_idx = strtoul(argv[5], NULL, 0);

	err = bt_mesh_cfg_hb_pub_set(net.net_idx, net.dst, &pub, &status);
	if (err) {
		printk("Heartbeat Publication Set failed (err %d)\n", err);
		return 0;
	}

	if (status) {
		printk("Heartbeat Publication Set failed (status 0x%02x)\n",
		       status);
	} else {
		printk("Heartbeat publication successfully set\n");
	}

	return 0;
}

static int cmd_hb_pub(int argc, char *argv[])
{
	if (argc > 1) {
		if (argc < 7) {
			return -EINVAL;
		}

		return hb_pub_set(argc, argv);
	} else {
		return hb_pub_get(argc, argv);
	}
}

struct shell_cmd_help cmd_hb_pub_help = {
	NULL, "<dst> <count> <period> <ttl> <features> <NetKeyIndex>" , NULL
};

#endif /* MYNEWT_VAL(BLE_MESH_CFG_CLI) */

#if MYNEWT_VAL(BLE_MESH_PROV)
static int cmd_pb(bt_mesh_prov_bearer_t bearer, int argc, char *argv[])
{
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	if (str2bool(argv[1])) {
		err = bt_mesh_prov_enable(bearer);
		if (err) {
			printk("Failed to enable %s (err %d)\n",
			       bearer2str(bearer), err);
		} else {
			printk("%s enabled\n", bearer2str(bearer));
		}
	} else {
		err = bt_mesh_prov_disable(bearer);
		if (err) {
			printk("Failed to disable %s (err %d)\n",
			       bearer2str(bearer), err);
		} else {
			printk("%s disabled\n", bearer2str(bearer));
		}
	}

	return 0;

}

struct shell_cmd_help cmd_pb_help = {
	NULL, "<val: off, on>", NULL
};

#endif

#if MYNEWT_VAL(BLE_MESH_PB_ADV)
static int cmd_pb_adv(int argc, char *argv[])
{
	return cmd_pb(BT_MESH_PROV_ADV, argc, argv);
}
#endif /* CONFIG_BT_MESH_PB_ADV */

#if MYNEWT_VAL(BLE_MESH_PB_GATT)
static int cmd_pb_gatt(int argc, char *argv[])
{
	return cmd_pb(BT_MESH_PROV_GATT, argc, argv);
}
#endif /* CONFIG_BT_MESH_PB_GATT */

static int cmd_provision(int argc, char *argv[])
{
	u16_t net_idx, addr;
	u32_t iv_index;
	int err;

	if (argc < 3) {
		return -EINVAL;
	}

	net_idx = strtoul(argv[1], NULL, 0);
	addr = strtoul(argv[2], NULL, 0);

	if (argc > 3) {
		iv_index = strtoul(argv[3], NULL, 0);
	} else {
		iv_index = 0;
	}

	err = bt_mesh_provision(default_key, net_idx, 0, iv_index, addr,
				default_key);
	if (err) {
		printk("Provisioning failed (err %d)\n", err);
	}

	return 0;
}

struct shell_cmd_help cmd_provision_help = {
	NULL, "<NetKeyIndex> <addr> [IVIndex]" , NULL
};

#if MYNEWT_VAL(BLE_MESH_HEALTH_CLI)

static int cmd_fault_get(int argc, char *argv[])
{
	u8_t faults[32];
	size_t fault_count;
	u8_t test_id;
	u16_t cid;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	cid = strtoul(argv[1], NULL, 0);
	fault_count = sizeof(faults);

	err = bt_mesh_health_fault_get(net.net_idx, net.dst, net.app_idx, cid,
				       &test_id, faults, &fault_count);
	if (err) {
		printk("Failed to send Health Fault Get (err %d)\n", err);
	} else {
		show_faults(test_id, cid, faults, fault_count);
	}

	return 0;
}

struct shell_cmd_help cmd_fault_get_help = {
	NULL, "<Company ID>", NULL
};

static int cmd_fault_clear(int argc, char *argv[])
{
	u8_t faults[32];
	size_t fault_count;
	u8_t test_id;
	u16_t cid;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	cid = strtoul(argv[1], NULL, 0);
	fault_count = sizeof(faults);

	err = bt_mesh_health_fault_clear(net.net_idx, net.dst, net.app_idx,
					 cid, &test_id, faults, &fault_count);
	if (err) {
		printk("Failed to send Health Fault Clear (err %d)\n", err);
	} else {
		show_faults(test_id, cid, faults, fault_count);
	}

	return 0;
}

struct shell_cmd_help cmd_fault_clear_help = {
	NULL, "<Company ID>", NULL
};

static int cmd_fault_clear_unack(int argc, char *argv[])
{
	u16_t cid;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	cid = strtoul(argv[1], NULL, 0);

	err = bt_mesh_health_fault_clear(net.net_idx, net.dst, net.app_idx,
					 cid, NULL, NULL, NULL);
	if (err) {
		printk("Health Fault Clear Unacknowledged failed (err %d)\n",
		       err);
	}

	return 0;
}

struct shell_cmd_help cmd_fault_clear_unack_help = {
	NULL, "<Company ID>", NULL
};

static int cmd_fault_test(int argc, char *argv[])
{
	u8_t faults[32];
	size_t fault_count;
	u8_t test_id;
	u16_t cid;
	int err;

	if (argc < 3) {
		return -EINVAL;
	}

	cid = strtoul(argv[1], NULL, 0);
	test_id = strtoul(argv[2], NULL, 0);
	fault_count = sizeof(faults);

	err = bt_mesh_health_fault_test(net.net_idx, net.dst, net.app_idx,
					cid, test_id, faults, &fault_count);
	if (err) {
		printk("Failed to send Health Fault Test (err %d)\n", err);
	} else {
		show_faults(test_id, cid, faults, fault_count);
	}

	return 0;
}

struct shell_cmd_help cmd_fault_test_help = {
	NULL, "<Company ID> <Test ID>", NULL
};

static int cmd_fault_test_unack(int argc, char *argv[])
{
	u16_t cid;
	u8_t test_id;
	int err;

	if (argc < 3) {
		return -EINVAL;
	}

	cid = strtoul(argv[1], NULL, 0);
	test_id = strtoul(argv[2], NULL, 0);

	err = bt_mesh_health_fault_test(net.net_idx, net.dst, net.app_idx,
					cid, test_id, NULL, NULL);
	if (err) {
		printk("Health Fault Test Unacknowledged failed (err %d)\n",
		       err);
	}

	return 0;
}

struct shell_cmd_help cmd_fault_test_unack_help = {
	NULL, "<Company ID> <Test ID>", NULL
};

static int cmd_period_get(int argc, char *argv[])
{
	u8_t divisor;
	int err;

	err = bt_mesh_health_period_get(net.net_idx, net.dst, net.app_idx,
					&divisor);
	if (err) {
		printk("Failed to send Health Period Get (err %d)\n", err);
	} else {
		printk("Health FastPeriodDivisor: %u\n", divisor);
	}

	return 0;
}

static int cmd_period_set(int argc, char *argv[])
{
	u8_t divisor, updated_divisor;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	divisor = strtoul(argv[1], NULL, 0);

	err = bt_mesh_health_period_set(net.net_idx, net.dst, net.app_idx,
					divisor, &updated_divisor);
	if (err) {
		printk("Failed to send Health Period Set (err %d)\n", err);
	} else {
		printk("Health FastPeriodDivisor: %u\n", updated_divisor);
	}

	return 0;
}

struct shell_cmd_help cmd_period_set_help = {
	NULL, "<divisor>", NULL
};

static int cmd_period_set_unack(int argc, char *argv[])
{
	u8_t divisor;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	divisor = strtoul(argv[1], NULL, 0);

	err = bt_mesh_health_period_set(net.net_idx, net.dst, net.app_idx,
					divisor, NULL);
	if (err) {
		printk("Failed to send Health Period Set (err %d)\n", err);
	}

	return 0;
}

struct shell_cmd_help cmd_period_set_unack_help = {
	NULL, "<divisor>", NULL
};

static int cmd_attention_get(int argc, char *argv[])
{
	u8_t attention;
	int err;

	err = bt_mesh_health_attention_get(net.net_idx, net.dst, net.app_idx,
					   &attention);
	if (err) {
		printk("Failed to send Health Attention Get (err %d)\n", err);
	} else {
		printk("Health Attention Timer: %u\n", attention);
	}

	return 0;
}

static int cmd_attention_set(int argc, char *argv[])
{
	u8_t attention, updated_attention;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	attention = strtoul(argv[1], NULL, 0);

	err = bt_mesh_health_attention_set(net.net_idx, net.dst, net.app_idx,
					   attention, &updated_attention);
	if (err) {
		printk("Failed to send Health Attention Set (err %d)\n", err);
	} else {
		printk("Health Attention Timer: %u\n", updated_attention);
	}

	return 0;
}

struct shell_cmd_help cmd_attention_set_help = {
	NULL, "<timer>", NULL
};

static int cmd_attention_set_unack(int argc, char *argv[])
{
	u8_t attention;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	attention = strtoul(argv[1], NULL, 0);

	err = bt_mesh_health_attention_set(net.net_idx, net.dst, net.app_idx,
					   attention, NULL);
	if (err) {
		printk("Failed to send Health Attention Set (err %d)\n", err);
	}

	return 0;
}

struct shell_cmd_help cmd_attention_set_unack_help = {
	NULL, "<timer>", NULL
};

#endif /* MYNEWT_VAL(BLE_MESH_HEALTH_CLI) */

static int cmd_add_fault(int argc, char *argv[])
{
	u8_t fault_id;
	u8_t i;

	if (argc < 2) {
		return -EINVAL;
	}

	fault_id = strtoul(argv[1], NULL, 0);
	if (!fault_id) {
		printk("The Fault ID must be non-zero!\n");
		return -EINVAL;
	}

	for (i = 0; i < sizeof(cur_faults); i++) {
		if (!cur_faults[i]) {
			cur_faults[i] = fault_id;
			break;
		}
	}

	if (i == sizeof(cur_faults)) {
		printk("Fault array is full. Use \"del-fault\" to clear it\n");
		return 0;
	}

	for (i = 0; i < sizeof(reg_faults); i++) {
		if (!reg_faults[i]) {
			reg_faults[i] = fault_id;
			break;
		}
	}

	if (i == sizeof(reg_faults)) {
		printk("No space to store more registered faults\n");
	}

	bt_mesh_fault_update(&elements[0]);

	return 0;
}

struct shell_cmd_help cmd_add_fault_help = {
	NULL, "<Fault ID>", NULL
};

static int cmd_del_fault(int argc, char *argv[])
{
	u8_t fault_id;
	u8_t i;

	if (argc < 2) {
		memset(cur_faults, 0, sizeof(cur_faults));
		printk("All current faults cleared\n");
		bt_mesh_fault_update(&elements[0]);
		return 0;
	}

	fault_id = strtoul(argv[1], NULL, 0);
	if (!fault_id) {
		printk("The Fault ID must be non-zero!\n");
		return -EINVAL;
	}

	for (i = 0; i < sizeof(cur_faults); i++) {
		if (cur_faults[i] == fault_id) {
			cur_faults[i] = 0;
			printk("Fault cleared\n");
		}
	}

	bt_mesh_fault_update(&elements[0]);

	return 0;
}

struct shell_cmd_help cmd_del_fault_help = {
	NULL, "[Fault ID]", NULL
};

#if MYNEWT_VAL(BLE_MESH_SHELL_MODELS)
static int cmd_gen_onoff_get(int argc, char *argv[])
{
	u8_t state;
	int err;

	err = bt_mesh_gen_onoff_get(net.net_idx, net.dst, net.app_idx,
				    &state);
	if (err) {
		printk("Failed to send Generic OnOff Get (err %d)\n", err);
	} else {
		printk("Gen OnOff State %d\n", state);
	}

	return 0;
}

static int cmd_gen_onoff_set(int argc, char *argv[])
{
	u8_t state;
	u8_t val;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	val = strtoul(argv[1], NULL, 0);

	err = bt_mesh_gen_onoff_set(net.net_idx, net.dst, net.app_idx,
				    val, &state);
	if (err) {
		printk("Failed to send Generic OnOff Get (err %d)\n", err);
	} else {
		printk("Gen OnOff State %d\n", state);
	}

	return 0;
}

struct shell_cmd_help cmd_gen_onoff_set_help = {
	NULL, "<0|1>", NULL
};

static int cmd_gen_onoff_set_unack(int argc, char *argv[])
{
	u8_t val;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	val = strtoul(argv[1], NULL, 0);

	err = bt_mesh_gen_onoff_set(net.net_idx, net.dst, net.app_idx,
				    val, NULL);
	if (err) {
		printk("Failed to send Generic OnOff Get (err %d)\n", err);
	}

	return 0;
}

struct shell_cmd_help cmd_gen_onoff_set_unack_help = {
	NULL, "<0|1>", NULL
};

static int cmd_gen_level_get(int argc, char *argv[])
{
	s16_t state;
	int err;

	err = bt_mesh_gen_level_get(net.net_idx, net.dst, net.app_idx,
				    &state);
	if (err) {
		printk("Failed to send Generic Level Get (err %d)\n", err);
	} else {
		printk("Gen Level State %d\n", state);
	}

	return 0;
}

static int cmd_gen_level_set(int argc, char *argv[])
{
	s16_t state;
	s16_t val;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	val = (s16_t)strtoul(argv[1], NULL, 0);

	err = bt_mesh_gen_level_set(net.net_idx, net.dst, net.app_idx,
				    val, &state);
	if (err) {
		printk("Failed to send Generic Level Get (err %d)\n", err);
	} else {
		printk("Gen Level State %d\n", state);
	}

	return 0;
}

struct shell_cmd_help cmd_gen_level_set_help = {
	NULL, "<level>", NULL
};

static int cmd_gen_level_set_unack(int argc, char *argv[])
{
	s16_t val;
	int err;

	if (argc < 2) {
		return -EINVAL;
	}

	val = (s16_t)strtoul(argv[1], NULL, 0);

	err = bt_mesh_gen_level_set(net.net_idx, net.dst, net.app_idx,
				    val, NULL);
	if (err) {
		printk("Failed to send Generic Level Get (err %d)\n", err);
	}

	return 0;
}

struct shell_cmd_help cmd_gen_level_set_unack_help = {
	NULL, "<level>", NULL
};

#endif /* MYNEWT_VAL(BLE_MESH_SHELL_MODELS) */

static int cmd_print_credentials(int argc, char *argv[])
{
	bt_test_print_credentials();
	return 0;
}

static void print_comp_elem(struct bt_mesh_elem *elem,
			    bool primary)
{
	struct bt_mesh_model *mod;
	int i;

	printk("Loc: %u\n", elem->loc);
	printk("Model count: %u\n", elem->model_count);
	printk("Vnd model count: %u\n", elem->vnd_model_count);

	for (i = 0; i < elem->model_count; i++) {
		mod = &elem->models[i];
		printk("  Model: %u\n", i);
		printk("    ID: 0x%04x\n", mod->id);
		printk("    Opcode: 0x%08lx\n", mod->op->opcode);
	}

	for (i = 0; i < elem->vnd_model_count; i++) {
		mod = &elem->vnd_models[i];
		printk("  Vendor model: %u\n", i);
		printk("    Company: 0x%04x\n", mod->vnd.company);
		printk("    ID: 0x%04x\n", mod->vnd.id);
		printk("    Opcode: 0x%08lx\n", mod->op->opcode);
	}
}

static int cmd_print_composition_data(int argc, char *argv[])
{
	const struct bt_mesh_comp *comp;
	int i;

	comp = bt_mesh_comp_get();

	printk("CID: %u\n", comp->cid);
	printk("PID: %u\n", comp->pid);
	printk("VID: %u\n", comp->vid);

	for (i = 0; i < comp->elem_count; i++) {
		print_comp_elem(&comp->elem[i], i == 0);
	}

	return 0;
}

static const struct shell_cmd mesh_commands[] = {
	{ "init", cmd_mesh_init, NULL },
#if MYNEWT_VAL(BLE_MESH_PB_ADV)
	{ "pb-adv", cmd_pb_adv, &cmd_pb_help },
#endif
#if MYNEWT_VAL(BLE_MESH_PB_GATT)
	{ "pb-gatt", cmd_pb_gatt, &cmd_pb_help },
#endif
	{ "reset", cmd_reset, &cmd_reset_help },
	{ "uuid", cmd_uuid, &cmd_uuid_help },
	{ "input-num", cmd_input_num, &cmd_input_num_help },
	{ "input-str", cmd_input_str, &cmd_input_str_help },
	{ "static-oob", cmd_static_oob, &cmd_static_oob_help },
	{ "auth-oob", cmd_auth_oob_set, &cmd_auth_oob_help },
	{ "pub-key-oob", cmd_pub_key_oob, &cmd_pub_key_oob_help },
	{ "provision", cmd_provision, &cmd_provision_help },
#if MYNEWT_VAL(BLE_MESH_LOW_POWER)
	{ "lpn", cmd_lpn, &cmd_lpn_help },
	{ "poll", cmd_poll, NULL },
#endif
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY)
	{ "ident", cmd_ident, NULL },
#endif
	{ "dst", cmd_dst, &cmd_dst_help },
	{ "netidx", cmd_netidx, &cmd_netidx_help },
	{ "appidx", cmd_appidx, &cmd_appidx_help },

	/* Commands which access internal APIs, for testing only */
	{ "net-send", cmd_net_send, &cmd_net_send_help },
	{ "iv-update", cmd_iv_update, NULL },
	{ "iv-update-test", cmd_iv_update_test, &cmd_iv_update_test_help },
	{ "rpl-clear", cmd_rpl_clear, NULL },
#if MYNEWT_VAL(BLE_MESH_LOW_POWER)
	{ "lpn-subscribe", cmd_lpn_subscribe, &cmd_lpn_subscribe_help },
	{ "lpn-unsubscribe", cmd_lpn_unsubscribe, &cmd_lpn_unsubscribe_help },
#endif
	{ "print-credentials", cmd_print_credentials, NULL },
	{ "print-composition-data", cmd_print_composition_data, NULL },


#if MYNEWT_VAL(BLE_MESH_CFG_CLI)
	/* Configuration Client Model operations */
	{ "timeout", cmd_timeout, &cmd_timeout_help },
	{ "get-comp", cmd_get_comp, &cmd_get_comp_help },
	{ "beacon", cmd_beacon, &cmd_beacon_help },
	{ "ttl", cmd_ttl, &cmd_ttl_help},
	{ "friend", cmd_friend, &cmd_friend_help },
	{ "gatt-proxy", cmd_gatt_proxy, &cmd_gatt_proxy_help },
	{ "relay", cmd_relay, &cmd_relay_help },
	{ "net-key-add", cmd_net_key_add, &cmd_net_key_add_help },
	{ "net-key-update", cmd_net_key_update, &cmd_net_key_update_help },
	{ "net-key-get", cmd_net_key_get, NULL },
	{ "net-key-del", cmd_net_key_del, &cmd_net_key_del_help },
	{ "app-key-add", cmd_app_key_add, &cmd_app_key_add_help },
	{ "app-key-update", cmd_app_key_update, &cmd_app_key_update_help },
	{ "app-key-get", cmd_app_key_get, &cmd_app_key_get_help },
	{ "app-key-del", cmd_app_key_del, &cmd_app_key_del_help },
	{ "key-refresh-get", cmd_key_refresh_get, NULL },
	{ "key-refresh-set", cmd_key_refresh_set, &cmd_key_refresh_set_help },
	{ "mod-app-bind", cmd_mod_app_bind, &cmd_mod_app_bind_help },
    { "mod-app-unbind", cmd_mod_app_unbind, &cmd_mod_app_unbind_help },
    { "mod-app-get", cmd_mod_app_get, &cmd_mod_app_get_help },
	{ "mod-pub", cmd_mod_pub, &cmd_mod_pub_help },
	{ "mod-pub_va", cmd_mod_pub_va, &cmd_mod_pub_va_help },
	{ "mod-sub-add", cmd_mod_sub_add, &cmd_mod_sub_add_help },
	{ "mod-sub-del", cmd_mod_sub_del, &cmd_mod_sub_del_help },
	{ "mod-sub-get", cmd_mod_sub_get, &cmd_mod_sub_get_help },
	{ "mod-sub-overwrite", cmd_mod_sub_overwrite, &cmd_mod_sub_overwrite_help },
	{ "mod-sub-del_all", cmd_mod_sub_del_all, &cmd_mod_sub_del_all_help },
	{ "mod-sub-add-va", cmd_mod_sub_add_va, &cmd_mod_sub_add_va_help },
	{ "mod-sub-del-va", cmd_mod_sub_del_va, &cmd_mod_sub_del_va_help },
	{ "mod-sub-overwrite-va", cmd_mod_sub_overwrite_va, &cmd_mod_sub_overwrite_va_help },
	{ "nod-id-get", cmd_node_id_get, &cmd_node_id_get_help },
	{ "nod-id-set", cmd_node_id_set, &cmd_node_id_set_help },
	{ "lpn-tmo-get", cmd_lpn_timeout_get, &cmd_lpn_timeout_get_help },
	{ "net-transmit-param", cmd_net_transmit, &cmd_net_transmit_help },
	{ "hb-sub", cmd_hb_sub, &cmd_hb_sub_help },
	{ "hb-pub", cmd_hb_pub, &cmd_hb_pub_help },
#endif
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY_CLIENT)
	{ "proxy-connect", cmd_proxy_client_connect, &cmd_proxy_client_connect_help },
	{ "proxy-client-cfg-send", cmd_proxy_client_cfg_send, NULL },	
#endif

#if MYNEWT_VAL(BLE_MESH_HEALTH_CLI)
	/* Health Client Model Operations */
	{ "fault-get", cmd_fault_get, &cmd_fault_get_help },
	{ "fault-clear", cmd_fault_clear, &cmd_fault_clear_help },
	{ "fault-clear-unack", cmd_fault_clear_unack, &cmd_fault_clear_unack_help },
	{ "fault-test", cmd_fault_test, &cmd_fault_test_help },
	{ "fault-test-unack", cmd_fault_test_unack, &cmd_fault_test_unack_help },
	{ "period-get", cmd_period_get, NULL },
	{ "period-set", cmd_period_set, &cmd_period_set_help },
	{ "period-set-unack", cmd_period_set_unack, &cmd_period_set_unack_help },
	{ "attention-get", cmd_attention_get, NULL },
	{ "attention-set", cmd_attention_set, &cmd_attention_set_help },
	{ "attention-set-unack", cmd_attention_set_unack, &cmd_attention_set_unack_help },
#endif

	/* Health Server Model Operations */
	{ "add-fault", cmd_add_fault, &cmd_add_fault_help },
	{ "del-fault", cmd_del_fault, &cmd_del_fault_help },

#if MYNEWT_VAL(BLE_MESH_SHELL_MODELS)
	/* Generic Client Model Operations */
	{ "gen-onoff-get", cmd_gen_onoff_get, NULL },
	{ "gen-onoff-set", cmd_gen_onoff_set, &cmd_gen_onoff_set_help },
	{ "gen-onoff-set-unack", cmd_gen_onoff_set_unack, &cmd_gen_onoff_set_unack_help },
	{ "gen-level-get", cmd_gen_level_get, NULL },
	{ "gen-level-set", cmd_gen_level_set, &cmd_gen_level_set_help },
	{ "gen-level-set-unack", cmd_gen_level_set_unack, &cmd_gen_level_set_unack_help },
#endif

#if MYNEWT_VAL(BLE_MESH_PROVISIONER)
	{ "provisioner", cmd_provisioner, &cmd_provisioner_help },
	{ "pinput-num", cmd_provisioner_input_num, &cmd_provisioner_input_num_help },
	{ "pinput-str", cmd_provisioner_input_str, &cmd_provisioner_input_str_help },
	{ "pstatic-oob", cmd_provisioner_static_oob, &cmd_provisioner_static_oob_help },
	{ "ppub_key-oob", cmd_provisioner_pub_key_oob, &cmd_provisioner_pub_key_oob_help },
	{ "local-app-key-update", cmd_local_app_key_update, &cmd_local_app_key_update_help },//Key refresh phase 2 use
	{ "local-net-key-update", cmd_local_net_key_update, &cmd_local_net_key_update_help },//Key refresh phase 2 use
	{ "local-app-key-add", cmd_local_app_key_add, &cmd_local_app_key_add_help },
	{ "local-net-key-add", cmd_local_net_key_add, &cmd_local_net_key_add_help },
	{ "provisioner-beacon", cmd_provisioner_beacon, &cmd_provisioner_beacon_help },
#endif

	{ NULL, NULL, NULL}
};


static void print_command_params(int cmdid)
{
    const struct shell_cmd *shell_cmd = &mesh_commands[cmdid];
    int i;

    if (!(shell_cmd->help && shell_cmd->help->params))
    {
        return;
    }

    for (i = 0; shell_cmd->help->params[i].param; i++)
    {
        iot_printf("%-30s%s\n", shell_cmd->help->params[i].param,
                       shell_cmd->help->params[i].param_desc);
    }
}


#define MAX_ARGV_CNT 16
static void cli_btmesh(char *pLine)
{
	int argc=0;
	char *argv[MAX_ARGV_CNT];
	char * str;
	int cmdid=0;
	const struct shell_cmd *cmd;
	
	while(1){
		//pLine = CmdLine_SkipSpace(pLine);
	    str = cli_get_token(&pLine);
	    if (str[0] == 0)
	    {
	        break;
	    }		
		argv[argc]=str;
		argc++;
		if(argc>=MAX_ARGV_CNT){
			iot_printf("too much args error!!\n");
			return;
		}
	}
	if(argc >=1){
		for(cmdid=0;cmdid<sizeof(mesh_commands)/sizeof(mesh_commands[0]);cmdid++){
			if(cli_string_cmmpare(argv[0], "help")){
				if(argc==2){
					if (cli_string_cmmpare(argv[1], mesh_commands[cmdid].sc_cmd)){
			        	cmd = &mesh_commands[cmdid];

			            if (!cmd->help || (!cmd->help->summary &&
			                               !cmd->help->usage &&
			                               !cmd->help->params))
			            {
			                iot_printf("(no help available)\n");
			                return;
			            }

			            if (cmd->help->summary)
			            {
			                iot_printf("Summary:\n");
			                iot_printf("%s\n", cmd->help->summary);
			            }

			            if (cmd->help->usage)
			            {
			                iot_printf("Usage:\n");
			                iot_printf("%s\n", cmd->help->usage);
			            }

			            if (cmd->help->params)
			            {
			                iot_printf("Parameters:\n");
			                print_command_params(cmdid);
			            }					
					}
				}else{
					for(cmdid=0;cmdid<sizeof(mesh_commands)/sizeof(mesh_commands[0]);cmdid++){
						iot_printf("%s\n",mesh_commands[cmdid].sc_cmd);
					}
				}
			}
			else if (cli_string_cmmpare(argv[0], mesh_commands[cmdid].sc_cmd)){
				 mesh_commands[cmdid].sc_cmd_func(argc,argv);
			}
		}
	}
}

static struct cli_cmd_struct btMeshCommands[] =
{
	{.cmd ="btmesh",	.fn = cli_btmesh,		.next = (void*)0 },
};



static void blemesh_on_reset(int reason)
{
    MODLOG_DFLT(INFO, "Resetting state; reason=%d\n", reason);
}

static void blemesh_on_sync(void)
{
	MODLOG_DFLT(INFO, "blemesh_on_sync\n");
	//cmd_mesh_init(0, NULL);
}

static void blemesh_host_task(void *param)
{
    ble_hs_cfg.reset_cb = blemesh_on_reset;
    ble_hs_cfg.sync_cb = blemesh_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    nimble_port_run();
}

static TaskHandle_t host_task_h;

static void ble_mesh_thread_startup(void)
{
	xTaskCreate(blemesh_host_task, "host", 4096,
				NULL, tskIDLE_PRIORITY + 1, &host_task_h);
}



void ble_mesh_shell_init(void)
{
	cli_add_cmds(&btMeshCommands[0],
		   sizeof(btMeshCommands)/sizeof(btMeshCommands[0]));

	health_pub_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    bt_mesh_register_gatt();
	ble_mesh_thread_startup();
}

#endif

