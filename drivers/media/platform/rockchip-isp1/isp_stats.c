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
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP statistics */
#include "regs.h"
#include "isp_stats.h"
#include "isp_params.h"
#include "rkisp1.h"

#define RKISP1_ISP_STATS_REQ_BUFS_MIN 2
#define RKISP1_ISP_STATS_REQ_BUFS_MAX 8


/* TODO: should add RKISP1_STATS_FMT pixel format  to V4L2 framework ? */
static int rkisp1_stats_g_fmt_vid_cap(struct file *file, void *fh,
				      struct v4l2_format *f)
{
	/*
	 * Dummy function needed to allow allocation of
	 * buffers on this device
	 */
	return 0;
	/* return -EPERM; */
}

static int rkisp1_stats_s_fmt_vid_cap(struct file *file, void *priv,
				      struct v4l2_format *f)
{
	/*
	 * Dummy function needed to allow allocation of
	 * buffers on this device
	 */
	return 0;
	/* return -EPERM; */
}

static int rkisp1_stats_try_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	/*
	 * Dummy function needed to allow allocation of
	 * buffers on this device
	 */
	return 0;
	/* return -EPERM; */
}

static int rkisp1_stats_querycap(struct file *file,
				 void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);

	strcpy(cap->driver, DRIVER_NAME);
	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	strlcpy(cap->bus_info, "platform: " DRIVER_NAME, sizeof(cap->bus_info));

	cap->device_caps = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_STREAMING;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 1)
	cap->capabilities = cap->device_caps | V4L2_CAP_VIDEO_CAPTURE;
#else
	cap->capabilities = cap->device_caps | V4L2_CAP_META_CAPTURE;
#endif

	return 0;
}

/* ISP video device IOCTLs */
static const struct v4l2_ioctl_ops rkisp1_stats_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 1)
	.vidioc_g_fmt_vid_cap = rkisp1_stats_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = rkisp1_stats_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = rkisp1_stats_try_fmt_vid_cap,
#else
	/* TODO: _meta_cap ops*/
#endif
	.vidioc_querycap = rkisp1_stats_querycap
};

struct v4l2_file_operations rkisp1_stats_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = v4l2_fh_open,
	.release = vb2_fop_release
};

static int rkisp1_stats_vb2_queue_setup(struct vb2_queue *vq,
					const void *parg,
					unsigned int *count,
					unsigned int *num_planes,
					unsigned int sizes[],
					void *alloc_ctxs[])
{
	sizes[0] = sizeof(struct cifisp_stat_buffer);
	*num_planes = 1;

	*count = clamp_t(u32, *count, RKISP1_ISP_STATS_REQ_BUFS_MIN,
			 RKISP1_ISP_STATS_REQ_BUFS_MAX);
	return 0;
}

static void rkisp1_stats_vb2_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_buffer *stats_buf = to_rkisp1_buffer(vbuf);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkisp1_isp_stats_vdev *stats_dev = vq->drv_priv;
	unsigned long flags;

	spin_lock_irqsave(&stats_dev->irq_lock, flags);
	list_add_tail(&stats_buf->queue, &stats_dev->stat);
	spin_unlock_irqrestore(&stats_dev->irq_lock, flags);
}

static void rkisp1_stats_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkisp1_isp_stats_vdev *stats_vdev = vq->drv_priv;
	struct rkisp1_buffer *buf;
	unsigned long flags;
	int i;

	/* stop stats received firstly */
	spin_lock_irqsave(&stats_vdev->irq_lock, flags);
	stats_vdev->streamon = false;
	spin_unlock_irqrestore(&stats_vdev->irq_lock, flags);

	drain_workqueue(stats_vdev->readout_wq);

	for (i = 0; i < RKISP1_ISP_STATS_REQ_BUFS_MAX; i++) {
		spin_lock_irqsave(&stats_vdev->irq_lock, flags);
		if (!list_empty(&stats_vdev->stat)) {
			buf = list_first_entry(&stats_vdev->stat,
					       struct rkisp1_buffer, queue);
			list_del(&buf->queue);
			spin_unlock_irqrestore(&stats_vdev->irq_lock, flags);
		} else {
			spin_unlock_irqrestore(&stats_vdev->irq_lock, flags);
			break;
		}

		if (buf)
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		buf = NULL;
	}
}

