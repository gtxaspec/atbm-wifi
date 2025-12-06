# SPDX-License-Identifier: GPL-2.0

# Add include path for backport compatibility headers
ccflags-y += -I$(src)/../include

#ccflags-$(CONFIG_WIFI6) += -DCONFIG_ATBM_HE
ccflags-y += -DCONFIG_ATBM_HE
ccflags-$(CONFIG_CPTCFG_CFG80211) += -DCONFIG_CPTCFG_CFG80211=1
ccflags-$(CONFIG_CPTCFG_CFG80211) += -DCPTCFG_CFG80211=1
ccflags-$(CONFIG_CPTCFG_CFG80211_WEXT) += -DCONFIG_CPTCFG_CFG80211_WEXT=1 -DCPTCFG_CFG80211_WEXT=1 -DCONFIG_WEXT_CORE=1 -DCONFIG_WEXT_PROC=1 -DCONFIG_WEXT_SPY=1 -DCONFIG_WEXT_PRIV=1
# Define debugfs unconditionally to prevent redefinition errors
ccflags-y += -DCONFIG_CPTCFG_CFG80211_DEBUGFS=1 -DCPTCFG_CFG80211_DEBUGFS=1
#CONFIG_CPTCFG_CFG80211_COUNTRY_CODE
ifneq ($(COUNTRY_CODE),)
ccflags-y += -DCONFIG_CPTCFG_CFG80211_COUNTRY_CODE
endif

obj-$(CPTCFG_CFG80211) += cfg80211.o

obj-$(CPTCFG_LIB80211) += lib80211.o
obj-$(CPTCFG_LIB80211_CRYPT_WEP) += lib80211_crypt_wep.o
obj-$(CPTCFG_LIB80211_CRYPT_CCMP) += lib80211_crypt_ccmp.o
obj-$(CPTCFG_LIB80211_CRYPT_TKIP) += lib80211_crypt_tkip.o

# WEXT support disabled - kernel provides wireless_send_event and wireless_nlevent_flush
# The driver's wext-*.c files are incompatible with kernel 4.4
# obj-$(CONFIG_WEXT_CORE) += wext-core.o
# obj-$(CONFIG_WEXT_PROC) += wext-proc.o
# obj-$(CONFIG_WEXT_SPY) += wext-spy.o
# obj-$(CONFIG_WEXT_PRIV) += wext-priv.o

cfg80211-y += core.o sysfs.o radiotap.o util.o reg.o scan.o nl80211.o
cfg80211-y += mlme.o ibss.o sme.o chan.o ethtool.o mesh.o ap.o trace.o ocb.o
cfg80211-y += pmsr.o
cfg80211-y += backport-5.2.o backport-4.18.o backport-4.7.o  backport-4.0.o compat-3.7.o backport-genetlink.o
# Disable of.o - incompatible with kernel 3.10 (uses of_prop_next_u32 which doesn't exist)
# cfg80211-$(CONFIG_OF) += of.o
cfg80211-$(CONFIG_CPTCFG_CFG80211_DEBUGFS) += debugfs.o
cfg80211-$(CONFIG_CPTCFG_CFG80211_WEXT) += wext-compat.o wext-sme.o
cfg80211-$(COUNTRY_CODE) += regdb.o

CFLAGS_trace.o := -I$(src)

# Commented out - these files require custom build rules that are incompatible with kernel 4.4
# cfg80211-$(CPTCFG_CFG80211_USE_KERNEL_REGDB_KEYS) += shipped-certs.o
# ifneq ($(CPTCFG_CFG80211_EXTRA_REGDB_KEYDIR),)
# cfg80211-y += extra-certs.o
# endif


