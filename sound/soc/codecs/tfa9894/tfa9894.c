// SPDX-License-Identifier: GPL-2.0-only
/* TFA9894 local CoolFlux DSP amplifier, using the original speaker container. */
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "tfa_container.h"
#include "tfa_internal.h"
#include "tfa9894_tfafieldnames.h"
#include "tfa.h"

#define TFA9894_AAC_FW "qcom/sound/tfa98xx_aac.cnt"
#define TFA9894_GOER_FW "qcom/sound/tfa98xx_goer.cnt"
#define TFA9894_RATE_COUNT 9

struct tfa9894 {
	struct device *dev;
	struct i2c_client *i2c;
	struct regmap *regmap;
	struct gpio_desc *reset;
	struct mutex lock;
	struct tfa_device dsp;
	struct tfa_container *container;
	const char *firmware;
	unsigned int rates[TFA9894_RATE_COUNT];
	int profiles[TFA9894_RATE_COUNT];
	struct snd_pcm_hw_constraint_list rate_constraint;
	int profile;
	int vstep;
	bool running;
};

/* Transport callbacks used by the container/DSP implementation. */
enum tfa98xx_error tfa98xx_write_register16(struct tfa_device *dsp,
					 unsigned char reg, unsigned short value)
{
	struct tfa9894 *amp = dsp->data;

	return regmap_write(amp->regmap, reg, value) ?
		TFA98XX_ERROR_FAIL : TFA98XX_ERROR_OK;
}

enum tfa98xx_error tfa98xx_read_register16(struct tfa_device *dsp,
					unsigned char reg, unsigned short *value)
{
	struct tfa9894 *amp = dsp->data;
	unsigned int data;
	int ret;

	ret = regmap_read(amp->regmap, reg, &data);
	if (ret)
		return TFA98XX_ERROR_FAIL;
	*value = data;
	return TFA98XX_ERROR_OK;
}

enum tfa98xx_error tfa98xx_read_data(struct tfa_device *dsp,
				   unsigned char reg, int len, unsigned char *data)
{
	struct tfa9894 *amp = dsp->data;
	struct i2c_msg msgs[] = {
		{ .addr = amp->i2c->addr, .len = 1, .buf = &reg },
		{ .addr = amp->i2c->addr, .flags = I2C_M_RD, .len = len, .buf = data },
	};

	if (len <= 0 || len > U16_MAX)
		return TFA98XX_ERROR_BAD_PARAMETER;
	return i2c_transfer(amp->i2c->adapter, msgs, ARRAY_SIZE(msgs)) ==
		ARRAY_SIZE(msgs) ? TFA98XX_ERROR_OK : TFA98XX_ERROR_FAIL;
}

enum tfa98xx_error tfa98xx_write_raw(struct tfa_device *dsp, int len,
				   const unsigned char *data)
{
	struct tfa9894 *amp = dsp->data;

	if (len <= 0 || len > U16_MAX)
		return TFA98XX_ERROR_BAD_PARAMETER;
	return i2c_master_send(amp->i2c, data, len) == len ?
		TFA98XX_ERROR_OK : TFA98XX_ERROR_FAIL;
}

static int tfa9894_powerdown(struct tfa9894 *amp)
{
	int ret;

	ret = regmap_update_bits(amp->regmap, 0x00, BIT(0) | BIT(3), BIT(0));
	if (ret)
		gpiod_set_value_cansleep(amp->reset, 1);
	amp->running = false;
	amp->dsp.stream_state = 0;
	return ret;
}

