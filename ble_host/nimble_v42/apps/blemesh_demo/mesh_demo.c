/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/* BLE */
#include "syscfg/syscfg.h"

#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "mesh/mesh.h"

#include "FreeRTOS.h"
#include "task.h"
#include "cli.h"



#define BT_DBG_ENABLED (MYNEWT_VAL(BLE_MESH_DEBUG))

/* Model Operation Codes */
#define BT_MESH_MODEL_OP_GEN_ONOFF_GET		BT_MESH_MODEL_OP_2(0x82, 0x01)
#define BT_MESH_MODEL_OP_GEN_ONOFF_SET		BT_MESH_MODEL_OP_2(0x82, 0x02)
#define BT_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK	BT_MESH_MODEL_OP_2(0x82, 0x03)
#define BT_MESH_MODEL_OP_GEN_ONOFF_STATUS	BT_MESH_MODEL_OP_2(0x82, 0x04)


/* Company ID */
#define CID_VENDOR 0x05C3
#define STANDARD_TEST_ID 0x00
#define TEST_ID 0x01
static int recent_test_id = STANDARD_TEST_ID;

#define FAULT_ARR_SIZE 2


#define	MESH_LED_PIN	11
#define	MESH_KEY_PIN	12

#define	MESH_LED_ON		0
#define	MESH_LED_OFF	1

#define	MESH_KEY_DOWN	0
#define	MESH_KEY_UP		1


static bool has_reg_fault = true;

static struct ble_npl_event mesh_demo_key_event;
static uint16_t primary_addr;
static uint8_t trans_id;
static uint8_t press_state = 0;



static struct bt_mesh_cfg_srv cfg_srv = {
    .relay = BT_MESH_RELAY_ENABLED,
    .beacon = BT_MESH_BEACON_ENABLED,
#if MYNEWT_VAL(BLE_MESH_FRIEND)
    .frnd = BT_MESH_FRIEND_ENABLED,
#endif
#if MYNEWT_VAL(BLE_MESH_GATT_PROXY)
    .gatt_proxy = BT_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = BT_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl = 7,

    /* 3 transmissions with 20ms interval */
    .net_transmit = BT_MESH_TRANSMIT(2, 20),
    .relay_retransmit = BT_MESH_TRANSMIT(2, 20),
};

void ble_store_config_init(void);

static int
fault_get_cur(struct bt_mesh_model *model,
              uint8_t *test_id,
              uint16_t *company_id,
              uint8_t *faults,
              uint8_t *fault_count)
{
    uint8_t reg_faults[FAULT_ARR_SIZE] = { [0 ... FAULT_ARR_SIZE - 1] = 0xff };

    MODLOG_DFLT(INFO, "fault_get_cur() has_reg_fault %u\n", has_reg_fault);

    *test_id = recent_test_id;
    *company_id = CID_VENDOR;

    *fault_count = min(*fault_count, sizeof(reg_faults));
    memcpy(faults, reg_faults, *fault_count);

    return 0;
}

static int
fault_get_reg(struct bt_mesh_model *model,
              uint16_t company_id,
              uint8_t *test_id,
              uint8_t *faults,
              uint8_t *fault_count)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }

    MODLOG_DFLT(INFO, "fault_get_reg() has_reg_fault %u\n", has_reg_fault);

    *test_id = recent_test_id;

    if (has_reg_fault) {
        uint8_t reg_faults[FAULT_ARR_SIZE] = { [0 ... FAULT_ARR_SIZE - 1] = 0xff };

        *fault_count = min(*fault_count, sizeof(reg_faults));
        memcpy(faults, reg_faults, *fault_count);
    } else {
        *fault_count = 0;
    }

    return 0;
}

static int
fault_clear(struct bt_mesh_model *model, uint16_t company_id)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }

    has_reg_fault = false;

    return 0;
}

static int
fault_test(struct bt_mesh_model *model, uint8_t test_id, uint16_t company_id)
{
    if (company_id != CID_VENDOR) {
        return -BLE_HS_EINVAL;
    }

    if (test_id != STANDARD_TEST_ID && test_id != TEST_ID) {
        return -BLE_HS_EINVAL;
    }

    recent_test_id = test_id;
    has_reg_fault = true;
    bt_mesh_fault_update(bt_mesh_model_elem(model));

    return 0;
}

static const struct bt_mesh_health_srv_cb health_srv_cb = {
    .fault_get_cur = &fault_get_cur,
    .fault_get_reg = &fault_get_reg,
    .fault_clear = &fault_clear,
    .fault_test = &fault_test,
};

static struct bt_mesh_health_srv health_srv = {
    .cb = &health_srv_cb,
};

static struct bt_mesh_model_pub health_pub;

