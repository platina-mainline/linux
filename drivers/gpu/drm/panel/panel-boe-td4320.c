// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 Barnabas Czeman <barnabas.czeman@mainlining.org>
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved.

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

static bool trace;
module_param(trace, bool, 0600);
MODULE_PARM_DESC(trace, "Log TD4320 power, reset and initialization stages");

static bool vendor_before_wake;
module_param(vendor_before_wake, bool, 0600);
MODULE_PARM_DESC(vendor_before_wake,
	"Test vendor configuration before Sleep Out, then 75ms and Display On");

static bool hold_before_prepare;
module_param(hold_before_prepare, bool, 0600);
MODULE_PARM_DESC(hold_before_prepare,
	"Pause before panel supplies, reset and commands; write N to resume");

static unsigned int prepare_pause_stage;
module_param(prepare_pause_stage, uint, 0600);
MODULE_PARM_DESC(prepare_pause_stage,
	"Pause preparation: 0=none, 3=controls, 4=display on, 5=sleep out");

static bool hold_after_init;
module_param(hold_after_init, bool, 0600);
MODULE_PARM_DESC(hold_after_init,
	"Pause after panel initialization, before DPU scanout; write N to resume");

struct boe_td4320 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
	struct delayed_work dimming_work;
	bool preserve_bootloader;
};

enum boe_td4320_supply {
	BOE_TD4320_IOVCC,
	BOE_TD4320_VSP,
	BOE_TD4320_VSN,
};

static const struct regulator_bulk_data boe_td4320_supplies[] = {
	{ .supply = "iovcc" },
	{ .supply = "vsp" },
	{ .supply = "vsn" },
};

/* From the downstream TD4320 panel supply entries, in supply order. */
static const int boe_td4320_enable_loads[] = { 32000, 100000, 100000 };
static const int boe_td4320_disable_loads[] = { 80, 100, 100 };

static ssize_t power_mode_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(dev);
	struct boe_td4320 *ctx = mipi_dsi_get_drvdata(dsi);
	u8 mode;
	int ret;

	/* Even diagnostic reads must not send DCS in this experiment. */
	if (ctx->preserve_bootloader)
		return -EOPNOTSUPP;

	if (!READ_ONCE(ctx->panel.prepared))
		return -EHOSTDOWN;

	ret = mipi_dsi_dcs_get_power_mode(dsi, &mode);
	if (ret)
		return sysfs_emit(buf, "err=%d\n", ret);

	return sysfs_emit(buf, "0x%02x\n", mode);
}
static DEVICE_ATTR_RO(power_mode);

static inline struct boe_td4320 *to_boe_td4320(struct drm_panel *panel)
{
	return container_of(panel, struct boe_td4320, panel);
}

static int boe_td4320_reset(struct boe_td4320 *ctx)
{
	ktime_t start, low, high, done;
	int ret;

	/* Probe leaves the bootloader level intact; drive reset here. */
	ret = gpiod_direction_output(ctx->reset_gpio, 0);
	if (ret)
		return ret;
	start = ktime_get();
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	low = ktime_get();
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	high = ktime_get();
	usleep_range(10000, 11000);
	done = ktime_get();
	if (READ_ONCE(trace))
		dev_info(&ctx->dsi->dev,
			 "PANEL TRACE reset: physical_sequence=1,0,1 high_us=%lld low_us=%lld settle_us=%lld final_raw=%d\n",
			 (long long)ktime_us_delta(low, start),
			 (long long)ktime_us_delta(high, low),
			 (long long)ktime_us_delta(done, high),
			 gpiod_get_raw_value_cansleep(ctx->reset_gpio));

	return 0;
}

static void boe_td4320_disable_supplies_from(struct boe_td4320 *ctx, int last)
{
	int i;

	for (i = last; i >= 0; i--) {
		if (i == BOE_TD4320_VSN)
			msleep(5);
		else if (i == BOE_TD4320_IOVCC)
			msleep(20);

		regulator_set_load(ctx->supplies[i].consumer,
				   boe_td4320_disable_loads[i]);
		regulator_disable(ctx->supplies[i].consumer);
	}
}

