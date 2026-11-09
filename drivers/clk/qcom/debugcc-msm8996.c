// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved. */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/module.h>

#include "clk-debug.h"
#include "common.h"

static struct measure_clk_data measure_data = {
	.ctl_reg = 0x62004,
	.status_reg = 0x62008,
	.xo_div4_cbcr = 0x43008,
};

static const char * const mmss_debug_parents[] = {
	"mmss_mmagic_ahb_clk",
	"mmss_misc_ahb_clk",
	"vmem_maxi_clk",
	"vmem_ahb_clk",
	"video_core_clk",
	"video_axi_clk",
	"video_maxi_clk",
	"video_ahb_clk",
	"mmss_rbcpr_clk",
	"mmss_rbcpr_ahb_clk",
	"mdss_mdp_clk",
	"mdss_pclk0_clk",
	"mdss_pclk1_clk",
	"mdss_extpclk_clk",
	"video_subcore0_clk",
	"video_subcore1_clk",
	"mdss_vsync_clk",
	"mdss_hdmi_clk",
	"mdss_byte0_clk",
	"mdss_byte1_clk",
	"mdss_esc0_clk",
	"mdss_esc1_clk",
	"mdss_ahb_clk",
	"mdss_hdmi_ahb_clk",
	"mdss_axi_clk",
	"camss_top_ahb_clk",
	"camss_micro_ahb_clk",
	"camss_gp0_clk",
	"camss_gp1_clk",
	"camss_mclk0_clk",
	"camss_mclk1_clk",
	"camss_mclk2_clk",
	"camss_mclk3_clk",
	"camss_cci_clk",
	"camss_cci_ahb_clk",
	"camss_csi0phytimer_clk",
	"camss_csi1phytimer_clk",
	"camss_csi2phytimer_clk",
	"camss_jpeg0_clk",
	"camss_ispif_ahb_clk",
	"camss_jpeg2_clk",
	"camss_jpeg_ahb_clk",
	"camss_jpeg_axi_clk",
	"camss_ahb_clk",
	"camss_vfe0_clk",
	"camss_vfe1_clk",
	"camss_cpp_clk",
	"camss_cpp_ahb_clk",
	"camss_vfe_ahb_clk",
	"camss_vfe_axi_clk",
	"camss_csi_vfe0_clk",
	"camss_csi_vfe1_clk",
	"camss_csi0_clk",
	"camss_csi0_ahb_clk",
	"camss_csi0phy_clk",
	"camss_csi0rdi_clk",
	"camss_csi0pix_clk",
	"camss_csi1_clk",
	"camss_csi1_ahb_clk",
	"camss_csi1phy_clk",
	"camss_csi1rdi_clk",
	"camss_csi1pix_clk",
	"camss_csi2_clk",
	"camss_csi2_ahb_clk",
	"camss_csi2phy_clk",
	"camss_csi2rdi_clk",
	"camss_csi2pix_clk",
	"camss_csi3_clk",
	"camss_csi3_ahb_clk",
	"camss_csi3phy_clk",
	"camss_csi3rdi_clk",
	"camss_csi3pix_clk",
	"mmss_mmagic_maxi_clk",
	"camss_vfe0_stream_clk",
	"camss_vfe1_stream_clk",
	"camss_cpp_vbif_ahb_clk",
	"mmss_mmagic_cfg_ahb_clk",
	"mmss_misc_cxo_clk",
	"camss_cpp_axi_clk",
	"camss_jpeg_dma_clk",
	"camss_vfe0_ahb_clk",
	"camss_vfe1_ahb_clk",
	"fd_core_clk",
	"fd_core_uar_clk",
	"fd_ahb_clk",
	"camss_csiphy0_3p_clk",
	"camss_csiphy1_3p_clk",
	"camss_csiphy2_3p_clk",
	"smmu_vfe_ahb_clk",
	"smmu_vfe_axi_clk",
	"smmu_cpp_ahb_clk",
	"smmu_cpp_axi_clk",
	"smmu_jpeg_ahb_clk",
	"smmu_jpeg_axi_clk",
	"mmagic_camss_axi_clk",
	"smmu_rot_ahb_clk",
	"smmu_rot_axi_clk",
	"smmu_mdp_ahb_clk",
	"smmu_mdp_axi_clk",
	"mmagic_mdss_axi_clk",
	"smmu_video_ahb_clk",
	"smmu_video_axi_clk",
	"mmagic_video_axi_clk",
	"mmagic_camss_noc_cfg_ahb_clk",
	"mmagic_mdss_noc_cfg_ahb_clk",
	"mmagic_video_noc_cfg_ahb_clk",
	"mmagic_bimc_noc_cfg_ahb_clk",
	"gpu_ahb_clk",
	"gpu_gx_gfx3d_clk",
	"gpu_gx_rbbmtimer_clk",
	"gpu_aon_isense_clk",
};

