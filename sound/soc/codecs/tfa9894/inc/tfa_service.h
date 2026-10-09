/* Copyright 2014-2017 NXP Semiconductors. See LICENSE.nxp.txt. */
#ifndef TFA9894_SERVICE_H
#define TFA9894_SERVICE_H
#include <linux/types.h>
#include "tfa.h"
enum tfa98xx_error {
	TFA_ERROR = -1,
	TFA98XX_ERROR_OK = 0,
	TFA98XX_ERROR_DEVICE,			/* 1. in sync with tfa_error */
	TFA98XX_ERROR_BAD_PARAMETER,	/* 2. */
	TFA98XX_ERROR_FAIL,             /* 3. generic failure, avoid mislead */
	TFA98XX_ERROR_NO_CLOCK,         /* 4. no clock detected */
	TFA98XX_ERROR_STATE_TIMED_OUT,	/* 5. */
	TFA98XX_ERROR_DSP_NOT_RUNNING,	/* 6. communication with DSP failed */
	TFA98XX_ERROR_AMPON,            /* 7. amp is still running */
	TFA98XX_ERROR_NOT_OPEN,	        /* 8. the given handle is not open */
	TFA98XX_ERROR_IN_USE,	        /* 9. too many handles */
	TFA98XX_ERROR_BUFFER_TOO_SMALL, /* 10. if a buffer is too small */
	/* the expected response did not occur within the expected time */
	TFA98XX_ERROR_BUFFER_RPC_BASE = 100,
	TFA98XX_ERROR_RPC_BUSY = 101,
	TFA98XX_ERROR_RPC_MOD_ID = 102,
	TFA98XX_ERROR_RPC_PARAM_ID = 103,
	TFA98XX_ERROR_RPC_INVALID_CC = 104,
	TFA98XX_ERROR_RPC_INVALID_SEQ = 105,
	TFA98XX_ERROR_RPC_INVALID_PARAM = 106,
	TFA98XX_ERROR_RPC_BUFFER_OVERFLOW = 107,
	TFA98XX_ERROR_RPC_CALIB_BUSY = 108,
	TFA98XX_ERROR_RPC_CALIB_FAILED = 109,
	TFA98XX_ERROR_NOT_IMPLEMENTED,
	TFA98XX_ERROR_NOT_SUPPORTED,
	TFA98XX_ERROR_I2C_FATAL,	/* Fatal I2C error occurred */
	/* Nonfatal I2C error, and retry count reached */
	TFA98XX_ERROR_I2C_NON_FATAL,
	TFA98XX_ERROR_OTHER = 1000
};
enum tfa98xx_status_id {
	TFA98XX_DSP_NOT_RUNNING = -1,
	/* No response from DSP */
	TFA98XX_I2C_REQ_DONE = 0,
	/* Request executed correctly and result,
	 * if any, is available for download
	 */
	TFA98XX_I2C_REQ_BUSY = 1,
	/* Request is being processed, just wait for result */
	TFA98XX_I2C_REQ_INVALID_M_ID = 2,
	/* Provided M-ID does not fit in valid rang [0..2] */
	TFA98XX_I2C_REQ_INVALID_P_ID = 3,
	/* Provided P-ID isn�t valid in the given M-ID context */
	TFA98XX_I2C_REQ_INVALID_CC = 4,
	/* Invalid channel configuration bits (SC|DS|DP|DC) combination */
	TFA98XX_I2C_REQ_INVALID_SEQ = 5,
	/* Invalid sequence of commands,
	 * in case the DSP expects some commands in a specific order
	 */
	TFA98XX_I2C_REQ_INVALID_PARAM = 6,
	/* Generic error */
	TFA98XX_I2C_REQ_BUFFER_OVERFLOW = 7,
	/* I2C buffer has overflowed:
	 * host has sent too many parameters,
	 * memory integrity is not guaranteed
	 */
	TFA98XX_I2C_REQ_CALIB_BUSY = 8,
	/* Calibration not finished */
	TFA98XX_I2C_REQ_CALIB_FAILED = 9
	/* Calibration failed */
};
enum tfa98xx_mode {
	TFA98XX_MODE_NORMAL = 0,
	TFA98XX_MODE_RCV
};
enum tfa98xx_mute {
	TFA98XX_MUTE_OFF,
	TFA98XX_MUTE_DIGITAL,
	TFA98XX_MUTE_AMPLIFIER
};
struct tfa_msg {
	uint8_t msg_size;
	unsigned char cmd_id[3];
	int data[9];
};
enum tfa98xx_dmem {
	TFA98XX_DMEM_ERR = -1,
	TFA98XX_DMEM_PMEM = 0,
	TFA98XX_DMEM_XMEM = 1,
	TFA98XX_DMEM_YMEM = 2,
	TFA98XX_DMEM_IOMEM = 3,
};
#include "tfa_device.h"
#define MEMTRACK_MAX_WORDS           150
#define FW_VAR_API_VERSION          (521)
#define TFA98XX_WAITRESULT_NTRIES		40
#define TFA98XX_PRESET_LENGTH			87
#define TFA98XX_BIQUAD_NUM	10
#define TFA_FAM(tfa, field) TFA9894_BF_##field
#define TFA_FAM_FW(tfa, field) TFA2_FW_##field
#define TFA_SET_BF(tfa, field, value) tfa_set_bf(tfa, TFA_FAM(tfa, field), value)
#define TFA_SET_BF_VOLATILE(tfa, field, value) tfa_set_bf_volatile(tfa, TFA_FAM(tfa, field), value)
#define TFA_GET_BF(tfa, field) tfa_get_bf(tfa, TFA_FAM(tfa, field))
#define TFA_SET_BF_VALUE(tfa, field, value, reg) tfa_set_bf_value(TFA_FAM(tfa, field), value, reg)
#define TFA_GET_BF_VALUE(tfa, field, reg) tfa_get_bf_value(TFA_FAM(tfa, field), reg)
#define TFA_WRITE_REG(tfa, field, value) tfa_write_reg(tfa, TFA_FAM(tfa, field), value)
#define TFA_READ_REG(tfa, field) tfa_read_reg(tfa, TFA_FAM(tfa, field))
enum tfa_header_type;
struct tfa_bitfield;
struct tfa_container;
struct tfa_desc_ptr;
struct tfa_device;
struct tfa_device_list;
struct tfa_device_ops;
struct tfa_file_dsc;
struct tfa_filter;
struct tfa_header;
struct tfa_profile_list;
void tfa_set_query_info(struct tfa_device *tfa);
enum tfa98xx_dmem tfa98xx_filter_mem(struct tfa_device *tfa,
	int filter_index, unsigned short *address, int channel);
