#CONFIG_TX_NO_CONFIRM
#CONFIG_USB_AGGR_URB_TX
#CONFIG_TX_NO_CONFIRM
#
# 可以用来选择不同是模式，CONFIG_USB_AGGR_URB_TX 定义上时一定需要定义CONFIG_USE_DMA_ADDR_BUFFER
# 1> CONFIG_USB_AGGR_URB_TX + CONFIG_TX_NO_CONFIRM，每个urb 发送多个txframe 并且不需要confirm，需要打开lmac的NO_NEED_CONFIRM 宏，并且ratecontrol在lmac 实现
# 2> CONFIG_USB_AGGR_URB_TX 每个urb 发送多个txframe 并且需要confirm，需要关闭lmac的NO_NEED_CONFIRM 宏
# 3> 所有宏都不定义，每个urb 发送一个txframe 并且需要confirm，并且urb直接使用skbbuffer，需要关闭lmac的NO_NEED_CONFIRM 宏
# 4> 定义CONFIG_USE_DMA_ADDR_BUFFER，每个urb 发送一个txframe 并且需要confirm，并且urb直接DMAbuffer，需要把数据从skb copy到DMAbuffer，需要关闭lmac的NO_NEED_CONFIRM 宏，
# 5> 定义CONFIG_USE_DMA_ADDR_BUFFER+ CONFIG_TX_NO_CONFIRM，每个urb 发送一个txframe 并且不需要confirm，并且urb直接DMAbuffer，需要把数据从skb copy到DMAbuffer，需要开lmac的NO_NEED_CONFIRM 宏，并且ratecontrol在lmac 实现
# 6> 定义CONFIG_TX_NO_CONFIRM，每个urb 发送一个txframe 并且不需要confirm，并且urb直接使用skbbuffer，需要打开lmac的NO_NEED_CONFIRM 宏，并且ratecontrol在lmac 实现
#
##对于调度慢+cpu强的板子使用 模式1 或者 模式2
##对于调度快+cpu慢的板子使用 模式3 或者 模式6
## 默认使用模式 3
#####################################################################################################################################################################################################################
# thingino: always include the driver .config that buildroot stages at $(M)/.config.
# The in-kernel (M=) build exports ATBM_BUILD_IN_KERNEL=y, which previously skipped
# this and left every CONFIG_ATBM_* empty (broke WEP/SW_ENC, P2P, fw name, ...).
-include $(M)/.config
-include $(WIFI_PROJ_DIR)/.config



k-dir:=$(if $(filter /%,$(src)),$(src),$(srctree)/$(src))
NOSTDINC_FLAGS := -I$(k-dir)/../include/ \
	-include $(k-dir)/../include/linux/compat-2.6.h \
	-DCOMPAT_STATIC
	
ifeq ($(CONFIG_CPTCFG_CFG80211),y)
NOSTDINC_FLAGS +=	-I$(k-dir)/../include/backport-include \
	-I$(k-dir)/../include/uapi 
endif

KBUILD_CFLAGS += -Wno-error \
	-Wno-error=vla \
	-Wno-error=unused-function \
	-Wno-error=strict-prototypes \
	-w
#	-Wno-error=date-time 
#	-Wno-error=incompatible-pointer-types

#####################################################

ifeq ($(CONFIG_ATBM_MODULE_NAME_WIFI6),)
CONFIG_ATBM_MODULE_NAME_WIFI6 = atbm_wifi6
endif
ifeq ($(CONFIG_ATBM_DEFAULT_COUNTRY_CODE_WIFI6),)
CONFIG_ATBM_DEFAULT_COUNTRY_CODE_WIFI6 = "00"
endif
ifeq ($(CONFIG_ATBM_WIFIIF1_NAME_WIFI6),)
CONFIG_ATBM_WIFIIF1_NAME_WIFI6 = "wlan%d"
endif
ifeq ($(CONFIG_ATBM_WIFIIF2_NAME_WIFI6),)
CONFIG_ATBM_WIFIIF2_NAME_WIFI6 = "p2p%d"
endif
ifeq ($(CONFIG_ATBM_USB_VID_WIFI6),)
CONFIG_ATBM_USB_VID_WIFI6 = 0x007a
endif
ifeq ($(CONFIG_ATBM_USB_PID_WIFI6),)
CONFIG_ATBM_USB_PID_WIFI6 = 0x8888
endif

ifeq ($(CONFIG_ATBM_SDIO_VID_WIFI6),)
CONFIG_ATBM_SDIO_VID_WIFI6 = 0x007a
endif
ifeq ($(CONFIG_ATBM_SDIO_PID_WIFI6),)
CONFIG_ATBM_SDIO_PID_WIFI6 = 0x6011
endif
ifeq ($(CONFIG_ATBM_MODULE_DRIVER_NAME_WIFI6),)
CONFIG_ATBM_MODULE_DRIVER_NAME_WIFI6 = "atbm_wlan6"
endif
ifeq ($(CONFIG_ATBM_PLATFORM_DEVICE_NAME_WIFI6),)
CONFIG_ATBM_PLATFORM_DEVICE_NAME_WIFI6 = "atbm_dev_wifi"
endif
ifeq ($(CONFIG_ATBM_MODULE_PM_STAYAWK_WIFI6),)
CONFIG_ATBM_PLATFORM_DEVICE_NAME_WIFI6 = "pm_stayawake"
endif
ifeq ($(CONFIG_ATBM_FW_NAME_WIFI6),)
CONFIG_ATBM_FW_NAME_WIFI6="fw.bin"
endif

SDIO_HOST      ?= $(shell echo $(CONFIG_ATBM_SDIO_MMCx_WIFI6))
IF1NAME        ?= $(shell echo $(CONFIG_ATBM_WIFIIF1_NAME_WIFI6))
IF2NAME        ?= $(shell echo $(CONFIG_ATBM_WIFIIF2_NAME_WIFI6))
FW             ?= $(shell echo $(CONFIG_ATBM_FW_NAME_WIFI6))
MODULES_NAME   ?= $(shell echo $(CONFIG_ATBM_MODULE_NAME_WIFI6))
USBVID		   ?= $(shell echo $(CONFIG_ATBM_USB_VID_WIFI6))
USBPID		   ?= $(shell echo $(CONFIG_ATBM_USB_PID_WIFI6))
SDIOVID			?=$(shell echo $(CONFIG_ATBM_SDIO_VID_WIFI6))
SDIOPID			?=$(shell echo $(CONFIG_ATBM_SDIO_PID_WIFI6))
MODDRVNAME	   ?= $(shell echo $(CONFIG_ATBM_MODULE_DRIVER_NAME_WIFI6))
PLFDEVNAME	   ?= $(shell echo $(CONFIG_ATBM_PLATFORM_DEVICE_NAME_WIFI6))
MODPMSTAYAWK   ?= $(shell echo $(CONFIG_ATBM_MODULE_PM_STAYAWK_WIFI6))