static int
rkisp1_stats_vb2_start_streaming(struct vb2_queue *queue,
				 unsigned int count)
{
	struct rkisp1_isp_stats_vdev *stats_vdev = queue->drv_priv;
	unsigned long flags;

	spin_lock_irqsave(&stats_vdev->irq_lock, flags);
	stats_vdev->streamon = true;
	INIT_LIST_HEAD(&stats_vdev->stat);
	spin_unlock_irqrestore(&stats_vdev->irq_lock, flags);

	return 0;
}

static struct vb2_ops rkisp1_stats_vb2_ops = {
	.queue_setup = rkisp1_stats_vb2_queue_setup,
	.buf_queue = rkisp1_stats_vb2_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.stop_streaming = rkisp1_stats_vb2_stop_streaming,
	.start_streaming = rkisp1_stats_vb2_start_streaming,
};

static int rkisp1_stats_init_vb2_queue(struct vb2_queue *q,
				       struct rkisp1_isp_stats_vdev *stats_vdev)
{
	memset(q, 0, sizeof(*q));
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 1)
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
#else
	q->type = V4L2_BUF_TYPE_META_CAPTURE;
#endif
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = stats_vdev;
	q->ops = &rkisp1_stats_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct rkisp1_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	return vb2_queue_init(q);
}

static void rkisp1_stats_get_awb_meas(struct rkisp1_isp_stats_vdev *stats_vdev,
				      struct cifisp_stat_buffer *pbuf)
{
	/* Protect against concurrent access from ISR? */
	u32 reg_val;

	pbuf->meas_type |= CIFISP_STAT_AWB;
	reg_val = readl(stats_vdev->dev->base_addr + CIF_ISP_AWB_WHITE_CNT);
	pbuf->params.awb.awb_mean[0].cnt = CIF_ISP_AWB_GET_PIXEL_CNT(reg_val);
	reg_val = readl(stats_vdev->dev->base_addr + CIF_ISP_AWB_MEAN);

	pbuf->params.awb.awb_mean[0].mean_cr_or_r =
		CIF_ISP_AWB_GET_MEAN_CR_R(reg_val);
	pbuf->params.awb.awb_mean[0].mean_cb_or_b =
		CIF_ISP_AWB_GET_MEAN_CB_B(reg_val);
	pbuf->params.awb.awb_mean[0].mean_y_or_g =
		CIF_ISP_AWB_GET_MEAN_Y_G(reg_val);
}

static void rkisp1_stats_get_aec_meas(struct rkisp1_isp_stats_vdev *stats_vdev,
				      struct cifisp_stat_buffer *pbuf)
{
	unsigned int i;
	void __iomem *addr = stats_vdev->dev->base_addr + CIF_ISP_EXP_MEAN_00;

	pbuf->meas_type |= CIFISP_STAT_AUTOEXP;
	for (i = 0; i < CIFISP_AE_MEAN_MAX; i++)
		pbuf->params.ae.exp_mean[i] = (u8) readl(addr + i * 4);
}

static void rkisp1_stats_get_afc_meas(struct rkisp1_isp_stats_vdev *stats_vdev,
				      struct cifisp_stat_buffer *pbuf)
{
	pbuf->meas_type = CIFISP_STAT_AFM_FIN;

	pbuf->params.af.window[0].sum = readl(stats_vdev->dev->base_addr + CIF_ISP_AFM_SUM_A);
	pbuf->params.af.window[0].lum = readl(stats_vdev->dev->base_addr + CIF_ISP_AFM_LUM_A);
	pbuf->params.af.window[1].sum = readl(stats_vdev->dev->base_addr + CIF_ISP_AFM_SUM_B);
	pbuf->params.af.window[1].lum = readl(stats_vdev->dev->base_addr + CIF_ISP_AFM_LUM_B);
	pbuf->params.af.window[2].sum = readl(stats_vdev->dev->base_addr + CIF_ISP_AFM_SUM_C);
	pbuf->params.af.window[2].lum = readl(stats_vdev->dev->base_addr + CIF_ISP_AFM_LUM_C);
}

static void rkisp1_stats_get_hst_meas(struct rkisp1_isp_stats_vdev *stats_vdev,
				      struct cifisp_stat_buffer *pbuf)
{
	int i;
	void __iomem *addr = stats_vdev->dev->base_addr + CIF_ISP_HIST_BIN_0;

	pbuf->meas_type |= CIFISP_STAT_HIST;
	for (i = 0; i < CIFISP_HIST_BIN_N_MAX; i++)
		pbuf->params.hist.hist_bins[i] = readl(addr + (i * 4));
}

