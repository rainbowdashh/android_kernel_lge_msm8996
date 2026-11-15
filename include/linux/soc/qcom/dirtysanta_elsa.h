/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_SOC_QCOM_DIRTYSANTA_ELSA_H
#define _LINUX_SOC_QCOM_DIRTYSANTA_ELSA_H

#include <linux/init.h>
#include <linux/kconfig.h>

#if IS_ENABLED(CONFIG_DIRTYSANTA_FIXUP)
void __init dirtysanta_elsa_fixup_cmdline(char *cmdline);
#else
static inline void dirtysanta_elsa_fixup_cmdline(char *cmdline) { }
#endif

#endif
