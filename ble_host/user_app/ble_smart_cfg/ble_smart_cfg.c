
#include <assert.h>


#include <linux/fs.h>
//#include <linux/kernel.h>
//#include <linux/module.h>
#include <linux/string.h>




#include "atbm_hal.h"
#include "services/gap/ble_svc_gap.h"
#include "services/dis/ble_svc_dis.h"
#include "services/gatt/ble_svc_gatt.h"

#include "host/ble_gatt.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/ble_hci_trans.h"
#include "ble_smart_cfg.h"
#include "atbm_os_api.h"


#define BLE_USER_DATA_SVC_UUID16			0x181C
#define BLE_ATBM_SMART_DATA					0x2A8A
#define BLE_ATBM_SMART_NOTIFY				0x2A90

#define BLE_ATBM_GAP_SVR					0xBBB0
#define BLE_ATBM_GAP_SVR1					0xBBB1


#define BLE_GATT_THOUGHPUT_TEST				0


static const char *device_name = "AltoBeam";
static const char *ble_version = "nimble4.2";
static const char *firmware_info = "cronus_1.0";
static const char *software_info = "V1.0.0";
static const char* wifi_conn_ok ="ok";
static const char* wifi_conn_no ="no";
static const char* wifi_conn_wait ="wt";
//static int ble_parse_smt_adv_data(struct ioctl_ble_adv_data *adv_data);
extern sem_t sem_sock_sync;
extern char cmd_line[1600];
static void
print_bytes(const uint8_t *bytes, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        MODLOG_DFLT(INFO, "%s0x%02x", i != 0 ? ":" : "", bytes[i]);
    }
}

static void
print_addr(const void *addr)
{
    const uint8_t *u8p;

    u8p = addr;
    MODLOG_DFLT(INFO, "%02x:%02x:%02x:%02x:%02x:%02x",
                u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
}


void get_ble_addr(uint8_t* ble_addr)
{
	uint8_t *addr;
	//ble_hs_id_addr(BLE_ADDR_RANDOM,&addr,NULL);
	//iot_printf("addr %02hhx:%B2hhx:%B2hhx:%02hhx:%2hhx:%02hhxin",addr[0], addr[1], addr[2], addr[3], addr[4],addr[5]);
	
	ble_hs_id_addr(BLE_ADDR_PUBLIC,&addr, NULL);
	//iot_printf("pub addr %02hhx:%02hx:%2hx:%2hhx:%02hhx:%02hhx\n",addr[5],addr[4],addr[3],addr[2],addr[1],addr[0]);
	memcpy(ble_addr, addr, 6);
}
static uint8_t blesmt_addr_type;
uint16_t blesmt_notify_handle;
uint8_t atbm_ble_status=0;


struct ble_smart_cfg_t  ble_smart_info;
int smart_cfg_start = 0;
static struct ble_npl_sem hci_smt_sem;
static uint16_t smt_conn_handle;
pAtbm_thread_t ble_smt_thread = NULL;
extern uint16_t ble_svc_gatt_changed_val_handle;
extern bool atbm_ble_is_quit;

static void blesmt_advertise(void);
static SRAM_CODE void
print_uuid(const ble_uuid_t *uuid)
{
    char buf[BLE_UUID_STR_LEN];

    ble_uuid_to_str(uuid, buf);

    iot_printf("%s", buf);
}


static int gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
                   void *dst, uint16_t *len)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

int gatt_svr_chr_notify(uint16_t conn_handle, uint16_t chr_val_handle, const char *data, int len)
{
	int rc;
	struct os_mbuf *om = NULL;

	om = ble_hs_mbuf_from_flat((void *)data, (uint16_t)len);
	//assert(om != NULL);
	if(om == NULL)
		return -1;
	rc = ble_gattc_notify_custom(conn_handle, chr_val_handle, om);
	//assert(rc == 0);
	if(rc)
		return -2;
	return 0;
}
static int gatt_svr_chr_indicate(uint16_t conn_handle, uint16_t chr_val_handle, const char *data, int len)
{
	int rc;
	struct os_mbuf *om = NULL;

	om = ble_hs_mbuf_from_flat((void *)data, (uint16_t)len);
	//assert(om != NULL);
	if(om == NULL)
		return -1;

	rc = ble_gattc_indicate_custom(conn_handle, chr_val_handle, om);
	if(rc)
		return -2;

	return 0;
}

static int ble_svc_gap_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    assert(uuid16 != 0);

    switch (uuid16) {
    case BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, device_name, strlen(device_name));
        } else {
            assert(0);
        }
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
	return 0;
}

static int ble_svc_dis_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid    = ble_uuid_u16(ctxt->chr->uuid);
    const char *info = NULL;
	int rc;

	switch(uuid) {
	case BLE_SVC_DIS_CHR_UUID16_MANUFACTURER_NAME:
		info = ble_version;
		break;
	case BLE_SVC_DIS_CHR_UUID16_FIRMWARE_REVISION:
		info = firmware_info;
		break;
	case BLE_SVC_DIS_CHR_UUID16_SOFTWARE_REVISION:
		info = software_info;
		break;
    default:
		assert(0);
		return BLE_ATT_ERR_UNLIKELY;
	}
    if (info != NULL) {
		rc = os_mbuf_append(ctxt->om, info, strlen(info));
		return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
	return 0;
}
#define BUFFER_SIZE 4096




