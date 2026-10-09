/*
 * Copyright 2014-2017 NXP Semiconductors
 */

#define DEBUG

#include "dbgprint.h"
#include "tfa_container.h"
#include "tfa.h"
#include "tfa98xx_tfafieldnames.h"
#include "tfa_internal.h"

#define TFA_MK_BF(reg, pos, len) ((reg<<8)|(pos<<4)|(len-1))

#define FAM_TFA98XX_CF_CONTROLS (TFA_FAM(tfa,RST) >> 8)
#define FAM_TFA98XX_CF_MEM      (TFA_FAM(tfa,MEMA)>> 8)
#define FAM_TFA98XX_MTP0        (TFA_FAM(tfa,MTPOTC) >> 8)
#define FAM_TFA98xx_INT_EN      (TFA_FAM(tfa,INTENVDDS) >> 8)

#define CF_STATUS_I2C_CMD_ACK 0x01

#define TFA98XX_INTERRUPT_ENABLE_REG1		0x48
#define TFA98XX_INTERRUPT_IN_REG1		0x44
#define TFA98XX_INTERRUPT_OUT_REG1		0x40
#define TFA98XX_STATUS_POLARITY_REG1		0x4c
#define TFA98XX_KEY2_PROTECTED_MTP0_MTPEX_MSK	0x2
#define TFA98XX_KEY2_PROTECTED_MTP0_MTPOTC_MSK	0x1
#define TFA98XX_KEY2_PROTECTED_MTP0_MTPEX_POS	1
#define TFA98XX_KEY2_PROTECTED_MTP0_MTPOTC_POS	0

void tfa9894_ops(struct tfa_device_ops *ops);

#ifndef MIN
#define MIN(A, B) ((A < B) ? A : B)
#endif

#define CFSTABLE_TRIES		10
#define AMPOFFWAIT_TRIES	50
#define MTPBWAIT_TRIES		50
#define MTPEX_WAIT_NTRIES	50
#define ACS_RESET_WAIT_NTRIES 10

#define REDUCED_REGISTER_SETTING
#define WRITE_CALIBRATION_DATA_TO_MTP
#define CHECK_CALIBRATION_DATA_RANGE
#define SET_CALIBRATION_AT_ALL_DEVICE_READY
#undef SET_DUMMY_CALIBRATION_VALUE
#define TFA_RAMPDOWN_BEFORE_MUTE

#define TFA_MTPEX_POS           TFA98XX_KEY2_PROTECTED_MTP0_MTPEX_POS 

void tfa_set_query_info(struct tfa_device *tfa)
{
	/* invalidate device struct cached values */
	tfa->hw_feature_bits = -1;
	tfa->sw_feature_bits[0] = -1;
	tfa->sw_feature_bits[1] = -1;
	tfa->profile = -1;
	tfa->vstep = -1;
	/* defaults */
	tfa->spkr_select = 0;
	tfa->spkr_damaged = 0;
	tfa->support_tcoef = SUPPORT_YES;
	tfa->support_drc = SUPPORT_NOT_SET;
	tfa->support_saam = SUPPORT_NOT_SET;
	/* respond to external DSP: -1:none, 0:no_dsp, 1:cold, 2:warm */
	tfa->partial_enable = 0;
	tfa->sync_iv_delay = 0;

	tfa->is_cold = 1;
	tfa->is_bypass = 0;
	tfa->reset_mtpex = 0;
	tfa->stream_state = 0; /* not in use */
	tfa->prev_samstream = -1; /* not in use */
	tfa->first_after_boot = 1;
	tfa->temp = 0xffff;
	tfa->ampgain = -1;
	tfa->individual_msg = 0;

	/* TODO use the getfeatures() for retrieving the features [artf103523]
	 * tfa->support_drc = SUPPORT_NOT_SET;
	 */

	pr_info("%s: device type (0x%04x)\n", __func__, tfa->rev);
	tfa9894_ops(&tfa->dev_ops);
}

enum tfa98xx_dmem tfa98xx_filter_mem(struct tfa_device *tfa,
	int filter_index, unsigned short *address, int channel)
{
	/* TFA9894 containers use DSP messages rather than legacy filter addresses. */
	return -1;
}

enum tfa98xx_error tfa98xx_dsp_reset(struct tfa_device *tfa, int state)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;

	error = (tfa->dev_ops.dsp_reset)(tfa, state);

	return error;
}

enum tfa98xx_error tfa98xx_dsp_system_stable(struct tfa_device *tfa,
	int *ready)
{
	return (tfa->dev_ops.dsp_system_stable)(tfa, ready);
}

enum tfa98xx_error tfa98xx_auto_copy_mtp_to_iic(struct tfa_device *tfa)
{
	return (tfa->dev_ops.auto_copy_mtp_to_iic)(tfa);
}

enum tfa98xx_error tfa98xx_faim_protect(struct tfa_device *tfa, int state)
{
	return (tfa->dev_ops.faim_protect)(tfa, state);
}

enum tfa98xx_error tfa98xx_init(struct tfa_device *tfa)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	uint16_t value = 0;

	/* reset all i2C registers to default
	 * Write the register directly to avoid read in the bitfield function.
	 * The I2CR bit may overwrite full register because it is reset anyway.
	 * This will save a reg read transaction.
	 */
	TFA_SET_BF_VALUE(tfa, I2CR, 1, &value);
	TFA_WRITE_REG(tfa, I2CR, value);

	/* Put DSP in reset */
	tfa98xx_dsp_reset(tfa, 1); /* in pair of tfa_run_start_dsp() */

	/* some other registers must be set for optimal amplifier behaviour
	 * This is implemented in a file specific for the type number
	 */
	if (tfa->dev_ops.tfa_init)
		error = (tfa->dev_ops.tfa_init)(tfa);

	return error;
}

enum tfa98xx_error tfa98xx_dsp_write_tables(struct tfa_device *tfa,
	int sample_rate)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;

	error = (tfa->dev_ops.dsp_write_tables)(tfa, sample_rate);

	return error;
}

enum tfa98xx_error tfa98xx_set_osc_powerdown(struct tfa_device *tfa, int state)
{
	if (tfa->dev_ops.set_osc_powerdown)
		return tfa->dev_ops.set_osc_powerdown(tfa, state);

	return TFA98XX_ERROR_NOT_IMPLEMENTED;
}

enum tfa98xx_error tfa98xx_update_lpm(struct tfa_device *tfa, int state)
{
	if (tfa->dev_ops.update_lpm)
		return tfa->dev_ops.update_lpm(tfa, state);

	return TFA98XX_ERROR_NOT_IMPLEMENTED;
}

enum tfa98xx_error tfa98xx_get_mtp(struct tfa_device *tfa, uint16_t *value)
{
	int status;
	int result;

	/* not possible if PLL in powerdown */
	if (TFA_GET_BF(tfa, PWDN)) {
		pr_debug("PLL in powerdown\n");
		return TFA98XX_ERROR_NO_CLOCK;
	}

	tfa98xx_dsp_system_stable(tfa, &status);
	if (status == 0) {
		pr_debug("PLL not running\n");
		return TFA98XX_ERROR_NO_CLOCK;
	}

	result = TFA_READ_REG(tfa, MTP0);
	if (result < 0)
		return -result;
	*value = (uint16_t)result;

	return TFA98XX_ERROR_OK;
}

void tfa98xx_key2(struct tfa_device *tfa, int lock)
{
	/* unhide lock registers */
	reg_write(tfa, 0x0f, 0x5A6B);
	/* lock/unlock key2 MTPK */
	TFA_WRITE_REG(tfa, MTPKEY2, lock ? 0 : 0x5A);
	/* unhide lock registers */
	reg_write(tfa, 0x0f, 0);
}

void tfa2_manual_mtp_cpy(struct tfa_device *tfa,
	uint16_t reg_row_to_keep, uint16_t reg_row_to_set, uint8_t row)
///MCH_TO_TEST
{
	uint16_t value;
	int loop = 0;
	enum tfa98xx_error error;

	/* Assure FAIM is enabled (enable it when neccesery) */
	reg_read(tfa, (unsigned char)reg_row_to_keep, &value);
	if (!row)
	{
		reg_write(tfa, 0xA7, value);
		reg_write(tfa, 0xA8, reg_row_to_set);
	}
	else
	{
		reg_write(tfa, 0xA7, reg_row_to_set);
		reg_write(tfa, 0xA8, value);
	}
	reg_write(tfa, 0xA3, 0x10 | row);
}

enum tfa98xx_error tfa98xx_set_mtp(struct tfa_device *tfa,
	uint16_t value, uint16_t mask)
{
	unsigned short mtp_old, mtp_new;
	int loop, status;
	enum tfa98xx_error error;

	error = tfa98xx_get_mtp(tfa, &mtp_old);

	if (error != TFA98XX_ERROR_OK)
		return error;

	mtp_new = (value & mask) | (mtp_old & ~mask);

	if (mtp_old == mtp_new) /* no change */ {
		if (tfa->verbose)
			pr_info("No change in MTP. Value not written!\n");
		return TFA98XX_ERROR_OK;
	}
	error = tfa98xx_update_lpm(tfa, 1);
	if (error)
		return error;
	/* Assure FAIM is enabled (enable it when neccesery) */
	error = tfa98xx_faim_protect(tfa, 1);
	if (error)
		return error;
	if (tfa->verbose)
		pr_debug("MTP clock enabled.\n");

	/* assure that the clock is up, else we can't write MTP */
	error = tfa98xx_dsp_system_stable(tfa, &status);
	if (error)
		return error;
	if (status == 0)
		return TFA98XX_ERROR_NO_CLOCK;

	tfa98xx_key2(tfa, 0); /* unlock */
	TFA_WRITE_REG(tfa, MTP0, mtp_new); 	/* write to i2c shadow reg */
	/* CIMTP=1 start copying all the data from i2c regs_mtp to mtp*/
	tfa2_manual_mtp_cpy(tfa, 0xF1, mtp_new, 0);
	/* wait until MTP write is done */
	error = TFA98XX_ERROR_STATE_TIMED_OUT;
	for (loop = 0; loop < 100 /*x10ms*/; loop++) {
		msleep_interruptible(10); /* wait 10ms to avoid busload */
		if (tfa_dev_get_mtpb(tfa) == 0) {
			error = TFA98XX_ERROR_OK;
			break;
		}
	}
	tfa98xx_key2(tfa, 1); /* lock */
	/* MTP setting failed due to timeout ?*/
	if (error) {
		tfa98xx_faim_protect(tfa, 0);
		return error;
	}

	/* Disable the FAIM, if this is neccessary */
	error = tfa98xx_faim_protect(tfa, 0);
	if (error)
		return error;
	if (tfa->verbose)
		pr_debug("MTP clock disabled.\n");
	error = tfa98xx_update_lpm(tfa, 0);

	return error;
}

