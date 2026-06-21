/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_SOC_QCOM_DIRTYSANTA_G6_H
#define _LINUX_SOC_QCOM_DIRTYSANTA_G6_H

#include <linux/init.h>
#include <linux/types.h>

#ifdef CONFIG_DIRTYSANTA_FIXUP_G6
void __init dirtysanta_g6_fixup_cmdline(char *cmdline, size_t size);
#else
static inline void dirtysanta_g6_fixup_cmdline(char *cmdline, size_t size) { }
#endif

#endif