uint16_t ble_conn_handle;
extern ble_att_set_preferred_mtu(uint16_t mtu);
extern bool g_is_quit;


#if BLE_GATT_THOUGHPUT_TEST
u32 recv_byte_cnt = 0;
u32 pre_byte_cnt = 0;
u32 start_time = 0;
s32 diff_time = 0;
#endif


static int ble_svc_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	int rc;
    uint16_t uuid;
	uint8_t data[255] = {0};
	uint16_t len = 0;
	uint8_t i=0;

	uuid = ble_uuid_u16(ctxt->chr->uuid);
	if(uuid == BLE_ATBM_SMART_DATA){
		rc = gatt_svr_chr_write(ctxt->om, 0, 255, data, &len);
		if(rc){
			return rc;
		}
		iot_printf("Len=%d step %d\n", len, ble_smart_info.step);
		iot_printf("att=");
		for(i=0;i<len;i++)
		{
			iot_printf("0x%x ", data[i]);
		}
		iot_printf("\n");
		//atbm_ble_is_quit=1;
		#if ATBM_BLE_SMART_APP
		if(((memcmp(data, "recv ok", strlen("recv ok"))) == 0) && (ble_smart_info.step == 4)){
			ble_smart_info.step = BLE_SMART_CFG_STATUS_END;
			ble_npl_sem_release(&hci_smt_sem);
		}

		if(ble_smart_info.step == BLE_SMART_CFG_STATUS_END){
			return 0;
		}
		
		switch(ble_smart_info.step)
		{
			case 0:
				if((memcmp(data,"ssid",strlen("ssid"))) == 0){
					if(len == 6){
						ble_smart_info.ssid_len = (data[5] - '0');
					}
					else if(len == 7){
						ble_smart_info.ssid_len = 10*(data[5] - '0') + (data[6] - '0');
					}

					iot_printf("ble ssid_len = %d\n",ble_smart_info.ssid_len);
					if(ble_smart_info.ssid_len > 32){					
						iot_printf("ble ssid_len too big error\n");
						ble_smart_info.step = BLE_SMART_CFG_STATUS_SSID_LEN_ERR;
						ble_npl_sem_release(&hci_smt_sem);
						break;
					}
					else {
						ble_smart_info.step =1;
					}
				}				
				break;
				
			case 1:
				if((ble_smart_info.ssid_cnt + len <= ble_smart_info.ssid_len)){
					memcpy(&ble_smart_info.ssid[ble_smart_info.ssid_cnt], data, len);
					ble_smart_info.ssid_cnt += len;
				}
				else{
					ble_smart_info.step = BLE_SMART_CFG_STATUS_SSID_ERR;
					ble_npl_sem_release(&hci_smt_sem);
					break;
				}
				if(ble_smart_info.ssid_cnt == ble_smart_info.ssid_len){
					ble_smart_info.step =2;
					iot_printf("ble ssid = %s\n", ble_smart_info.ssid);
				}				
				break;

			case 2:
				if((memcmp(data, "pwd", strlen("pwd"))) == 0){
					if(len == 5){
						ble_smart_info.pwd_len = (data[4] - '0');
					}
					else if(len == 6){
						ble_smart_info.pwd_len = 10*(data[4] - '0') + (data[5] - '0');
					}

					iot_printf("ble pwd_len = %d\n",ble_smart_info.pwd_len);

					if(ble_smart_info.pwd_len > 64){					
						iot_printf("ble pwd_len too big error\n");
						ble_smart_info.step = BLE_SMART_CFG_STATUS_PWD_LEN_ERR;
						ble_npl_sem_release(&hci_smt_sem);
					}
					else if((ble_smart_info.pwd_len < 5)&&(ble_smart_info.pwd_len > 0)){					
						iot_printf("ble pwd_len too short error\n");
						ble_smart_info.step = BLE_SMART_CFG_STATUS_PWD_LEN_ERR;
						ble_npl_sem_release(&hci_smt_sem);
					}
					else if(ble_smart_info.pwd_len == 0){
						iot_printf("ble pwd_len is empty\n");
						ble_smart_info.step =4;
					}
					else {
						ble_smart_info.step =3;
					}
				}				
				break;
				
			case 3:
				if((ble_smart_info.pwd_cnt + len <= ble_smart_info.pwd_len)){
					memcpy(&ble_smart_info.pwd[ble_smart_info.pwd_cnt], data, len);
					ble_smart_info.pwd_cnt += len;
				}
				else{
					ble_smart_info.step = BLE_SMART_CFG_STATUS_PWD_ERR;
					ble_npl_sem_release(&hci_smt_sem);
					break;
				}
				if(ble_smart_info.pwd_cnt == ble_smart_info.pwd_len){
					ble_smart_info.step =4;
					iot_printf("ble pwd = %s\n", ble_smart_info.ssid);
				}				
				break;
				
			default:
				break;
		}

		if(ble_smart_info.step == 5){
			iot_printf("connection, notify success\n");
			gatt_svr_chr_notify(conn_handle, blesmt_notify_handle, wifi_conn_ok, strlen(wifi_conn_ok));
		}
		else if(ble_smart_info.step == BLE_SMART_CFG_SSID_PWD_TRANS_END){   //wait
			iot_printf("connection, notify trans done\n");
			ble_npl_sem_release(&hci_smt_sem);
			//gatt_svr_chr_notify(conn_handle, blesmt_notify_handle, wifi_conn_ok, strlen(wifi_conn_ok));
		}
		else if(ble_smart_info.step == 4){
			iot_printf("connection, notify success\n");
			//ble_smart_info.step = BLE_SMART_CFG_SSID_PWD_TRANS_END;
			ble_npl_sem_release(&hci_smt_sem);
			//gatt_svr_chr_notify(conn_handle, blesmt_notify_handle, wifi_conn_ok, strlen(wifi_conn_ok));
		}
		else if(ble_smart_info.step >= BLE_SMART_CFG_STATUS_BUSY){
				iot_printf("connection, notify wait failed\n");
			gatt_svr_chr_notify(conn_handle, blesmt_notify_handle, wifi_conn_wait, strlen(wifi_conn_wait));
		}
		#endif
	}
	
