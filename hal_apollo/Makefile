#CONFIG_TX_NO_CONFIRM
#CONFIG_USB_AGGR_URB_TX
#CONFIG_TX_NO_CONFIRM
#
# USB TX mode selection notes.
# If CONFIG_USB_AGGR_URB_TX is enabled, also enable CONFIG_USE_DMA_ADDR_BUFFER.
# 1) CONFIG_USB_AGGR_URB_TX + CONFIG_TX_NO_CONFIRM:
#    One URB carries multiple tx frames, no confirm required. Needs LMAC NO_NEED_CONFIRM,
#    with rate control handled in LMAC.
# 2) CONFIG_USB_AGGR_URB_TX only:
#    One URB carries multiple tx frames, confirm required. Disable LMAC NO_NEED_CONFIRM.
# 3) Neither option enabled (default mode):
#    One URB carries one tx frame, confirm required, URB uses skb buffer directly.
# 4) CONFIG_USE_DMA_ADDR_BUFFER only:
#    One URB carries one tx frame, confirm required, URB uses DMA buffer and copies from skb.
# 5) CONFIG_USE_DMA_ADDR_BUFFER + CONFIG_TX_NO_CONFIRM:
#    One URB carries one tx frame, no confirm required, URB uses DMA buffer,
#    with rate control handled in LMAC.
# 6) CONFIG_TX_NO_CONFIRM only:
#    One URB carries one tx frame, no confirm required, URB uses skb buffer directly,
#    with rate control handled in LMAC.
#
## For high throughput and stronger CPU platforms, prefer mode 1 or mode 2.
## For lower throughput and weaker CPU platforms, prefer mode 3 or mode 6.
## Default mode: 3.
#####################################################################################################################################################################################################################

ifneq ($(ATBM_BUILD_IN_KERNEL),y)
-include $(WIFI_PROJ_DIR)/.config
endif

##################   build parameterss    ################################



k-dir:=$(if $(filter /%,$(src)),$(src),$(srctree)/$(src))
NOSTDINC_FLAGS := -I$(k-dir)/../include/ \
        -include $(k-dir)/../include/linux/compat-2.6.h \
        -DCOMPAT_STATIC

#####
KBUILD_CFLAGS += -Wno-error \
	-Wno-error=vla \
	-Wno-error=unused-function

#export
ifeq ($(CONFIG_ATBM_MODULE_NAME),)
CONFIG_ATBM_MODULE_NAME = atbm_wifi
endif
ifeq ($(CONFIG_ATBM_WIFIIF1_NAME),)
CONFIG_ATBM_WIFIIF1_NAME = "wlan%d"
endif
ifeq ($(CONFIG_ATBM_WIFIIF2_NAME),)
CONFIG_ATBM_WIFIIF2_NAME = "p2p%d"
endif
ifeq ($(CONFIG_ATBM_USB_VID),)
CONFIG_ATBM_USB_VID = 0x007a
endif
ifeq ($(CONFIG_ATBM_USB_PID),)
CONFIG_ATBM_USB_PID = 0x8888
endif