SAEAUTHEN      ?= $(CONFIG_ATBM_FUNC_SAE_AUTHEN_WIFI6)
LOAD_FW_H      ?= $(CONFIG_ATBM_USE_FIRMWARE_H_WIFI6)
SKB_DEBUG      ?= $(CONFIG_ATBM_FUNC_SKB_DEBUG_WIFI6)
MEM_DEBUG      ?= $(CONFIG_ATBM_FUNC_SKB_DEBUG_WIFI6)
BRIDGE         ?= $(CONFIG_ATBM_SUPPORT_BRIDGE_WIFI6)
MONITOR        ?= $(CONFIG_ATBM_FUNC_MONITOR_WIFI6)
EARLYSUSPEND   ?= $(CONFIG_ATBM_FUNC_EARLYSUSPEND_WIFI6)
NOTXCONFIRM    ?= $(CONFIG_ATBM_FUNC_NOTXCONFIRM_WIFI6)
CH5G           ?= $(CONFIG_ATBM_SUPPORT_BAN_5G_24G_WIFI6)
CH5G_PRETEND24 ?= $(CONFIG_ATBM_SUPPORT_BAN_5G_PRETEND_24_WIFI6)
CH24G_ONLY     ?= $(CONFIG_ATBM_SUPPORT_BAN_24G_WIFI6)
ONLY_HT20	   ?= $(CONFIG_ATBM_WITHBAND_2_4G_ONLY_HT20_WIFI6)
USBAGGTX       ?= $(CONFIG_ATBM_FUNC_USB_AGGRTX_WIFI6)
USBDMABUFF     ?= $(CONFIG_ATBM_FUNC_USB_DMABUFF_WIFI6)
USBCMDENHANCE  ?= $(CONFIG_ATBM_FUNC_USB_CMD_ENHANCE_WIFI6)
USBDATAENHANCE ?= $(CONFIG_ATBM_FUNC_USB_DATA_ENHANCE_WIFI6)
PMRELODDFW     ?= $(CONFIG_ATBM_FUNC_PS_WAKEUP_RELOAD_FW_WIFI6)
USB_BUS        ?= $(CONFIG_ATBM_USB_BUS_WIFI6)
SDIO_BUS       ?= $(CONFIG_ATBM_SDIO_BUS_WIFI6)
SPI_BUS        ?= $(CONFIG_ATBM_SPI_BUS_WIFI6)
WIFI6_VARIANT_OCEANUS  ?= $(CONFIG_ATBM_WIFI6_OCEANUS)
WIFI6_VARIANT_CLITE  ?= $(CONFIG_ATBM_WIFI6_CLITE)
COMPAT_6062X  ?= $(CONFIG_COMPAT_6062X)
COMPAT_6162X  ?= $(CONFIG_COMPAT_6162X)
COMPAT_6062_CX  ?= $(CONFIG_COMPAT_6062_CX)
COMPAT_6132_C  ?= $(CONFIG_COMPAT_6132_C)

CHECKSUM       ?= $(CONFIG_ATBM_FUNC_HW_CHSUM_WIFI6)
SWRATECTRL     ?= $(CONFIG_ATBM_SW_RATE_CTRL_WIFI6)
P2PENABLE      ?= $(CONFIG_ATBM_FUNC_P2P_ENABLE_WIFI6)
SWENC          ?= $(CONFIG_ATBM_FUNC_SW_ENC_WIFI6)
MODULEFS       ?= $(CONFIG_ATBM_FUNC_MODULE_FS_WIFI6)
DEVCTRL        ?= $(CONFIG_ATBM_FUNC_DEV_CTRL_API_WIFI6)
SMARTCONFIG    ?= $(CONFIG_ATBM_FUNC_SMARTCONFIG_WIFI6)
CONFIG_ARESB   ?= $(CONFIG_ATBM603x_WIFI6)
CONFIG_HERA    ?= $(CONFIG_ATBM6041_WIFI6)
CONFIG_CRONUS  ?= $(CONFIG_ATBM606x_WIFI6)
NEED_SCOND_INTERFACE ?= $(CONFIG_NEED_P2P0_INTERFACE_WIFI6)
CUSTORMSPECIAL ?= y
SENDSPECIAL_MGMT ?= y
CONFIG_NOT_SUPPORT_40M_CHW ?= $(CONFIG_ATBM601x_WIFI6)
DRVLOADERFAST  ?= $(CONFIG_ATBM_FUNC_DRV_LOADER_FAST_WIFI6)
PRI_IE         ?= $(CONFIG_ATBM_FUNC_PRIVE_IE_WIFI6)
MONHDRPRISM         ?= $(CONFIG_ATBM_FUNC_MONITOR_HDR_PRISM_WIFI6)
BASETUP        ?= $(CONFIG_ATBM_FUNC_DRIVER_SETUP_AMPDU_WIFI6)
COUNTRY_CODE	?=$(CONFIG_CPTCFG_CFG80211_COUNTRY_CODE_WIFI6)
COUNTRY_VAL		?= $(shell echo $(CONFIG_ATBM_DEFAULT_COUNTRY_CODE_WIFI6))
USBINTR			?=$(CONFIG_ATBM_USB_INTR_WIFI6)
RADAR			?=$(CONFIG_ATBM_FUNC_RADAR_DETECT)
BLEHOST			?=$(CONFIG_ATBM_BLE_HOST_DRIVER_WIFI6)
BLEPLATFORM		?=$(CONFIG_ATBM_BLE_WIFI_PLATFORM_WIFI6)
BLECOEXIST      ?=$(CONFIG_ATBM_BLE_ADV_COEXIST_WIFI6)
BLEMODULE		?=$(CONFIG_ATBM_BLE_HOST_DRIVER_WIFI6)$(CONFIG_ATBM_BLE_WIFI_PLATFORM_WIFI6)
BLECODESRAME    ?=$(CONFIG_ATBM_BLE_WIFI_PLATFORM_WIFI6)
BLUEDROID	?=$(CONFIG_BLUEDROID_WIFI6)
BLE			?=$(CONFIG_ATBM_BLE_WIFI6)
CERTFIRMWARE	?=$(CONFIG_CERT_FIRMWARE)
ONLYCOMB_FIRMWARE	?=$(CONFIG_ONLY_COMB_FIRMWARE)
############################# ANDROID ##########################################
ifeq ($(ANDROID),y)
NEED_SCOND_INTERFACE = y
ONLY_HT20 = n
P2PENABLE = y
SAEAUTHEN = y
SWENC = y
endif