#if BLE_GATT_THOUGHPUT_TEST
	else if(BLE_ATBM_SMART_NOTIFY){
		rc = gatt_svr_chr_write(ctxt->om, 0, 255, data, &len);
		if(rc){
			return rc;
		}

		if(ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR){
			recv_byte_cnt += len;
			diff_time = (s32)(ble_npl_time_get() - start_time);
			if(diff_time > 1000){
				start_time = ble_npl_time_get();
				iot_printf("Time[%d], Rx[%d]bytes, [%d]Kbps/sec\n", 
							diff_time, (recv_byte_cnt-pre_byte_cnt),(recv_byte_cnt-pre_byte_cnt)*8/diff_time);
				pre_byte_cnt = recv_byte_cnt;
			}
		}
	}
#endif
	
	return 0;
}


static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
#if  0
    {
        /* Service: GAP */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_GAP_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            /* Characteristic: Heart-rate measurement */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_GAP_CHR_UUID16_DEVICE_NAME),
            .access_cb = ble_svc_gap_access,
            .flags = BLE_GATT_CHR_F_READ,
        },  {
            0, /* No more characteristics in this service */
        }, }
    },
#endif
    {
        /* Service: Device Information */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(BLE_SVC_DIS_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            /* Characteristic: * Manufacturer name */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_DIS_CHR_UUID16_MANUFACTURER_NAME),
            .access_cb = ble_svc_dis_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /* Characteristic: Model number string */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_DIS_CHR_UUID16_FIRMWARE_REVISION),
            .access_cb = ble_svc_dis_access,
            .flags = BLE_GATT_CHR_F_READ,
        },{
            /* Characteristic: Model number string */
            .uuid = BLE_UUID16_DECLARE(BLE_SVC_DIS_CHR_UUID16_SOFTWARE_REVISION),
            .access_cb = ble_svc_dis_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0, /* No more characteristics in this service */
        }, }
    },
    
	{
		/* Service: GATT */
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = BLE_UUID16_DECLARE(BLE_USER_DATA_SVC_UUID16),
		.characteristics = (struct ble_gatt_chr_def[]) { {
			.uuid = BLE_UUID16_DECLARE(BLE_ATBM_SMART_DATA),
			.access_cb = ble_svc_gatt_access,
			.flags = BLE_GATT_CHR_F_WRITE,
		},{
			.uuid = BLE_UUID16_DECLARE(BLE_ATBM_SMART_NOTIFY),
			.access_cb = ble_svc_gatt_access,
			.val_handle = &blesmt_notify_handle,
			.flags = BLE_GATT_CHR_F_NOTIFY 
#if BLE_GATT_THOUGHPUT_TEST
					| BLE_GATT_CHR_F_WRITE_NO_RSP
#endif
					,
		}, {
			0, /* No more characteristics in this service */
		}, }
	},

    {
        0, /* No more services */
    },
};
SRAM_CODE void static
btshell_print_adv_fields(const struct ble_hs_adv_fields *fields)
{
    uint8_t *u8p;
    int i;

    if (fields->flags != 0) {
        iot_printf("    flags=0x%02x:\n", fields->flags);

        if (!(fields->flags & BLE_HS_ADV_F_DISC_LTD) &&
                !(fields->flags & BLE_HS_ADV_F_DISC_GEN)) {
                iot_printf("        Non-discoverable mode\n");
        }

        if (fields->flags & BLE_HS_ADV_F_DISC_LTD) {
                iot_printf("        Limited discoverable mode\n");
        }

        if (fields->flags & BLE_HS_ADV_F_DISC_GEN) {
                iot_printf("        General discoverable mode\n");
        }

        if (fields->flags & BLE_HS_ADV_F_BREDR_UNSUP) {
                iot_printf("        BR/EDR not supported\n");
        }
    }

    if (fields->uuids16 != NULL) {
        iot_printf("    uuids16(%scomplete)=",
                       fields->uuids16_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids16; i++) {
            print_uuid(&fields->uuids16[i].u);
            iot_printf(" ");
        }
        iot_printf("\n");
    }

    if (fields->uuids32 != NULL) {
        iot_printf("    uuids32(%scomplete)=",
                       fields->uuids32_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids32; i++) {
            print_uuid(&fields->uuids32[i].u);
            iot_printf(" ");
        }
        iot_printf("\n");
    }

    if (fields->uuids128 != NULL) {
        iot_printf("    uuids128(%scomplete)=",
                       fields->uuids128_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids128; i++) {
            print_uuid(&fields->uuids128[i].u);
            iot_printf(" ");
        }
        iot_printf("\n");
    }

    if (fields->name != NULL) {
		int i=0;
        iot_printf("    name(%scomplete)=",
                       fields->name_is_complete ? "" : "in");
        for(i=0;i<fields->name_len;i++){
			//putchar(((char *)fields->name)[i]);
			iot_printf("%c", ((char *)fields->name)[i]);
        }
		
        iot_printf("\n");
    }

    if (fields->tx_pwr_lvl_is_present) {
        iot_printf("    tx_pwr_lvl=%d\n", fields->tx_pwr_lvl);
    }

    if (fields->slave_itvl_range != NULL) {
        iot_printf("    slave_itvl_range=");
        print_bytes(fields->slave_itvl_range,
                            BLE_HS_ADV_SLAVE_ITVL_RANGE_LEN);
        iot_printf("\n");
    }

    if (fields->svc_data_uuid16 != NULL) {
        iot_printf("    svc_data_uuid16=");
        print_bytes(fields->svc_data_uuid16,
                            fields->svc_data_uuid16_len);
        iot_printf("\n");
    }

    if (fields->public_tgt_addr != NULL) {
        iot_printf("    public_tgt_addr=");
        u8p = fields->public_tgt_addr;
        for (i = 0; i < fields->num_public_tgt_addrs; i++) {
            print_addr(u8p);
            u8p += BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN;
        }
        iot_printf("\n");
    }

    if (fields->appearance_is_present) {
        iot_printf("    appearance=0x%04x\n", fields->appearance);
    }

    if (fields->adv_itvl_is_present) {
        iot_printf("    adv_itvl=0x%04x\n", fields->adv_itvl);
    }

    if (fields->svc_data_uuid32 != NULL) {
        iot_printf("    svc_data_uuid32=");
        print_bytes(fields->svc_data_uuid32,
                             fields->svc_data_uuid32_len);
        iot_printf("\n");
    }

    if (fields->svc_data_uuid128 != NULL) {
        iot_printf("    svc_data_uuid128=");
        print_bytes(fields->svc_data_uuid128,
                            fields->svc_data_uuid128_len);
        iot_printf("\n");
    }

    if (fields->uri != NULL) {
        iot_printf("    uri=");
        print_bytes(fields->uri, fields->uri_len);
        iot_printf("\n");
    }

    if (fields->mfg_data != NULL) {
        iot_printf("    mfg_data=");
        print_bytes(fields->mfg_data, fields->mfg_data_len);
        iot_printf("\n");
    }
}

