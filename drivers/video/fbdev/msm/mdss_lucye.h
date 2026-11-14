/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MDSS_LUCYE_H
#define MDSS_LUCYE_H

struct msm_fb_data_type;
struct mdss_lucye {
	struct mutex lock;
	struct dsi_panel_cmds reader[11];
	struct dsi_panel_cmds reg55, regf0, regf2, regf3, regfb;
	struct dsi_panel_cmds gamma[2];
	struct dsi_panel_cmds aod_enter, aod_exit;
	u8 default55, defaultf0, defaultfb;
	int comfort_view, sre_mode, hdr_mode, dolby_mode, linear_gamma;
	bool ready;
};

int mdss_lucye_parse(struct device_node *np, struct mdss_dsi_ctrl_pdata *ctrl);
void mdss_lucye_apply(struct mdss_dsi_ctrl_pdata *ctrl);
int mdss_lucye_low_power(struct mdss_dsi_ctrl_pdata *ctrl, bool enable);
int mdss_lucye_create_sysfs(struct msm_fb_data_type *mfd);
void mdss_lucye_remove_sysfs(struct msm_fb_data_type *mfd);

#endif
