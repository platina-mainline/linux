/*
 * Copyright 2014-2017 NXP Semiconductors
 */

#define DEBUG

#include "tfa_container.h"
#include "tfa.h"
#include "tfa98xx_tfafieldnames.h"
#include "tfa_internal.h"

#define MODULE_BIQUADFILTERBANK 2
#define BIQUAD_COEFF_SIZE       6

#define TFADSP_ADD_TOTAL_SIZE_TO_BLOB
#define TFADSP_SET_EXT_TEMP_FROM_DRIVER

static uint8_t gslave_address = 0; 

enum tfa_error tfa_load_cnt(void *cnt, int length)
{
	struct tfa_container *cntbuf = (struct tfa_container *)cnt;

	if (length < sizeof(*cntbuf) || length > TFA_MAX_CNT_LENGTH) {
		pr_err("incorrect length\n");
		return tfa_error_container;
	}
	if (cntbuf->size != length ||
	    cntbuf->ndev > TFACONT_MAXDEVS ||
	    cntbuf->nprof > TFACONT_MAXPROFS ||
	    cntbuf->ndev > (length - sizeof(*cntbuf)) / sizeof(cntbuf->index[0]))
		return tfa_error_container;

	if (HDR(cntbuf->id[0], cntbuf->id[1]) == 0) {
		pr_err("header is 0\n");
		return tfa_error_container;
	}

	if ((HDR(cntbuf->id[0], cntbuf->id[1])) != params_hdr) {
		pr_err("wrong header type: 0x%02x 0x%02x\n",
			cntbuf->id[0], cntbuf->id[1]);
		return tfa_error_container;
	}

	if (cntbuf->size == 0) {
		pr_err("data size is 0\n");
		return tfa_error_container;
	}

	/* check CRC */
	if (tfa_cont_crc_check_container(cntbuf)) {
		pr_err("CRC error\n");
		return tfa_error_container;
	}

	/* check sub version level */
	if ((cntbuf->subversion[1] != NXPTFA_PM_SUBVERSION)
		&& (cntbuf->subversion[0] != '0')) {
		pr_err("container sub-version not supported: %c%c\n",
			cntbuf->subversion[0], cntbuf->subversion[1]);
		return tfa_error_container;
	}

	return tfa_error_ok;
}

void tfa_cont_show_header(struct tfa_header *hdr)
{
	char _id[2];

	pr_debug("File header\n");

	_id[1] = hdr->id >> 8;
	_id[0] = hdr->id & 0xff;
	pr_debug("\tid:%.2s version:%.2s subversion:%.2s\n", _id,
		hdr->version, hdr->subversion);
	pr_debug("\tsize:%d CRC:0x%08x\n", hdr->size, hdr->crc);
	pr_debug("\tcustomer:%.8s application:%.8s type:%.8s\n",
		hdr->customer, hdr->application, hdr->type);
}

struct tfa_device_list *tfa_cont_get_dev_list
(struct tfa_container *cont, int dev_idx)
{
	uint8_t *base = (uint8_t *)cont;

	if (cont == NULL)
		return NULL;

	if ((dev_idx < 0) || (dev_idx >= cont->ndev))
		return NULL;

	if (cont->index[dev_idx].type != dsc_device)
		return NULL;

	base += cont->index[dev_idx].offset;
	return (struct tfa_device_list *)base;
}

struct tfa_profile_list *tfa_cont_get_dev_prof_list
(struct tfa_container *cont, int dev_idx, int prof_idx)
{
	struct tfa_device_list *dev;
	int idx, hit;
	uint8_t *base = (uint8_t *)cont;

	dev = tfa_cont_get_dev_list(cont, dev_idx);
	if (dev) {
		for (idx = 0, hit = 0; idx < dev->length; idx++) {
			if (dev->list[idx].type == dsc_profile) {
				if (prof_idx == hit++)
					return (struct tfa_profile_list *)
						(dev->list[idx].offset + base);
			}
		}
	}

	return NULL;
}

int tfa_cnt_get_dev_nprof(struct tfa_device *tfa)
{
	struct tfa_device_list *dev;
	int idx, nprof = 0;

	if (tfa->cnt == NULL)
		return 0;

	if ((tfa->dev_idx < 0) || (tfa->dev_idx >= tfa->cnt->ndev))
		return 0;

	dev = tfa_cont_get_dev_list(tfa->cnt, tfa->dev_idx);
	if (dev) {
		for (idx = 0; idx < dev->length; idx++) {
			if (dev->list[idx].type == dsc_profile) {
				nprof++;
			}
		}
	}

	return nprof;
}

int tfa_cont_get_max_vstep(struct tfa_device *tfa, int prof_idx)
{
	struct tfa_volume_step2_file *vp;
	struct tfa_volume_step_max2_file *vp3;
	int vstep_count = 0;

	vp = (struct tfa_volume_step2_file *)
		tfa_cont_get_file_data(tfa, prof_idx, volstep_hdr);
	if (vp == NULL)
		return 0;
	/* check the header type to load different NrOfVStep appropriately */
	{
		/* this is actually tfa2, so re-read the buffer*/
		vp3 = (struct tfa_volume_step_max2_file *)
			tfa_cont_get_file_data(tfa, prof_idx, volstep_hdr);
		if (vp3)
			vstep_count = vp3->nr_of_vsteps;
	}

	return vstep_count;
}

struct tfa_file_dsc *tfa_cont_get_file_data(struct tfa_device *tfa,
	int prof_idx, enum tfa_header_type type)
{
	struct tfa_device_list *dev;
	struct tfa_profile_list *prof;
	struct tfa_file_dsc *file;
	struct tfa_header *hdr;
	unsigned int i;

	if (tfa->cnt == NULL) {
		pr_err("invalid pointer to container file\n");
		return NULL;
	}

	dev = tfa_cont_get_dev_list(tfa->cnt, tfa->dev_idx);
	if (dev == NULL) {
		pr_err("invalid pointer to container file device list\n");
		return NULL;
	}

	/* process the device list until a file type is encountered */
	for (i = 0; i < dev->length; i++) {
		if (dev->list[i].type == dsc_file) {
			file = (struct tfa_file_dsc *)
				(dev->list[i].offset + (uint8_t *)tfa->cnt);
			if (file != NULL) {
				hdr = (struct tfa_header *)file->data;
				/* check for file type */
				if (hdr->id == type) {
					/* pr_debug("%s: file found of type "
					 *  "%d in device %s\n",
					 *  __func__, type,
					 * tfa_cont_device_name(tfa->cnt,
					 * tfa->dev_idx));
					 */
					return (struct tfa_file_dsc *)
						&file->data;
				}
			}
		}
	}

	/* File not found in device tree.
	 * So, look in the profile list until the file type is encountered
	 */
	prof = tfa_cont_get_dev_prof_list(tfa->cnt, tfa->dev_idx, prof_idx);
	if (prof == NULL) {
		pr_err("invalid pointer to container file profile list\n");
		return NULL;
	}

	for (i = 0; i < prof->length; i++) {
		if (prof->list[i].type == dsc_file) {
			file = (struct tfa_file_dsc *)
				(prof->list[i].offset + (uint8_t *)tfa->cnt);
			if (file != NULL) {
				hdr = (struct tfa_header *)file->data;
				if (hdr != NULL) {
					/* check for file type */
					if (hdr->id == type) {
						/* pr_debug("%s: file found of "
						 *  "type %d in profile %s\n",
						 *  __func__, type,
						 * tfa_cont_profile_name
						 *  (tfa->cnt, tfa->dev_idx,
						 *  prof_idx));
						 */
						return (struct tfa_file_dsc *)
							&file->data;
					}
				}
			}
		}
	}

	if (tfa->verbose)
		pr_debug("%s: no file found of type %d\n", __func__, type);

	return NULL;
}

static struct tfa_volume_step_message_info *
tfa_cont_get_msg_info_from_reg(struct tfa_volume_step_register_info *reg_info)
{
	char *p = (char *)reg_info;

	p += sizeof(reg_info->nr_of_registers)
		+ (reg_info->nr_of_registers * sizeof(uint32_t));
	return (struct tfa_volume_step_message_info *)p;
}