ifeq ($(CONFIG_ATBM_APOLLO_WIFI6),)
CONFIG_ATBM_APOLLO_WIFI6=m
endif
############################# interface ########################################
ifeq ($(USB_BUS_WIFI6),y)
HIF:=usb
endif
ifeq ($(SDIO_BUS_WIFI6),y)
HIF:=sdio
endif
ifeq ($(SPI_BUS_WIFI6),y)
HIF:=spi
endif

###################################################################################
#ccflags-y += -DCONFIG_ATBM_APOLLO_5GHZ_SUPPORT
#ccflags-y += -DCONFIG_ATBM_APOLLO_WAPI_SUPPORT
#ccflags-y  += -I$(shell pwd)/../include/ \
        -include $(shell pwd)/../include/linux/compat-2.6.h \
        -DCOMPAT_STATIC

# wext_cfg.c puts its whole body, including the tx-power helpers that
# dev_ioctl.c and internal_cmd.c call unconditionally, behind
# #ifdef CONFIG_WIRELESS_EXT. That kernel symbol is a bare bool with no prompt
# and nothing selects it (CFG80211_WEXT selects WEXT_CORE instead), so it can
# never come from the kernel config and the module ends up with undefined
# atbm_{oceanus,cronus}_set_tx_power and
# atbm_set_txpwr_by_atbm_txpwer_dcxo_cfg_file. The wext API itself is present
# whenever WEXT_CORE is, so define the macro the driver expects.
ccflags-y += -DCONFIG_WIRELESS_EXT

ATBM_BUILD_TIME = $(shell date "+%Y-%m-%d_%H:%M:%S")
ccflags-y += -DCONFIG_ATBM_BUILD_TIME=\"$(ATBM_BUILD_TIME)\"
##指定配置文件路径，当配置文件存在时使用配置文件中的dcxo值不使用efuse中的dcxo，配置文件中的delta_gain为修正值，如果文件不存在则直接使用efuse中的数据
ATBM_CONFIG_FILE="/tmp/atbm_txpwer_dcxo_cfg.txt"
ccflags-y += -DCONFIG_TXPOWER_DCXO_VALUE=\"$(ATBM_CONFIG_FILE)\"
# use the deltagain value in file :"$(ATBM_CONFIG_FILE)",when file exist
ATBM_RATE_POWER_CONFIG_FILE="/tmp/set_rate_power.txt"
ccflags-y += -DCONFIG_RATE_TXPOWER=\"$(ATBM_RATE_POWER_CONFIG_FILE)\"

ATBM_RATE_POWER_CONFIG_FILE_5G="/tmp/set_rate_power_5g.txt"
ccflags-y += -DCONFIG_RATE_TXPOWER_5G=\"$(ATBM_RATE_POWER_CONFIG_FILE_5G)\"

# use the temp table and dcxo table in file :"$(ATBM_TEMP_DCXO_TABLE_CONFIG_FILE)",when file exist
ATBM_TEMP_DCXO_TABLE_CONFIG_FILE="/tmp/temp_dcxo_table.txt"
ccflags-y += -DCONFIG_TEMP_DCXO_TABLE=\"$(ATBM_TEMP_DCXO_TABLE_CONFIG_FILE)\"

ccflags-y += -DCONFIG_COUNTRY_VAL=\"$(COUNTRY_VAL)\"

ifneq ($(IF1NAME),)
ccflags-y += -DCONFIG_IF1NAME=\"$(IF1NAME)\"
endif
ifneq ($(IF2NAME),)
ccflags-y += -DCONFIG_IF2NAME=\"$(IF2NAME)\"
endif
ifneq ($(USBVID),)
ccflags-y += -DCONFIG_USBVID=$(USBVID)
endif
ifneq ($(USBPID),)
ccflags-y += -DCONFIG_USBPID=$(USBPID)
endif
ifneq ($(SDIOVID),)
ccflags-y += -DCONFIG_SDIOVID=$(SDIOVID)
endif
ifneq ($(SDIOPID),)
ccflags-y += -DCONFIG_SDIOPID=$(SDIOPID)
endif


ifneq ($(MODDRVNAME),)
ccflags-y += -DCONFIG_MODDRVNAME=\"$(MODDRVNAME)\"
endif
ifneq ($(PLFDEVNAME),)
ccflags-y += -DCONFIG_PLFDEVNAME=\"$(PLFDEVNAME)\"
endif

ifneq ($(MODPMSTAYAWK),)
ccflags-y += -DCONFIG_MODPMSTAYAWK=\"$(PLFDEVNAME)\"
endif

ATBM_MODULES_NAME ?= $(MODULES_NAME)
ifneq ($(FW),)
ccflags-y += -DCONFIG_FW_NAME=\"$(FW)\"
endif
ifneq ($(SDIO_HOST),)
ccflags-y += -DCONFIG_ATBM_SDIO_MMC_ID=\"$(SDIO_HOST)\"
endif
ifneq ($(COUNTRY_CODE),)
ccflags-y += -DCONFIG_CPTCFG_CFG80211_COUNTRY_CODE
endif
ccflags-y += $(ATBM_WIFI__EXT_CCFLAGS)
ccflags-y += -DCONFIG_ATBM_APOLLO_DISABLE_BEACON_HINTS
ccflags-y += -DOLD_RATE_POLICY=1
ccflags-y += -DCONFIG_MAC80211_BRIDGE_MULTI_PORT
ccflags-y += -DMINSTREL_RSSI_USED
ccflags-y += -DATBM_P2P_ADDR_USE_LOCAL_BIT
ccflags-y += -DATBM_WIFI_QUEUE_LOCK_BUG
ccflags-y += -DATBM_CHANGE_AP_TIMESTAMP_TO_BOOTTIME
ccflags-y += -DATBM_FUNCTION
#ccflags-y += -DATBM_USE_SAVED_FW
#ccflags-y += -DATBM_AP_SME
#ccflags-y += -DP2P_MULTIVIF