static short twos(short x)
{
	return (x < 0) ? x + 512 : x;
}

short tfa98xx_get_exttemp(struct tfa_device *tfa)
{
	short ext_temp = (short)TFA_GET_BF(tfa, EXTTS);

	return twos(ext_temp);
}

static enum tfa98xx_error
tfa9894_set_mute_dsp(struct tfa_device *tfa, enum tfa98xx_mute mute)
{
	enum tfa98xx_error error;
	int ret;

	if (tfa->dev_ops.set_mute == NULL)
		return TFA98XX_ERROR_NOT_SUPPORTED;

	switch (mute) {
	case TFA98XX_MUTE_OFF:
		error = tfa->dev_ops.set_mute(tfa, 0);
		if (error != TFA98XX_ERROR_OK)
			return error;
		ret = TFA_SET_BF(tfa, AMPE, 1);
		break;
	case TFA98XX_MUTE_AMPLIFIER:
	case TFA98XX_MUTE_DIGITAL:
		error = tfa->dev_ops.set_mute(tfa, 1);
		ret = TFA_SET_BF(tfa, AMPE, 0);
		break;
	default:
		return TFA98XX_ERROR_BAD_PARAMETER;
	}

	if (ret < 0)
		return -ret;
	return error;
}

enum tfa98xx_error
tfa98xx_set_mute(struct tfa_device *tfa, enum tfa98xx_mute mute)
{
	if (tfa->in_use == 0) {
		pr_err("device is not opened\n");
		return TFA98XX_ERROR_NOT_OPEN;
	}

	return tfa9894_set_mute_dsp(tfa, mute);
}

static enum tfa98xx_error
tfa98xx_process_patch_file(struct tfa_device *tfa, int length,
	const unsigned char *bytes)
{
	unsigned short size;
	int index = 0;
	enum tfa98xx_error error = TFA98XX_ERROR_OK;

	while (index < length) {
		if (length - index < 2)
			return TFA98XX_ERROR_BAD_PARAMETER;
		size = bytes[index] + bytes[index + 1] * 256;
		index += 2;
		if ((index + size) > length) {
			/* outside the buffer, error in the input data */
			return TFA98XX_ERROR_BAD_PARAMETER;
		}

		if (size > tfa->buffer_size) {
			/* too big, must fit buffer */
			return TFA98XX_ERROR_BAD_PARAMETER;
		}

		error = tfa98xx_write_raw(tfa, size, &bytes[index]);
		if (error != TFA98XX_ERROR_OK)
			break;
		index += size;
	}

	return error;
}

static enum tfa98xx_error
tfa98xx_check_ic_rom_version(struct tfa_device *tfa,
	const unsigned char patchheader[])
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	unsigned short checkrev, revid;
	unsigned char lsb_revid;
	unsigned short checkaddress;
	int checkvalue;
	int value = 0;
	int status;
	checkrev = patchheader[0];
	lsb_revid = tfa->rev & 0xff; /* only compare lower byte */

	if ((checkrev != 0xFF) && (checkrev != lsb_revid))
		return TFA98XX_ERROR_NOT_SUPPORTED;

	checkaddress = (patchheader[1] << 8) + patchheader[2];
	checkvalue =
		(patchheader[3] << 16) + (patchheader[4] << 8) + patchheader[5];
	if (checkaddress != 0xFFFF) {
		/* before reading XMEM, check if we can access the DSP */
		error = tfa98xx_dsp_system_stable(tfa, &status);
		if (error == TFA98XX_ERROR_OK) {
			if (!status)
				/* DSP subsys not running */
				error = TFA98XX_ERROR_DSP_NOT_RUNNING;
		}
		/* read register to check the correct ROM version */
		if (error == TFA98XX_ERROR_OK)
			error = mem_read(tfa, checkaddress, 1, &value);
		if (error == TFA98XX_ERROR_OK) {
			if (value != checkvalue) {
				pr_err("patch file romid type check failed [0x%04x]: expected 0x%02x, actual 0x%02x\n",
					checkaddress, value, checkvalue);
				error = TFA98XX_ERROR_NOT_SUPPORTED;
			}
		}
	} else { /* == 0xffff */
		/* check if the revid subtype is in there */
		if (checkvalue != 0xFFFFFF && checkvalue != 0) {
			revid = patchheader[5] << 8 | patchheader[0];
			/* full revid */
			if (revid != tfa->rev) {
				pr_err("patch file device type check failed: expected 0x%02x, actual 0x%02x\n",
					tfa->rev, revid);
				return TFA98XX_ERROR_NOT_SUPPORTED;
			}
		}
	}

	return error;
}

#define PATCH_HEADER_LENGTH 6
enum tfa98xx_error
tfa_dsp_patch(struct tfa_device *tfa, int patch_length,
	const unsigned char *patch_bytes)
{
	enum tfa98xx_error error;
	int status;

	if (tfa->in_use == 0)
		return TFA98XX_ERROR_NOT_OPEN;

	if (patch_length < PATCH_HEADER_LENGTH)
		return TFA98XX_ERROR_BAD_PARAMETER;

	error = tfa98xx_check_ic_rom_version(tfa, patch_bytes);
	if (TFA98XX_ERROR_OK != error)
		return error;

	tfa98xx_dsp_system_stable(tfa, &status);
	if (!status)
		/* Only test when we have a clock. */
		return TFA98XX_ERROR_NO_CLOCK;
	/******MCH_TO_TEST*********/
	if (error == TFA98XX_ERROR_OK) {
		error = tfa_run_coldboot(tfa, 1);
		if (error)
			return TFA98XX_ERROR_DSP_NOT_RUNNING;
	}
	/**************************/
	error = tfa98xx_process_patch_file(tfa,
		patch_length - PATCH_HEADER_LENGTH,
		patch_bytes + PATCH_HEADER_LENGTH);

	return error;
}

enum tfa98xx_error
tfa98xx_wait_result(struct tfa_device *tfa, int wait_retry_count)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	int cf_status; /* the contents of the CF_STATUS register */
	int tries = 0;

	do {
		/* i2c_cmd_ack */
		cf_status = TFA_GET_BF(tfa, ACKCMD);
		if (cf_status < 0)
			error = -cf_status;
		tries++;
		/* don't wait forever, DSP is pretty quick to respond (< 1ms) */
	} while ((error == TFA98XX_ERROR_OK)
		&& ((cf_status & CF_STATUS_I2C_CMD_ACK) == 0)
		&& (tries < wait_retry_count));

	if (tries >= wait_retry_count)
		/* something wrong with communication with DSP */
		error = TFA98XX_ERROR_DSP_NOT_RUNNING;

	return error;
}

void tfa98xx_convert_bytes2data(int num_bytes,
	const unsigned char bytes[], int data[])
{
	int i;			/* index for data */
	int k;			/* index for bytes */
	int d;
	int num_data = num_bytes / 3;

	_ASSERT((num_bytes % 3) == 0);
	for (i = 0, k = 0; i < num_data; ++i, k += 3) {
		d = (bytes[k] << 16) | (bytes[k + 1] << 8) | (bytes[k + 2]);
		_ASSERT(d >= 0);
		_ASSERT(d < (1 << 24));	/* max 24 bits in use */
		if (bytes[k] & 0x80)	/* sign bit was set */
			d = -((1 << 24) - d);

		data[i] = d;
	}
}

void tfa98xx_convert_data2bytes(int num_data, const int data[],
	unsigned char bytes[])
{
	int i; /* index for data */
	int k; /* index for bytes */
	int d;

	/* note: cannot just take the lowest 3 bytes from the 32 bit
	 * integer, because also need to take care of clipping any
	 * value > 2&23
	 */
	for (i = 0, k = 0; i < num_data; ++i, k += 3) {
		if (data[i] >= 0)
			d = MIN(data[i], (1 << 23) - 1);
		else {
			/* 2's complement */
			d = (1 << 24) - MIN(-data[i], 1 << 23);
		}
		_ASSERT(d >= 0);
		_ASSERT(d < (1 << 24));	/* max 24 bits in use */
		bytes[k] = (d >> 16) & 0xFF;	/* MSB */
		bytes[k + 1] = (d >> 8) & 0xFF;
		bytes[k + 2] = (d) & 0xFF;	/* LSB */
	}
}

enum tfa98xx_error tfa_dsp_msg_write(struct tfa_device *tfa,
	int length, const char *buffer)
{
	int offset = 0;
	int chunk_size = ROUND_DOWN(tfa->buffer_size, 3);
	/* XMEM word size */
	int remaining_bytes = length;
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	uint16_t cfctl;
	int value;

	value = TFA_READ_REG(tfa, DMEM);
	if (value < 0) {
		error = -value;
		return error;
	}
	cfctl = (uint16_t)value;
	/* assume no I2C errors from here */

	TFA_SET_BF_VALUE(tfa, DMEM, (uint16_t)TFA98XX_DMEM_XMEM, &cfctl);
	/* set cf ctl to DMEM  */
	TFA_SET_BF_VALUE(tfa, AIF, 0, &cfctl); /* set to autoincrement */
	TFA_WRITE_REG(tfa, DMEM, cfctl);

	/* xmem[1] is start of message
	 * direct write to register to save cycles avoiding read-modify-write
	 */
	TFA_WRITE_REG(tfa, MADD, 1);

	/* due to autoincrement in cf_ctrl, next write will happen at
	 * the next address
	 */
	while ((error == TFA98XX_ERROR_OK) && (remaining_bytes > 0)) {
		if (remaining_bytes < chunk_size)
			chunk_size = remaining_bytes;
		/* else chunk_size remains at initialize value above */
		error = tfa98xx_write_data(tfa, FAM_TFA98XX_CF_MEM,
			chunk_size, (const unsigned char *)buffer + offset);
		remaining_bytes -= chunk_size;
		offset += chunk_size;
	}

	/* notify the DSP */
	if (error == TFA98XX_ERROR_OK) {
		/* cf_int=0, cf_aif=0, cf_dmem=XMEM=01, cf_rst_dsp=0 */
		/* set the cf_req1 and cf_int bit */
		TFA_SET_BF_VALUE(tfa, REQCMD, 0x01, &cfctl); /* bit 0 */
		TFA_SET_BF_VALUE(tfa, CFINT, 1, &cfctl);
		error = -TFA_WRITE_REG(tfa, CFINT, cfctl);
	}

	return error;
}