static int
tfa_cont_get_msg_len(struct tfa_volume_step_message_info *msg_info)
{
	return (msg_info->message_length.b[0] << 16)
		+ (msg_info->message_length.b[1] << 8)
		+ msg_info->message_length.b[2];
}

static struct tfa_volume_step_message_info *
tfa_cont_get_next_msg_info(struct tfa_volume_step_message_info *msg_info)
{
	char *p = (char *)msg_info;
	int msgLen = tfa_cont_get_msg_len(msg_info);
	int type = msg_info->message_type;

	p += sizeof(msg_info->message_type) + sizeof(msg_info->message_length);
	if (type == 3)
		p += msgLen;
	else
		p += msgLen * 3;

	return (struct tfa_volume_step_message_info *)p;
}

static struct tfa_volume_step_register_info *
tfa_cont_get_next_reg_from_end_info
(struct tfa_volume_step_message_info *msg_info)
{
	char *p = (char *)msg_info;

	p += sizeof(msg_info->nr_of_messages);
	return (struct tfa_volume_step_register_info *)p;

}

static struct tfa_volume_step_register_info *
tfa_cont_get_reg_for_vstep(struct tfa_volume_step_max2_file *vp, int idx)
{
	int i, j, nrMessage;

	struct tfa_volume_step_register_info *reg_info
		= (struct tfa_volume_step_register_info *)vp->vsteps_bin;
	struct tfa_volume_step_message_info *msg_info = NULL;

	for (i = 0; i < idx; i++) {
		msg_info = tfa_cont_get_msg_info_from_reg(reg_info);
		nrMessage = msg_info->nr_of_messages;

		for (j = 0; j < nrMessage; j++)
			msg_info = tfa_cont_get_next_msg_info(msg_info);
		reg_info = tfa_cont_get_next_reg_from_end_info(msg_info);
	}

	return reg_info;
}

#pragma pack(push, 1)
struct tfa_partial_msg_block {
	uint8_t offset;
	uint16_t change;
	uint8_t update[16][3];
};
#pragma pack(pop)

static enum tfa98xx_error tfa_cont_write_vstepMax2_One
(struct tfa_device *tfa, struct tfa_volume_step_message_info *new_msg,
	struct tfa_volume_step_message_info *old_msg, int enable_partial_update)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	int len = (tfa_cont_get_msg_len(new_msg) - 1) * 3;
	char *buf = (char *)new_msg->parameter_data;
	uint8_t *partial = NULL;
	uint8_t cmdid[3];
	int use_partial_coeff = 0;

	if (enable_partial_update) {
		if (new_msg->message_type != old_msg->message_type) {
			pr_debug("Message type differ - Disable Partial update\n");
			enable_partial_update = 0;
		} else if (tfa_cont_get_msg_len(new_msg)
			!= tfa_cont_get_msg_len(old_msg)) {
			pr_debug("Message Length differ - Disable Partial update\n");
			enable_partial_update = 0;
		}
	}

	if ((enable_partial_update) && (new_msg->message_type == 1)) {
		/* No patial updates for message type 1 (Coefficients) */
		enable_partial_update = 0;
	}

	/* Change Message Len to the actual buffer len */
	memcpy(cmdid, new_msg->cmd_id, sizeof(cmdid));

	/* The algoparams and mbdrc msg id will be changed
	 * to the reset type when SBSL=0
	 * if SBSL=1 the msg will remain unchanged.
	 * It's up to the tuning engineer to choose the 'without_reset'
	 * types inside the vstep.
	 * In other words: the reset msg is applied during SBSL==0
	 * else it remains unchanged.
	 */
	pr_info("%s: is_cold %d\n", __func__, tfa->is_cold);
	if (tfa_needs_reset(tfa) == 1) {
		if (new_msg->message_type == 0) {
			cmdid[2] = SB_PARAM_SET_ALGO_PARAMS;
			if (tfa->verbose)
				pr_debug("P-ID for SetAlgoParams modified!\n");
		} else if (new_msg->message_type == 2) {
			cmdid[2] = SB_PARAM_SET_MBDRC;
			if (tfa->verbose)
				pr_debug("P-ID for SetMBDrc modified!\n");
		}
	}

	/*
	 * +sizeof(struct tfa_partial_msg_block) will allow to fit one
	 * additonnal partial block If the partial update goes over the len of
	 * a regular message, we can safely write our block and check afterward
	 * that we are over the size of a usual update
	 */
	if (enable_partial_update) {
		partial = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!partial)
			pr_debug("Partial update memory error - Disabling\n");
	}

	if (partial) {
		uint8_t offset = 0, i = 0;
		uint16_t *change;
		uint8_t *n = new_msg->parameter_data;
		uint8_t *o = old_msg->parameter_data;
		uint8_t *p = partial;
		uint8_t *trim = partial;

		/* set dspFiltersReset */
		*p++ = 0x02;
		*p++ = 0x00;
		*p++ = 0x00;

		while ((o < (old_msg->parameter_data + len)) &&
			(p < (partial + len - 3))) {
			if ((offset == 0xff) ||
				(memcmp(n, o, 3 * sizeof(uint8_t)))) {
				*p++ = offset;
				change = (uint16_t *)p;
				*change = 0;
				p += 2;

				for (i = 0; (i < 16)
					&& (o < (old_msg->parameter_data + len));
					i++, n += 3, o += 3) {
					if (memcmp(n, o, 3 * sizeof(uint8_t))) {
						*change |= BIT(i);
						memcpy(p, n, 3);
						p += 3;
						trim = p;
					}
				}

				offset = 0;
				*change = cpu_to_be16(*change);
			} else {
				n += 3;
				o += 3;
				offset++;
			}
		}

		if (trim == partial) {
			pr_debug("No Change in message - discarding %d bytes\n",
				len);
			len = 0;
		} else if (trim < (partial + len - 3)) {
			pr_debug("Using partial update: %d -> %d bytes\n",
				len, (int)(trim - partial + 3));

			/* Add the termination marker */
			memset(trim, 0x00, 3);
			trim += 3;

			/* Signal This will be a partial update */
			cmdid[2] |= BIT(6);
			buf = (char *)partial;
			len = (int)(trim - partial);
		} else {
			pr_debug("Partial too big - use regular update\n");
		}
	} else {
		if (!enable_partial_update)
			pr_debug("Partial update - Not enabled\n");
		else /* partial == NULL */
			pr_err("Partial update memory error - Disabling\n");
	}

	if (use_partial_coeff) {
		err = dsp_partial_coefficients(tfa,
			old_msg->parameter_data, new_msg->parameter_data);
	} else if (len) {
		uint8_t *buffer;

		if (tfa->verbose)
			pr_debug("Command-ID used: 0x%02x%02x%02x \n",
				cmdid[0], cmdid[1], cmdid[2]);

		buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (buffer == NULL) {
			err = TFA98XX_ERROR_FAIL;
		} else {
			memcpy(&buffer[0], cmdid, 3);
			memcpy(&buffer[3], buf, len);
			err = dsp_msg(tfa, 3 + len, (char *)buffer);
			kfree(buffer);
		}
	}

	if (partial)
		kfree(partial);

	return err;
}

