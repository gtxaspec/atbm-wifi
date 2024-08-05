

LOCAL_PATH:= $(call my-dir)

################## build ble tools  ###################
include $(CLEAR_VARS)
LOCAL_SRC_FILES := tools.c
LOCAL_CFLAGS += -Wno-error -Wshadow -Wpointer-arith -Wcast-qual -Winline -MMD -fPIC
LOCAL_MODULE:= atbm_tool		
LOCAL_STATIC_LIBRARIES := libcutils libm
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES) # install to system/xbin
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := tools_cli.c
LOCAL_CFLAGS += -Wno-error -Wshadow -Wpointer-arith -Wcast-qual -Winline -MMD -fPIC
LOCAL_MODULE:= atbm_cli	
LOCAL_STATIC_LIBRARIES := libcutils libm
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES) # install to system/xbin
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := ble_smt_demo.c
LOCAL_CFLAGS += -Wno-error -Wshadow -Wpointer-arith -Wcast-qual -Winline -MMD -fPIC
LOCAL_MODULE:= atbm_ble_smt
LOCAL_STATIC_LIBRARIES := libcutils libm
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES) # install to system/xbin
include $(BUILD_EXECUTABLE)
