#ifndef __COMPAT_RFKILL_H
#define __COMPAT_RFKILL_H

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31))

#include_next <linux/rfkill.h>

#if LINUX_VERSION_IS_LESS(5,11,0)

/* This should come from uapi/linux/rfkill.h, but it was much easier
 * to do it this way.
 */
enum rfkill_hard_block_reasons {
	RFKILL_HARD_BLOCK_SIGNAL        = 1 << 0,
	RFKILL_HARD_BLOCK_NOT_OWNER     = 1 << 1,
};

static inline bool rfkill_set_hw_state_reason(struct rfkill *rfkill,
					      bool blocked, unsigned long reason)
{
	return rfkill_set_hw_state(rfkill, blocked);
}

#endif /* 5.11 */


#else

#include <linux/compat-2.6.h>

#undef CONFIG_RFKILL
#undef CONFIG_RFKILL_INPUT
#undef CONFIG_RFKILL_LEDS

#ifdef CONFIG_RFKILL_BACKPORT
#define CONFIG_RFKILL 1
#endif

#ifdef CONFIG_RFKILL_BACKPORT_INPUT
#define CONFIG_RFKILL_INPUT
#endif

#ifdef CONFIG_RFKILL_BACKPORT_LEDS
#define CONFIG_RFKILL_LEDS
#endif

#include <linux/rfkill_backport.h>

#endif

#endif