static enum tfa98xx_error tfa_cont_write_vstepMax2
(struct tfa_device *tfa, struct tfa_volume_step_max2_file *vp,
	int vstep_idx, int vstep_msg_idx)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	struct tfa_volume_step_register_info *reg_info = NULL;
	struct tfa_volume_step_message_info *msg_info = NULL,
		*p_msg_info = NULL;
	struct tfa_bitfield bit_f;
	int i, nr_messages, enp = tfa->partial_enable;

	if (vstep_idx >= vp->nr_of_vsteps) {
		pr_debug("Volumestep %d is not available \n", vstep_idx);
		return TFA98XX_ERROR_BAD_PARAMETER;
	}

	if (tfa->p_reg_info == NULL) {
		if (tfa->verbose)
			pr_debug("Inital vstep write\n");
		enp = 0;
	}

	reg_info = tfa_cont_get_reg_for_vstep(vp, vstep_idx);

	msg_info = tfa_cont_get_msg_info_from_reg(reg_info);
	nr_messages = msg_info->nr_of_messages;

	if (enp) {
		p_msg_info = tfa_cont_get_msg_info_from_reg(tfa->p_reg_info);
		if (nr_messages != p_msg_info->nr_of_messages) {
			pr_debug("Message different - Disable partial update\n");
			enp = 0;
		}
	}

	for (i = 0; i < nr_messages; i++) {
		/* Messagetype(3) is Smartstudio Info! Dont send this! */
		if (msg_info->message_type == 3) {
			pr_debug("Skipping Message Type 3\n");
			/* message_length is in bytes */
			msg_info = tfa_cont_get_next_msg_info(msg_info);
			if (enp)
				p_msg_info = tfa_cont_get_next_msg_info
					(p_msg_info);
			continue;
		}

		/* If no vstepMsgIndex is passed on,
		 * all message needs to be send
		 */
		if ((vstep_msg_idx >= TFA_MAX_VSTEP_MSG_MARKER)
			|| (vstep_msg_idx == i)) {
			err = tfa_cont_write_vstepMax2_One
				(tfa, msg_info, p_msg_info, enp);
			if (err != TFA98XX_ERROR_OK) {
				/*
				 * Force a full update for the next write
				 * As the current status of the DSP is unknown
				 */
				tfa->p_reg_info = NULL;
				return err;
			}
		}

		msg_info = tfa_cont_get_next_msg_info(msg_info);
		if (enp)
			p_msg_info = tfa_cont_get_next_msg_info(p_msg_info);
	}

	tfa->p_reg_info = reg_info;

	for (i = 0; i < reg_info->nr_of_registers * 2; i++) {
		/* Byte swap the datasheetname */
		bit_f.field = (uint16_t)(reg_info->register_info[i] >> 8)
			| (reg_info->register_info[i] << 8);
		i++;
		bit_f.value = (uint16_t)reg_info->register_info[i] >> 8;
		err = tfa_run_write_bitfield(tfa, bit_f);
		if (err != TFA98XX_ERROR_OK)
			return err;
	}

	/* Save the current vstep */
	tfa_dev_set_swvstep(tfa, (unsigned short)vstep_idx);

	return err;
}

enum tfa98xx_error tfa_cont_write_drc_file
(struct tfa_device *tfa, int size, uint8_t data[])
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	uint8_t *msg = NULL;

	msg = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (msg == NULL)
		return TFA98XX_ERROR_FAIL;
	memcpy(msg, data, size);

	if (TFA_GET_BF(tfa, SBSL) == 0) {
		/* Only do this when not set already */
		if (msg[2] != SB_PARAM_SET_MBDRC) {
			msg[2] = SB_PARAM_SET_MBDRC;

			if (tfa->verbose) {
				pr_debug("P-ID for SetMBDrc modified!: ");
				pr_debug("Command-ID used: 0x%02x%02x%02x\n",
					msg[0], msg[1], msg[2]);
			}
		}
	}

	/* Send cmd_id + payload to dsp */
	err = dsp_msg(tfa, size, (const char *)msg);

	kfree(msg);

	return err;
}

enum tfa98xx_error tfa_cont_write_file(struct tfa_device *tfa,
	struct tfa_file_dsc *file, int vstep_idx, int vstep_msg_idx)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	struct tfa_header *hdr = (struct tfa_header *)file->data;
	enum tfa_header_type type;
	int size, i;
	char sub_ver_string[8] = {0};
	int subversion = 0;
	char *data_buf;
	int temp_index;

	if (tfa->verbose)
		tfa_cont_show_header(hdr);

	type = (enum tfa_header_type)hdr->id;
	if ((type == msg_hdr)
		|| ((type == volstep_hdr) && (2 == 2))) {
		sub_ver_string[0] = hdr->subversion[0];
		sub_ver_string[1] = hdr->subversion[1];
		sub_ver_string[2] = '\0';

		sscanf(sub_ver_string, "%d", &subversion);

		subversion = 0; // TEMPORARY

		if ((subversion > 0)
			&& (((hdr->customer[0]) == 'A')
			&& ((hdr->customer[1]) == 'P')
			&& ((hdr->customer[2]) == 'I')
			&& ((hdr->customer[3]) == 'V'))) {
			if ((tfa->fw_itf_ver[2] != hdr->customer[4])
				|| (tfa->fw_itf_ver[1] != hdr->customer[5])
				|| ((tfa->fw_itf_ver[0] >> 6) & 0x03) != hdr->customer[6]) {
				pr_err("%s: Error: Expected FW API version = %d.%d.%d, Msg File version: %d.%d.%d\n",
					__func__,
					(tfa->fw_itf_ver[2]) & 0xff,
					(tfa->fw_itf_ver[1]) & 0xff,
					(tfa->fw_itf_ver[0] >> 6) & 0x03,
					hdr->customer[4],
					hdr->customer[5],
					hdr->customer[6]);
				return TFA98XX_ERROR_BAD_PARAMETER;
			}
		}
	}

	switch (type) {
	case msg_hdr: /* generic DSP message */
		size = hdr->size - sizeof(struct tfa_msg_file);

		/* write temp stored in driver */
		data_buf = (char *)((struct tfa_msg_file *)hdr)->data;
		/* SetChipTempSelect */
		if (((data_buf[1] & 0xf0) == 0x80)
			&& data_buf[2] == FW_PAR_ID_SET_CHIP_TEMP_SELECTOR
			&& tfa->temp != 0xffff) {
			/* set index by skipping command and two parameters */
			temp_index = (1 + 2 + tfa->dev_idx) * 3;
			pr_info("%s: @%d temp in msg 0x%02x%02x%02x",
				__func__, temp_index,
				data_buf[temp_index],
				data_buf[temp_index + 1],
				data_buf[temp_index + 2]);

			data_buf[temp_index]
				= (char)((tfa->temp & 0xff0000) >> 16);
			data_buf[temp_index + 1]
				= (char)((tfa->temp & 0x00ff00) >> 8);
			data_buf[temp_index + 2]
				= (char)(tfa->temp & 0x0000ff);

			pr_info("%s: set temp from driver 0x%02x%02x%02x",
				__func__, data_buf[temp_index],
				data_buf[temp_index + 1],
				data_buf[temp_index + 2]);
		}

		err = dsp_msg(tfa, size,
			(const char *)((struct tfa_msg_file *)hdr)->data);

		/* Reset bypass if writing msg files */
		if (err == TFA98XX_ERROR_OK)
			tfa->is_bypass = 0;
		break;
	case volstep_hdr:
		err = tfa_cont_write_vstepMax2(tfa,
				(struct tfa_volume_step_max2_file *)hdr,
				vstep_idx, vstep_msg_idx);

		/* If writing the vstep was succesful, set new current vstep */

		/* Reset bypass if writing vstep files */
		if (err == TFA98XX_ERROR_OK)
			tfa->is_bypass = 0;
		break;
	case speaker_hdr:
		{
			/* Remove header and xml_id */
			size = hdr->size - sizeof(struct tfa_spk_header)
				- sizeof(struct tfa_fw_ver);

			err = dsp_msg(tfa, size,
				(const char *)
				(((struct tfa_speaker_file *)hdr)->data
				+ (sizeof(struct tfa_fw_ver))));
		}
		break;
	case preset_hdr:
		size = hdr->size - sizeof(struct tfa_preset_file);
		err = tfa98xx_dsp_write_preset(tfa, size,
			(const unsigned char *)
			((struct tfa_preset_file *)hdr)->data);
		break;
	case equalizer_hdr:
		err = tfa_cont_write_filterbank(tfa,
			((struct tfa_equalizer_file *)hdr)->filter);
		break;
	case patch_hdr:
		size = hdr->size - sizeof(struct tfa_patch_file);
		/* total length */
		err = tfa_dsp_patch(tfa, size,
			(const unsigned char *)
			((struct tfa_patch_file *)hdr)->data);
		break;
	case config_hdr:
		size = hdr->size - sizeof(struct tfa_config_file);
		err = tfa98xx_dsp_write_config(tfa, size,
			(const unsigned char *)
			((struct tfa_config_file *)hdr)->data);
		break;
	case drc_hdr:
		if (hdr->version[0] == NXPTFA_DR3_VERSION) {
			/* Size is total size - hdrsize(36) - xmlversion(3) */
			size = hdr->size - sizeof(struct tfa_drc_file2);
			err = tfa_cont_write_drc_file(tfa, size,
				((struct tfa_drc_file2 *)hdr)->data);
		} else {
			/*
			 * The DRC file is split as:
			 * 36 bytes for generic header
			 * (customer, application, and type)
			 * 127x3 (381) bytes first block contains
			 *             the device and sample rate
			 *             independent settings
			 * 127x3 (381) bytes block
			 *             the device and sample rate
			 *             specific values.
			 * The second block can always be recalculated
			 * from the first block,
			 * if vlsCal and the sample rate are known.
			 */
			/* = hdr->size - sizeof(struct tfa_drc_file); */
			size = 381; /* fixed size for first block */

			/* 381 is done to only send 2nd part of the drc block */
			err = tfa98xx_dsp_write_drc(tfa, size,
				((const unsigned char *)
				((struct tfa_drc_file *)hdr)->data + 381));
		}
		break;
	case info_hdr:
		/* Ignore */
		break;
	default:
		pr_err("Header is of unknown type: 0x%x\n", type);
		return TFA98XX_ERROR_BAD_PARAMETER;
	}

	return err;
}

