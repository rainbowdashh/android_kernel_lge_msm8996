// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2017, The Linux Foundation. All rights reserved.
 * MSM8996 CPU and CBF providers adapted from the LG 4.4 clock driver.
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/cpuhotplug.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/regmap.h>
#include <asm/cputype.h>
#include <asm/smp_plat.h>
#include <asm/sysreg.h>
#include <soc/qcom/scm.h>
#include <dt-bindings/clock/msm-clocks-8996.h>

#include "clk-alpha-pll.h"
#include "clk-debug.h"
#include "clk-regmap-mux.h"
#include "vdd-level-8996.h"

#define CPU_REF_RATE		19200000UL
#define CPU_PLL_MIN_RATE	600000000UL
#define CPU_PLL_MAX_RATE	3000000000UL
#define CPU_ACD_THRESHOLD	595200000UL
#define CPU_MUX_REG		0x40
#define CBF_MUX_REG		0x18

struct msm8996_cpu_domain {
	struct clk_hw hw;
	struct clk_alpha_pll pll;
	struct clk_alpha_pll alt;
	struct clk_fixed_factor main;
	struct clk_fixed_factor acd;
	struct clk_regmap_mux low;
	struct clk_vdd_class vdd;
	struct regulator *supply;
	struct regmap *mux;
	void __iomem *base;
	unsigned long *rates;
	unsigned int count;
	unsigned int index;
	unsigned int postdiv;
	unsigned int mux_reg;
	unsigned int saved_mux;
	bool has_acd;
	bool qos_active;
	bool transition;
	bool cfa_workaround;
	bool manual_pll;
	bool v3;
	bool pro;
	cpumask_t cpus;
	struct pm_qos_request qos;
	struct notifier_block nb;
};

struct msm8996_cpu_cc {
	struct msm8996_cpu_domain domain[3];
	struct clk_fixed_factor aux;
	struct clk_debug_mux debug;
	bool v3;
	bool pro;
	int hp_state;
};

static DEFINE_VDD_REGULATORS(vdd_dig, VDD_DIG_NUM, 1, vdd_corner);
static struct msm8996_cpu_cc *cpu_cc;
static DEFINE_RAW_SPINLOCK(acd_lock);

static const u8 cpu_pll_regs[PLL_OFF_MAX_REGS] = {
	[PLL_OFF_L_VAL] = 0x04,
	[PLL_OFF_ALPHA_VAL] = 0x08,
	[PLL_OFF_USER_CTL] = 0x10,
	[PLL_OFF_CONFIG_CTL] = 0x18,
	[PLL_OFF_CONFIG_CTL_U] = 0x1c,
	[PLL_OFF_TEST_CTL] = 0x20,
	[PLL_OFF_TEST_CTL_U] = 0x24,
	[PLL_OFF_STATUS] = 0x28,
};

/* The alternate PLL has 40-bit alpha and no upper configuration register. */
static const u8 cpu_alt_pll_regs[PLL_OFF_MAX_REGS] = {
	[PLL_OFF_L_VAL] = 0x04,
	[PLL_OFF_ALPHA_VAL] = 0x08,
	[PLL_OFF_ALPHA_VAL_U] = 0x0c,
	[PLL_OFF_USER_CTL] = 0x10,
	[PLL_OFF_USER_CTL_U] = 0x14,
	[PLL_OFF_CONFIG_CTL] = 0x18,
	[PLL_OFF_TEST_CTL] = 0x20,
	[PLL_OFF_TEST_CTL_U] = 0x24,
	[PLL_OFF_STATUS] = 0x28,
};

static const u8 cbf_pll_regs[PLL_OFF_MAX_REGS] = {
	[PLL_OFF_L_VAL] = 0x08,
	[PLL_OFF_ALPHA_VAL] = 0x10,
	[PLL_OFF_USER_CTL] = 0x18,
	[PLL_OFF_CONFIG_CTL] = 0x20,
	[PLL_OFF_CONFIG_CTL_U] = 0x24,
	[PLL_OFF_TEST_CTL] = 0x30,
	[PLL_OFF_TEST_CTL_U] = 0x34,
	[PLL_OFF_STATUS] = 0x28,
};

static const struct pll_vco alt_vcos[] = {
	{ 250000000, 500000000, 3 },
	{ 500000000, 750000000, 2 },
	{ 750000000, 1000000000, 1 },
	{ 1000000000, 2150400000UL, 0 },
};

static const struct parent_map low_parent_map[] = {
	{ .cfg = 0 }, { .cfg = 1 }, { .cfg = 3 },
};

static struct msm8996_cpu_domain *to_cpu_domain(struct clk_hw *hw)
{
	return container_of(hw, struct msm8996_cpu_domain, hw);
}

static int cpu_mux_write(struct msm8996_cpu_domain *d, unsigned int mask,
			 unsigned int value)
{
	int ret = regmap_update_bits(d->mux, d->mux_reg, mask, value);

	/* The mux switch must complete before changing PLL or voltage. */
	mb();
	udelay(5);
	return ret;
}

static u8 cpu_get_parent(struct clk_hw *hw)
{
	struct msm8996_cpu_domain *d = to_cpu_domain(hw);
	u32 val = 0;

	regmap_read(d->mux, d->mux_reg, &val);
	return val & 3;
}

static int cpu_set_parent(struct clk_hw *hw, u8 index)
{
	return cpu_mux_write(to_cpu_domain(hw), 3, index);
}