static int boe_td4320_enable_supplies(struct boe_td4320 *ctx)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(boe_td4320_supplies); i++) {
		ret = regulator_set_load(ctx->supplies[i].consumer,
					 boe_td4320_enable_loads[i]);
		if (ret)
			goto disable_previous;

		ret = regulator_enable(ctx->supplies[i].consumer);
		if (ret) {
			regulator_set_load(ctx->supplies[i].consumer,
					   boe_td4320_disable_loads[i]);
			goto disable_previous;
		}

		if (i == BOE_TD4320_IOVCC)
			msleep(10);
		else if (i == BOE_TD4320_VSP)
			msleep(20);
	}

	if (READ_ONCE(trace))
		dev_info(&ctx->dsi->dev,
			 "PANEL TRACE supplies enabled; setpoints_uV: iovcc=%d vsp=%d vsn=%d\n",
			 regulator_get_voltage(ctx->supplies[BOE_TD4320_IOVCC].consumer),
			 regulator_get_voltage(ctx->supplies[BOE_TD4320_VSP].consumer),
			 regulator_get_voltage(ctx->supplies[BOE_TD4320_VSN].consumer));

	return 0;

disable_previous:
	boe_td4320_disable_supplies_from(ctx, i - 1);
	return ret;
}

static void boe_td4320_disable_supplies(struct boe_td4320 *ctx)
{
	boe_td4320_disable_supplies_from(ctx, BOE_TD4320_VSN);
}

static void boe_td4320_pause_prepare(struct device *dev, unsigned int stage,
				   const char *description)
{
	if (READ_ONCE(prepare_pause_stage) != stage)
		return;

	dev_info(dev, "PANEL TEST HOLD STAGE %u: %s\n", stage, description);
	while (READ_ONCE(prepare_pause_stage) == stage)
		msleep(100);
	dev_info(dev, "PANEL TEST RELEASE STAGE %u\n", stage);
}

static int boe_td4320_vendor_config(struct boe_td4320 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb8,
				     0x23, 0x51, 0x01, 0xbe, 0x00, 0x04, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb9,
				     0x67, 0x51, 0x02, 0xe6, 0x00, 0x02, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xba,
				     0xcd, 0x51, 0x03, 0xe6, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
				     0x5d, 0x40, 0x60, 0x7f, 0x9b, 0xb9,
				     0xce, 0xdb, 0xe7, 0xef, 0xf2, 0xf4,
				     0xf5, 0xf7, 0xf9, 0xfb, 0xfd, 0xff,
				     0x04, 0x00, 0x04, 0x04, 0x42, 0x00,
				     0x69, 0x5a, 0x40, 0x40, 0x00, 0x00,
				     0x04, 0xfa, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xca,
				     0x1c, 0xfc, 0xfc, 0xfc, 0x00, 0x07,
				     0x0b, 0x07, 0x00, 0x07, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
				     0xfd, 0xfd, 0x00, 0xdb, 0xc8, 0x80,
				     0x84, 0xc8, 0x80, 0x84, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x03);

	return dsi_ctx.accum_err;
}

static int boe_td4320_on(struct boe_td4320 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	struct device *dev = &ctx->dsi->dev;
	bool vendor_first = READ_ONCE(vendor_before_wake);

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	if (vendor_first || READ_ONCE(trace))
		dev_info(dev, "PANEL TEST INIT ORDER: %s\n",
			 vendor_first ? "controls -> vendor -> 11 -> 75ms -> 29" :
			 "controls -> 29 -> 11 -> 75ms -> vendor");

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				     0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	if (dsi_ctx.accum_err)
		return dsi_ctx.accum_err;
	boe_td4320_pause_prepare(dev, 3, vendor_first ?
		"controls complete; before vendor commands and Sleep Out" :
		"51 ff, 53 24, 55 00 complete; before Display On");

	if (vendor_first) {
		dsi_ctx.accum_err = boe_td4320_vendor_config(ctx);
	} else {
		mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
		if (!dsi_ctx.accum_err)
			boe_td4320_pause_prepare(dev, 4,
				"Display On (29) complete; before Sleep Out");
	}
	if (dsi_ctx.accum_err)
		return dsi_ctx.accum_err;

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 75);
	if (dsi_ctx.accum_err)
		return dsi_ctx.accum_err;
	boe_td4320_pause_prepare(dev, 5, vendor_first ?
		"Sleep Out (11) and 75ms complete; before Display On" :
		"Sleep Out (11) and 75ms complete; before vendor commands");

	if (vendor_first) {
		mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
		if (!dsi_ctx.accum_err)
			boe_td4320_pause_prepare(dev, 4,
				"Display On (29) complete; Sleep Out already sent");
	} else {
		dsi_ctx.accum_err = boe_td4320_vendor_config(ctx);
	}

	return dsi_ctx.accum_err;
}

