#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance

# with the License.  You may obtain a copy of the License at
#  *  http://www.apache.org/licenses/LICENSE-2.0
#  * Unless required by applicable law or agreed to in writing,
#  software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

# Toolchain commands
CROSS_COMPILE ?= /wifi_prj/staff/zhouzhanchao/android4_4_SIN/Source/lichee/brandy/gcc-linaro/bin/arm-linux-gnueabi-
CC      := $(CROSS_COMPILE)gcc 
CXX     := $(CROSS_COMPILE)g++
LD      := $(CROSS_COMPILE)gcc
AR      := $(CROSS_COMPILE)ar
AS      := $(CROSS_COMPILE)as
NM      := $(CROSS_COMPILE)nm
OBJDUMP := $(CROSS_COMPILE)objdump
OBJCOPY := $(CROSS_COMPILE)objcopy
SIZE    := $(CROSS_COMPILE)size
CONFIG_LINUX_BLE_STACK_APP = y
CONFIG_LINUX_BLE_STACK_LIB = n
# Configure NimBLE variables
#NIMBLE_ROOT := nimble_v42
ifeq ($(PLAT),)
BLEHOST = $(shell pwd)
else
BLEHOST = $(shell pwd)/ble_host
endif
NIMBLE_ROOT = $(BLEHOST)/nimble_v42
include $(NIMBLE_ROOT)/Makefile_host.include

NIMBLE_CFG_TINYCRYPT := 1

# Skip files that don't build for this port
NIMBLE_IGNORE := $(NIMBLE_ROOT)/porting/nimble/src/hal_timer.c \
	$(NIMBLE_ROOT)/porting/nimble/src/os_cputime.c \
	$(NIMBLE_ROOT)/porting/nimble/src/os_cputime_pwr2.c \
	$(NULL)


SRC := 

# Source files for NPL OSAL
SRC += \
	$(wildcard $(BLEHOST)/os/linux_app/src/*.c) \
	$(wildcard $(BLEHOST)/os/linux_app/src/*.cc) \
	$(wildcard $(BLEHOST)/nimble/transport/socket/src/*.c) \
	$(TINYCRYPT_SRC) \
	$(NULL)


SRC += $(BLEHOST)/user_app/ble_smart_cfg/ble_smart_cfg.c \
	$(BLEHOST)/user_app/ble_smart_cfg/ble_smart_demo.c

SRC += $(BLEHOST)/user_app/ble_adv_cfg/ble_adv_cfg.c


# Add NPL and all NimBLE directories to include paths
INC = \
    $(BLEHOST)/include \
	$(BLEHOST)/os/linux_app/include \
	$(NIMBLE_ROOT)/nimble/transport/socket/include \
	$(NIMBLE_INCLUDE) \
	$(TINYCRYPT_INCLUDE) \
	$(NULL)

INCLUDES := $(addprefix -I, $(INC))
INCLUDES += $(NIMBLE_INCLUDE_PATH)
INCLUDES += -I user_app/ble_smart_cfg

SRC_C  = $(filter %.c,  $(SRC))
SRC_CC = $(filter %.cc, $(SRC))

OBJ := $(SRC_C:.c=.o)
OBJ += $(SRC_CC:.cc=.o)
OBJ += $(LIB_NIMBLE_SRC)

TINYCRYPT_OBJ := $(TINYCRYPT_SRC:.c=.o)

CFLAGS =                    \
    $(NIMBLE_CFLAGS)        \
    $(INCLUDES)             \
    -g                      \
    -D_GNU_SOURCE   -Os     \
    -static    -fPIC      	    \
    $(NULL)

ifeq ($(CONFIG_LINUX_BLE_STACK_APP), y)
CFLAGS +=  -DCONFIG_LINUX_BLE_STACK_APP=1
endif

ifeq ($(CONFIG_LINUX_BLE_STACK_LIB), y)
CFLAGS +=  -DCONFIG_LINUX_BLE_STACK_LIB=1
endif

CFLAGS +=  -DCONFIG_BLE_PTS_TEST_MOD=0

#CFLAGS +=  -DCONFIG_BLE_RECV_DATA_HIGHT_LAYER

LIBS := $(NIMBLE_LDFLAGS) -lstdc++ -lrt -lpthread 

.PHONY: all clean
.DEFAULT: all

all: nimble_linux

ifeq ($(CONFIG_LINUX_BLE_STACK_LIB), y)
ble_stack: nimble_linux_lib
else
ble_stack: nimble_linux
endif

ble_stack_clean:clean

clean:
	rm $(OBJ) -f
	rm nimble_linux -f

$(TINYCRYPT_OBJ): CFLAGS+=$(TINYCRYPT_CFLAGS)

%.o: %.c
	$(CC) -c $(INCLUDES) $(CFLAGS) -o $@ $<

%.o: %.cc
	$(CXX) -c $(INCLUDES) $(CFLAGS) -o $@ $<

nimble_linux: $(OBJ) $(TINYCRYPT_OBJ)
	$(LD) -o $@ $^ $(LIBS)
	$(SIZE) $@
	$(OBJDUMP) -d -S nimble_linux > nimble_linux.S
	$(CROSS_COMPILE)strip nimble_linux --strip-unneeded
	cp -rf nimble_linux  $(BLEHOST)/../driver_install

nimble_linux_lib: $(OBJ) $(TINYCRYPT_OBJ)
	$(AR) rc libnimble_linux.a $(OBJ)
	cp -rf libnimble_linux.a  $(BLEHOST)/../driver_install
	
strip:
	$(CROSS_COMPILE)strip nimble_linux --strip-unneeded
	cp -rf nimble_linux $(BLEHOST)/../driver_install