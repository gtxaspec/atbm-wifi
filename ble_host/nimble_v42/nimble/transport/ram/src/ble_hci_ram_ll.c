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

#include <assert.h>


#include "syscfg/syscfg.h"
#include "sysinit/sysinit.h"
#include "os/os.h"
#include "mem/mem.h"
#include "nimble/ble.h"
#include "nimble/ble_hci_trans.h"
#include "transport/ram/ble_hci_ram.h"
#include "transport/uart/ble_hci_uart.h"
#include "nimble/hci_common.h"
#include "dev_int.h"
#include "hw_defs.h"



#define ACL_BLOCK_SIZE  OS_ALIGN(MYNEWT_VAL(BLE_ACL_BUF_SIZE) \
                                 + BLE_MBUF_MEMBLOCK_OVERHEAD \
                                 + BLE_HCI_DATA_HDR_SZ, OS_ALIGNMENT)


//extern volatile BOOL g_hci_uart_mode;
struct BLE_TIME_SYNC_PARAM{
	u32 ll_sleep_expiry;
	u32 host_time_base;
	u32 delta_time;
	u32 reserve;
};

static ble_hci_trans_rx_cmd_fn *ble_hci_ram_rx_cmd_hs_cb;
static void *ble_hci_ram_rx_cmd_hs_arg;

static ble_hci_trans_rx_cmd_fn *ble_hci_ram_rx_cmd_ll_cb;
static void *ble_hci_ram_rx_cmd_ll_arg;

static ble_hci_trans_rx_acl_fn *ble_hci_ram_rx_acl_hs_cb;
static void *ble_hci_ram_rx_acl_hs_arg;

static ble_hci_trans_rx_acl_fn *ble_hci_ram_rx_acl_ll_cb;
static void *ble_hci_ram_rx_acl_ll_arg;

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


#define HCI_CMD_SHARE_NUM	2
#define HCI_ACL_SHARE_NUM	4

#define HCI_CMD_SHARE_SIZE	256
#define HCI_ACL_SHARE_SIZE	512


struct HCI_CMD_SHARE_PARAM{
	u32 host_cnt;
	u32 ll_cnt;
	u8 hci_cmd[HCI_CMD_SHARE_NUM][HCI_CMD_SHARE_SIZE];
};

struct HCI_ACL_SHARE_PARAM{
	u32 host_cnt;
	u32 ll_cnt;
	u8 hci_acl[HCI_ACL_SHARE_NUM][HCI_ACL_SHARE_SIZE];	
};

struct HCI_CMD_SHARE_PARAM *ble_hci_cmd_h2l_share;
struct HCI_ACL_SHARE_PARAM *ble_hci_acl_h2l_share;

struct HCI_CMD_SHARE_PARAM *ble_hci_cmd_l2h_share;
struct HCI_ACL_SHARE_PARAM *ble_hci_acl_l2h_share;

struct BLE_TIME_SYNC_PARAM *ble_time_sync_share;

extern struct os_mbuf *ble_hci_trans_acl_buf_alloc(void);
extern void ble_hci_trans_buf_free(uint8_t *buf);

#if 0
static struct os_mbuf * ble_ll_mbuf_gen_pkt(uint16_t leading_space)
{
    struct os_mbuf *om;
    int rc;

    om = os_msys_get_pkthdr(0, 0);
    if (om == NULL) {
        return NULL;
    }

    if (om->om_omp->omp_databuf_len < leading_space) {
        rc = os_mbuf_free_chain(om);
        assert(rc == 0);
        return NULL;
    }

    om->om_data += leading_space;

    return om;
}

struct os_mbuf *ble_ll_mbuf_acl_pkt(void)
{
    return ble_ll_mbuf_gen_pkt(BLE_HCI_DATA_HDR_SZ);
}
#endif

void ble_notify_wifi_int(void)
{
	uint32_t val;
	val = HW_READ_REG(0x161000c4);
	val |= BIT(15);
	HW_WRITE_REG(0x161000c4, val);
	//printf("%s:%d\n",__FUNCTION__,__LINE__);
}


