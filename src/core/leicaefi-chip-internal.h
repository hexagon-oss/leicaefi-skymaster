#ifndef _LINUX_LEICAEFI_CHIP_INTERNAL_H
#define _LINUX_LEICAEFI_CHIP_INTERNAL_H

#include <linux/i2c.h>
#include <common/leicaefi-chip.h>

int devm_leicaefi_add_chip(struct i2c_client *i2c,
			   struct leicaefi_chip **efichip);

#endif /*_LINUX_LEICAEFI_CHIP_INTERNAL_H*/
