/*
 * Copyright 2014-2017 NXP Semiconductors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef TFA9894_DEVICE_H
#define TFA9894_DEVICE_H
#include "config.h"
struct tfa_device;
enum feature_support {
	SUPPORT_NOT_SET, /**< default means not set yet */
	SUPPORT_NO,      /**< no support */
	SUPPORT_YES      /**< supported */
};
/*
 * supported Digital Audio Interfaces bitmap
 */
enum tfa98xx_dai_bitmap {
	TFA98XX_DAI_I2S = 0x01, /**< I2S only */
	TFA98XX_DAI_TDM = 0x02, /**< TDM, I2S */
	TFA98XX_DAI_PDM = 0x04, /**< PDM  */
};

/*
 * device ops function structure
 */
struct tfa_device_ops {
	enum tfa98xx_error(*dsp_msg)(struct tfa_device *tfa, int length, const char *buf);
	enum tfa98xx_error(*dsp_msg_read)(struct tfa_device *tfa, int length, unsigned char *bytes);

	enum tfa98xx_error(*reg_read)(struct tfa_device *tfa, unsigned char subaddress, unsigned short *value);
	enum tfa98xx_error(*reg_write)(struct tfa_device *tfa, unsigned char subaddress, unsigned short value);
	enum tfa98xx_error(*mem_read)(struct tfa_device *tfa, unsigned int start_offset, int num_words, int *p_values);
	enum tfa98xx_error(*mem_write)(struct tfa_device *tfa, unsigned short address, int value, int memtype);

	enum tfa98xx_error (*tfa_init)(struct tfa_device *tfa); /**< init typically for loading optimal settings */
	enum tfa98xx_error (*dsp_reset)(struct tfa_device *tfa, int state); /**< reset the coolflux dsp */
	enum tfa98xx_error (*dsp_system_stable)(struct tfa_device *tfa, int *ready); /**< ready when clocks are stable to allow DSP subsystem access */
	enum tfa98xx_error (*dsp_write_tables)(struct tfa_device *tfa, int sample_rate); /**< write the device/type specific delaytables */
	enum tfa98xx_error (*auto_copy_mtp_to_iic)(struct tfa_device *tfa); /**< Set auto_copy_mtp_to_iic */
	enum tfa98xx_error (*factory_trimmer)(struct tfa_device *tfa); /**< Factory trimming for the Boost converter */
	int (*set_swprof)(struct tfa_device *tfa, unsigned short new_value); /**< Set the sw profile in the struct and the hw register */
	int (*get_swprof)(struct tfa_device *tfa); /**< Get the sw profile from the hw register */
	int(*set_swvstep)(struct tfa_device *tfa, unsigned short new_value); /**< Set the sw vstep in the struct and the hw register */
	int(*get_swvstep)(struct tfa_device *tfa); /**< Get the sw vstep from the hw register */
	int(*get_mtpb)(struct tfa_device *tfa); /**< get status of MTB busy bit*/
	enum tfa98xx_error (*set_mute)(struct tfa_device *tfa, int mute); /**< set mute */
	enum tfa98xx_error (*faim_protect)(struct tfa_device *tfa, int state); /**< Protect FAIM from being corrupted  */
	enum tfa98xx_error(*set_osc_powerdown)(struct tfa_device *tfa, int state); /**< Allow to change internal osc. gating settings */
	enum tfa98xx_error(*update_lpm)(struct tfa_device *tfa, int state); /**< Allow to change lowpowermode settings */
};

/**
 * Device states and modifier flags to allow a device/type independent fine
 * grained control of the internal state.\n
 * Values below 0x10 are referred to as base states which can be or-ed with
 * state modifiers, from 0x10 and higher.
 *
 */
enum tfa_state {
	TFA_STATE_UNKNOWN,      /**< unknown or invalid */
	TFA_STATE_POWERDOWN,    /**< PLL in powerdown, Algo is up/warm */
	TFA_STATE_INIT_HW,      /**< load I2C/PLL hardware setting (~wait2srcsettings) */
	TFA_STATE_INIT_CF,      /**< coolflux HW access possible (~initcf) */
	TFA_STATE_INIT_FW,      /**< DSP framework active (~patch loaded) */
	TFA_STATE_OPERATING,    /**< Amp and Algo running */
	TFA_STATE_FAULT,        /**< An alarm or error occurred */
	TFA_STATE_RESET,        /**< I2C reset and ACS set */
	/* --sticky state modifiers-- */
	TFA_STATE_MUTE=0x10,         /**< Algo & Amp mute */
	TFA_STATE_UNMUTE=0x20,       /**< Algo & Amp unmute */
	TFA_STATE_CLOCK_ALWAYS=0x40, /**< PLL connect to internal oscillator */
	TFA_STATE_CLOCK_AUDIO=0x80,  /**< PLL connect to audio clock (BCK/FS) */
	TFA_STATE_LOW_POWER=0x100,   /**< lowest possible power state */
};


/**
 * This is the main tfa device context structure, it will carry all information
 * that is needed to handle a single I2C device instance.
 * All functions dealing with the device will need access to the fields herein.
 */
struct tfa_device {
	int dev_idx;			/**< device container index */
	int in_use;
	int buffer_size;		/**< lowest level max buffer size */
	unsigned char slave_address; /**< I2C slave address (not shifted) */
	unsigned short rev;     /**< full revid of this device */
	enum feature_support support_drc;
	enum feature_support support_framework;
	enum feature_support support_saam;
	int sw_feature_bits[2]; /**< cached copy of sw feature bits */
	int hw_feature_bits;    /**< cached copy of hw feature bits */
	int profile;            /**< active profile */
	int vstep;              /**< active vstep */
	unsigned char spkr_select;
	unsigned char support_tcoef;/**< legacy tfa9887, will be removed */
	int mohm[3]; /**< speaker calibration values in milli ohms -1 is error */
	struct tfa_device_ops dev_ops;
	int verbose; /**< verbosity level for debug print output */
	enum tfa_state state;  /**< last known state or-ed with optional state_modifier */
	struct tfa_container *cnt;/**< the loaded container file */
	struct tfa_volume_step_register_info *p_reg_info; /**< remember vstep for partial updates */
	int partial_enable; /**< enable partial updates */
	void *data; /**< typically pointing to Linux driver structure owning this device */
	int sync_iv_delay; /**< synchronize I/V delay at cold start */
	int needs_reset; /**< add the reset trigger for SetAlgoParams and SetMBDrc commands */
	char fw_itf_ver[4];          /* Firmware ITF version */

	int temp; /* calibration temperature */
	int spkr_damaged; /* check if speaker is damaged */
	int is_cold; /* respond to MANSTATE, before tfa_run_speaker_boost */
	int is_bypass; /* respond to vstep in profile, before sending calibration data */
	int reset_mtpex; /* suspended resetting MTPEX, if it's called when not active */
	int stream_state; /* b0: pstream (Rx), b1: cstream (Tx), b2: samstream (SaaM) */
	int prev_samstream;
	int first_after_boot;
	int ampgain;
	int individual_msg;
};


enum tfa_mtp {
	TFA_MTP_OTC,		/**< */
	TFA_MTP_EX,		/**< */
	TFA_MTP_RE25,		/**< */
	TFA_MTP_RE25_PRIM,	/**< */
	TFA_MTP_RE25_SEC,	/**< */
	TFA_MTP_LOCK,		/**< */
};
#endif
