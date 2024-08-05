Import('RTT_ROOT')
Import('SDK_ROOT')
Import('rtconfig')

from building import *

cwd  = GetCurrentDir()
src = Glob('api/*.c')
src += Glob('hal/*.c')
src += Glob('hal/usb/*.c')
src += Glob('net/*.c')
src += Glob('net/wpa/*.c')
src += Glob('os/rt-thread/*.c')


CPPPATH = [cwd, str(Dir('#'))]
CPPPATH += [cwd + '/api']
CPPPATH += [cwd + '/include']
CPPPATH += [cwd + '/hal/include']
CPPPATH += [cwd + '/net/include']
CPPPATH += [cwd + '/net/include/proto']
CPPPATH += [cwd + '/os/include']
CPPPATH += [cwd + '/os/rt-thread/include']
CPPPATH += [cwd + '/hal/usb']

#CPPPATH += [SDK_ROOT + '/Libraries/inc']
#CPPPATH += [SDK_ROOT + '/components/net/api_wifi']
##marvell wifi
# 802.11n features
#
# CONFIG_STA_AMPDU_RX=y
# CONFIG_STA_AMPDU_TX=y
# CONFIG_UAP_AMPDU_RX is not set
# CONFIG_UAP_AMPDU_TX=y
myccflags = ('-w -Wall')

group = DefineGroup('atbm', src, depend = ['WIFI_USING_USBWIFI_ATBM'], CPPPATH = CPPPATH, CCFLAGS = myccflags)
#group = DefineGroup('atbm', src, depend = ['WIFI_USING_USBWIFI_ATBM'], CPPPATH = CPPPATH)

Return('group')
