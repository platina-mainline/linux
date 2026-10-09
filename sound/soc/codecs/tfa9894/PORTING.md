The CoolFlux firmware and speaker protection implementation is based on
NXP's `nxpsw/tfa98xx` release at commit
`b3c158a9bd52c1abd0dcf824a21059eed3d55a00` (see `LICENSE.nxp.txt`).
The TFA9894 board selection follows Xiaomi's platina downstream driver:
GPIO113 low selects GOER, floating selects AAC, and high is rejected.

The firmware files are external and are requested as
`qcom/sound/tfa98xx_goer.cnt` or `qcom/sound/tfa98xx_aac.cnt`.
The amplifier and its sound card link remain disabled in the platina DTS
until the actual containers and on-device calibration can be validated.
