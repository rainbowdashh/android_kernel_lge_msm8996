// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 * MSM8996 clock tree, adapted from drivers/clk/msm/mdss in the LG 4.4 tree.
 */
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mdss-14nm-pll-clk.h>

#include "mdss-dsi-pll.h"
#include "mdss-dsi-pll-8996.h"

static struct dsi_pll_db pll_db[DSI_PLL_NUM];

static const struct clk_ops vco_ops = {
	.recalc_rate = pll_vco_recalc_rate_8996,
	.round_rate = pll_vco_round_rate_8996,
	.set_rate = pll_vco_set_rate_8996,
	.prepare = pll_vco_prepare_8996,
	.unprepare = pll_vco_unprepare_8996,
};

static const struct clk_ops shadow_vco_ops = {
	.recalc_rate = pll_vco_recalc_rate_8996,
	.round_rate = pll_vco_round_rate_8996,
	.set_rate = shadow_pll_vco_set_rate_8996,
};

static struct dsi8996_divider *to_dsi_div(struct clk_hw *hw)
{
	return container_of(hw, struct dsi8996_divider, hw);
}

static int dsi_div_write(struct dsi8996_divider *div, unsigned int value)
{
	int ret;

	if (!value || value > 15)
		return -EINVAL;
	if (div->pixel && div->shadow)
		ret = msm8996_shadow_n2_div_set_div(div, value);
	else if (div->pixel)
		ret = msm8996_n2_div_set_div(div, value);
	else
		ret = msm8996_post_n1_div_set_div(div, value);
	if (!ret)
		div->div = value;
	return ret;
}

static int dsi_div_prepare(struct clk_hw *hw)
{
	struct dsi8996_divider *div = to_dsi_div(hw);

	/* The PHY power sequence resets both divider fields. */
	return dsi_div_write(div, div->div);
}

static unsigned long dsi_div_recalc(struct clk_hw *hw,
				  unsigned long parent_rate)
{
	struct dsi8996_divider *div = to_dsi_div(hw);
	int value;

	if (!div->shadow) {
		value = div->pixel ? msm8996_n2_div_get_div(div) :
				     msm8996_post_n1_div_get_div(div);
		if (value > 0 && value <= 15)
			div->div = value;
	}
	return parent_rate / div->div;
}

static long dsi_div_round(struct clk_hw *hw, unsigned long rate,
			  unsigned long *parent_rate)
{
	struct dsi8996_divider *div = to_dsi_div(hw);
	struct clk_hw *parent = clk_hw_get_parent(hw);
	unsigned long best = 0, best_parent = *parent_rate, candidate;
	long rounded;
	unsigned int i;

	if (!rate || !parent)
		return -EINVAL;
	if (div->pixel)
		return *parent_rate / clamp_t(unsigned long,
			DIV_ROUND_CLOSEST(*parent_rate, rate), 1, 15);
	for (i = 1; i <= 15; i++) {
		if (rate > ULONG_MAX / i)
			break;
		rounded = clk_hw_round_rate(parent, rate * i);
		if (rounded <= 0)
			continue;
		candidate = rounded / i;
		/* Match the legacy divider's preference for rates >= request. */
		if (!best || (candidate >= rate &&
		    (best < rate || candidate < best)) ||
		    (candidate < rate && best < rate && candidate > best)) {
			best = candidate;
			best_parent = rounded;
		}
		if (candidate == rate)
			break;
	}
	*parent_rate = best_parent;
	return best ? best : -EINVAL;
}

static int dsi_div_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	unsigned int div;

	if (!rate)
		return -EINVAL;
	div = DIV_ROUND_CLOSEST(parent_rate, rate);
	if (!div || div > 15 || parent_rate / div != rate)
		return -EINVAL;
	return dsi_div_write(to_dsi_div(hw), div);
}

static const struct clk_ops div_ops = {
	.prepare = dsi_div_prepare,
	.recalc_rate = dsi_div_recalc,
	.round_rate = dsi_div_round,
	.set_rate = dsi_div_set_rate,
};

