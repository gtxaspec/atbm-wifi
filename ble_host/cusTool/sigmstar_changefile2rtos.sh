echo ">useage : sh sgmstar_changefile.sh  filename"
echo ">example: "
echo "         sh sgmstar_changefile.sh  atbm_wifi_40M_svn767_6"
echo ---------------------------------------------
echo Building sigmstar patch code
echo ---------------------------------------------
echo Targets: altobeam hmac wifi
echo ---------------------------------------------


echo ----------------copy sigmastar_wifi\-------------
echo ----file name $1-----------------
rm -rf sigmastar_wifi
echo cp -rf $1 sigmastar_wifi
cp -rf $1 sigmastar_wifi

echo ----------------del sigmastar_wifi\.svn-------------
find sigmastar_wifi -type d -name ".svn"|xargs rm -rf

echo ========================change file content start==============================
echo ----------------sed sigmastar_wifi\\altobeam-------------
grep altobeam -rl sigmastar_wifi |xargs sed -i "s/altobeam/sigmastar/g"

echo ----------------sed sigmastar_wifi\\atbm601x-------------
grep atbm601x -rl sigmastar_wifi |xargs sed -i "s/atbm601x/ssw101x/g"

echo ----------------sed sigmastar_wifi\\ATBM601x-------------
grep ATBM601x -rl sigmastar_wifi |xargs sed -i "s/ATBM601x/SSW101x/g"

echo ----------------sed sigmastar_wifi\\atbm602x-------------
grep atbm602x -rl sigmastar_wifi |xargs sed -i "s/atbm602x/ssw101/g"

echo ----------------sed sigmastar_wifi\\ATBM602x-------------
grep ATBM602x -rl sigmastar_wifi |xargs sed -i "s/ATBM602x/SSW101/g"

echo ----------------sed sigmastar_wifi\\atbm603x-------------
grep atbm603x -rl sigmastar_wifi |xargs sed -i "s/atbm603x/ssw101b/g"

echo ----------------sed sigmastar_wifi\\ATBM603x-------------
grep ATBM603x -rl sigmastar_wifi |xargs sed -i "s/ATBM603x/SSW101B/g"

echo ----------------sed sigmastar_wifi\\ATBM603x-------------
grep Atbm -rl sigmastar_wifi |xargs sed -i "s/Atbm/Sstar/g"

echo ----------------sed sigmastar_wifi\\USB_CLASS_VENDOR_SPEC-------------
grep USB_CLASS_VENDOR_SPEC -rl sigmastar_wifi |xargs sed -i "s/USB_CLASS_VENDOR_SPEC/SS_USB_CLASS_VENDOR_SPEC/g"

echo ----------------sed sigmastar_wifi\\LWIP_DEBUGF-------------
grep LWIP_DEBUGF -rl sigmastar_wifi |xargs sed -i "s/LWIP_DEBUGF/SSN_DEBUGF/g"

echo ----------------sed sigmastar_wifi\\NETIF_DEBUG-------------
grep NETIF_DEBUG -rl sigmastar_wifi |xargs sed -i "s/NETIF_DEBUG/SSN_NETIF_DBGF/g"


echo ----------------sed sigmastar_wifi\\atbm-------------
grep atbm -rl sigmastar_wifi |xargs sed -i "s/atbm/Sstar/g"
echo ----------------sed sigmastar_wifi\\ATBM-------------
grep ATBM -rl sigmastar_wifi |xargs sed -i "s/ATBM/SSTAR/g"
echo ----------------sed sigmastar_wifi\\ALTOBEAM-------------
grep ALTOBEAM -rl sigmastar_wifi |xargs sed -i "s/ALTOBEAM/SIGMASTAR/g"
echo ========================change file content end==============================
echo ========================change file name start==============================
#for file in $(ls  sigmastar_wifi/*/*.c)
#do 
#	echo $file
#	sed 's/atbm/sgmstar/2' $file 
#	echo $newfile 
#done
find sigmastar_wifi/hal -name "*atbm*"| xargs -i echo mv {} {} | sed 's/atbm/Sstar/2' | sh
find sigmastar_wifi/include -name "*atbm*"| xargs -i echo mv {} {} | sed 's/atbm/Sstar/2' | sh
find sigmastar_wifi/net -name "*atbm*"| xargs -i echo mv {} {} | sed 's/atbm/Sstar/2' | sh
find sigmastar_wifi/net/include -name "*atbm*"| xargs -i echo mv {} {} | sed 's/atbm/Sstar/2' | sh
find sigmastar_wifi/net/proto -name "*atbm*"| xargs -i echo mv {} {} | sed 's/atbm/Sstar/2' | sh
find sigmastar_wifi -name "*atbm*"| xargs -i echo mv {} {} | sed 's/atbm/Sstar/2' | sh
find sigmastar_wifi -name "*ATBM*"| xargs -i echo mv {} {} | sed 's/ATBM/SSTAR/2' | sh
echo " "
echo "#########             success            ###########"