enum tfa98xx_error tfa_dsp_msg_status(struct tfa_device *tfa, int *p_rpc_status)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;

	error = tfa98xx_wait_result(tfa, 2); /* 2 is only one try */
	if (error == TFA98XX_ERROR_DSP_NOT_RUNNING) {
		*p_rpc_status = -1;
		return TFA98XX_ERROR_OK;
	} else if (error != TFA98XX_ERROR_OK) {
		return error;
	}

	error = tfa98xx_check_rpc_status(tfa, p_rpc_status);

	return error;
}

const char *tfa98xx_get_i2c_status_id_string(int status)
{
	const char *p_id_str;

	switch (status) {
	case TFA98XX_DSP_NOT_RUNNING:
		p_id_str = "No response from DSP";
		break;
	case TFA98XX_I2C_REQ_DONE:
		p_id_str = "Ok";
		break;
	case TFA98XX_I2C_REQ_BUSY:
		p_id_str = "Request is being processed";
		break;
	case TFA98XX_I2C_REQ_INVALID_M_ID:
		p_id_str =
			"Provided M-ID does not fit in valid range [0..2]";
		break;
	case TFA98XX_I2C_REQ_INVALID_P_ID:
		p_id_str =
			"Provided P-ID is not valid in the given M-ID context";
		break;
	case TFA98XX_I2C_REQ_INVALID_CC:
		p_id_str =
			"Invalid channel configuration bits (SC|DS|DP|DC) combination";
		break;
	case TFA98XX_I2C_REQ_INVALID_SEQ:
		p_id_str =
			"Invalid sequence of commands, in case DSP expects some commands in a specific order";
		break;
	case TFA98XX_I2C_REQ_INVALID_PARAM:
		p_id_str = "Generic error, invalid parameter";
		break;
	case TFA98XX_I2C_REQ_BUFFER_OVERFLOW:
		p_id_str =
			"I2C buffer overflowed: host sent too many parameters, memory integrity is not guaranteed";
		break;
	case TFA98XX_I2C_REQ_CALIB_BUSY:
		p_id_str = "Calibration not completed";
		break;
	case TFA98XX_I2C_REQ_CALIB_FAILED:
		p_id_str = "Calibration failed";
		break;
	default:
		p_id_str = "Unspecified error";
	}

	return p_id_str;
}

enum tfa98xx_error tfa_dsp_msg_read(struct tfa_device *tfa,
	int length, unsigned char *bytes)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	int burst_size;		/* number of words per burst size */
	int bytes_per_word = 3;
	int num_bytes;
	int offset = 0;
	unsigned short start_offset = 2; /* msg starts @xmem[2], [1]=cmd */

	if (length > TFA2_MAX_PARAM_SIZE)
		return TFA98XX_ERROR_BAD_PARAMETER;

	TFA_SET_BF(tfa, DMEM, (uint16_t)TFA98XX_DMEM_XMEM);
	error = -TFA_WRITE_REG(tfa, MADD, start_offset);
	if (error != TFA98XX_ERROR_OK)
		return error;

	num_bytes = length; /* input param */
	while (num_bytes > 0) {
		burst_size = ROUND_DOWN(tfa->buffer_size, bytes_per_word);
		if (num_bytes < burst_size)
			burst_size = num_bytes;
		error = tfa98xx_read_data(tfa, FAM_TFA98XX_CF_MEM,
			burst_size, bytes + offset);
		if (error != TFA98XX_ERROR_OK)
			return error;

		num_bytes -= burst_size;
		offset += burst_size;
	}

	return error;
}

enum tfa98xx_error dsp_msg(struct tfa_device *tfa,
	int length24, const char *buf24)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	static int lastmessage;
	uint8_t *blob = NULL;
	int i;
	int *intbuf = NULL;
	char *buf = (char *)buf24;
	int length = length24;

	if ((tfa->stream_state & BIT_PSTREAM) == 0) {
		pr_info("%s: skip if PSTREAM is lost\n", __func__);
		return error;
	}

	if (tfa->verbose == 8) {
		pr_info("%s: skip dsp_msg(algorithm bypass) if verbose is 8\n",
			__func__);
		return error;
	}

	/* Only create multi-msg when the dsp is cold */
	{
		if ((tfa->stream_state & BIT_PSTREAM) == 1) {
			error = (tfa->dev_ops.dsp_msg)
					(tfa, length, buf);
		} else {
			pr_info("%s: skip if PSTREAM is lost\n", __func__);
		}
	}

	if (error != TFA98XX_ERROR_OK)
		/* Get actual error code from softDSP */
		error = (enum tfa98xx_error)
			(error + TFA98XX_ERROR_BUFFER_RPC_BASE);

	/* DSP verbose has argument 0x04 */
	if ((tfa->verbose & 0x04) != 0) {
		pr_debug("DSP W [%d]: ", length);
		// for (i = 0; i < ((length > 3) ? 3 : length); i++)
		// 	pr_debug("0x%02x ", (uint8_t)buf[i]);
		// pr_debug("\n");
	}

	return error;
}

enum tfa98xx_error dsp_msg_read(struct tfa_device *tfa,
	int length24, unsigned char *bytes24)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	int i;
	int length = length24;
	unsigned char *bytes = bytes24;

	if ((tfa->stream_state & BIT_PSTREAM) == 1) {
		error = (tfa->dev_ops.dsp_msg_read)(tfa, length, bytes);
		if (error == TFA98XX_ERROR_OK)
			pr_debug("%s: OK\n", __func__);
	} else {
		pr_info("%s: skip if PSTREAM is lost\n", __func__);
	}

	if (error != TFA98XX_ERROR_OK)
		/* Get actual error code from softDSP */
		error = (enum tfa98xx_error)
			(error + TFA98XX_ERROR_BUFFER_RPC_BASE);

	/* DSP verbose has argument 0x04 */
	if ((tfa->verbose & 0x04) != 0) {
		pr_debug("DSP R [%d]: ", length);
		// for (i = 0; i < ((length > 3) ? 3 : length); i++)
		// 	pr_debug("0x%02x ", (uint8_t)bytes[i]);
		// pr_debug("\n");
	}

	return error;
}

enum tfa98xx_error reg_read(struct tfa_device *tfa,
	unsigned char subaddress, unsigned short *value)
{
	enum tfa98xx_error error;

	error = (tfa->dev_ops.reg_read)(tfa, subaddress, value);
	if (error != TFA98XX_ERROR_OK)
		/* Get actual error code from softDSP */
		error = (enum tfa98xx_error)
			(error + TFA98XX_ERROR_BUFFER_RPC_BASE);

	return error;
}

enum tfa98xx_error reg_write(struct tfa_device *tfa,
	unsigned char subaddress, unsigned short value)
{
	enum tfa98xx_error error;

	error = (tfa->dev_ops.reg_write)(tfa, subaddress, value);
	if (error != TFA98XX_ERROR_OK)
		/* Get actual error code from softDSP */
		error = (enum tfa98xx_error)
			(error + TFA98XX_ERROR_BUFFER_RPC_BASE);

	return error;
}

enum tfa98xx_error mem_read(struct tfa_device *tfa,
	unsigned int start_offset, int num_words, int *p_values)
{
	enum tfa98xx_error error;

	error = (tfa->dev_ops.mem_read)(tfa, start_offset, num_words, p_values);
	if (error != TFA98XX_ERROR_OK)
		/* Get actual error code from softDSP */
		error = (enum tfa98xx_error)
			(error + TFA98XX_ERROR_BUFFER_RPC_BASE);

	return error;
}

enum tfa98xx_error mem_write(struct tfa_device *tfa,
	unsigned short address, int value, int memtype)
{
	enum tfa98xx_error error;

	error = (tfa->dev_ops.mem_write)(tfa, address, value, memtype);
	if (error != TFA98XX_ERROR_OK)
		/* Get actual error code from softDSP */
		error = (enum tfa98xx_error)
			(error + TFA98XX_ERROR_BUFFER_RPC_BASE);

	return error;
}

#define MAX_WORDS (300)
enum tfa98xx_error tfa_dsp_msg(struct tfa_device *tfa,
	int length, const char *buf)
{
	enum tfa98xx_error error;
	int tries, rpc_status = TFA98XX_I2C_REQ_DONE;

	/* write the message and notify the DSP */
	error = tfa_dsp_msg_write(tfa, length, buf);
	if (error != TFA98XX_ERROR_OK)
		return error;

	/* get the result from the DSP (polling) */
	for (tries = TFA98XX_WAITRESULT_NTRIES; tries > 0; tries--) {
		error = tfa_dsp_msg_status(tfa, &rpc_status);
		if (error == TFA98XX_ERROR_OK
			&& rpc_status == TFA98XX_I2C_REQ_DONE)
			break;
		/* If the rpc status is a specific error we want to know it.
		 * If it is busy or not running it should retry
		 */
		if (rpc_status != TFA98XX_I2C_REQ_BUSY
			&& rpc_status != TFA98XX_DSP_NOT_RUNNING)
			break;
	}

	if (rpc_status != TFA98XX_I2C_REQ_DONE) {
		/* DSP RPC call returned an error */
		error = (enum tfa98xx_error)
			(rpc_status + TFA98XX_ERROR_BUFFER_RPC_BASE);
		pr_debug("DSP msg status: %d (%s)\n", rpc_status,
			tfa98xx_get_i2c_status_id_string(rpc_status));
	}
	return error;
}