ifeq ($(CONFIG_ATBM_SDIO_VID),)
CONFIG_ATBM_SDIO_VID = 0x007a
endif
ifeq ($(CONFIG_ATBM_SDIO_PID),)
CONFIG_ATBM_SDIO_PID = 0x6011
endif
ifeq ($(CONFIG_ATBM_MODULE_DRIVER_NAME),)
CONFIG_ATBM_MODULE_DRIVER_NAME = "atbm_wlan"
endif
ifeq ($(CONFIG_ATBM_PLATFORM_DEVICE_NAME),)
CONFIG_ATBM_PLATFORM_DEVICE_NAME = "atbm_dev_wifi"
endif
ifeq ($(CONFIG_ATBM_MODULE_PM_STAYAWK),)
CONFIG_ATBM_PLATFORM_DEVICE_NAME = "pm_stayawake"
endif
ifeq ($(CONFIG_ATBM_FW_NAME),)
CONFIG_ATBM_FW_NAME="fw.bin"
endif
#export
SDIO_HOST      ?= $(shell echo $(CONFIG_ATBM_SDIO_MMCx))
IF1NAME        ?= $(shell echo $(CONFIG_ATBM_WIFIIF1_NAME))
IF2NAME        ?= $(shell echo $(CONFIG_ATBM_WIFIIF2_NAME))
FW             ?= $(shell echo $(CONFIG_ATBM_FW_NAME))
MODULES_NAME   ?= $(shell echo $(CONFIG_ATBM_MODULE_NAME))
USBVID		   ?= $(shell echo $(CONFIG_ATBM_USB_VID))
USBPID		   ?= $(shell echo $(CONFIG_ATBM_USB_PID))
SDIOVID			?=$(shell echo $(CONFIG_ATBM_SDIO_VID))
SDIOPID			?=$(shell echo $(CONFIG_ATBM_SDIO_PID))
MODDRVNAME	   ?= $(shell echo $(CONFIG_ATBM_MODULE_DRIVER_NAME))
PLFDEVNAME	   ?= $(shell echo $(CONFIG_ATBM_PLATFORM_DEVICE_NAME))
MODPMSTAYAWK   ?= $(shell echo $(CONFIG_ATBM_MODULE_PM_STAYAWK))

SAEAUTHEN      ?= $(CONFIG_ATBM_FUNC_SAE_AUTHEN)
LOAD_FW_H      ?= $(CONFIG_ATBM_USE_FIRMWARE_H)
SKB_DEBUG      ?= $(CONFIG_ATBM_FUNC_SKB_DEBUG)
MEM_DEBUG      ?= $(CONFIG_ATBM_FUNC_SKB_DEBUG)
BRIDGE         ?= $(CONFIG_ATBM_SUPPORT_BRIDGE)
MONITOR        ?= $(CONFIG_ATBM_FUNC_MONITOR)
EARLYSUSPEND   ?= $(CONFIG_ATBM_FUNC_EARLYSUSPEND)
NOTXCONFIRM    ?= $(CONFIG_ATBM_FUNC_NOTXCONFIRM)
CH24CHANONLY	?= $(CONFIG_ATBM_SUPPORT_BAN_24G)
CH5GSPECIAL    ?= $(CONFIG_ATBM_SUPPORT_BAN_5G_PRETEND_24)
CH5GAND24G     ?= $(CONFIG_ATBM_SUPPORT_BAN_5G_24G)
ONLY_HT20		?= $(CONFIG_ATBM_WITHBAND_ONLY_HT20)
USBAGGTX       ?= $(CONFIG_ATBM_FUNC_USB_AGGRTX)
USBDMABUFF     ?= $(CONFIG_ATBM_FUNC_USB_DMABUFF)
USBCMDENHANCE  ?= $(CONFIG_ATBM_FUNC_USB_CMD_ENHANCE)
USBDATAENHANCE ?= $(CONFIG_ATBM_FUNC_USB_DATA_ENHANCE)
PMRELODDFW     ?= $(CONFIG_ATBM_FUNC_PS_WAKEUP_RELOAD_FW)
USB_BUS        ?= $(CONFIG_ATBM_USB_BUS)
SDIO_BUS       ?= $(CONFIG_ATBM_SDIO_BUS)
SPI_BUS        ?= $(CONFIG_ATBM_SPI_BUS)
CHECKSUM       ?= $(CONFIG_ATBM_FUNC_HW_CHSUM)
SWRATECTRL     ?= $(CONFIG_ATBM_SW_RATE_CTRL)
P2PENABLE      ?= $(CONFIG_ATBM_FUNC_P2P_ENABLE)
SWENC          ?= $(CONFIG_ATBM_FUNC_SW_ENC)
MODULEFS       ?= $(CONFIG_ATBM_FUNC_MODULE_FS)
DEVCTRL        ?= $(CONFIG_ATBM_FUNC_DEV_CTRL_API)
SMARTCONFIG    ?= $(CONFIG_ATBM_FUNC_SMARTCONFIG)
CONFIG_ATHENAB ?= $(CONFIG_ATBM601x)$(CONFIG_ATBM602x)
CONFIG_ARESB   ?= $(CONFIG_ATBM603x)
CONFIG_ASMLITE ?= $(CONFIG_ATBM6012B_y)
CONFIG_MERCURIUS ?= $(CONFIG_ATBM6132)
CONFIG_COMB		?= $(CONFIG_WIFI4_ALL_CHIP)
CONFIG_HERA    ?= $(CONFIG_ATBM6041)
NEED_SCOND_INTERFACE ?= $(CONFIG_NEED_P2P0_INTERFACE)
CUSTORMSPECIAL ?= y
SENDSPECIAL_MGMT ?= y

