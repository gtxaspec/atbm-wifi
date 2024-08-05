#-------------------------------------------------------------------------------
#	Description of some variables owned by the library
#-------------------------------------------------------------------------------
# Library module (lib) or Binary module (bin)
PROCESS = lib


#PP_OPT_COMMON +=ATBM_USB_BUS=1  ATBM_PKG_REORDER=1
PATH_C +=\
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/api \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/hal \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/hal/usb \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/net \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/net/wpa \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/net/hostapd \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/os/rk_rtos \
  
PATH_H += \
    $(PATH_ahc)/inc\
    $(PATH_application_sys_utility_FileProspector_Porting)/inc\
    $(PATH_project_Dvr_CarDV_SDK_cfg_isp)/inc\
    $(PATH_project_Dvr_CarDV_SDK_cfg_core)/inc\
    $(PATH_project_Dvr_CarDV_SDK_cfg_net)/inc\
    $(PATH_project_Dvr_CarDV_SDK_cfg_sdk)/inc\
    $(PATH_project_Dvr_CarDV_SDK_UI)/inc\
    $(PATH_project_Dvr_CarDV_SDK_Menu)/inc\
    $(PATH_core_application_mmpd_flow_ctl)/inc\
    $(PATH_core_application_mmps_display)/inc\
    $(PATH_core_application_mmps_system)/inc\
    $(PATH_core_application_mmps_vid_recd)/inc\
    $(PATH_core_driver_bsp)/inc\
    $(PATH_core_driver_common)/inc\
    $(PATH_core_driver_img_pipe)/inc\
    $(PATH_core_driver_misc)/inc\
    $(PATH_core_driver_pwm)/inc\
    $(PATH_core_driver_timer)/inc\
    $(PATH_core_driver_uart)/inc\
    $(PATH_core_include_application)\
    $(PATH_core_include_display)\
    $(PATH_core_include_img_pipe)\
    $(PATH_core_include_misc)\
    $(PATH_core_lib_fs)/inc\
    $(PATH_core_lib_gui_inc_Config)\
    $(PATH_core_lib_gui_inc_Core)\
    $(PATH_core_lib_isp)/inc\
    $(PATH_core_system_buffering)/inc\
    $(PATH_core_system_host_if)/inc\
    $(PATH_core_system_misc)/inc\
    $(PATH_core_system_mm)/inc\
    $(PATH_core_system_net_api)/inc\
    $(PATH_core_system_net_arch_v4l_src)\
    $(PATH_core_system_net_arch_v4l)/inc\
    $(PATH_core_system_net_dhcp)/inc\
    $(PATH_core_system_net_libupnp_core_genlib)/inc\
    $(PATH_core_system_net_libupnp_upnp)/inc\
    $(PATH_core_system_net_lib)/inc\
    $(PATH_core_system_net_lwip_api)/inc\
    $(PATH_core_system_net_lwip_ipv4)/inc\
    $(PATH_core_system_net_lwip_netif)/inc\
    $(PATH_core_system_net_lwip_port)/inc\
    $(PATH_core_system_net_streaming_server)/inc\
    $(PATH_core_system_os)/inc\
    $(PATH_core_system_sensor)/inc\
    $(PATH_core_system_vid_play)/inc\
    $(PATH_core_utility)/inc\
    $(PATH_driver_drv_bluetooth_bt_host_arch_rtk_pif_drv)/inc\
    $(PATH_driver_drv_int_pub)\
    $(PATH_driver_drv_io_pub)\
    $(PATH_driver_drv_timer_pub)\
    $(PATH_driver_hal_infinity_int_pub)\
    $(PATH_driver_hal_infinity_io_pub)\
    $(PATH_driver_hal_infinity_kernel)/inc\
    $(PATH_driver_hal_infinity_timer_pub)\
    $(PATH_middleware_pm_pub)\
    $(PATH_LibSourceInc_wifi_WiFi-GB9662_wlan_src_include)\
    $(PATH_LibSourceInc_wifi_WiFi-GB9662_wlan_src_include_proto)\
    $(PATH_system_MsWrapper_pub)\
    $(PATH_system_fc_pub)\
    $(PATH_system_libc)/inc\
    $(PATH_system_rtk_pub)\
    $(PATH_system_sdtarget_common_include)\
    $(PATH_system_sdtarget_wintarget)/inc\
    $(PATH_system_sys_pub)\
    $(PATH_core_system_net_wifi)/inc\
    $(PATH_core_system_net_lwip_api_inc_lwip)\
    $(PATH_core_system_net_lwip_api)/inc\
    $(PATH_core_system_net_lwip_ipv4)/inc\
    $(PATH_core_system_net_lwip_netif_inc_netif)\
    $(PATH_core_system_net_lwip_netif)/inc\
    $(PATH_core_system_net_lwip_port)/inc\
    
    