static int boe_td4320_off(struct boe_td4320 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 70);

	return dsi_ctx.accum_err;
}

static void boe_td4320_dimming_work(struct work_struct *work)
{
	struct boe_td4320 *ctx = container_of(to_delayed_work(work),
						struct boe_td4320, dimming_work);
	int ret;

	if (ctx->preserve_bootloader)
		return;

	/* Android sends this after the backlight first turns on. */
	ret = mipi_dsi_dcs_write(ctx->dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				 (u8[]){ 0x2c }, 1);
	if (ret < 0)
		dev_err(&ctx->dsi->dev, "Failed to enable dimming: %d\n", ret);
}

static int boe_td4320_enable(struct drm_panel *panel)
{
	struct boe_td4320 *ctx = to_boe_td4320(panel);

	if (ctx->preserve_bootloader) {
		dev_info(&ctx->dsi->dev,
			 "PANEL KEEP: enable, no dimming command; MSM scanout may proceed\n");
		return 0;
	}

	schedule_delayed_work(&ctx->dimming_work, msecs_to_jiffies(120));
	return 0;
}

static int boe_td4320_disable(struct drm_panel *panel)
{
	struct boe_td4320 *ctx = to_boe_td4320(panel);

	cancel_delayed_work_sync(&ctx->dimming_work);
	return 0;
}

static int boe_td4320_prepare(struct drm_panel *panel)
{
	struct boe_td4320 *ctx = to_boe_td4320(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->preserve_bootloader) {
		dev_info(dev,
			 "PANEL KEEP: prepare, preserving bootloader supplies/reset/commands/backlight\n");
		return 0;
	}

	/* The upstream DSI bridge has enabled its PHY, clocks and host here. */
	if (READ_ONCE(hold_before_prepare)) {
		dev_info(dev,
			 "PANEL TEST HOLD BEFORE PREPARE: before supplies, reset and commands; write N to hold_before_prepare to resume\n");
		while (READ_ONCE(hold_before_prepare))
			msleep(100);
		dev_info(dev, "PANEL TEST RELEASE: allowing panel preparation\n");
	}

	if (READ_ONCE(trace))
		dev_info(dev, "PANEL TRACE prepare begin\n");
	ret = boe_td4320_enable_supplies(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}
	ret = boe_td4320_reset(ctx);
	if (ret) {
		dev_err(dev, "Failed to reset panel: %d\n", ret);
		boe_td4320_disable_supplies(ctx);
		return ret;
	}
	if (READ_ONCE(trace))
		dev_info(dev, "PANEL TRACE initialization begin, LP mode\n");
	ret = boe_td4320_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		boe_td4320_disable_supplies(ctx);
		return ret;
	}
	if (READ_ONCE(trace))
		dev_info(dev, "PANEL TRACE initialization complete, ret=%d\n", ret);

	/* Keep the first commit before scanout while observing an awake panel. */
	if (READ_ONCE(hold_after_init)) {
		ret = backlight_enable(panel->backlight);
		dev_info(dev,
			 "PANEL TEST HOLD: initialization complete, before DPU scanout; backlight=%s ret=%d; write N to hold_after_init to resume\n",
			 panel->backlight ? "present" : "absent", ret);
		while (READ_ONCE(hold_after_init))
			msleep(100);
		dev_info(dev, "PANEL TEST RELEASE: allowing first display commit\n");
	}
	return 0;
}