int tfa_cont_get_idx(struct tfa_device *tfa)
{
	struct tfa_device_list *dev = NULL;
	int i;

	for (i = 0; i < tfa->cnt->ndev; i++) {
		dev = tfa_cont_device(tfa->cnt, i);
		if (dev->dev == tfa->slave_address)
			break;
	}

	if (i == tfa->cnt->ndev)
		return TFA_ERROR;

	return i;
}

enum tfa98xx_error tfa_run_write_bitfield(struct tfa_device *tfa,
	struct tfa_bitfield bf)
{
	enum tfa98xx_error error;
	uint16_t value;
	union {
		uint16_t field;
		struct tfa_bf_enum bf_enum;
	} bf_uni;

	value = bf.value;
	bf_uni.field = bf.field;
	error = tfa_set_bf(tfa, bf_uni.field, value);

	return error;
}

static enum tfa98xx_error tfa_run_write_dsp_mem(struct tfa_device *tfa,
	struct tfa_dsp_mem *cfmem)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	int i;

	for (i = 0; i < cfmem->size; i++) {
		if (tfa->verbose)
			pr_debug("dsp mem (%d): 0x%02x=0x%04x\n",
				cfmem->type, cfmem->address, cfmem->words[i]);

		error = mem_write(tfa, cfmem->address++,
			cfmem->words[i], cfmem->type);
		if (error)
			return error;
	}

	return error;
}

static enum tfa98xx_error tfa_run_write_filter(struct tfa_device *tfa,
	union tfa_cont_biquad *bq)
{
	enum tfa98xx_error error = TFA98XX_ERROR_OK;
	enum tfa98xx_dmem dmem;
	uint16_t address;
	uint8_t data[3 * 3 + sizeof(bq->aa.bytes)];
	int i, channel = 0, runs = 1;
	int8_t saved_index = bq->aa.index; /* This is used to set back index */

	/* Channel=1 is primary, Channel=2 is secondary*/
	if (bq->aa.index > 100) {
		bq->aa.index -= 100;
		channel = 2;
	} else if (bq->aa.index > 50) {
		bq->aa.index -= 50;
		channel = 1;
	} else ;

	if (tfa->verbose) {
		if (channel == 2)
			pr_debug("filter[%d,S]", bq->aa.index);
		else if (channel == 1)
			pr_debug("filter[%d,P]", bq->aa.index);
		else
			pr_debug("filter[%d]", bq->aa.index);
	}

	for (i = 0; i < runs; i++) {
		if (runs == 2)
			channel++;

		/* get the target address for the filter on this device */
		dmem = tfa98xx_filter_mem(tfa, bq->aa.index, &address, channel);
		if (dmem == TFA98XX_DMEM_ERR) {
			if (tfa->verbose)
				pr_debug("Warning: XFilter settings are applied via msg file (ini filter[x] format is skipped).\n");
			/* Don't exit with an error here,
			 * We could continue without problems
			 */
			return TFA98XX_ERROR_OK;
		}

		/* send a DSP memory message
		 * that targets the devices specific memory for the filter
		 * msg params: which_mem, start_offset, num_words
		 */
		memset(data, 0, 3 * 3);
		data[2] = dmem; /* output[0] = which_mem */
		data[4] = address >> 8; /* output[1] = start_offset */
		data[5] = address & 0xff;
		data[8] = sizeof(bq->aa.bytes) / 3; /*output[2] = num_words */
		/* payload */
		memcpy(&data[9], bq->aa.bytes, sizeof(bq->aa.bytes));

		error = tfa_dsp_cmd_id_write(tfa,
				MODULE_FRAMEWORK, FW_PAR_ID_SET_MEMORY,
				sizeof(data), data);
	}

	/* Because we can load the same filters multiple times
	 * For example: When we switch profile we re-write in operating mode.
	 * We then need to remember the index (primary, secondary or both)
	 */
	bq->aa.index = saved_index;

	return error;
}

static enum tfa98xx_error tfa_run_write_register
(struct tfa_device *tfa, struct tfa_reg_patch *reg)
{
	enum tfa98xx_error error;
	uint16_t value, newvalue;

	if (tfa->verbose)
		pr_debug("register: 0x%02x=0x%04x (msk=0x%04x)\n",
			reg->address, reg->value, reg->mask);

	error = reg_read(tfa, reg->address, &value);
	if (error)
		return error;

	value &= ~reg->mask;
	newvalue = reg->value & reg->mask;

	value |= newvalue;
	error = reg_write(tfa, reg->address, value);

	return error;
}

enum tfa98xx_error tfa_cont_write_regs_dev(struct tfa_device *tfa)
{
	struct tfa_device_list *dev
		= tfa_cont_device(tfa->cnt, tfa->dev_idx);
	struct tfa_bitfield *bit_f;
	int i;
	enum tfa98xx_error err = TFA98XX_ERROR_OK;

	if (!dev)
		return TFA98XX_ERROR_BAD_PARAMETER;

	/* process the list until a patch, file of profile is encountered */
	for (i = 0; i < dev->length; i++) {
		if (dev->list[i].type == dsc_patch
			|| dev->list[i].type == dsc_file
			|| dev->list[i].type == dsc_profile)
			break;

		if (dev->list[i].type == dsc_bit_field) {
			bit_f = (struct tfa_bitfield *)
				(dev->list[i].offset + (uint8_t *)tfa->cnt);
			err = tfa_run_write_bitfield(tfa, *bit_f);
		}
		if (dev->list[i].type == dsc_register)
			err = tfa_run_write_register(tfa,
				(struct tfa_reg_patch *)
				(dev->list[i].offset + (char *)tfa->cnt));

		if (err)
			break;
	}

	return err;
}

enum tfa98xx_error  tfa_cont_write_regs_prof(struct tfa_device *tfa,
	int prof_idx)
{
	struct tfa_profile_list *prof
		= tfa_cont_get_dev_prof_list(tfa->cnt,
			tfa->dev_idx, prof_idx);
	struct tfa_bitfield *bitf;
	unsigned int i;
	enum tfa98xx_error err = TFA98XX_ERROR_OK;

	if (!prof)
		return TFA98XX_ERROR_BAD_PARAMETER;

	if (tfa->verbose)
		pr_debug("----- profile: %s (%d) -----\n",
			tfa_cont_get_string(tfa->cnt, &prof->name), prof_idx);

	/* process the list until the end of the profile or the default section */
	for (i = 0; i < prof->length; i++) {
		/* We only want to write the values before the default section when we switch profile */
		if (prof->list[i].type == dsc_default)
			break;

		if (prof->list[i].type == dsc_bit_field) {
			bitf = (struct tfa_bitfield *)
				(prof->list[i].offset + (uint8_t *)tfa->cnt);
			err = tfa_run_write_bitfield(tfa, *bitf);
		}
		if (prof->list[i].type == dsc_register)
			err = tfa_run_write_register(tfa,
				(struct tfa_reg_patch *)(prof->list[i].offset
				+ (char *)tfa->cnt));
		if (err)
			break;
	}
	return err;
}

