LG MSM8996 multi-device bring-up
==============================

The LG G5 H1 additions are experimental. H850 is the first hardware test
model. No G5 hardware boot or peripheral validation has been performed.
The existing G6 H870D device validation does not cover the G5.

Build profiles
--------------

The workspace build-kernel.sh accepts KERNEL_DEVICE=h850, h830, rs988 or
h870d. Each profile selects the common defconfig, a family fragment, a
model fragment and a separate output directory. An explicit BOOT_TEMPLATE
from the target ROM is required to package G5 boot images. Image.gz-dtb is
a kernel payload, not a boot partition image.

H850 selects the international H1 DTBs; H830/RS988 select the US H1 DTBs.
Keep the original board and silicon revision IDs. In particular, v2,
v3.0 and v3.1 boards use different CPR/GPU tables. Do not identify the
silicon revision from the product name alone.

The H1 family fragment overrides G6 touch, fingerprint, Type-C, DAC and
modem fixup selections. H830 uses BCM4358; H850 and RS988 use BCM43455.
H850 additionally enables quaternary MI2S. Do not enable the newer LGE_PM
implementation to satisfy removed 4.4 power configuration symbols.

Ported components
-----------------

* H1 global/US board DTs, battery profiles, camera/OIS wiring and panels.
* Legacy PMIC GPIO/MPP states translated to the SPMI pinctrl bindings.
* MSM8996 v2 and v3.0 revision includes adapted to the current clocks.
* LG4946 touch, watch and production diagnostics, and H1 touch firmware.
* H1 panel reset/LCD notifications and the U2 FTRIM diagnostic read.
  The Lucye shared-reset retention path remains SW49408-specific.
* ANX7418 Type-C with wakeup-source and I2C locking API adaptations.
  The charger discovers usb_pd after the battery/USB supplies register,
  avoiding a circular probe dependency.
* Old FPC1020 HAL ABI selected for G5, and H1 OIS object selection.

ANX7418 OTP programming is disabled by default. The allow_firmware_update
module parameter must be explicitly enabled to program controller OTP.
Existing controller firmware is used for normal bring-up.

Known limitations and next work
------------------------------

A compiled DTB and linked driver do not establish correct power sequencing
on a physical phone. Validate stock bootloader panel selection, reset and
bias supplies, touch/LPWG, UFS, USB roles and charging first. LG full AOD,
panel-maker/revision control, Slimport output, removable module accessories,
IRRC, battery-ID policy, camera/OIS and audio require further porting or
hardware validation. The presence of camera/OIS DT nodes and objects does
not establish complete camera feature support.

Compare the actual UFS manufacturer/model against the existing 4.19 quirk
table before adding workarounds. The legacy SK hynix model-specific gear
switch entries already exist. Keep storage quirks separate from model
selection.

G6 regional and V20 support, and the additional factory-only G5 regional
boards, remain subsequent work. Preserve the workspace variant inventory
and record build-only versus hardware-tested status separately.