static int tfa9894_select_firmware(struct tfa9894 *amp)
{
	struct pinctrl_state *pull_down, *pull_up, *no_pull;
	struct gpio_desc *speaker_id;
	struct pinctrl *pinctrl;
	int down = -1, up = -1, ret, restore;

	pinctrl = devm_pinctrl_get(amp->dev);
	if (IS_ERR(pinctrl))
		return PTR_ERR(pinctrl);
	pull_down = pinctrl_lookup_state(pinctrl, "pull_down");
	pull_up = pinctrl_lookup_state(pinctrl, "pull_up");
	no_pull = pinctrl_lookup_state(pinctrl, "no_pull");
	if (IS_ERR(pull_down) || IS_ERR(pull_up) || IS_ERR(no_pull))
		return -EINVAL;
	speaker_id = devm_gpiod_get(amp->dev, "speaker-id", GPIOD_IN);
	if (IS_ERR(speaker_id))
		return PTR_ERR(speaker_id);

	ret = pinctrl_select_state(pinctrl, pull_down);
	if (ret)
		return ret;
	msleep(3);
	down = gpiod_get_value_cansleep(speaker_id);
	ret = pinctrl_select_state(pinctrl, pull_up);
	if (ret)
		goto restore;
	msleep(3);
	up = gpiod_get_value_cansleep(speaker_id);
	if (down < 0 || up < 0) {
		ret = down < 0 ? down : up;
		goto restore;
	}
	if (!down && !up)
		amp->firmware = TFA9894_GOER_FW;
	else if (!down && up)
		amp->firmware = TFA9894_AAC_FW;
	else
		ret = -ENODEV;
restore:
	restore = pinctrl_select_state(pinctrl, no_pull);
	if (ret || restore)
		return dev_err_probe(amp->dev, ret ?: restore,
			"Speaker ID failed (pull-down %d, pull-up %d)\n", down, up);
	dev_info(amp->dev, "Speaker firmware: %s\n", amp->firmware);
	return 0;
}

static bool tfa9894_playback_profile(const char *name)
{
	return name && !strstr(name, ".cal") && !strstr(name, ".standby");
}

static int tfa9894_create_profiles(struct tfa9894 *amp)
{
	struct tfa_device *dsp = &amp->dsp;
	const char *name, *first = NULL;
	size_t basename_len = 0;
	unsigned int rate, count = 0, i;
	int prof, nprof;

	nprof = tfa_cnt_get_dev_nprof(dsp);
	for (prof = 0; prof < nprof; prof++) {
		name = tfa_cont_profile_name(dsp->cnt, dsp->dev_idx, prof);
		if (!tfa9894_playback_profile(name))
			continue;
		if (!first) {
			first = name;
			basename_len = strcspn(first, ".");
		}
		if (!basename_len || strcspn(name, ".") != basename_len ||
		    strncmp(name, first, basename_len))
			continue;
		rate = tfa98xx_get_profile_sr(dsp, prof);
		if (!rate || rate > 48000)
			return -EINVAL;
		for (i = 0; i < count; i++)
			if (amp->rates[i] == rate)
				return -EINVAL;
		if (count == TFA9894_RATE_COUNT)
			return -EINVAL;
		amp->rates[count] = rate;
		amp->profiles[count++] = prof;
	}
	if (!count)
		return -EINVAL;
	amp->rate_constraint.list = amp->rates;
	amp->rate_constraint.count = count;
	amp->profile = -1;
	dev_info(amp->dev, "Playback profile group: %.*s\n", (int)basename_len, first);
	return 0;
}

static int tfa9894_load_firmware(struct tfa9894 *amp)
{
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, amp->firmware, amp->dev);
	if (ret)
		return dev_err_probe(amp->dev, ret, "Cannot load %s\n", amp->firmware);
	if (fw->size < sizeof(struct tfa_container) || fw->size > TFA_MAX_CNT_LENGTH) {
		ret = -EINVAL;
		goto release;
	}
	amp->container = devm_kmemdup(amp->dev, fw->data, fw->size, GFP_KERNEL);
	if (!amp->container) {
		ret = -ENOMEM;
		goto release;
	}
	ret = tfa_load_cnt(amp->container, fw->size);
	if (ret != tfa_error_ok || amp->container->ndev != 1) {
		ret = -EINVAL;
		goto release;
	}
	amp->dsp.cnt = amp->container;
	amp->dsp.buffer_size = 65535;
	ret = tfa_dev_probe(amp->i2c->addr, &amp->dsp);
	if (ret != tfa_error_ok) {
		ret = -ENODEV;
		goto release;
	}
	amp->dsp.dev_idx = tfa_cont_get_idx(&amp->dsp);
	if (amp->dsp.dev_idx != 0) {
		ret = -EINVAL;
		goto release;
	}
	ret = tfa9894_create_profiles(amp);