static void
health_pub_init(void)
{
    health_pub.msg  = BT_MESH_HEALTH_FAULT_MSG(0);
}

static struct bt_mesh_model_pub gen_level_pub;
static struct bt_mesh_model_pub gen_onoff_pub_srv;
static struct bt_mesh_model_pub gen_onoff_pub_cli;

static uint8_t gen_on_off_state;
static int16_t gen_level_state;

static void gen_onoff_status(struct bt_mesh_model *model,
                             struct bt_mesh_msg_ctx *ctx)
{
    struct os_mbuf *msg = NET_BUF_SIMPLE(3);
    uint8_t *status;

    MODLOG_DFLT(INFO, "#mesh-onoff STATUS:%d\n", gen_on_off_state);

    bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_2(0x82, 0x04));
    status = net_buf_simple_add(msg, 1);
    *status = gen_on_off_state;

    if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
        MODLOG_DFLT(INFO, "#mesh-onoff STATUS: send status failed\n");
    }

    os_mbuf_free_chain(msg);
}

static void gen_onoff_get(struct bt_mesh_model *model,
                          struct bt_mesh_msg_ctx *ctx,
                          struct os_mbuf *buf)
{
    MODLOG_DFLT(INFO, "#mesh-onoff GET\n");

    gen_onoff_status(model, ctx);
}

static void gen_onoff_set(struct bt_mesh_model *model,
                          struct bt_mesh_msg_ctx *ctx,
                          struct os_mbuf *buf)
{ 
    gen_on_off_state = buf->om_data[0];
	MODLOG_DFLT(INFO, "#mesh-onoff SET:%d\n", gen_on_off_state);

#if 0	
	if(gen_on_off_state){
		rt_pin_write(MESH_LED_PIN, MESH_LED_ON);
	}else{
		rt_pin_write(MESH_LED_PIN, MESH_LED_OFF);
	}
#endif
    gen_onoff_status(model, ctx);
}

static void gen_onoff_set_unack(struct bt_mesh_model *model,
                                struct bt_mesh_msg_ctx *ctx,
                                struct os_mbuf *buf)
{
    gen_on_off_state = buf->om_data[0];
	MODLOG_DFLT(INFO, "#mesh-onoff SET-UNACK:%d\n", gen_on_off_state);
#if 0
	if(gen_on_off_state){
		rt_pin_write(MESH_LED_PIN, MESH_LED_ON);
	}else{
		rt_pin_write(MESH_LED_PIN, MESH_LED_OFF);
	}
#endif
}

static void gen_onoff_status_cli(struct bt_mesh_model *model,
                          struct bt_mesh_msg_ctx *ctx,
                          struct os_mbuf *buf)
{
	uint8_t state;

	state = net_buf_simple_pull_u8(buf);
	press_state = state;

	MODLOG_DFLT(INFO, "Node 0x%04x OnOff status from 0x%04x with state 0x%02x",
                bt_mesh_model_elem(model)->addr, ctx->addr, state);
}


static const struct bt_mesh_model_op gen_onoff_op_srv[] = {
    { BT_MESH_MODEL_OP_2(0x82, 0x01), 0, gen_onoff_get },
    { BT_MESH_MODEL_OP_2(0x82, 0x02), 2, gen_onoff_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x03), 2, gen_onoff_set_unack },
    BT_MESH_MODEL_OP_END,
};

static const struct bt_mesh_model_op gen_onoff_op_cli[] = {
    { BT_MESH_MODEL_OP_2(0x82, 0x04), 1, gen_onoff_status_cli },
    BT_MESH_MODEL_OP_END,	
};


static void gen_level_status(struct bt_mesh_model *model,
                             struct bt_mesh_msg_ctx *ctx)
{
    struct os_mbuf *msg = NET_BUF_SIMPLE(4);

    MODLOG_DFLT(INFO, "#mesh-level STATUS:%d\n", gen_level_state);

    bt_mesh_model_msg_init(msg, BT_MESH_MODEL_OP_2(0x82, 0x08));
    net_buf_simple_add_le16(msg, gen_level_state);

    if (bt_mesh_model_send(model, ctx, msg, NULL, NULL)) {
        MODLOG_DFLT(INFO, "#mesh-level STATUS: send status failed\n");
    }

    os_mbuf_free_chain(msg);
}

static void gen_level_get(struct bt_mesh_model *model,
                          struct bt_mesh_msg_ctx *ctx,
                          struct os_mbuf *buf)
{
    MODLOG_DFLT(INFO, "#mesh-level GET\n");

    gen_level_status(model, ctx);
}

static void gen_level_set(struct bt_mesh_model *model,
                          struct bt_mesh_msg_ctx *ctx,
                          struct os_mbuf *buf)
{
    int16_t level;

