// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2020, The Linux Foundation. All rights reserved.
 */
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem_state.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/pm_wakeup.h>

struct smp2p_sleepstate {
	struct qcom_smem_state *state;
	struct wakeup_source *notify_ws;
	struct notifier_block pm_nb;
	u32 awake_mask;
};

static int sleepstate_pm_notifier(struct notifier_block *nb,
				  unsigned long event, void *unused)
{
	struct smp2p_sleepstate *slst = container_of(nb,
					struct smp2p_sleepstate, pm_nb);
	int ret;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		ret = qcom_smem_state_update_bits(slst->state,
						 slst->awake_mask, 0);
		break;
	case PM_POST_SUSPEND:
		ret = qcom_smem_state_update_bits(slst->state,
					 slst->awake_mask, slst->awake_mask);
		break;
	default:
		return NOTIFY_DONE;
	}

	return notifier_from_errno(ret);
}

static irqreturn_t smp2p_sleepstate_handler(int irq, void *ctxt)
{
	struct smp2p_sleepstate *slst = ctxt;

	__pm_wakeup_event(slst->notify_ws, 200);
	return IRQ_HANDLED;
}

static void sleepstate_put(void *data)
{
	qcom_smem_state_put(data);
}

static void sleepstate_wakeup_unregister(void *data)
{
	wakeup_source_unregister(data);
}

static void sleepstate_notifier_unregister(void *data)
{
	unregister_pm_notifier(data);
}

static int smp2p_sleepstate_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct smp2p_sleepstate *slst;
	unsigned int bit;
	int ret, irq;

	slst = devm_kzalloc(dev, sizeof(*slst), GFP_KERNEL);
	if (!slst)
		return -ENOMEM;

	slst->state = qcom_smem_state_get(dev, NULL, &bit);
	if (IS_ERR(slst->state))
		return PTR_ERR(slst->state);
	ret = devm_add_action_or_reset(dev, sleepstate_put, slst->state);
	if (ret)
		return ret;
	if (bit >= 32)
		return -EINVAL;
	slst->awake_mask = BIT(bit);

	/* Some firmware only consumes AP sleep notifications. */
	if (of_find_property(dev->of_node, "interrupts", NULL) ||
	    of_find_property(dev->of_node, "interrupts-extended", NULL)) {
		irq = of_irq_get_byname(dev->of_node, "smp2p-sleepstate-in");
		if (irq <= 0)
			return irq ? irq : -EINVAL;
		slst->notify_ws = wakeup_source_register(dev, "smp2p-sleepstate");
		if (!slst->notify_ws)
			return -ENOMEM;
		ret = devm_add_action_or_reset(dev, sleepstate_wakeup_unregister,
					       slst->notify_ws);
		if (ret)
			return ret;
		ret = devm_request_threaded_irq(dev, irq, NULL,
					 smp2p_sleepstate_handler,
					 IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					 "smp2p_sleepstate", slst);
		if (ret)
			return ret;
	}

	ret = qcom_smem_state_update_bits(slst->state,
					 slst->awake_mask, slst->awake_mask);
	if (ret)
		return ret;
	slst->pm_nb.notifier_call = sleepstate_pm_notifier;
	slst->pm_nb.priority = INT_MAX;
	ret = register_pm_notifier(&slst->pm_nb);
	if (ret)
		return ret;
	return devm_add_action_or_reset(dev, sleepstate_notifier_unregister,
				       &slst->pm_nb);
}

static const struct of_device_id smp2p_slst_match_table[] = {
	{ .compatible = "qcom,smp2p-sleepstate" },
	{ }
};
MODULE_DEVICE_TABLE(of, smp2p_slst_match_table);

static struct platform_driver smp2p_sleepstate_driver = {
	.probe = smp2p_sleepstate_probe,
	.driver = {
		.name = "smp2p_sleepstate",
		.of_match_table = smp2p_slst_match_table,
	},
};
module_platform_driver(smp2p_sleepstate_driver);

MODULE_DESCRIPTION("SMP2P SLEEP STATE");
MODULE_LICENSE("GPL v2");