CONFIG_NOT_SUPPORT_40M_CHW ?= $(CONFIG_ATBM601x)
DRVLOADERFAST  ?= $(CONFIG_ATBM_FUNC_DRV_LOADER_FAST)
PRI_IE         ?= $(CONFIG_ATBM_FUNC_PRIVE_IE)
MONHDRPRISM         ?= $(CONFIG_ATBM_FUNC_MONITOR_HDR_PRISM)
PHYREG         ?= $(CONFIG_ATBM_FUNC_PHY_REG_INIT)
BASETUP        ?= $(CONFIG_ATBM_FUNC_DRIVER_SETUP_AMPDU)
BLEHOST			?=$(CONFIG_ATBM_BLE_HOST_DRIVER)
BLEPLATFORM		?=$(CONFIG_ATBM_BLE_WIFI_PLATFORM)$(CONFIG_ATBM_ONLY_BLE_WIFI_PLATFORM)
BLECOEXIST      ?=$(CONFIG_ATBM_BLE_ADV_COEXIST)
BLEMODULE		?=$(CONFIG_ATBM_BLE_HOST_DRIVER)$(CONFIG_ATBM_BLE_WIFI_PLATFORM)$(CONFIG_ATBM_ONLY_BLE_WIFI_PLATFORM)
BLECODESRAME    ?=$(CONFIG_ATBM_BLE_WIFI_PLATFORM)$(CONFIG_ATBM_ONLY_BLE_WIFI_PLATFORM)
ONLYBLEPLATFORM ?=$(CONFIG_ATBM_ONLY_BLE_WIFI_PLATFORM)
BLUEDROID	?=$(CONFIG_BLUEDROID)
BLE		?=$(CONFIG_ATBM_BLE)
##################################################
#ATBM6041 only use SDIO
ifeq ($(CONFIG_HERA),y)
SDIO_BUS=y
endif
ifeq ($(CONFIG_ATBM_SDIO_24M),y)
ifeq ($(CONFIG_ATHENAB),y)
CONFIG_ATHENAB_24M=y
CONFIG_ATHENAB=n
endif
endif
MULT_NAME=y

#export
ifeq ($(CONFIG_ATBM_APOLLO),)
CONFIG_ATBM_APOLLO=m
endif


################### WIRELESS #########################

ifeq ($(USB_BUS),y)
HIF:=usb
endif
ifeq ($(SDIO_BUS),y)
HIF:=sdio
endif
ifeq ($(SPI_BUS),y)
HIF:=spi
endif


#################   end set #############################################




#ccflags-y += -DCONFIG_ATBM_APOLLO_5GHZ_SUPPORT
#ccflags-y += -DCONFIG_ATBM_APOLLO_WAPI_SUPPORT
ccflags-y += -I$(k-dir)/../include/ \
        -include $(k-dir)/../include/linux/compat-2.6.h \
        -DCOMPAT_STATIC