enum tfa98xx_error tfa_cont_write_patch(struct tfa_device *tfa)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	struct tfa_device_list *dev = tfa_cont_device(tfa->cnt, tfa->dev_idx);
	struct tfa_file_dsc *file;
	struct tfa_patch_file *patchfile;
	int size, i;

	if (!dev)
		return TFA98XX_ERROR_BAD_PARAMETER;

	/* process the list until a patch is encountered */
	for (i = 0; i < dev->length; i++) {
		if (dev->list[i].type == dsc_patch) {
			file = (struct tfa_file_dsc *)
				(dev->list[i].offset + (uint8_t *)tfa->cnt);
			patchfile = (struct tfa_patch_file *)&file->data;

			if (tfa->verbose)
				tfa_cont_show_header(&patchfile->hdr);

			/* size is total length */
			size = patchfile->hdr.size
				- sizeof(struct tfa_patch_file);
			err = tfa_dsp_patch(tfa, size,
				(const unsigned char *)patchfile->data);
			if (err)
				return err;
		}
	}

	return TFA98XX_ERROR_OK;
}

static void create_dsp_buffer_msg(struct tfa_device *tfa,
	struct tfa_msg *msg, char *buffer, int *size)
{
	int i, nr = 0;

	(void)tfa;

	/* Copy cmd_id. Remember that the cmd_id is reversed */
	buffer[nr++] = msg->cmd_id[2];
	buffer[nr++] = msg->cmd_id[1];
	buffer[nr++] = msg->cmd_id[0];

	/* Copy the data to the buffer */
	for (i = 0; i < msg->msg_size; i++) {
		buffer[nr++] = (uint8_t)((msg->data[i] >> 16) & 0xffff);
		buffer[nr++] = (uint8_t)((msg->data[i] >> 8) & 0xff);
		buffer[nr++] = (uint8_t)(msg->data[i] & 0xff);
	}

	*size = nr;
}

enum tfa98xx_error tfa_cont_write_files(struct tfa_device *tfa)
{
	struct tfa_device_list *dev = tfa_cont_device(tfa->cnt, tfa->dev_idx);
	struct tfa_file_dsc *file;
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	char buffer[(MEMTRACK_MAX_WORDS * 3) + 3] = {0};
	/* every word requires 3 bytes, and 3 is the msg */
	int i, size = 0;

	if (!dev)
		return TFA98XX_ERROR_BAD_PARAMETER;

	/* process the list and write all files */
	for (i = 0; i < dev->length; i++) {
		if (dev->list[i].type == dsc_file) {
			file = (struct tfa_file_dsc *)
				(dev->list[i].offset + (uint8_t *)tfa->cnt);
			if (tfa_cont_write_file
				(tfa, file, 0, TFA_MAX_VSTEP_MSG_MARKER))
				return TFA98XX_ERROR_BAD_PARAMETER;
		}

		if (dev->list[i].type == dsc_set_input_select
			|| dev->list[i].type == dsc_set_output_select
			|| dev->list[i].type == dsc_set_program_config
			|| dev->list[i].type == dsc_set_lag_w
			|| dev->list[i].type == dsc_set_gains
			|| dev->list[i].type == dsc_set_vbat_factors
			|| dev->list[i].type == dsc_set_senses_cal
			|| dev->list[i].type == dsc_set_senses_delay
			|| dev->list[i].type == dsc_set_mb_drc
			|| dev->list[i].type == dsc_set_fw_use_case
			|| dev->list[i].type == dsc_set_vddp_config) {
			create_dsp_buffer_msg(tfa, (struct tfa_msg *)
				(dev->list[i].offset + (char *)tfa->cnt),
				buffer, &size);
			if (tfa->verbose) {
				pr_debug("command: %s=0x%02x%02x%02x\n",
					tfa_cont_get_command_string
					(dev->list[i].type),
					(unsigned char)buffer[0],
					(unsigned char)buffer[1],
					(unsigned char)buffer[2]);
			}

			err = dsp_msg(tfa, size, buffer);
		}

		if (dev->list[i].type == dsc_cmd) {
			size = *(uint16_t *)
				(dev->list[i].offset + (char *)tfa->cnt);

			err = dsp_msg(tfa, size,
				dev->list[i].offset + 2 + (char *)tfa->cnt);
			if (tfa->verbose) {
				const char *cmd_id = dev->list[i].offset
					+ 2 + (char *)tfa->cnt;

				pr_debug("Writing cmd=0x%02x%02x%02x\n",
					(uint8_t)cmd_id[0],
					(uint8_t)cmd_id[1],
					(uint8_t)cmd_id[2]);
			}
		}
		if (err != TFA98XX_ERROR_OK)
			break;

		if (dev->list[i].type == dsc_cf_mem)
			err = tfa_run_write_dsp_mem(tfa,
				(struct tfa_dsp_mem *)(dev->list[i].offset
				+ (uint8_t *)tfa->cnt));

		if (err != TFA98XX_ERROR_OK)
			break;
	}

	return err;
}

enum tfa98xx_error tfa_cont_write_files_prof(struct tfa_device *tfa,
	int prof_idx, int vstep_idx)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	struct tfa_profile_list *prof
		= tfa_cont_get_dev_prof_list(tfa->cnt,
			tfa->dev_idx, prof_idx);
	char buffer[(MEMTRACK_MAX_WORDS * 3) + 3] = {0};
	/* every word requires 3 bytes, and 3 is the msg */
	unsigned int i;
	struct tfa_file_dsc *file;
	struct tfa_patch_file *patchfile;
	int size;

	if (!prof)
		return TFA98XX_ERROR_BAD_PARAMETER;

	/* process the list and write all files */
	for (i = 0; i < prof->length; i++) {
		switch (prof->list[i].type) {
		case dsc_file:
			file = (struct tfa_file_dsc *)
				(prof->list[i].offset + (uint8_t *)tfa->cnt);
			err = tfa_cont_write_file(tfa, file,
				vstep_idx, TFA_MAX_VSTEP_MSG_MARKER);
			break;
		case dsc_patch:
			file = (struct tfa_file_dsc *)
				(prof->list[i].offset + (uint8_t *)tfa->cnt);
			patchfile = (struct tfa_patch_file *)&file->data;
			if (tfa->verbose)
				tfa_cont_show_header(&patchfile->hdr);

			/* size is total length */
			size = patchfile->hdr.size
				- sizeof(struct tfa_patch_file);
			err = tfa_dsp_patch(tfa, size,
				(const unsigned char *)patchfile->data);
			break;
		case dsc_cf_mem:
			err = tfa_run_write_dsp_mem(tfa,
				(struct tfa_dsp_mem *)(prof->list[i].offset
				+ (uint8_t *)tfa->cnt));
			break;
		case dsc_set_input_select:
		case dsc_set_output_select:
		case dsc_set_program_config:
		case dsc_set_lag_w:
		case dsc_set_gains:
		case dsc_set_vbat_factors:
		case dsc_set_senses_cal:
		case dsc_set_senses_delay:
		case dsc_set_mb_drc:
		case dsc_set_fw_use_case:
		case dsc_set_vddp_config:
			create_dsp_buffer_msg(tfa,
				(struct tfa_msg *)(prof->list[i].offset
				+ (uint8_t *)tfa->cnt), buffer, &size);
			if (tfa->verbose)
				pr_debug("command: %s=0x%02x%02x%02x \n",
					tfa_cont_get_command_string
					(prof->list[i].type),
					(unsigned char)buffer[0],
					(unsigned char)buffer[1],
					(unsigned char)buffer[2]);

			err = dsp_msg(tfa, size, buffer);
			break;
		case dsc_cmd: /* to change set-commands per profile */
			size = *(uint16_t *)
				(prof->list[i].offset
				+ (char *)tfa->cnt);
			err = dsp_msg(tfa, size,
				prof->list[i].offset
				+ 2 + (char *)tfa->cnt);

			if (tfa->verbose) {
				const char *cmd_id = (prof->list[i].offset
					+ 2 + (char *)tfa->cnt);
				pr_debug("Writing cmd=0x%02x%02x%02x\n",
					(uint8_t)cmd_id[0],
					(uint8_t)cmd_id[1],
					(uint8_t)cmd_id[2]);
			}
			break;
		default:
			/* ignore any other type */
			break;
		}
	}

	return err;
}

