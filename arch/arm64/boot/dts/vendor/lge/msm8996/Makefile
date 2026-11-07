# SPDX-License-Identifier: GPL-2.0-only
dtc_cpp_flags += -include $(objtree)/include/generated/autoconf.h

# Preserve the board revision IDs expected by the LGE bootloader.
ifeq ($(CONFIG_MACH_MSM8996_LUCYE_GLOBAL_COM),y)
lucye-dts := $(sort $(wildcard $(srctree)/$(src)/lge/g6/lucye_global_com/*.dts))
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(lucye-dts)))

$(obj)/%.dtb: $(src)/lge/g6/lucye_global_com/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

always := $(dtb-y)
clean-files := *.dtb