static void rkisp1_stats_get_bls_meas(struct rkisp1_isp_stats_vdev *stats_vdev,
				      struct cifisp_stat_buffer *pbuf)
{
	struct rkisp1_device *dev = stats_vdev->dev;
	struct rkisp1_isp_subdev *sd = &dev->isp_sdev;
	const struct rkisp1_fmt *in_fmt = &sd->in_fmt;
	void __iomem *base = stats_vdev->dev->base_addr;

	/* TODO: get in_fmt */

	if (in_fmt->bayer_pat == RAW_BGGR) {
		pbuf->params.ae.bls_val.meas_b = readl(base + CIF_ISP_BLS_A_MEASURED);
		pbuf->params.ae.bls_val.meas_gb = readl(base + CIF_ISP_BLS_B_MEASURED);
		pbuf->params.ae.bls_val.meas_gr = readl(base + CIF_ISP_BLS_C_MEASURED);
		pbuf->params.ae.bls_val.meas_r = readl(base + CIF_ISP_BLS_D_MEASURED);
	} else if (in_fmt->bayer_pat == RAW_GBRG) {
		pbuf->params.ae.bls_val.meas_gb = readl(base + CIF_ISP_BLS_A_MEASURED);
		pbuf->params.ae.bls_val.meas_b = readl(base + CIF_ISP_BLS_B_MEASURED);
		pbuf->params.ae.bls_val.meas_r = readl(base + CIF_ISP_BLS_C_MEASURED);
		pbuf->params.ae.bls_val.meas_gr = readl(base + CIF_ISP_BLS_D_MEASURED);
	} else if (in_fmt->bayer_pat == RAW_GRBG) {
		pbuf->params.ae.bls_val.meas_gr = readl(base + CIF_ISP_BLS_A_MEASURED);
		pbuf->params.ae.bls_val.meas_r = readl(base + CIF_ISP_BLS_B_MEASURED);
		pbuf->params.ae.bls_val.meas_b = readl(base + CIF_ISP_BLS_C_MEASURED);
		pbuf->params.ae.bls_val.meas_gb = readl(base + CIF_ISP_BLS_D_MEASURED);
	} else if (in_fmt->bayer_pat == RAW_RGGB) {
		pbuf->params.ae.bls_val.meas_r = readl(base + CIF_ISP_BLS_A_MEASURED);
		pbuf->params.ae.bls_val.meas_gr = readl(base + CIF_ISP_BLS_B_MEASURED);
		pbuf->params.ae.bls_val.meas_gb = readl(base + CIF_ISP_BLS_C_MEASURED);
		pbuf->params.ae.bls_val.meas_b = readl(base + CIF_ISP_BLS_D_MEASURED);
	}
}

void rkisp1_stats_frame_in(struct rkisp1_isp_stats_vdev *stats_vdev,
			   const struct timeval *fi_t)
{
	spin_lock(&stats_vdev->irq_lock);
	/* Called in an interrupt context. */
	stats_vdev->fi_t = *fi_t;
	spin_unlock(&stats_vdev->irq_lock);
}

void rkisp1_stats_v_start(struct rkisp1_isp_stats_vdev *stats_vdev,
			  const struct timeval *vs_t)
{
	spin_lock(&stats_vdev->irq_lock);
	/* Called in an interrupt context. */
	stats_vdev->frame_id++;
	stats_vdev->vs_t = *vs_t;
	spin_unlock(&stats_vdev->irq_lock);
}

static void
rkisp1_stats_send_measurement(struct rkisp1_isp_stats_vdev *stats_vdev,
			      struct rkisp1_isp_readout_work *meas_work)
{
	unsigned long lock_flags = 0;
	struct vb2_buffer *vb = NULL;
	struct rkisp1_buffer *buf = NULL;
	unsigned int active_meas;
	struct cifisp_stat_buffer *vb2_plane_vaddr;