enum tfa98xx_error tfa98xx_dsp_reset(struct tfa_device *tfa, int state);
enum tfa98xx_error tfa98xx_dsp_system_stable(struct tfa_device *tfa,
	int *ready);
enum tfa98xx_error tfa98xx_auto_copy_mtp_to_iic(struct tfa_device *tfa);
enum tfa98xx_error tfa98xx_faim_protect(struct tfa_device *tfa, int state);
enum tfa98xx_error tfa98xx_init(struct tfa_device *tfa);
enum tfa98xx_error tfa98xx_dsp_write_tables(struct tfa_device *tfa,
	int sample_rate);
enum tfa98xx_error tfa98xx_set_osc_powerdown(struct tfa_device *tfa, int state);
enum tfa98xx_error tfa98xx_update_lpm(struct tfa_device *tfa, int state);
enum tfa98xx_error tfa98xx_get_mtp(struct tfa_device *tfa, uint16_t *value);
void tfa98xx_key2(struct tfa_device *tfa, int lock);
void tfa2_manual_mtp_cpy(struct tfa_device *tfa,
	uint16_t reg_row_to_keep, uint16_t reg_row_to_set, uint8_t row)
;
enum tfa98xx_error tfa98xx_set_mtp(struct tfa_device *tfa,
	uint16_t value, uint16_t mask);
enum tfa98xx_error
tfa98xx_set_mute(struct tfa_device *tfa, enum tfa98xx_mute mute);
enum tfa98xx_error
tfa_dsp_patch(struct tfa_device *tfa, int patch_length,
	const unsigned char *patch_bytes);
enum tfa98xx_error
tfa98xx_wait_result(struct tfa_device *tfa, int wait_retry_count);
void tfa98xx_convert_bytes2data(int num_bytes,
	const unsigned char bytes[], int data[]);
void tfa98xx_convert_data2bytes(int num_data, const int data[],
	unsigned char bytes[]);
enum tfa98xx_error tfa_dsp_msg_write(struct tfa_device *tfa,
	int length, const char *buffer);
enum tfa98xx_error tfa_dsp_msg_status(struct tfa_device *tfa, int *p_rpc_status);
const char *tfa98xx_get_i2c_status_id_string(int status);
enum tfa98xx_error tfa_dsp_msg_read(struct tfa_device *tfa,
	int length, unsigned char *bytes);
enum tfa98xx_error dsp_msg(struct tfa_device *tfa,
	int length24, const char *buf24);
enum tfa98xx_error dsp_msg_read(struct tfa_device *tfa,
	int length24, unsigned char *bytes24);
enum tfa98xx_error reg_read(struct tfa_device *tfa,
	unsigned char subaddress, unsigned short *value);
