# SPDX-License-Identifier: GPL-2.0-only
# Link downstream helpers, with one shared PMIC voter for both drivers.
obj-$(CONFIG_QCOM_PMIC_VOTER) += pmic-voter.o
obj-$(CONFIG_QPNP_FG_GEN3) += qpnp-fg-gen3-driver.o
qpnp-fg-gen3-driver-y := qpnp-fg-gen3.o fg-memif.o fg-util.o of_batterydata.o
obj-$(CONFIG_QPNP_SMB2) += qpnp-smb2-driver.o
qpnp-smb2-driver-y := step-chg-jeita.o battery.o qpnp-smb2.o smb-lib.o storm-watch.o
# qpnp-revid.h checks CONFIG_QPNP_REVID rather than IS_ENABLED().
obj-$(CONFIG_QPNP_REVID) += qpnp-revid.o