enum tfa98xx_error
tfa98xx_check_rpc_status(struct tfa_device *tfa, int *p_rpc_status)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	/* the value to sent to the * CF_CONTROLS register: cf_req=00000000,
	 * cf_int=0, cf_aif=0, cf_dmem=XMEM=01, cf_rst_dsp=0 */
	unsigned short cf_ctrl = 0x0002;
	/* memory address to be accessed (0: Status, 1: ID, 2: parameters) */
	unsigned short cf_mad = 0x0000;

	if (tfa->in_use == 0)
		return TFA98XX_ERROR_NOT_OPEN;
	if (p_rpc_status == NULL)
		return TFA98XX_ERROR_BAD_PARAMETER;

	/* 1) write DMEM=XMEM to the DSP XMEM */
	{
		/* minimize the number of I2C transactions
		 * by making use of the autoincrement in I2C
		 */
		unsigned char buffer[4];
		/* first the data for CF_CONTROLS */
		buffer[0] = (unsigned char)((cf_ctrl >> 8) & 0xFF);
		buffer[1] = (unsigned char)(cf_ctrl & 0xFF);
		/* write the contents of CF_MAD which is the subaddress
		 * following CF_CONTROLS
		 */
		buffer[2] = (unsigned char)((cf_mad >> 8) & 0xFF);
		buffer[3] = (unsigned char)(cf_mad & 0xFF);
		error = tfa98xx_write_data(tfa,
			FAM_TFA98XX_CF_CONTROLS, sizeof(buffer), buffer);
	}

	if (error == TFA98XX_ERROR_OK)
		/* read 1 word (24 bit) from XMEM */
		error = tfa98xx_dsp_read_mem(tfa, 0, 1, p_rpc_status);

	return error;
}

enum tfa98xx_error
tfa98xx_dsp_read_mem(struct tfa_device *tfa,
	unsigned int start_offset, int num_words, int *p_values)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	unsigned char *bytes;
	int burst_size;		/* number of words per burst size */
	const int bytes_per_word = 3;
	int dmem;
	int num_bytes;
	int *p;

	bytes = (unsigned char *)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (bytes == NULL)
		return TFA98XX_ERROR_FAIL;

	/* If no offset is given, assume XMEM! */
	if (((start_offset >> 16) & 0xf) > 0)
		dmem = (start_offset >> 16) & 0xf;
	else
		dmem = TFA98XX_DMEM_XMEM;

	/* Remove offset from address */
	start_offset = start_offset & 0xffff;
	num_bytes = num_words * bytes_per_word;
	p = p_values;

	TFA_SET_BF(tfa, DMEM, (uint16_t)dmem);
	error = -TFA_WRITE_REG(tfa, MADD, (unsigned short)start_offset);
	if (error != TFA98XX_ERROR_OK)
		goto tfa98xx_dsp_read_mem_exit;

	for (; num_bytes > 0;) {
		burst_size = ROUND_DOWN(tfa->buffer_size, bytes_per_word);
		if (num_bytes < burst_size)
			burst_size = num_bytes;

		_ASSERT(burst_size <= sizeof(bytes));
		error = tfa98xx_read_data(tfa, FAM_TFA98XX_CF_MEM,
			burst_size, bytes);
		if (error != TFA98XX_ERROR_OK)
			goto tfa98xx_dsp_read_mem_exit;

		tfa98xx_convert_bytes2data(burst_size, bytes, p);

		num_bytes -= burst_size;
		p += burst_size / bytes_per_word;
	}

tfa98xx_dsp_read_mem_exit:
	kfree(bytes);

	return error;
}

enum tfa98xx_error
tfa98xx_dsp_write_mem_word(struct tfa_device *tfa,
	unsigned short address, int value, int memtype)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	unsigned char bytes[3];

	TFA_SET_BF(tfa, DMEM, (uint16_t)memtype);

	error = -TFA_WRITE_REG(tfa, MADD, address);
	if (error != TFA98XX_ERROR_OK)
		return error;

	tfa98xx_convert_data2bytes(1, &value, bytes);
	error = tfa98xx_write_data(tfa, FAM_TFA98XX_CF_MEM, 3, bytes);

	return error;
}

enum tfa98xx_error
tfa_cont_write_filterbank(struct tfa_device *tfa, struct tfa_filter *filter)
{
	unsigned char biquad_index;
	enum tfa98xx_error error = TFA98XX_ERROR_OK;

	for (biquad_index = 0; biquad_index < 10; biquad_index++) {
		if (filter[biquad_index].enabled)
			error = tfa_dsp_cmd_id_write(tfa,
				MODULE_BIQUADFILTERBANK,
				biquad_index + 1, /* start @1 */
				sizeof(filter[biquad_index].biquad.bytes),
				filter[biquad_index].biquad.bytes);
		else
			error = tfa98xx_dsp_biquad_disable(tfa,
				biquad_index + 1);
		if (error)
			return error;
	}

	return error;
}

enum tfa98xx_error
tfa98xx_dsp_biquad_disable(struct tfa_device *tfa, int biquad_index)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	int coeff_buffer[BIQUAD_COEFF_SIZE];
	unsigned char bytes[3 + BIQUAD_COEFF_SIZE * 3];
	int nr = 0;

	if (biquad_index > TFA98XX_BIQUAD_NUM)
		return TFA98XX_ERROR_BAD_PARAMETER;
	if (biquad_index < 1)
		return TFA98XX_ERROR_BAD_PARAMETER;

	/* make opcode */
	bytes[nr++] = 0;
	bytes[nr++] = MODULE_BIQUADFILTERBANK + 128;
	bytes[nr++] = (unsigned char)biquad_index;

	/* set in correct order and format for the DSP */
	coeff_buffer[0] = (int)-8388608;	/* -1.0f */
	coeff_buffer[1] = 0;
	coeff_buffer[2] = 0;
	coeff_buffer[3] = 0;
	coeff_buffer[4] = 0;
	coeff_buffer[5] = 0;

	/* convert to packed 24 */
	tfa98xx_convert_data2bytes(BIQUAD_COEFF_SIZE, coeff_buffer, &bytes[nr]);
	nr += BIQUAD_COEFF_SIZE * 3;

	error = dsp_msg(tfa, nr, (char *)bytes);

	return error;
}

enum tfa98xx_error tfa_dsp_cmd_id_write(struct tfa_device *tfa,
	unsigned char module_id,
	unsigned char param_id, int num_bytes,
	const unsigned char data[])
{
	enum tfa98xx_error error;
	unsigned char *buffer;
	int nr = 0;

	buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (buffer == NULL)
		return TFA98XX_ERROR_FAIL;

	buffer[nr++] = tfa->spkr_select;
	buffer[nr++] = module_id + 128;
	buffer[nr++] = param_id;

	memcpy(&buffer[nr], data, num_bytes);
	nr += num_bytes;

	error = dsp_msg(tfa, nr, (char *)buffer);

	kfree(buffer);

	return error;
}

enum tfa98xx_error tfa_dsp_cmd_id_write_read(struct tfa_device *tfa,
	unsigned char module_id,
	unsigned char param_id, int num_bytes,
	unsigned char data[])
{
	enum tfa98xx_error error;
	unsigned char buffer[3];
	int nr = 0;

	if (num_bytes <= 0) {
		pr_debug("Error: The number of READ bytes is smaller or equal to 0!\n");
		return TFA98XX_ERROR_FAIL;
	}

	{
		buffer[nr++] = tfa->spkr_select; /* CC: 0 (reset all) for stereo */
		pr_debug("%s: CC bit: %d\n", __func__, buffer[nr - 1]);
	}

	buffer[nr++] = module_id + 128;
	buffer[nr++] = param_id;

	error = dsp_msg(tfa, nr, (char *)buffer);
	if (error != TFA98XX_ERROR_OK)
		return error;

	/* read the data from the dsp */
	error = dsp_msg_read(tfa, num_bytes, data);
	return error;
}

enum tfa98xx_error
tfa98xx_dsp_write_preset(struct tfa_device *tfa, int length,
	const unsigned char *p_preset_bytes)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	if (p_preset_bytes != NULL)
		/* by design: keep the data opaque and no
		 * interpreting/calculation
		 */
		error = tfa_dsp_cmd_id_write(tfa, MODULE_SPEAKERBOOST,
			SB_PARAM_SET_PRESET, length,
			p_preset_bytes);
	else
		error = TFA98XX_ERROR_BAD_PARAMETER;

	return error;
}

enum tfa98xx_error
tfa98xx_dsp_get_sw_feature_bits(struct tfa_device *tfa, int features[2])
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	const int byte_size = 2 * 3;
	unsigned char bytes[2 * 3];

	/* return the cache data if it's valid */
	if (tfa->sw_feature_bits[0] != -1) {
		features[0] = tfa->sw_feature_bits[0];
		features[1] = tfa->sw_feature_bits[1];
	} else {
		/* for tfa1 check if we have clock */

		error = tfa_dsp_cmd_id_write_read(tfa, MODULE_FRAMEWORK,
			FW_PAR_ID_GET_FEATURE_INFO, byte_size, bytes);

		if (error != TFA98XX_ERROR_OK)
			/* old ROM code may respond
			 * with TFA98XX_ERROR_RPC_PARAM_ID
			 */
			return error;

		tfa98xx_convert_bytes2data(byte_size, bytes, features);
	}

	return error;
}

enum tfa98xx_error
tfa98xx_dsp_support_drc(struct tfa_device *tfa, int *pb_support_drc)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;

	*pb_support_drc = 0;

	if (tfa->in_use == 0)
		return TFA98XX_ERROR_NOT_OPEN;

	if (tfa->support_drc != SUPPORT_NOT_SET) {
		*pb_support_drc = (tfa->support_drc == SUPPORT_YES);
	} else {
		int feature_bits[2];

		error = tfa98xx_dsp_get_sw_feature_bits(tfa, feature_bits);
		if (error == TFA98XX_ERROR_OK) {
			/* easy case: new API available */
			/* bit=0 means DRC enabled */
			*pb_support_drc
				= (feature_bits[0] & FEATURE1_DRC) == 0;
		} else if (error == TFA98XX_ERROR_RPC_PARAM_ID) {
			/* older ROM code, doesn't support it */
			*pb_support_drc = 0;
			error = TFA98XX_ERROR_OK;
		}
		/* else some other error, return transparently */
		/* pb_support_drc only changed when error == TFA98XX_ERROR_OK */

		if (error == TFA98XX_ERROR_OK)
			tfa->support_drc = *pb_support_drc
				? SUPPORT_YES : SUPPORT_NO;
	}

	return error;
}