static int mmss_debug_selectors[] = {
	0x0001, /* mmss_mmagic_ahb_clk */
	0x0003, /* mmss_misc_ahb_clk */
	0x0009, /* vmem_maxi_clk */
	0x000a, /* vmem_ahb_clk */
	0x000e, /* video_core_clk */
	0x000f, /* video_axi_clk */
	0x0010, /* video_maxi_clk */
	0x0011, /* video_ahb_clk */
	0x0012, /* mmss_rbcpr_clk */
	0x0013, /* mmss_rbcpr_ahb_clk */
	0x0014, /* mdss_mdp_clk */
	0x0016, /* mdss_pclk0_clk */
	0x0017, /* mdss_pclk1_clk */
	0x0018, /* mdss_extpclk_clk */
	0x001a, /* video_subcore0_clk */
	0x001b, /* video_subcore1_clk */
	0x001c, /* mdss_vsync_clk */
	0x001d, /* mdss_hdmi_clk */
	0x001e, /* mdss_byte0_clk */
	0x001f, /* mdss_byte1_clk */
	0x0020, /* mdss_esc0_clk */
	0x0021, /* mdss_esc1_clk */
	0x0022, /* mdss_ahb_clk */
	0x0023, /* mdss_hdmi_ahb_clk */
	0x0024, /* mdss_axi_clk */
	0x0025, /* camss_top_ahb_clk */
	0x0026, /* camss_micro_ahb_clk */
	0x0027, /* camss_gp0_clk */
	0x0028, /* camss_gp1_clk */
	0x0029, /* camss_mclk0_clk */
	0x002a, /* camss_mclk1_clk */
	0x002b, /* camss_mclk2_clk */
	0x002c, /* camss_mclk3_clk */
	0x002d, /* camss_cci_clk */
	0x002e, /* camss_cci_ahb_clk */
	0x002f, /* camss_csi0phytimer_clk */
	0x0030, /* camss_csi1phytimer_clk */
	0x0031, /* camss_csi2phytimer_clk */
	0x0032, /* camss_jpeg0_clk */
	0x0033, /* camss_ispif_ahb_clk */
	0x0034, /* camss_jpeg2_clk */
	0x0035, /* camss_jpeg_ahb_clk */
	0x0036, /* camss_jpeg_axi_clk */
	0x0037, /* camss_ahb_clk */
	0x0038, /* camss_vfe0_clk */
	0x0039, /* camss_vfe1_clk */
	0x003a, /* camss_cpp_clk */
	0x003b, /* camss_cpp_ahb_clk */
	0x003c, /* camss_vfe_ahb_clk */
	0x003d, /* camss_vfe_axi_clk */
	0x003f, /* camss_csi_vfe0_clk */
	0x0040, /* camss_csi_vfe1_clk */
	0x0041, /* camss_csi0_clk */
	0x0042, /* camss_csi0_ahb_clk */
	0x0043, /* camss_csi0phy_clk */
	0x0044, /* camss_csi0rdi_clk */
	0x0045, /* camss_csi0pix_clk */
	0x0046, /* camss_csi1_clk */
	0x0047, /* camss_csi1_ahb_clk */
	0x0048, /* camss_csi1phy_clk */
	0x0049, /* camss_csi1rdi_clk */
	0x004a, /* camss_csi1pix_clk */
	0x004b, /* camss_csi2_clk */
	0x004c, /* camss_csi2_ahb_clk */
	0x004d, /* camss_csi2phy_clk */
	0x004e, /* camss_csi2rdi_clk */
	0x004f, /* camss_csi2pix_clk */
	0x0050, /* camss_csi3_clk */
	0x0051, /* camss_csi3_ahb_clk */
	0x0052, /* camss_csi3phy_clk */
	0x0053, /* camss_csi3rdi_clk */
	0x0054, /* camss_csi3pix_clk */
	0x0070, /* mmss_mmagic_maxi_clk */
	0x0071, /* camss_vfe0_stream_clk */
	0x0072, /* camss_vfe1_stream_clk */
	0x0073, /* camss_cpp_vbif_ahb_clk */
	0x0074, /* mmss_mmagic_cfg_ahb_clk */
	0x0077, /* mmss_misc_cxo_clk */
	0x007a, /* camss_cpp_axi_clk */
	0x007b, /* camss_jpeg_dma_clk */
	0x0086, /* camss_vfe0_ahb_clk */
	0x0087, /* camss_vfe1_ahb_clk */
	0x0089, /* fd_core_clk */
	0x008a, /* fd_core_uar_clk */
	0x008c, /* fd_ahb_clk */
	0x0091, /* camss_csiphy0_3p_clk */
	0x0092, /* camss_csiphy1_3p_clk */
	0x0093, /* camss_csiphy2_3p_clk */
	0x0094, /* smmu_vfe_ahb_clk */
	0x0095, /* smmu_vfe_axi_clk */
	0x0096, /* smmu_cpp_ahb_clk */
	0x0097, /* smmu_cpp_axi_clk */
	0x0098, /* smmu_jpeg_ahb_clk */
	0x0099, /* smmu_jpeg_axi_clk */
	0x009a, /* mmagic_camss_axi_clk */
	0x009b, /* smmu_rot_ahb_clk */
	0x009c, /* smmu_rot_axi_clk */
	0x009d, /* smmu_mdp_ahb_clk */
	0x009e, /* smmu_mdp_axi_clk */
	0x009f, /* mmagic_mdss_axi_clk */
	0x00a0, /* smmu_video_ahb_clk */
	0x00a1, /* smmu_video_axi_clk */
	0x00a2, /* mmagic_video_axi_clk */
	0x00ad, /* mmagic_camss_noc_cfg_ahb_clk */
	0x00ae, /* mmagic_mdss_noc_cfg_ahb_clk */
	0x00af, /* mmagic_video_noc_cfg_ahb_clk */
	0x00b0, /* mmagic_bimc_noc_cfg_ahb_clk */
	0x000c, /* gpu_ahb_clk */
	0x000d, /* gpu_gx_gfx3d_clk */
	0x003e, /* gpu_gx_rbbmtimer_clk */
	0x0088, /* gpu_aon_isense_clk */
};