static int cpu_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct msm8996_cpu_domain *d = to_cpu_domain(hw);
	struct clk_hw *parent;
	unsigned int i, index;
	long rounded;

	for (i = 1; i < d->count - 1; i++)
		if (req->rate <= d->rates[i])
			break;
	req->rate = d->rates[i];
	if (d->index == 2)
		index = req->rate < CPU_PLL_MIN_RATE ? 2 : 1;
	else
		index = req->rate <= CPU_ACD_THRESHOLD ? 0 :
			(d->has_acd ? 2 : 1);
	parent = clk_hw_get_parent_by_index(hw, index);
	if (!parent)
		return -EINVAL;
	rounded = clk_hw_round_rate(parent, req->rate);
	if (rounded < 0)
		return rounded;
	req->rate = req->best_parent_rate = rounded;
	req->best_parent_hw = parent;
	return 0;
}

static const struct clk_ops cpu_ops = {
	.debug_init = clk_debug_measure_add,
	.get_parent = cpu_get_parent,
	.set_parent = cpu_set_parent,
	.determine_rate = cpu_determine_rate,
};

/* The primary PLL is integer-N, with a 600 MHz to 3 GHz VCO range. */
static long cpu_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	rate = clamp(rate, CPU_PLL_MIN_RATE, CPU_PLL_MAX_RATE);
	return min(CPU_PLL_MAX_RATE, DIV_ROUND_UP(rate, CPU_REF_RATE) * CPU_REF_RATE);
}

static int cpu_pll_program(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_alpha_pll *pll = container_of(to_clk_regmap(hw), struct clk_alpha_pll, clkr);
	struct msm8996_cpu_domain *d = container_of(pll, struct msm8996_cpu_domain, pll);
	unsigned int test_reg = pll->regs[PLL_OFF_TEST_CTL];
	u32 status;
	int ret;

	if (rate != cpu_pll_round_rate(hw, rate, &parent_rate))
		return -EINVAL;
	if (d->cfa_workaround) {
		ret = regmap_update_bits(pll->clkr.regmap, test_reg, BIT(16), BIT(16));
		if (ret)
			return ret;
	}
	udelay(1);
	ret = regmap_write(pll->clkr.regmap, pll->regs[PLL_OFF_L_VAL], rate / CPU_REF_RATE);
	if (d->cfa_workaround)
		regmap_update_bits(pll->clkr.regmap, test_reg, BIT(16), 0);
	if (ret)
		return ret;
	/* Ignore transient lock indications for the first 50 microseconds. */
	udelay(50);
	return readl_poll_timeout_atomic(d->base, status, status & BIT(31), 1, 500);
}

static int cpu_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_alpha_pll *pll = container_of(to_clk_regmap(hw), struct clk_alpha_pll, clkr);
	struct msm8996_cpu_domain *d = container_of(pll, struct msm8996_cpu_domain, pll);
	int ret;

	if (!d->v3)
		mutex_lock(&scm_lmh_lock);
	ret = cpu_pll_program(hw, rate, parent_rate);
	if (!d->v3)
		mutex_unlock(&scm_lmh_lock);
	return ret;
}

static int cpu_pll_manual_enable(struct msm8996_cpu_domain *d)
{
	struct regmap *map = d->pll.clkr.regmap;
	u32 status;
	int ret;

	ret = regmap_update_bits(map, 0, BIT(1), BIT(1));
	if (ret)
		return ret;
	udelay(10);
	ret = regmap_update_bits(map, 0, BIT(2), BIT(2));
	if (ret)
		return ret;
	udelay(50);
	ret = readl_poll_timeout_atomic(d->base, status, status & BIT(31), 1, 500);
	if (ret)
		return ret;
	return regmap_update_bits(map, 0, BIT(0), BIT(0));
}

static int cpu_pll_enable(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = container_of(to_clk_regmap(hw), struct clk_alpha_pll, clkr);
	struct msm8996_cpu_domain *d = container_of(pll, struct msm8996_cpu_domain, pll);

	if (d->manual_pll)
		return cpu_pll_manual_enable(d);
	return clk_alpha_pll_hwfsm_ops.enable(hw);
}

static void cpu_pll_disable(struct clk_hw *hw)
{
	clk_alpha_pll_hwfsm_ops.disable(hw);
}

static int cpu_pll_is_enabled(struct clk_hw *hw)
{
	struct clk_alpha_pll *pll = container_of(to_clk_regmap(hw), struct clk_alpha_pll, clkr);
	struct msm8996_cpu_domain *d = container_of(pll, struct msm8996_cpu_domain, pll);

	if (d->manual_pll)
		return !!(readl_relaxed(d->base) & BIT(31));
	return clk_alpha_pll_hwfsm_ops.is_enabled(hw);
}

static unsigned long cpu_pll_recalc(struct clk_hw *hw, unsigned long parent_rate)
{
	return clk_alpha_pll_hwfsm_ops.recalc_rate(hw, parent_rate);
}

static const struct clk_ops cpu_pll_ops = {
	.enable = cpu_pll_enable,
	.disable = cpu_pll_disable,
	.is_enabled = cpu_pll_is_enabled,
	.recalc_rate = cpu_pll_recalc,
	.round_rate = cpu_pll_round_rate,
	.set_rate = cpu_pll_set_rate,
};

static void acd_write(u64 reg, u64 value)
{
	write_sysreg_s(reg, sys_reg(3, 3, 15, 0, 6));
	isb();
	write_sysreg_s(value, sys_reg(3, 3, 15, 0, 7));
	isb();
}

