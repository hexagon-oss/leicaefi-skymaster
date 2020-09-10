#ifndef _LINUX_LEICAEFI_CHIP_INTERNAL_H
#define _LINUX_LEICAEFI_CHIP_INTERNAL_H

#include <linux/i2c.h>
#include <common/leicaefi-chip.h>

#include "leicaefi-irq.h"

int devm_leicaefi_add_chip(struct i2c_client *i2c,
			   struct leicaefi_chip **efichip);

void devm_leicaefi_del_chip(struct device *dev, struct leicaefi_chip *efichip);

int leicaefi_chip_init(struct leicaefi_chip *efichip,
		       struct leicaefi_irq_chip *irqchip);

#endif /*_LINUX_LEICAEFI_CHIP_INTERNAL_H*/
