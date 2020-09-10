#ifndef _LINUX_LEICAEFI_CHR_H
#define _LINUX_LEICAEFI_CHR_H

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/cdev.h>

#include <common/leicaefi-device.h>

struct leicaefi_chr_flash {
	int irq_flash_complete;
	int irq_flash_error;
	struct mutex op_lock;
	atomic_t op_state;
	wait_queue_head_t op_wq;
};

struct leicaefi_chr_device {
	struct platform_device *pdev;
	struct leicaefi_chip *efichip;

	bool chr_region_allocated;
	dev_t chr_dev;
	struct cdev chr_cdev;
	bool chr_cdev_added;
	bool chr_device_created;
	struct file_operations chr_file_ops;
	int chr_major;

	struct leicaefi_chr_flash flash;
};

long leicaefi_chr_reg_handle_ioctl(struct leicaefi_chr_device *efidev,
				   unsigned int cmd, unsigned long arg,
				   bool *handled);

long leicaefi_chr_flash_handle_ioctl(struct leicaefi_chr_device *efidev,
				     unsigned int cmd, unsigned long arg,
				     bool *handled);

long leicaefi_chr_power_handle_ioctl(struct leicaefi_chr_device *efidev,
				     unsigned int cmd, unsigned long arg,
				     bool *handled);

int leicaefi_chr_flash_init(struct leicaefi_chr_device *efidev);

#endif /*_LINUX_LEICAEFI_CHR_H*/