## Set the calibration file path. If the file is valid, use its DCXO value;
## otherwise use the DCXO/eFuse values directly.
ATBM_BUILD_TIME = $(shell date "+%Y-%m-%d_%H-%M-%S")
ccflags-y += -DCONFIG_ATBM_BUILD_TIME=\"$(ATBM_BUILD_TIME)\"

# Default values
ATBM_CONFIG_FILE="/tmp/atbm_txpwer_dcxo_cfg.txt"
ATBM_RATE_POWER_CONFIG_FILE="/tmp/set_rate_power.txt"

# Use default value if CONFIG_ATBM_CONFIG_FILE is not set
ifndef CONFIG_ATBM_CONFIG_FILE
    CONFIG_ATBM_CONFIG_FILE := $(ATBM_CONFIG_FILE)
endif

# Use default value if CONFIG_ATBM_RATE_POWER_CONFIG_FILE is not set
ifndef CONFIG_ATBM_RATE_POWER_CONFIG_FILE
    CONFIG_ATBM_RATE_POWER_CONFIG_FILE := $(ATBM_RATE_POWER_CONFIG_FILE)
endif

ccflags-y += -DCONFIG_TXPOWER_DCXO_VALUE=\"$(CONFIG_ATBM_CONFIG_FILE)\"
ccflags-y += -DCONFIG_RATE_TXPOWER=\"$(CONFIG_ATBM_RATE_POWER_CONFIG_FILE)\"

#ccflags-y += -DATBM_USE_FASTLINK
ccflags-y += -DATBM_SAVE_BSS=\"/data/fastlink.txt\"

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
ccflags-y += $(ATBM_WIFI__EXT_CCFLAGS)
ccflags-y += -DCONFIG_ATBM_APOLLO_DISABLE_BEACON_HINTS
ccflags-y += -DOLD_RATE_POLICY=1
#ccflags-y += -DCONFIG_ATBM_SCAN_SPLIT
ccflags-y += -DCONFIG_MAC80211_BRIDGE_MULTI_PORT
ccflags-y += -DMINSTREL_RSSI_USED
ccflags-y += -DATBM_P2P_ADDR_USE_LOCAL_BIT
ccflags-y += -DATBM_WIFI_QUEUE_LOCK_BUG
ccflags-y += -DATBM_CHANGE_AP_TIMESTAMP_TO_BOOTTIME
ccflags-y += -DATBM_FUNCTION
#ccflags-y += -DATBM_USE_SAVED_FW
#ccflags-y += -DATBM_AP_SME
#ccflags-y += -DP2P_MULTIVIF
ccflags-y += -DATBM_P2P_CHANGE
ccflags-y += -DATBM_WIFI_NO_P2P_PS
ccflags-y += -DATBM_SPECIAL_PKG_DOWN_RATE
#ccflags-y += -DMCAST_FWDING
ccflags-y += -DATBM_APOLLO_SUSPEND_RESUME_FILTER_ENABLE
ccflags-y += -DCONFIG_ATBM_IOCTRL
ccflags-y += -DOPER_CLOCK_USE_SEM
ccflags-y += -DEXIT_MODULE_RESET_USB=1
ccflags-y += -DATBM_VIF_LIST_USE_RCU_LOCK
ccflags-y += -DCONFIG_ATBM_ETF_OLD
ccflags-y += -DCONFIG_ATBM_SUPPORT_REKEY

