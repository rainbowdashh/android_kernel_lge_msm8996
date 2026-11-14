// SPDX-License-Identifier: GPL-2.0-only
/* Lucye SW49408 panel controls, using the command sequences and register
 * masks from LG's MSM8996 Linux 4.4 driver.
 * Copyright (c) 2016, LG Electronics. All rights reserved.
 */
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/of.h>
#include "mdss_fb.h"
#include "mdss_dsi.h"
#include "mdss_lucye.h"

static bool lucye_cmd_valid(struct dsi_panel_cmds *cmd, unsigned int len)
{
	return cmd->cmd_cnt && cmd->cmds && cmd->cmds[0].dchdr.dlen >= len;
}

int mdss_lucye_parse(struct device_node *np, struct mdss_dsi_ctrl_pdata *ctrl)
{
	struct mdss_lucye *p;
	char key[64];
	int i;

	if (!ctrl->sw49408_panel)
		return 0;
	p = ctrl->lucye;
	if (!p) {
		p = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		mutex_init(&p->lock);
		ctrl->lucye = p;
	}
	/* Lucye has a single default display timing. */
	if (p->ready)
		return 0;
	for (i = 0; i < ARRAY_SIZE(p->reader); i++) {
		snprintf(key, sizeof(key), "qcom,panel-reader-mode-step%d-command", i);
		mdss_dsi_parse_dcs_cmds(np, &p->reader[i], key,
				      "qcom,mdss-dsi-reader-mode-command-state");
	}
#define PARSE(member, property) \
	mdss_dsi_parse_dcs_cmds(np, &p->member, property, \
			      "qcom,mdss-dsi-common-hs-command-state")
	PARSE(reg55, "lge,mdss-dsi-55h-command");
	PARSE(regf0, "lge,mdss-dsi-f0h-command");
	PARSE(regf2, "lge,mdss-dsi-f2h-command");
	PARSE(regf3, "lge,mdss-dsi-f3h-command");
	PARSE(regfb, "lge,mdss-dsi-fbh-command");
	PARSE(gamma[0], "qcom,panel-linear-gamma-default-command");
	PARSE(gamma[1], "qcom,panel-linear-gamma-tuning-command");
#undef PARSE
	mdss_dsi_parse_dcs_cmds(np, &p->aod_enter, "lge,mode-change-cmds-u3-to-u2",
			      "qcom,mdss-dsi-on-command-state");
	mdss_dsi_parse_dcs_cmds(np, &p->aod_exit, "lge,mode-change-cmds-u2-to-u3",
			      "qcom,mdss-dsi-on-command-state");
	if (!lucye_cmd_valid(&p->reg55, 2) ||
	    !lucye_cmd_valid(&p->regf0, 2) ||
	    !lucye_cmd_valid(&p->regfb, 5))
		return 0;
	p->default55 = p->reg55.cmds[0].payload[1];
	p->defaultf0 = p->regf0.cmds[0].payload[1];
	p->defaultfb = p->regfb.cmds[0].payload[4];
	p->ready = true;
	return 0;
}

static void lucye_send(struct mdss_dsi_ctrl_pdata *ctrl,
		       struct dsi_panel_cmds *cmd)
{
	if (cmd->cmd_cnt)
		mdss_dsi_panel_cmds_send(ctrl, cmd, CMD_REQ_COMMIT);
}

/* Caller holds p->lock; restore defaults so leaving a mode is reversible. */
static void lucye_apply_locked(struct mdss_dsi_ctrl_pdata *ctrl)
{
	struct mdss_lucye *p = ctrl->lucye;
	u8 reg55 = p->default55, regf0 = p->defaultf0;

	reg55 &= ~(0x70 | 0x08); /* SRE and monochrome */
	if (p->sre_mode)
		reg55 |= 0x30 + p->sre_mode * 0x10;
	if (p->comfort_view)
		regf0 |= 0x04; /* reader gamma */
	else
		regf0 &= ~0x04;
	p->regfb.cmds[0].payload[4] = p->defaultfb;
	if (p->hdr_mode || p->dolby_mode) {
		reg55 &= ~(0x01 | 0x70); /* CABC and SRE */
		regf0 &= ~(0x02 | 0x80); /* sharpness and saturation */
		p->regfb.cmds[0].payload[4] = 0x02;
	}
	p->reg55.cmds[0].payload[1] = reg55;
	p->regf0.cmds[0].payload[1] = regf0;
	lucye_send(ctrl, &p->reader[p->comfort_view]);
	lucye_send(ctrl, &p->gamma[p->linear_gamma]);
	if (p->dolby_mode) {
		lucye_send(ctrl, &p->regf2);
		lucye_send(ctrl, &p->regf3);
	}
	lucye_send(ctrl, &p->regfb);
	lucye_send(ctrl, &p->reg55);
	lucye_send(ctrl, &p->regf0);
}

void mdss_lucye_apply(struct mdss_dsi_ctrl_pdata *ctrl)
{
	struct mdss_lucye *p = ctrl->lucye;

	if (!p || !p->ready)
		return;
	mutex_lock(&p->lock);
	lucye_apply_locked(ctrl);
	mutex_unlock(&p->lock);
}

int mdss_lucye_low_power(struct mdss_dsi_ctrl_pdata *ctrl, bool enable)
{
	struct mdss_lucye *p = ctrl->lucye;

	if (!p || !p->ready)
		return 0;
	mutex_lock(&p->lock);
	if (!enable)
		lucye_apply_locked(ctrl);
	lucye_send(ctrl, enable ? &p->aod_enter : &p->aod_exit);
	mutex_unlock(&p->lock);
	return 0;
}

