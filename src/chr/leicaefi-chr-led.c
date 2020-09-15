#include <linux/uaccess.h>
#include <linux/platform_device.h>

#include <chr/leicaefi-chr.h>
#include <chr/leicaefi-chr-utils.h>
#include <leicaefi.h>
#include <common/leicaefi-chip.h>

static long
leicaefi_chr_ioctl_led_set_test_mode(struct leicaefi_chr_device *efidev,
				     unsigned long arg)
{
	struct leicaefi_ioctl_led_test_mode_enable data;
	int rv = 0;

	rv = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rv) {
		return rv;
	}

	rv = leicaefi_chip_gencmd(efidev->efichip,
				  LEICAEFI_CMD_LED_TEST_MODE_WRITE,
				  data.enable ? 1 : 0, NULL);
	if (rv != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - setting led test mode failed rv=%d\n", __func__,
			 rv);
		return -EINVAL;
	}

	return rv;
}

long leicaefi_chr_led_handle_ioctl(struct leicaefi_chr_device *efidev,
				   unsigned int cmd, unsigned long arg,
				   bool *handled)
{
	int result = -EINVAL;

	switch (cmd) {
	case LEICAEFI_IOCTL_LED_SET_TEST_MODE:
		result = leicaefi_chr_ioctl_led_set_test_mode(efidev, arg);
		*handled = true;
		break;
	default:
		*handled = false;
		break;
	}

	return result;
}
