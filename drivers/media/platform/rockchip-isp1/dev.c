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

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include "mipi_dphy_sy.h"
#include "regs.h"
#include "rkisp1.h"

struct isp_match_data {
	const char * const *clks;
	int size;
};

int rkisp1_debug;
module_param_named(debug, rkisp1_debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

static int __isp_pipeline_prepare(struct rkisp1_pipeline *p,
				  struct media_entity *me)
{
	struct v4l2_subdev *sd;
	int i;

	memset(p->subdevs, 0, sizeof(p->subdevs));

	while (1) {
		struct media_pad *pad = NULL;

		/* Find remote source pad */
		for (i = 0; i < me->num_pads; i++) {
			struct media_pad *spad = &me->pads[i];

			if (!(spad->flags & MEDIA_PAD_FL_SINK))
				continue;
			pad = media_entity_remote_pad(spad);
			if (pad)
				break;
		}

		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;
		sd = media_entity_to_v4l2_subdev(pad->entity);
		switch (sd->grp_id) {
		case GRP_ID_SENSOR:
			p->subdevs[IDX_SENSOR] = sd;
			break;
		case GRP_ID_MIPIPHY:
			p->subdevs[IDX_MIPIPHY] = sd;
			break;
		case GRP_ID_ISP:
			p->subdevs[IDX_ISP] = sd;
			break;
		default:
			break;
		}
		me = &sd->entity;
		if (me->num_pads == 1)
			break;
	}
	return 0;
}

static int __subdev_set_power(struct v4l2_subdev *sd, int on)
{
	int *use_count;
	int ret;

	v4l2_info(sd, "%d: name %s,on %d\n", __LINE__, sd->name, on);

	if (sd == NULL)
		return -ENXIO;

	use_count = &sd->entity.use_count;
	if (on && (*use_count)++ > 0)
		return 0;
	else if (!on && (*use_count == 0 || --(*use_count) > 0))
		return 0;
	ret = v4l2_subdev_call(sd, core, s_power, on);

	return ret != -ENOIOCTLCMD ? ret : 0;
}

static int __isp_pipeline_s_power(struct rkisp1_pipeline *p, bool on)
{
	static const u8 seq[2][IDX_MAX] = {
		{ IDX_ISP, IDX_SENSOR, IDX_MIPIPHY },
		{ IDX_ISP, IDX_SENSOR, IDX_MIPIPHY },
	};
	int i, ret = 0;

	if (p->subdevs[IDX_SENSOR] == NULL)
		return -ENXIO;

	for (i = 0; i < IDX_MAX; i++) {
		unsigned int idx = seq[on][i];

		ret = __subdev_set_power(p->subdevs[idx], on);
		if (ret < 0 && ret != -ENXIO)
			goto error;
	}

	return 0;
error:
	for (; i >= 0; i--) {
		unsigned int idx = seq[on][i];

		__subdev_set_power(p->subdevs[idx], !on);
	}
	return ret;
}

static int rkisp1_pipeline_open(struct rkisp1_pipeline *p, struct media_entity *me,
		      bool prepare)
{
	int ret;
	struct v4l2_subdev *sd;

	if (WARN_ON(p == NULL || me == NULL))
		return -EINVAL;

	if (prepare)
		__isp_pipeline_prepare(p, me);

	sd = p->subdevs[IDX_SENSOR];
	if (sd == NULL)
		return -EINVAL;

	ret = __isp_pipeline_s_power(p, 1);
	if (ret < 0)
		return ret;

	return 0;
}

static int rkisp1_pipeline_close(struct rkisp1_pipeline *p)
{
	int ret;

	ret = __isp_pipeline_s_power(p, 0);

	return ret == -ENXIO ? 0 : ret;
}

static int rkisp1_pipeline_set_stream(struct rkisp1_pipeline *p, bool on)
{
	static const u8 seq[2][IDX_MAX] = {
		{ IDX_MIPIPHY, IDX_SENSOR, IDX_ISP },
		{ IDX_ISP, IDX_MIPIPHY, IDX_SENSOR },
	};
	int i, ret = 0;

	if (p->subdevs[IDX_SENSOR] == NULL)
		return -ENODEV;

	for (i = 0; i < IDX_MAX; i++) {
		unsigned int idx = seq[on][i];

		ret = v4l2_subdev_call(p->subdevs[idx], video, s_stream, on);

		if (ret < 0 && ret != -ENOIOCTLCMD && ret != -ENODEV)
			goto error;
	}

	return 0;
error:
	for (; i >= 0; i--) {
		unsigned int idx = seq[on][i];

		v4l2_subdev_call(p->subdevs[idx], video, s_stream, !on);
	}
	return ret;
}

static int rkisp1_create_links(struct rkisp1_device *dev)
{
	int ret, i;
	unsigned int flags;
	struct media_entity *source, *sink;

	/* note: entities source pad link should be
	 * established prior to sink pad link.
	 * isp sink links should be established first.
	 */
	for (i = 0; i < dev->num_sensors; i++) {
		flags = i ? 0 : MEDIA_LNK_FL_ENABLED;
		source = &dev->sensors[i].sd->entity;
		/*TODO: find out the sensor source pad instead of hardcode 0*/
		sink = &dev->subdevs[RKISP1_SD_PHY_CSI]->entity;
		ret = media_entity_create_link(source, 0, sink,
					       MIPI_DPHY_SY_PAD_SINK, flags);
		if (ret < 0)
			return ret;
	}
	/* Make the first sensor enable as default */
	dev->subdevs[RKISP1_SD_SENSOR] = dev->sensors[0].sd;

	flags = MEDIA_LNK_FL_ENABLED/* | MEDIA_LNK_FL_IMMUTABLE*/;
	source = &dev->subdevs[RKISP1_SD_PHY_CSI]->entity;
	sink = &dev->isp_sdev.sd.entity;
	ret = media_entity_create_link(source, MIPI_DPHY_SY_PAD_SOURCE,
				       sink, RKISP1_ISP_PAD_SINK, flags);
	if (ret < 0)
		return ret;

	ret = media_entity_call(sink, link_setup,
				&sink->pads[RKISP1_ISP_PAD_SINK],
				&source->pads[MIPI_DPHY_SY_PAD_SOURCE], flags);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	/* params links */
	source = &dev->params_vdev.vnode.vdev.entity;
	sink = &dev->isp_sdev.sd.entity;
	ret = media_entity_create_link(source, 0, sink,
				       RKISP1_ISP_PAD_SINK_PARAMS, flags);
	if (ret < 0)
		return ret;

	/* create isp internal links */
	/* SP links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stream[RKISP1_STREAM_SP].vnode.vdev.entity;
	ret = media_entity_create_link(source, RKISP1_ISP_PAD_SOURCE_PATH,
					sink, 0, flags);
	if (ret < 0)
		return ret;

	/* MP links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stream[RKISP1_STREAM_MP].vnode.vdev.entity;
	ret = media_entity_create_link(source, RKISP1_ISP_PAD_SOURCE_PATH,
				       sink, 0, flags);
	if (ret < 0)
		return ret;

	/* 3A stats links */
	source = &dev->isp_sdev.sd.entity;
	sink = &dev->stats_vdev.vnode.vdev.entity;
	return media_entity_create_link(source, RKISP1_ISP_PAD_SOURCE_STATS,
					sink, 0, flags);
}

static int subdev_notifier_complete(struct v4l2_async_notifier *notifier)
{
	struct rkisp1_device *dev;
	int ret;

	dev = container_of(notifier, struct rkisp1_device, notifier);

	mutex_lock(&dev->media_dev.graph_mutex);
	ret = rkisp1_create_links(dev);
	if (ret < 0)
		goto unlock;
	ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
	if (ret < 0)
		goto unlock;

	v4l2_info(&dev->v4l2_dev, "Async subdev notifier completed\n");

unlock:
	mutex_unlock(&dev->media_dev.graph_mutex);
	return ret;
}

static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *subdev,
				 struct v4l2_async_subdev *asd)
{
	struct rkisp1_sensor_info *sensor_sd;

