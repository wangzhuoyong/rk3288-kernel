/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <media/v4l2-common.h>
#include "regs.h"

void disable_dcrop(struct rkisp1_stream *stream, bool async)
{
	void __iomem *base = stream->base_addr;
	void __iomem *dc_ctrl_addr = base + stream->regs->dual_crop_ctrl;
	u32 dc_ctrl = readl(dc_ctrl_addr);
	u32 mask = ~(stream->regs->dual_crop_yuvmode_mask | stream->regs->dual_crop_rawmode_mask );
	u32 val = dc_ctrl & mask;

	if (async)
		val |= CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		val |= CIF_DUAL_CROP_CFG_UPD;
	writel(val, dc_ctrl_addr);
}

void config_dcrop(struct rkisp1_stream *stream, struct v4l2_rect *rect, bool async)
{
	void __iomem *base = stream->base_addr;
	void __iomem *dc_ctrl_addr = base + stream->regs->dual_crop_ctrl;
	u32 dc_ctrl = readl(dc_ctrl_addr);

	writel(rect->left, base + stream->regs->dual_crop_h_offset);
	writel(rect->top, base + stream->regs->dual_crop_v_offset);
	writel(rect->width, base + stream->regs->dual_crop_h_size);
	writel(rect->height, base + stream->regs->dual_crop_v_size);
	dc_ctrl |= stream->regs->dual_crop_yuvmode_mask;
	if (async)
		dc_ctrl |= CIF_DUAL_CROP_GEN_CFG_UPD;
	else
		dc_ctrl |= CIF_DUAL_CROP_CFG_UPD;
	writel(dc_ctrl, dc_ctrl_addr);
}

void mp_dump_rsz_regs(void __iomem *base)
{
	pr_info("MRSZ_CTRL 0x%08x/0x%08x\n"
			"MRSZ_SCALE_HY %d/%d\n"
			"MRSZ_SCALE_HCB %d/%d\n"
			"MRSZ_SCALE_HCR %d/%d\n"
			"MRSZ_SCALE_VY %d/%d\n"
			"MRSZ_SCALE_VC %d/%d\n"
			"MRSZ_PHASE_HY %d/%d\n"
			"MRSZ_PHASE_HC %d/%d\n"
			"MRSZ_PHASE_VY %d/%d\n"
			"MRSZ_PHASE_VC %d/%d\n",
			readl(base + CIF_MRSZ_CTRL),
			readl(base + CIF_MRSZ_CTRL_SHD),
			readl(base + CIF_MRSZ_SCALE_HY),
			readl(base + CIF_MRSZ_SCALE_HY_SHD),
			readl(base + CIF_MRSZ_SCALE_HCB),
			readl(base + CIF_MRSZ_SCALE_HCB_SHD),
			readl(base + CIF_MRSZ_SCALE_HCR),
			readl(base + CIF_MRSZ_SCALE_HCR_SHD),
			readl(base + CIF_MRSZ_SCALE_VY),
			readl(base + CIF_MRSZ_SCALE_VY_SHD),
			readl(base + CIF_MRSZ_SCALE_VC),
			readl(base + CIF_MRSZ_SCALE_VC_SHD),
			readl(base + CIF_MRSZ_PHASE_HY),
			readl(base + CIF_MRSZ_PHASE_HY_SHD),
			readl(base + CIF_MRSZ_PHASE_HC),
			readl(base + CIF_MRSZ_PHASE_HC_SHD),
			readl(base + CIF_MRSZ_PHASE_VY),
			readl(base + CIF_MRSZ_PHASE_VY_SHD),
			readl(base + CIF_MRSZ_PHASE_VC),
			readl(base + CIF_MRSZ_PHASE_VC_SHD));
}