static int msm8996_acd_online(unsigned int cpu)
{
	struct msm8996_cpu_domain *d;
	unsigned long flags;
	unsigned int cluster = MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 1);

	if (!cpu_cc || !cpu_cc->v3 || cluster > 1)
		return 0;
	d = &cpu_cc->domain[cluster];
	raw_spin_lock_irqsave(&acd_lock, flags);
	acd_write(0x581, 0x00006a11);
	acd_write(0x584, 0x000e0f0f);
	acd_write(0x589, 0x00000601);
	if (!cluster) {
		writel_relaxed(0xf, d->base + 0x160);
		/* Complete soft-start setup before enabling the ACD leg. */
		mb();
	}
	acd_write(0x580, 0x002c5ffd);
	if (cluster) {
		writel_relaxed(0xf, d->base + 0x160);
		/* Complete soft-start setup before enabling the ACD leg. */
		mb();
	}
	raw_spin_unlock_irqrestore(&acd_lock, flags);
	return 0;
}

/* Keep the ACD leg off the CPU while its regulator changes voltage. */
static int cpu_set_vdd(struct clk_vdd_class *vdd, int level)
{
	struct msm8996_cpu_domain *d = container_of(vdd, struct msm8996_cpu_domain, vdd);
	unsigned int parent = 0;
	int ret, restore;

	if (d->has_acd) {
		regmap_read(d->mux, d->mux_reg, &parent);
		if ((parent & 3) == 2) {
			ret = cpu_mux_write(d, 0xf, 0x4); /* primary PLL / 2 */
			if (ret)
				return ret;
		}
	}
	ret = regulator_set_voltage(d->supply, vdd->vdd_uv[level], INT_MAX);
	if (!ret && level && (!vdd->cur_level || vdd->cur_level == d->count))
		ret = regulator_enable(d->supply);
	else if (!ret && !level && vdd->cur_level != d->count)
		ret = regulator_disable(d->supply);
	if ((parent & 3) == 2) {
		restore = cpu_mux_write(d, 0xf, parent & 0xf);
		if (!ret)
			ret = restore;
	}
	return ret;
}

static void cpu_wake(void *unused)
{
	msm8996_acd_online(smp_processor_id());
}

static int cpu_rate_notify(struct notifier_block *nb, unsigned long event, void *arg)
{
	struct msm8996_cpu_domain *d = container_of(nb, struct msm8996_cpu_domain, nb);
	struct clk_notifier_data *change = arg;
	int ret = 0;

	switch (event) {
	case PRE_RATE_CHANGE:
		ret = regmap_read(d->mux, d->mux_reg, &d->saved_mux);
		if (ret)
			return notifier_from_errno(ret);
		d->transition = true;
		if (d->index < 2) {
			d->qos.type = PM_QOS_REQ_AFFINE_CORES;
			d->qos.cpus_affine = d->cpus;
			pm_qos_add_request(&d->qos, PM_QOS_CPU_DMA_LATENCY, 1);
			d->qos_active = true;
			ret = smp_call_function_any(&d->cpus, cpu_wake, NULL, 1);
			if (ret)
				goto out_qos;
		}
		if (d->index == 1) {
			if (!d->v3)
				mutex_lock(&scm_lmh_lock);
			ret = clk_set_rate(d->alt.clkr.hw.clk,
				change->new_rate > 1190400000 ? 556800000 : 307200000);
			if (!d->v3)
				mutex_unlock(&scm_lmh_lock);
			if (ret)
				goto out_qos;
		}
		/* Avoid transient overclocking when selecting a divided output. */
		ret = cpu_mux_write(d, d->index == 2 ? 3 : 0xf,
				    d->index == 2 ? 2 : 0xc);
		if (!ret)
			return NOTIFY_OK;
		break;
	case POST_RATE_CHANGE:
		if (!d->transition)
			return NOTIFY_DONE;
		if (d->index < 2 && change->new_rate <= CPU_ACD_THRESHOLD)
			ret = cpu_mux_write(d, 0xc, 0x4);
		if (ret)
			break;
		ret = cpu_set_parent(&d->hw, d->index == 2 ?
			(change->new_rate < CPU_PLL_MIN_RATE ? 2 : 1) :
			(change->new_rate <= CPU_ACD_THRESHOLD ? 0 : (d->has_acd ? 2 : 1)));
		break;
	case ABORT_RATE_CHANGE:
		if (!d->transition)
			return NOTIFY_DONE;
		ret = cpu_mux_write(d, d->index == 2 ? 3 : 0xf,
				    d->saved_mux & (d->index == 2 ? 3 : 0xf));
		break;
	default:
		return NOTIFY_DONE;
	}
out_qos:
	d->transition = false;
	if (d->qos_active) {
		pm_qos_remove_request(&d->qos);
		d->qos_active = false;
	}
	return notifier_from_errno(ret);
}