	sensor_sd = container_of(asd, struct rkisp1_sensor_info, asd);
	sensor_sd->sd = subdev;
	subdev->grp_id = GRP_ID_SENSOR;

	v4l2_dbg(1, rkisp1_debug, subdev, "Async registered subdev\n");

	return 0;
}

static int isp_subdev_notifier(struct rkisp1_device *isp_dev,
			       struct device_node *parent)
{
	struct v4l2_async_notifier *ntf = &isp_dev->notifier;
	struct v4l2_async_subdev *asd;
	struct device *dev = isp_dev->dev;
	struct device_node *node, *pre_node = NULL;
	struct device_node *remote_parent = NULL, *remote = NULL;
	int i, lanes;

	ntf->subdevs = devm_kzalloc(dev,
				    RKISP1_MAX_SENSOR * sizeof(*ntf->subdevs),
				    GFP_KERNEL);
	if (!ntf->subdevs)
		return -ENOMEM;

	i = 0;
	while ((node = of_graph_get_next_endpoint(parent, pre_node)) != NULL) {
		struct rkisp1_sensor_info *sensor = &isp_dev->sensors[i];

		if (pre_node)
			of_node_put(pre_node);
		pre_node = node;
		remote = of_parse_phandle(node, "remote-endpoint", 0);
		if (v4l2_fwnode_endpoint_parse(of_fwnode_handle(remote),
			&sensor->ep)) {
			dev_err(dev, "Can't parse endpoint\n");
			goto put_node;
		}

		if (sensor->ep.bus_type == V4L2_MBUS_CSI2) {
			lanes = sensor->ep.bus.mipi_csi2.num_data_lanes;
			if (lanes < 1 || lanes > 4) {
				dev_err(dev, "DPHY lanes(%d) out of range[1..4]\n",
					lanes);
				goto put_node;
			}
		}

		remote_parent = of_graph_get_remote_port_parent(node);
		if (!remote_parent) {
			dev_err(dev, "get remote port parent failed\n");
			goto put_node;
		}

		asd = &sensor->asd;
		asd->match_type = V4L2_ASYNC_MATCH_FWNODE;
		asd->match.fwnode.fwnode = of_fwnode_handle(remote_parent);
		if (!asd->match.fwnode.fwnode) {
			dev_err(dev, "Can't get remote endpoint parent\n");
			goto put_node;
		}
		of_node_put(remote);
		ntf->subdevs[i] = asd;
		i++;
	}
	if (i == 0) {
		dev_err(dev, "Can't find the sensor endpoint\n");
		goto put_node;
	}

