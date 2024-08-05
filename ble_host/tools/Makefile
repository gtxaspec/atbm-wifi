TOOL_NAME:=atbm_tool
TOOL_CLI_NAME:=atbm_cli
DEMO_NAME:=ble_smt
ble_tool_dir:=$(shell pwd)/ble_host/tools

NOSTDINC_FLAGS :=

$(MODULE_NAME)-y = power_func.o

obj-m = $(MODULE_NAME).o

tool_cflags += -I./ -I./hal_apollo/ -lpthread

tools_install:
	$(CROSS_COMPILE)gcc $(ble_tool_dir)/tools.c $(tool_cflags) -o $(ble_tool_dir)/$(TOOL_NAME)
	$(CROSS_COMPILE)gcc $(ble_tool_dir)/tools_cli.c $(tool_cflags) -o $(ble_tool_dir)/$(TOOL_CLI_NAME)
#	cp $(TOOL_NAME) ../
#	cp $(TOOL_CLI_NAME) ../

ble_demo:
	$(CROSS_COMPILE)gcc $(ble_tool_dir)/ble_smt_demo.c $(tool_cflags) -o $(ble_tool_dir)/$(DEMO_NAME)
#	cp $(DEMO_NAME) ../