static int cpu_read_plan(struct device *dev, struct msm8996_cpu_domain *d,
			 const char *name, unsigned int bin)
{
	const __be32 *plan;
	char property[40];
	unsigned int i;
	int length;

	snprintf(property, sizeof(property), "qcom,%s-speedbin%u-v0", name, bin);
	plan = of_get_property(dev->of_node, property, &length);
	if (!plan) {
		snprintf(property, sizeof(property), "qcom,%s-speedbin0-v0", name);
		plan = of_get_property(dev->of_node, property, &length);
	}
	if (!plan || length < 4 * sizeof(*plan) || length % (2 * sizeof(*plan)))
		return -EINVAL;
	d->count = length / (2 * sizeof(*plan));
	d->rates = devm_kcalloc(dev, d->count, sizeof(*d->rates), GFP_KERNEL);
	d->vdd.vdd_uv = devm_kcalloc(dev, d->count, sizeof(int), GFP_KERNEL);
	d->vdd.level_votes = devm_kcalloc(dev, d->count, sizeof(int), GFP_KERNEL);
	if (!d->rates || !d->vdd.vdd_uv || !d->vdd.level_votes)
		return -ENOMEM;
	for (i = 0; i < d->count; i++) {
		d->rates[i] = be32_to_cpup(plan + i * 2);
		d->vdd.vdd_uv[i] = be32_to_cpup(plan + i * 2 + 1);
		if ((!i && (d->rates[i] || d->vdd.vdd_uv[i])) ||
		    (i && (d->rates[i] <= d->rates[i - 1] ||
			   d->vdd.vdd_uv[i] < d->vdd.vdd_uv[i - 1])))
			return -EINVAL;
	}
	d->vdd.class_name = devm_kasprintf(dev, GFP_KERNEL, "vdd_%s", name);
	if (!d->vdd.class_name)
		return -ENOMEM;
	d->vdd.num_levels = d->count;
	d->vdd.cur_level = d->count;
	d->vdd.skip_handoff = true;
	d->vdd.set_vdd = cpu_set_vdd;
	mutex_init(&d->vdd.lock);
	return 0;
}

static struct regmap *cpu_map(struct platform_device *pdev, const char *name,
			     void __iomem **base)
{
	const struct regmap_config config = {
		.name = name,
		.reg_bits = 32, .val_bits = 32, .reg_stride = 4,
		.max_register = 0xffc, .fast_io = true,
	};
	struct resource *res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);

	if (!res)
		return ERR_PTR(-EINVAL);
	/* PLL and mux resources intentionally overlap in the legacy binding. */
	*base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!*base)
		return ERR_PTR(-ENOMEM);
	return devm_regmap_init_mmio(&pdev->dev, *base, &config);
}

static int cpu_register_hw(struct device *dev, struct clk_hw *hw, const char *name,
			   const char **parents, unsigned int num_parents,
			   const struct clk_ops *ops, unsigned long flags,
			   struct clk_vdd_class *vdd, unsigned long *rates, unsigned int count)
{
	struct clk_init_data *init;

	if (!name)
		return -ENOMEM;
	init = devm_kzalloc(dev, sizeof(*init), GFP_KERNEL);
	if (!init)
		return -ENOMEM;
	init->parent_names = devm_kmemdup(dev, parents, num_parents * sizeof(*parents), GFP_KERNEL);
	if (!init->parent_names)
		return -ENOMEM;
	init->name = name;
	init->num_parents = num_parents;
	init->ops = ops;
	init->flags = flags;
	init->vdd_class = vdd;
	init->rate_max = rates;
	init->num_rate_max = count;
	hw->init = init;
	return devm_clk_hw_register(dev, hw);
}

static int cpu_park_domains(struct msm8996_cpu_cc *cc)
{
	unsigned int i;
	int ret;

	/* CPR is shared: park both clusters and CBF before changing any rail. */
	for (i = 0; i < ARRAY_SIZE(cc->domain); i++) {
		struct msm8996_cpu_domain *d = &cc->domain[i];

		ret = cpu_mux_write(d, i == 2 ? 3 : 0xf, i == 2 ? 3 : 0xc);
		if (ret)
			return ret;
		/* Use the same safe source for automatic clock selection. */
		ret = regmap_update_bits(d->mux, d->mux_reg, GENMASK(5, 4), 3 << 4);
		if (ret)
			return ret;
	}
	mb();
	udelay(5);
	return 0;
}

static int cpu_alt_pll_disable(struct msm8996_cpu_domain *d)
{
	struct regmap *map = d->pll.clkr.regmap;
	u32 mode;
	int ret;

	ret = regmap_read(map, 0x100, &mode);
	if (ret)
		return ret;
	if (mode & BIT(20)) {
		/* The bootloader may leave the alternate PLL running in FSM mode. */
		ret = regmap_update_bits(map, 0x100, BIT(7), BIT(7));
		if (ret)
			return ret;
		ret = readl_poll_timeout_atomic(d->base + 0x100, mode,
					       mode & BIT(28), 1, 500);
		if (ret)
			return ret;
		ret = regmap_update_bits(map, 0x100,
					BIT(20) | (d->v3 ? 0 : BIT(7)), 0);
	} else {
		ret = regmap_write(map, 0x100, 0);
	}
	if (ret)
		return ret;
	mb();
	return readl_poll_timeout_atomic(d->base + 0x100, mode,
					!(mode & BIT(30)), 1, 500);
}

static int cpu_domain_init(struct platform_device *pdev,
			   struct msm8996_cpu_domain *d, const char *name)
{
	struct regmap *map = d->pll.clkr.regmap;
	unsigned int user = d->index == 2 ? 0x18 : 0x10;
	unsigned int config = d->index == 2 ? 0x20 : 0x18;
	unsigned int test = d->index == 2 ? 0x30 : 0x20;
	unsigned int l_reg = d->index == 2 ? 0x08 : 0x04;
	unsigned long boot_rate = d->index == 2 ? 614400000 : 883200000;
	char property[32];
	u32 requested, status;
	unsigned int level;
	int ret;