static struct clk_debug_mux mmss_debug_mux = {
	.priv = &measure_data,
	.debug_offset = 0x900,
	.cbcr_offset = 0x900,
	.en_mask = BIT(16),
	.src_sel_mask = 0x3ff,
	.post_div_offset = 0x900,
	.post_div_mask = GENMASK(18, 17),
	.post_div_shift = 17,
	.post_div_val = 1,
	.mux_sels = mmss_debug_selectors,
	.hw.init = &(struct clk_init_data){
		.name = "mmss_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = mmss_debug_parents,
		.num_parents = ARRAY_SIZE(mmss_debug_parents),
		.flags = CLK_IS_MEASURE,
	},
};

static const char * const gcc_debug_parents[] = {
	"mmss_debug_mux",
	"cpu_debug_mux",
	"cnoc_clk",
	"pcnoc_clk",
	"snoc_clk",
	"bimc_clk",
	"ce1_clk",
	"gcc_ce1_axi_m_clk",
	"gcc_ce1_ahb_m_clk",
	"measure_only_bimc_hmss_axi_clk",
	"gcc_periph_noc_usb20_ahb_clk",
	"gcc_mmss_noc_cfg_ahb_clk",
	"gcc_mmss_bimc_gfx_clk",
	"gcc_bimc_gfx_clk",
	"gcc_sys_noc_usb3_axi_clk",
	"gcc_sys_noc_ufs_axi_clk",
	"gcc_usb30_master_clk",
	"gcc_usb30_sleep_clk",
	"gcc_usb30_mock_utmi_clk",
	"gcc_usb3_phy_aux_clk",
	"gcc_usb3_phy_pipe_clk",
	"gcc_usb20_master_clk",
	"gcc_usb20_sleep_clk",
	"gcc_usb20_mock_utmi_clk",
	"gcc_usb_phy_cfg_ahb2phy_clk",
	"gcc_sdcc1_apps_clk",
	"gcc_sdcc1_ahb_clk",
	"gcc_sdcc2_apps_clk",
	"gcc_sdcc2_ahb_clk",
	"gcc_sdcc3_apps_clk",
	"gcc_sdcc3_ahb_clk",
	"gcc_sdcc4_apps_clk",
	"gcc_sdcc4_ahb_clk",
	"gcc_blsp1_ahb_clk",
	"gcc_blsp1_qup1_spi_apps_clk",
	"gcc_blsp1_qup1_i2c_apps_clk",
	"gcc_blsp1_uart1_apps_clk",
	"gcc_blsp1_qup2_spi_apps_clk",
	"gcc_blsp1_qup2_i2c_apps_clk",
	"gcc_blsp1_uart2_apps_clk",
	"gcc_blsp1_qup3_spi_apps_clk",
	"gcc_blsp1_qup3_i2c_apps_clk",
	"gcc_blsp1_uart3_apps_clk",
	"gcc_blsp1_qup4_spi_apps_clk",
	"gcc_blsp1_qup4_i2c_apps_clk",
	"gcc_blsp1_uart4_apps_clk",
	"gcc_blsp1_qup5_spi_apps_clk",
	"gcc_blsp1_qup5_i2c_apps_clk",
	"gcc_blsp1_uart5_apps_clk",
	"gcc_blsp1_qup6_spi_apps_clk",
	"gcc_blsp1_qup6_i2c_apps_clk",
	"gcc_blsp1_uart6_apps_clk",
	"gcc_blsp2_ahb_clk",
	"gcc_blsp2_qup1_spi_apps_clk",
	"gcc_blsp2_qup1_i2c_apps_clk",
	"gcc_blsp2_uart1_apps_clk",
	"gcc_blsp2_qup2_spi_apps_clk",
	"gcc_blsp2_qup2_i2c_apps_clk",
	"gcc_blsp2_uart2_apps_clk",
	"gcc_blsp2_qup3_spi_apps_clk",
	"gcc_blsp2_qup3_i2c_apps_clk",
	"gcc_blsp2_uart3_apps_clk",
	"gcc_blsp2_qup4_spi_apps_clk",
	"gcc_blsp2_qup4_i2c_apps_clk",
	"gcc_blsp2_uart4_apps_clk",
	"gcc_blsp2_qup5_spi_apps_clk",
	"gcc_blsp2_qup5_i2c_apps_clk",
	"gcc_blsp2_uart5_apps_clk",
	"gcc_blsp2_qup6_spi_apps_clk",
	"gcc_blsp2_qup6_i2c_apps_clk",
	"gcc_blsp2_uart6_apps_clk",
	"gcc_pdm_ahb_clk",
	"gcc_pdm2_clk",
	"gcc_prng_ahb_clk",
	"gcc_tsif_ahb_clk",
	"gcc_tsif_ref_clk",
	"gcc_boot_rom_ahb_clk",
	"gcc_gp1_clk",
	"gcc_gp2_clk",
	"gcc_gp3_clk",
	"gcc_hmss_rbcpr_clk",
	"gcc_pcie_0_slv_axi_clk",
	"gcc_pcie_0_mstr_axi_clk",
	"gcc_pcie_0_cfg_ahb_clk",
	"gcc_pcie_0_aux_clk",
	"gcc_pcie_0_pipe_clk",
	"gcc_pcie_1_slv_axi_clk",
	"gcc_pcie_1_mstr_axi_clk",
	"gcc_pcie_1_cfg_ahb_clk",
	"gcc_pcie_1_aux_clk",
	"gcc_pcie_1_pipe_clk",
	"gcc_pcie_2_slv_axi_clk",
	"gcc_pcie_2_mstr_axi_clk",
	"gcc_pcie_2_cfg_ahb_clk",
	"gcc_pcie_2_aux_clk",
	"gcc_pcie_2_pipe_clk",
	"gcc_pcie_phy_cfg_ahb_clk",
	"gcc_pcie_phy_aux_clk",
	"gcc_ufs_axi_clk",
	"gcc_ufs_ahb_clk",
	"gcc_ufs_tx_cfg_clk",
	"gcc_ufs_rx_cfg_clk",
	"gcc_ufs_tx_symbol_0_clk",
	"gcc_ufs_rx_symbol_0_clk",
	"gcc_ufs_rx_symbol_1_clk",
	"gcc_ufs_unipro_core_clk",
	"gcc_ufs_ice_core_clk",
	"gcc_ufs_sys_clk_core_clk",
	"gcc_ufs_tx_symbol_clk_core_clk",
	"gcc_aggre0_snoc_axi_clk",
	"gcc_aggre0_cnoc_ahb_clk",
	"gcc_smmu_aggre0_axi_clk",
	"gcc_smmu_aggre0_ahb_clk",
	"gcc_aggre0_noc_qosgen_extref_clk",
	"gcc_aggre2_ufs_axi_clk",
	"gcc_aggre2_usb3_axi_clk",
	"gcc_dcc_ahb_clk",
	"gcc_aggre0_noc_mpu_cfg_ahb_clk",
	"ipa_clk",
	"gcc_mss_cfg_ahb_clk",
	"gcc_mss_mnoc_bimc_axi_clk",
	"gcc_mss_snoc_axi_clk",
	"gcc_mss_q6_bimc_axi_clk",
};

