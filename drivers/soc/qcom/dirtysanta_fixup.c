// SPDX-License-Identifier: GPL-2.0-or-later
/* V20 VENDOR0 layout and identity correction from Elliott Mitchell's LG port. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/soc/qcom/smem.h>

struct elsa_vendor0 {
	int hw_rev;
	char model_name[10];
	char nt_code[2048];
	char lg_model_name[22];
	int sim_num;
	int flag_gpio;
};

static int elsa_identity_probe(struct platform_device *pdev)
{
	struct elsa_vendor0 *data;
	size_t size;

	BUILD_BUG_ON(offsetof(struct elsa_vendor0, lg_model_name) != 2062);
	BUILD_BUG_ON(offsetof(struct elsa_vendor0, sim_num) != 2084);
	data = qcom_smem_get(QCOM_SMEM_HOST_ANY, 134, &size);
	if (IS_ERR(data))
		return PTR_ERR(data);
	if (size < sizeof(*data))
		return -EINVAL;
	strscpy(data->lg_model_name, CONFIG_DIRTYSANTA_FIXUP_DEVICENAME,
		 sizeof(data->lg_model_name));
	data->sim_num = CONFIG_DIRTYSANTA_FIXUP_SIMCOUNT;
	dev_info(&pdev->dev, "V20 modem identity: %s, %d SIM slots\n",
		 data->lg_model_name, data->sim_num);
	return 0;
}

static const struct of_device_id elsa_identity_match[] = {
	{ .compatible = "lge,elsa-modem-fixup" },
	{ }
};
static struct platform_driver elsa_identity_driver = {
	.probe = elsa_identity_probe,
	.driver = {
		.name = "lge-elsa-modem-fixup",
		.of_match_table = elsa_identity_match,
	},
};
static int __init elsa_identity_init(void)
{
	return platform_driver_register(&elsa_identity_driver);
}
subsys_initcall(elsa_identity_init);
