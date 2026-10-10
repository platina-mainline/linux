/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_POWER_QCOM_POWER_SUPPLY_EXTENSIONS_H
#define _LINUX_POWER_QCOM_POWER_SUPPLY_EXTENSIONS_H

/*
 * Temporary Qualcomm downstream power-supply extensions for FG GEN3.
 * Expand these lists at the end of the mainline enums to preserve all
 * existing mainline values. Property handling remains in the drivers.
 */
#define QCOM_POWER_SUPPLY_PROPERTIES(_) \
	_(CHARGE_NOW_RAW) \
	_(CAPACITY_RAW) \
	_(CHARGING_ENABLED) \
	_(INPUT_SUSPEND) \
	_(RESISTANCE) \
	_(RESISTANCE_ID) \
	_(CHARGE_DONE) \
	_(CHARGE_QNOVO_ENABLE) \
	_(CYCLE_COUNT_ID) \
	_(SOC_REPORTING_READY) \
	_(DEBUG_BATTERY) \
	_(CC_STEP) \
	_(CC_STEP_SEL) \
	_(BATTERY_TYPE)

#define QCOM_POWER_SUPPLY_CHARGE_TYPES(_) \
	_(TAPER)

#define QCOM_POWER_SUPPLY_TYPES(_) \
	_(BMS)

#endif /* _LINUX_POWER_QCOM_POWER_SUPPLY_EXTENSIONS_H */
