// SPDX-License-Identifier: GPL-2.0-only
/*
 * SDM660 GFX supply management with the GFX LDO in BHS mode.
 *
 * The downstream platina configuration disables both LDO operation and
 * closed-loop CPR. GFX therefore follows CX, with a separate MX vote for
 * the GPU PLLs and a memory-access configuration for each CX level.
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/power/qcom-rpmpd.h>

#define PWRSWITCH_CTRL		0x1c
#define PWRSWITCH_STATUS		0x28
#define LDO_CLAMP_IO		BIT(31)
#define EN_LDOAP_CTRL_CPR		BIT(29)
#define PWR_SRC_SEL		BIT(9)
#define ACK_SW_OVR		BIT(8)
#define LDO_BYPASS		BIT(6)
#define LDO_PDN			BIT(5)
#define LDO_UNDER_SW_CTRL		BIT(4)
#define BHS_EN_REST		BIT(2)
#define BHS_EN_FEW		BIT(1)
#define BHS_UNDER_SW_CTRL		BIT(0)
#define LDO_READY		BIT(2)
#define BHS_EN_REST_ACK		BIT(1)
#define MEM_ACC_LOW_VOLTAGE	BIT(0)
#define GPU_GX_GDSCR		0x1094
#define GDSC_POWER_ON		BIT(31)

enum sdm660_gfx_domain {
	GFX_CX,
	GFX_MX,
	GFX_GX,
	GFX_NUM_DOMAINS,
};

struct sdm660_gfx {
	struct generic_pm_domain pd;
	struct device *dev;
	struct dev_pm_domain_list *supplies;
	struct regmap *gpucc;
	void __iomem *bhs;
	void __iomem *mem_acc;
	unsigned int level;
	bool enabled;
};

static struct sdm660_gfx *to_sdm660_gfx(struct generic_pm_domain *pd)
{
	return container_of(pd, struct sdm660_gfx, pd);
}

static void sdm660_gfx_mem_acc(struct sdm660_gfx *gfx, unsigned int level)
{
	u32 val = readl(gfx->mem_acc);

	/* Downstream corners 1..3 select 1; corners 4..7 select 0. */
	if (level < RPM_SMD_LEVEL_SVS_PLUS)
		val |= MEM_ACC_LOW_VOLTAGE;
	else
		val &= ~MEM_ACC_LOW_VOLTAGE;
	writel(val, gfx->mem_acc);
	readl(gfx->mem_acc);
}

static int sdm660_gfx_set_level(struct sdm660_gfx *gfx, unsigned int level)
{
	struct device *cx = gfx->supplies->pd_devs[GFX_CX];
	unsigned int old_level = gfx->level;
	int ret;

	/* GX/SMMU may still be on at the GPU's idle OPP. */
	level = max(level, RPM_SMD_LEVEL_LOW_SVS);
	if (level < old_level)
		sdm660_gfx_mem_acc(gfx, level);

	ret = dev_pm_domain_set_performance_state(cx, level);
	if (ret) {
		/* RPM can fail after updating its cached corner. Restore both. */
		if (dev_pm_domain_set_performance_state(cx, old_level))
			dev_err(gfx->dev, "failed to restore CX level %u\n", old_level);
		else if (level < old_level)
			sdm660_gfx_mem_acc(gfx, old_level);
		return ret;
	}

	if (level >= old_level)
		sdm660_gfx_mem_acc(gfx, level);
	gfx->level = level;
	return 0;
}

static int sdm660_gfx_set_performance(struct generic_pm_domain *pd,
				      unsigned int state)
{
	struct sdm660_gfx *gfx = to_sdm660_gfx(pd);

	switch (state) {
	case 0:
	case RPM_SMD_LEVEL_MIN_SVS:
	case RPM_SMD_LEVEL_LOW_SVS:
	case RPM_SMD_LEVEL_SVS:
	case RPM_SMD_LEVEL_SVS_PLUS:
	case RPM_SMD_LEVEL_NOM:
	case RPM_SMD_LEVEL_NOM_PLUS:
	case RPM_SMD_LEVEL_TURBO:
		break;
	default:
		return -EINVAL;
	}

	if (!gfx->enabled) {
		gfx->level = max(state, RPM_SMD_LEVEL_LOW_SVS);
		return 0;
	}

	return sdm660_gfx_set_level(gfx, state);
}