enum tfa98xx_error
tfa98xx_dsp_write_config(struct tfa_device *tfa, int length,
	const unsigned char *p_config_bytes)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	int b_support_drc;

	error = tfa_dsp_cmd_id_write(tfa,
		MODULE_SPEAKERBOOST,
		SB_PARAM_SET_CONFIG, length,
		p_config_bytes);
	if (error != TFA98XX_ERROR_OK)
		return error;

	error = tfa98xx_dsp_support_drc(tfa, &b_support_drc);
	if (error != TFA98XX_ERROR_OK)
		return error;

	if (b_support_drc) {
		/* Need to set AgcGainInsert back to PRE,
		 * as the SetConfig forces it to POST
		 */
		uint8_t bytes[3] = {0, 0, 0};

		error = tfa_dsp_cmd_id_write(tfa,
			MODULE_SPEAKERBOOST,
			SB_PARAM_SET_AGCINS,
			3,
			bytes);
	}

	return error;
}

enum tfa98xx_error tfa98xx_dsp_write_drc(struct tfa_device *tfa,
	int length, const unsigned char *p_drc_bytes)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	if (p_drc_bytes != NULL)
		error = tfa_dsp_cmd_id_write(tfa,
			MODULE_SPEAKERBOOST,
			SB_PARAM_SET_DRC, length,
			p_drc_bytes);
	else
		error = TFA98XX_ERROR_BAD_PARAMETER;

	return error;
}

enum tfa98xx_error tfa98xx_powerdown(struct tfa_device *tfa, int powerdown)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;

	if (tfa->in_use == 0)
		return TFA98XX_ERROR_NOT_OPEN;

	error = TFA_SET_BF(tfa, PWDN, (uint16_t)powerdown);

	if (powerdown) {
		/* Workaround for ticket PLMA5337 */
		TFA_SET_BF_VOLATILE(tfa, AMPE, 0);
	}

	return error;
}

enum tfa98xx_error
tfa98xx_select_mode(struct tfa_device *tfa, enum tfa98xx_mode mode)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;

	if (tfa->in_use == 0)
		return TFA98XX_ERROR_NOT_OPEN;

	if (error == TFA98XX_ERROR_OK) {
		switch (mode) {
		default:
			error = TFA98XX_ERROR_BAD_PARAMETER;
		}
	}

	return error;
}

int tfa_set_bf(struct tfa_device *tfa,
	const uint16_t bf, const uint16_t value)
{
	enum tfa98xx_error err;
	uint16_t regvalue, msk, oldvalue;

	/*
	 * bitfield enum:
	 * - 0..3  : len
	 * - 4..7  : pos
	 * - 8..15 : address
	 */
	uint8_t len = bf & 0x0f;
	uint8_t pos = (bf >> 4) & 0x0f;
	uint8_t address = (bf >> 8) & 0xff;

	err = reg_read(tfa, address, &regvalue);
	if (err) {
		pr_err("Error getting bf :%d\n", -err);
		return -err;
	}

	oldvalue = regvalue;
	msk = ((1 << (len + 1)) - 1) << pos;
	regvalue &= ~msk;
	regvalue |= value << pos;

	/* Only write when the current register value is
	 * not the same as the new value
	 */
	if (oldvalue != regvalue) {
		err = reg_write(tfa, address, regvalue);
		if (err) {
			pr_err("Error setting bf :%d\n", -err);
			return -err;
		}
	}

	return 0;
}

int tfa_set_bf_volatile(struct tfa_device *tfa,
	const uint16_t bf, const uint16_t value)
{
	enum tfa98xx_error err;
	uint16_t regvalue, msk;

	/*
	 * bitfield enum:
	 * - 0..3  : len
	 * - 4..7  : pos
	 * - 8..15 : address
	 */
	uint8_t len = bf & 0x0f;
	uint8_t pos = (bf >> 4) & 0x0f;
	uint8_t address = (bf >> 8) & 0xff;

	err = reg_read(tfa, address, &regvalue);
	if (err) {
		pr_err("Error getting bf :%d\n", -err);
		return -err;
	}

	msk = ((1 << (len + 1)) - 1) << pos;
	regvalue &= ~msk;
	regvalue |= value << pos;

	err = reg_write(tfa, address, regvalue);
	if (err) {
		pr_err("Error setting bf :%d\n", -err);
		return -err;
	}

	return 0;
}

int tfa_get_bf(struct tfa_device *tfa, const uint16_t bf)
{
	enum tfa98xx_error err;
	uint16_t regvalue, msk;
	uint16_t value;

	/*
	 * bitfield enum:
	 * - 0..3  : len
	 * - 4..7  : pos
	 * - 8..15 : address
	 */
	uint8_t len = bf & 0x0f;
	uint8_t pos = (bf >> 4) & 0x0f;
	uint8_t address = (bf >> 8) & 0xff;

	err = reg_read(tfa, address, &regvalue);
	if (err) {
		pr_err("Error getting bf :%d\n", -err);
		return -err;
	}

	msk = ((1 << (len + 1)) - 1) << pos;
	regvalue &= msk;
	value = regvalue >> pos;

	return value;
}

enum tfa98xx_error tfa_cf_powerup(struct tfa_device *tfa)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	int tries, status;

	/* power on the sub system */
	TFA_SET_BF_VOLATILE(tfa, PWDN, 0);

	// wait until everything is stable, in case clock has been off
	if (tfa->verbose)
		pr_info("Waiting for DSP system stable...\n");

	for (tries = CFSTABLE_TRIES; tries > 0; tries--) {
		err = tfa98xx_dsp_system_stable(tfa, &status);
		_ASSERT(err == TFA98XX_ERROR_OK);
		if (status)
			break;
		else
			msleep_interruptible(10); /* wait 10ms to avoid busload */
	}

	if (tries == 0) { // timedout
		pr_err("DSP subsystem start timed out\n");
		return TFA98XX_ERROR_STATE_TIMED_OUT;
	}

	return err;
}

int is_94_N2_device(struct tfa_device *tfa)
{
	return ((((tfa->rev) & 0xff) == 0x94)
		&& (((tfa->rev >> 8) & 0xff) > 0x1a));
}

enum tfa98xx_error show_current_state(struct tfa_device *tfa)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	int manstate = -1;

	if (2 == 2 && tfa->verbose) {
		if (is_94_N2_device(tfa))
			manstate = tfa_get_bf(tfa, TFA9894N2_BF_MANSTATE);
		else
			manstate = TFA_GET_BF(tfa, MANSTATE);
		if (manstate < 0)
			return -manstate;

		pr_debug("tfa (dev %d): current HW manager state: %d\n",
			tfa->dev_idx, manstate);

		switch (manstate) {
		case 0:
			pr_debug("power_down_state\n");
			break;
		case 1:
			pr_debug("wait_for_source_settings_state\n");
			break;
		case 2:
			pr_debug("connnect_pll_input_state\n");
			break;
		case 3:
			pr_debug("disconnect_pll_input_state\n");
			break;
		case 4:
			pr_debug("enable_pll_state\n");
			break;
		case 5:
			pr_debug("enable_cgu_state\n");
			break;
		case 6:
			pr_debug("init_cf_state\n");
			break;
		case 7:
			pr_debug("enable_amplifier_state\n");
			break;
		case 8:
			pr_debug("alarm_state\n");
			break;
		case 9:
			pr_debug("operating_state\n");
			break;
		case 10:
			pr_debug("mute_audio_state\n");
			break;
		case 11:
			pr_debug("disable_cgu_pll_state\n");
			break;
		default:
			pr_debug("Unable to find current state\n");
			break;
		}
	}

	return err;
}

enum tfa98xx_error tfa_get_fw_api_version(struct tfa_device *tfa,
	unsigned char *pfw_version)
{
	enum tfa98xx_error err = 0;
	int i;

	if (tfa == NULL)
		return TFA98XX_ERROR_BAD_PARAMETER;

	{
		err = mem_read(tfa, FW_VAR_API_VERSION,
			1, (int *)pfw_version);
		if (err) {
			pr_debug("%s Error: Unable to get API Version from DSP\n",
				__func__);
			return err;
		}
	}

	return err;
}

enum tfa98xx_error tfa_run_speaker_boost(struct tfa_device *tfa,
	int force, int profile, int vstep)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	int value;

	if (force) {
		tfa->is_cold = 1;
		err = tfa_run_coldstartup(tfa, profile);
		if (err)
			return err;
	}

	/* Returns 1 when device is "cold" and 0 when device is warm */
	value = tfa_is_cold(tfa);
	if (value < 0)
		err = value;
	else
		tfa->is_cold = value;

	pr_info("%s: %s boot, ext_dsp = %d, profile = %d\n",
		__func__, value ? "cold" : "warm",
		(-1), profile);

	pr_debug("Startup of device [%s] is a %sstart\n",
		tfa_cont_device_name(tfa->cnt, tfa->dev_idx),
		value ? "cold" : "warm");

	/* cold start and not tap profile */
	if (value) {
		/* CHECK: only once before buffering */
		{
			/* flush message buffer */
			pr_debug("%s: flush buffer in blob, in cold start\n",
				__func__);
			err = TFA98XX_ERROR_OK;
		}

		/* Run startup and write all files */
		pr_info("%s: cold start, speaker startup\n", __func__);
		err = tfa_run_speaker_startup(tfa, force, profile, vstep);
		if (err) {
			pr_info("%s: speaker startup error = %d\n",
				__func__, err);
			return err;
		}
	}

	/* Save the current profile and set the vstep to 0 */
	/* This needs to be overwritten even in CF bypass */
	tfa_dev_set_swprof(tfa, (unsigned short)profile);
	tfa_dev_set_swvstep(tfa, vstep);

	/* Synchonize I/V delay on 96/97 at cold start */

	{
		/* always send the SetRe25 message
		 * to indicate all messages are send
		 */
		pr_info("%s: tfa_set_calibration_values\n", __func__);
		err = TFA98XX_ERROR_OK;
		if (err)
			pr_info("%s: set calibration values error = %d\n",
				__func__, err);
	}

	return err;
}