static int gcc_debug_selectors[] = {
	0x001b, /* mmss_debug_mux */
	0x00bb, /* cpu_debug_mux */
	0x000e, /* cnoc_clk */
	0x0011, /* pcnoc_clk */
	0x0000, /* snoc_clk */
	0x00ad, /* bimc_clk */
	0x0099, /* ce1_clk */
	0x009a, /* gcc_ce1_axi_m_clk */
	0x009b, /* gcc_ce1_ahb_m_clk */
	0x00a5, /* measure_only_bimc_hmss_axi_clk */
	0x0014, /* gcc_periph_noc_usb20_ahb_clk */
	0x0019, /* gcc_mmss_noc_cfg_ahb_clk */
	0x001c, /* gcc_mmss_bimc_gfx_clk */
	0x00af, /* gcc_bimc_gfx_clk */
	0x0006, /* gcc_sys_noc_usb3_axi_clk */
	0x0007, /* gcc_sys_noc_ufs_axi_clk */
	0x002d, /* gcc_usb30_master_clk */
	0x002e, /* gcc_usb30_sleep_clk */
	0x002f, /* gcc_usb30_mock_utmi_clk */
	0x0030, /* gcc_usb3_phy_aux_clk */
	0x0031, /* gcc_usb3_phy_pipe_clk */
	0x0035, /* gcc_usb20_master_clk */
	0x0036, /* gcc_usb20_sleep_clk */
	0x0037, /* gcc_usb20_mock_utmi_clk */
	0x0038, /* gcc_usb_phy_cfg_ahb2phy_clk */
	0x0039, /* gcc_sdcc1_apps_clk */
	0x003a, /* gcc_sdcc1_ahb_clk */
	0x003b, /* gcc_sdcc2_apps_clk */
	0x003c, /* gcc_sdcc2_ahb_clk */
	0x003d, /* gcc_sdcc3_apps_clk */
	0x003e, /* gcc_sdcc3_ahb_clk */
	0x003f, /* gcc_sdcc4_apps_clk */
	0x0040, /* gcc_sdcc4_ahb_clk */
	0x0041, /* gcc_blsp1_ahb_clk */
	0x0043, /* gcc_blsp1_qup1_spi_apps_clk */
	0x0044, /* gcc_blsp1_qup1_i2c_apps_clk */
	0x0045, /* gcc_blsp1_uart1_apps_clk */
	0x0047, /* gcc_blsp1_qup2_spi_apps_clk */
	0x0048, /* gcc_blsp1_qup2_i2c_apps_clk */
	0x0049, /* gcc_blsp1_uart2_apps_clk */
	0x004b, /* gcc_blsp1_qup3_spi_apps_clk */
	0x004c, /* gcc_blsp1_qup3_i2c_apps_clk */
	0x004d, /* gcc_blsp1_uart3_apps_clk */
	0x004f, /* gcc_blsp1_qup4_spi_apps_clk */
	0x0050, /* gcc_blsp1_qup4_i2c_apps_clk */
	0x0051, /* gcc_blsp1_uart4_apps_clk */
	0x0053, /* gcc_blsp1_qup5_spi_apps_clk */
	0x0054, /* gcc_blsp1_qup5_i2c_apps_clk */
	0x0055, /* gcc_blsp1_uart5_apps_clk */
	0x0057, /* gcc_blsp1_qup6_spi_apps_clk */
	0x0058, /* gcc_blsp1_qup6_i2c_apps_clk */
	0x0059, /* gcc_blsp1_uart6_apps_clk */
	0x005b, /* gcc_blsp2_ahb_clk */
	0x005d, /* gcc_blsp2_qup1_spi_apps_clk */
	0x005e, /* gcc_blsp2_qup1_i2c_apps_clk */
	0x005f, /* gcc_blsp2_uart1_apps_clk */
	0x0061, /* gcc_blsp2_qup2_spi_apps_clk */
	0x0062, /* gcc_blsp2_qup2_i2c_apps_clk */
	0x0063, /* gcc_blsp2_uart2_apps_clk */
	0x0065, /* gcc_blsp2_qup3_spi_apps_clk */
	0x0066, /* gcc_blsp2_qup3_i2c_apps_clk */
	0x0067, /* gcc_blsp2_uart3_apps_clk */
	0x0069, /* gcc_blsp2_qup4_spi_apps_clk */
	0x006a, /* gcc_blsp2_qup4_i2c_apps_clk */
	0x006b, /* gcc_blsp2_uart4_apps_clk */
	0x006d, /* gcc_blsp2_qup5_spi_apps_clk */
	0x006e, /* gcc_blsp2_qup5_i2c_apps_clk */
	0x006f, /* gcc_blsp2_uart5_apps_clk */
	0x0071, /* gcc_blsp2_qup6_spi_apps_clk */
	0x0072, /* gcc_blsp2_qup6_i2c_apps_clk */
	0x0073, /* gcc_blsp2_uart6_apps_clk */
	0x0076, /* gcc_pdm_ahb_clk */
	0x0078, /* gcc_pdm2_clk */
	0x0079, /* gcc_prng_ahb_clk */
	0x007a, /* gcc_tsif_ahb_clk */
	0x007b, /* gcc_tsif_ref_clk */
	0x007e, /* gcc_boot_rom_ahb_clk */
	0x00e3, /* gcc_gp1_clk */
	0x00e4, /* gcc_gp2_clk */
	0x00e5, /* gcc_gp3_clk */
	0x00ba, /* gcc_hmss_rbcpr_clk */
	0x00e6, /* gcc_pcie_0_slv_axi_clk */
	0x00e7, /* gcc_pcie_0_mstr_axi_clk */
	0x00e8, /* gcc_pcie_0_cfg_ahb_clk */
	0x00e9, /* gcc_pcie_0_aux_clk */
	0x00ea, /* gcc_pcie_0_pipe_clk */
	0x00ec, /* gcc_pcie_1_slv_axi_clk */
	0x00ed, /* gcc_pcie_1_mstr_axi_clk */
	0x00ee, /* gcc_pcie_1_cfg_ahb_clk */
	0x00ef, /* gcc_pcie_1_aux_clk */
	0x00f0, /* gcc_pcie_1_pipe_clk */
	0x00f2, /* gcc_pcie_2_slv_axi_clk */
	0x00f3, /* gcc_pcie_2_mstr_axi_clk */
	0x00f4, /* gcc_pcie_2_cfg_ahb_clk */
	0x00f5, /* gcc_pcie_2_aux_clk */
	0x00f6, /* gcc_pcie_2_pipe_clk */
	0x00f8, /* gcc_pcie_phy_cfg_ahb_clk */
	0x00f9, /* gcc_pcie_phy_aux_clk */
	0x00fc, /* gcc_ufs_axi_clk */
	0x00fd, /* gcc_ufs_ahb_clk */
	0x00fe, /* gcc_ufs_tx_cfg_clk */
	0x00ff, /* gcc_ufs_rx_cfg_clk */
	0x0100, /* gcc_ufs_tx_symbol_0_clk */
	0x0101, /* gcc_ufs_rx_symbol_0_clk */
	0x0102, /* gcc_ufs_rx_symbol_1_clk */
	0x0106, /* gcc_ufs_unipro_core_clk */
	0x0107, /* gcc_ufs_ice_core_clk */
	0x108, /* gcc_ufs_sys_clk_core_clk */
	0x0109, /* gcc_ufs_tx_symbol_clk_core_clk */
	0x0116, /* gcc_aggre0_snoc_axi_clk */
	0x0117, /* gcc_aggre0_cnoc_ahb_clk */
	0x0119, /* gcc_smmu_aggre0_axi_clk */
	0x011a, /* gcc_smmu_aggre0_ahb_clk */
	0x011b, /* gcc_aggre0_noc_qosgen_extref_clk */
	0x0126, /* gcc_aggre2_ufs_axi_clk */
	0x0127, /* gcc_aggre2_usb3_axi_clk */
	0x012b, /* gcc_dcc_ahb_clk */
	0x012c, /* gcc_aggre0_noc_mpu_cfg_ahb_clk */
	0x12f, /* ipa_clk */
	0x0133, /* gcc_mss_cfg_ahb_clk */
	0x0134, /* gcc_mss_mnoc_bimc_axi_clk */
	0x0135, /* gcc_mss_snoc_axi_clk */
	0x0136, /* gcc_mss_q6_bimc_axi_clk */
};