	snprintf(property, sizeof(property), "qcom,%s-early-boot-freq", name);
	if (!of_property_read_u32(pdev->dev.of_node, property, &requested))
		boot_rate = requested;
	for (level = 1; level < d->count; level++)
		if (boot_rate <= d->rates[level])
			break;
	if (level == d->count || boot_rate % CPU_REF_RATE)
		return -EINVAL;
	/* All domains are already parked on the always-on source. */
	ret = cpu_set_vdd(&d->vdd, level);
	if (ret)
		return ret;
	d->vdd.cur_level = level;

	if (d->index < 2) {
		ret = cpu_alt_pll_disable(d);
		if (ret)
			return ret;
	}
	ret = regmap_write(map, 0, 0);
	if (ret)
		return ret;
	/* Complete PLL shutdown before rewriting its configuration. */
	mb();
	ret = regmap_update_bits(map, user, BIT(12) | GENMASK(9, 8) | BIT(24) | 9,
				((d->postdiv - 1) << 8) | 9);
	if (ret)
		return ret;
	ret = regmap_write(map, d->index == 2 ? 0x10 : 0x08, 0);
	if (ret)
		return ret;
	ret = regmap_write(map, config, d->v3 ? 0x200d4828 : 0x200d4aa8);
	if (ret)
		return ret;
	ret = regmap_write(map, config + 4, d->v3 ? 0x6 : 0x2);
	if (ret)
		return ret;
	ret = regmap_write(map, test, d->v3 ? 0x1c000000 : 0x04000000);
	if (ret)
		return ret;
	ret = regmap_write(map, test + 4, 0x00004000);
	if (ret)
		return ret;
	ret = regmap_write(map, l_reg, boot_rate / CPU_REF_RATE);
	if (ret)
		return ret;
	ret = regmap_write(map, 0, 0x00118000);
	if (ret)
		return ret;
	/* MSM8996 v1 CBF has no hardware FSM bits. */
	ret = regmap_read(map, 0, &status);
	if (ret)
		return ret;
	if (d->index == 2 && !status) {
		d->manual_pll = true;
		ret = cpu_pll_manual_enable(d);
		if (ret)
			return ret;
	}
	udelay(50);
	ret = readl_poll_timeout_atomic(d->base, status, status & BIT(31), 1, 500);
	if (ret)
		return ret;
	if (d->index < 2) {
		struct alpha_pll_config alt_config = {
			.l = 16, .vco_val = 3 << 20, .vco_mask = 3 << 20,
			.config_ctl_val = 0x4001051b,
			.post_div_mask = GENMASK(11, 8), .post_div_val = BIT(8),
			.main_output_mask = BIT(0), .early_output_mask = BIT(3),
			.alpha_en_mask = BIT(24),
		};

		d->alt.offset = 0x100;
		d->alt.regs = cpu_alt_pll_regs;
		clk_alpha_pll_configure(&d->alt, map, &alt_config);
		ret = regmap_write(map, 0x108, 0);
		if (ret)
			return ret;
		ret = regmap_write(map, 0x10c, 0);
		if (ret)
			return ret;
		ret = regmap_write(map, 0x100, 0x00118000);
		if (ret)
			return ret;
		ret = readl_poll_timeout_atomic(d->base + 0x100, status,
					       status & BIT(30), 1, 500);
		if (ret)
			return ret;
		ret = regmap_update_bits(d->mux, 0x44, BIT(8), BIT(8));
		if (ret)
			return ret;
		ret = regmap_write(d->mux, 0x164, 0x00030005);
		if (ret)
			return ret;
		if (d->pro) {
			ret = regmap_write(d->mux, 0x7c, 3);
			if (ret)
				return ret;
		}
	} else {
		ret = regmap_update_bits(d->mux, CBF_MUX_REG, BIT(6), BIT(6));
		if (ret)
			return ret;
	}
	return cpu_mux_write(d, d->index == 2 ? 3 : 0xf, d->index == 2 ? 1 : 0xd);
}

static int cpu_domain_resources(struct platform_device *pdev,
				struct msm8996_cpu_domain *d, const char *name,
				unsigned int bin)
{
	struct device *dev = &pdev->dev;
	struct clk_alpha_pll *pll = &d->pll;
	void __iomem *mux_base;
	char supply[20], resource[20];
	int ret;

	snprintf(resource, sizeof(resource), "%s_pll", name);
	pll->clkr.regmap = cpu_map(pdev, resource, &d->base);
	if (IS_ERR(pll->clkr.regmap))
		return PTR_ERR(pll->clkr.regmap);
	snprintf(resource, sizeof(resource), "%s_mux", name);
	d->mux = cpu_map(pdev, resource, &mux_base);
	if (IS_ERR(d->mux))
		return PTR_ERR(d->mux);
	snprintf(supply, sizeof(supply), "vdd-%s", name);
	d->supply = devm_regulator_get(dev, supply);
	if (IS_ERR(d->supply))
		return PTR_ERR(d->supply);
	ret = cpu_read_plan(dev, d, name, bin);
	if (ret)
		return ret;
	return 0;
}

static void cpu_unregister_notifier(void *arg)
{
	struct msm8996_cpu_domain *d = arg;

	clk_notifier_unregister(d->hw.clk, &d->nb);
}