release:
	release_firmware(fw);
	return ret;
}

static int tfa9894_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct tfa9894 *amp = snd_soc_component_get_drvdata(dai->component);

	if (!substream->runtime)
		return 0;
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &amp->rate_constraint);
}

static int tfa9894_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_CBC_CFC ||
	    (fmt & SND_SOC_DAIFMT_FORMAT_MASK) != SND_SOC_DAIFMT_I2S ||
	    (fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF)
		return -EINVAL;
	return 0;
}

static int tfa9894_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct tfa9894 *amp = snd_soc_component_get_drvdata(dai->component);
	unsigned int i;

	mutex_lock(&amp->lock);
	if (amp->running) {
		mutex_unlock(&amp->lock);
		return -EBUSY;
	}
	amp->profile = -1;
	for (i = 0; i < amp->rate_constraint.count; i++) {
		if (amp->rates[i] != params_rate(params))
			continue;
		amp->profile = amp->profiles[i];
		/* Container volume steps count down from the loudest setting. */
		amp->vstep = max(tfa_cont_get_max_vstep(&amp->dsp, amp->profile) - 1, 0);
		break;
	}
	mutex_unlock(&amp->lock);
	return amp->profile >= 0 ? 0 : -EINVAL;
}

static int tfa9894_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct tfa9894 *amp = snd_soc_component_get_drvdata(dai->component);
	struct tfa_device *dsp = &amp->dsp;
	int ret = 0, re25;

	if (stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;
	mutex_lock(&amp->lock);
	if (mute) {
		if (amp->running)
			tfa_dev_stop(dsp);
		ret = tfa9894_powerdown(amp);
		goto out;
	}
	if (amp->running)
		goto out;
	if (amp->profile < 0) {
		ret = -EINVAL;
		goto fail;
	}
	/* Avoid automatic calibration profiles during normal playback. */
	re25 = tfa_dev_mtp_get(dsp, TFA_MTP_RE25);
	if (tfa_dev_mtp_get(dsp, TFA_MTP_EX) != 1 ||
	    tfa_dev_mtp_get(dsp, TFA_MTP_OTC) != 1 || re25 < 1000 || re25 > 32000) {
		dev_err(amp->dev, "Valid factory calibration required (RE25=%d mOhm)\n", re25);
		ret = -EIO;
		goto fail;
	}
	dsp->stream_state = BIT_PSTREAM;
	if (tfa_dev_start(dsp, amp->profile, amp->vstep) != tfa_error_ok) {
		dev_err(amp->dev, "DSP start failed\n");
		ret = -EIO;
		goto fail;
	}
	/* Validate the 9894 reference DSP route before enabling output. */
	if (dsp->is_bypass || tfa_get_bf(dsp, TFA9894_BF_CFE) != 1 ||
	    tfa_get_bf(dsp, TFA9894_BF_AMPC) != 1 ||
	    tfa_get_bf(dsp, TFA9894_BF_AMPINSEL) != 2 ||
	    tfa_get_bf(dsp, TFA9894_BF_RST) != 0 ||
	    tfa_get_bf(dsp, TFA9894_BF_CLKS) != 1 ||
	    tfa_dsp_get_calibration_impedance(dsp) != TFA98XX_ERROR_OK ||
	    dsp->mohm[0] < 1000 || dsp->mohm[0] > 32000) {
		dev_err(amp->dev, "DSP playback configuration not ready\n");
		ret = -EIO;
		goto fail;
	}
	if (tfa_dev_set_state(dsp, TFA_STATE_UNMUTE, 0) != tfa_error_ok) {
		ret = -EIO;
		goto fail;
	}
	amp->running = true;
	dev_info(amp->dev, "DSP playback ready, RE25=%d mOhm\n", dsp->mohm[0]);
	goto out;
fail:
	tfa_dev_stop(dsp);
	tfa9894_powerdown(amp);
out:
	mutex_unlock(&amp->lock);
	return ret;
}