#ccflags-y += -DATBM_SUPPORT_SMARTCONFIG
#ccflags-y += -DIEEE80211_TASKLET
#ccflags-y += -DCONFIG_RATE_HW_CONTROL
ccflags-y += -DCONFIG_ATBM_SUPPORT_SG
ccflags-y += -DCONFIG_ATBM_START_WOL_TEST
#ccflags-y += -DCONFIG_ATBM_SUPPORT_COUNTRY_CODE
#add AP+STA mode , sta channel change send event to hostapd
ccflags-y += -DCONFIG_ATBM_AP_CHANNEL_CHANGE_EVENT
#ccflags-y += -DCONFIG_ATBM_SELF_WORKQUEUE
ifeq ($(platform),PLATFORM_AMLOGIC_S805)
ccflags-y += -DCONFIG_ATBM_APOLLO_USE_GPIO_IRQ
endif
ifeq ($(platform),PLATFORM_AMLOGIC)
#ccflags-y += -DCONFIG_ATBM_APOLLO_USE_GPIO_IRQ
endif
#ccflags-y += -DCONFIG_ATBM_APOLLO_USE_GPIO_IRQ
#Select GPIO23 or GPIO1 use for sdio irq
#ccflags-y += -DUSE_GPIO_23=1
ccflags-y += -DUSE_GPIO_1=1

#ccflags-y += -DCONFIG_ATBM_GET_GPIO4

#ccflags-y += -DATBM_PKG_REORDER
ccflags-y += -DATBM_SUPPORT_WIDTH_40M
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
#ccflags-y += -DCONFIG_ATBM_SA_QUERY


ccflags-y += -DCONFIG_ATBM_IWPRIV_USELESS
ccflags-y += -DCONFIG_ATBM_SUPPORT_AP_CONFIG
#ccflags-y += -DCONFIG_ATBM_STA_DYNAMIC_PS
ccflags-y += -DCONFIG_ATBM_SUPPORT_REALTIME_CHANGE_MAC
ccflags-y += -DCONFIG_LMAC_RATECTRL_RATE_BUG_FIX
ccflags-y += -DAP_MODE_SEND_PROBE_REQ
#ccflags-y += -DAP_MODE_SUPPORT_SLEEP
ccflags-y += -DCONFIG_ATBM_DRIVER_PROCESS_BA
#ccflags-y += -DATBM_ANKER_WTD
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
ccflags-$(ONLYBLEPLATFORM) += -DCONFIG_ATBM_ONLY_WIFI_BLE_PLATFORM
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
ccflags-$(CH5GSPECIAL) += -DCONFIG_ATBM_5G_PRETEND_2G
ccflags-$(CH5GAND24G) += -DCONFIG_ATBM_APOLLO_5GHZ_SUPPORT
ccflags-$(CH24CHANONLY) += -DCONFIG_ATBM_APOLLO_24GHZ_ONLY_SUPPORT
####################################################################
#CONFIG_ATBM_SUPPORT_P2P
#enable p2p function
###################################################################
ccflags-$(P2PENABLE) += -DCONFIG_ATBM_SUPPORT_P2P
####################################################################
#rate ctrl select
#hw or sw
####################################################################
ifneq ($(SWRATECTRL),y)
ccflags-y += -DCONFIG_RATE_HW_CONTROL
else
CONFIG_MAC80211_ATBM_RC_MINSTREL = y
CONFIG_MAC80211_ATBM_RC_MINSTREL_HT = y
endif
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
ccflags-y += -DATBM_PRODUCT_TEST_USE_FEATURE_ID

####################################################################
# enable phy register initial
####################################################################
#ccflags-$(PHYREG) += -DCONFIG_ATBM_PHY_REG_INIT
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
ifeq ($(CONFIG_ARES),y)
ccflags-y += -DPROJ_TYPE=ARES_A
endif