#ccflags-y += -DATBM_P2P_CHANGE
# chang role to go default
#ccflags-y += -DATBM_CHANGE_LOCAL_REMOUT_ROLE
# don't support PERSISTENT GROUP and invitation, need to modify wpa_supplicant
#ccflags-y += -DATBM_PARASE_P2P_CAP
ccflags-y += -DATBM_WIFI_NO_P2P_PS
ccflags-y += -DATBM_SPECIAL_PKG_DOWN_RATE
#ccflags-y += -DMCAST_FWDING
ccflags-y += -DATBM_APOLLO_SUSPEND_RESUME_FILTER_ENABLE
ccflags-y += -DCONFIG_ATBM_IOCTRL
ccflags-y += -DOPER_CLOCK_USE_SEM
ccflags-y += -DEXIT_MODULE_RESET_USB=1
ccflags-y += -DATBM_VIF_LIST_USE_RCU_LOCK
#ccflags-y += -DATBM_SUPPORT_SMARTCONFIG 
#ccflags-y += -DIEEE80211_TASKLET
#ccflags-y += -DCONFIG_RATE_HW_CONTROL
ccflags-y += -DCONFIG_ATBM_SUPPORT_SG
ccflags-y += -DCONFIG_ATBM_SUPPORT_TSO
ccflags-y += -DCONFIG_ATBM_SUPPORT_GRO
ccflags-y += -DCONFIG_ATBM_SUPPORT_GRO_AGGR
ccflags-y += -DCONFIG_ATBM_SUPPORT_REKEY
#ccflags-y += -DCONFIG_ATBM_SUPPORT_COUNTRY_CODE
#add AP+STA mode , sta channel change send event to hostapd
ccflags-y += -DCONFIG_ATBM_AP_CHANNEL_CHANGE_EVENT
#ccflags-y += -DCONFIG_ATBM_SELF_WORKQUEUE
ifeq ($(platform),PLATFORM_AMLOGIC_S805)
ccflags-y += -DCONFIG_ATBM_APOLLO_USE_GPIO_IRQ
endif
#ifeq ($(platform),PLATFORM_AMLOGIC)
#ccflags-y += -DCONFIG_ATBM_APOLLO_USE_GPIO_IRQ
#endif
#ccflags-y += -DCONFIG_ATBM_APOLLO_USE_GPIO_IRQ
#Select GPIO23 or GPIO1 use for sdio irq
#ccflags-y += -DUSE_GPIO_23=1
#ccflags-y += -DUSE_GPIO_1=1

ccflags-$(CONFIG_ATBM_USB_AUTO_WAKEUP) += -DCONFIG_ATBM_USB_AUTO_WAKEUP
#ccflags-y += -DCONFIG_ATBM_GET_GPIO4

ccflags-y += -DATBM_SUPPORT_WIDTH_40M
ccflags-y += -DATBM_SUPPORT_LSLP_PRIV_COMMUNICATION
#ccflags-y += -DCONFIG_CFG880211_SUPPORT_HE
#ccflags-y += -DIPC_AP_USED_11G_NO_RTS
#p2p mode always in client mode, used in 4.4.2 android ,because monitor interface is not open
#(ANDRIOD_P2P in wpa_supplicant is open will cause this bug )
#ccflags-y += -DP2P_ALWAYS_CLIENT
#ccflags-y += -DATBM_11W_TEST

#wifi alliance test ,
#some function will cause pass wifi alliance certificate,but not good for some AP
#ccflags-y += -DWIFI_ALLIANCE_CERTIF
#ccflags-y += -DATBM_11W_TEST
#if used sdio download firmware
ccflags-y += -DWIFI_FW_DOWNLOAD
# Extra IE for probe response from upper layer is needed in P2P GO
# For offloading probe response to FW, the extra IE must be included
# in the probe response template
ccflags-y += -DATBM_PROBE_RESP_EXTRA_IE
#ccflags-y += -DCONFIG_ATBM_APOLLO_DEBUG
#ccflags-y += -DCONFIG_ATBM_STA_LISTEN
ccflags-$(SAEAUTHEN) += -DCONFIG_ATBM_SA_QUERY


ccflags-y += -DCONFIG_ATBM_IWPRIV_USELESS
ccflags-y += -DCONFIG_ATBM_SUPPORT_AP_CONFIG
#ccflags-y += -DCONFIG_ATBM_STA_DYNAMIC_PS
ccflags-y += -DCONFIG_ATBM_SUPPORT_REALTIME_CHANGE_MAC
ccflags-y += -DCONFIG_LMAC_RATECTRL_RATE_BUG_FIX
ccflags-$(BASETUP) += -DCONFIG_ATBM_DRIVER_PROCESS_BA
###################################################################
#SRRC/CE/FCC need Dedicated certification firmware
###################################################################
ccflags-$(CERTFIRMWARE) += -DATBM_ADD_CRERT_FIRMWARE
ccflags-$(ONLYCOMB_FIRMWARE) += -DCONFIG_ATBM_ONLY_COMB_FIRMWARE
###################################################################
#ble recv packet to ble_host use atbm_netif_rx function
#
###################################################################
#ccflags-y += -DCONFIG_BLE_RECV_DATA_HIGHT_LAYER

###################################################################
#customer function
#
###################################################################
ccflags-y += -DCONFIG_JUAN_MISC
#ccflags-y += -DTP_TWO_ANT_FUNC
###################################################################
#use PA
# PA_EN 	: GPIO1
# LNA_EN 	: GPIO2
# C_RX 		: GPIO3
#Transmit Mode: GPIO1=1,GPIO2=0, GPIO3=0
#Receive LNA Mode: GPIO0=0,GPIO2=1, GPIO3=1
#Bypass Mode: GPIO1=0,GPIO2=0, GPIO3=1
###################################################################
#ccflags-y += -DATBM_USE_EXTERN_PA
#ccflags-y += -DKXCOMTECH_OUTERPA_SW=1
###################################################################
#fastlink support 
###################################################################
#ccflags-y += -DATBM_USE_FASTLINK
ccflags-y += -DATBM_SAVE_BSS_ID_0=\"/data/fastlink_0.txt\"
ccflags-y += -DATBM_SAVE_BSS_ID_1=\"/data/fastlink_1.txt\"