void ble_hci_cmd_check(void)
{
	u8 *data;
	int len;
	u8 *cmdbuf;
	
	if((int)(ble_hci_cmd_h2l_share->host_cnt - ble_hci_cmd_h2l_share->ll_cnt) < 0){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);
	}
	
	if((int)(ble_hci_cmd_h2l_share->host_cnt - ble_hci_cmd_h2l_share->ll_cnt) > HCI_CMD_SHARE_NUM){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);		
	}

	//printf("cmd h2l host_cnt:%d, ll_cnt:%d\n", 
	//	ble_hci_cmd_h2l_share->host_cnt, ble_hci_cmd_h2l_share->ll_cnt);

	if(ble_hci_cmd_h2l_share->host_cnt == ble_hci_cmd_h2l_share->ll_cnt){
		return;
	}

	while((int)(ble_hci_cmd_h2l_share->host_cnt - ble_hci_cmd_h2l_share->ll_cnt) > 0){
		data = &ble_hci_cmd_h2l_share->hci_cmd[ble_hci_cmd_h2l_share->ll_cnt%HCI_CMD_SHARE_NUM][0];
		len = data[2] + BLE_HCI_CMD_HDR_LEN;
		if(len > BLE_HCI_TRANS_CMD_SZ){
			assert(0);
		}
		cmdbuf = ble_hci_trans_buf_alloc(BLE_HCI_TRANS_BUF_CMD);
		if(cmdbuf == NULL){
			break;
		}
		memcpy(cmdbuf, data, len);
		assert(ble_hci_ram_rx_cmd_ll_cb != NULL);
		ble_hci_ram_rx_cmd_ll_cb(cmdbuf, ble_hci_ram_rx_cmd_ll_arg);
		ble_hci_cmd_h2l_share->ll_cnt ++;
	}	
}

void ble_hci_acl_check(void)
{
	struct os_mbuf *om;
	uint16_t pktlen;
	u8 *data;
	
	if((int)(ble_hci_acl_h2l_share->host_cnt - ble_hci_acl_h2l_share->ll_cnt) < 0){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);
	}
	
	if((int)(ble_hci_acl_h2l_share->host_cnt - ble_hci_acl_h2l_share->ll_cnt) > HCI_ACL_SHARE_NUM){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);		
	}
	
	//printf("acl h2l ll_cnt:%d, host_cnt:%d\n", 
	//		ble_hci_acl_h2l_share->ll_cnt, ble_hci_acl_h2l_share->host_cnt);

	if(ble_hci_acl_h2l_share->host_cnt == ble_hci_acl_h2l_share->ll_cnt){
		return;
	}	

	while((int)(ble_hci_acl_h2l_share->host_cnt - ble_hci_acl_h2l_share->ll_cnt) > 0){
		data = &ble_hci_acl_h2l_share->hci_acl[ble_hci_acl_h2l_share->ll_cnt%HCI_ACL_SHARE_NUM][0];
		pktlen = get_le16(&data[2]) + BLE_HCI_DATA_HDR_SZ;
		//printf("pktlen:%d, acl h2l data\n", pktlen);
		if(pktlen > MYNEWT_VAL(BLE_ACL_BUF_SIZE)){
			printf("acl tx pktlen too large\n");
			assert(0);
		}
#if 0
		printf("data:");
		for(i=0; i<pktlen; i++){
			printf("%02X ", data[i]);
			if(0 == ((i+1)%16)){
				printf("\n");
				printf("data:");
			}
		}
		printf("\n");
#endif
		om = ble_hci_trans_acl_buf_alloc();
		if(om){
			memcpy(om->om_data, data, pktlen);
			om->om_len = pktlen;
			OS_MBUF_PKTHDR(om)->omp_len = om->om_len;
			assert(ble_hci_ram_rx_acl_ll_cb != NULL);
			ble_hci_ram_rx_acl_ll_cb(om, ble_hci_ram_rx_acl_ll_arg);	
			ble_hci_acl_h2l_share->ll_cnt ++;
		}else{
			printf("ble acl buff has no mem\n");
			break;
		}


	}
}


