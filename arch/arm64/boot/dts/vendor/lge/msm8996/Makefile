# SPDX-License-Identifier: GPL-2.0-only
dtc_cpp_flags += -include $(objtree)/include/generated/autoconf.h

# Preserve the board revision IDs expected by the LGE bootloader.
ifeq ($(CONFIG_MACH_MSM8996_LUCYE_GLOBAL_COM),y)
lucye-dts := $(sort $(wildcard $(srctree)/$(src)/lge/g6/lucye_global_com/*.dts))
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(lucye-dts)))

$(obj)/%.dtb: $(src)/lge/g6/lucye_global_com/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

ifeq ($(CONFIG_MACH_MSM8996_H1_GLOBAL_COM),y)
h1-dts := $(sort $(wildcard $(srctree)/$(src)/lge/g5/h1_global/*.dts))
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(h1-dts)))

$(obj)/%.dtb: $(src)/lge/g5/h1_global/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

ifeq ($(CONFIG_MACH_MSM8996_H1_US),y)
h1-us-dts := $(sort $(wildcard $(srctree)/$(src)/lge/g5/h1_us/*.dts))
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(h1-us-dts)))

$(obj)/%.dtb: $(src)/lge/g5/h1_us/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

ifeq ($(CONFIG_MACH_MSM8996_ELSA_GLOBAL_COM),y)
elsa-dts := $(sort $(wildcard $(srctree)/$(src)/lge/v20/elsa_global_com/*.dts))
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(elsa-dts)))

$(obj)/%.dtb: $(src)/lge/v20/elsa_global_com/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

always := $(dtb-y)
clean-files := *.dtb

ifeq ($(CONFIG_MACH_MSM8996_LUCYE_NAO_US),y)
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(wildcard $(srctree)/$(src)/lge/g6/lucye_nao_us/*.dts)))
$(obj)/%.dtb: $(src)/lge/g6/lucye_nao_us/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

ifeq ($(CONFIG_MACH_MSM8996_LUCYE_TMO_US),y)
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(wildcard $(srctree)/$(src)/lge/g6/lucye_tmo_us/*.dts)))
$(obj)/%.dtb: $(src)/lge/g6/lucye_tmo_us/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

ifeq ($(CONFIG_MACH_MSM8996_ELSA_ATT_US),y)
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(wildcard $(srctree)/$(src)/lge/v20/elsa_att_us/*.dts)))
$(obj)/%.dtb: $(src)/lge/v20/elsa_att_us/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

ifeq ($(CONFIG_MACH_MSM8996_ELSA_TMO_US),y)
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(wildcard $(srctree)/$(src)/lge/v20/elsa_tmo_us/*.dts)))
$(obj)/%.dtb: $(src)/lge/v20/elsa_tmo_us/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

ifeq ($(CONFIG_MACH_MSM8996_ELSA_SPR_US),y)
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(wildcard $(srctree)/$(src)/lge/v20/elsa_spr_us/*.dts)))
$(obj)/%.dtb: $(src)/lge/v20/elsa_spr_us/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

ifeq ($(CONFIG_MACH_MSM8996_ELSA_NAO_US),y)
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(wildcard $(srctree)/$(src)/lge/v20/elsa_nao_us/*.dts)))
$(obj)/%.dtb: $(src)/lge/v20/elsa_nao_us/%.dts FORCE
	$(call if_changed_dep,dtc)
endif

ifeq ($(CONFIG_MACH_MSM8996_ELSA_VZW),y)
dtb-y += $(patsubst %.dts,%.dtb,$(notdir $(wildcard $(srctree)/$(src)/lge/v20/elsa_vzw/*.dts)))
$(obj)/%.dtb: $(src)/lge/v20/elsa_vzw/%.dts FORCE
	$(call if_changed_dep,dtc)
endif