static struct clk_debug_mux gcc_debug_mux = {
	.priv = &measure_data,
	.debug_offset = 0x62000,
	.cbcr_offset = 0x62000,
	.en_mask = BIT(16),
	.src_sel_mask = 0x3ff,
	.post_div_offset = 0x62000,
	.post_div_mask = 0,
	.post_div_shift = 0,
	.post_div_val = 1,
	.mux_sels = gcc_debug_selectors,
	.hw.init = &(struct clk_init_data){
		.name = "gcc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = gcc_debug_parents,
		.num_parents = ARRAY_SIZE(gcc_debug_parents),
		.flags = CLK_IS_MEASURE,
	},
};

static struct clk_dummy measure_only_bimc_hmss_axi_clk = {
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_bimc_hmss_axi_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct regmap *debug_regmap(struct platform_device *pdev, const char *property)
{
	struct device_node *node = of_parse_phandle(pdev->dev.of_node, property, 0);
	struct platform_device *controller;
	struct regmap *map;

	if (!node)
		return ERR_PTR(-EINVAL);
	controller = of_find_device_by_node(node);
	of_node_put(node);
	if (!controller)
		return ERR_PTR(-EPROBE_DEFER);
	map = dev_get_regmap(&controller->dev, NULL);
	put_device(&controller->dev);
	return map ?: ERR_PTR(-EPROBE_DEFER);
}

static struct clk_hw *debug_clk_get(struct of_phandle_args *spec, void *data)
{
	if (spec->args_count != 1 || spec->args[0])
		return ERR_PTR(-EINVAL);
	return data;
}

static int debugcc_msm8996_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk *clock;
	int ret;