extern int ble_att_set_preferred_mtu(uint16_t mtu);
int
ble_slave_gap_event(struct ble_gap_event *event, void *arg)
{
	struct ble_gap_conn_desc desc;
	struct ble_sm_io pkey;
	/* Connection handle */
	//uint16_t conn_handle=1;
	struct ble_gap_upd_params params;
	params.itvl_min=7;////Range: 0x0006 to 0x0C80 Time = N * 1.25 msec	      1*1.25=7.5ms
	params.itvl_max=8;
	params.latency=0;
	params.supervision_timeout=300;//????3?¡§o?¨¤¡§o?¨¤??Supervision timeout for the LE Link.Range: 0x000A to 0x0C80Time = connTimeout* 10 msecTime Range: 100 msec to 32 seconds
	int conn_idx;
	int rc;	
	uint8_t i;
	struct ble_hs_adv_fields fields;
	uint8_t ble_name[30];
	iot_printf("ble_slave_gap_event->type=%d\n",event->type);
	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
		iot_printf("connection %s; status=%d ",
		               event->connect.status == 0 ? "established" : "failed",
		               event->connect.status);
		if (event->connect.status == 0) {
			atbm_ble_status=ATBM_BLE_STATUS_CONNECT;
			ble_att_set_preferred_mtu(256);
			rc = ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);
//			ble_gap_update_params(event->connect.conn_handle, &params);
		    //rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
		    //assert(rc == 0);
		    //print_conn_desc(&desc);
		    //btshell_conn_add(&desc);
		    iot_printf("connection, handle=%d\n",event->connect.conn_handle);
		    ble_conn_handle = event->connect.conn_handle;
		}
		else {
			iot_printf("Connection failed; resume advertising\n");
			/* Connection failed; resume advertising */
			blesmt_advertise();
		}
		return 0;
		
	case BLE_GAP_EVENT_DISCONNECT:
		atbm_ble_status=ATBM_BLE_STATUS_IDLE;
	    iot_printf("disconnect; reason=%d \n", event->disconnect.reason);
	    //print_conn_desc(&event->disconnect.conn);
	    if(!atbm_ble_is_quit)
	    	blesmt_advertise();
	    //conn_idx = btshell_conn_find_idx(event->disconnect.conn.conn_handle);
	    if (conn_idx != -1) {
	       // btshell_conn_delete_idx(conn_idx);
	    }

	    return 0;
#if (MYNEWT_VAL_BLE_EXT_ADV)
	case BLE_GAP_EVENT_EXT_DISC:
	    btshell_decode_event_type(&event->ext_disc, arg);
	    return 0;