###################################################################
#support start two hostapd , need beacon 100 / 200 ms
###################################################################
#ccflags-y += -DCONFIG_ATBM_SUPPORT_MULTIAP
###################################################################
#CSA MODE SUPPORT
###################################################################
ccflags-y += -DCONFIG_ATBM_SUPPORT_CSA
ccflags-y += -DCONFIG_ATBM_LOW_KERNEL_SUPPORT_CSA=0
###################################################################
#Ingenic T31 & duofang(Augentix HC1703_1723_1753_1783s) platform need close
###################################################################
ccflags-$(USBINTR) += -DCONFIG_ATBM_USB_INTERVAL
###################################################################
#wifi6 back-port
###################################################################
#ccflags-$(CONFIG_WIFI6) += -DCONFIG_ATBM_HE=1 
CONFIG_HE=y
ifeq ($(CONFIG_HE),y)
ccflags-y += -DCONFIG_ATBM_HE=1 
ccflags-y += -DCONFIG_ATBM_HE_AP
endif

ifeq ($(COMPAT_6132_C),y)
ifneq ($(COMPAT_6162X),y)
ifneq ($(COMPAT_6062_CX),y)
#VHT = y
#ccflags-$(VHT) += -DCONFIG_ATBM_VHT
#ccflags-y      += -DCONFIG_CFG880211_SUPPORT_VHT
endif
endif

else
VHT = y
ccflags-$(VHT) += -DCONFIG_ATBM_VHT
ccflags-y      += -DCONFIG_CFG880211_SUPPORT_VHT
endif
###################################################################
#wifi6 5G 80M not support in hardware, but some AP will tx 80M to us
# CONFIG_SUPPORT_80M_ADAPTER is to check this, will auto reconnect use 20Monly
###################################################################
ccflags-y     += -DCONFIG_SUPPORT_80M_ADAPTER
###################################################################
#wifi6 CFG80211 
###################################################################
ifeq ($(CONFIG_HE),y)
ccflags-$(CONFIG_CPTCFG_CFG80211) += -DCONFIG_CFG880211_SUPPORT_HE=1
endif
#ccflags-$(CONFIG_WIFI6_BEAMFORM) += -DCONFIG_ATBM_HE_BEAMFORM=1
#ccflags-$(CONFIG_WIFI6_WFA_CERTIFICATE) += -DWFA_CERTIFICATE_TEST=1

###ccflags-y += -DCONFIG_CFG880211_SUPPORT_HE
ccflags-$(CONFIG_CPTCFG_CFG80211) += -DCONFIG_CPTCFG_CFG80211=1  -DCONFIG_CFG80211_COMPAT_MODIFIED=1
ccflags-$(CONFIG_CPTCFG_CFG80211) += -DCPTCFG_CFG80211=1
ccflags-$(CONFIG_CPTCFG_CFG80211_WEXT) += -DCONFIG_CPTCFG_CFG80211_WEXT=1 -DCPTCFG_CFG80211_WEXT=1
################################wpa3 in linux < 5.10 need open this ========start====
ccflags-$(CONFIG_WPA3_CFG80211_EX) += -DCONFIG_CFG80211_COMPAT_MODIFIED=1
################################wpa3 in linux < 5.10 need open this ========end====
###################################################################
#wifi6 ciper
###################################################################
#ccflags-y += -DATBM_TEST_DUMMY

###################################################################
#used scond interface p2p0
###################################################################
ifeq ($(NEED_SCOND_INTERFACE),y)
ccflags-y += -DNEED_P2P0_INTERFACE=1
else
ccflags-y += -DNEED_P2P0_INTERFACE=0
endif

###################################################################
#atbm ble module support
###################################################################
###########WIFI BLE braodcast comb
ccflags-$(BLECOEXIST) += -DCONFIG_ATBM_BLE_ADV_COEXIST=1  -DCONFIG_ATBM_BLE=1
###########WIFI BLE connected comb
#ccflags-$(BLECODESRAME) += -DCONFIG_ATBM_BLE_CODE_SRAM
ccflags-$(BLEPLATFORM) +=  -DCONFIG_ATBM_BLE_CODE_SRAM -DCONFIG_WIFI_BT_COMB=1 -DCONFIG_ATBM_BLE=1
###################################################################
#monitor hdr prism
###################################################################
ccflags-$(MONHDRPRISM) += -DCONFIG_ATBM_MONITOR_HDR_PRISM

###################################################################
#loader firmware fast
###################################################################
ccflags-$(DRVLOADERFAST) += -DCONDIF_ATBM_CTRL_REQUEST_ASYNC
###################################################################
#private protocl 
################################################################### 
ccflags-$(PRI_IE) += -DATBM_PRIVATE_IE
#ifneq ($(PRI_IE),y)
ccflags-y += -DCONFIG_ATBM_STA_LISTEN
#endif
###################################################################
#ATBM_SUSPEND_REMOVE_INTERFACE
#before enter suspend state ,remove all interface
###################################################################
ccflags-$(PMRELODDFW) += -DATBM_SUSPEND_REMOVE_INTERFACE
###################################################################
#ATBM_PM_USE_EARLYSUSPEND
#early suspend enbale
###################################################################
ccflags-$(EARLYSUSPEND) += -DATBM_PM_USE_EARLYSUSPEND
###################################################################
#ATBM_ALLOC_MEM_DEBUG
#debug mem leakage
###################################################################
ccflags-$(MEM_DEBUG) += -DATBM_ALLOC_MEM_DEBUG
###################################################################
#ATBM_ALLOC_SKB_DEBUG
#debug skb leakage
###################################################################
ccflags-$(SKB_DEBUG) += -DATBM_ALLOC_SKB_DEBUG
####################################################################
#CONFIG_USE_FW_H
#use firmware.h as firmware
####################################################################
ccflags-$(LOAD_FW_H) += -DCONFIG_USE_FW_H
####################################################################
#ATBM_NOT_SUPPORT_40M_CHW
#only support 20m channel
####################################################################
#CONFIG_NOT_SUPPORT_40M_CHW=$(ONLY_HT20)
#ccflags-$(CONFIG_NOT_SUPPORT_40M_CHW) += -DATBM_NOT_SUPPORT_40M_CHW
ccflags-$(ONLY_HT20) += -DATBM_NOT_SUPPORT_40M_CHW
####################################################################
#ATBM_SUPPORT_PKG_MONITOR
#monitor funciton
####################################################################
ccflags-$(MONITOR) += -DATBM_SUPPORT_PKG_MONITOR
####################################################################
#CONFIG_MAC80211_BRIDGE
#enable bridge
####################################################################
ccflags-$(BRIDGE) += -DCONFIG_MAC80211_BRIDGE
####################################################################
#CONFIG_TX_NO_CONFIRM      
#enable tx noconfirm
####################################################################
ccflags-$(NOTXCONFIRM) += -DCONFIG_TX_NO_CONFIRM
####################################################################
#CONFIG_USB_AGGR_URB_TX
#enable usb aggr tx 
####################################################################
ccflags-$(USBAGGTX) += -DCONFIG_USB_AGGR_URB_TX
###################################################################
#fw open HIT TRACE driver must open
#
###################################################################