static enum tfa98xx_error tfa_cont_write_item
(struct tfa_device *tfa, struct tfa_desc_ptr *dsc)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	struct tfa_reg_patch *reg;
	struct tfa_mode *cas;
	struct tfa_bitfield *bitf;

	/* When no DSP should only write to HW registers. */

	switch (dsc->type) {
	case dsc_default:
	case dsc_device: /* ignore */
	case dsc_profile: /* profile list */
		break;
	case dsc_register: /* register patch */
		reg = (struct tfa_reg_patch *)(dsc->offset + (uint8_t *)tfa->cnt);
		return tfa_run_write_register(tfa, reg);
		/* pr_debug("$0x%2x=0x%02x,0x%02x\n",
		 * reg->address, reg->mask, reg->value);
		 */
		break;
	case dsc_string: /* ascii: zero terminated string */
		pr_debug(";string: %s\n", tfa_cont_get_string(tfa->cnt, dsc));
		break;
	case dsc_file: /* filename + file contents */
	case dsc_patch:
		break;
	case dsc_mode:
		cas = (struct tfa_mode *)(dsc->offset + (uint8_t *)tfa->cnt);
		if (cas->value == TFA98XX_MODE_RCV)
			tfa98xx_select_mode(tfa, TFA98XX_MODE_RCV);
		else
			tfa98xx_select_mode(tfa, TFA98XX_MODE_NORMAL);
		break;
	case dsc_cf_mem:
		err = tfa_run_write_dsp_mem(tfa,
			(struct tfa_dsp_mem *)
			(dsc->offset + (uint8_t *)tfa->cnt));
		break;
	case dsc_bit_field:
		bitf = (struct tfa_bitfield *)(dsc->offset + (uint8_t *)tfa->cnt);
		return tfa_run_write_bitfield(tfa, *bitf);
	case dsc_filter:
		return tfa_run_write_filter(tfa,
			(union tfa_cont_biquad *)
			(dsc->offset + (uint8_t *)tfa->cnt));
	}

	return err;
}

static unsigned int tfa98xx_sr_from_field(unsigned int field)
{
	switch (field) {
	case 0:
		return 8000;
	case 1:
		return 11025;
	case 2:
		return 12000;
	case 3:
		return 16000;
	case 4:
		return 22050;
	case 5:
		return 24000;
	case 6:
		return 32000;
	case 7:
		return 44100;
	case 8:
		return 48000;
	default:
		return 0;
	}
}

unsigned int tfa98xx_get_profile_sr(struct tfa_device *tfa,
	unsigned int prof_idx)
{
	struct tfa_bitfield *bitf;
	unsigned int i;
	struct tfa_device_list *dev;
	struct tfa_profile_list *prof;
	int fs_profile = -1;

	dev = tfa_cont_device(tfa->cnt, tfa->dev_idx);
	if (!dev)
		return 0;

	if (prof_idx == -1) { /* refer to prof in other device */
		if (tfa->profile != -1)
			prof_idx = tfa->profile;
	}
	prof = tfa_cont_get_dev_prof_list(tfa->cnt, tfa->dev_idx, prof_idx);
	if (!prof)
		return 0;

	/* Check profile fields first */
	for (i = 0; i < prof->length; i++) {
		if (prof->list[i].type == dsc_default)
			break;

		/* check for profile settingd (AUDFS) */
		if (prof->list[i].type == dsc_bit_field) {
			bitf = (struct tfa_bitfield *)
				(prof->list[i].offset + (uint8_t *)tfa->cnt);
			if (bitf->field == TFA_FAM(tfa, AUDFS)) {
				fs_profile = bitf->value;
				break;
			}
		}
	}

	if (tfa->verbose)
		pr_debug("%s - profile fs: 0x%x = %dHz (%d - %d)\n",
			__func__, fs_profile,
			tfa98xx_sr_from_field(fs_profile),
			tfa->dev_idx, prof_idx);

	if (fs_profile != -1)
		return tfa98xx_sr_from_field(fs_profile);

	/* Check for container default setting */
	/* process the list until a patch, file of profile is encountered */
	for (i = 0; i < dev->length; i++) {
		if (dev->list[i].type == dsc_patch
			|| dev->list[i].type == dsc_file
			|| dev->list[i].type == dsc_profile)
			break;

		if (dev->list[i].type == dsc_bit_field) {
			bitf = (struct tfa_bitfield *)
				(dev->list[i].offset + (uint8_t *)tfa->cnt);
			if (bitf->field == TFA_FAM(tfa, AUDFS)) {
				fs_profile = bitf->value;
				break;
			}
		}
		/* Ignore register case */
	}

	if (tfa->verbose)
		pr_debug("%s - default fs: 0x%x = %dHz (%d - %d)\n",
			__func__, fs_profile,
			tfa98xx_sr_from_field(fs_profile),
			tfa->dev_idx, prof_idx);

	if (fs_profile != -1)
		return tfa98xx_sr_from_field(fs_profile);

	return 48000; /* default of HW */
}

static enum tfa98xx_error get_sample_rate_info(struct tfa_device *tfa,
	struct tfa_profile_list *prof, struct tfa_profile_list *previous_prof,
	int fs_previous_profile)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	struct tfa_bitfield *bitf;
	unsigned int i;
	int fs_default_profile = 8;	/* default is 48kHz */
	int fs_next_profile = 8;	/* default is 48kHz */

	/* ---------- default settings previous profile ---------- */
	for (i = 0; i < previous_prof->length; i++) {
		/* Search for the default section */
		if (i == 0) {
			while (previous_prof->list[i].type != dsc_default
				&& i < previous_prof->length) {
				i++;
			}
			i++;
		}

		/* Only if we found the default section search for AUDFS */
		if (i < previous_prof->length) {
			if (previous_prof->list[i].type == dsc_bit_field) {
				bitf = (struct tfa_bitfield *)
					(previous_prof->list[i].offset
					+ (uint8_t *)tfa->cnt);
				if (bitf->field == TFA_FAM(tfa, AUDFS)) {
					fs_default_profile = bitf->value;
					break;
				}
			}
		}
	}

	/* ---------- settings next profile ---------- */
	for (i = 0; i < prof->length; i++) {
		/* We only want to write the values before the default section */
		if (prof->list[i].type == dsc_default)
			break;
		/* search for AUDFS */
		if (prof->list[i].type == dsc_bit_field) {
			bitf = (struct tfa_bitfield *)
				(prof->list[i].offset + (uint8_t *)tfa->cnt);
			if (bitf->field == TFA_FAM(tfa, AUDFS)) {
				fs_next_profile = bitf->value;
				break;
			}
		}
	}

	/* Enable if needed for debugging!
	 * if (tfa98xx_cnt_verbose) {
	 *  pr_debug("sample rate from the previous profile: %d\n",
	 *   fs_previous_profile);
	 *  pr_debug("sample rate in the default section: %d\n",
	 *   fs_default_profile);
	 *  pr_debug("sample rate for the next profile: %d\n",
	 *   fs_next_profile);
	 * }
	 */

	if (fs_next_profile != fs_default_profile) {
		if (tfa->verbose)
			pr_debug("Writing delay tables for AUDFS=%d \n",
				fs_next_profile);

		/* If AUDFS from the next profile is not the same as
		 * AUDFS from the default we need to write new delay tables
		 */
		err = tfa98xx_dsp_write_tables(tfa, fs_next_profile);
	}
	else if (fs_default_profile != fs_previous_profile) {
		if (tfa->verbose)
			pr_debug("Writing delay tables for AUDFS=%d \n",
				fs_default_profile);

		/* But if we do not have a new AUDFS in the next profile and
		 * AUDFS from the default profile is not the same as AUDFS
		 * from the previous profile we also need to write new delay tables
		 */
		err = tfa98xx_dsp_write_tables(tfa, fs_default_profile);
	}

	return err;
}