    level = (int16_t) net_buf_simple_pull_le16(buf);
    MODLOG_DFLT(INFO, "#mesh-level SET: level=%d\n", level);

    gen_level_status(model, ctx);

    gen_level_state = level;
    MODLOG_DFLT(INFO, "#mesh-level: level=%d\n", gen_level_state);
}

static void gen_level_set_unack(struct bt_mesh_model *model,
                                struct bt_mesh_msg_ctx *ctx,
                                struct os_mbuf *buf)
{
    int16_t level;

    level = (int16_t) net_buf_simple_pull_le16(buf);
    MODLOG_DFLT(INFO, "#mesh-level SET-UNACK: level=%d\n", level);

    gen_level_state = level;
    MODLOG_DFLT(INFO, "#mesh-level: level=%d\n", gen_level_state);
}

static void gen_delta_set(struct bt_mesh_model *model,
                          struct bt_mesh_msg_ctx *ctx,
                          struct os_mbuf *buf)
{
    int16_t delta_level;

    delta_level = (int16_t) net_buf_simple_pull_le16(buf);
    MODLOG_DFLT(INFO, "#mesh-level DELTA-SET: delta_level=%d\n", delta_level);

    gen_level_status(model, ctx);

    gen_level_state += delta_level;
    MODLOG_DFLT(INFO, "#mesh-level: level=%d\n", gen_level_state);
}

static void gen_delta_set_unack(struct bt_mesh_model *model,
                                struct bt_mesh_msg_ctx *ctx,
                                struct os_mbuf *buf)
{
    int16_t delta_level;

    delta_level = (int16_t) net_buf_simple_pull_le16(buf);
    MODLOG_DFLT(INFO, "#mesh-level DELTA-SET: delta_level=%d\n", delta_level);

    gen_level_state += delta_level;
    MODLOG_DFLT(INFO, "#mesh-level: level=%d\n", gen_level_state);
}

static void gen_move_set(struct bt_mesh_model *model,
                         struct bt_mesh_msg_ctx *ctx,
                         struct os_mbuf *buf)
{
}

static void gen_move_set_unack(struct bt_mesh_model *model,
                               struct bt_mesh_msg_ctx *ctx,
                               struct os_mbuf *buf)
{
}

static const struct bt_mesh_model_op gen_level_op[] = {
    { BT_MESH_MODEL_OP_2(0x82, 0x05), 0, gen_level_get },
    { BT_MESH_MODEL_OP_2(0x82, 0x06), 3, gen_level_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x07), 3, gen_level_set_unack },
    { BT_MESH_MODEL_OP_2(0x82, 0x09), 5, gen_delta_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x0a), 5, gen_delta_set_unack },
    { BT_MESH_MODEL_OP_2(0x82, 0x0b), 3, gen_move_set },
    { BT_MESH_MODEL_OP_2(0x82, 0x0c), 3, gen_move_set_unack },
    BT_MESH_MODEL_OP_END,
};

static struct bt_mesh_model root_models[] = {
    BT_MESH_MODEL_CFG_SRV(&cfg_srv),
    BT_MESH_MODEL_HEALTH_SRV(&health_srv, &health_pub),
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_SRV, gen_onoff_op_srv,
    &gen_onoff_pub_srv, NULL),
	BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_ONOFF_CLI, gen_onoff_op_cli,
	&gen_onoff_pub_cli, NULL),
    BT_MESH_MODEL(BT_MESH_MODEL_ID_GEN_LEVEL_SRV, gen_level_op,
    &gen_level_pub, NULL),
};

static struct bt_mesh_model vnd_models[] = {
    BT_MESH_MODEL_VND(CID_VENDOR, BT_MESH_MODEL_ID_GEN_ONOFF_SRV, gen_onoff_op_srv,
    &gen_onoff_pub_srv, NULL),
};

static struct bt_mesh_elem elements[] = {
    BT_MESH_ELEM(0, root_models, vnd_models),
};

static const struct bt_mesh_comp comp = {
    .cid = CID_VENDOR,
    .elem = elements,
    .elem_count = ARRAY_SIZE(elements),
};

static int output_number(bt_mesh_output_action_t action, uint32_t number)
{
    MODLOG_DFLT(INFO, "OOB Number: %u\n", number);

    return 0;
}

static void prov_complete(u16_t net_idx, u16_t addr)
{
    MODLOG_DFLT(INFO, "Local node provisioned, primary address 0x%04x\n", addr);
	primary_addr = addr;
}

//static uint8_t dev_uuid[16] = MYNEWT_VAL(BLE_MESH_DEV_UUID);
static uint8_t dev_uuid[16] = {0xdd, 0xdd, 0};