static int boe_td4320_unprepare(struct drm_panel *panel)
{
	struct boe_td4320 *ctx = to_boe_td4320(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	cancel_delayed_work_sync(&ctx->dimming_work);

	if (ctx->preserve_bootloader) {
		dev_info(dev,
			 "PANEL KEEP: unprepare, leaving supplies/reset/commands untouched\n");
		return 0;
	}

	if (READ_ONCE(trace))
		dev_info(dev, "PANEL TRACE unprepare begin, HS off commands\n");
	ret = boe_td4320_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_direction_output(ctx->reset_gpio, 1);
	boe_td4320_disable_supplies(ctx);
	if (READ_ONCE(trace))
		dev_info(dev, "PANEL TRACE unprepare complete, off_ret=%d\n", ret);

	return 0;
}

static const struct drm_display_mode boe_td4320_mode = {
	.clock = (1080 + 92 + 20 + 112) * (2280 + 12 + 4 + 60) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 92,
	.hsync_end = 1080 + 92 + 20,
	.htotal = 1080 + 92 + 20 + 112,
	.vdisplay = 2280,
	.vsync_start = 2280 + 12,
	.vsync_end = 2280 + 12 + 4,
	.vtotal = 2280 + 12 + 4 + 60,
	.width_mm = 68,
	.height_mm = 144,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int boe_td4320_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &boe_td4320_mode);
}

static const struct drm_panel_funcs boe_td4320_panel_funcs = {
	.prepare = boe_td4320_prepare,
	.enable = boe_td4320_enable,
	.disable = boe_td4320_disable,
	.unprepare = boe_td4320_unprepare,
	.get_modes = boe_td4320_get_modes,
};

static int boe_td4320_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct boe_td4320 *ctx;
	int ret;

	dev_info(dev, "%s: enter\n", __func__);

	ctx = devm_drm_panel_alloc(dev, struct boe_td4320, panel,
				   &boe_td4320_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx)) {
		dev_err(dev, "Failed to alloc panel: %ld\n", PTR_ERR(ctx));
		return PTR_ERR(ctx);
	}

	/* Without a reset GPIO, rely on the bootloader's complete panel setup. */
	ctx->preserve_bootloader =
		!of_property_present(dev->of_node, "reset-gpios");
	if (!ctx->preserve_bootloader) {
		ret = devm_regulator_bulk_get_const(dev,
						ARRAY_SIZE(boe_td4320_supplies),
						boe_td4320_supplies,
						&ctx->supplies);
		if (ret < 0) {
			/* -EPROBE_DEFER: supply provider not bound yet. */
			dev_err(dev, "Failed to get regulators iovcc/vsn/vsp: %d\n",
				ret);
			return ret;
		}

		ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
		if (IS_ERR(ctx->reset_gpio)) {
			ret = PTR_ERR(ctx->reset_gpio);
			dev_err(dev, "Failed to get reset-gpios: %d\n", ret);
			return ret;
		}
	}

	ctx->dsi = dsi;
	INIT_DELAYED_WORK(&ctx->dimming_work, boe_td4320_dimming_work);
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE;

	ctx->panel.prepare_prev_first = true;

	/* The panel core otherwise switches WLED in enable/disable callbacks. */
	if (!ctx->preserve_bootloader) {
		ret = drm_panel_of_backlight(&ctx->panel);
		if (ret) {
			/* -EPROBE_DEFER: backlight provider not bound yet. */
			dev_err(dev, "Failed to get backlight: %d\n", ret);
			return ret;
		}
	} else {
		dev_info(dev,
			 "PANEL KEEP: probe, using bootloader state; no panel hardware operations\n");
	}

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);

	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		return ret;
	}

	ret = device_create_file(dev, &dev_attr_power_mode);
	if (ret)
		dev_warn(dev, "Failed to create power_mode diagnostic: %d\n", ret);

	dev_info(dev, "%s: done\n", __func__);

	return 0;
}

static void boe_td4320_remove(struct mipi_dsi_device *dsi)
{
	struct boe_td4320 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	cancel_delayed_work_sync(&ctx->dimming_work);
	device_remove_file(&dsi->dev, &dev_attr_power_mode);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_td4320_of_match[] = {
	{ .compatible = "boe,td4320" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_td4320_of_match);

static struct mipi_dsi_driver boe_td4320_driver = {
	.probe = boe_td4320_probe,
	.remove = boe_td4320_remove,
	.driver = {
		.name = "panel-boe-td4320",
		.of_match_table = boe_td4320_of_match,
	},
};
module_mipi_dsi_driver(boe_td4320_driver);

MODULE_AUTHOR("Barnabas Czeman <barnabas.czeman@mainlining.org>");
MODULE_DESCRIPTION("DRM driver for boe td4320 fhdplus video mode dsi panel");
MODULE_LICENSE("GPL");