#ccflags-y += -DCONFIG_SUPPORT_HIF_TRACE

####################################################################
#CONFIG_USE_DMA_ADDR_BUFFER
#urb use dma buff
####################################################################
ccflags-$(USBDMABUFF) += -DCONFIG_USE_DMA_ADDR_BUFFER
####################################################################
#CONFIG_WSM_CMD_XMIT_DIRECTLY
#cmd transmit directly
####################################################################
ccflags-$(USBCMDENHANCE) += -DCONFIG_WSM_CMD_XMIT_DIRECTLY
####################################################################
#CONFIG_USB_DATA_XMIT_DIRECTLY
#usb data xmit directly
####################################################################
ccflags-$(USBDATAENHANCE) += -DCONFIG_USB_DATA_XMIT_DIRECTLY
####################################################################
#CONFIG_ATBM_5G_PRETEND_2G
#wifi use 5G channel to pretend 2g special channel
####################################################################
ccflags-$(CH5G_PRETEND24) += -DCONFIG_ATBM_5G_PRETEND_2G
ccflags-$(CH5G) += -DCONFIG_ATBM_APOLLO_5GHZ_SUPPORT
ccflags-$(CH24G_ONLY) += -DCONFIG_ATBM_APOLLO_24GHZ_ONLY_SUPPORT
####################################################################
#CONFIG_ATBM_SUPPORT_P2P
#enable p2p function
###################################################################
ccflags-$(P2PENABLE) += -DCONFIG_ATBM_SUPPORT_P2P
####################################################################
#rate ctrl select
#hw or sw
####################################################################
####################################################################
####################################################################
#enable sw enc
####################################################################
ccflags-$(SWENC) += -DCONFIG_ATBM_USE_SW_ENC
####################################################################
#CONFIG_ATBM_MOULE_FS
#enable module fs
####################################################################
ccflags-$(MODULEFS) += -DCONFIG_ATBM_MOULE_FS
####################################################################
#CONFIG_ATBM_DEV_IOCTL
#enabel dev ioctl
####################################################################
ccflags-$(DEVCTRL) += -DCONFIG_ATBM_DEV_IOCTL 
####################################################################
#ATBM_SUPPORT_SMARTCONFIG
#enabel smart config
####################################################################
ccflags-$(SMARTCONFIG) += -DATBM_SUPPORT_SMARTCONFIG
####################################################################
ccflags-$(SPECTRUMMGMT) += -DCONFIG_ATBM_SPECTRUM_MGMT

####################################################################
CUSTORMSPECIAL=y
ccflags-$(CUSTORMSPECIAL) += -DCONFIG_IEEE80211_SPECIAL_FILTER
ccflags-$(SENDSPECIAL_MGMT) += -DCONFIG_IEEE80211_SEND_SPECIAL_MGMT
####################################################################
#SAE authen for wpa3
#
####################################################################
ccflags-$(SAEAUTHEN) += -DCONFIG_ATBM_SUPPORT_SAE
#CONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
#wifi product  test use Golden LED
####################################################################
ccflags-y += -DCONFIG_ATBM_PRODUCT_TEST_USE_GOLDEN_LED
####################################################################
#ATBM_PRODUCT_TEST_USE_FEATURE_ID
#wifi product test use feature ID
####################################################################
ifeq ($(ATBM_SYSTEM),Linux)
ccflags-y += -DATBM_PRODUCT_TEST_USE_FEATURE_ID
endif
####################################################################
#WOW
#
####################################################################
#ccflags-y += -DATBM_SUPPORT_WOW
#ccflags-y += -DCONFIG_ATBM_START_WOL_TEST
####################################################################
#Radar Detect
#
####################################################################
ccflags-$(RADAR) += -DCONFIG_ATBM_RADAR_DETECT
####################################################################
#sigmstar change iwlist scan print
#add noise, b/g/n ,max rate , need to change cfg80211,
#if need this feature
####################################################################
#ccflags-y += -DSIGMSTAR_SCAN_FEATURE
#ccflags-y += -DIPV6_FILTERING
#ccflags-y += -DCONFIG_ATBM_APOLLO_BH_DEBUG
#ccflags-y += -DCONFIG_ATBM_APOLLO_WSM_DEBUG
#ccflags-y += -DCONFIG_ATBM_APOLLO_WSM_DUMPS
#ccflags-y += -DCONFIG_ATBM_APOLLO_WSM_DUMPS_SHORT
#ccflags-y += -DCONFIG_ATBM_APOLLO_TXRX_DEBUG
#ccflags-y += -DCONFIG_ATBM_APOLLO_TX_POLICY_DEBUG
#ccflags-y += -DCONFIG_ATBM_APOLLO_STA_DEBUG
#ccflags-y += -DCONFIG_ATBM_APOLLO_DUMP_ON_ERROR
#ccflags-y += -DCONFIG_ATBM_APOLLO_ITP
#ccflags-y += -DCONFIG_ATBM_APOLLO_TESTMODE
# use the mac addr in file :"/data/.mac.info"
ccflags-y += -DCUSTOM_FEATURE_MAC
#ccflags-y += -DTEST_RF_POWER
#ccflags-y += -DUSB_CMD_UES_EP0
#ccflags-y += -DRESET_CHANGE
#ccflags-y += -DCONFIG_ATBMWIFI_WDT
#ccflags-y += -DCONFIG_ATBM_ALIGNED_ACCESS


