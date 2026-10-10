# SPDX-License-Identifier: GPL-2.0-only
# The unchanged downstream helpers have no module exports; link them together.
obj-$(CONFIG_QPNP_FG_GEN3) += qpnp-fg-gen3-driver.o
qpnp-fg-gen3-driver-y := qpnp-fg-gen3.o fg-memif.o fg-util.o pmic-voter.o of_batterydata.o
# qpnp-revid.h checks CONFIG_QPNP_REVID rather than IS_ENABLED().
obj-$(CONFIG_QPNP_REVID) += qpnp-revid.o