	BUILD_BUG_ON(ARRAY_SIZE(mmss_debug_parents) != ARRAY_SIZE(mmss_debug_selectors));
	BUILD_BUG_ON(ARRAY_SIZE(gcc_debug_parents) != ARRAY_SIZE(gcc_debug_selectors));
	clock = devm_clk_get(dev, "xo");
	if (IS_ERR(clock))
		return PTR_ERR(clock);
	measure_data.cxo = clock;
	gcc_debug_mux.regmap = debug_regmap(pdev, "qcom,gcc");
	if (IS_ERR(gcc_debug_mux.regmap))
		return PTR_ERR(gcc_debug_mux.regmap);
	mmss_debug_mux.regmap = debug_regmap(pdev, "qcom,mmcc");
	if (IS_ERR(mmss_debug_mux.regmap))
		return PTR_ERR(mmss_debug_mux.regmap);
	ret = devm_clk_hw_register(dev, &mmss_debug_mux.hw);
	if (ret)
		return ret;
	ret = devm_clk_hw_register(dev, &gcc_debug_mux.hw);
	if (ret)
		return ret;
	ret = devm_clk_hw_register(dev, &measure_only_bimc_hmss_axi_clk.hw);
	if (ret)
		return ret;
	ret = regmap_write(gcc_debug_mux.regmap, 0x6200c, 0x51a00);
	if (ret)
		return ret;
	ret = clk_debug_measure_register(&gcc_debug_mux.hw);
	if (ret)
		return ret;
	return devm_of_clk_add_hw_provider(dev, debug_clk_get, &gcc_debug_mux.hw);
}

static const struct of_device_id debugcc_msm8996_match[] = {
	{ .compatible = "qcom,msm8996-debugcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, debugcc_msm8996_match);

static struct platform_driver debugcc_msm8996_driver = {
	.probe = debugcc_msm8996_probe,
	.driver = {
		.name = "msm8996-debugcc",
		.of_match_table = debugcc_msm8996_match,
		.suppress_bind_attrs = true,
	},
};

static int __init debugcc_msm8996_init(void)
{
	return platform_driver_register(&debugcc_msm8996_driver);
}
fs_initcall(debugcc_msm8996_init);
MODULE_DESCRIPTION("MSM8996 clock measurement providers");
MODULE_LICENSE("GPL v2");