	isp_dev->num_sensors = i;
	ntf->num_subdevs = i;
	ntf->bound = subdev_notifier_bound;
	ntf->complete = subdev_notifier_complete;

	return v4l2_async_notifier_register(&isp_dev->v4l2_dev, ntf);

put_node:
	if (pre_node)
		of_node_put(pre_node);
	return -EINVAL;
}

static int register_mipidphy_subdev(struct rkisp1_device *isp_dev)
{
	struct media_entity *me;
	struct v4l2_subdev *sd;
	struct device *dev = isp_dev->dev;
	struct platform_device *mipi_pdev;
	struct device_node *of_mipi;
	int ret;

	ret = of_platform_populate(dev->of_node, NULL, NULL, dev);
	if (ret < 0) {
		dev_err(dev, "Failed to populate child mipidphy(%d)\n", ret);
		return ret;
	}

	of_mipi = of_get_next_available_child(dev->of_node, NULL);
	if (!of_mipi) {
		dev_err(dev, "Failed to get mipidphy node\n");
		return -EINVAL;
	}
	mipi_pdev = of_find_device_by_node(of_mipi);
	if (!mipi_pdev) {
		dev_err(dev, "Failed to get mipidphy device\n");
		ret = -EINVAL;
		goto err_put_node;
	}

	me = platform_get_drvdata(mipi_pdev);
	if (!me) {
		dev_err(dev, "Deferred for mipidphy device is not probed\n");
		ret = -EPROBE_DEFER;
		goto err_put_node;
	}

	sd = media_entity_to_v4l2_subdev(me);
	ret = v4l2_device_register_subdev(&isp_dev->v4l2_dev, sd);
	if (ret < 0) {
		v4l2_err(&isp_dev->v4l2_dev, "Failed to register dphy sd\n");
		goto err_put_node;
	}

	isp_dev->subdevs[RKISP1_SD_PHY_CSI] = sd;
	sd->grp_id = GRP_ID_MIPIPHY;

	ret = isp_subdev_notifier(isp_dev, of_mipi);
	if (ret < 0) {
		v4l2_err(&isp_dev->v4l2_dev,
			 "Failed to register subdev notifier(%d)\n", ret);
		goto err_unreg_v4l2_subdev;
	}
	of_node_put(of_mipi);

	return 0;
err_unreg_v4l2_subdev:
	v4l2_device_unregister_subdev(sd);
err_put_node:
	of_node_put(of_mipi);
	isp_dev->subdevs[RKISP1_SD_PHY_CSI] = NULL;
	return ret;
}

