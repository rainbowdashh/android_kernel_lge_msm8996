/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_SOC_QCOM_DIRTYSANTA_H
#define _LINUX_SOC_QCOM_DIRTYSANTA_H

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>

static inline void __init dirtysanta_fixup_boot_state(char *cmdline, size_t size)
{
	static const char key[] = "androidboot.verifiedbootstate=";
	static const char replacement[] = "orange";
	char *arg = cmdline, *value;
	size_t len;

	/* The engineering bootloader reports red despite permitting our kernel. */
	while ((arg = strstr(arg, key))) {
		value = arg + sizeof(key) - 1;
		if ((arg == cmdline || arg[-1] == ' ') &&
		    !strncmp(value, "red", 3) &&
		    (value[3] == ' ' || value[3] == '\0')) {
			len = strlen(cmdline);
			if (len + sizeof(replacement) - 1 - 3 >= size) {
				pr_warn("DirtySanta: no room to normalize boot state\n");
				return;
			}
			memmove(value + sizeof(replacement) - 1, value + 3,
				strlen(value + 3) + 1);
			memcpy(value, replacement, sizeof(replacement) - 1);
			pr_info("DirtySanta: reporting engineering boot state as orange\n");
			arg = value + sizeof(replacement) - 1;
		} else {
			arg = value;
		}
	}
}

#endif