static int sdm660_gfx_enable_bhs(struct sdm660_gfx *gfx)
{
	u32 ctl = readl(gfx->bhs + PWRSWITCH_CTRL);
	u32 status = readl(gfx->bhs + PWRSWITCH_STATUS);
	int ret;

	/* Preserve a live firmware LDO supply until BHS has acknowledged. */
	if ((ctl & PWR_SRC_SEL) && (status & LDO_READY)) {
		u32 saved_ctl = ctl;

		ctl |= ACK_SW_OVR;
		writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
		ctl &= ~(PWR_SRC_SEL | LDO_PDN);
		writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
		ctl |= LDO_UNDER_SW_CTRL;
		writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
		ctl |= BHS_EN_FEW;
		writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
		ctl |= BHS_EN_REST;
		writel(ctl, gfx->bhs + PWRSWITCH_CTRL);

		ret = readl_poll_timeout(gfx->bhs + PWRSWITCH_STATUS, status,
					status & BHS_EN_REST_ACK, 10, 500);
		if (ret) {
			writel(saved_ctl, gfx->bhs + PWRSWITCH_CTRL);
			readl(gfx->bhs + PWRSWITCH_CTRL);
			return ret;
		}

		ctl |= LDO_BYPASS | LDO_PDN;
		writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
		ctl &= ~(ACK_SW_OVR | BHS_UNDER_SW_CTRL);
		writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
	}

	/* Match downstream enable_bhs_mode(): BHS is controlled by GX GDSC. */
	ctl &= ~LDO_CLAMP_IO;
	ctl |= EN_LDOAP_CTRL_CPR | LDO_UNDER_SW_CTRL;
	writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
	ctl &= ~PWR_SRC_SEL;
	writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
	ctl &= ~BHS_EN_FEW;
	writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
	ctl &= ~BHS_EN_REST;
	writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
	ctl &= ~BHS_UNDER_SW_CTRL;
	writel(ctl, gfx->bhs + PWRSWITCH_CTRL);
	readl(gfx->bhs + PWRSWITCH_CTRL);
	return 0;
}

static int sdm660_gfx_power_on(struct generic_pm_domain *pd)
{
	struct sdm660_gfx *gfx = to_sdm660_gfx(pd);
	struct device **supplies = gfx->supplies->pd_devs;
	int ret;

	ret = dev_pm_domain_set_performance_state(supplies[GFX_MX],
						RPM_SMD_LEVEL_SVS_PLUS);
	if (ret)
		return ret;
	ret = pm_runtime_resume_and_get(supplies[GFX_MX]);
	if (ret < 0)
		return ret;

	ret = dev_pm_domain_set_performance_state(supplies[GFX_CX], gfx->level);
	if (ret)
		goto put_mx;
	ret = pm_runtime_resume_and_get(supplies[GFX_CX]);
	if (ret < 0)
		goto put_mx;

	sdm660_gfx_mem_acc(gfx, gfx->level);
	ret = sdm660_gfx_enable_bhs(gfx);
	if (ret)
		goto put_cx;

	/* Only bring up GX (and its CX GDSC parent) once supplies are ready. */
	ret = pm_runtime_resume_and_get(supplies[GFX_GX]);
	if (ret < 0)
		goto put_cx;

	gfx->enabled = true;
	return 0;

put_cx:
	pm_runtime_put_sync_suspend(supplies[GFX_CX]);
put_mx:
	pm_runtime_put_sync_suspend(supplies[GFX_MX]);
	return ret;
}

