// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm APCS clock controller driver
 *
 * Copyright (c) 2017, Linaro Limited
 * 2019, Ramon Rebersak <ramonr@aplit-soft.com>
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "clk-regmap.h"
#include "clk-regmap-mux-div.h"

static const u32 gpll0_a7cc_map[] = { 4, 5 };

static const char * const gpll0_a7cc[] = {
	"gpll0_vote",
	"a7pll",
};

/*
 * We use the notifier function for switching to a temporary safe configuration
 * (mux and divider), while the A7 PLL is reconfigured.
 */
static int a7cc_notifier_cb(struct notifier_block *nb, unsigned long event,
			     void *data)
{
	int ret = 0;
	struct clk_regmap_mux_div *md = container_of(nb,
						     struct clk_regmap_mux_div,
						     clk_nb);
	if (event == PRE_RATE_CHANGE)
		/* set the mux and divider to safe frequency (400mhz) */
		ret = mux_div_set_src_div(md, 4, 3);

	return notifier_from_errno(ret);
}

static int qcom_apcs_msm8909_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *parent = dev->parent;
	struct clk_regmap_mux_div *a7cc;
	struct regmap *regmap;
	struct clk_init_data init = { };
	int ret = -ENODEV;

	regmap = dev_get_regmap(parent, NULL);
	if (!regmap) {
		dev_err(dev, "failed to get regmap: %d\n", ret);
		return ret;
	}

	a7cc = devm_kzalloc(dev, sizeof(*a7cc), GFP_KERNEL);
	if (!a7cc)
		return -ENOMEM;

	init.name = "a7mux";
	init.parent_names = gpll0_a7cc;
	init.num_parents = ARRAY_SIZE(gpll0_a7cc);
	init.ops = &clk_regmap_mux_div_ops;
	init.flags = CLK_SET_RATE_PARENT;

	a7cc->clkr.hw.init = &init;
	a7cc->clkr.regmap = regmap;
	a7cc->reg_offset = 0x50;
	a7cc->hid_width = 5;
	a7cc->hid_shift = 0;
	a7cc->src_width = 3;
	a7cc->src_shift = 8;
	a7cc->parent_map = gpll0_a7cc_map;

	a7cc->pclk = devm_clk_get(parent, NULL);
	if (IS_ERR(a7cc->pclk)) {
		ret = PTR_ERR(a7cc->pclk);
		dev_err(dev, "failed to get clk: %d\n", ret);
		return ret;
	}

	a7cc->clk_nb.notifier_call = a7cc_notifier_cb;
	ret = clk_notifier_register(a7cc->pclk, &a7cc->clk_nb);
	if (ret) {
		dev_err(dev, "failed to register clock notifier: %d\n", ret);
		return ret;
	}

	ret = devm_clk_register_regmap(dev, &a7cc->clkr);
	if (ret) {
		dev_err(dev, "failed to register regmap clock: %d\n", ret);
		goto err;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  &a7cc->clkr.hw);
	if (ret) {
		dev_err(dev, "failed to add clock provider: %d\n", ret);
		goto err;
	}

	platform_set_drvdata(pdev, a7cc);

	return 0;

err:
	clk_notifier_unregister(a7cc->pclk, &a7cc->clk_nb);
	return ret;
}

static int qcom_apcs_msm8909_clk_remove(struct platform_device *pdev)
{
	struct clk_regmap_mux_div *a7cc = platform_get_drvdata(pdev);

	clk_notifier_unregister(a7cc->pclk, &a7cc->clk_nb);

	return 0;
}

static struct platform_driver qcom_apcs_msm8909_clk_driver = {
	.probe = qcom_apcs_msm8909_clk_probe,
	.remove = qcom_apcs_msm8909_clk_remove,
	.driver = {
		.name = "qcom-apcs-msm8909-clk",
	},
};
module_platform_driver(qcom_apcs_msm8909_clk_driver);

MODULE_AUTHOR("Georgi Djakov <georgi.djakov@linaro.org>, Ramon Rebersak <ramonr@aplit-soft.com");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm MSM8909 APCS clock driver");
