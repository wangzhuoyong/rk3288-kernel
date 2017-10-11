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

#ifndef _RKISP1_COMMON_H
#define _RKISP1_COMMON_H

#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

#define DRIVER_NAME "rkisp1"
#define ISP_VDEV_NAME DRIVER_NAME  "_ispdev"
#define SP_VDEV_NAME DRIVER_NAME   "_selfpath"
#define MP_VDEV_NAME DRIVER_NAME   "_mainpath"
#define DMA_VDEV_NAME DRIVER_NAME  "_dmapath"

#define GRP_ID_SENSOR			BIT(0)
#define GRP_ID_MIPIPHY			BIT(1)
#define GRP_ID_ISP			BIT(2)
#define GRP_ID_ISP_MP			BIT(3)
#define GRP_ID_ISP_SP			BIT(4)

#define RKISP1_DEFAULT_WIDTH 800
#define RKISP1_DEFAULT_HEIGHT 600

#define RKISP1_MAX_STREAM	2
#define RKISP1_STREAM_SP	0
#define RKISP1_STREAM_MP	1

#define RKISP1_PLANE_Y 0
#define RKISP1_PLANE_CB 1
#define RKISP1_PLANE_CR 2

enum isp_subdev_index {
	IDX_SENSOR,
	IDX_MIPIPHY,
	IDX_ISP,
	IDX_MAX,
};

enum rkisp1_sd_type {
	RKISP1_SD_SENSOR,
	RKISP1_SD_PHY_CSI,
	RKISP1_SD_VCM,
	RKISP1_SD_FLASH,
	RKISP1_SD_MAX,
};

struct rkisp1_pipeline;

/*
 * struct rkisp1_pipeline - An ISP hardware pipeline
 * @entities: Bitmask of entities in the pipeline (indexed by entity ID)
 */
struct rkisp1_pipeline {
	struct media_pipeline pipe;
	struct v4l2_subdev *subdevs[IDX_MAX];
	int (*open) (struct rkisp1_pipeline *p,
					struct media_entity *me, bool prepare);
	int (*close) (struct rkisp1_pipeline *p);
	int (*set_stream) (struct rkisp1_pipeline *p, bool on);
};

/* One structure per video node */
struct rkisp1_vdev_node {
	struct vb2_queue buf_queue;
	struct mutex vlock;
	struct video_device vdev;
	struct media_pad pad;
};

enum rkisp1_fmt_pix_type {
	FMT_YUV,
	FMT_RGB,
	FMT_BAYER,
	FMT_JPEG,
	FMT_MAX
};

enum rkisp1_fmt_raw_pat_type {
	RAW_BGGR,
	RAW_GBRG,
	RAW_GRBG,
	RAW_RGGB,
	RAW_MAX,
};

/*
  * @fourcc: pixel format
  * @mbus_code: pixel format over bus
  * @colorspace: enum of v4l2_colorspace
  * @fmt_type: helper filed for pixel format
  * @bpp: bits per pixel
  * @xsubs: horizon color samples in a 4*4 matrix, for yuv
  * @ysubs: vertical color samples in a 4*4 matrix, for yuv
  * @bayer_pat: bayer patten type
  * @cplanes: number of colour planes
  * @mplanes: number of stored memory planes
  * @uv_swap: if cb cr swaped, for yuv
  * @yc_swap: if cb/cr comes before y, for yuv
  * @write_format: defines how YCbCr self picture data is written to memory
  * @input_format: defines sp input format
  * @output_format: defines sp output format
  */
struct rkisp1_fmt {
	u32 fourcc;
	u32 mbus_code;
	u8 colorspace;
	u8 fmt_type;
	u8 xsubs;
	u8 ysubs;
	u8 bayer_pat;
	u8 cplanes;
	u8 mplanes;
	u8 uv_swap;
	u8 yc_swap;
	u32 write_format;
	u32 input_format;
	u32 output_format;
	u32 mipi_data_type;
	u8 bpp[VIDEO_MAX_PLANES];
};

enum rkisp1_state {
	/* path not yet opened: */
	RKISP1_STATE_DISABLED,
	/* path opened and configured, ready for streaming: */
	RKISP1_STATE_READY,
	/* path is streaming: */
	RKISP1_STATE_STREAMING
};

struct rkisp1_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	u32 buff_addr[VIDEO_MAX_PLANES];
};

struct rkisp1_win {
	int w;
	int h;
};

enum rkisp1_sp_inp {
	RKISP1_SP_INP_ISP,
	RKISP1_SP_INP_DMA_SP,
	RKISP1_SP_INP_MAX
};

struct rkisp1_device;
struct rkisp1_stream;
struct streams_regs;

struct streams_ops {
	void (*stream_init)(struct rkisp1_stream *stream);
	int (*check_against)(struct rkisp1_stream *stream);
	void (*config_mi)(struct rkisp1_stream *stream);
	void (*stop_mi)(struct rkisp1_stream *stream);
	void (*enable_mi)(struct rkisp1_stream *stream);
	void (*disable_mi)(struct rkisp1_stream *stream);
	void (*set_data_path)(void __iomem *base);
	void (*clr_frame_end_int)(void __iomem *base);
	u32 (*is_frame_end_int_masked)(void __iomem *base);
};

struct rkisp1_stream_sp {
	enum rkisp1_sp_inp input_sel;
};

struct rkisp1_stream_mp {
	bool raw_enable;
};

struct rkisp1_stream {
	u32 id;
	struct rkisp1_device *ispdev;
	struct rkisp1_vdev_node vnode;
	enum rkisp1_state state;
	enum rkisp1_state saved_state;
	const struct rkisp1_fmt *fmts;
	int fmt_size;
	struct rkisp1_fmt out_isp_fmt;
	struct v4l2_pix_format_mplane out_fmt;
	struct v4l2_rect dcrop;
	struct streams_ops *ops;
	struct streams_regs *regs;
	/* spinlock for videobuf queues */
	spinlock_t vbq_lock;
	/* mi config */
	struct list_head buf_queue;
	struct rkisp1_buffer *curr_buf;
	struct rkisp1_buffer *next_buf;
	bool stop;
	wait_queue_head_t done;

	struct rkisp1_stream_sp sp_config;
	struct rkisp1_stream_mp mp_config;
};

extern int rkisp1_debug;

static inline
struct rkisp1_vdev_node *vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct rkisp1_vdev_node, vdev);
}

static inline struct rkisp1_vdev_node *queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct rkisp1_vdev_node, buf_queue);
}

static inline struct rkisp1_buffer *to_rkisp1_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rkisp1_buffer, vb);
}

static inline struct vb2_queue *to_vb2_queue(struct file *file)
{
	struct rkisp1_vdev_node *vnode = video_drvdata(file);

	return &vnode->buf_queue;
}

#endif /* _RKISP1_COMMON_H */