void ble_hci_handle_isr(void)
{
	uint32_t val;
	
	val = HW_READ_REG(0x161000cc);
	
	if(val & BIT(15)){
		ble_hci_cmd_check();
		ble_hci_acl_check();
		
		val = HW_READ_REG(0x161000d0);
		val |= BIT(15);
		HW_WRITE_REG(0x161000d0, val);
	}
}

static int ble_l2h_evt_cb(uint8_t *hci_ev, void *arg)
{
	os_sr_t sr;
	int i, len;
	u8 *data;

	if((int)(ble_hci_cmd_l2h_share->ll_cnt - ble_hci_cmd_l2h_share->host_cnt) < 0){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);
	}	
	
	if((int)(ble_hci_cmd_l2h_share->ll_cnt - ble_hci_cmd_l2h_share->host_cnt) > HCI_CMD_SHARE_NUM){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);		
	}

	//printf("evt l2h ll_cnt:%d,host_cnt:%d\n", 
	//	ble_hci_cmd_l2h_share->ll_cnt, ble_hci_cmd_l2h_share->host_cnt);
	if((int)(ble_hci_cmd_l2h_share->ll_cnt - ble_hci_cmd_l2h_share->host_cnt) == HCI_CMD_SHARE_NUM){
		ble_hci_trans_buf_free(hci_ev);
		return;
	}

	data = &ble_hci_cmd_l2h_share->hci_cmd[ble_hci_cmd_l2h_share->ll_cnt%HCI_CMD_SHARE_NUM][0];
	OS_ENTER_CRITICAL(sr);
	memset(data, 0, HCI_CMD_SHARE_SIZE);
	len = hci_ev[1] + 2;
	if(len > HCI_CMD_SHARE_SIZE){
		assert(0);
	}
	memcpy(data, hci_ev, len);
#if 0
	printf("ll evt send:");
	for(i=0; i<len; i++){
		printf("%02X ", data[i]);
	}
	printf("\n");
#endif
	ble_hci_trans_buf_free(hci_ev);
	ble_hci_cmd_l2h_share->ll_cnt ++;
	ble_notify_wifi_int();
	OS_EXIT_CRITICAL(sr);

    return 0;
}

static int ble_l2h_acl_cb(struct os_mbuf *om, void *arg)
{
	os_sr_t sr;
	uint16_t rem_tx_len;
	u8 *data;
	struct os_mbuf *om_next;
	int i;

	if((int)(ble_hci_acl_l2h_share->ll_cnt - ble_hci_acl_l2h_share->host_cnt) < 0){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);
	}	
	
	if((int)(ble_hci_acl_l2h_share->ll_cnt - ble_hci_acl_l2h_share->host_cnt) > HCI_ACL_SHARE_NUM){
		printf("ble_hci_cmd_share cnt err\n");
		assert(0);		
	}

	if((int)(ble_hci_acl_l2h_share->ll_cnt - ble_hci_acl_l2h_share->host_cnt) == HCI_ACL_SHARE_NUM){
		os_mbuf_free_chain(om);
		return;
	}

	data = &ble_hci_acl_l2h_share->hci_acl[ble_hci_acl_l2h_share->ll_cnt%HCI_ACL_SHARE_NUM][0];
	om = os_mbuf_trim_front(om);
	rem_tx_len = OS_MBUF_PKTLEN(om);
	//printf("acl l2h, rem_tx_len:%d\n", rem_tx_len);
	if(rem_tx_len > HCI_ACL_SHARE_SIZE){
		assert(0);
	}

	OS_ENTER_CRITICAL(sr);
	while(1){
		if(om->om_len >= rem_tx_len){
			memcpy(data, om->om_data, rem_tx_len);
			os_mbuf_free_chain(om);
			break;
		}else{
			memcpy(data, om->om_data, om->om_len);
			data += om->om_len;
			rem_tx_len -= om->om_len;			
			om_next = SLIST_NEXT(om, om_next);
			os_mbuf_free(om);
			om = om_next;
			if (om == NULL){
				assert(0);
			}
		}
	}
	
#if 0
	data = &ble_hci_acl_l2h_share->hci_acl[ble_hci_acl_l2h_share->ll_cnt%HCI_ACL_SHARE_NUM][0];
	rem_tx_len = get_le16(&data[2]) + BLE_HCI_DATA_HDR_SZ;

	printf("acl l2h rem_tx_len:%d\n", rem_tx_len);
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

	ble_hci_acl_l2h_share->ll_cnt ++;
	ble_notify_wifi_int();
	OS_EXIT_CRITICAL(sr);		
	
    return 0;
}