########## ARESB ############################
ifeq ($(CONFIG_ARESB),y)
ccflags-y += -DPROJ_TYPE=ARES_B
ccflags-$(CHECKSUM) += -DCHKSUM_HW_SUPPORT
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_24M
ccflags-y += -DCONFIG_SUPPORT_ARESB_FIRMWARE_H_
endif
######## ASMLITE 6012B-X/Y 6032-X ###########
ifeq ($(CONFIG_ASMLITE),y)
ccflags-y += -DPROJ_TYPE=ARES_B
ccflags-$(CHECKSUM) += -DCHKSUM_HW_SUPPORT
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_24M
ccflags-y += -DCONFIG_SUPPORT_ASMLITE_FIRMWARE_H_
endif
######### Mercurius ####################
ifeq ($(CONFIG_MERCURIUS),y)
ccflags-y += -DPROJ_TYPE=ARES_B
ccflags-$(CHECKSUM) += -DCHKSUM_HW_SUPPORT
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_24M
ccflags-y += -DCONFIG_SUPPORT_MERCURIUS_FIRMWARE_H_
endif
#########ARESB + ASMLITE ####################
ifeq ($(CONFIG_COMB),y)
ccflags-y += -DPROJ_TYPE=ARES_B
ccflags-$(CHECKSUM) += -DCHKSUM_HW_SUPPORT
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_24M
ccflags-y += -DCONFIG_SUPPORT_WIFI4_ALL_FIRMWARE_H_
endif
#############################################


ifeq ($(CONFIG_HERA),y)
ccflags-y += -DPROJ_TYPE=HERA
ccflags-$(CHECKSUM) += -DCHKSUM_HW_SUPPORT
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_24M
endif

ifeq ($(CONFIG_ATHENAB),y)
ccflags-y += -DPROJ_TYPE=ATHENA_B
ifeq ($(SDIO_BUS),y)
ccflags-y += -DATBM_SDIO_PATCH
ccflags-y += -DCONFIG_TX_NO_CONFIRM
endif
endif
ifeq ($(CONFIG_FPGA),y)
#ccflags-y += -DDCXO_USE_SMU_REG
ccflags-y += -DSDIO_CHANGE=1
#ccflags-y += -DRESET_CHANGE
#ccflags-y += -DTEST_DCXO_CONFIG=1
endif
#if TEST_DCXO_CONFIG is not config,DCXO_TYPE is not used
#if TEST_DPLL_CONFIG is not config,DPLL_CLOCK is not used
ifeq ($(CONFIG_1601),y)
ccflags-y += -DDCXO_USE_SMU_REG
ccflags-y += -DSDIO_CHANGE=0
ccflags-y += -DTEST_DCXO_CONFIG
ccflags-y += -DTEST_DPLL_CONFIG
ccflags-y += -DCHIP_V1601=1
ccflags-y += -DPROJ_TYPE=APOLLO_1601
ccflags-y += -DDCXO_TYPE=CRYSTAL_MODE
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_40M
endif
ifeq ($(CONFIG_1606),y)
ccflags-y += -DDCXO_USE_SMU_REG
ccflags-y += -DSDIO_CHANGE=0
ccflags-y += -DTEST_DCXO_CONFIG
ccflags-y += -DTEST_DPLL_CONFIG
ccflags-y += -DCHIP_V1601=0
ccflags-y += -DPROJ_TYPE=APOLLO_1606
ccflags-y += -DDCXO_TYPE=CRYSTAL_MODE
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_40M
endif
ifeq ($(CONFIG_APOLLOC),y)
#ccflags-y += -DDCXO_USE_SMU_REG
#ccflags-y += -DTEST_DCXO_CONFIG
#ccflags-y += -DTEST_DPLL_CONFIG
ccflags-y += -DCHIP_V1601=0
ccflags-y += -DSDIO_CHANGE=1
ccflags-y += -DPROJ_TYPE=APOLLO_C
ccflags-y += -DDCXO_TYPE=CRYSTAL_MODE
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_40M
#ccflags-y += -DCONFIG_ATBM_APOLLO_SUPPORT_SGI
endif

