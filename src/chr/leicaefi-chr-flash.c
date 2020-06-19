#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>

#include <chr/leicaefi-chr.h>
#include <leicaefi.h>
#include <common/leicaefi-chip.h>
#include <chr/leicaefi-chr-utils.h>

enum leicaefi_flash_op_state {
	LEICAEFI_FLASH_OP_IDLE,
	LEICAEFI_FLASH_OP_PENDING,
	LEICAEFI_FLASH_OP_DONE,
	LEICAEFI_FLASH_OP_FAILED,
};

static int leicaefi_chr_flash_exclusive_lock(struct leicaefi_chr_device *efidev)
{
	return mutex_lock_interruptible(&efidev->flash.op_lock);
}

static void
leicaefi_chr_flash_exclusive_unlock(struct leicaefi_chr_device *efidev)
{
	return mutex_unlock(&efidev->flash.op_lock);
}

static int leicaefi_chr_flash_set_op_state(struct leicaefi_chr_device *efidev,
					   const char *op_info,
					   int expected_state, int next_state)
{
	int prev_value = atomic_cmpxchg(&efidev->flash.op_state, expected_state,
					next_state);
	if (prev_value != expected_state) {
		dev_err(&efidev->pdev->dev,
			"%s - cannot set operation state for %s (state: %d, target: %d)\n",
			__func__, op_info, prev_value, next_state);
		return -LEICAEFI_EINTERNAL;
	}

	return 0;
}