static int cpu_register_domain(struct platform_device *pdev, struct msm8996_cpu_domain *d,
			       const char *name, const char *xo, const char *aux,
			       unsigned int bin)
{
	struct device *dev = &pdev->dev;
	const char *pll_name, *alt_name, *main_name, *acd_name, *low_name, *out_name;
	const char *parents[4];
	struct clk_alpha_pll *pll = &d->pll;
	unsigned long *pll_rates;
	int ret;

	ret = cpu_domain_init(pdev, d, name);
	if (ret)
		return ret;

	pll_name = devm_kasprintf(dev, GFP_KERNEL, "%s_pll", name);
	alt_name = devm_kasprintf(dev, GFP_KERNEL, "%s_alt_pll", name);
	main_name = devm_kasprintf(dev, GFP_KERNEL, "%s_pll_main", name);
	acd_name = devm_kasprintf(dev, GFP_KERNEL, "%s_pll_acd", name);
	low_name = devm_kasprintf(dev, GFP_KERNEL, "%s_lf_mux", name);
	out_name = devm_kasprintf(dev, GFP_KERNEL, "%s_clk", name);
	if (!pll_name || !alt_name || !main_name || !acd_name || !low_name || !out_name)
		return -ENOMEM;
	pll_rates = devm_kcalloc(dev, VDD_DIG_NUM, sizeof(*pll_rates), GFP_KERNEL);
	if (!pll_rates)
		return -ENOMEM;
	pll_rates[VDD_DIG_LOW] = CPU_PLL_MAX_RATE;
	pll->regs = d->index == 2 ? cbf_pll_regs : cpu_pll_regs;
	pll->flags = SUPPORTS_DYNAMIC_UPDATE | SUPPORTS_FSM_MODE;
	parents[0] = xo;
	ret = cpu_register_hw(dev, &pll->clkr.hw, pll_name, parents, 1, &cpu_pll_ops,
		CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL, &vdd_dig, pll_rates, VDD_DIG_NUM);
	if (ret)
		return ret;
	d->main.mult = 1;
	d->main.div = d->postdiv;
	parents[0] = pll_name;
	ret = cpu_register_hw(dev, &d->main.hw, main_name, parents, 1,
		&clk_fixed_factor_ops, CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE, NULL, NULL, 0);
	if (ret)
		return ret;
	if (d->index < 2) {
		d->alt.offset = 0x100;
		d->alt.regs = cpu_alt_pll_regs;
		d->alt.vco_table = alt_vcos;
		d->alt.num_vco = ARRAY_SIZE(alt_vcos);
		d->alt.flags = SUPPORTS_FSM_MODE | SUPPORTS_OFFLINE_REQ;
		d->alt.clkr.regmap = pll->clkr.regmap;
		parents[0] = xo;
		ret = cpu_register_hw(dev, &d->alt.clkr.hw, alt_name, parents, 1,
			&clk_alpha_pll_hwfsm_ops, CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL,
			NULL, NULL, 0);
		if (ret)
			return ret;
		d->acd.mult = d->acd.div = 1;
		parents[0] = pll_name;
		ret = cpu_register_hw(dev, &d->acd.hw, acd_name, parents, 1,
			&clk_fixed_factor_ops, CLK_SET_RATE_PARENT, NULL, NULL, 0);
		if (ret)
			return ret;
		d->low.reg = CPU_MUX_REG;
		d->low.shift = d->low.width = 2;
		d->low.parent_map = low_parent_map;
		d->low.clkr.regmap = d->mux;
		parents[0] = xo;
		parents[1] = main_name;
		parents[2] = aux;
		ret = cpu_register_hw(dev, &d->low.clkr.hw, low_name, parents, 3,
			&clk_regmap_mux_closest_ops, CLK_SET_RATE_PARENT |
			CLK_SET_RATE_NO_REPARENT | CLK_GET_RATE_NOCACHE,
			NULL, NULL, 0);
		if (ret)
			return ret;
		ret = clk_set_parent(d->low.clkr.hw.clk, d->main.hw.clk);
		if (ret)
			return ret;
		parents[0] = low_name;
		parents[1] = pll_name;
		parents[2] = acd_name;
		parents[3] = alt_name;
	} else {
		/* Hardware selectors 1=early, 2=main and 3=aux. */
		parents[0] = xo;
		parents[1] = pll_name;
		parents[2] = main_name;
		parents[3] = aux;
	}
	ret = cpu_register_hw(dev, &d->hw, out_name, parents, 4, &cpu_ops,
		CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL,
		&d->vdd, d->rates, d->count);
	if (ret)
		return ret;
	d->nb.notifier_call = cpu_rate_notify;
	ret = clk_notifier_register(d->hw.clk, &d->nb);
	if (ret)
		return ret;
	return devm_add_action_or_reset(dev, cpu_unregister_notifier, d);
}

struct cpu_clk_id {
	u32 id;
	struct clk_hw *hw;
};

struct cpu_clk_data {
	unsigned int count;
	struct cpu_clk_id clocks[19];
};

static struct clk_hw *cpu_clk_get(struct of_phandle_args *spec, void *arg)
{
	struct cpu_clk_data *data = arg;
	unsigned int i;

	if (spec->args_count != 1)
		return ERR_PTR(-EINVAL);
	for (i = 0; i < data->count; i++)
		if (spec->args[0] == data->clocks[i].id)
			return data->clocks[i].hw;
	return ERR_PTR(-ENOENT);
}