#endif
	case BLE_GAP_EVENT_DISC:
		#if 1
			//if (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
				//return 0;
			//}
			
			ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
			if(fields.name && fields.name_len){

				memcpy(ble_name, fields.name, fields.name_len);
				ble_name[fields.name_len]=0;
				iot_printf("\n********************ble scan adv data*****************************\n");
				iot_printf("ble_name:%s\n",ble_name);
				//if(0 == memcmp(ble_scan_show_name, fields.name, fields.name_len))
				{
					iot_printf("received advertisement; event_type=%d rssi=%d "
								   "addr_type=%d addr=", event->disc.event_type,
								   event->disc.rssi, event->disc.addr.type);
					print_addr(event->disc.addr.val);
					iot_printf(" fields:\n");
					btshell_print_adv_fields(&fields);
					iot_printf("\n");
				}
				iot_printf("\n********************end*****************************\n");
			}	
	        /*
	         * There is no adv data to print in case of connectable
	         * directed advertising
	         */
	        if (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
	                iot_printf("\nConnectable directed advertising event\n");
	                return 0;
	        }
			
			//ble_parse_smt_adv_data(&ble_rx_adv_data);
	        //btshell_decode_adv_data(event->disc.data, event->disc.length_data, arg);
	#endif
	    return 0;

	case BLE_GAP_EVENT_CONN_UPDATE:
	    iot_printf("connection updated; status=%d ",
	                   event->conn_update.status);
	    rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
	    assert(rc == 0);
	    //print_conn_desc(&desc);
		iot_printf(" conn_itvl=%d conn_latency=%d supervision_timeout=%d "
					   "encrypted=%d authenticated=%d bonded=%d\n",
					   desc.conn_itvl, desc.conn_latency,
					   desc.supervision_timeout,
					   desc.sec_state.encrypted,
					   desc.sec_state.authenticated,
					   desc.sec_state.bonded);
	    return 0;

	case BLE_GAP_EVENT_CONN_UPDATE_REQ:
	    iot_printf("connection update request\n");
	    *event->conn_update_req.self_params =
	        *event->conn_update_req.peer_params;
	    return 0;

	case BLE_GAP_EVENT_PASSKEY_ACTION:
	    iot_printf("passkey action event; action=%d",
	                   event->passkey.params.action);
	    if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
	        iot_printf(" numcmp=%lu",
	                       (unsigned long)event->passkey.params.numcmp);
	    }else{
	        if(event->passkey.params.action == BLE_SM_IOACT_INPUT){
	            pkey.action = BLE_SM_IOACT_INPUT;
				//pkey.passkey =123456;//PTS¡§|?€¡§¡§|?¡§¡§??¡ã??-?23456??¡ê¡è¡§|€?¡§¡§??¡§|a?¡§¡§?£¤??€?
				//ble_sm_inject_io(event->passkey.conn_handle, &pkey);
			}

		}
	    iot_printf("\n");
	    return 0;


	case BLE_GAP_EVENT_DISC_COMPLETE:
	    iot_printf("discovery complete; reason=%d\n",
	                   event->disc_complete.reason);
	    return 0;

	case BLE_GAP_EVENT_ADV_COMPLETE:
#if (MYNEWT_VAL_BLE_EXT_ADV)
	    iot_printf("advertise complete; reason=%d, instance=%u, handle=%d\n",
	                   event->adv_complete.reason, event->adv_complete.instance,
	                   event->adv_complete.conn_handle);

	    ext_adv_restart[event->adv_complete.instance].conn_handle =
	        event->adv_complete.conn_handle;
#else
	    iot_printf("advertise complete; reason=%d\n",
	                   event->adv_complete.reason);
