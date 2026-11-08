// SPDX-License-Identifier: GPL-2.0-only
/* Deliver the legacy SMB charger's OCP event through the regulator core. */
static int somc_usb_otg_regulator_ocp_notify(struct smbchg_chip *chip)
{
	if (chip->otg_vreg.rdev)
		regulator_notifier_call_chain(chip->otg_vreg.rdev,
					     REGULATOR_EVENT_OVER_CURRENT, NULL);
	return 0;
}