#PLATFORM_XUNWEI
#PLATFORM_SUN6I
#PLATFORM_FRIENDLY
#PLATFORM_SUN6I_64
#ccflags-y += -DATBM_WIFI_PLATFORM=PLATFORM_HIS

ifeq ($(CONFIG_CRONUS),y)
ccflags-y += -DPROJ_TYPE=CRONUS
ccflags-$(CHECKSUM) += -DCHKSUM_HW_SUPPORT
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_24M
endif


ifeq ($(CONFIG_FPGA),y)
#ccflags-y += -DDCXO_USE_SMU_REG
ccflags-y += -DSDIO_CHANGE=1
#ccflags-y += -DRESET_CHANGE
#ccflags-y += -DTEST_DCXO_CONFIG=1
endif

ccflags-y += -DCONFIG_ADD_HTC_HDR=0

#if TEST_DCXO_CONFIG is not config,DCXO_TYPE is not used
#if TEST_DPLL_CONFIG is not config,DPLL_CLOCK is not used

ccflags-y += -DCONFIG_ATBM_APOLLO_SUPPORT_SGI
ifeq ($(USB_BUS),y)
ccflags-y += -DUSB_BUS
ccflags-y += -DUSB_USE_TASTLET_TXRX
ccflags-y += -DUSB_BUS_BUG
#ccflags-y += -DCONFIG_USB_URB_RX_SUBMIT_DIRECTLY
#ccflags-y += -DCONFIG_WSM_CMD_XMIT_DIRECTLY
#ccflags-y += -DCONFIG_USB_DATA_XMIT_DIRECTLY
#ccflags-y += -DCONFIG_USE_DMA_ADDR_BUFFER
#ccflags-y += -DCONFIG_USB_AGGR_URB_TX
#ccflags-y += -DCONFIG_TX_NO_CONFIRM
#ccflags-y += -DATBM_IPC_ASSOC
#ccflags-y += -DATBM_PRIVATE_IE
#ccflags-y += -DSIGMASTAR_FILTER_MACADDR_ONLY
#ccflags-y += -DCONFIG_ATBM_5G_PRETEND_2G
endif
ifeq ($(WIFI6_VARIANT_OCEANUS),y)
ccflags-y += -DATBM_WIFI6_VARIANT_OCEANUS
endif
ifeq ($(WIFI6_VARIANT_CLITE),y)
ccflags-y += -DATBM_WIFI6_VARIANT_CLITE
endif

ifeq ($(COMPAT_6062X),y)
ccflags-y += -DATBM_WIFI6_COMPAT_6062X
endif
ifeq ($(COMPAT_6162X),y)
ccflags-y += -DATBM_WIFI6_COMPAT_6162X
endif
#because COMPAT_6132_C && COMPAT_6162X is used the same firmware.bin
#so also define ATBM_WIFI6_COMPAT_6162X
ifeq ($(COMPAT_6132_C),y)
ccflags-y += -DATBM_WIFI6_COMPAT_6162X
endif

ifeq ($(COMPAT_6062_CX),y)
ccflags-y += -DATBM_WIFI6_COMPAT_6062_CX
endif

ifeq ($(TX_NO_CONFIRM),y)
#ccflags-y += -DCONFIG_USE_DMA_ADDR_BUFFER
ccflags-y += -DCONFIG_USB_AGGR_URB_TX
ccflags-y += -DCONFIG_TX_NO_CONFIRM
endif
ifeq ($(SDIO_BUS),y)
ccflags-y += -DMCAST_FWDING
ccflags-y += -DRESET_CHANGE
ccflags-y += -DSDIO_BUS
ccflags-y += -DCONFIG_SDIO_IRQ_THREAD_PROCESS_DATA
ccflags-y += -DCONFIG_ATBM_SDIO_TX_HOLD
ccflags-y += -DCONFIG_ATBM_SDIO_TX_THREAD_FIFO
ccflags-y += -DCONFIG_SDIO_THREAD_PROCESS_MAC80211_DATA
#ccflags-y += -DCONFIG_SDIO_THREAD_SEM
ccflags-y += -DCONFIG_ATBM_SDIO_IRQ_HIGH
ccflags-y += -DCONFIG_SDIO_TX_PATH_PREEMPT
#ccflags-y += -DCONFIG_ATBM_SDIO_TX_PROCESS_NORMAL
#ccflags-y += -DCONFIG_SDIO_TXRX_WORKQUEUE
#ccflags-y += -DATBM_SDIO_TXRX_ENHANCE
#ccflags-y += -DATBM_WSM_SDIO_TX_MULT
#ccflags-y += -DRESET_CHIP
endif
ifeq ($(SPI_BUS),y)
ccflags-y += -DSPI_BUS
endif
#ccflags-y += -DPS_SETUP
MODFLAGS =-DMODULE -fno-pic 
CFLAGS_MODULE =$(MODFLAGS)

ifeq ($(CONFIG_ATBM_APOLLO_DEBUGFS),y)
ccflags-y  += -DCONFIG_ATBM_APOLLO_DEBUGFS
endif
#ccflags-y  += -DCONFIG_ATBM_APOLLO_DEBUG
ccflags-y += -DCONFIG_MAC80211_ATBM_RC_DEFAULT_MINSTREL
#ccflags-y += -DMINSTREL_SAMPLE_SKIP_LOW_RATE_HT
#ccflags-y += -DMINSTREL_SAMPLE_SKIP_LOW_RATE