#endif
	    return 0;

	case BLE_GAP_EVENT_ENC_CHANGE:
	    iot_printf("encryption change event; status=%d ",
	                   event->enc_change.status);
	    rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
	    assert(rc == 0);
	    //print_conn_desc(&desc);
	    return 0;

	case BLE_GAP_EVENT_NOTIFY_RX:
	    iot_printf("notification rx event; attr_handle=%d indication=%d "
	                   "len=%d data=",
	                   event->notify_rx.attr_handle,
	                   event->notify_rx.indication,
	                   OS_MBUF_PKTLEN(event->notify_rx.om));

	   // print_mbuf(event->notify_rx.om);
	    iot_printf("\n");
	    return 0;

	case BLE_GAP_EVENT_NOTIFY_TX:
	    iot_printf("notification tx event; status=%d attr_handle=%d "
	                   "indication=%d\n",
	                   event->notify_tx.status,
	                   event->notify_tx.attr_handle,
	                   event->notify_tx.indication);
	    return 0;

	case BLE_GAP_EVENT_SUBSCRIBE:
	    iot_printf("subscribe event; conn_handle=%d attr_handle=%d "
	                   "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
	                   event->subscribe.conn_handle,
	                   event->subscribe.attr_handle,
	                   event->subscribe.reason,
	                   event->subscribe.prev_notify,
	                   event->subscribe.cur_notify,
	                   event->subscribe.prev_indicate,
	                   event->subscribe.cur_indicate);
		#if 0
	    if(ble_svc_gatt_changed_val_handle==event->subscribe.attr_handle)
	    {
	    		//log ?aindicate¡§¡§?¡ì?Tsubscribe event; conn_handle=1 attr_handle=10 reason=1 prevn=0 curn=0 previ=0 curi=1
	    		//log1?indicate¡§¡§?¡ì?Tsubscribe event; conn_handle=1 attr_handle=10 reason=1 prevn=0 curn=0 previ=1 curi=0
		    if((event->subscribe.prev_indicate==0)&&(event->subscribe.cur_indicate==1))
		    {	   
			    char* ble_indicate ="send indicate";
			     iot_printf("ble_svc_gatt_changed_val_handle=%d\n",
			                   ble_svc_gatt_changed_val_handle);
			    rc=gatt_svr_chr_indicate(ble_conn_handle, ble_svc_gatt_changed_val_handle, ble_indicate, strlen(ble_indicate));
			    assert(rc == 0);
		    }
	    }
		#endif
	    return 0;

	case BLE_GAP_EVENT_MTU:
	    iot_printf("mtu update event; conn_handle=%d cid=%d mtu=%d\n",
	                   event->mtu.conn_handle,
	                   event->mtu.channel_id,
	                   event->mtu.value);
	    return 0;

	case BLE_GAP_EVENT_IDENTITY_RESOLVED:
	    iot_printf("identity resolved ");
	    rc = ble_gap_conn_find(event->identity_resolved.conn_handle, &desc);
	    assert(rc == 0);
	    //print_conn_desc(&desc);
	    return 0;

	case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
	    iot_printf("PHY update complete; status=%d, conn_handle=%d "
	                   " tx_phy=%d, rx_phy=%d\n",
	                   event->phy_updated.status,
	                   event->phy_updated.conn_handle,
	                   event->phy_updated.tx_phy,
	                   event->phy_updated.rx_phy);
	    return 0;

	case BLE_GAP_EVENT_REPEAT_PAIRING:
	    /* We already have a bond with the peer, but it is attempting to
	     * establish a new secure link.  This app sacrifices security for
	     * convenience: just throw away the old bond and accept the new link.
	     */

	    /* Delete the old bond. */
	    rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
	    assert(rc == 0);
	    ble_store_util_delete_peer(&desc.peer_id_addr);

	    /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
	     * continue with the pairing operation.
	     */
	    return BLE_GAP_REPEAT_PAIRING_RETRY;

	default:
	    return 0;
	}
}

static void blesmt_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
	ble_uuid16_t uuids16[2];
	uint8_t mfg_data[3] = {0xAB, 0xAB, 0xA0};
	int rc;

    memset(&fields, 0, sizeof(fields));	
	
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

	uuids16[0].u.type = BLE_UUID_TYPE_16;
	uuids16[0].value = BLE_ATBM_GAP_SVR;
	uuids16[1].u.type = BLE_UUID_TYPE_16;
	uuids16[1].value = BLE_ATBM_GAP_SVR1;
	fields.uuids16 = &uuids16[0];
	fields.num_uuids16 = 2;
	fields.uuids16_is_complete = 1;

	fields.mfg_data = mfg_data;
	fields.mfg_data_len = 3;
	
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        iot_printf("error setting advertisement data; rc=%d\n", rc);
        return;
    }
	//¡§¡ã???¡§o?¡§|?¡ì?¡§¡§??¡§?|??¡§1????
	struct ble_hs_adv_fields adv_fields;
	ble_uuid16_t uuids16_rsp[2];
	uint8_t mfg_rsp_data[3] = {0xff, 0xcc, 0xdd};
	memset(&adv_fields, 0, sizeof(adv_fields));	

	adv_fields.name = (uint8_t *)device_name;
	adv_fields.name_len = strlen(device_name);
	adv_fields.name_is_complete = 1;

	uuids16_rsp[0].u.type = BLE_UUID_TYPE_16;
	uuids16_rsp[0].value = 0xccc0;
	uuids16_rsp[1].u.type = BLE_UUID_TYPE_16;
	uuids16_rsp[1].value = 0xddd0;
	adv_fields.uuids16 = &uuids16_rsp[0];
	adv_fields.num_uuids16 = 2;
	adv_fields.uuids16_is_complete = 1;
	adv_fields.mfg_data = mfg_data;
	adv_fields.mfg_data_len = 3;
	rc=ble_gap_adv_rsp_set_fields(&adv_fields);
	 if (rc != 0) {
	    iot_printf("error setting advertisement rsp data; rc=%d\n", rc);
	    return;
	}
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

	//adv_params.itvl_min= 60;//?¨¢?D?1????????¨º?0 ?¨¤¡§a¡§o?¡§o1¡§????¡§¡§??|¨¬?¨º?|¨¬¡ê¡è???¨ºo0.625ms */
	//adv_params.itvl_max   = 70;//?¨¢???¡§?1????????¨º?0 ?¨¤¡§a¡§o?¡§o1¡§????¡§¡§??|¨¬,|¨¬¡ê¡è???¨ºo0.625ms */
	//1?2¡ê¡è?????|¨¬20ms-10.24s
	#if 0
	blesmt_addr_type=BLE_OWN_ADDR_RANDOM;
	ble_addr_t addr_t;
	uint8_t mac_addr[6] = {0xC0, 0x22, 0x33,0x44,0x55,0xC0};//D?????¡§o?  ?¨¢???¡ì¡§¡ã?mac|¨¬???¡è
	addr_t.type=BLE_OWN_ADDR_RANDOM;
	memcpy(addr_t.val,mac_addr,6);
	rc = ble_hs_id_set_rnd(mac_addr);
	if (rc != 0) {
	    iot_printf("ble_hs_id_set_rnd rc=%d\n", rc);
	    return;
	}	      
	#endif
    rc = ble_gap_adv_start(blesmt_addr_type, NULL, BLE_HS_FOREVER,
                           		&adv_params, ble_slave_gap_event, NULL);
	if(!rc)
		atbm_ble_status=ATBM_BLE_STATUS_ADVERTISE;
    if (rc != 0) {
        iot_printf("error enabling advertisement; rc=%d\n", rc);
        return;
    }	

	#if 0
	uint8_t own_addr_type;
	struct ble_gap_disc_params disc_params;
	 /* Tell the controller to filter duplicates; we don't want to process
	 * repeated advertisements from the same device.
	 */
	disc_params.filter_duplicates = 1;

	/**
	 * Perform a passive scan.	I.e., don't send follow-up scan requests to
	 * each advertiser.
	 0x00 Passive Scanning. No scan request PDUs shall be sent.
	 0x01 Active Scanning. Scan request PDUs may be sent.
	 */
	disc_params.passive = 0;

	/* Use defaults for the rest of the parameters. */
	/*Time interval from when the Controller started its last scan until it 
	begins the subsequent scan on the primary advertising physical channel.
	Range: 0x0004 to 0xFFFF
	Time = N * 0.625 ms
	Time Range: 2.5 ms to 40.959375 s*/
	disc_params.itvl = 300;//æ‰«æé—´éš”ï¼Œè®¾ç½®å¤šä¹…æ‰«æä¸€æ¬¡  
	
	/* Duration of the scan on the primary advertising physical channel.
	Range: 0x0004 to 0xFFFF
	Time = N * 0.625 ms
	Time Range: 2.5 ms to 40.959375 s*/
	disc_params.window = 200;//æ‰«æçª—å£æ—¶é—´,

	/*
	Scanning_Filter_Policy:
	Value Parameter Description
	0x00 Basic unfiltered scanning filter policy
	0x01 Basic filtered scanning filter policy
	0x02 Extended unfiltered scanning filter policy
	0x03 Extended filtered scanning filter policy
	All other values Reserved for future use
	*/
	disc_params.filter_policy = 0;
	disc_params.limited = 0;

	rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
					  ble_slave_gap_event, NULL);
	if (rc != 0) {
		iot_printf("Error initiating GAP discovery procedure; rc=%d\n",rc);
	}
	#endif
}