static const struct clk_ops shadow_div_ops = {
	.recalc_rate = dsi_div_recalc,
	.round_rate = dsi_div_round,
	.set_rate = dsi_div_set_rate,
};

struct dsi8996_mux {
	struct clk_hw hw;
	u8 parent;
};

static u8 dsi_mux_get_parent(struct clk_hw *hw)
{
	return container_of(hw, struct dsi8996_mux, hw)->parent;
}

static int dsi_mux_set_parent(struct clk_hw *hw, u8 index)
{
	/* Shadow PLL programming is committed by the DSI dynamic-refresh engine. */
	container_of(hw, struct dsi8996_mux, hw)->parent = index;
	return 0;
}

static const struct clk_ops mux_ops = {
	.get_parent = dsi_mux_get_parent,
	.set_parent = dsi_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

static struct clk_hw *dsi_register_vco(struct device *dev,
				     struct mdss_pll_resources *pll, bool shadow)
{
	struct dsi_pll_vco_clk *vco;
	const char *parent = "xo_board";
	struct clk_init_data init = {};
	int ret;

	vco = devm_kzalloc(dev, sizeof(*vco), GFP_KERNEL);
	if (!vco)
		return ERR_PTR(-ENOMEM);
	init.name = devm_kasprintf(dev, GFP_KERNEL, "dsi%upll_%svco_clk_8996",
				  pll->index, shadow ? "shadow_" : "");
	if (!init.name)
		return ERR_PTR(-ENOMEM);
	init.parent_names = &parent;
	init.num_parents = 1;
	init.flags = CLK_GET_RATE_NOCACHE;
	init.ops = shadow ? &shadow_vco_ops : &vco_ops;
	vco->hw.init = &init;
	vco->priv = pll;
	vco->ref_clk_rate = 19200000;
	vco->min_rate = 1300000000;
	vco->max_rate = 2600000000;
	vco->pll_en_seq_cnt = 1;
	vco->pll_enable_seqs[0] = dsi_pll_enable_seq_8996;
	ret = devm_clk_hw_register(dev, &vco->hw);
	return ret ? ERR_PTR(ret) : &vco->hw;
}

static struct clk_hw *dsi_register_div(struct device *dev,
		struct mdss_pll_resources *pll, struct clk_hw *parent,
		bool pixel, bool shadow)
{
	struct dsi8996_divider *div;
	const char *parent_name = clk_hw_get_name(parent);
	struct clk_init_data init = {};
	int ret;

	div = devm_kzalloc(dev, sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);
	init.name = devm_kasprintf(dev, GFP_KERNEL, "dsi%upll_%s%s_div_clk_8996",
		pll->index, shadow ? "shadow_" : "", pixel ? "n2" : "post_n1");
	if (!init.name)
		return ERR_PTR(-ENOMEM);
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_GET_RATE_NOCACHE | (pixel ? 0 : CLK_SET_RATE_PARENT);
	init.ops = shadow ? &shadow_div_ops : &div_ops;
	div->hw.init = &init;
	div->priv = pll;
	div->div = 1;
	div->pixel = pixel;
	div->shadow = shadow;
	ret = devm_clk_hw_register(dev, &div->hw);
	return ret ? ERR_PTR(ret) : &div->hw;
}

static struct clk_hw *dsi_register_output(struct device *dev,
		struct mdss_pll_resources *pll, struct clk_hw *parent,
		bool pixel, bool shadow)
{
	const char *name = devm_kasprintf(dev, GFP_KERNEL,
		"dsi%upll_%s%s_clk_src_8996", pll->index,
		shadow ? "shadow_" : "", pixel ? "pixel" : "byte");
	struct clk_hw *hw;

	if (!name)
		return ERR_PTR(-ENOMEM);
	hw = clk_hw_register_fixed_factor(dev, name, clk_hw_get_name(parent),
		CLK_SET_RATE_PARENT | CLK_GET_RATE_NOCACHE, 1, pixel ? 2 : 8);
	return hw;
}

static void dsi_output_unregister(void *hw)
{
	clk_hw_unregister_fixed_factor(hw);
}

static struct clk_hw *dsi_register_mux(struct device *dev,
		struct mdss_pll_resources *pll, struct clk_hw **parents, bool pixel)
{
	struct dsi8996_mux *mux;
	const char *names[] = { clk_hw_get_name(parents[0]),
				clk_hw_get_name(parents[1]) };
	struct clk_init_data init = {};
	int ret;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);
	init.name = devm_kasprintf(dev, GFP_KERNEL, "dsi%u_phy_pll_out_%sclk",
				  pll->index, pixel ? "dsi" : "byte");
	if (!init.name)
		return ERR_PTR(-ENOMEM);
	init.parent_names = names;
	init.num_parents = ARRAY_SIZE(names);
	init.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT;
	init.ops = &mux_ops;
	mux->hw.init = &init;
	ret = devm_clk_hw_register(dev, &mux->hw);
	return ret ? ERR_PTR(ret) : &mux->hw;
}

