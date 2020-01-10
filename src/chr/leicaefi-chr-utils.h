#ifndef _LINUX_LEICAEFI_CHR_UTILS_H
#define _LINUX_LEICAEFI_CHR_UTILS_H

#include <linux/errno.h>
#include <linux/types.h>

static inline int leicaefi_chr_copy_from_user(void *dst, unsigned long arg,
					      const size_t data_size)
{
	void __user *src = (void __user *)arg;

	if (arg == 0) {
		return -EFAULT;
	}

	if (copy_from_user(dst, src, data_size) != 0) {
		return -EACCES;
	}

	return 0;
}

static inline int leicaefi_chr_copy_to_user(unsigned long arg, const void *src,
					    const size_t data_size)
{
	void __user *dst = (void __user *)arg;

	if (arg == 0) {
		return -EFAULT;
	}

	if (copy_to_user(dst, src, data_size) != 0) {
		return -EACCES;
	}

	return 0;
}

#endif /*_LINUX_LEICAEFI_CHR_UTILS_H*/
