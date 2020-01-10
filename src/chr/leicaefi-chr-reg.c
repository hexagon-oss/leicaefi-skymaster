#include <linux/uaccess.h>
#include <linux/platform_device.h>

#include <chr/leicaefi-chr.h>
#include <chr/leicaefi-chr-utils.h>
#include <leicaefi.h>
#include <common/leicaefi-chip.h>

static long leicaefi_chr_ioctl_read(struct leicaefi_chr_device *efidev,
				    unsigned long arg)
{
	struct leicaefi_ioctl_regrw data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	if (leicaefi_chip_read(efidev->efichip, data.reg_no, &data.reg_value) !=
	    0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d)\n", __func__,
			 (int)data.reg_no);
		return -EIO;
	}

	rc = leicaefi_chr_copy_to_user(arg, &data, sizeof(data));
	if (rc) {
		return rc;
	}

	return 0;
}

static long leicaefi_chr_ioctl_write(struct leicaefi_chr_device *efidev,
				     unsigned long arg)
{
	struct leicaefi_ioctl_regrw data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	if (leicaefi_chip_write(efidev->efichip, data.reg_no, data.reg_value) !=
	    0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d, value: %d)\n",
			 __func__, (int)data.reg_no, (int)data.reg_value);
		return -EIO;
	}

	return 0;
}

static long leicaefi_chr_ioctl_write_raw(struct leicaefi_chr_device *efidev,
					 unsigned long arg)
{
	struct leicaefi_ioctl_regrw data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	if (data.reg_no & LEICAEFI_SCBIT_SET) {
		if (leicaefi_chip_set_bits(efidev->efichip,
					   data.reg_no & LEICAEFI_REGNO_MASK,
					   data.reg_value) != 0) {
			dev_warn(
				&efidev->pdev->dev,
				"%s - I/O operation failed (regno: %d, value: %d)\n",
				__func__, (int)data.reg_no,
				(int)data.reg_value);
			return -EIO;
		}
	} else {
		if (leicaefi_chip_clear_bits(efidev->efichip,
					     data.reg_no & LEICAEFI_REGNO_MASK,
					     data.reg_value) != 0) {
			dev_warn(
				&efidev->pdev->dev,
				"%s - I/O operation failed (regno: %d, value: %d)\n",
				__func__, (int)data.reg_no,
				(int)data.reg_value);
			return -EIO;
		}
	}

	return 0;
}

static long leicaefi_chr_ioctl_bits_set(struct leicaefi_chr_device *efidev,
					unsigned long arg)
{
	struct leicaefi_ioctl_regrw data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	if (leicaefi_chip_set_bits(efidev->efichip, data.reg_no,
				   data.reg_value) != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d, value: %d)\n",
			 __func__, (int)data.reg_no, (int)data.reg_value);
		return -EIO;
	}

	return 0;
}

static long leicaefi_chr_ioctl_bits_clear(struct leicaefi_chr_device *efidev,
					  unsigned long arg)
{
	struct leicaefi_ioctl_regrw data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	if (leicaefi_chip_clear_bits(efidev->efichip, data.reg_no,
				     data.reg_value) != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d, value: %d)\n",
			 __func__, (int)data.reg_no, (int)data.reg_value);
		return -EIO;
	}

	return 0;
}

long leicaefi_chr_reg_handle_ioctl(struct leicaefi_chr_device *efidev,
				   unsigned int cmd, unsigned long arg,
				   bool *handled)
{
	int result = -EINVAL;

	switch (cmd) {
		/* direct register access */
	case LEICAEFI_IOCTL_READ:
		result = leicaefi_chr_ioctl_read(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_WRITE:
		result = leicaefi_chr_ioctl_write(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_BITS_SET:
		result = leicaefi_chr_ioctl_bits_set(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_BITS_CLEAR:
		result = leicaefi_chr_ioctl_bits_clear(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_WRITE_RAW:
		result = leicaefi_chr_ioctl_write_raw(efidev, arg);
		*handled = true;
		break;
	default:
		*handled = false;
		break;
	}

	return result;
}