ifeq ($(CONFIG_APOLLOD),y)
ccflags-y += -DDCXO_USE_SMU_REG
ccflags-y += -DTEST_DCXO_CONFIG
ccflags-y += -DTEST_DPLL_CONFIG
ccflags-y += -DCHIP_V1601=0
ccflags-y += -DSDIO_CHANGE=1
ccflags-y += -DPROJ_TYPE=APOLLO_C
ccflags-y += -DDCXO_TYPE=CRYSTAL_MODE
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_40M
#ccflags-y += -DCONFIG_ATBM_APOLLO_SUPPORT_SGI
endif
ifeq ($(CONFIG_APOLLOE),y)
ccflags-y += -DCHIP_V1601=0
ccflags-y += -DSDIO_CHANGE=1
ccflags-y += -DPROJ_TYPE=APOLLO_E
ccflags-y += -DCONFIG_ATBM_APOLLO_SUPPORT_SGI
endif
ifeq ($(CONFIG_ATHENAB_24M),y)
ccflags-y += -DDCXO_USE_SMU_REG
ccflags-y += -DTEST_DCXO_CONFIG
ccflags-y += -DTEST_DPLL_CONFIG
ccflags-y += -DPROJ_TYPE=ATHENA_B
ccflags-y += -DDCXO_TYPE=CRYSTAL_MODE
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_24M
ccflags-y += -DATBM_SDIO_PATCH
ccflags-y += -DCONFIG_TX_NO_CONFIRM

endif
ifeq ($(CONFIG_ATHENALITE),y)
ccflags-y += -DCHKSUM_HW_SUPPORT
ccflags-y += -DCONFIG_USB_AGGR_URB_TX
ccflags-y += -DCONFIG_USE_DMA_ADDR_BUFFER
ccflags-y += -DPROJ_TYPE=ATHENA_LITE
endif
ifeq ($(CONFIG_ATHENALITE_ECO),y)
ccflags-y += -DCHKSUM_HW_SUPPORT
ccflags-y += -DCONFIG_USB_AGGR_URB_TX
ccflags-y += -DCONFIG_USE_DMA_ADDR_BUFFER
ccflags-y += -DPROJ_TYPE=ATHENA_LITE_ECO
endif
ifeq ($(CONFIG_ARES_24M),y)
#ccflags-y += -DDCXO_USE_SMU_REG
ccflags-y += -DTEST_DCXO_CONFIG
ccflags-y += -DTEST_DPLL_CONFIG
ccflags-y += -DPROJ_TYPE=ARES_A
ccflags-y += -DDCXO_TYPE=CRYSTAL_MODE
ccflags-y += -DDPLL_CLOCK=DPLL_CLOCK_24M
endif

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
ifeq ($(TX_NO_CONFIRM),y)
#ccflags-y += -DCONFIG_USE_DMA_ADDR_BUFFER
ccflags-y += -DCONFIG_USB_AGGR_URB_TX
ccflags-y += -DCONFIG_TX_NO_CONFIRM
endif
ifeq ($(SDIO_BUS),y)
ccflags-y += -DMCAST_FWDING
ccflags-y += -DRESET_CHANGE
ccflags-y += -DSDIO_BUS
#ccflags-y += -DCONFIG_SDIO_IRQ_THREAD_PROCESS_DATA
ccflags-y += -DCONFIG_ATBM_SDIO_TX_HOLD
ccflags-y += -DCONFIG_ATBM_SDIO_TX_THREAD_FIFO
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
# ccflags-y += -DCONFIG_MAC80211_ATBM_HT_DEBUG
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
ccflags-$(CONFIG_MAC80211_ATBM_RC_MINSTREL) += -DCONFIG_MAC80211_ATBM_RC_MINSTREL
ccflags-$(CONFIG_MAC80211_ATBM_RC_MINSTREL_HT) += -DCONFIG_MAC80211_ATBM_RC_MINSTREL_HT
$(ATBM_MODULES_NAME)-y := \
        mac80211/main.o \
        mac80211/status.o \
        mac80211/sta_info.o \
        mac80211/scan.o \
        mac80211/ht.o mac80211/agg-rx.o \
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