PATH_H +=\
	$(PATH_project_Dvr_CarDV_SDK_cfg_fs)/inc\
	$(PATH_project_Dvr_CarDV_SDK_cfg_usb)/inc\
	$(PATH_core_application_component)/inc\
	$(PATH_core_include_application)\
	$(PATH_core_include_img_pipe)\
	$(PATH_core_include_usb)\
	$(PATH_core_driver_usb)/inc\
	$(PATH_core_system_comp_ctl)/inc\
	$(PATH_core_system_usb_dev)/uvc/inc\
  $(PATH_core_system_usb_dev)/vendor/inc\
	$(PATH_core_system_usb_host)/inc\
	$(PATH_core_system_fs)/inc\
	$(PATH_core_lib_fs)/inc\
	$(PATH_core_lib_gui_inc_Core)\
	$(PATH_core_utility)/inc\
	$(PATH_usbhost)/pub\
	$(PATH_usbhost)/inc/include\
	$(PATH_utopia)/common/inc\
  $(PATH_utopia)/msos/inc\
  $(PATH_usbhost_hal)/inc\
  $(PATH_MsWrapper)/pub\
  $(PATH_core_system_os)/inc\
  $(PATH_core_system_host_if)/inc\
  $(PATH_hostuvc_mdl)/pub\
  $(PATH_hostuvc_mdl)/inc
  
PATH_H +=\
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/api \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/include \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/hal/include \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/net/include \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/net/include/proto \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/os/include \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/os/rk_rtos/include \
  $(PATH_LibSourceInc_wifi_WiFi-ATBM6022)/hal/usb 
#-------------------------------------------------------------------------------
#	List of source files of the library or executable to generate
#-------------------------------------------------------------------------------
SRC_C_LIST =\
   atbm_api.c \
   atbm_ap.c \
   atbm_config.c \
   atbm_init.c \
   atbm_main.c \
   atbm_queue.c \
   atbm_skbuf.c \
   atbm_smartconfig.c \
   atbm_sta.c \
   atbm_task.c \
   atbm_txrx.c \
   atbm_wifi_driver_api.c \
   atbm_wsm.c \
   smartconfig.c \
   atbm_usb.c \
   atbm_usb_bh.c \
   atbm_usb_fwio.c \
   atbm_usb_hwio.c \
   app_wifi_cmd.c \
   atbm_key.c \
   atbm_mgmt.c \
   atbm_ratectrl.c \
   atbm_rc80211_pid_algo.c \
   atbm_util.c \
   aes_core.c \
   hostapd_main.c \
   sha1.c \
   wpa_common.c \
   wpa_main.c \
   wpa_timer.c \
   atbm_os_atomic.c \
   atbm_os_mem.c \
   atbm_os_mutex.c \
   atbm_os_skbuf.c \
   atbm_os_spinlock.c \
   atbm_os_thread.c \
   atbm_os_timer.c \
   atbm_os_usb.c \
   atbm_os_api.c \
   atbm_etf.c \
   atbm_os_workqueue.c 