static int leicaefi_chr_request_set_mode(struct leicaefi_chr_device *efidev,
					 const struct leicaefi_ioctl_mode *data)
{
	int rc = 0;
	int state_value = 0;
	u16 current_modid_mode_value = 0;
	u16 target_modid_mode_value = 0;

	if (data->mode == LEICAEFI_SOFTWARE_MODE_FIRMWARE) {
		target_modid_mode_value = (LEICAEFI_MODID_MODE_FIRMWARE
					   << LEICAEFI_MODID_MODE_SHIFT);
	} else if (data->mode == LEICAEFI_SOFTWARE_MODE_LOADER) {
		target_modid_mode_value = (LEICAEFI_MODID_MODE_LOADER
					   << LEICAEFI_MODID_MODE_SHIFT);
	} else {
		dev_warn(&efidev->pdev->dev, "%s - invalid mode %d\n", __func__,
			 (int)data->mode);
		return -EINVAL;
	}

	/* get current mode */
	rc = leicaefi_chip_read(efidev->efichip, LEICAEFI_REG_MOD_ID,
				&current_modid_mode_value);
	if (rc) {
		return -EIO;
	}

	/* keep only mode bit */
	current_modid_mode_value &=
		(LEICAEFI_MODID_MODE_MASK << LEICAEFI_MODID_MODE_SHIFT);

	/* if already in requested mode - return success */
	if (current_modid_mode_value == target_modid_mode_value) {
		return 0;
	}

	rc = leicaefi_chr_flash_set_op_state(efidev, "set_mode_start",
					     LEICAEFI_FLASH_OP_IDLE,
					     LEICAEFI_FLASH_OP_PENDING);
	if (rc) {
		return rc;
	}

	/* request mode switch */
	if ((leicaefi_chip_set_bits(efidev->efichip, LEICAEFI_REG_FLASH_CTRL,
				    LEICAEFI_FLASHCTRLBIT_SWITCH) != 0)) {
		dev_warn(&efidev->pdev->dev, "%s - request failed\n", __func__);

		rc = leicaefi_chr_flash_set_op_state(efidev, "set_mode_reqfail",
						     LEICAEFI_FLASH_OP_PENDING,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		return -EIO;
	}

	/* non-interruptible - it must be finished */
	wait_event(efidev->flash.op_wq, atomic_read(&efidev->flash.op_state) !=
						LEICAEFI_FLASH_OP_PENDING);

	state_value = atomic_read(&efidev->flash.op_state);
	if (state_value == LEICAEFI_FLASH_OP_DONE) {
		rc = leicaefi_chr_flash_set_op_state(efidev, "set_mode_done",
						     LEICAEFI_FLASH_OP_DONE,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		return 0;
	} else if (state_value == LEICAEFI_FLASH_OP_FAILED) {
		rc = leicaefi_chr_flash_set_op_state(efidev, "set_mode_fail",
						     LEICAEFI_FLASH_OP_FAILED,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		return -LEICAEFI_EOPFAIL;
	} else {
		dev_err(&efidev->pdev->dev,
			"%s - execution error (state: %d)\n", __func__,
			state_value);
		return -LEICAEFI_EINTERNAL;
	}
}

static int leicaefi_chr_request_get_mode(struct leicaefi_chr_device *efidev,
					 struct leicaefi_ioctl_mode *data)
{
	u16 mod_id_value = 0;
	u16 modid_mode = 0;

	if (leicaefi_chip_read(efidev->efichip, LEICAEFI_REG_MOD_ID,
			       &mod_id_value) != 0) {
		dev_warn(&efidev->pdev->dev, "%s - request failed\n", __func__);
		return -EIO;
	}

	modid_mode = (mod_id_value >> LEICAEFI_MODID_MODE_SHIFT) &
		     LEICAEFI_MODID_MODE_MASK;

	if (modid_mode == LEICAEFI_MODID_MODE_LOADER) {
		data->mode = LEICAEFI_SOFTWARE_MODE_LOADER;
	} else {
		data->mode = LEICAEFI_SOFTWARE_MODE_FIRMWARE;
	}

	return 0;
}

static int leicaefi_chr_flash_request_check_checksum(
	struct leicaefi_chr_device *efidev,
	struct leicaefi_ioctl_flash_checksum *checksum_data)
{
	static const u8 CHECK_RESULT_SUCCESS = 1;
	static const u8 CHECK_RESULT_FAILURE = 0;

	int rc = 0;
	int state_value = 0;
	u16 flash_ctrl_mask = 0;
	struct leicaefi_ioctl_mode mode_data;

	if (checksum_data->mode == LEICAEFI_SOFTWARE_MODE_FIRMWARE) {
		flash_ctrl_mask = LEICAEFI_FLASHCTRLBIT_FWCHK;
	} else if (checksum_data->mode == LEICAEFI_SOFTWARE_MODE_LOADER) {
		flash_ctrl_mask = LEICAEFI_FLASHCTRLBIT_LDRCHK;
	} else {
		dev_warn(&efidev->pdev->dev, "%s - invalid partition %d\n",
			 __func__, (int)checksum_data->mode);
		return -EINVAL;
	}

	dev_dbg(&efidev->pdev->dev, "%s - starting check for part %d\n",
		__func__, checksum_data->mode);

	/* check mode, only 'opposite' partition could be checked */
	rc = leicaefi_chr_request_get_mode(efidev, &mode_data);
	if (rc) {
		dev_warn(&efidev->pdev->dev, "%s - cannot get mode %d\n",
			 __func__, rc);
		return rc;
	}
	if ((mode_data.mode == checksum_data->mode)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - cannot check partition used by current mode\n",
			 __func__);
		return -LEICAEFI_EBADMODE;
	}

	/* execute the check */
	rc = leicaefi_chr_flash_set_op_state(efidev, "flash_ctrl_start",
					     LEICAEFI_FLASH_OP_IDLE,
					     LEICAEFI_FLASH_OP_PENDING);
	if (rc) {
		return rc;
	}

	if (leicaefi_chip_set_bits(efidev->efichip, LEICAEFI_REG_FLASH_CTRL,
				   flash_ctrl_mask) != 0) {
		dev_warn(&efidev->pdev->dev, "%s - request failed\n", __func__);

		rc = leicaefi_chr_flash_set_op_state(efidev,
						     "flash_ctrl_reqfail",
						     LEICAEFI_FLASH_OP_PENDING,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		return -EIO;
	}

	/* non-interruptible - it must be finished */
	wait_event(efidev->flash.op_wq, atomic_read(&efidev->flash.op_state) !=
						LEICAEFI_FLASH_OP_PENDING);

	state_value = atomic_read(&efidev->flash.op_state);
	if (state_value == LEICAEFI_FLASH_OP_DONE) {
		rc = leicaefi_chr_flash_set_op_state(efidev, "flash_ctrl_done",
						     LEICAEFI_FLASH_OP_DONE,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		dev_info(&efidev->pdev->dev, "%s - flash check success\n",
			 __func__);

		checksum_data->check_result = CHECK_RESULT_SUCCESS;

		return 0;
	} else if (state_value == LEICAEFI_FLASH_OP_FAILED) {
		rc = leicaefi_chr_flash_set_op_state(efidev, "flash_ctrl_fail",
						     LEICAEFI_FLASH_OP_FAILED,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		dev_info(&efidev->pdev->dev, "%s - flash check failure\n",
			 __func__);

		checksum_data->check_result = CHECK_RESULT_FAILURE;

		return 0;
	} else {
		dev_err(&efidev->pdev->dev,
			"%s - execution error (state: %d)\n", __func__,
			state_value);
		return -LEICAEFI_EINTERNAL;
	}
}

static int leicaefi_chr_flash_request_read(struct leicaefi_chr_device *efidev,
					   struct leicaefi_ioctl_flash_rw *data)
{
	if ((leicaefi_chip_write(efidev->efichip, LEICAEFI_REG_FLASH_ADDR,
				 data->address) != 0) ||
	    (leicaefi_chip_read(efidev->efichip, LEICAEFI_REG_FLASH_DATA,
				&data->value) != 0)) {
		dev_warn(&efidev->pdev->dev, "%s - request failed\n", __func__);
		return -EIO;
	}

	return 0;
}

static int
leicaefi_chr_iflash_request_read(struct leicaefi_chr_device *efidev,
				 struct leicaefi_ioctl_flash_rw *data)
{
	static const __u16 IFLASH_REG_COUNT = 64;

	if (data->address >= IFLASH_REG_COUNT) {
		dev_warn(&efidev->pdev->dev,
			 "%s - invalid register number %d\n", __func__,
			 (int)data->address);
		return -EINVAL;
	}

	if ((leicaefi_chip_write(efidev->efichip, LEICAEFI_REG_IFLASH_ADDR,
				 data->address) != 0) ||
	    (leicaefi_chip_read(efidev->efichip, LEICAEFI_REG_IFLASH_DATA,
				&data->value) != 0)) {
		dev_warn(&efidev->pdev->dev, "%s - request failed\n", __func__);
		return -EIO;
	}

	return 0;
}

static int
leicaefi_chr_flash_request_write(struct leicaefi_chr_device *efidev,
				 const struct leicaefi_ioctl_flash_rw *data)
{
	int rc = 0;
	int state_value = 0;

	rc = leicaefi_chr_flash_set_op_state(efidev, "flash_write_start",
					     LEICAEFI_FLASH_OP_IDLE,
					     LEICAEFI_FLASH_OP_PENDING);
	if (rc) {
		return rc;
	}

	if ((leicaefi_chip_write(efidev->efichip, LEICAEFI_REG_FLASH_ADDR,
				 data->address) != 0) ||
	    (leicaefi_chip_write(efidev->efichip, LEICAEFI_REG_FLASH_DATA,
				 data->value) != 0)) {
		dev_warn(&efidev->pdev->dev, "%s - request failed\n", __func__);

		rc = leicaefi_chr_flash_set_op_state(efidev,
						     "flash_write_reqfail",
						     LEICAEFI_FLASH_OP_PENDING,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		return -EIO;
	}

	/* non-interruptible - it must be finished */
	wait_event(efidev->flash.op_wq, atomic_read(&efidev->flash.op_state) !=
						LEICAEFI_FLASH_OP_PENDING);

	state_value = atomic_read(&efidev->flash.op_state);
	if (state_value == LEICAEFI_FLASH_OP_DONE) {
		rc = leicaefi_chr_flash_set_op_state(efidev, "flash_write_done",
						     LEICAEFI_FLASH_OP_DONE,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		return 0;
	} else if (state_value == LEICAEFI_FLASH_OP_FAILED) {
		rc = leicaefi_chr_flash_set_op_state(efidev, "flash_write_fail",
						     LEICAEFI_FLASH_OP_FAILED,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		return -LEICAEFI_EFLASHACCESS;
	} else {
		dev_err(&efidev->pdev->dev,
			"%s - execution error (state: %d)\n", __func__,
			state_value);
		return -LEICAEFI_EINTERNAL;
	}
}

static int
leicaefi_chr_flash_request_erase(struct leicaefi_chr_device *efidev,
				 const struct leicaefi_ioctl_flash_erase *data)
{
	int rc = 0;
	int state_value = 0;

	rc = leicaefi_chr_flash_set_op_state(efidev, "flash_erase_start",
					     LEICAEFI_FLASH_OP_IDLE,
					     LEICAEFI_FLASH_OP_PENDING);
	if (rc) {
		return rc;
	}

	if ((leicaefi_chip_write(efidev->efichip, LEICAEFI_REG_FLASH_ADDR,
				 data->address) != 0) ||
	    (leicaefi_chip_set_bits(efidev->efichip, LEICAEFI_REG_FLASH_CTRL,
				    LEICAEFI_FLASHCTRLBIT_ESEC) != 0)) {
		dev_warn(&efidev->pdev->dev, "%s - request failed\n", __func__);

		rc = leicaefi_chr_flash_set_op_state(efidev,
						     "flash_erase_reqfail",
						     LEICAEFI_FLASH_OP_PENDING,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		return -EIO;
	}

	/* non-interruptible - it must be finished */
	wait_event(efidev->flash.op_wq, atomic_read(&efidev->flash.op_state) !=
						LEICAEFI_FLASH_OP_PENDING);

	state_value = atomic_read(&efidev->flash.op_state);
	if (state_value == LEICAEFI_FLASH_OP_DONE) {
		rc = leicaefi_chr_flash_set_op_state(efidev, "flash_erase_done",
						     LEICAEFI_FLASH_OP_DONE,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		return 0;
	} else if (state_value == LEICAEFI_FLASH_OP_FAILED) {
		rc = leicaefi_chr_flash_set_op_state(efidev, "flash_erase_fail",
						     LEICAEFI_FLASH_OP_FAILED,
						     LEICAEFI_FLASH_OP_IDLE);
		if (rc) {
			return rc;
		}

		return -LEICAEFI_EFLASHACCESS;
	} else {
		dev_err(&efidev->pdev->dev,
			"%s - execution error (state: %d)\n", __func__,
			state_value);
		return -EIO;
	}
}

static int
leicaefi_chr_flash_request_write_enable(struct leicaefi_chr_device *efidev,
					const bool enable)
{
	if (enable) {
		if (leicaefi_chip_set_bits(efidev->efichip,
					   LEICAEFI_REG_FLASH_CTRL,
					   LEICAEFI_FLASHCTRLBIT_WREN) != 0) {
			dev_warn(&efidev->pdev->dev,
				 "%s - enable request failed\n", __func__);
			return -EIO;
		}
	} else {
		if (leicaefi_chip_clear_bits(efidev->efichip,
					     LEICAEFI_REG_FLASH_CTRL,
					     LEICAEFI_FLASHCTRLBIT_WREN) != 0) {
			dev_warn(&efidev->pdev->dev,
				 "%s - disable request failed\n", __func__);
			return -EIO;
		}
	}
	return 0;
}

static long
leicaefi_chr_ioctl_flash_check_checksum(struct leicaefi_chr_device *efidev,
					unsigned long arg)
{
	struct leicaefi_ioctl_flash_checksum data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	rc = leicaefi_chr_flash_exclusive_lock(efidev);
	if (rc == 0) {
		rc = leicaefi_chr_flash_request_check_checksum(efidev, &data);
		if (rc == 0) {
			rc = leicaefi_chr_copy_to_user(arg, &data,
						       sizeof(data));
		}
		leicaefi_chr_flash_exclusive_unlock(efidev);
	}

	return rc;
}

static long leicaefi_chr_ioctl_flash_read(struct leicaefi_chr_device *efidev,
					  unsigned long arg)
{
	struct leicaefi_ioctl_flash_rw data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	rc = leicaefi_chr_flash_exclusive_lock(efidev);
	if (rc == 0) {
		rc = leicaefi_chr_flash_request_read(efidev, &data);
		if (rc == 0) {
			rc = leicaefi_chr_copy_to_user(arg, &data,
						       sizeof(data));
		}

		leicaefi_chr_flash_exclusive_unlock(efidev);
	}

	return rc;
}

static long leicaefi_chr_ioctl_flash_write(struct leicaefi_chr_device *efidev,
					   unsigned long arg)
{
	struct leicaefi_ioctl_flash_rw data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	rc = leicaefi_chr_flash_exclusive_lock(efidev);
	if (rc == 0) {
		rc = leicaefi_chr_flash_request_write(efidev, &data);
		leicaefi_chr_flash_exclusive_unlock(efidev);
	}

	return rc;
}

static long
leicaefi_chr_ioctl_flash_erase_segment(struct leicaefi_chr_device *efidev,
				       unsigned long arg)
{
	struct leicaefi_ioctl_flash_erase data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	rc = leicaefi_chr_flash_exclusive_lock(efidev);
	if (rc == 0) {
		rc = leicaefi_chr_flash_request_erase(efidev, &data);
		leicaefi_chr_flash_exclusive_unlock(efidev);
	}

	return rc;
}

static long
leicaefi_chr_ioctl_flash_write_enable(struct leicaefi_chr_device *efidev,
				      unsigned long arg)
{
	struct leicaefi_ioctl_flash_write_enable data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	rc = leicaefi_chr_flash_exclusive_lock(efidev);
	if (rc == 0) {
		rc = leicaefi_chr_flash_request_write_enable(efidev,
							     data.enable != 0);
		leicaefi_chr_flash_exclusive_unlock(efidev);
	}

	return rc;
}

static long leicaefi_chr_ioctl_set_mode(struct leicaefi_chr_device *efidev,
					unsigned long arg)
{
	struct leicaefi_ioctl_mode data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	rc = leicaefi_chr_flash_exclusive_lock(efidev);
	if (rc == 0) {
		rc = leicaefi_chr_request_set_mode(efidev, &data);
		leicaefi_chr_flash_exclusive_unlock(efidev);
	}

	return rc;
}

static long leicaefi_chr_ioctl_get_mode(struct leicaefi_chr_device *efidev,
					unsigned long arg)
{
	struct leicaefi_ioctl_mode data;
	int rc = 0;

	rc = leicaefi_chr_flash_exclusive_lock(efidev);
	if (rc == 0) {
		rc = leicaefi_chr_request_get_mode(efidev, &data);
		if (rc == 0) {
			rc = leicaefi_chr_copy_to_user(arg, &data,
						       sizeof(data));
		}
		leicaefi_chr_flash_exclusive_unlock(efidev);
	}

	return rc;
}

static irqreturn_t leicaefi_chr_handle_flash_irq(int irq, void *dev_efi)
{
	struct leicaefi_chr_device *efidev =
		(struct leicaefi_chr_device *)dev_efi;

	dev_dbg(&efidev->pdev->dev, "%s\n", __func__);

	leicaefi_chr_flash_set_op_state(efidev, "flash_irq",
					LEICAEFI_FLASH_OP_PENDING,
					LEICAEFI_FLASH_OP_DONE);
	wake_up(&efidev->flash.op_wq);

	return IRQ_HANDLED;
}

static irqreturn_t leicaefi_chr_handle_flash_error_irq(int irq, void *dev_efi)
{
	struct leicaefi_chr_device *efidev =
		(struct leicaefi_chr_device *)dev_efi;

	dev_dbg(&efidev->pdev->dev, "%s\n", __func__);

	leicaefi_chr_flash_set_op_state(efidev, "flash_irq",
					LEICAEFI_FLASH_OP_PENDING,
					LEICAEFI_FLASH_OP_FAILED);
	wake_up(&efidev->flash.op_wq);

	return IRQ_HANDLED;
}

static long leicaefi_chr_ioctl_iflash_read(struct leicaefi_chr_device *efidev,
					   unsigned long arg)
{
	struct leicaefi_ioctl_flash_rw data;
	int rc = 0;

	rc = leicaefi_chr_copy_from_user(&data, arg, sizeof(data));
	if (rc) {
		return rc;
	}

	// NOTE: we are using the same lock here
	rc = leicaefi_chr_flash_exclusive_lock(efidev);
	if (rc == 0) {
		rc = leicaefi_chr_iflash_request_read(efidev, &data);
		if (rc == 0) {
			rc = leicaefi_chr_copy_to_user(arg, &data,
						       sizeof(data));
		}

		leicaefi_chr_flash_exclusive_unlock(efidev);
	}

	return rc;
}

long leicaefi_chr_flash_handle_ioctl(struct leicaefi_chr_device *efidev,
				     unsigned int cmd, unsigned long arg,
				     bool *handled)
{
	int result = -EINVAL;

	switch (cmd) {
		/* flash operations */
		// TODO: it should be discussed if we want to allow use of these stuff
		//       after the update is introduced (?) to the driver itself. Hiding
		//       the operations will protect from accidental usage
	case LEICAEFI_IOCTL_FLASH_CHECK_CHECKSUM:
		result = leicaefi_chr_ioctl_flash_check_checksum(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_FLASH_READ:
		result = leicaefi_chr_ioctl_flash_read(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_FLASH_WRITE:
		result = leicaefi_chr_ioctl_flash_write(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_FLASH_ERASE_SEGMENT:
		result = leicaefi_chr_ioctl_flash_erase_segment(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_FLASH_WRITE_ENABLE:
		result = leicaefi_chr_ioctl_flash_write_enable(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_SET_MODE:
		result = leicaefi_chr_ioctl_set_mode(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_GET_MODE:
		result = leicaefi_chr_ioctl_get_mode(efidev, arg);
		*handled = true;
		break;
	case LEICAEFI_IOCTL_IFLASH_READ:
		result = leicaefi_chr_ioctl_iflash_read(efidev, arg);
		*handled = true;
		break;
	default:
		*handled = false;
		break;
	}

	return result;
}

int leicaefi_chr_flash_init_irq(struct leicaefi_chr_device *efidev,
				int *irq_ptr, const char *irq_name,
				irq_handler_t handler_func)
{
	int rc = 0;

	*irq_ptr = platform_get_irq_byname(efidev->pdev, irq_name);
	if (*irq_ptr < 0) {
		dev_err(&efidev->pdev->dev,
			"%s - failed: cannot find irq %s (error :%d)\n",
			__func__, irq_name, *irq_ptr);
		return -EINVAL;
	}

	rc = devm_request_threaded_irq(&efidev->pdev->dev, *irq_ptr, NULL,
				       handler_func, IRQF_ONESHOT,
				       efidev->pdev->name, efidev);
	if (rc) {
		dev_err(&efidev->pdev->dev,
			"%s - failed: irq request (IRQ: %s/%d, error :%d)\n",
			__func__, irq_name, *irq_ptr, rc);
		return rc;
	}

	return 0;
}

int leicaefi_chr_flash_init(struct leicaefi_chr_device *efidev)
{
	int rc = 0;

	mutex_init(&efidev->flash.op_lock);
	atomic_set(&efidev->flash.op_state, LEICAEFI_FLASH_OP_IDLE);
	init_waitqueue_head(&efidev->flash.op_wq);

	rc = leicaefi_chr_flash_init_irq(efidev, &efidev->flash.irq_flash,
					 "LEICAEFI_FLASH",
					 leicaefi_chr_handle_flash_irq);
	if (rc) {
		return rc;
	}

	rc = leicaefi_chr_flash_init_irq(efidev, &efidev->flash.irq_flash_error,
					 "LEICAEFI_FLASH_ERROR",
					 leicaefi_chr_handle_flash_error_irq);
	if (rc) {
		return rc;
	}

	return 0;
}
