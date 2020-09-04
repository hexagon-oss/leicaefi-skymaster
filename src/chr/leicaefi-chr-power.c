#include <linux/uaccess.h>
#include <linux/platform_device.h>

#include <chr/leicaefi-chr.h>
#include <chr/leicaefi-chr-utils.h>
#include <leicaefi.h>
#include <common/leicaefi-chip.h>

static long
leicaefi_chr_ioctl_get_active_power_source(struct leicaefi_chr_device *efidev,
					   unsigned long arg)
{
	struct leicaefi_ioctl_power_source data;
	u16 power_value = 0;
	int rv = 0;

	rv = leicaefi_chip_read(efidev->efichip, LEICAEFI_REG_PWR_SRC_STATUS,
				&power_value);

	if (rv != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - reading power source status data failed rv=%d\n",
			 __func__, rv);
		return -EINVAL;
	}

	dev_dbg(&efidev->pdev->dev, "Power source status read => 0x%X\n",
		(int)power_value);

	if (power_value & LEICAEFI_POWERSRCBIT_BAT1ACT) {
		data.power_source = LEICAEFI_POWER_SOURCE_INTERNAL_BATTERY1;
	} else if (power_value & LEICAEFI_POWERSRCBIT_EXT1ACT) {
		data.power_source = LEICAEFI_POWER_SOURCE_EXTERNAL_SOURCE1;
	} else if (power_value & LEICAEFI_POWERSRCBIT_EXT2ACT) {
		data.power_source = LEICAEFI_POWER_SOURCE_EXTERNAL_SOURCE2;
	} else if (power_value & LEICAEFI_POWERSRCBIT_POE1ACT) {
		data.power_source = LEICAEFI_POWER_SOURCE_POE_SOURCE1;
	} else {
		dev_warn(&efidev->pdev->dev,
			 "%s - no active power source detected\n", __func__);
		data.power_source = LEICAEFI_POWER_SOURCE_UNKOWN_SOURCE;
	}

	rv = leicaefi_chr_copy_to_user(arg, &data, sizeof(data));

	return rv;
}

long leicaefi_chr_power_handle_ioctl(struct leicaefi_chr_device *efidev,
				     unsigned int cmd, unsigned long arg,
				     bool *handled)
{
	int result = -EINVAL;

	switch (cmd) {
	case LEICAEFI_IOCTL_GET_ACTIVE_POWER_SOURCE:
		result =
			leicaefi_chr_ioctl_get_active_power_source(efidev, arg);
		*handled = true;
		break;
	default:
		*handled = false;
		break;
	}

	return result;
}