static const struct snd_soc_dai_ops tfa9894_dai_ops = {
	.startup = tfa9894_startup,
	.set_fmt = tfa9894_set_fmt,
	.hw_params = tfa9894_hw_params,
	.mute_stream = tfa9894_mute,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver tfa9894_dai = {
	.name = "tfa9894-aif",
	.playback = {
		.stream_name = "AIF Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tfa9894_dai_ops,
};

static const struct snd_soc_dapm_widget tfa9894_widgets[] = {
	SND_SOC_DAPM_AIF_IN("AIF IN", "AIF Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("OUTL"),
};

static const struct snd_soc_dapm_route tfa9894_routes[] = {
	{ "OUTL", NULL, "AIF IN" },
};

static const struct snd_soc_component_driver tfa9894_component = {
	.dapm_widgets = tfa9894_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tfa9894_widgets),
	.dapm_routes = tfa9894_routes,
	.num_dapm_routes = ARRAY_SIZE(tfa9894_routes),
};

static const struct regmap_config tfa9894_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0xff,
	.cache_type = REGCACHE_NONE,
};

static void tfa9894_reset_action(void *data)
{
	struct tfa9894 *amp = data;

	gpiod_set_value_cansleep(amp->reset, 1);
}

static int tfa9894_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct tfa9894 *amp;
	unsigned int rev;
	int ret;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;
	amp = devm_kzalloc(dev, sizeof(*amp), GFP_KERNEL);
	if (!amp)
		return -ENOMEM;
	amp->dev = dev;
	amp->i2c = i2c;
	amp->dsp.data = amp;
	amp->profile = -1;
	mutex_init(&amp->lock);
	i2c_set_clientdata(i2c, amp);
	amp->regmap = devm_regmap_init_i2c(i2c, &tfa9894_regmap);
	if (IS_ERR(amp->regmap))
		return PTR_ERR(amp->regmap);
	amp->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(amp->reset))
		return dev_err_probe(dev, PTR_ERR(amp->reset), "Cannot get reset GPIO\n");
	ret = devm_add_action_or_reset(dev, tfa9894_reset_action, amp);
	if (ret)
		return ret;
	ret = tfa9894_select_firmware(amp);
	if (ret)
		return ret;
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(amp->reset, 0);
	usleep_range(5000, 6000);
	ret = regmap_read(amp->regmap, 0x03, &rev);
	if (ret)
		return ret;
	if (rev != 0x0a94 && rev != 0x1a94 && rev != 0x2a94 && rev != 0x3a94)
		return dev_err_probe(dev, -ENODEV, "Unsupported TFA9894 revision 0x%x\n", rev);
	ret = tfa9894_powerdown(amp);
	if (ret)
		return ret;
	ret = tfa9894_load_firmware(amp);
	if (ret)
		return ret;
	return devm_snd_soc_register_component(dev, &tfa9894_component, &tfa9894_dai, 1);
}

static void tfa9894_remove(struct i2c_client *i2c)
{
	struct tfa9894 *amp = i2c_get_clientdata(i2c);

	mutex_lock(&amp->lock);
	if (amp->running)
		tfa_dev_stop(&amp->dsp);
	tfa9894_powerdown(amp);
	mutex_unlock(&amp->lock);
}

static const struct of_device_id tfa9894_of_match[] = {
	{ .compatible = "nxp,tfa9894" },
	{ }
};
MODULE_DEVICE_TABLE(of, tfa9894_of_match);

static const struct i2c_device_id tfa9894_i2c_ids[] = {
	{ "tfa9894" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tfa9894_i2c_ids);

static struct i2c_driver tfa9894_driver = {
	.driver = {
		.name = "tfa9894",
		.of_match_table = tfa9894_of_match,
	},
	.probe = tfa9894_probe,
	.remove = tfa9894_remove,
	.id_table = tfa9894_i2c_ids,
};
module_i2c_driver(tfa9894_driver);

MODULE_DESCRIPTION("TFA9894 speaker amplifier with local CoolFlux DSP");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(TFA9894_AAC_FW);
MODULE_FIRMWARE(TFA9894_GOER_FW);