static int sdm660_gfx_power_off(struct generic_pm_domain *pd)
{
	struct sdm660_gfx *gfx = to_sdm660_gfx(pd);
	struct device **supplies = gfx->supplies->pd_devs;
	u32 status;
	int ret;

	/* Supply votes must survive until GX has finished collapsing. */
	ret = pm_runtime_put_sync_suspend(supplies[GFX_GX]);
	if (ret < 0) {
		pm_runtime_get_noresume(supplies[GFX_GX]);
		return ret;
	}

	/*
	 * A suspended virtual device does not guarantee that genpd collapsed
	 * GX: boot handover, other consumers or a GDSC timeout can keep it on.
	 * Do not withdraw supply votes in that case.
	 */
	ret = regmap_read(gfx->gpucc, GPU_GX_GDSCR, &status);
	if (!ret && (status & GDSC_POWER_ON))
		ret = -EBUSY;
	if (ret)
		goto resume_gx;

	ret = pm_runtime_put_sync_suspend(supplies[GFX_CX]);
	if (ret < 0) {
		pm_runtime_get_noresume(supplies[GFX_CX]);
		goto resume_gx;
	}
	ret = pm_runtime_put_sync_suspend(supplies[GFX_MX]);
	if (ret < 0) {
		pm_runtime_get_noresume(supplies[GFX_MX]);
		if (pm_runtime_resume_and_get(supplies[GFX_CX]) < 0)
			dev_err(gfx->dev, "failed to restore CX after power-off error\n");
		goto resume_gx;
	}

	gfx->enabled = false;
	return 0;

resume_gx:
	if (pm_runtime_resume_and_get(supplies[GFX_GX]) < 0)
		dev_err(gfx->dev, "failed to restore GX after power-off error\n");
	return ret;
}

static int sdm660_gfx_probe(struct platform_device *pdev)
{
	static const char * const names[] = { "cx", "mx", "gx" };
	const struct dev_pm_domain_attach_data attach = {
		.pd_names = names,
		.num_pd_names = ARRAY_SIZE(names),
		.pd_flags = PD_FLAG_NO_DEV_LINK,
	};
	struct device *dev = &pdev->dev;
	struct platform_device *gpucc;
	struct of_phandle_args args;
	struct sdm660_gfx *gfx;
	int ret;

	gfx = devm_kzalloc(dev, sizeof(*gfx), GFP_KERNEL);
	if (!gfx)
		return -ENOMEM;
	gfx->dev = dev;
	gfx->bhs = devm_platform_ioremap_resource_byname(pdev, "bhs");
	if (IS_ERR(gfx->bhs))
		return PTR_ERR(gfx->bhs);
	gfx->mem_acc = devm_platform_ioremap_resource_byname(pdev, "mem-acc");
	if (IS_ERR(gfx->mem_acc))
		return PTR_ERR(gfx->mem_acc);

	ret = devm_pm_domain_attach_list(dev, &attach, &gfx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to attach GFX supplies\n");
	if (ret != GFX_NUM_DOMAINS)
		return -EINVAL;

	ret = of_parse_phandle_with_args(dev->of_node, "power-domains",
					"#power-domain-cells", GFX_GX, &args);
	if (ret)
		return ret;
	gpucc = of_find_device_by_node(args.np);
	of_node_put(args.np);
	if (!gpucc)
		return -EPROBE_DEFER;
	gfx->gpucc = dev_get_regmap(&gpucc->dev, NULL);
	put_device(&gpucc->dev);
	if (!gfx->gpucc)
		return -EPROBE_DEFER;

	/* Retain a conservative voltage until the first GPU OPP request. */
	gfx->level = RPM_SMD_LEVEL_TURBO;
	gfx->pd.name = "sdm660-gfx";
	gfx->pd.power_on = sdm660_gfx_power_on;
	gfx->pd.power_off = sdm660_gfx_power_off;
	gfx->pd.set_performance_state = sdm660_gfx_set_performance;
	ret = pm_genpd_init(&gfx->pd, NULL, true);
	if (ret)
		return ret;
	ret = of_genpd_add_provider_simple(dev->of_node, &gfx->pd);
	if (ret)
		pm_genpd_remove(&gfx->pd);
	return ret;
}

static const struct of_device_id sdm660_gfx_match[] = {
	{ .compatible = "qcom,sdm660-gfx-bhs" },
	{ }
};

static void sdm660_gfx_sync_state(struct device *dev)
{
	of_genpd_sync_state(dev->of_node);
}

static struct platform_driver sdm660_gfx_driver = {
	.probe = sdm660_gfx_probe,
	.driver = {
		.name = "qcom-sdm660-gfx",
		.of_match_table = sdm660_gfx_match,
		.suppress_bind_attrs = true,
		.sync_state = sdm660_gfx_sync_state,
	},
};
builtin_platform_driver(sdm660_gfx_driver);

MODULE_DESCRIPTION("Qualcomm SDM660 GFX BHS power domain");
MODULE_LICENSE("GPL");
