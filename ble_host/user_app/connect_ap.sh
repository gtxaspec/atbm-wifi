#!/bin/sh




write_wpa_conf()
{

if [ "$2" == "" ];then
	echo -e "
ctrl_interface=/var/run/wpa_supplicant\n
ap_scan=1\n
network={\n
\tssid=\"$1\"\n
\tkey_mgmt=NONE\n
\tscan_ssid=1\n
	}\n
	" > /tmp/wpa_supplicant.conf
else
	echo -e "
ctrl_interface=/var/run/wpa_supplicant\n
ap_scan=1\n
network={\n
\tssid=\"$1\"\n
\tpsk=\"$2\"\n
\tscan_ssid=1\n
	}\n
	" > /tmp/wpa_supplicant.conf
fi
}






connect_ap()
{

	write_wpa_conf $1 $2
	if [ -f "/tmp/wpa_supplicant.pid" ];then
		wpa_s_pid=`cat /tmp/wpa_supplicant.pid`
		kill ${wpa_s_pid}
		#echo "==========>>wpa_s_pid=${wpa_s_pid}"
		
		sleep 1
	else
		killall wpa_supplicant
		sleep 1
	fi
	if [ -f "/tmp/udhcpc.pid" ];then
		udhcpc_pid=`cat /tmp/udhcpc.pid`
		kill ${udhcpc_pid}
	else
		killall udhcpc
	fi
	wpa_supplicant -Dnl80211 -iwlan0 -c /tmp/wpa_supplicant.conf -P /tmp/wpa_supplicant.pid -B
	udhcpc -iwlan0 -p /tmp/udhcpc.pid &
}



connect_ap $1 $2