static int cpu_add_opps(struct msm8996_cpu_domain *d, struct device *dev)
{
	unsigned int i;
	int uv, ret;

	if (!dev)
		return -EPROBE_DEFER;
	for (i = 1; i < d->count; i++) {
		uv = regulator_list_corner_voltage(d->supply, d->vdd.vdd_uv[i]);
		if (uv <= 0)
			return uv ?: -EINVAL;
		ret = dev_pm_opp_add(dev, d->rates[i], uv);
		if (ret)
			return ret;
	}
	return 0;
}

static void cpu_debug_enable(struct clk_debug_mux *mux)
{
	struct msm8996_cpu_cc *cc = container_of(mux, struct msm8996_cpu_cc, debug);
	unsigned int i;

	for (i = 0; i < 2; i++)
		regmap_update_bits(cc->domain[i].mux, 0x48, GENMASK(11, 8), GENMASK(11, 8));
	/* Both cluster diagnostic gates must be enabled before measurement. */
	mb();
	udelay(5);
}

static void cpu_debug_disable(struct clk_debug_mux *mux)
{
	struct msm8996_cpu_cc *cc = container_of(mux, struct msm8996_cpu_cc, debug);
	unsigned int i;

	for (i = 0; i < 2; i++)
		regmap_update_bits(cc->domain[i].mux, 0x48, GENMASK(11, 8), 0);
}

static int cpu_register_debug(struct device *dev, struct msm8996_cpu_cc *cc)
{
	static const char *parents[] = { "cbf_clk", "pwrcl_clk", "perfcl_clk" };
	static int selectors[] = { 0x01, 0x11, 0x21 };
	static int dividers[] = { 1, 16, 16 };

	cc->debug.enable_debug = cpu_debug_enable;
	cc->debug.disable_debug = cpu_debug_disable;
	cc->debug.debug_offset = 0x78;
	cc->debug.cbcr_offset = U32_MAX;
	cc->debug.src_sel_mask = 0xff00;
	cc->debug.src_sel_shift = 8;
	cc->debug.post_div_val = 1;
	cc->debug.mux_sels = selectors;
	cc->debug.pre_div_vals = dividers;
	return cpu_register_hw(dev, &cc->debug.hw, "cpu_debug_mux", parents,
		ARRAY_SIZE(parents), &clk_debug_mux_ops, CLK_IS_MEASURE, NULL, NULL, 0);
}

static int cpu_pm_notify(struct notifier_block *nb, unsigned long event, void *arg)
{
	if (event == CPU_PM_EXIT || event == CPU_PM_ENTER_FAILED)
		msm8996_acd_online(smp_processor_id());
	return NOTIFY_OK;
}

static struct notifier_block cpu_pm_nb = {
	.notifier_call = cpu_pm_notify,
};

static void cpu_unregister_hotplug(void *arg)
{
	struct msm8996_cpu_cc *cc = arg;

	cpu_pm_unregister_notifier(&cpu_pm_nb);
	cpuhp_remove_state(cc->hp_state);
	cpu_cc = NULL;
}

static int cpu_msm8996_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct msm8996_cpu_cc *cc;
	struct cpu_clk_data *data;
	struct clk *xo, *aux;
	struct resource *res;
	void __iomem *efuse;
	unsigned int i, cpu, bin;
	const char *parent;
	int ret;

	cc = devm_kzalloc(dev, sizeof(*cc), GFP_KERNEL);
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!cc || !data)
		return -ENOMEM;
	cc->pro = of_device_is_compatible(dev->of_node, "qcom,cpu-clock-8996-pro");
	cc->v3 = cc->pro || of_device_is_compatible(dev->of_node, "qcom,cpu-clock-8996-v3");
	xo = devm_clk_get(dev, "xo_ao");
	if (IS_ERR(xo))
		return PTR_ERR(xo);
	aux = devm_clk_get(dev, "aux_clk");
	if (IS_ERR(aux))
		return PTR_ERR(aux);
	vdd_dig.regulator[0] = devm_regulator_get(dev, "vdd-dig");
	if (IS_ERR(vdd_dig.regulator[0]))
		return PTR_ERR(vdd_dig.regulator[0]);
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "efuse");
	if (!res)
		return -EINVAL;
	efuse = devm_ioremap(dev, res->start, resource_size(res));
	if (!efuse)
		return -ENOMEM;
	bin = (readl_relaxed(efuse) >> 29) & 7;
	/* Resolve deferred supplies before registering critical clocks. */
	for (i = 0; i < 3; i++) {
		struct msm8996_cpu_domain *d = &cc->domain[i];

		d->index = i;
		d->v3 = cc->v3;
		d->pro = cc->pro;
		d->postdiv = i == 2 && cc->pro ? 4 : 2;
		d->mux_reg = i == 2 ? CBF_MUX_REG : CPU_MUX_REG;
		d->has_acd = i < 2 && cc->v3;
		d->cfa_workaround = !cc->v3 || i == 2;
		for_each_possible_cpu(cpu)
			if (MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 1) == i)
				cpumask_set_cpu(cpu, &d->cpus);
		ret = cpu_domain_resources(pdev, d,
				 i == 0 ? "pwrcl" : i == 1 ? "perfcl" : "cbf", bin);
		if (ret)
			return ret;
	}
	cc->aux.mult = 1;
	cc->aux.div = 2;
	parent = __clk_get_name(aux);
	ret = cpu_register_hw(dev, &cc->aux.hw, "sys_apcsaux_clk", &parent, 1,
		&clk_fixed_factor_ops, CLK_IS_CRITICAL, NULL, NULL, 0);
	if (ret)
		return ret;
	{
		void __iomem *debug_base, *recal;
		struct regmap *debug = cpu_map(pdev, "debug", &debug_base);

		if (IS_ERR(debug))
			return PTR_ERR(debug);
		cc->debug.regmap = debug;
		ret = regmap_update_bits(debug, 0x50, GENMASK(17, 16), BIT(16));
		if (ret)
			return ret;
		/* Complete the aux divider update before selecting it. */
		mb();
		udelay(5);
		if (cc->pro) {
			recal = devm_ioremap(dev, 0x099e00c8, 0x10);
			if (!recal)
				return -ENOMEM;
			writel_relaxed(0, recal);
			writel_relaxed(0x800003ff, recal + 8);
			/* Program settling delays before ACD can be enabled. */
			wmb();
		}
	}
	ret = cpu_park_domains(cc);
	if (ret)
		return ret;
	for (i = 0; i < 3; i++) {
		ret = cpu_register_domain(pdev, &cc->domain[i],
				 i == 0 ? "pwrcl" : i == 1 ? "perfcl" : "cbf",
				 __clk_get_name(xo), "sys_apcsaux_clk", bin);
		if (ret)
			return ret;
	}
	ret = cpu_register_debug(dev, cc);
	if (ret)
		return ret;