int dsi_pll_clock_register_8996(struct platform_device *pdev,
			    struct mdss_pll_resources *pll)
{
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *data;
	struct clk_hw *vco, *n1, *n2, *byte[2], *pixel[2];
	unsigned int base, shadow, i;
	int ret;

	if (pll->index >= DSI_PLL_NUM)
		return -EINVAL;
	data = devm_kzalloc(dev, struct_size(data, hws, 24), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->num = 24;
	for (i = 0; i < data->num; i++)
		data->hws[i] = ERR_PTR(-ENOENT);
	pll->priv = &pll_db[pll->index];
	pll_db[pll->index].pll = pll;
	pll_db[pll->index].next = &pll_db[!pll->index];
	pll->vco_delay = 1;
	if (pll->ssc_en) {
		if (!pll->ssc_freq)
			pll->ssc_freq = 31500;
		if (!pll->ssc_ppm)
			pll->ssc_ppm = 5000;
	}
	base = pll->index * 12;
	for (shadow = 0; shadow < 2; shadow++) {
		vco = dsi_register_vco(dev, pll, shadow);
		if (IS_ERR(vco))
			return PTR_ERR(vco);
		n1 = dsi_register_div(dev, pll, vco, false, shadow);
		if (IS_ERR(n1))
			return PTR_ERR(n1);
		n2 = dsi_register_div(dev, pll, n1, true, shadow);
		if (IS_ERR(n2))
			return PTR_ERR(n2);
		byte[shadow] = dsi_register_output(dev, pll, n1, false, shadow);
		if (IS_ERR(byte[shadow]))
			return PTR_ERR(byte[shadow]);
		ret = devm_add_action_or_reset(dev, dsi_output_unregister, byte[shadow]);
		if (ret)
			return ret;
		pixel[shadow] = dsi_register_output(dev, pll, n2, true, shadow);
		if (IS_ERR(pixel[shadow]))
			return PTR_ERR(pixel[shadow]);
		ret = devm_add_action_or_reset(dev, dsi_output_unregister, pixel[shadow]);
		if (ret)
			return ret;
		data->hws[base + (shadow ? 7 : 1)] = byte[shadow];
		data->hws[base + (shadow ? 8 : 3)] = pixel[shadow];
		data->hws[base + (shadow ? 9 : 4)] = n2;
		data->hws[base + (shadow ? 10 : 5)] = n1;
		data->hws[base + (shadow ? 11 : 6)] = vco;
	}
	data->hws[base] = dsi_register_mux(dev, pll, byte, false);
	if (IS_ERR(data->hws[base]))
		return PTR_ERR(data->hws[base]);
	data->hws[base + 2] = dsi_register_mux(dev, pll, pixel, true);
	if (IS_ERR(data->hws[base + 2]))
		return PTR_ERR(data->hws[base + 2]);
	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, data);
}