static const struct bt_mesh_prov prov = {
    .uuid = dev_uuid,
    .output_size = 4,
    .output_actions = BT_MESH_DISPLAY_NUMBER | BT_MESH_BEEP | BT_MESH_VIBRATE | BT_MESH_BLINK,
    .output_number = output_number,
    .complete = prov_complete,
};

static void
blemesh_on_reset(int reason)
{
    MODLOG_DFLT(INFO, "Resetting state; reason=%d\n", reason);
}

static void
blemesh_on_sync(void)
{
    int err;
    ble_addr_t addr;

    MODLOG_DFLT(INFO, "Bluetooth initialized\n");

    /* Use NRPA */
    err = ble_hs_id_gen_rnd(1, &addr);
    assert(err == 0);
    err = ble_hs_id_set_rnd(addr.val);
    assert(err == 0);

    err = bt_mesh_init(addr.type, &prov, &comp, NULL);
    if (err) {
        MODLOG_DFLT(INFO, "Initializing mesh failed (err %d)\n", err);
        return;
    }

    MODLOG_DFLT(INFO, "Mesh initialized\n");

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    if (bt_mesh_is_provisioned()) {
        MODLOG_DFLT(INFO, "Mesh network restored from flash\n");
    }else{
		bt_mesh_prov_enable(BT_MESH_PROV_ADV | BT_MESH_PROV_GATT);
		MODLOG_DFLT(INFO, "Mesh prov_enable\n");
	}
}

static void blemesh_host_task(void *param)
{
    ble_hs_cfg.reset_cb = blemesh_on_reset;
    ble_hs_cfg.sync_cb = blemesh_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    health_pub_init();
    nimble_port_run();
}

static TaskHandle_t host_task_h;

static void ble_mesh_thread_startup(void)
{
	xTaskCreate(blemesh_host_task, "host", 4096,
				NULL, tskIDLE_PRIORITY + 1, &host_task_h);
}

#if 0
static void blemesh_keypress_irq(void *arg)
{
	int state = 0;

	state = rt_pin_read(MESH_KEY_PIN);
	MODLOG_DFLT(INFO, "key press irq value:%d\n", state);

	if(state == 0){
		ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &mesh_demo_key_event);
	}
}
#endif

static void blemesh_keypress_handle(struct ble_npl_event *ev)
{
	struct bt_mesh_model *mod_cli;
	struct bt_mesh_model_pub *pub_cli;
	int err;

	MODLOG_DFLT(INFO, "blemesh_keypress_handle\n");
	
	if(primary_addr == BT_MESH_ADDR_UNASSIGNED){
		MODLOG_DFLT(INFO, "model unprovisioned\n");
		return;
	}
	
	mod_cli = &root_models[3];
	pub_cli = mod_cli->pub;

    if (pub_cli->addr == BT_MESH_ADDR_UNASSIGNED) {
		MODLOG_DFLT(INFO, "model unset publish address\n");
        return;
    }	
	if(press_state){
		press_state = 0;
	}else{
		press_state = 1;
	}

    MODLOG_DFLT(INFO, "publish to 0x%04x onoff 0x%04x\n",
                pub_cli->addr, press_state);

	pub_cli->msg = NET_BUF_SIMPLE(4);
    bt_mesh_model_msg_init(pub_cli->msg, BT_MESH_MODEL_OP_GEN_ONOFF_SET);
    net_buf_simple_add_u8(pub_cli->msg, press_state);
    net_buf_simple_add_u8(pub_cli->msg, trans_id++);
    err = bt_mesh_model_publish(mod_cli);
    if (err) {
        MODLOG_DFLT(INFO, "bt_mesh_model_publish err %d", err);
    }		
	os_mbuf_free_chain(pub_cli->msg);
}


int ble_mesh_demo(void)
{
	iot_printf("ble_mesh_demo\n");
	
    ble_npl_event_init(&mesh_demo_key_event, blemesh_keypress_handle, NULL);

    ble_svc_gap_init();
    ble_svc_gatt_init();

    bt_mesh_register_gatt();
    ble_mesh_thread_startup();
	
	return 0;
}

static void cli_btmesh(char *pLine)
{
	char * str;
	
	str = cli_get_token(&pLine);
	if(cli_string_cmmpare(str, "onofftest")){
		ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &mesh_demo_key_event);
	}
}

static struct cli_cmd_struct btMeshCommands[] =
{
	{.cmd ="btmesh",	.fn = cli_btmesh,		.next = (void*)0 },
};

void btMesh_cmd_init(void)
{
    cli_add_cmds(&btMeshCommands[0],
		sizeof(btMeshCommands)/sizeof(btMeshCommands[0]));
}