	spin_lock_irqsave(&stats_vdev->irq_lock, lock_flags);
	if (stats_vdev->frame_id != meas_work->frame_id) {
		spin_unlock_irqrestore(&stats_vdev->irq_lock, lock_flags);
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "Measurement late(%d, %d)\n",
			  stats_vdev->frame_id, meas_work->frame_id);
		goto end;
	}

	if (!list_empty(&stats_vdev->stat)) {
		buf = list_first_entry(&stats_vdev->stat,
				       struct rkisp1_buffer, queue);
		vb = &buf->vb.vb2_buf;

	} else {
		spin_unlock_irqrestore(&stats_vdev->irq_lock, lock_flags);
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "no queued measurement buf\n");
		goto end;
	}
	spin_unlock_irqrestore(&stats_vdev->irq_lock, lock_flags);

	active_meas = rkisp1_get_active_meas(&stats_vdev->dev->params_vdev,
					     meas_work->frame_id);

	vb->state = VB2_BUF_STATE_ACTIVE;
	vb2_plane_vaddr = (struct cifisp_stat_buffer *)vb->vb2_queue->mem_ops->vaddr(
		vb->planes[0].mem_priv);
	memset(vb2_plane_vaddr, 0, sizeof(struct cifisp_stat_buffer));

	if (active_meas & CIF_ISP_AWB_DONE)
		rkisp1_stats_get_awb_meas(stats_vdev, vb2_plane_vaddr);

	if (active_meas & CIF_ISP_AFM_FIN)
		rkisp1_stats_get_afc_meas(stats_vdev, vb2_plane_vaddr);

	if (active_meas & CIF_ISP_EXP_END) {
		rkisp1_stats_get_aec_meas(stats_vdev, vb2_plane_vaddr);
		rkisp1_stats_get_bls_meas(stats_vdev, vb2_plane_vaddr);
	}

	if (active_meas & CIF_ISP_HIST_MEASURE_RDY)
		rkisp1_stats_get_hst_meas(stats_vdev, vb2_plane_vaddr);

	spin_lock_irqsave(&stats_vdev->irq_lock, lock_flags);

	if (stats_vdev->frame_id != meas_work->frame_id) {
		spin_unlock_irqrestore(&stats_vdev->irq_lock, lock_flags);
		v4l2_warn(stats_vdev->vnode.vdev.v4l2_dev,
			  "Measurement late(%d, %d)\n",
			  stats_vdev->frame_id, meas_work->frame_id);
		goto end;
	}
	list_del(&buf->queue);

	spin_unlock_irqrestore(&stats_vdev->irq_lock, lock_flags);

	if (active_meas & CIF_ISP_AWB_DONE) {
		stats_vdev->meas_stats.params.awb = vb2_plane_vaddr->params.awb;
		stats_vdev->meas_stats.meas_type |= CIFISP_STAT_AWB;
	}
	if (active_meas & CIF_ISP_AFM_FIN) {
		stats_vdev->meas_stats.params.af = vb2_plane_vaddr->params.af;
		stats_vdev->meas_stats.meas_type |= CIFISP_STAT_AFM_FIN;
	}
	if (active_meas & CIF_ISP_EXP_END) {
		stats_vdev->meas_stats.params.ae = vb2_plane_vaddr->params.ae;
		stats_vdev->meas_stats.meas_type |= CIFISP_STAT_AUTOEXP;
	}
	if (active_meas & CIF_ISP_HIST_MEASURE_RDY) {
		stats_vdev->meas_stats.params.hist = vb2_plane_vaddr->params.hist;
		stats_vdev->meas_stats.meas_type |= CIFISP_STAT_HIST;
	}
	stats_vdev->meas_stats.frame_id = meas_work->frame_id;

	vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	wake_up(&vb->vb2_queue->done_wq);
	return;
end:

	if (vb && (vb->state == VB2_BUF_STATE_ACTIVE))
		vb->state = VB2_BUF_STATE_QUEUED;
}

static void rkisp1_stats_readout_work(struct work_struct *work)
{
	unsigned long lock_flags = 0;
	struct rkisp1_isp_readout_work *readout_work = container_of(work,
						struct rkisp1_isp_readout_work,
						work);
	struct rkisp1_isp_stats_vdev *stats_vdev = readout_work->stats_vdev;

	spin_lock_irqsave(&stats_vdev->irq_lock, lock_flags);
	if (!stats_vdev->streamon) {
		spin_unlock(&stats_vdev->irq_lock);
		goto end;
	}
	spin_unlock_irqrestore(&stats_vdev->irq_lock, lock_flags);

	if (readout_work->readout == RKISP1_ISP_READOUT_MEAS)
		rkisp1_stats_send_measurement(stats_vdev, readout_work);
end:
	kfree(work);
}