enum tfa98xx_error tfa_cont_write_profile(struct tfa_device *tfa,
	int prof_idx, int vstep_idx)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	struct tfa_profile_list *prof
		= tfa_cont_get_dev_prof_list(tfa->cnt, tfa->dev_idx, prof_idx);
	struct tfa_profile_list *previous_prof
		= tfa_cont_get_dev_prof_list(tfa->cnt,
			tfa->dev_idx, tfa_dev_get_swprof(tfa));
	char buffer[(MEMTRACK_MAX_WORDS * 4) + 4] = {0};
	/* every word requires 3 or 4 bytes, and 3 or 4 is the msg */
	unsigned int i, k = 0, j = 0, tries = 0;
	struct tfa_file_dsc *file;
	int size = 0, ready, fs_previous_profile = 8; /* default fs is 48kHz */

	if (!prof || !previous_prof) {
		pr_err("Error trying to get the (previous) swprofile\n");
		return TFA98XX_ERROR_BAD_PARAMETER;
	}

	if (tfa->verbose)
		tfa98xx_trace_printk("device:%s profile:%s vstep:%d\n",
			tfa_cont_device_name(tfa->cnt, tfa->dev_idx),
			tfa_cont_profile_name(tfa->cnt, tfa->dev_idx,
				prof_idx),
			vstep_idx);

	/* We only make a power cycle when the profiles are not in the same group */
	if (prof->group == previous_prof->group && prof->group != 0) {
		if (tfa->verbose)
			pr_debug("The new profile (%s) is in the same group as the current profile (%s)\n",
				tfa_cont_get_string(tfa->cnt, &prof->name),
				tfa_cont_get_string(tfa->cnt,
					&previous_prof->name));
	} else {
		/* mute */
		err = tfa_run_mute(tfa);
		if (err)
			return err;

		/* Get current sample rate before we start switching */
		fs_previous_profile = TFA_GET_BF(tfa, AUDFS);

		/* clear SBSL to make sure we stay in initCF state */
		TFA_SET_BF_VOLATILE(tfa, SBSL, 0);

		/* When we switch profile we first power down the subsystem
		 * This should only be done when we are in operating mode
		 */
		if (((2 == 2)
			&& (TFA_GET_BF(tfa, MANSTATE) >= 6))
			|| (2 != 2)) {
			err = tfa98xx_powerdown(tfa, 1);
			if (err)
				return err;

			/* Wait until we are in PLL powerdown */
			do {
				err = tfa98xx_dsp_system_stable(tfa, &ready);
				if (!ready)
					break;
				else
					msleep_interruptible(10); /* wait 10ms to avoid busload */
				tries++;
			} while (tries <= 100);

			if (tries > 100) {
				pr_debug("Wait for PLL powerdown timed out!\n");
				return TFA98XX_ERROR_STATE_TIMED_OUT;
			}
		} else {
			pr_debug("No need to go to powerdown now\n");
		}
	}

	/* set all bitfield settings */
	/* First set all default settings */
	if (tfa->verbose)
		pr_debug("---------- default settings profile: %s (%d) ----------\n",
			tfa_cont_get_string(tfa->cnt,
				&previous_prof->name), tfa_dev_get_swprof(tfa));

	err = show_current_state(tfa);

	/* Loop profile length */
	for (i = 0; i < previous_prof->length; i++) {
		/* Search for the default section */
		if (i == 0) {
			while (previous_prof->list[i].type != dsc_default
				&& i < previous_prof->length)
				i++;
			i++;
		}

		/* Only if we found the default section try writing the items */
		if (i < previous_prof->length) {
			if (tfa_cont_write_item(tfa,
				&previous_prof->list[i])
				!= TFA98XX_ERROR_OK)
				return TFA98XX_ERROR_BAD_PARAMETER;
		}
	}

	if (tfa->verbose)
		pr_debug("---------- new settings profile: %s (%d) ----------\n",
			tfa_cont_get_string(tfa->cnt,
			&prof->name), prof_idx);

	/* set new settings */
	for (i = 0; i < prof->length; i++) {
		/* Remember where we currently are with writing items*/
		j = i;

		/* We only want to write the values before the default section when we switch profile */
		/* process and write all non-file items */
		switch (prof->list[i].type) {
		case dsc_file:
		case dsc_patch:
		case dsc_set_input_select:
		case dsc_set_output_select:
		case dsc_set_program_config:
		case dsc_set_lag_w:
		case dsc_set_gains:
		case dsc_set_vbat_factors:
		case dsc_set_senses_cal:
		case dsc_set_senses_delay:
		case dsc_set_mb_drc:
		case dsc_set_fw_use_case:
		case dsc_set_vddp_config:
		case dsc_cmd:
		case dsc_filter:
		case dsc_default:
			/* When one of these files are found, we exit */
			i = prof->length;
			break;
		default:
			err = tfa_cont_write_item(tfa, &prof->list[i]);
			if (err != TFA98XX_ERROR_OK)
				return TFA98XX_ERROR_BAD_PARAMETER;
			break;
		}
	}

	if (prof->group != previous_prof->group || prof->group == 0) {
		TFA_SET_BF_VOLATILE(tfa, MANSCONF, 1);

		/* Leave powerdown state */
		err = tfa_cf_powerup(tfa);
		if (err)
			return err;

		err = show_current_state(tfa);

		{
			/* Reset SBSL to 0 (workaround of enbl_powerswitch=0) */
			TFA_SET_BF_VOLATILE(tfa, SBSL, 0);
			/* Sending commands to DSP need to make sure RST is 0
			 * (otherwise we get no response)
			 */
			TFA_SET_BF(tfa, RST, 0);
		}
	}

	/* Check if there are sample rate changes */
	err = get_sample_rate_info(tfa, prof,
		previous_prof, fs_previous_profile);
	if (err)
		return err;

	/* Write files from previous profile (default section)
	 * Should only be used for the patch&trap patch (file)
	 */
	{
		{
			for (i = 0; i < previous_prof->length; i++) {
				/* Search for the default section */
				if (i == 0) {
					while (previous_prof->list[i].type
						!= dsc_default
						&& i < previous_prof->length) {
						i++;
					}
					i++;
				}

				/* Only if we found the default section try writing the file */
				if (i < previous_prof->length) {
					char type = previous_prof->list[i].type;

					if (type == dsc_file || type == dsc_patch) {
						/* Only write this once */
						if (tfa->verbose && k == 0) {
							pr_debug("---------- files default profile: %s (%d) ----------\n",
								tfa_cont_get_string(tfa->cnt,
									&previous_prof->name),
								prof_idx);
							k++;
						}
						file = (struct tfa_file_dsc *)
							(previous_prof->list[i].offset
							+ (uint8_t *)tfa->cnt);
						err = tfa_cont_write_file(tfa,
							file, vstep_idx,
							TFA_MAX_VSTEP_MSG_MARKER);
					}
				}
			}
		}

		if (tfa->verbose)
			pr_debug("---------- files new profile: %s (%d) ----------\n",
				tfa_cont_get_string(tfa->cnt, &prof->name), prof_idx);
	}

	/* write everything until end or the default section starts
	 * Start where we currenly left */
	for (i = j; i < prof->length; i++) {
		/* We only want to write the values before the default section when we switch profile */

		if (prof->list[i].type == dsc_default)
			break;

		switch (prof->list[i].type) {
		case dsc_file:
		case dsc_patch:
			/* For tiberius stereo 1 device does not have a dsp! */

			file = (struct tfa_file_dsc *)
				(prof->list[i].offset
				+ (uint8_t *)tfa->cnt);
			err = tfa_cont_write_file(tfa, file,
				vstep_idx, TFA_MAX_VSTEP_MSG_MARKER);
			break;
		case dsc_set_input_select:
		case dsc_set_output_select:
		case dsc_set_program_config:
		case dsc_set_lag_w:
		case dsc_set_gains:
		case dsc_set_vbat_factors:
		case dsc_set_senses_cal:
		case dsc_set_senses_delay:
		case dsc_set_mb_drc:
		case dsc_set_fw_use_case:
		case dsc_set_vddp_config:
			/* For tiberius stereo 1 device does not have a dsp! */

			create_dsp_buffer_msg(tfa,
				(struct tfa_msg *)
				(prof->list[i].offset
				+ (char *)tfa->cnt),
				buffer, &size);
			err = dsp_msg(tfa, size, buffer);

			if (tfa->verbose)
				pr_debug("command: %s=0x%02x%02x%02x\n",
					tfa_cont_get_command_string
					(prof->list[i].type),
					(unsigned char)buffer[0],
					(unsigned char)buffer[1],
					(unsigned char)buffer[2]);
			break;
		case dsc_cmd:
			/* For tiberius stereo 1 device does not have a dsp! */

			size = *(uint16_t *)
				(prof->list[i].offset
				+ (char *)tfa->cnt);
			err = dsp_msg(tfa, size,
			
				prof->list[i].offset
				+ 2 + (char *)tfa->cnt);
			if (tfa->verbose) {
				const char *cmd_id
					= prof->list[i].offset
					+ 2 + (char *)tfa->cnt;
				pr_debug("Writing cmd=0x%02x%02x%02x\n",
					(uint8_t)cmd_id[0],
					(uint8_t)cmd_id[1],
					(uint8_t)cmd_id[2]);
			}
			break;
		default:
			/* This allows us to write bitfield, registers or xmem after files */
			if (tfa_cont_write_item(tfa,
				&prof->list[i]) != TFA98XX_ERROR_OK)
				return TFA98XX_ERROR_BAD_PARAMETER;
			break;
		}

		if (err != TFA98XX_ERROR_OK)
			return err;
	}

	/* TODO?: put SetRe25C message to indicate all messages are send */
	{ /* check only master device */
		/* put SetRe25C message to indicate all messages are send */
		pr_info("%s: tfa_set_calibration_values\n", __func__);
		tfa_dev_set_swprof(tfa, (unsigned short)prof_idx);
		err = TFA98XX_ERROR_OK;
		if (err)
			pr_info("%s: set calibration values error = %d\n",
				__func__, err);
	}

	if ((prof->group != previous_prof->group
		|| prof->group == 0)
		&& (2 == 2)) {
		if (TFA_GET_BF(tfa, REFCKSEL) == 0)
			/* set SBSL to go to operation mode */
			TFA_SET_BF_VOLATILE(tfa, SBSL, 1);
	}

	return err;
}