enum tfa98xx_error reg_write(struct tfa_device *tfa,
	unsigned char subaddress, unsigned short value);
enum tfa98xx_error mem_read(struct tfa_device *tfa,
	unsigned int start_offset, int num_words, int *p_values);
enum tfa98xx_error mem_write(struct tfa_device *tfa,
	unsigned short address, int value, int memtype);
enum tfa98xx_error tfa_dsp_msg(struct tfa_device *tfa,
	int length, const char *buf);
enum tfa98xx_error
tfa98xx_check_rpc_status(struct tfa_device *tfa, int *p_rpc_status);
enum tfa98xx_error
tfa98xx_dsp_read_mem(struct tfa_device *tfa,
	unsigned int start_offset, int num_words, int *p_values);
enum tfa98xx_error
tfa98xx_dsp_write_mem_word(struct tfa_device *tfa,
	unsigned short address, int value, int memtype);
enum tfa98xx_error
tfa_cont_write_filterbank(struct tfa_device *tfa, struct tfa_filter *filter);
enum tfa98xx_error
tfa98xx_dsp_biquad_disable(struct tfa_device *tfa, int biquad_index);
enum tfa98xx_error tfa_dsp_cmd_id_write(struct tfa_device *tfa,
	unsigned char module_id,
	unsigned char param_id, int num_bytes,
	const unsigned char data[]);
enum tfa98xx_error tfa_dsp_cmd_id_write_read(struct tfa_device *tfa,
	unsigned char module_id,
	unsigned char param_id, int num_bytes,
	unsigned char data[]);
enum tfa98xx_error
tfa98xx_dsp_write_preset(struct tfa_device *tfa, int length,
	const unsigned char *p_preset_bytes);
enum tfa98xx_error
tfa98xx_dsp_get_sw_feature_bits(struct tfa_device *tfa, int features[2]);
enum tfa98xx_error
tfa98xx_dsp_support_drc(struct tfa_device *tfa, int *pb_support_drc);
enum tfa98xx_error
tfa98xx_dsp_write_config(struct tfa_device *tfa, int length,
	const unsigned char *p_config_bytes);
enum tfa98xx_error tfa98xx_dsp_write_drc(struct tfa_device *tfa,
	int length, const unsigned char *p_drc_bytes);
enum tfa98xx_error tfa98xx_powerdown(struct tfa_device *tfa, int powerdown);
enum tfa98xx_error
tfa98xx_select_mode(struct tfa_device *tfa, enum tfa98xx_mode mode);
int tfa_set_bf(struct tfa_device *tfa,
	const uint16_t bf, const uint16_t value);
int tfa_set_bf_volatile(struct tfa_device *tfa,
	const uint16_t bf, const uint16_t value);
int tfa_get_bf(struct tfa_device *tfa, const uint16_t bf);
enum tfa98xx_error tfa_cf_powerup(struct tfa_device *tfa);
int is_94_N2_device(struct tfa_device *tfa);
enum tfa98xx_error show_current_state(struct tfa_device *tfa);
enum tfa98xx_error tfa_get_fw_api_version(struct tfa_device *tfa,
	unsigned char *pfw_version);
enum tfa98xx_error tfa_run_speaker_boost(struct tfa_device *tfa,
	int force, int profile, int vstep);
enum tfa98xx_error
tfa_run_speaker_startup(struct tfa_device *tfa, int force, int profile, int vstep);
enum tfa98xx_error tfa_run_coldboot(struct tfa_device *tfa, int state);
enum tfa98xx_error tfa_run_start_dsp(struct tfa_device *tfa);
enum tfa98xx_error tfa_run_startup(struct tfa_device *tfa, int profile);
enum tfa98xx_error tfa_run_coldstartup(struct tfa_device *tfa, int profile);
enum tfa98xx_error tfa_run_mute(struct tfa_device *tfa);
enum tfa_error tfa_dev_start(struct tfa_device *tfa, int profile, int vstep);
enum tfa_error tfa_dev_switch_profile(struct tfa_device *tfa,
	int next_profile, int vstep);
enum tfa_error tfa_dev_stop(struct tfa_device *tfa);
enum tfa98xx_error
tfa98xx_write_data(struct tfa_device *tfa,
	unsigned char subaddress, int num_bytes,
	const unsigned char data[]);
