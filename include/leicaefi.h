#ifndef _LINUX_LEICAEFI_H
#define _LINUX_LEICAEFI_H

#include <linux/types.h>
#include <leicaefi-defs.h>

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

// write means user -> write-to -> kernel
#define LEICAEFI_IOCTL_MAGIC 0xDF

struct leicaefi_ioctl_regrw {
	__u8 reg_no;
	__u16 reg_value;
};

#define LEICAEFI_IOCTL_READ                                                    \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 1,                              \
	     sizeof(struct leicaefi_ioctl_regrw))
#define LEICAEFI_IOCTL_WRITE                                                   \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 2,                              \
	     sizeof(struct leicaefi_ioctl_regrw))
#define LEICAEFI_IOCTL_BITS_SET                                                \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 3,                              \
	     sizeof(struct leicaefi_ioctl_regrw))
#define LEICAEFI_IOCTL_BITS_CLEAR                                              \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 4,                              \
	     sizeof(struct leicaefi_ioctl_regrw))
#define LEICAEFI_IOCTL_WRITE_RAW                                               \
    _IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 5,                              \
         sizeof(struct leicaefi_ioctl_regrw))

#endif /*_LINUX_LEICAEFI_H*/