$(ATBM_MODULES_NAME)-$(SWENC) += \
        mac80211/wep.o \
        mac80211/wpa.o \
        mac80211/wapi.o \
        mac80211/tkip.o \
        mac80211/michael.o \
        mac80211/aes_ccm.o \
        mac80211/aes_cmac.o
$(ATBM_MODULES_NAME)-y += mac80211/key.o
$(ATBM_MODULES_NAME)-y += mac80211/agg-tx.o
$(ATBM_MODULES_NAME)-y += mac80211/regdb.o
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
$(ATBM_MODULES_NAME)-$(SWRATECTRL) += mac80211/rate.o
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

# objects for PID algorithm
rc80211_pid-$(CONFIG_MAC80211_ATBM_DEBUGFS) := mac80211/rc80211_pid_algo.o
rc80211_pid-$(CONFIG_MAC80211_ATBM_DEBUGFS) += mac80211/rc80211_pid_debugfs.o

rc80211_minstrel-y := mac80211/rc80211_minstrel.o
rc80211_minstrel-$(CONFIG_MAC80211_ATBM_DEBUGFS) += mac80211/rc80211_minstrel_debugfs.o

rc80211_minstrel_ht-y := mac80211/rc80211_minstrel_ht.o
rc80211_minstrel_ht-$(CONFIG_MAC80211_ATBM_DEBUGFS) += mac80211/rc80211_minstrel_ht_debugfs.o

$(ATBM_MODULES_NAME)-$(CONFIG_MAC80211_ATBM_RC_PID) += $(rc80211_pid-y)
$(ATBM_MODULES_NAME)-$(CONFIG_MAC80211_ATBM_RC_MINSTREL) += $(rc80211_minstrel-y)
$(ATBM_MODULES_NAME)-$(CONFIG_MAC80211_ATBM_RC_MINSTREL_HT) += $(rc80211_minstrel_ht-y)
#		firmware_mercurius_wifi.o s
$(ATBM_MODULES_NAME)-y += \
		fwio.o \
		firmware_wifi.o \
		firmware_wifi_bt_comb.o \
		firmware_lite_wifi.o \
		firmware_wifi_mercurius_bt_comb.o \
		firmware_mercurius_wifi.o \
		txrx.o \
		queue.o \
		wsm.o \
		sta.o \
		ap.o \
		scan.o \
		main.o \
		util.o \
		internal_cmd.o
$(ATBM_MODULES_NAME)-$(P2PENABLE) += atbm_p2p.o
$(ATBM_MODULES_NAME)-$(MODULEFS) += module_fs.o
$(ATBM_MODULES_NAME)-$(DEVCTRL) += dev_ioctl.o
$(ATBM_MODULES_NAME)-$(SMARTCONFIG) += atbm_smartconfig.o \
                                       smartconfig.o
$(ATBM_MODULES_NAME)-$(DBGEVENT) += dbg_event.o
$(ATBM_MODULES_NAME)-y += phy_reg.o
ifeq ($(USB_BUS),y)
$(ATBM_MODULES_NAME)-y += apollo_usb.o atbm_platform.o hwio_usb.o bh_usb.o
else ifeq ($(SDIO_BUS),y)
$(ATBM_MODULES_NAME)-y += apollo_sdio.o atbm_platform.o hwio_sdio.o bh_sdio.o dcxo_dpll.o
else ifeq ($(SPI_BUS),y)
$(ATBM_MODULES_NAME)-y += apollo_spi.o atbm_platform.o hwio_spi.o bh_spi.o
endif
$(ATBM_MODULES_NAME)-$(ATBMDEBUG)  += debug.o
$(ATBM_MODULES_NAME)-$(CONFIG_PM)  += pm.o


obj-$(CONFIG_ATBM_APOLLO) += $(ATBM_MODULES_NAME).o

ccflags-y += -D__CHECK_ENDIAN__
MODFLAGS =-DMODULE -fno-pic
CFLAGS_MODULE =$(MODFLAGS)