#ccflags-y+= -DCONFIG_MAC80211_LEDS
# ccflags-y += -DCONFIG_MAC80211_NOINLINE
# ccflags-y += -DCONFIG_MAC80211_VERBOSE_DEBUG
ccflags-y += -DCONFIG_MAC80211_ATBM_HT_DEBUG
# ccflags-y += -DCONFIG_MAC80211_TKIP_DEBUG
# ccflags-y += -DCONFIG_MAC80211_IBSS_DEBUG
# ccflags-y += -DCONFIG_MAC80211_VERBOSE_PS_DEBUG
# ccflags-y += -DCONFIG_MAC80211_VERBOSE_MPL_DEBUG
# ccflags-y += -DCONFIG_MAC80211_VERBOSE_MHWMP_DEBUG
# ccflags-y += -DCONFIG_MAC80211_DEBUG_COUNTERS
ccflags-y  += -DPROBE_RESP_EXTRA_IE
#ccflags-y  += -DIPV6_FILTERING
#ccflags-y += -DCONFIG_ATBM_APOLLO_TESTMODE
#ccflags-y += -DCONFIG_NL80211_TESTMODE
ccflags-y += -DCONFIG_MAC80211_ATBM_ROAMING_CHANGES
ifeq ($(CONFIG_MAC80211_ATBM_DEBUGFS),y)
ccflags-y  += -DCONFIG_MAC80211_ATBM_DEBUGFS
endif
ccflags-$(BLUEDROID) += -DBLUEDROID
ccflags-$(BLE) += -DCONFIG_ATBM_BLE_CDEV
$(ATBM_MODULES_NAME)-y := \
        mac80211/main.o \
        mac80211/status.o \
        mac80211/sta_info.o \
        mac80211/scan.o \
        mac80211/ht.o mac80211/he.o mac80211/agg-rx.o \
        mac80211/mlme.o mac80211/work.o \
        mac80211/iface.o \
        mac80211/cfg.o \
        mac80211/rx.o \
        mac80211/tx.o \
        mac80211/util.o \
        mac80211/wme.o \
        mac80211/chan.o \
	mac80211/wext_cfg.o		\
	mac80211/atbm_workqueue.o

$(ATBM_MODULES_NAME)-y += \
        mac80211/wep.o \
        mac80211/wpa.o \
        mac80211/wapi.o
ifeq ($(COMPAT_6132_C),y)
ifneq ($(COMPAT_6162X),y)
ifneq ($(COMPAT_6062_CX),y)
#$(ATBM_MODULES_NAME)-$(VHT) += mac80211/vht.o
endif
endif
else
$(ATBM_MODULES_NAME)-$(VHT) += mac80211/vht.o
endif
$(ATBM_MODULES_NAME)-y += mac80211/twt.o
$(ATBM_MODULES_NAME)-y += mac80211/rate.o
$(ATBM_MODULES_NAME)-y += mac80211/rc80211_hmac.o
$(ATBM_MODULES_NAME)-y += mac80211/key.o
$(ATBM_MODULES_NAME)-y += mac80211/agg-tx.o
$(ATBM_MODULES_NAME)-$(BLE) += mac80211/ble.o
#$(ATBM_MODULES_NAME)-$(BLECOEXIST) += mac80211/ble.o
$(ATBM_MODULES_NAME)-$(CUSTORMSPECIAL) += mac80211/special_filter.o
$(ATBM_MODULES_NAME)-$(SENDSPECIAL_MGMT) += mac80211/send_special_mgmt.o
$(ATBM_MODULES_NAME)-$(APSME)    += mac80211/ap_sme.o
$(ATBM_MODULES_NAME)-$(MICREPORT)    += mac80211/event.o
$(ATBM_MODULES_NAME)-$(SPECTRUMMGMT) += mac80211/spectmgmt.o
$(ATBM_MODULES_NAME)-$(IBSSENABLE) += mac80211/ibss.o
$(ATBM_MODULES_NAME)-$(P2PENABLE) += mac80211/offchannel.o
$(ATBM_MODULES_NAME)-$(BRIDGE) += mac80211/bridge.o
$(ATBM_MODULES_NAME)-$(MEM_DEBUG) += mac80211/ieee80211_atbm_mem.o
$(ATBM_MODULES_NAME)-$(SKB_DEBUG) += mac80211/ieee80211_atbm_skb.o
$(ATBM_MODULES_NAME)-$(CONFIG_MAC80211_ATBM_LEDS) += mac80211/led.o
$(ATBM_MODULES_NAME)-$(CONFIG_MAC80211_ATBM_DEBUGFS) += \
        mac80211/debugfs.o \
        mac80211/debugfs_sta.o \
        mac80211/debugfs_netdev.o \
        mac80211/debugfs_key.o

$(ATBM_MODULES_NAME)-$(CONFIG_MAC80211_ATBM_MESH) += \
        mac80211/mesh.o \
        mac80211/mesh_pathtbl.o \
        mac80211/mesh_plink.o \
        mac80211/mesh_hwmp.o

$(ATBM_MODULES_NAME)-$(CONFIG_PM) += mac80211/pm.o

$(ATBM_MODULES_NAME)-$(CONFIG_MAC80211_ATBM_DRIVER_API_TRACER) += mac80211/driver-trace.o
CFLAGS_driver-trace.o := -I$(src)
$(ATBM_MODULES_NAME)-y += \
		fwio.o \
		firmware_wifi.o \
		firmware_wifi_bt_comb_cert.o \
		txrx.o \
		queue.o \
		wsm.o \
		sta.o \
		ap.o \
		scan.o \
		main.o \
		util.o \
		internal_cmd.o offload_tcpack.o
$(ATBM_MODULES_NAME)-$(P2PENABLE) += atbm_p2p.o
$(ATBM_MODULES_NAME)-$(MODULEFS) += module_fs.o
$(ATBM_MODULES_NAME)-$(DEVCTRL) += dev_ioctl.o
$(ATBM_MODULES_NAME)-$(SMARTCONFIG) += atbm_smartconfig.o \
                                       smartconfig.o
$(ATBM_MODULES_NAME)-$(DBGEVENT) += dbg_event.o
ifeq ($(USB_BUS),y)
$(ATBM_MODULES_NAME)-y += apollo_usb.o atbm_platform.o hwio_usb.o bh_usb.o
else ifeq ($(SDIO_BUS),y)
$(ATBM_MODULES_NAME)-y += apollo_sdio.o atbm_platform.o hwio_sdio.o bh_sdio.o dcxo_dpll.o
else ifeq ($(SPI_BUS),y)
$(ATBM_MODULES_NAME)-y += apollo_spi.o atbm_platform.o hwio_spi.o bh_spi.o
endif
$(ATBM_MODULES_NAME)-$(ATBMDEBUG)  += debug.o
$(ATBM_MODULES_NAME)-$(CONFIG_PM)  += pm.o


obj-$(CONFIG_ATBM_APOLLO_WIFI6) += $(ATBM_MODULES_NAME).o

ccflags-y += -D__CHECK_ENDIAN__
MODFLAGS =-DMODULE -fno-pic
CFLAGS_MODULE =$(MODFLAGS)
