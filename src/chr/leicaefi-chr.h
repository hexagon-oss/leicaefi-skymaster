#ifndef _LINUX_LEICAEFI_CHR_H
#define _LINUX_LEICAEFI_CHR_H

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/cdev.h>

#include <common/leicaefi-device.h>

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
};

long leicaefi_chr_reg_handle_ioctl(struct leicaefi_chr_device *efidev,
				   unsigned int cmd, unsigned long arg,
				   bool *handled);

#endif /*_LINUX_LEICAEFI_CHR_H*/
