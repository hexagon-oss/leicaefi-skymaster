#ifndef _LINUX_LEICAEFI_IRQ_H
#define _LINUX_LEICAEFI_IRQ_H

#include <linux/device.h>
#include <linux/irqdomain.h>

#include <common/leicaefi-chip.h>

#define LEICAEFI_IRQNO_FLASH (0)
#define LEICAEFI_IRQNO_ERR_FLASH (1)
#define LEICAEFI_IRQNO_KEY (2)
#define LEICAEFI_TOTAL_IRQ_COUNT (3)

struct leicaefi_irq_chip;

int leicaefi_add_irq_chip(struct device *dev, int irq,
			  struct leicaefi_chip *efichip,
			  struct leicaefi_irq_chip **irqchip);

int leicaefi_del_irq_chip(struct leicaefi_irq_chip *irqchip);

struct irq_domain *leicaefi_irq_get_domain(struct leicaefi_irq_chip *irqchip);

int devm_leicaefi_add_irq_chip(struct device *dev, int irq,
			       struct leicaefi_chip *efichip,
			       struct leicaefi_irq_chip **irqchip);

void devm_leicaefi_del_irq_chip(struct device *dev, int irq,
				struct leicaefi_irq_chip *irqchip);

#endif /*_LINUX_LEICAEFI_IRQ_H*/