static struct mdss_dsi_ctrl_pdata *lucye_fb_ctrl(struct msm_fb_data_type *mfd)
{
	struct mdss_panel_data *pdata = dev_get_platdata(&mfd->pdev->dev);

	if (!pdata || mfd->index)
		return NULL;
	return container_of(pdata, struct mdss_dsi_ctrl_pdata, panel_data);
}

static int *lucye_setting(struct mdss_lucye *p, const char *name, int *max)
{
	*max = 1;
	if (!strcmp(name, "comfort_view")) {
		*max = 10;
		return &p->comfort_view;
	}
	if (!strcmp(name, "sre_mode") || !strcmp(name, "daylight_mode")) {
		*max = 3;
		return &p->sre_mode;
	}
	if (!strcmp(name, "linear_gamma"))
		return &p->linear_gamma;
	if (!strcmp(name, "hdr_mode"))
		return &p->hdr_mode;
	return &p->dolby_mode;
}

static ssize_t lucye_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct mdss_dsi_ctrl_pdata *ctrl = lucye_fb_ctrl(fbi->par);
	int max, value;

	if (!ctrl || !ctrl->lucye || !ctrl->lucye->ready)
		return -ENODEV;
	mutex_lock(&ctrl->lucye->lock);
	value = *lucye_setting(ctrl->lucye, attr->attr.name, &max);
	mutex_unlock(&ctrl->lucye->lock);
	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t lucye_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = fbi->par;
	struct mdss_dsi_ctrl_pdata *ctrl = lucye_fb_ctrl(mfd);
	int value, max, *setting;

	if (!ctrl || !ctrl->lucye || !ctrl->lucye->ready)
		return -ENODEV;
	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	/* Serialize display power transitions with user changes. */
	lock_fb_info(fbi);
	mutex_lock(&ctrl->lucye->lock);
	setting = lucye_setting(ctrl->lucye, attr->attr.name, &max);
	if (value < 0 || value > max) {
		mutex_unlock(&ctrl->lucye->lock);
		unlock_fb_info(fbi);
		return -EINVAL;
	}
	*setting = value;
	if (!mdss_dsi_is_panel_off(&ctrl->panel_data))
		lucye_apply_locked(ctrl);
	mutex_unlock(&ctrl->lucye->lock);
	unlock_fb_info(fbi);
	return count;
}

#define LUCYE_ATTR(name) static DEVICE_ATTR(name, 0644, lucye_show, lucye_store)
LUCYE_ATTR(comfort_view);
LUCYE_ATTR(sre_mode);
LUCYE_ATTR(daylight_mode);
LUCYE_ATTR(linear_gamma);
LUCYE_ATTR(hdr_mode);
LUCYE_ATTR(dolby_mode);
static struct attribute *lucye_attrs[] = {
	&dev_attr_comfort_view.attr, &dev_attr_sre_mode.attr,
	&dev_attr_daylight_mode.attr, &dev_attr_linear_gamma.attr,
	&dev_attr_hdr_mode.attr, &dev_attr_dolby_mode.attr, NULL,
};
static const struct attribute_group lucye_group = { .attrs = lucye_attrs };
static struct class *lucye_panel_class;
static struct device *lucye_img_tune;

int mdss_lucye_create_sysfs(struct msm_fb_data_type *mfd)
{
	struct mdss_dsi_ctrl_pdata *ctrl = lucye_fb_ctrl(mfd);
	int ret;

	if (!ctrl || !ctrl->lucye || !ctrl->lucye->ready)
		return 0;
	ret = sysfs_create_group(&mfd->fbi->dev->kobj, &lucye_group);
	if (ret)
		return ret;
	/* Retain the LG userspace ABI as well as the fb0 control aliases. */
	lucye_panel_class = class_create(THIS_MODULE, "panel");
	if (IS_ERR(lucye_panel_class)) {
		ret = PTR_ERR(lucye_panel_class);
		lucye_panel_class = NULL;
		goto remove_fb;
	}
	lucye_img_tune = device_create(lucye_panel_class, NULL, 0,
				      mfd->fbi, "img_tune");
	if (IS_ERR(lucye_img_tune)) {
		ret = PTR_ERR(lucye_img_tune);
		lucye_img_tune = NULL;
		goto destroy_class;
	}
	ret = sysfs_create_group(&lucye_img_tune->kobj, &lucye_group);
	if (!ret)
		return 0;
	device_unregister(lucye_img_tune);
	lucye_img_tune = NULL;
destroy_class:
	class_destroy(lucye_panel_class);
	lucye_panel_class = NULL;
remove_fb:
	sysfs_remove_group(&mfd->fbi->dev->kobj, &lucye_group);
	return ret;
}

void mdss_lucye_remove_sysfs(struct msm_fb_data_type *mfd)
{
	struct mdss_dsi_ctrl_pdata *ctrl = lucye_fb_ctrl(mfd);

	if (ctrl && ctrl->lucye && ctrl->lucye->ready) {
		if (lucye_img_tune) {
			sysfs_remove_group(&lucye_img_tune->kobj, &lucye_group);
			device_unregister(lucye_img_tune);
			lucye_img_tune = NULL;
		}
		if (lucye_panel_class) {
			class_destroy(lucye_panel_class);
			lucye_panel_class = NULL;
		}
		sysfs_remove_group(&mfd->fbi->dev->kobj, &lucye_group);
	}
}