enum tfa98xx_error
tfa_run_speaker_startup(struct tfa_device *tfa, int force, int profile, int vstep)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	enum tfa_error ret = tfa_error_ok;

	pr_debug("coldstart%s :", force? " (forced)" : "");

	if (!force) { /* in case of force CF already runnning */
		err = tfa_run_startup(tfa, profile);
		PRINT_ASSERT(err);
		if (err) {
			pr_info("%s: tfa_run_startup error = %d\n",
				__func__, err);
			return err;
		}

		/* Startup with CF in bypass then return here */
		if (tfa_cf_enabled(tfa) == 0)
			return err;

		/* respond to external DSP: -1:none, 0:no_dsp, 1:cold, 2:warm */
		{
			err = tfa_run_start_dsp(tfa);
			if (err)
				return err;
		}
	}

	if (tfa->reset_mtpex) { /* reset MTPEX, if suspended */
		tfa->reset_mtpex = 0;
		ret = tfa_dev_mtp_set(tfa, TFA_MTP_EX, 0);
		if (ret)
			pr_err("%s: resetting MTPEX failed (%d)",
				__func__, ret);
	}

	/* Set auto_copy_mtp_to_iic (bit 5 of A3) to 1 */
	tfa98xx_auto_copy_mtp_to_iic(tfa);

	err = tfa_get_fw_api_version(tfa,
		(unsigned char *)&tfa->fw_itf_ver[0]);
	if (err) {
		pr_debug("[%s] cannot get FWAPI error = %d\n",
			__func__, err);
		return err;
	}

	/* DSP is running now */
	/* write all the files from the device list */
	err = tfa_cont_write_files(tfa);
	if (err) {
		pr_debug("[%s] tfa_cont_write_files error = %d\n",
			__func__, err);
		return err;
	}

	tfa->is_bypass = 1; /* reset before start */
	/* skip sending messages in bypass */
	// if (TFA_GET_BF(tfa, MTPEX) == 0) /* need calibration */
	// 	tfa->is_bypass = 0; /* forced for calibration */

	/* write all the files from the profile list (use volumstep 0) */
	pr_info("%s: load prof files (device %d, profile %d)\n",
		__func__, tfa->dev_idx, profile);
	err = tfa_cont_write_files_prof(tfa, profile, vstep);
	if (err) {
		pr_debug("[%s] tfa_cont_write_files_prof error = %d\n",
			__func__, err);
		return err;
	}

	return err;
}

enum tfa98xx_error tfa_run_coldboot(struct tfa_device *tfa, int state)
{
#define CF_CONTROL 0x8100
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	int tries = 10;

	/* repeat set ACS bit until set as requested */
	while (state != TFA_GET_BF(tfa, ACS)) {
		/* set colstarted in CF_CONTROL to force ACS */
		err = mem_write(tfa, CF_CONTROL, state, TFA98XX_DMEM_IOMEM);
		PRINT_ASSERT(err);

		if (tries-- == 0) {
			pr_debug("coldboot (ACS) did not %s\n",
				state ? "set" : "clear");
			return TFA98XX_ERROR_OTHER;
		}
	}

	return err;
}

static enum tfa98xx_error tfa_run_load_patch(struct tfa_device *tfa)
{
	return tfa_cont_write_patch(tfa);
}

enum tfa98xx_error tfa_run_start_dsp(struct tfa_device *tfa)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;

	err = tfa_run_load_patch(tfa);
	if (err) { /* patch load is fatal so return immediately */
		return err;
	}

	/* Clear count_boot, should be reset to 0
	 * before the DSP reset is released
	 */
	err = mem_write(tfa, 512, 0, TFA98XX_DMEM_XMEM);
	PRINT_ASSERT(err);

	/* Reset DSP once for sure after initializing */
	if (err == TFA98XX_ERROR_OK) {
		err = tfa98xx_dsp_reset(tfa, 0);
		/* in pair of tfa98xx_init() - tfa_run_startup() */
		PRINT_ASSERT(err);
	}

	/* Sample rate is needed to set the correct tables */
	err = tfa98xx_dsp_write_tables(tfa, TFA_GET_BF(tfa, AUDFS));
	PRINT_ASSERT(err);

	return err;
}

enum tfa98xx_error tfa_run_startup(struct tfa_device *tfa, int profile)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	struct tfa_device_list *dev = tfa_cont_device(tfa->cnt, tfa->dev_idx);
	int i, noinit = 0, audfs = 0, fractdel = 0;
	char prof_name[MAX_CONTROL_NAME] = {0};
	int is_cold_amp;
	int tfa_state;

	if (dev == NULL)
		return TFA98XX_ERROR_FAIL;

	is_cold_amp = tfa_is_cold_amp(tfa);
	tfa_state = tfa_dev_get_state(tfa);
	pr_info("%s: is_cold_amp %d, TFA state %d\n",
		__func__, is_cold_amp, tfa_state);
	if (tfa->first_after_boot || is_cold_amp == 1) {
		/* process the device list
		 * to see if the user implemented the noinit
		 */
		for (i = 0; i < dev->length; i++) {
			if (dev->list[i].type == dsc_no_init) {
				noinit = 1;
				break;
			}
		}

		if (!noinit) {
			/* Read AUDFS & FRACTDEL prior to (re)init. */
			audfs = TFA_GET_BF(tfa, AUDFS);
			fractdel = TFA_GET_BF(tfa, FRACTDEL);
			/* load the optimal TFA98XX in HW settings */
			err = tfa98xx_init(tfa);
			PRINT_ASSERT(err);

			/* Restore audfs & fractdel after coldboot,
			 * so we can calibrate with correct fs setting.
			 * in case something else was given in cnt file,
			 * profile below will apply this.
			 */
			TFA_SET_BF(tfa, AUDFS, audfs);
			TFA_SET_BF(tfa, FRACTDEL, fractdel);
		} else {
			pr_debug("Warning: No init keyword found in the cnt file. Init is skipped!\n");
		}

		/* I2S settings to define the audio input properties
		 * these must be set before the subsys is up
		 * this will run the list
		 * until a non-register item is encountered
		 */
		pr_info("%s: write registers under dev to device %d\n",
			__func__, tfa->dev_idx);
		err = tfa_cont_write_regs_dev(tfa);
		/* write device register settings */
		PRINT_ASSERT(err);
	} else {
		pr_info("%s: skip_init and writing registers under dev (%d:%d)\n",
			__func__, tfa->first_after_boot, is_cold_amp);
	}

	if ((tfa->first_after_boot || (is_cold_amp == 1))
		|| (profile != tfa_dev_get_swprof(tfa))) {
		/* also write register the settings from the default profile
		 * NOTE we may still have ACS=1
		 * so we can switch sample rate here
		 */
		pr_info("%s: write registers under profile to device %d\n",
			__func__, tfa->dev_idx);
		err =  tfa_cont_write_regs_prof(tfa, profile);
		PRINT_ASSERT(err);
	} else {
		pr_info("%s: skip writing registers under profile (%d)\n",
			__func__, profile);
	}

	/* Factory trimming for the Boost converter */
	tfa98xx_factory_trimmer(tfa);

	tfa->first_after_boot = 0;

	tfa_state = tfa_dev_get_state(tfa);
	if (tfa_state == TFA_STATE_INIT_CF
		|| tfa_state == TFA_STATE_INIT_FW
		|| tfa_state == TFA_STATE_OPERATING) {
		pr_info("%s: skip setting state to INIT_CF (%d)\n",
			__func__, tfa_state);
		err = show_current_state(tfa);

		return err;
	}

	/* Go to the initCF state */
	strscpy(prof_name, tfa_cont_profile_name(tfa->cnt,
		tfa->dev_idx, profile), MAX_CONTROL_NAME);
	tfa_dev_set_state(tfa, TFA_STATE_INIT_CF,
		strnstr(prof_name, ".cal", strlen(prof_name)) != NULL);

	err = show_current_state(tfa);

	return err;
}

enum tfa98xx_error tfa_run_coldstartup(struct tfa_device *tfa, int profile)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;

	err = tfa_run_startup(tfa, profile);
	PRINT_ASSERT(err);
	if (err)
		return err;

	{
		/* force cold boot */
		err = tfa_run_coldboot(tfa, 1); /* set ACS */
		PRINT_ASSERT(err);
		if (err)
			return err;
	}

	/* start */
	err = tfa_run_start_dsp(tfa);
	PRINT_ASSERT(err);

	return err;
}

enum tfa98xx_error tfa_run_mute(struct tfa_device *tfa)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	int status;
	int tries = 0;

	/* signal the TFA98XX to mute  */
	/* err = tfa98xx_set_mute(tfa, TFA98XX_MUTE_AMPLIFIER); */
	tfa_dev_set_state(tfa, TFA_STATE_MUTE, 0);

	if (tfa->verbose)
		pr_debug("-------------------- muted ------------------\n");

	return err;
}

static enum tfa_error tfa_gain_rampdown(struct tfa_device *tfa, int count)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	int cur_ampgain;
	int i;

	cur_ampgain = tfa_get_bf(tfa, TFA9894_BF_AMPGAIN);
	if (cur_ampgain <= 0) {
		pr_debug("%s: ampgain is already rampdown (%d)\n",
			__func__, cur_ampgain);
		return err;
	}
	tfa->ampgain = cur_ampgain;

	/* ramp down amplifier gain for "count" msec */
	pr_debug("%s: ramp down ampgain (%d)\n",
		__func__, tfa->ampgain);
	for (i = count; i > 0; i--) {
		err = tfa_set_bf(tfa, TFA9894_BF_AMPGAIN,
			tfa->ampgain * (i - 1) / count);
		if (err)
			pr_err("%s: error in setting AMPGAIN\n",
				__func__);

		msleep_interruptible(1);
	}

	return err;
}

enum tfa_error tfa_dev_start(struct tfa_device *tfa, int profile, int vstep)
{
	enum tfa98xx_error err;
	enum tfa_error state_err;

	if (profile < 0 || profile >= tfa_cnt_get_dev_nprof(tfa) ||
	    tfa_dev_mtp_get(tfa, TFA_MTP_EX) != 1)
		return tfa_error_bad_param;

	/* Only factory-calibrated playback; never switch into a calibration profile. */
	err = tfa_run_speaker_boost(tfa, 0, profile, vstep);
	if (err != TFA98XX_ERROR_OK)
		return tfa_error_dsp;
	state_err = tfa_dev_set_state(tfa, TFA_STATE_OPERATING | TFA_STATE_MUTE, 0);
	if (state_err != tfa_error_ok)
		return state_err;
	return tfa_dev_switch_profile(tfa, profile, vstep);
}