enum tfa98xx_error tfa_dsp_get_calibration_impedance(struct tfa_device *tfa);
int tfa_dev_get_swprof(struct tfa_device *tfa);
int tfa_dev_set_swprof(struct tfa_device *tfa, unsigned short new_value);
int tfa_dev_set_swvstep(struct tfa_device *tfa, unsigned short new_value);
int tfa_dev_get_mtpb(struct tfa_device *tfa);
int tfa_is_cold(struct tfa_device *tfa);
int tfa_needs_reset(struct tfa_device *tfa);
int tfa_is_cold_amp(struct tfa_device *tfa);
int tfa_cf_enabled(struct tfa_device *tfa);
enum tfa98xx_error
dsp_partial_coefficients(struct tfa_device *tfa,
	uint8_t *prev, uint8_t *next);
int tfa_dev_probe(int slave, struct tfa_device *tfa);
enum tfa_error
tfa_dev_set_state(struct tfa_device *tfa,
	enum tfa_state state, int is_calibration);
enum tfa_state tfa_dev_get_state(struct tfa_device *tfa);
int tfa_dev_mtp_get(struct tfa_device *tfa, enum tfa_mtp item);
enum tfa_error tfa_dev_mtp_set(struct tfa_device *tfa,
	enum tfa_mtp item, int value);
int tfa_set_bf_value(const uint16_t bf,
	const uint16_t bf_value, uint16_t *p_reg_value);
int tfa_write_reg(struct tfa_device *tfa,
	const uint16_t bf, const uint16_t reg_value);
int tfa_read_reg(struct tfa_device *tfa, const uint16_t bf);
enum tfa_error tfa_load_cnt(void *cnt, int length);
void tfa_cont_show_header(struct tfa_header *hdr);
struct tfa_device_list *tfa_cont_get_dev_list
(struct tfa_container *cont, int dev_idx);
struct tfa_profile_list *tfa_cont_get_dev_prof_list
(struct tfa_container *cont, int dev_idx, int prof_idx);
int tfa_cnt_get_dev_nprof(struct tfa_device *tfa);
int tfa_cont_get_max_vstep(struct tfa_device *tfa, int prof_idx);
struct tfa_file_dsc *tfa_cont_get_file_data(struct tfa_device *tfa,
	int prof_idx, enum tfa_header_type type);
enum tfa98xx_error tfa_cont_write_drc_file
(struct tfa_device *tfa, int size, uint8_t data[]);
enum tfa98xx_error tfa_cont_write_file(struct tfa_device *tfa,
	struct tfa_file_dsc *file, int vstep_idx, int vstep_msg_idx);
int tfa_cont_get_idx(struct tfa_device *tfa);
enum tfa98xx_error tfa_run_write_bitfield(struct tfa_device *tfa,
	struct tfa_bitfield bf);
enum tfa98xx_error tfa_cont_write_regs_dev(struct tfa_device *tfa);
enum tfa98xx_error  tfa_cont_write_regs_prof(struct tfa_device *tfa,
	int prof_idx);
enum tfa98xx_error tfa_cont_write_patch(struct tfa_device *tfa);
enum tfa98xx_error tfa_cont_write_files(struct tfa_device *tfa);
enum tfa98xx_error tfa_cont_write_files_prof(struct tfa_device *tfa,
	int prof_idx, int vstep_idx);
unsigned int tfa98xx_get_profile_sr(struct tfa_device *tfa,
	unsigned int prof_idx);
enum tfa98xx_error tfa_cont_write_profile(struct tfa_device *tfa,
	int prof_idx, int vstep_idx);
enum tfa98xx_error tfa_cont_write_files_vstep(struct tfa_device *tfa,
	int prof_idx, int vstep_idx);
char *tfa_cont_get_string(struct tfa_container *cnt, struct tfa_desc_ptr *dsc);
char *tfa_cont_get_command_string(uint32_t type);
char *tfa_cont_device_name(struct tfa_container *cnt, int dev_idx);
char *tfa_cont_profile_name(struct tfa_container *cnt,
	int dev_idx, int prof_idx);
struct tfa_device_list *tfa_cont_device(struct tfa_container *cnt,
	int dev_idx);
int tfa_cont_crc_check_container(struct tfa_container *cont);
enum tfa98xx_error tfa98xx_factory_trimmer(struct tfa_device *tfa);
enum tfa98xx_error tfa_set_filters(struct tfa_device *tfa, int prof_idx);
void tfa9894_ops(struct tfa_device_ops *ops);
enum tfa98xx_error tfa98xx_write_register16(struct tfa_device *dsp,
					 unsigned char reg, unsigned short value);
enum tfa98xx_error tfa98xx_read_register16(struct tfa_device *dsp,
					unsigned char reg, unsigned short *value);
enum tfa98xx_error tfa98xx_read_data(struct tfa_device *dsp,
				   unsigned char reg, int len, unsigned char *data);
enum tfa98xx_error tfa98xx_write_raw(struct tfa_device *dsp, int len,
				   const unsigned char *data);
#endif
