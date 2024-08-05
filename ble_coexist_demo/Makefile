DEMO_NAME:=ble_coex

DEMO_CFLAGS = -g -O2
DEMO_CFLAGS += -I./ -I./hal_apollo/ -lpthread
ble_coex_dir:=$(shell pwd)/ble_coexist_demo

ble_coex:
	$(CROSS_COMPILE)gcc ble_coexist_demo/ble_coexist_demo.c $(DEMO_CFLAGS) -o $(ble_coex_dir)/$(DEMO_NAME)
	#cp $(DEMO_NAME)		 $(WIFI_INSTALL_DIR)
	#rm -f $(DEMO_NAME)