enum tfa98xx_error tfa_cont_write_files_vstep(struct tfa_device *tfa,
	int prof_idx, int vstep_idx)
{
	struct tfa_profile_list *prof
		= tfa_cont_get_dev_prof_list(tfa->cnt, tfa->dev_idx, prof_idx);
	unsigned int i;
	struct tfa_file_dsc *file;
	struct tfa_header *hdr;
	enum tfa_header_type type;
	enum tfa98xx_error err = TFA98XX_ERROR_OK;

	if (!prof)
		return TFA98XX_ERROR_BAD_PARAMETER;

	if (tfa->verbose)
		tfa98xx_trace_printk("device:%s profile:%s vstep:%d\n",
			tfa_cont_device_name(tfa->cnt, tfa->dev_idx),
			tfa_cont_profile_name(tfa->cnt, tfa->dev_idx,
				prof_idx),
			vstep_idx);

	/* write vstep file only! */
	for (i = 0; i < prof->length; i++) {
		if (prof->list[i].type == dsc_file) {
			file = (struct tfa_file_dsc *)
				(prof->list[i].offset + (uint8_t *)tfa->cnt);
			hdr = (struct tfa_header *)file->data;
			type = (enum tfa_header_type)hdr->id;

			switch (type) {
			case volstep_hdr:
				if (tfa_cont_write_file(tfa, file,
					vstep_idx, TFA_MAX_VSTEP_MSG_MARKER))
					return TFA98XX_ERROR_BAD_PARAMETER;
				break;
			default:
				break;
			}
		}
	}

	return err;
}

char *tfa_cont_get_string(struct tfa_container *cnt, struct tfa_desc_ptr *dsc)
{
	if (dsc->type != dsc_string)
		return "Undefined string";

	return dsc->offset + (char *)cnt;
}

char *tfa_cont_get_command_string(uint32_t type)
{
	if (type == dsc_set_input_select)
		return "SetInputSelector";
	else if (type == dsc_set_output_select)
		return "SetOutputSelector";
	else if (type == dsc_set_program_config)
		return "SetProgramConfig";
	else if (type == dsc_set_lag_w)
		return "SetLagW";
	else if (type == dsc_set_gains)
		return "SetGains";
	else if (type == dsc_set_vbat_factors)
		return "SetvBatFactors";
	else if (type == dsc_set_senses_cal)
		return "SetSensesCal";
	else if (type == dsc_set_senses_delay)
		return "SetSensesDelay";
	else if (type == dsc_set_mb_drc)
		return "SetMBDrc";
	else if (type == dsc_set_fw_use_case)
		return "SetFwkUseCase";
	else if (type == dsc_set_vddp_config)
		return "SetVddpConfig";
	else if (type == dsc_filter)
		return "filter";
	else
		return "Undefined string";
}

char *tfa_cont_device_name(struct tfa_container *cnt, int dev_idx)
{
	struct tfa_device_list *dev;

	dev = tfa_cont_device(cnt, dev_idx);
	if (dev == NULL)
		return "!ERROR!";

	return tfa_cont_get_string(cnt, &dev->name);
}

char *tfa_cont_profile_name(struct tfa_container *cnt,
	int dev_idx, int prof_idx)
{
	struct tfa_profile_list *prof = NULL;

	/* the Nth profiles for this device */
	prof = tfa_cont_get_dev_prof_list(cnt, dev_idx, prof_idx);

	/* If the index is out of bound */
	if (prof == NULL)
		return "NONE";

	return tfa_cont_get_string(cnt, &prof->name);
}

struct tfa_device_list *tfa_cont_device(struct tfa_container *cnt,
	int dev_idx)
{
	return tfa_cont_get_dev_list(cnt, dev_idx);
}

int tfa_cont_crc_check_container(struct tfa_container *cont)
{
	uint8_t *base;
	size_t size;
	uint32_t crc;

	base = (uint8_t *)&cont->crc + 4;
	/* ptr to bytes following the CRC field */
	size = (size_t)(cont->size - (base - (uint8_t *)cont));
	/* nr of bytes following the CRC field */
	crc = ~crc32_le(~0u, base, size);

	return crc != cont->crc;
}

enum tfa98xx_error tfa98xx_factory_trimmer(struct tfa_device *tfa)
{
	return (tfa->dev_ops.factory_trimmer)(tfa);
}

enum tfa98xx_error tfa_set_filters(struct tfa_device *tfa, int prof_idx)
{
	enum tfa98xx_error err = TFA98XX_ERROR_OK;
	struct tfa_profile_list *prof
		= tfa_cont_get_dev_prof_list(tfa->cnt, tfa->dev_idx, prof_idx);
	unsigned int i;

	if (!prof)
		return TFA98XX_ERROR_BAD_PARAMETER;

	/* If we are in powerdown there is no need to set filters */
	if (TFA_GET_BF(tfa, PWDN) == 1)
		return TFA98XX_ERROR_OK;

	/* loop the profile to find filter settings */
	for (i = 0; i < prof->length; i++) {
		/* write values before default section */
		if (prof->list[i].type == dsc_default)
			break;

		/* write all filter settings */
		if (prof->list[i].type == dsc_filter) {
			if (tfa_cont_write_item(tfa,
				&prof->list[i]) != TFA98XX_ERROR_OK)
				return err;
		}
	}

	return err;
}