void sp_dump_rsz_regs(void __iomem *base)
{
	pr_info("SRSZ_CTRL 0x%08x/0x%08x\n"
			"SRSZ_SCALE_HY %d/%d\n"
			"SRSZ_SCALE_HCB %d/%d\n"
			"SRSZ_SCALE_HCR %d/%d\n"
			"SRSZ_SCALE_VY %d/%d\n"
			"SRSZ_SCALE_VC %d/%d\n"
			"SRSZ_PHASE_HY %d/%d\n"
			"SRSZ_PHASE_HC %d/%d\n"
			"SRSZ_PHASE_VY %d/%d\n"
			"SRSZ_PHASE_VC %d/%d\n",
			readl(base + CIF_SRSZ_CTRL),
			readl(base + CIF_SRSZ_CTRL_SHD),
			readl(base + CIF_SRSZ_SCALE_HY),
			readl(base + CIF_SRSZ_SCALE_HY_SHD),
			readl(base + CIF_SRSZ_SCALE_HCB),
			readl(base + CIF_SRSZ_SCALE_HCB_SHD),
			readl(base + CIF_SRSZ_SCALE_HCR),
			readl(base + CIF_SRSZ_SCALE_HCR_SHD),
			readl(base + CIF_SRSZ_SCALE_VY),
			readl(base + CIF_SRSZ_SCALE_VY_SHD),
			readl(base + CIF_SRSZ_SCALE_VC),
			readl(base + CIF_SRSZ_SCALE_VC_SHD),
			readl(base + CIF_SRSZ_PHASE_HY),
			readl(base + CIF_SRSZ_PHASE_HY_SHD),
			readl(base + CIF_SRSZ_PHASE_HC),
			readl(base + CIF_SRSZ_PHASE_HC_SHD),
			readl(base + CIF_SRSZ_PHASE_VY),
			readl(base + CIF_SRSZ_PHASE_VY_SHD),
			readl(base + CIF_SRSZ_PHASE_VC),
			readl(base + CIF_SRSZ_PHASE_VC_SHD));
}

void set_scale(struct rkisp1_stream *stream, struct rkisp1_win *in_y,
		struct rkisp1_win *in_c, struct rkisp1_win *out_y,
		struct rkisp1_win *out_c)
{
	void __iomem *base = stream->base_addr;
	void __iomem *scale_hy_addr = base + stream->regs->scale_hy;
	void __iomem *scale_hcr_addr = base + stream->regs->scale_hcr;
	void __iomem *scale_hcb_addr = base + stream->regs->scale_hcb;
	void __iomem *scale_vy_addr = base + stream->regs->scale_vy;
	void __iomem *scale_vc_addr = base + stream->regs->scale_vc;
	void __iomem *rsz_ctrl_addr = base + stream->regs->rsz_ctrl;
	u32 scale_hy, scale_hc, scale_vy, scale_vc, rsz_ctrl = 0;

	if (in_y->w < out_y->w) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HY_ENABLE |
				CIF_RSZ_CTRL_SCALE_HY_UP;
		scale_hy = ((in_y->w - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_y->w - 1);
		writel(scale_hy, scale_hy_addr);
	} else if (in_y->w > out_y->w) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HY_ENABLE;
		scale_hy = ((out_y->w - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_y->w - 1) + 1;
		writel(scale_hy, scale_hy_addr);
	}
	if (in_c->w < out_c->w) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HC_ENABLE |
				CIF_RSZ_CTRL_SCALE_HC_UP;
		scale_hc = ((in_c->w - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_c->w - 1);
		writel(scale_hc, scale_hcb_addr);
		writel(scale_hc, scale_hcr_addr);
	} else if (in_c->w > out_c->w) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_HC_ENABLE;
		scale_hc = ((out_c->w - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_c->w - 1) + 1;
		writel(scale_hc, scale_hcb_addr);
		writel(scale_hc, scale_hcr_addr);
	}

	if (in_y->h < out_y->h) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VY_ENABLE |
				CIF_RSZ_CTRL_SCALE_VY_UP;
		scale_vy = ((in_y->h - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_y->h - 1);
		writel(scale_vy, scale_vy_addr);
	} else if (in_y->h > out_y->h) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VY_ENABLE;
		scale_vy = ((out_y->h - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_y->h - 1) + 1;
		writel(scale_vy, scale_vy_addr);
	}

	if (in_c->h < out_c->h) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VC_ENABLE |
				CIF_RSZ_CTRL_SCALE_VC_UP;
		scale_vc = ((in_c->h - 1) * CIF_RSZ_SCALER_FACTOR) /
				(out_c->h - 1);
		writel(scale_vc, scale_vc_addr);
	} else if (in_c->h > out_c->h) {
		rsz_ctrl |= CIF_RSZ_CTRL_SCALE_VC_ENABLE;
		scale_vc = ((out_c->h - 1) * CIF_RSZ_SCALER_FACTOR) /
				(in_c->h - 1) + 1;
		writel(scale_vc, scale_vc_addr);
	}

	writel(rsz_ctrl, rsz_ctrl_addr);
}