/*TODO: maybe renamed*/
static int rkisp1_register_platform_subdevs(struct rkisp1_device *dev)
{
	int ret;

	ret = rkisp1_register_isp_subdev(dev, &dev->v4l2_dev);
	if (ret < 0)
		return ret;

	ret = rkisp1_register_stream_vdevs(dev);
	if (ret < 0)
		goto err_unreg_isp_subdev;

	ret = rkisp1_register_stats_vdev(&dev->stats_vdev, &dev->v4l2_dev, dev);
	if (ret < 0)
		goto err_unreg_stream_vdev;

	ret = rkisp1_register_params_vdev(&dev->params_vdev, &dev->v4l2_dev,
					  dev);
	if (ret < 0)
		goto err_unreg_stats_vdev;

	ret = register_mipidphy_subdev(dev);
	if (ret < 0)
		goto err_unreg_params_vdev;

	return 0;
err_unreg_params_vdev:
	rkisp1_unregister_params_vdev(&dev->params_vdev);
err_unreg_stats_vdev:
	rkisp1_unregister_stats_vdev(&dev->stats_vdev);
err_unreg_stream_vdev:
	rkisp1_unregister_stream_vdevs(dev);
err_unreg_isp_subdev:
	rkisp1_unregister_isp_subdev(dev);
	return ret;
}

static const char * const rk3399_isp_clks[] = {
	"clk-isp",
	"aclk-isp-noc",
	"aclk-isp-wrapper",
	"hclk-isp-noc",
	"hclk-isp-wrapper",
};

static const char * const rk3288_isp_clks[] = {
	"clk-isp",
	"hclk_isp",
	"sclk_isp",
	"sclk_isp_jpe",
	"mipi_csi",
	"pclk_isp_in",
	"sclk_mipidsi_24m",
};

static const struct isp_match_data rk3288_isp_clk_data = {
	.clks = rk3288_isp_clks,
	.size = ARRAY_SIZE(rk3288_isp_clks),
};

static const struct isp_match_data rk3399_isp_clk_data = {
	.clks = rk3399_isp_clks,
	.size = ARRAY_SIZE(rk3399_isp_clks),
};

static const struct of_device_id rkisp1_plat_of_match[] = {
	{
		.compatible = "rockchip,rk3288-cif-isp",
		.data = &rk3288_isp_clk_data,
	}, {
		.compatible = "rockchip,rk3399-cif-isp",
		.data = &rk3399_isp_clk_data,
	},
	{},
};

static irqreturn_t rkisp1_irq_handler(int irq, void *cntxt)
{
	struct device *dev = cntxt;
	struct rkisp1_device *rkisp1_dev = dev_get_drvdata(dev);
	struct rkisp1_stream *sp_stream = &rkisp1_dev->stream[RKISP1_STREAM_SP];
	struct rkisp1_stream *mp_stream = &rkisp1_dev->stream[RKISP1_STREAM_MP];
	void __iomem *base = rkisp1_dev->base_addr;
	unsigned int mis_val;

	mis_val = readl(rkisp1_dev->base_addr + CIF_ISP_MIS);
	if (mis_val)
		rkisp1_isp_isr(mis_val, rkisp1_dev);

	mis_val = readl(rkisp1_dev->base_addr + CIF_MIPI_MIS);
	if (mis_val)
		rkisp1_mipi_isr(mis_val, rkisp1_dev);

	mis_val = sp_stream->ops->is_frame_end_int_masked(base);
	if (mis_val)
		rkisp1_mi_isr(&rkisp1_dev->stream[RKISP1_STREAM_SP]);

	mis_val = mp_stream->ops->is_frame_end_int_masked(base);
	if (mis_val)
		rkisp1_mi_isr(&rkisp1_dev->stream[RKISP1_STREAM_MP]);

	/* TODO: update crop & resize */
	clr_all_int(base);
	return IRQ_NONE;
}

static void rkisp1_disable_sys_clk(struct rkisp1_device *rkisp1_dev)
{
	struct clk *clk;
	int i;

	for (i = rkisp1_dev->clk_size - 1; i >= 0; i--) {
		clk = rkisp1_dev->clks[i];
		clk_disable_unprepare(clk);
	}
}

static int rkisp1_enable_sys_clk(struct rkisp1_device *rkisp1_dev)
{
	struct clk *clk;
	int i, ret = -EINVAL;

	for (i = 0; i < rkisp1_dev->clk_size; i++) {
		clk = rkisp1_dev->clks[i];
		ret = clk_prepare_enable(clk);
		if (ret < 0)
			goto err;
	}
	return 0;
err:
	for (--i; i >= 0; --i)
		clk_disable_unprepare(rkisp1_dev->clks[i]);
	return ret;
}

