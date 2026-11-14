// SPDX-License-Identifier: GPL-2.0-only
/* LG4946 production diagnostics use the panel's U2 fuse-trim status. */
#include <linux/fb.h>
#include <linux/module.h>
#include "mdss_fb.h"
#include "mdss_dsi.h"

int lge_is_valid_U2_FTRIM_reg(void)
{
	struct mdss_dsi_ctrl_pdata *ctrl;
	struct mdss_panel_data *pdata;
	struct msm_fb_data_type *mfd;
	char payload[2] = { 0xc7, 0 };
	char response[4] = {};
	struct dsi_cmd_desc cmd = {
		{ DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(payload) }, payload
	};
	struct dcs_cmd_req req = {
		.cmds = &cmd,
		.cmds_cnt = 1,
		.flags = CMD_REQ_RX | CMD_REQ_COMMIT | CMD_REQ_LP_MODE,
		.rlen = sizeof(response),
		.rbuf = response,
	};
	int ret;

	if (!registered_fb[0] || !registered_fb[0]->par)
		return -ENODEV;
	mfd = registered_fb[0]->par;
	pdata = dev_get_platdata(&mfd->pdev->dev);
	if (!pdata || !pdata->panel_info.panel_power_state)
		return -ENODEV;
	ctrl = container_of(pdata, struct mdss_dsi_ctrl_pdata, panel_data);
	if (!ctrl->h1_panel)
		return -ENODEV;
	ret = mdss_dsi_cmdlist_put(ctrl, &req);
	if (ret < 0)
		return ret;
	return !!(response[3] & BIT(6));
}
EXPORT_SYMBOL_GPL(lge_is_valid_U2_FTRIM_reg);
