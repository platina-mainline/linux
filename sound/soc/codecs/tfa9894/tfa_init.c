/* Copyright 2014-2017 NXP Semiconductors. See LICENSE.nxp.txt. */
#include "tfa_service.h"
#include "tfa_internal.h"
#include "tfa_container.h"
#include "tfa9894_tfafieldnames.h"

static enum tfa98xx_error tfa9894_noop(struct tfa_device *tfa)
{
	return TFA98XX_ERROR_OK;
}

static enum tfa98xx_error tfa9894_noop_arg(struct tfa_device *tfa, int value)
{
	return TFA98XX_ERROR_OK;
}

static enum tfa98xx_error tfa9894_dsp_reset(struct tfa_device *tfa, int state)
{
	int ret = tfa_set_bf_volatile(tfa, TFA9894_BF_RST, state);

	return ret < 0 ? -ret : TFA98XX_ERROR_OK;
}

static int tfa9894_set_swprofile
(struct tfa_device *tfa, unsigned short new_value)
{
	int active_value = tfa_dev_get_swprof(tfa);

	/* Set the new value in the struct */
	tfa->profile = new_value - 1;
	tfa_set_bf_volatile(tfa, TFA9894_BF_SWPROFIL, new_value);

	return active_value;
}

static int tfa9894_get_swprofile(struct tfa_device *tfa)
{
	return tfa_get_bf(tfa, TFA9894_BF_SWPROFIL) - 1;
}

static int tfa9894_set_swvstep
(struct tfa_device *tfa, unsigned short new_value)
{
	/* Set the new value in the struct */
	tfa->vstep = new_value - 1;
	tfa_set_bf_volatile(tfa, TFA9894_BF_SWVSTEP, new_value);

	return new_value;
}

static int tfa9894_get_swvstep(struct tfa_device *tfa)
{
	return tfa_get_bf(tfa, TFA9894_BF_SWVSTEP) - 1;
}

static int tfa9894_get_mtpb(struct tfa_device *tfa)
{
	int value = 0;

	value = tfa_get_bf(tfa, TFA9894_BF_MTPB);

	return value;
}

/** Set internal oscillator into power down mode for TFA9894.
*
*  This function is a worker for tfa98xx_set_osc_powerdown().
*
*  @param[in] tfa device description structure
*  @param[in] state new state 0 - oscillator is on, 1 oscillator is off.
*
*  @return TFA98XX_ERROR_OK when successfull, error otherwise.
*/
static enum tfa98xx_error tfa9894_set_osc_powerdown
(struct tfa_device *tfa, int state)
{
	if (state == 1 || state == 0) {
		return -tfa_set_bf(tfa, TFA9894_BF_MANAOOSC, (uint16_t)state);
	}

	return TFA98XX_ERROR_BAD_PARAMETER;
}

static enum tfa98xx_error tfa9894_faim_protect
(struct tfa_device *tfa, int status)
{
	enum tfa98xx_error ret = TFA98XX_ERROR_OK;
	/* 0b = FAIM protection enabled 1b = FAIM protection disabled*/
	ret = tfa_set_bf_volatile(tfa, TFA9894_BF_OPENMTP, (uint16_t)(status));
	return ret;
}

static enum tfa98xx_error tfa9894_specific(struct tfa_device *tfa)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	unsigned short value, xor;

	if (tfa->in_use == 0)
		return TFA98XX_ERROR_NOT_OPEN;
	if (tfa->verbose)
		if (is_94_N2_device(tfa))
			pr_debug("check_correct\n");
	/* Propagate every I2C failure: partial optimal settings cannot be used. */