enum tfa_error tfa_dev_switch_profile(struct tfa_device *tfa,
	int next_profile, int vstep)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	int active_profile = -1;
	char prof_name[MAX_CONTROL_NAME] = {0};

	active_profile = tfa_dev_get_swprof(tfa);

	/* Profile switching */
	if ((next_profile != active_profile && active_profile >= 0)) {
		tfa->is_bypass = 1; /* reset beforehand */
		pr_debug("%s: switch profile from %d to %d\n",
			__func__, active_profile, next_profile);
		err = tfa_cont_write_profile(tfa, next_profile, vstep);
		if (err != TFA98XX_ERROR_OK) {
			return err;
		}
	}

	/* If the profile contains the .standby suffix go
	 * to powerdown else we should be in operating state
	 */
	strscpy(prof_name, tfa_cont_profile_name(tfa->cnt,
		tfa->dev_idx, next_profile), MAX_CONTROL_NAME);
	if (strnstr(prof_name, ".standby", strlen(prof_name)) != NULL) {
		tfa_dev_set_swprof(tfa, (unsigned short)next_profile);
		tfa_dev_set_swvstep(tfa, (unsigned short)vstep);
		return err;
	}

	err = show_current_state(tfa);

	if ((TFA_GET_BF(tfa, CFE) != 0)
		&& (vstep != tfa->vstep) && (vstep != -1)) {
		err = tfa_cont_write_files_vstep(tfa, next_profile, vstep);
		if (err != TFA98XX_ERROR_OK)
			return err;
	}

	/* Always search and apply filters after a startup */
	err = tfa_set_filters(tfa, next_profile);
	if (err != TFA98XX_ERROR_OK)
		return err;

	tfa_dev_set_swprof(tfa, (unsigned short)next_profile);
	tfa_dev_set_swvstep(tfa, (unsigned short)vstep);

	return err;
}

enum tfa_error tfa_dev_stop(struct tfa_device *tfa)
{
	enum tfa_error err;

	err = tfa_dev_set_state(tfa, TFA_STATE_MUTE, 0);
	if (tfa98xx_set_osc_powerdown(tfa, 1) != TFA98XX_ERROR_OK)
		err = tfa_error_device;
	if (tfa98xx_powerdown(tfa, 1) != TFA98XX_ERROR_OK)
		err = tfa_error_device;
	return err;
}

enum tfa98xx_error
tfa98xx_write_data(struct tfa_device *tfa,
	unsigned char subaddress, int num_bytes,
	const unsigned char data[])
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	/* subaddress followed by data */
	const int bytes2write = num_bytes + 1;
	unsigned char *write_data;

	if (num_bytes > TFA2_MAX_PARAM_SIZE)
		return TFA98XX_ERROR_BAD_PARAMETER;

	write_data = (unsigned char *)
		kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (write_data == NULL)
		return TFA98XX_ERROR_FAIL;

	write_data[0] = subaddress;
	memcpy(&write_data[1], data, num_bytes);

	error = tfa98xx_write_raw(tfa, bytes2write, write_data);

	kfree(write_data);
	return error;
}

enum tfa98xx_error tfa_dsp_get_calibration_impedance(struct tfa_device *tfa)
{
	int re25;

	if (tfa_dev_mtp_get(tfa, TFA_MTP_OTC) != 1 ||
	    tfa_dev_mtp_get(tfa, TFA_MTP_EX) != 1)
		return TFA98XX_ERROR_BAD_PARAMETER;
	re25 = tfa_dev_mtp_get(tfa, TFA_MTP_RE25);
	if (re25 < 1000 || re25 > 32000)
		return TFA98XX_ERROR_BAD_PARAMETER;
	tfa->mohm[0] = re25;
	return TFA98XX_ERROR_OK;
}

int tfa_dev_get_swprof(struct tfa_device *tfa)
{
	return (tfa->dev_ops.get_swprof)(tfa);
}

int tfa_dev_set_swprof(struct tfa_device *tfa, unsigned short new_value)
{
	return (tfa->dev_ops.set_swprof)(tfa, new_value + 1);
}

int tfa_dev_set_swvstep(struct tfa_device *tfa, unsigned short new_value)
{
	return (tfa->dev_ops.set_swvstep)(tfa, new_value + 1);
}

int tfa_dev_get_mtpb(struct tfa_device *tfa)
{
	return (tfa->dev_ops.get_mtpb)(tfa);
}

int tfa_is_cold(struct tfa_device *tfa)
{
	int value;

	/*
	 * check for cold boot status
	 */
	{
		value = TFA_GET_BF(tfa, ACS);
	}

	return value;
}

int tfa_needs_reset(struct tfa_device *tfa)
{
	int value;

	/* checks if the DSP commands SetAlgoParams and SetMBDrc
	 * need a DSP reset (now: at coldstart or during calibration)
	 */
	if (tfa_is_cold(tfa) == 1 || tfa->needs_reset == 1)
		value = 1;
	else
		value = 0;

	return value;
}

int tfa_is_cold_amp(struct tfa_device *tfa)
{
	int value;

	value = (TFA_GET_BF(tfa, MANSCONF) == 0);

	return value;
}

int tfa_cf_enabled(struct tfa_device *tfa)
{
	int value;

	/* For 72 there is no CF */
	value = TFA_GET_BF(tfa, CFE);

	return value;
}

#define NR_COEFFS 6
#define NR_BIQUADS 28
#define BQ_SIZE (3 * NR_COEFFS)
#define DSP_MSG_OVERHEAD 27

#pragma pack(push, 1)
struct dsp_msg_all_coeff {
	uint8_t select_eq[3];
	uint8_t biquad[NR_BIQUADS][NR_COEFFS][3];
};
#pragma pack(pop)

static const int eq_biquads[] = {
	10, 10, 2, 2, 2, 2
};

#define NR_EQ (int)(sizeof(eq_biquads) / sizeof(int))

enum tfa98xx_error
dsp_partial_coefficients(struct tfa_device *tfa,
	uint8_t *prev, uint8_t *next)
{
	uint8_t bq, eq;
	int eq_offset;
	int new_cost, old_cost;
	uint32_t eq_biquad_mask[NR_EQ];
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	struct dsp_msg_all_coeff *data1 = (struct dsp_msg_all_coeff *)prev;
	struct dsp_msg_all_coeff *data2 = (struct dsp_msg_all_coeff *)next;

	old_cost = DSP_MSG_OVERHEAD + 3 + sizeof(struct dsp_msg_all_coeff);
	new_cost = 0;

	eq_offset = 0;
	for (eq = 0; eq < NR_EQ; eq++) {
		uint8_t *eq1 = &data1->biquad[eq_offset][0][0];
		uint8_t *eq2 = &data2->biquad[eq_offset][0][0];

		eq_biquad_mask[eq] = 0;

		if (memcmp(eq1, eq2, BQ_SIZE*eq_biquads[eq]) != 0) {
			int nr_bq = 0;
			int bq_sz, eq_sz;

			for (bq = 0; bq < eq_biquads[eq]; bq++) {
				uint8_t *bq1 = &eq1[bq*BQ_SIZE];
				uint8_t *bq2 = &eq2[bq*BQ_SIZE];

				if (memcmp(bq1, bq2, BQ_SIZE) != 0) {
					eq_biquad_mask[eq] |= (1 << bq);
					nr_bq++;
				}
			}

			bq_sz = (2 * 3 + BQ_SIZE) * nr_bq;
			eq_sz = 2 * 3 + BQ_SIZE * eq_biquads[eq];

			/* dsp message i2c transaction overhead */
			bq_sz += DSP_MSG_OVERHEAD * nr_bq;
			eq_sz += DSP_MSG_OVERHEAD;

			if (bq_sz >= eq_sz) {
				eq_biquad_mask[eq] = 0xffffffff;
				new_cost += eq_sz;
			} else {
				new_cost += bq_sz;
			}
		}
		pr_debug("eq_biquad_mask[%d] = 0x%.8x\n",
			eq, eq_biquad_mask[eq]);

		eq_offset += eq_biquads[eq];
	}

	pr_debug("cost for writing all coefficients = %d\n", old_cost);
	pr_debug("cost for writing changed coefficients = %d\n", new_cost);

	if (new_cost >= old_cost) {
		const int buffer_sz = 3 + sizeof(struct dsp_msg_all_coeff);
		uint8_t *buffer;

		buffer = kmalloc(buffer_sz, GFP_KERNEL);
		if (buffer == NULL)
			return TFA98XX_ERROR_FAIL;

		/* cmd id */
		buffer[0] = 0x00;
		buffer[1] = 0x82;
		buffer[2] = 0x00;

		/* parameters */
		memcpy(&buffer[3], data2, sizeof(struct dsp_msg_all_coeff));

		err = dsp_msg(tfa, buffer_sz, (const char *)buffer);

		kfree(buffer);

		return err;
	}

	/* (new_cost < old_cost) */
	eq_offset = 0;
	for (eq = 0; eq < NR_EQ; eq++) {
		uint8_t *eq2 = &data2->biquad[eq_offset][0][0];

		if (eq_biquad_mask[eq] == 0xffffffff) {
			const int msg_sz = 6 + BQ_SIZE * eq_biquads[eq];
			uint8_t *msg;

			msg = kmalloc(msg_sz, GFP_KERNEL);
			if (msg == NULL)
				return TFA98XX_ERROR_FAIL;

			/* cmd id */
			msg[0] = 0x00;
			msg[1] = 0x82;
			msg[2] = 0x00;

			/* select eq and bq */
			msg[3] = 0x00;
			msg[4] = eq + 1;
			msg[5] = 0x00; /* all biquads */

			/* biquad parameters */
			memcpy(&msg[6], eq2, BQ_SIZE * eq_biquads[eq]);

			err = dsp_msg(tfa, msg_sz, (const char *)msg);

			kfree(msg);
			if (err)
				return err;
		} else if (eq_biquad_mask[eq] != 0) {
			const int msg_sz = 6 + BQ_SIZE;
			uint8_t *msg;

			msg = kmalloc(PAGE_SIZE, GFP_KERNEL);
			if (msg == NULL)
				return TFA98XX_ERROR_FAIL;

			for (bq = 0; bq < eq_biquads[eq]; bq++) {
				if (eq_biquad_mask[eq] & (1 << bq)) {
					uint8_t *bq2
						= &eq2[bq * BQ_SIZE];

					/* cmd id */
					msg[0] = 0x00;
					msg[1] = 0x82;
					msg[2] = 0x00;

					/* select eq and bq */
					msg[3] = 0x00;
					msg[4] = eq + 1;
					msg[5] = bq + 1;

					/* biquad parameters */
					memcpy(&msg[6], bq2, BQ_SIZE);

					err = dsp_msg(tfa, msg_sz,
						(const char *)msg);

					if (err) {
						kfree(msg);
						return err;
					}
				}
			}
			kfree(msg);
		}
		eq_offset += eq_biquads[eq];
	}

	return err;
}

