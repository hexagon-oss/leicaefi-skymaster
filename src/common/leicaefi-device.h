#ifndef _LINUX_LEICAEFI_DEVICE_H
#define _LINUX_LEICAEFI_DEVICE_H

/* Handle for chip access. */
struct leicaefi_chip;

/* Platform data that will be passed to MFD devices. */
struct leicaefi_platform_data {
	struct leicaefi_chip *efichip;
};

#endif /*_LINUX_LEICAEFI_DEVICE_H*/