int rkisp1_stats_isr(struct rkisp1_isp_stats_vdev *stats_vdev, u32 isp_mis)
{
	unsigned int isp_mis_tmp = 0;
	struct rkisp1_isp_readout_work *work;
	unsigned int active_meas =
			rkisp1_get_active_meas(&stats_vdev->dev->params_vdev,
					       stats_vdev->frame_id);
#ifdef LOG_ISR_EXE_TIME
	ktime_t in_t = ktime_get();
#endif

	spin_lock(&stats_vdev->irq_lock);
	if (!stats_vdev->streamon) {
		spin_unlock(&stats_vdev->irq_lock);
		return 0;
	}
	spin_unlock(&stats_vdev->irq_lock);

	if (isp_mis & (CIF_ISP_DATA_LOSS | CIF_ISP_PIC_SIZE_ERROR))
		return 0;

	if (isp_mis & CIF_ISP_FRAME) {
		u32 isp_ris = readl(stats_vdev->dev->base_addr + CIF_ISP_RIS);

		writel((CIF_ISP_AWB_DONE | CIF_ISP_AFM_FIN | CIF_ISP_EXP_END |
			CIF_ISP_HIST_MEASURE_RDY),
			stats_vdev->dev->base_addr + CIF_ISP_ICR);

		isp_mis_tmp = readl(stats_vdev->dev->base_addr + CIF_ISP_MIS);
		if (isp_mis_tmp &
		    (CIF_ISP_AWB_DONE | CIF_ISP_AFM_FIN |
		     CIF_ISP_EXP_END | CIF_ISP_HIST_MEASURE_RDY))
			v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
				 "isp icr 3A info err: 0x%x\n",
				 isp_mis_tmp);

		if ((active_meas & isp_ris) == active_meas) {
			work = (struct rkisp1_isp_readout_work *)
			    kzalloc(sizeof(struct rkisp1_isp_readout_work),
				    GFP_ATOMIC);
			if (work) {
				INIT_WORK(&work->work,
					  rkisp1_stats_readout_work);
				work->readout = RKISP1_ISP_READOUT_MEAS;
				work->stats_vdev = stats_vdev;
				work->frame_id = stats_vdev->frame_id;
				if (!queue_work(stats_vdev->readout_wq,
						&work->work))
					kfree(work);
			} else {
				v4l2_err(stats_vdev->vnode.vdev.v4l2_dev,
					 "Could not allocate work\n");
			}
		}
	}
#ifdef LOG_ISR_EXE_TIME
	if (isp_mis & (CIF_ISP_EXP_END | CIF_ISP_AWB_DONE |
		       CIF_ISP_FRAME | CIF_ISP_HIST_MEASURE_RDY)) {
		unsigned int diff_us =
		    ktime_to_us(ktime_sub(ktime_get(), in_t));

		if (diff_us > g_longest_isr_time)
			g_longest_isr_time = diff_us;

		v4l2_info(stats_vdev->vnode.vdev.v4l2_dev,
			 "isp_isr time %d %d\n", diff_us, g_longest_isr_time);
	}
#endif

	return 0;
}

int rkisp1_register_stats_vdev(struct rkisp1_isp_stats_vdev *stats_vdev,
			       struct v4l2_device *v4l2_dev,
			       struct rkisp1_device *dev)
{
	int ret;
	struct rkisp1_vdev_node *node = &stats_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	stats_vdev->dev = dev;
	mutex_init(&node->vlock);
	INIT_LIST_HEAD(&stats_vdev->stat);
	spin_lock_init(&stats_vdev->irq_lock);

	strlcpy(vdev->name, "rkisp1-statistics", sizeof(vdev->name));
	vdev->vfl_type = V4L2_CAP_VIDEO_CAPTURE;
	video_set_drvdata(vdev, stats_vdev);
	vdev->ioctl_ops = &rkisp1_stats_ioctl;
	vdev->fops = &rkisp1_stats_fops;
	vdev->release = video_device_release_empty;
	vdev->lock = &node->vlock;
	vdev->v4l2_dev = v4l2_dev;
	vdev->queue = &node->buf_queue;
	rkisp1_stats_init_vb2_queue(vdev->queue, stats_vdev);
	video_set_drvdata(vdev, stats_vdev);

	node->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_init(&vdev->entity, 1, &node->pad, 0);
	if (ret < 0)
		goto err_release_queue;

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(&vdev->dev,
			"could not register Video for Linux device\n");
		goto err_cleanup_media_entity;
	}

	stats_vdev->readout_wq =
	    alloc_workqueue("measurement_queue",
			    WQ_UNBOUND | WQ_MEM_RECLAIM, 1);

	if (!stats_vdev->readout_wq) {
		ret = -ENOMEM;
			goto err_unreg_vdev;
	}

	return 0;
err_unreg_vdev:
	video_unregister_device(vdev);
err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);
	return ret;
}

void rkisp1_unregister_stats_vdev(struct rkisp1_isp_stats_vdev *stats_vdev)
{
	struct rkisp1_vdev_node *node = &stats_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	destroy_workqueue(stats_vdev->readout_wq);
	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
}