int tfa_dev_probe(int slave, struct tfa_device *tfa)
{
	uint16_t rev;

	tfa->slave_address = (unsigned char)slave;

	/* read revid via low level hal, register 3 */
	if (tfa98xx_read_register16(tfa, 0x03, &rev)
		!= TFA98XX_ERROR_OK) {
		pr_debug("\nError: Unable to read revid from slave:0x%02x\n",
			slave);
		return TFA_ERROR;
	}

	tfa->rev = rev;
	tfa->dev_idx = -1;
	tfa->state = TFA_STATE_UNKNOWN;
	tfa->p_reg_info = NULL;

	tfa_set_query_info(tfa);

	tfa->in_use = 1;

	return 0;
}

enum tfa_error
tfa_dev_set_state(struct tfa_device *tfa,
	enum tfa_state state, int is_calibration)
{
	enum tfa_error err = tfa_error_ok;
	int loop = 50, ready = 0;
	int count;

	pr_info("%s: state = 0x%02x\n", __func__, state & 0xff);

	/* Base states */
	/* Do not change the order of setting bits as this is important! */
	switch (state & 0x0f) {
	case TFA_STATE_POWERDOWN: /* PLL in powerdown, Algo up */
		break;
	case TFA_STATE_INIT_HW: /* load I2C/PLL hardware setting (~wait2srcsettings) */
		break;
	case TFA_STATE_INIT_CF: /* coolflux HW access possible (~initcf) */
		/* Start with SBSL=0 to stay in initCF state */
		TFA_SET_BF(tfa, SBSL, 0);

		/* We want to leave Wait4SrcSettings state for max2 */
		TFA_SET_BF(tfa, MANSCONF, 1);

		/* And finally set PWDN to 0 to leave powerdown state */
		TFA_SET_BF(tfa, PWDN, 0);

		/* Make sure the DSP is running! */
			do {
				err = tfa98xx_dsp_system_stable(tfa, &ready);
				if (err != tfa_error_ok)
					return err;
				if (ready)
					break;
			} while (loop--);
			if (!ready)
				return tfa_error_noclock;

		if (((!0) && (is_calibration))
			|| ((0x94) == 0x13)) {
			/* Enable FAIM when clock is stable, to avoid MTP corruption */
			err = tfa98xx_faim_protect(tfa, 1);
			if (tfa->verbose)
				pr_debug("FAIM enabled (err:%d).\n", err);
		}
		break;
	case TFA_STATE_INIT_FW: /* DSP framework active (~patch loaded) */
		break;
	case TFA_STATE_OPERATING: /* Amp and Algo running */
		/* Depending on our previous state we need to set 3 bits */
		TFA_SET_BF(tfa, PWDN, 0);	/* Coming from state 0 */
		TFA_SET_BF(tfa, MANSCONF, 1);	/* Coming from state 1 */
		TFA_SET_BF(tfa, SBSL, 1);	/* No SBSL for probus device, we set AMPE to 1  */

		/*
		 * Disable MTP clock to protect memory.
		 * However in case of calibration wait for DSP! (This should be case only during calibration).
		 */
		if (TFA_GET_BF(tfa, MTPOTC) == 1 && 2 == 2) {
			count = MTPEX_WAIT_NTRIES * 4; /* Calibration takes a lot of time */
			while ((TFA_GET_BF(tfa, MTPEX) != 1) && count) {
				msleep_interruptible(10);
				count--;
			}
		}
		if (((!0) && (is_calibration))
			|| ((0x94) == 0x13)) {
			err = tfa98xx_faim_protect(tfa, 0);
			if (tfa->verbose)
				pr_debug("FAIM disabled (err:%d).\n", err);
		}
		/* Synchonize I/V delay on 96/97 at cold start */
		if (tfa->sync_iv_delay) {
			if (tfa->verbose)
				pr_debug("syncing I/V delay for %x\n",
					(0x94));

			/* wait for ACS to be cleared */
			count = ACS_RESET_WAIT_NTRIES;
			while ((TFA_GET_BF(tfa, ACS) == 1) &&
				(count-- > 0))
				msleep_interruptible(1);

			tfa98xx_dsp_reset(tfa, 1);
			tfa98xx_dsp_reset(tfa, 0);
			tfa->sync_iv_delay = 0;
		}
		break;
	case TFA_STATE_FAULT:        /* An alarm or error occurred */
		break;
	case TFA_STATE_RESET:        /* I2C reset and ACS set */
		tfa98xx_init(tfa);
		break;
	default:
		if (state & 0x0f)
			return tfa_error_bad_param;
	}

	/* state modifiers */

	if (state & TFA_STATE_MUTE) {
		/* ramp down gain at muting only */
		if ((state & 0x0f) == 0
			&& is_calibration == 0)
			tfa_gain_rampdown(tfa, 5);
		if (tfa98xx_set_mute(tfa, TFA98XX_MUTE_AMPLIFIER) !=
		    TFA98XX_ERROR_OK)
			return tfa_error_dsp;
	}

	if (state & TFA_STATE_UNMUTE &&
	    tfa98xx_set_mute(tfa, TFA98XX_MUTE_OFF) != TFA98XX_ERROR_OK)
		return tfa_error_dsp;

	/* tfa->state = state; */ /* to correct with real state of device */
	tfa->state = tfa_dev_get_state(tfa);

	return tfa_error_ok;
}

enum tfa_state tfa_dev_get_state(struct tfa_device *tfa)
{
	int cold = 0;
	int manstate;

	/* different per family type */
	{
		if (is_94_N2_device(tfa))
			manstate = tfa_get_bf(tfa, TFA9894N2_BF_MANSTATE);
		else
			manstate = TFA_GET_BF(tfa, MANSTATE);

		pr_debug("%s: manstate = %d\n", __func__, manstate);

		switch (manstate) {
		case 0:
			tfa->state = TFA_STATE_POWERDOWN;
			break;
		case 8: /* if dsp reset if off assume framework is running */
			tfa->state = TFA_GET_BF(tfa, RST)
				? TFA_STATE_INIT_CF : TFA_STATE_INIT_FW;
			break;
		case 9:
			tfa->state = TFA_STATE_OPERATING;
			break;
		default:
			tfa->state = TFA_STATE_UNKNOWN;
			break;
		}
	}

	return tfa->state;
}

int tfa_dev_mtp_get(struct tfa_device *tfa, enum tfa_mtp item)
{
	int value = 0;

	switch (item) {
	case TFA_MTP_OTC:
		value = TFA_GET_BF(tfa, MTPOTC);
		break;
	case TFA_MTP_EX:
		value = TFA_GET_BF(tfa, MTPEX);
		break;
	case TFA_MTP_RE25:
	case TFA_MTP_RE25_PRIM:
		{
			value = TFA_GET_BF(tfa, R25C);
		}
		break;
	case TFA_MTP_RE25_SEC:
		{
			pr_debug("Error: Current device has no secondary Re25 channel\n");
		}
		break;
	case TFA_MTP_LOCK:
		break;
	}

	return value;
}

enum tfa_error tfa_dev_mtp_set(struct tfa_device *tfa,
	enum tfa_mtp item, int value)
{
	enum tfa_error err = tfa_error_ok;

	switch (item) {
	case TFA_MTP_OTC:
		err = tfa98xx_set_mtp(tfa,
			(uint16_t)(value << TFA98XX_KEY2_PROTECTED_MTP0_MTPOTC_POS),
			TFA98XX_KEY2_PROTECTED_MTP0_MTPOTC_MSK);
		break;
	case TFA_MTP_EX:
		err = tfa98xx_set_mtp(tfa,
			(uint16_t)(value << TFA98XX_KEY2_PROTECTED_MTP0_MTPEX_POS),
			TFA98XX_KEY2_PROTECTED_MTP0_MTPEX_MSK);
		break;
	case TFA_MTP_RE25:
	case TFA_MTP_RE25_PRIM:
		{
			tfa98xx_key2(tfa, 0); /* unlock */
			{
				TFA_SET_BF(tfa, R25C, (uint16_t)value);
				TFA_SET_BF(tfa, CIMTP, 1);
			}
			tfa98xx_key2(tfa, 1); /* lock */
		}
		break;
	case TFA_MTP_RE25_SEC:
		{
			pr_debug("Error: Current device has no secondary Re25 channel\n");
			err = tfa_error_bad_param;
		}
		break;
	case TFA_MTP_LOCK:
		break;
	}

	return err;
}

#define TFA_CAL_RANGE(max_devcnt, channel, type, revid) \
	((max_devcnt != 2) ? type##_LIMIT_CAL_##revid : \
	((channel == 0) ? \
	type##_LIMIT_CAL_P_##revid : type##_LIMIT_CAL_S_##revid))
#define LOWER_LIMIT_CAL_N1C 0
#define UPPER_LIMIT_CAL_N1C 32000
#define LOWER_LIMIT_CAL_P_N1C 0
#define UPPER_LIMIT_CAL_P_N1C 32000
#define LOWER_LIMIT_CAL_S_N1C 0
#define UPPER_LIMIT_CAL_S_N1C 32000

int tfa_set_bf_value(const uint16_t bf,
	const uint16_t bf_value, uint16_t *p_reg_value)
{
	uint16_t regvalue, msk;

	/*
	 * bitfield enum:
	 * - 0..3  : len
	 * - 4..7  : pos
	 * - 8..15 : address
	 */
	uint8_t len = bf & 0x0f;
	uint8_t pos = (bf >> 4) & 0x0f;

	regvalue = *p_reg_value;

	msk = ((1 << (len + 1)) - 1) << pos;
	regvalue &= ~msk;
	regvalue |= bf_value << pos;

	*p_reg_value = regvalue;

	return 0;
}

int tfa_write_reg(struct tfa_device *tfa,
	const uint16_t bf, const uint16_t reg_value)
{
	enum tfa98xx_error err;

	/* bitfield enum - 8..15 : address */
	uint8_t address = (bf >> 8) & 0xff;

	err = reg_write(tfa, address, reg_value);
	if (err)
		return -err;

	return 0;
}

int tfa_read_reg(struct tfa_device *tfa, const uint16_t bf)
{
	enum tfa98xx_error err;
	uint16_t regvalue;

	/* bitfield enum - 8..15 : address */
	uint8_t address = (bf >> 8) & 0xff;

	err = reg_read(tfa, address, &regvalue);
	if (err)
		return -err;

	return regvalue;
}
