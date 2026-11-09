/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/audio-ext-clk.h>
#include <dsp/q6afe-v2.h>
#include "audio-ext-clk-up.h"

enum msm8996_audio_clk_id {
	MSM8996_AUDIO_AP,
	MSM8996_AUDIO_PMI,
	MSM8996_AUDIO_LNBB,
	MSM8996_AUDIO_AP2,
	MSM8996_AUDIO_NUM_CLKS,
};

struct msm8996_audio_clk {
	struct clk_hw hw;
	int gpio;
	struct pinctrl *pinctrl;
	struct pinctrl_state *active;
	struct pinctrl_state *sleep;
	struct afe_clk_set config;
};

struct msm8996_audio_clocks {
	struct msm8996_audio_clk clocks[MSM8996_AUDIO_NUM_CLKS];
};

static struct msm8996_audio_clk *to_audio_clk(struct clk_hw *hw)
{
	return container_of(hw, struct msm8996_audio_clk, hw);
}

static int msm8996_audio_ap_prepare(struct clk_hw *hw)
{
	struct msm8996_audio_clk *clk = to_audio_clk(hw);

	if (gpio_is_valid(clk->gpio))
		return gpio_direction_output(clk->gpio, 1);
	return 0;
}

static void msm8996_audio_ap_unprepare(struct clk_hw *hw)
{
	struct msm8996_audio_clk *clk = to_audio_clk(hw);

	if (gpio_is_valid(clk->gpio))
		gpio_direction_output(clk->gpio, 0);
}

static int msm8996_audio_ap2_prepare(struct clk_hw *hw)
{
	struct msm8996_audio_clk *clk = to_audio_clk(hw);
	int ret;

	if (!clk->pinctrl)
		return 0;
	ret = pinctrl_select_state(clk->pinctrl, clk->active);
	if (ret)
		return ret;
	clk->config.enable = 1;
	ret = afe_set_lpass_clk_cfg(IDX_RSVD_3, &clk->config);
	if (ret)
		pinctrl_select_state(clk->pinctrl, clk->sleep);
	return ret;
}

static void msm8996_audio_ap2_unprepare(struct clk_hw *hw)
{
	struct msm8996_audio_clk *clk = to_audio_clk(hw);
	int ret;

	if (!clk->pinctrl)
		return;
	pinctrl_select_state(clk->pinctrl, clk->sleep);
	clk->config.enable = 0;
	ret = afe_set_lpass_clk_cfg(IDX_RSVD_3, &clk->config);
	if (ret)
		pr_err("audio-ref-clk: failed to disable LPASS clock: %d\n", ret);
}

static const struct clk_ops msm8996_audio_ap_ops = {
	.prepare = msm8996_audio_ap_prepare,
	.unprepare = msm8996_audio_ap_unprepare,
};

static const struct clk_ops msm8996_audio_ap2_ops = {
	.prepare = msm8996_audio_ap2_prepare,
	.unprepare = msm8996_audio_ap2_unprepare,
};

/* The common clock framework propagates votes to the RPM parent. */
static const struct clk_ops msm8996_audio_rpm_ops = {};

static struct clk_hw *msm8996_audio_clk_get(struct of_phandle_args *spec,
					void *data)
{
	struct msm8996_audio_clocks *audio = data;
	unsigned int index;

	if (spec->args_count != 1)
		return ERR_PTR(-EINVAL);
	switch (spec->args[0]) {
	case clk_audio_ap_clk:
		index = MSM8996_AUDIO_AP;
		break;
	case clk_audio_pmi_clk:
		index = MSM8996_AUDIO_PMI;
		break;
	case clk_audio_pmi_lnbb_clk:
		index = MSM8996_AUDIO_LNBB;
		break;
	case clk_audio_ap_clk2:
		index = MSM8996_AUDIO_AP2;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}
	return &audio->clocks[index].hw;
}