static int rkisp1_plat_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev;
	struct rkisp1_device *isp_dev;
	const struct isp_match_data *clk_data;
	struct rkisp1_stream *sp_stream, *mp_stream;
	struct resource *res;
	int i, ret, irq;

	match = of_match_node(rkisp1_plat_of_match, node);
	isp_dev = devm_kzalloc(dev, sizeof(*isp_dev), GFP_KERNEL);
	if (!isp_dev)
		return -ENOMEM;
	dev_set_drvdata(dev, isp_dev);
	isp_dev->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	isp_dev->base_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(isp_dev->base_addr))
		return PTR_ERR(isp_dev->base_addr);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	ret = devm_request_irq(dev, irq, rkisp1_irq_handler, IRQF_SHARED,
				dev_driver_string(dev), dev);
	if (ret < 0) {
		dev_err(dev, "request irq failed: %d\n", ret);
		return ret;
	}
	isp_dev->irq = irq;
	isp_dev->clk_size = 0;
	clk_data = (const struct isp_match_data *)match->data;
	for (i = 0; i < clk_data->size; i++) {
		struct clk *clk = devm_clk_get(dev, clk_data->clks[i]);

		if (IS_ERR(clk)) {
			dev_err(dev, "failed to get %s\n", clk_data->clks[i]);
			return PTR_ERR(clk);
		}
		isp_dev->clks[i] = clk;
		isp_dev->clk_size++;
	}

	isp_dev->pipe.open = rkisp1_pipeline_open;
	isp_dev->pipe.close = rkisp1_pipeline_close;
	isp_dev->pipe.set_stream = rkisp1_pipeline_set_stream;

	mp_stream = &isp_dev->stream[RKISP1_STREAM_MP];
	sp_stream = &isp_dev->stream[RKISP1_STREAM_SP];
	mp_stream->id = RKISP1_STREAM_SP;
	sp_stream->id = RKISP1_STREAM_MP;
	mp_stream->ispdev = isp_dev;
	sp_stream->ispdev = isp_dev;
	rkisp1_stream_init(sp_stream, RKISP1_STREAM_SP);
	rkisp1_stream_init(mp_stream, RKISP1_STREAM_MP);

	strlcpy(isp_dev->media_dev.model, "rkisp1",
		sizeof(isp_dev->media_dev.model));
	isp_dev->media_dev.dev = &pdev->dev;
	v4l2_dev = &isp_dev->v4l2_dev;
	v4l2_dev->mdev = &isp_dev->media_dev;
	strlcpy(v4l2_dev->name, "rkisp1", sizeof(v4l2_dev->name));

	ret = v4l2_device_register(isp_dev->dev, &isp_dev->v4l2_dev);
	if (ret < 0)
		return ret;
	ret = media_device_register(&isp_dev->media_dev);
	if (ret < 0) {
		v4l2_err(v4l2_dev, "Failed to register media device: %d\n",
			 ret);
		goto err_unreg_v4l2_dev;
	}

	/*create & register platefom subdev (from of_node) */
	ret = rkisp1_register_platform_subdevs(isp_dev);
	if (ret < 0)
		goto err_unreg_media_dev;

	pm_runtime_enable(&pdev->dev);

	return 0;

err_unreg_media_dev:
	media_device_unregister(&isp_dev->media_dev);
err_unreg_v4l2_dev:
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	return ret;
}

static int rkisp1_plat_remove(struct platform_device *pdev)
{
	struct rkisp1_device *isp_dev = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	media_device_unregister(&isp_dev->media_dev);
	v4l2_device_unregister(&isp_dev->v4l2_dev);
	rkisp1_unregister_params_vdev(&isp_dev->params_vdev);
	rkisp1_unregister_stats_vdev(&isp_dev->stats_vdev);
	rkisp1_unregister_stream_vdevs(isp_dev);
	rkisp1_unregister_isp_subdev(isp_dev);
	rkisp1_stream_release(&isp_dev->stream[RKISP1_STREAM_SP]);
	rkisp1_stream_release(&isp_dev->stream[RKISP1_STREAM_MP]);

	return 0;
}

static int rkisp1_runtime_suspend(struct device *dev)
{
	struct rkisp1_device *isp_dev = dev_get_drvdata(dev);

	rkisp1_disable_sys_clk(isp_dev);
	return pinctrl_pm_select_sleep_state(dev);
}

static int rkisp1_runtime_resume(struct device *dev)
{
	struct rkisp1_device *isp_dev = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(dev);
	if (ret < 0)
		return ret;
	rkisp1_enable_sys_clk(isp_dev);

	return 0;
}

static const struct dev_pm_ops rkisp1_plat_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(rkisp1_runtime_suspend, rkisp1_runtime_resume, NULL)
};

static struct platform_driver rkisp1_plat_drv = {
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = of_match_ptr(rkisp1_plat_of_match),
		   .pm = &rkisp1_plat_pm_ops,
	},
	.probe = rkisp1_plat_probe,
	.remove = rkisp1_plat_remove,
};

module_platform_driver(rkisp1_plat_drv);
MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip ISP1 platform driver");
MODULE_LICENSE("Dual BSD/GPL");
