#include <linux/uaccess.h>
#include <linux/platform_device.h>

#include <chr/leicaefi-chr.h>
#include <leicaefi.h>
#include <common/leicaefi-chip.h>

static long leicaefi_chr_ioctl_read(struct leicaefi_chr_device *efidev,
				    unsigned long arg)
{
	struct leicaefi_ioctl_regrw __user *user_data = (void __user *)arg;
	struct leicaefi_ioctl_regrw kernel_data;

	if ((arg == 0) || (copy_from_user(&kernel_data, user_data,
					  sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data from user space\n", __func__);
		return -EACCES;
	}

	if (leicaefi_chip_read(efidev->efichip, kernel_data.reg_no,
			       &kernel_data.reg_value) != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d)\n", __func__,
			 (int)kernel_data.reg_no);
		return -EIO;
	}

	if ((arg == 0) ||
	    (copy_to_user(user_data, &kernel_data, sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data to user space\n", __func__);
		return -EACCES;
	}

	return 0;
}

static long leicaefi_chr_ioctl_write(struct leicaefi_chr_device *efidev,
				     unsigned long arg)
{
	struct leicaefi_ioctl_regrw __user *user_data = (void __user *)arg;
	struct leicaefi_ioctl_regrw kernel_data;

	if ((arg == 0) || (copy_from_user(&kernel_data, user_data,
					  sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data from user space\n", __func__);
		return -EACCES;
	}

	if (leicaefi_chip_write(efidev->efichip, kernel_data.reg_no,
				kernel_data.reg_value) != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d, value: %d)\n",
			 __func__, (int)kernel_data.reg_no,
			 (int)kernel_data.reg_value);
		return -EIO;
	}

	return 0;
}

static long leicaefi_chr_ioctl_write_raw(struct leicaefi_chr_device *efidev,
					 unsigned long arg)
{
	struct leicaefi_ioctl_regrw __user *user_data = (void __user *)arg;
	struct leicaefi_ioctl_regrw kernel_data;

	if ((arg == 0) || (copy_from_user(&kernel_data, user_data,
					  sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data from user space\n", __func__);
		return -EACCES;
	}

	if (kernel_data.reg_no & LEICAEFI_SCBIT_SET) {
		if (leicaefi_chip_set_bits(efidev->efichip,
					   kernel_data.reg_no &
						   LEICAEFI_REGNO_MASK,
					   kernel_data.reg_value) != 0) {
			dev_warn(
				&efidev->pdev->dev,
				"%s - I/O operation failed (regno: %d, value: %d)\n",
				__func__, (int)kernel_data.reg_no,
				(int)kernel_data.reg_value);
			return -EIO;
		}
	} else {
		if (leicaefi_chip_clear_bits(efidev->efichip,
					     kernel_data.reg_no &
						     LEICAEFI_REGNO_MASK,
					     kernel_data.reg_value) != 0) {
			dev_warn(
				&efidev->pdev->dev,
				"%s - I/O operation failed (regno: %d, value: %d)\n",
				__func__, (int)kernel_data.reg_no,
				(int)kernel_data.reg_value);
			return -EIO;
		}
	}

	return 0;
}

static long leicaefi_chr_ioctl_bits_set(struct leicaefi_chr_device *efidev,
					unsigned long arg)
{
	struct leicaefi_ioctl_regrw __user *user_data = (void __user *)arg;
	struct leicaefi_ioctl_regrw kernel_data;

	if ((arg == 0) || (copy_from_user(&kernel_data, user_data,
					  sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data from user space\n", __func__);
		return -EACCES;
	}

	if (leicaefi_chip_set_bits(efidev->efichip, kernel_data.reg_no,
				   kernel_data.reg_value) != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d, value: %d)\n",
			 __func__, (int)kernel_data.reg_no,
			 (int)kernel_data.reg_value);
		return -EIO;
	}

	return 0;
}

static long leicaefi_chr_ioctl_bits_clear(struct leicaefi_chr_device *efidev,
					  unsigned long arg)
{
	struct leicaefi_ioctl_regrw __user *user_data = (void __user *)arg;
	struct leicaefi_ioctl_regrw kernel_data;

	if ((arg == 0) || (copy_from_user(&kernel_data, user_data,
					  sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data from user space\n", __func__);
		return -EACCES;
	}

	if (leicaefi_chip_clear_bits(efidev->efichip, kernel_data.reg_no,
				     kernel_data.reg_value) != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d, value: %d)\n",
			 __func__, (int)kernel_data.reg_no,
			 (int)kernel_data.reg_value);
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