void
ble_hci_trans_cfg_hs(ble_hci_trans_rx_cmd_fn *cmd_cb,
                     void *cmd_arg,
                     ble_hci_trans_rx_acl_fn *acl_cb,
                     void *acl_arg)
{
    ble_hci_ram_rx_cmd_hs_cb = cmd_cb;
    ble_hci_ram_rx_cmd_hs_arg = cmd_arg;
    ble_hci_ram_rx_acl_hs_cb = acl_cb;
    ble_hci_ram_rx_acl_hs_arg = acl_arg;
}

void
ble_hci_trans_cfg_ll(ble_hci_trans_rx_cmd_fn *cmd_cb,
                     void *cmd_arg,
                     ble_hci_trans_rx_acl_fn *acl_cb,
                     void *acl_arg)
{
    ble_hci_ram_rx_cmd_ll_cb = cmd_cb;
    ble_hci_ram_rx_cmd_ll_arg = cmd_arg;
    ble_hci_ram_rx_acl_ll_cb = acl_cb;
    ble_hci_ram_rx_acl_ll_arg = acl_arg;
	//ble_hci_trans_cfg_ll_uart(cmd_cb, cmd_arg, acl_cb, acl_arg);
}

int
ble_hci_trans_hs_cmd_tx(uint8_t *cmd)
{
    return 0;
}

int
ble_hci_trans_ll_evt_tx(uint8_t *hci_ev)
{
    int rc;

	if(0){//(g_hci_uart_mode){
//		rc = ble_hci_trans_ll_evt_tx_uart(hci_ev);
	}else{
		assert(ble_hci_ram_rx_cmd_hs_cb != NULL);
		rc = ble_hci_ram_rx_cmd_hs_cb(hci_ev, ble_hci_ram_rx_cmd_hs_arg);
	}
	
    return rc;
}

int
ble_hci_trans_hs_acl_tx(struct os_mbuf *om)
{
    return 0;
}

int
ble_hci_trans_ll_acl_tx(struct os_mbuf *om)
{
    int rc;
	
	if(0){//(g_hci_uart_mode){
//		rc = ble_hci_trans_ll_acl_tx_uart(om);
	}else{
		assert(ble_hci_ram_rx_acl_hs_cb != NULL);
		rc = ble_hci_ram_rx_acl_hs_cb(om, ble_hci_ram_rx_acl_hs_arg);
	}

    return rc;
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


void
ble_hci_trans_buf_free(uint8_t *buf)
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

/**
 * Unsupported; the RAM transport does not have a dedicated ACL data packet
 * pool.
 */
int
ble_hci_trans_set_acl_free_cb(os_mempool_put_fn *cb, void *arg)
{
	return BLE_ERR_UNSUPPORTED;
}

int
ble_hci_trans_reset(void)
{
    /* No work to do.  All allocated buffers are owned by the host or
     * controller, and they will get freed by their owners.
     */
	//if(g_hci_uart_mode){
	//	ble_hci_trans_reset_uart();
	//}
	
    return 0;
}

void
ble_hci_ram_init(void)
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
	ble_hci_acl_h2l_share = (u8 *)ble_hci_cmd_l2h_share + sizeof(struct HCI_CMD_SHARE_PARAM);
	ble_hci_acl_l2h_share = (u8 *)ble_hci_acl_h2l_share + sizeof(struct HCI_ACL_SHARE_PARAM);
	ble_time_sync_share = (u8 *)ble_hci_acl_l2h_share + sizeof(struct HCI_ACL_SHARE_PARAM);

	memset((u8 *)ble_time_sync_share, 0, sizeof(struct BLE_TIME_SYNC_PARAM));

	ble_hci_trans_cfg_hs(ble_l2h_evt_cb, NULL, ble_l2h_acl_cb, NULL);

	IRQ_RegisterHandler(DEV_INT_NUM_WIFI_CPU, ble_hci_handle_isr);
	
	
}