#ifdef CONFIG_LINUX_BLE_STACK_APP

int ble_startup_indication(unsigned char* data)
{
	
}
void atbm_ioctl_ble_smt_event_async(uint8_t* event_buffer, uint16_t event_len)
{
	
}
#endif  //ifeq ($(CONFIG_LINUX_BLE_STACK_APP), y)
extern bool g_is_quit;
void ble_smart_cfg_indication(uint8_t status, ble_smart_cfg* smart_info)
{
	struct wsm_ble_smt_ind smt_ind = { 0 };
	smt_ind.status = status;
	//if(status == BLE_SMART_CFG_STATUS_SUCESS)
	{
		smt_ind.ssid_len = smart_info->ssid_len;
		memcpy(smt_ind.ssid, smart_info->ssid, smart_info->ssid_len);
		smt_ind.pwd_len = smart_info->pwd_len;
		memcpy(smt_ind.pwd, smart_info->pwd, smart_info->pwd_len);
	}
	smt_demo_end_indicate((u8*)&smt_ind);
}

static int ble_smart_cfg_task(void* param)
{
	int rc;
	int wifi_connected_status = 0;
	uint8_t atbm_ble_addr[6];
	smart_cfg_start = 1;
	g_is_quit=0;
	atbm_ble_is_quit=0;
	ble_npl_time_t wifi_connected_start = 0;
	ble_npl_time_t wifi_connected_cur = 0;

	ble_npl_sem_init(&hci_smt_sem, 0);

	//	rc = ble_hs_id_infer_auto(0, &blesmt_addr_type);
	//	assert(rc == 0);
	blesmt_addr_type = BLE_OWN_ADDR_PUBLIC;
	ble_gap_set_default_tx_power(0);
	/* Begin advertising */
	blesmt_advertise();
	get_ble_addr(atbm_ble_addr);
	iot_printf("pub addr %02hhx:%02hx:%2hx:%2hhx:%02hhx:%02hhx\n",atbm_ble_addr[5],atbm_ble_addr[4],atbm_ble_addr[3],atbm_ble_addr[2],atbm_ble_addr[1],atbm_ble_addr[0]);
#if ATBM_BLE_SMART_APP
	while (!atbm_ble_is_quit) {
		//iot_printf(" ble_smart_cfg_task wait... step %d\n", ble_smart_info.step);

		if (ble_smart_info.step != BLE_SMART_CFG_SSID_PWD_TRANS_END) {
			rc = ble_npl_sem_pend(&hci_smt_sem, ble_npl_time_ms_to_ticks32(1000));//BLE_SMART_CFG_TIMEOUT_MS  1000ms
		}
		else {
			//rc = ble_npl_sem_pend(&hci_smt_sem, ble_npl_time_ms_to_ticks32(1000));
			sleep(1);
			rc = 0;
		}
		
		if (rc == 0) {
			if (ble_smart_info.step == 4) {
				iot_printf("ble smart cfg connect ap! ssid:%s pwd:%s \n", ble_smart_info.ssid, ble_smart_info.pwd);
				wifi_connected_start = ble_npl_time_get();
				wifi_connected_start = ble_npl_time_ticks_to_ms32(wifi_connected_start);
				ble_smart_cfg_indication(BLE_SMART_CFG_SSID_PWD_TRANS_END, &ble_smart_info);
				ble_smart_info.step = BLE_SMART_CFG_SSID_PWD_TRANS_END;
				//break;
			}
			else if (ble_smart_info.step == BLE_SMART_CFG_SSID_PWD_TRANS_END) {

				//check wifi connect status				 
				//	ble_smart_cfg_indication(BLE_SMART_CFG_STATUS_END, &ble_smart_info);
				wifi_connected_status = get_wifi_wpa_status();
				iot_printf("BLE_SMART_CFG_SSID_PWD_TRANS_END!wifi_connected_status:%d \n", wifi_connected_status);
				if (wifi_connected_status == 1) {
					ble_smart_info.step = BLE_SMART_CFG_STATUS_END;
					gatt_svr_chr_notify(ble_conn_handle, blesmt_notify_handle, wifi_conn_ok, strlen(wifi_conn_ok));
					break;
				}

				//break;
			}
			else if (ble_smart_info.step == BLE_SMART_CFG_STATUS_END) {
				iot_printf("### ble smart cfg sucess!\n");
				break;
			}
			else {
				assert(ble_smart_info.step >= BLE_SMART_CFG_STATUS_BUSY);
				iot_printf("ble smart cfg err(0x%X)!\n", ble_smart_info.step);
				break;
			}
		}
		/*
		else {
			iot_printf("ble smart cfg timeout!\n");
			ble_smart_cfg_indication(BLE_SMART_CFG_STATUS_TIMEOUT, NULL);
			break;
		}

		wifi_connected_cur = ble_npl_time_get();
		wifi_connected_cur = ble_npl_time_ticks_to_ms32(wifi_connected_cur);
		iot_printf("ble smart cfg wifi_connected_start:%d timeout!:%d\n", wifi_connected_start, wifi_connected_cur);
		if ((wifi_connected_cur - wifi_connected_start) > 15000)  //wifi connect 15s timeout
		{
			iot_printf("ble smart cfg wifi_connected_start:%d timeout!:%d elapse:%d\n", wifi_connected_start, wifi_connected_cur, (wifi_connected_cur - wifi_connected_start));
			gatt_svr_chr_notify(ble_conn_handle, blesmt_notify_handle, wifi_conn_no, strlen(wifi_conn_no));
			ble_smart_info.step = 0;
			break;
		}
	*/
	}
#endif

	while (!atbm_ble_is_quit) 
	{
		sleep(1);
	}
	if(atbm_ble_status==ATBM_BLE_STATUS_ADVERTISE)
	{
		ble_gap_adv_stop();
		iot_printf("<====== ble_gap_adv_stop\n");
	}
	if(atbm_ble_status==ATBM_BLE_STATUS_CONNECT)
	{
		ble_gap_terminate(ble_conn_handle,BLE_ERR_REM_USER_CONN_TERM);
		sleep(2);
		iot_printf("<====== ble_gap_terminate(ble_conn_handle,19);\n");
	}
	smart_cfg_start = 0;
	ble_npl_sem_free(&hci_smt_sem);
	atbm_ThreadStopEvent(ble_smt_thread);
	iot_printf("<====== ble_smart_cfg_task end step %d\n", ble_smart_info.step);
	sem_post(&sem_sock_sync);
	strcpy(cmd_line,"quit");
	g_is_quit=1;
	smart_cfg_start = 0;

	return 0;
}