#define CPU_CLK(_id, _hw) do { \
	data->clocks[data->count++] = (struct cpu_clk_id){ _id, _hw }; \
} while (0)
#define CPU_CLUSTER(_name, _domain) do { \
	CPU_CLK(clk_##_name##_clk, &_domain.hw); \
	CPU_CLK(clk_##_name##_pll, &_domain.pll.clkr.hw); \
	CPU_CLK(clk_##_name##_alt_pll, &_domain.alt.clkr.hw); \
	CPU_CLK(clk_##_name##_pll_main, &_domain.main.hw); \
	CPU_CLK(clk_##_name##_hf_mux, &_domain.hw); \
	CPU_CLK(clk_##_name##_lf_mux, &_domain.low.clkr.hw); \
} while (0)
	CPU_CLUSTER(pwrcl, cc->domain[0]);
	CPU_CLUSTER(perfcl, cc->domain[1]);
	CPU_CLK(clk_cbf_pll, &cc->domain[2].pll.clkr.hw);
	CPU_CLK(clk_cbf_pll_main, &cc->domain[2].main.hw);
	CPU_CLK(clk_cbf_hf_mux, &cc->domain[2].hw);
	CPU_CLK(clk_cbf_clk, &cc->domain[2].hw);
	CPU_CLK(clk_xo_ao, __clk_get_hw(xo));
	CPU_CLK(clk_sys_apcsaux_clk, &cc->aux.hw);
	CPU_CLK(clk_cpu_debug_mux, &cc->debug.hw);
#undef CPU_CLUSTER
#undef CPU_CLK
	cpu_cc = cc;
	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "clk/qcom/msm8996:online",
			       msm8996_acd_online, NULL);
	if (ret < 0) {
		cpu_cc = NULL;
		return ret;
	}
	cc->hp_state = ret;
	ret = cpu_pm_register_notifier(&cpu_pm_nb);
	if (ret) {
		cpuhp_remove_state(cc->hp_state);
		cpu_cc = NULL;
		return ret;
	}
	ret = devm_add_action_or_reset(dev, cpu_unregister_hotplug, cc);
	if (ret)
		return ret;
	for (i = 0; i < 2; i++) {
		if (cc->domain[i].has_acd) {
			ret = clk_set_parent(cc->domain[i].hw.clk, cc->domain[i].acd.hw.clk);
			if (ret)
				return ret;
		}
	}
	for_each_possible_cpu(cpu) {
		i = MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 1);
		if (i > 1)
			continue;
		ret = cpu_add_opps(&cc->domain[i], get_cpu_device(cpu));
		if (ret)
			dev_warn(dev, "CPU%u OPP population failed: %d\n", cpu, ret);
	}
	{
		struct device_node *node = of_parse_phandle(dev->of_node, "cbf-dev", 0);
		struct platform_device *cbf = of_find_device_by_node(node);

		of_node_put(node);
		if (cbf) {
			ret = cpu_add_opps(&cc->domain[2], &cbf->dev);
			if (ret)
				dev_warn(dev, "CBF OPP population failed: %d\n", ret);
			put_device(&cbf->dev);
		}
	}
	dev_info(dev, "registered CPU and CBF clocks, speed bin %u\n", bin);
	return devm_of_clk_add_hw_provider(dev, cpu_clk_get, data);
}

static const struct of_device_id cpu_msm8996_match[] = {
	{ .compatible = "qcom,cpu-clock-8996" },
	{ .compatible = "qcom,cpu-clock-8996-v3" },
	{ .compatible = "qcom,cpu-clock-8996-pro" },
	{ }
};
MODULE_DEVICE_TABLE(of, cpu_msm8996_match);

static struct platform_driver cpu_msm8996_driver = {
	.probe = cpu_msm8996_probe,
	.driver = {
		.name = "cpu-msm8996",
		.suppress_bind_attrs = true,
		.of_match_table = cpu_msm8996_match,
	},
};
builtin_platform_driver(cpu_msm8996_driver);
MODULE_DESCRIPTION("MSM8996 CPU and CBF clock provider");
MODULE_LICENSE("GPL v2");