static int msm8996_audio_clk_probe(struct platform_device *pdev)
{
	static const char * const names[] = {
		"audio_ext_ap_clk", "audio_ext_pmi_clk",
		"audio_ext_pmi_lnbb_clk", "audio_ext_ap_clk2",
	};
	struct device *dev = &pdev->dev;
	struct msm8996_audio_clocks *audio;
	struct msm8996_audio_clk *ap2;
	struct clk *rpm;
	const char *parent = NULL;
	int gpio, ret, i;

	audio = devm_kzalloc(dev, sizeof(*audio), GFP_KERNEL);
	if (!audio)
		return -ENOMEM;
	for (i = 0; i < MSM8996_AUDIO_NUM_CLKS; i++)
		audio->clocks[i].gpio = -EINVAL;

	if (of_property_read_bool(dev->of_node, "qcom,node_has_rpm_clock")) {
		rpm = devm_clk_get(dev, NULL);
		if (IS_ERR(rpm))
			return PTR_ERR(rpm);
		parent = __clk_get_name(rpm);
	}
	gpio = of_get_named_gpio(dev->of_node, "qcom,audio-ref-clk-gpio", 0);
	if (gpio == -EPROBE_DEFER)
		return gpio;
	if (gpio_is_valid(gpio)) {
		ret = devm_gpio_request(dev, gpio, "EXT_CLK");
		if (ret)
			return ret;
		if (!parent)
			audio->clocks[MSM8996_AUDIO_AP].gpio = gpio;
	}

	ap2 = &audio->clocks[MSM8996_AUDIO_AP2];
	ap2->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(ap2->pinctrl))
		return PTR_ERR(ap2->pinctrl);
	ap2->active = pinctrl_lookup_state(ap2->pinctrl, "active");
	if (IS_ERR(ap2->active))
		return PTR_ERR(ap2->active);
	ap2->sleep = pinctrl_lookup_state(ap2->pinctrl, "sleep");
	if (IS_ERR(ap2->sleep))
		return PTR_ERR(ap2->sleep);
	ret = pinctrl_select_state(ap2->pinctrl, ap2->sleep);
	if (ret)
		return ret;
	ap2->config = (struct afe_clk_set) {
		.clk_set_minor_version = Q6AFE_LPASS_CLK_CONFIG_API_VERSION,
		.clk_id = Q6AFE_LPASS_CLK_ID_SPEAKER_I2S_OSR,
		.clk_freq_in_hz = Q6AFE_LPASS_IBIT_CLK_11_P2896_MHZ,
		.clk_attri = Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
		.clk_root = Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	};

	for (i = 0; i < MSM8996_AUDIO_NUM_CLKS; i++) {
		struct clk_init_data init = {
			.name = names[i],
			.ops = &msm8996_audio_rpm_ops,
		};

		if (i == MSM8996_AUDIO_AP)
			init.ops = &msm8996_audio_ap_ops;
		else if (i == MSM8996_AUDIO_AP2)
			init.ops = &msm8996_audio_ap2_ops;
		else if (parent) {
			init.parent_names = &parent;
			init.num_parents = 1;
		}
		audio->clocks[i].hw.init = &init;
		ret = devm_clk_hw_register(dev, &audio->clocks[i].hw);
		if (ret)
			return ret;
	}
	return devm_of_clk_add_hw_provider(dev, msm8996_audio_clk_get, audio);
}

static const struct of_device_id msm8996_audio_clk_match[] = {
	{ .compatible = "qcom,audio-ref-clk" },
	{}
};
MODULE_DEVICE_TABLE(of, msm8996_audio_clk_match);

static struct platform_driver msm8996_audio_clk_driver = {
	.probe = msm8996_audio_clk_probe,
	.driver = {
		.name = "audio-ref-clk",
		.of_match_table = msm8996_audio_clk_match,
	},
};

int audio_ref_clk_platform_init(void)
{
	return platform_driver_register(&msm8996_audio_clk_driver);
}

void audio_ref_clk_platform_exit(void)
{
	platform_driver_unregister(&msm8996_audio_clk_driver);
}