void ble_smart_cfg_exit(void)
{
	atbm_stopThread(ble_smt_thread);
}

void ble_smart_gatt_svcs_init(void)
{
	int rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        return;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        return;
    }

    rc = ble_svc_gap_device_name_set(device_name);
    assert(rc == 0);

}

void ble_smart_cfg_startup(void)
{
	if(smart_cfg_start){
		iot_printf("ble smart cfg busy!\n");
		return;
	}
	iot_printf("ble_smart_cfg_startup!11\n");
	memset(&ble_smart_info, 0, sizeof(struct ble_smart_cfg_t));
	ble_smt_thread = atbm_createThread(ble_smart_cfg_task, (atbm_void*)ATBM_NULL, BLE_APP_PRIO);
}

void ble_smart_cfg_stop(void)
{
	if(smart_cfg_start == 0){
		iot_printf("ble smart cfg not start!\n");
		return;
	}
	ble_smart_info.step = BLE_SMART_CFG_STATUS_STOP;
	ble_npl_sem_release(&hci_smt_sem);
}

void ble_smart_cfg_test_ok(u8 *ssid, u8 *pwd)
{
	if(smart_cfg_start == 0){
		return;
	}
	iot_printf("smt test ok, ssid:%s, pwd:%s\n", ssid, pwd);
	strcpy(ble_smart_info.ssid, ssid);
	ble_smart_info.ssid_len = strlen(ssid);
	strcpy(ble_smart_info.pwd, pwd);
	ble_smart_info.pwd_len = strlen(pwd);	
	ble_smart_info.step = BLE_SMART_CFG_STATUS_END;
	ble_npl_sem_release(&hci_smt_sem);	
}




