#include <linux/uaccess.h>
#include <linux/platform_device.h>

#include <chr/leicaefi-chr.h>
#include <chr/leicaefi-chr-utils.h>
#include <leicaefi.h>
#include <common/leicaefi-chip.h>

static int leicaefi_chr_ioctl_read_onewire_register(
	struct leicaefi_chr_device *efidev, u8 reg_no,
	struct leicaefi_ioctl_onewire_device *data)
{
	int rv = 0;
	u16 value = 0;
	rv = leicaefi_chip_read(efidev->efichip, reg_no, &value);
	if (rv) {
		dev_warn(&efidev->pdev->dev,
			 "%s - reading device id has failed rv=%d\n", __func__,
			 rv);
		return -EINVAL;
	}

	dev_dbg(&efidev->pdev->dev, "One wire device status read => 0x%X\n",
		(int)value);

	data->id = (u8)(value & 0x00FF);
	data->familyCode = (u8)((value >> 8) & 0x00FF);

	return rv;
}

static long
leicaefi_chr_ioctl_get_onewire_device_info(struct leicaefi_chr_device *efidev,
					   unsigned long arg)
{
	struct leicaefi_ioctl_onewire_device data;
	int rv = 0;

	rv = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rv) {
		return rv;
	}

	switch (data.port) {
	case LEICAEFI_ONEWIRE_PORT_COM1:
		if (leicaefi_chr_ioctl_read_onewire_register(
			    efidev, LEICAEFI_REG_COM_1_ID, &data)) {
			return -EINVAL;
		}
		break;
	case LEICAEFI_ONEWIRE_PORT_COM2:
		if (leicaefi_chr_ioctl_read_onewire_register(
			    efidev, LEICAEFI_REG_COM_2_ID, &data)) {
			return -EINVAL;
		}
		break;
	case LEICAEFI_ONEWIRE_PORT_COM3:
		if (leicaefi_chr_ioctl_read_onewire_register(
			    efidev, LEICAEFI_REG_COM_3_ID, &data)) {
			return -EINVAL;
		}
		break;
	case LEICAEFI_ONEWIRE_PORT_PWR1:
	case LEICAEFI_ONEWIRE_PORT_PWR2:
		// TODO: CRA:	Implement missing port handling
		dev_warn(
			&efidev->pdev->dev,
			"%s - device info for this port currently not implemented\n",
			__func__);
		break;
	default:
		dev_warn(&efidev->pdev->dev, "%s - port number unknown: %d\n",
			 __func__, data.port);
		return -ENODEV;
	}

	rv = leicaefi_chr_copy_to_user(arg, &data, sizeof(data));

	return rv;
}

long leicaefi_chr_onewire_handle_ioctl(struct leicaefi_chr_device *efidev,
				       unsigned int cmd, unsigned long arg,
				       bool *handled)
{
	int result = -EINVAL;
	switch (cmd) {
	case LEICAEFI_IOCTL_ONE_WIRE_DEVICE_INFO:
		result =
			leicaefi_chr_ioctl_get_onewire_device_info(efidev, arg);
		*handled = true;
		break;
	default:
		*handled = false;
		break;
	}

	return result;
}
