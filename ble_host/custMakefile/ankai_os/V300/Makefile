KERNELDIR ?=/wifi_prj/staff/panxuqiang/wifi_prj/Sky39EV300_PDK_V1.0.05/PDK/SDK/ak/kernel
PROJ = libwifi_atbm6031
TARGET = $(PROJ).a
TOPDIR 	?= $(shell pwd)
LIBDIR = $(KERNELDIR)/lib
chips := ak39e
LWIP_VER := 2.0.3
#include .h
INCLUDE    =	-I. \
		-Iapi \
		-Ihal \
		-Ihal/sdio \
		-Ihal/include \
		-Iinclude \
		-Inet/include \
		-Inet/include/proto \
		-Inet/wpa/crypto/include \
		-Inet/wpa/wps/include \
		-Ios/include	\
		-Ios/ankai_os/V300/include \
		-Ios/ankai_os/V300/include/ak_inc

#KERNEL
KERNEL_INCLUDE   +=   -I. \
                -I$(KERNELDIR)/include/lwip_$(LWIP_VER) \
		-I$(KERNELDIR)/include/lwip_$(LWIP_VER)/ipv4 \
		-I$(KERNELDIR)/include/driver \
		-I$(KERNELDIR)/driver/include  \
		-I$(KERNELDIR)/common  \
		-I$(KERNELDIR)/include \
		-I$(KERNELDIR)/net/wifi 

INCLUDE +=$(KERNEL_INCLUDE)
BUILDPATH  ?= build
#include .c
SRCDIR += \
		hal	\
		hal/sdio \
		net \
		net/wpa \
		net/wpa/crypto \
		net/wpa/wps \
		os/ankai_os/V300 \
		api 

CSRCS += $(foreach d, $(SRCDIR), $(wildcard $d/*.c))

COBJS += $(patsubst %.c, $(BUILDPATH)/%.o, $(CSRCS))


#build rule
.PHONY: all prepare target clean install 

all: prepare target install

prepare:
ifneq ($(BUILDPATH),)
	@for i in $(SRCDIR); \
	do mkdir -p $(BUILDPATH)/$$i; \
	done
endif

target: $(BUILDPATH)/$(TARGET)
$(BUILDPATH)/$(TARGET): $(COBJS) 
	@echo ---------------------[build out]----------------------------------	
	$(AR) -rsv $@  $(COBJS) 
	
install: 
	@if [ ! -e $(BUILDPATH)/$(TARGET) ];then\
		echo "\033[1;31m"$(TARGET) is not exist"\033[m";\
		/bin/false;	\
	fi
	@echo "\033[1;32m"install $(BUILDPATH)/$(TARGET) to $(LIBDIR)"\033[m"
	cp $(BUILDPATH)/$(TARGET) $(LIBDIR)

clean: 
	rm -rf $(BUILDPATH)


# Rules
include $(TOPDIR)/rules.mk