#define TFA9894_WRITE(reg, value) do { \
		error = reg_write(tfa, reg, value); \
		if (error != TFA98XX_ERROR_OK) \
			return error; \
	} while (0)

	/* Unlock keys to write settings */
	TFA9894_WRITE(0x0F, 0x5A6B);
	error = reg_read(tfa, 0xFB, &value);
	if (error != TFA98XX_ERROR_OK)
		return error;
	xor = value ^ 0x005A;
	TFA9894_WRITE(0xA0, xor);
	pr_debug("Device REFID:%x\n", tfa->rev);
	/* The optimal settings */
	if (tfa->rev == 0x0a94) {
		/* V36 */
		/* ----- generated code start ----- */
		TFA9894_WRITE(0x00, 0xa245); //POR=0x8245
		TFA9894_WRITE(0x02, 0x51e8); //POR=0x55c8
		TFA9894_WRITE(0x52, 0xbe17); //POR=0xb617
		TFA9894_WRITE(0x57, 0x0344); //POR=0x0366
		TFA9894_WRITE(0x61, 0x0033); //POR=0x0073
		TFA9894_WRITE(0x71, 0x00cf); //POR=0x018d
		TFA9894_WRITE(0x72, 0x34a9); //POR=0x44e8
		TFA9894_WRITE(0x73, 0x3808); //POR=0x3806
		TFA9894_WRITE(0x76, 0x0067); //POR=0x0065
		TFA9894_WRITE(0x80, 0x0000); //POR=0x0003
		TFA9894_WRITE(0x81, 0x5715); //POR=0x561a
		TFA9894_WRITE(0x82, 0x0104); //POR=0x0044
		/* ----- generated code end   ----- */
	} else if (tfa->rev == 0x1a94) {
		/* V17 */
		/* ----- generated code start ----- */
		TFA9894_WRITE(0x00, 0xa245); //POR=0x8245
		TFA9894_WRITE(0x01, 0x15da); //POR=0x11ca
		TFA9894_WRITE(0x02, 0x5288); //POR=0x55c8
		TFA9894_WRITE(0x52, 0xbe17); //POR=0xb617
		TFA9894_WRITE(0x53, 0x0dbe); //POR=0x0d9e
		TFA9894_WRITE(0x56, 0x05c3); //POR=0x07c3
		TFA9894_WRITE(0x57, 0x0344); //POR=0x0366
		TFA9894_WRITE(0x61, 0x0032); //POR=0x0073
		TFA9894_WRITE(0x71, 0x00cf); //POR=0x018d
		TFA9894_WRITE(0x72, 0x34a9); //POR=0x44e8
		TFA9894_WRITE(0x73, 0x38c8); //POR=0x3806
		TFA9894_WRITE(0x76, 0x0067); //POR=0x0065
		TFA9894_WRITE(0x80, 0x0000); //POR=0x0003
		TFA9894_WRITE(0x81, 0x5799); //POR=0x561a
		TFA9894_WRITE(0x82, 0x0104); //POR=0x0044
		/* ----- generated code end ----- */

	} else if (tfa->rev == 0x2a94 || tfa->rev == 0x3a94) {
		/* ----- generated code start ----- */
		/* -----  version 25.00 ----- */
		TFA9894_WRITE(0x01, 0x15da); //POR=0x11ca
		TFA9894_WRITE(0x02, 0x51e8); //POR=0x55c8
		TFA9894_WRITE(0x04, 0x0200); //POR=0x0000
		TFA9894_WRITE(0x52, 0xbe17); //POR=0xb617
		TFA9894_WRITE(0x53, 0x0dbe); //POR=0x0d9e
		TFA9894_WRITE(0x57, 0x0344); //POR=0x0366
		TFA9894_WRITE(0x61, 0x0032); //POR=0x0073
		TFA9894_WRITE(0x71, 0x6ecf); //POR=0x6f8d
		TFA9894_WRITE(0x72, 0xb4a9); //POR=0x44e8
		TFA9894_WRITE(0x73, 0x38c8); //POR=0x3806
		TFA9894_WRITE(0x76, 0x0067); //POR=0x0065
		TFA9894_WRITE(0x80, 0x0000); //POR=0x0003
		TFA9894_WRITE(0x81, 0x5799); //POR=0x561a
		TFA9894_WRITE(0x82, 0x0104); //POR=0x0044
	/* ----- generated code end   ----- */
	} else {
		return TFA98XX_ERROR_NOT_SUPPORTED;
	}

#undef TFA9894_WRITE
	return TFA98XX_ERROR_OK;
}

static enum tfa98xx_error
tfa9894_set_mute(struct tfa_device *tfa, int mute)
{
	int ret = tfa_set_bf(tfa, TFA9894_BF_CFSM, (uint16_t)mute);

	return ret < 0 ? -ret : TFA98XX_ERROR_OK;
}

static enum tfa98xx_error tfa9894_dsp_system_stable
(struct tfa_device *tfa, int *ready)
{
	int clocks;

	/* check CLKS: ready if set */
	clocks = tfa_get_bf(tfa, TFA9894_BF_CLKS);
	if (clocks < 0) {
		*ready = 0;
		return -clocks;
	}
	*ready = clocks == 1;

	return TFA98XX_ERROR_OK;
}

void tfa9894_ops(struct tfa_device_ops *ops)
{
	/* Set defaults for ops */
	ops->reg_read = tfa98xx_read_register16;
	ops->reg_write = tfa98xx_write_register16;
	ops->mem_read = tfa98xx_dsp_read_mem;
	ops->mem_write = tfa98xx_dsp_write_mem_word;
	ops->dsp_msg = tfa_dsp_msg;
	ops->dsp_msg_read = tfa_dsp_msg_read;
	ops->dsp_reset = tfa9894_dsp_reset;
	ops->dsp_write_tables = tfa9894_noop_arg;
	ops->auto_copy_mtp_to_iic = tfa9894_noop;
	ops->factory_trimmer = tfa9894_noop;
	ops->update_lpm = tfa9894_noop_arg;

	ops->tfa_init = tfa9894_specific;
	ops->dsp_system_stable = tfa9894_dsp_system_stable;
	ops->set_mute = tfa9894_set_mute;
	ops->faim_protect = tfa9894_faim_protect;
	ops->get_mtpb = tfa9894_get_mtpb;
	ops->set_swprof = tfa9894_set_swprofile;
	ops->get_swprof = tfa9894_get_swprofile;
	ops->set_swvstep = tfa9894_set_swvstep;
	ops->get_swvstep = tfa9894_get_swvstep;
	/* ops->auto_copy_mtp_to_iic = tfa9894_auto_copy_mtp_to_iic; */
	ops->set_osc_powerdown = tfa9894_set_osc_powerdown;
}
