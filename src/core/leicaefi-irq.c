#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>

#include <core/leicaefi-irq.h>
#include <common/leicaefi-chip.h>
#include <leicaefi-defs.h>

struct leicaefi_irq_descriptor {
	u16 reg_mask;
	bool is_error;
};

static struct leicaefi_irq_descriptor leicaefi_irq_descriptors[LEICAEFI_TOTAL_IRQ_COUNT] = {
    [LEICAEFI_IRQNO_FLASH] =
    {
		.is_error = false,
		.reg_mask = LEICAEFI_IRQBIT_FLASH,
	},
	[LEICAEFI_IRQNO_ERR_FLASH] =
	{
		.is_error = true,
        .reg_mask = LEICAEFI_ERRBIT_FLASH,
	},
    [LEICAEFI_IRQNO_KEY] =
    {
        .is_error = false,
        .reg_mask = LEICAEFI_IRQBIT_KEY,
    },
    [LEICAEFI_IRQNO_GENCMD_COMPLETE] =
    {
        .is_error = false,
        .reg_mask = LEICAEFI_IRQBIT_GCC,
    },
    [LEICAEFI_IRQNO_GENCMD_ERROR] =
    {
        .is_error = true,
        .reg_mask = LEICAEFI_IRQBIT_GCC,
    },
};

struct leicaefi_irq_chip {
	struct device *dev;
	int irq;
	struct leicaefi_chip *efichip;

	struct mutex lock;

	struct irq_domain *domain;
	bool irq_requested;

	int wake_count;
	bool irq_mask_current[LEICAEFI_TOTAL_IRQ_COUNT];
	bool irq_mask_requested[LEICAEFI_TOTAL_IRQ_COUNT];
};

static void leicaefi_irq_chip_lock(struct irq_data *data)
{
	struct leicaefi_irq_chip *chip = irq_data_get_irq_chip_data(data);

	dev_dbg(chip->dev, "%s\n", __func__);

	mutex_lock(&chip->lock);
}

static void leicaefi_irq_chip_sync_unlock(struct irq_data *data)
{
	struct leicaefi_irq_chip *chip = irq_data_get_irq_chip_data(data);
	int i = 0;
	u16 ie_enable_mask = 0;
	u16 ie_disable_mask = 0;
	int rc = 0;

	dev_dbg(chip->dev, "%s\n", __func__);

	/* process changes */
	for (i = 0; i < LEICAEFI_TOTAL_IRQ_COUNT; ++i) {
		if (chip->irq_mask_current[i] == chip->irq_mask_requested[i]) {
			continue;
		}

		chip->irq_mask_current[i] = chip->irq_mask_requested[i];

		if (leicaefi_irq_descriptors[i].is_error) {
			continue;
		}

		if (chip->irq_mask_current[i]) {
			ie_enable_mask |= leicaefi_irq_descriptors[i].reg_mask;
		} else {
			ie_disable_mask |= leicaefi_irq_descriptors[i].reg_mask;
		}
	}

	/* update interrupt regiter */
	dev_dbg(chip->dev,
		"%s - interrupts bit changes: enable=%X disable=%X\n", __func__,
		(unsigned int)ie_enable_mask, (unsigned int)ie_disable_mask);

	if (ie_disable_mask) {
		rc = leicaefi_chip_clear_bits(
			chip->efichip, LEICAEFI_REG_MOD_IE, ie_disable_mask);
		if (rc != 0) {
			dev_err(chip->dev, "%s - disabling interrupts failed\n",
				__func__);
		}
	}
	if (ie_enable_mask) {
		rc = leicaefi_chip_set_bits(chip->efichip, LEICAEFI_REG_MOD_IE,
					    ie_enable_mask);
		if (rc != 0) {
			dev_err(chip->dev, "%s - enabling interrupts failed\n",
				__func__);
		}
	}

	/* if wakeup count changed propagate it to the parent */
	if (chip->wake_count < 0) {
		for (i = chip->wake_count; i < 0; i++) {
			irq_set_irq_wake(chip->irq, 0);
		}
	} else if (chip->wake_count > 0) {
		for (i = 0; i < chip->wake_count; i++) {
			irq_set_irq_wake(chip->irq, 1);
		}
	}

	chip->wake_count = 0;

	mutex_unlock(&chip->lock);
}

static void leicaefi_irq_enable(struct irq_data *data)
{
	struct leicaefi_irq_chip *chip = irq_data_get_irq_chip_data(data);

	dev_dbg(chip->dev, "%s\n", __func__);

	chip->irq_mask_requested[data->hwirq] = true;
}

static void leicaefi_irq_disable(struct irq_data *data)
{
	struct leicaefi_irq_chip *chip = irq_data_get_irq_chip_data(data);

	dev_dbg(chip->dev, "%s\n", __func__);

	chip->irq_mask_requested[data->hwirq] = false;
}

static int leicaefi_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct leicaefi_irq_chip *chip = irq_data_get_irq_chip_data(data);

	dev_dbg(chip->dev, "%s\n", __func__);

	// this function is required by kernel API but we do not handle other types
	// (normally it is not called)

	if (type != IRQ_TYPE_LEVEL_HIGH) {
		return -EINVAL;
	}

	return 0;
}

static int leicaefi_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct leicaefi_irq_chip *chip = irq_data_get_irq_chip_data(data);

	dev_dbg(chip->dev, "%s\n", __func__);

	if (on) {
		chip->wake_count++;
	} else {
		chip->wake_count--;
	}

	return 0;
}

static struct irq_chip leicaefi_irq_chip = {
	.name = "leicaefi_irqchip",
	.irq_bus_lock = leicaefi_irq_chip_lock,
	.irq_bus_sync_unlock = leicaefi_irq_chip_sync_unlock,
	.irq_disable = leicaefi_irq_disable,
	.irq_enable = leicaefi_irq_enable,
	.irq_set_type = leicaefi_irq_set_type,
	.irq_set_wake = leicaefi_irq_set_wake,
};

static int leicaefi_irq_domain_map(struct irq_domain *domain,
				   unsigned int virt_irq_num,
				   irq_hw_number_t hw_irq_num)
{
	struct leicaefi_irq_chip *chip = domain->host_data;

	dev_dbg(chip->dev, "%s virq=%u hw=%lu\n", __func__, virt_irq_num,
		hw_irq_num);

	irq_set_chip_data(virt_irq_num, chip);
	irq_set_chip(virt_irq_num, &leicaefi_irq_chip);
	irq_set_nested_thread(virt_irq_num, 1);
	irq_set_parent(virt_irq_num, chip->irq);
	irq_set_noprobe(virt_irq_num);

	return 0;
}

static const struct irq_domain_ops leicaefi_irq_domain_ops = {
	.map = leicaefi_irq_domain_map,
	.xlate = irq_domain_xlate_onetwocell,
};

static irqreturn_t leicaefi_irq_thread(int irq, void *cookie)
{
	struct leicaefi_irq_chip *chip = cookie;
	u16 ifg_value = 0;
	u16 err_value = 0;
	int rc = 0;
	int i = 0;

	dev_dbg(chip->dev, "%s\n", __func__);

	/* read the interrupts */
	rc = leicaefi_chip_read(chip->efichip, LEICAEFI_REG_MOD_IFG,
				&ifg_value);
	if (rc != 0) {
		dev_err(chip->dev, "%s - failed to read IFG register: %d\n",
			__func__, rc);
		return IRQ_HANDLED;
	}

	if (ifg_value & LEICAEFI_IRQBIT_ERR) {
		rc = leicaefi_chip_read(chip->efichip, LEICAEFI_REG_MOD_ERR,
					&err_value);
		if (rc != 0) {
			dev_err(chip->dev,
				"%s - failed to read ERR register: %d\n",
				__func__, rc);
			return IRQ_HANDLED;
		}
	}

	/* process interrupts */
	for (i = 0; i < LEICAEFI_TOTAL_IRQ_COUNT; ++i) {
		if (!chip->irq_mask_current[i]) {
			continue;
		}

		if (leicaefi_irq_descriptors[i].is_error) {
			if (err_value & leicaefi_irq_descriptors[i].reg_mask) {
				handle_nested_irq(
					irq_find_mapping(chip->domain, i));
			}
		} else {
			if (ifg_value & leicaefi_irq_descriptors[i].reg_mask) {
				handle_nested_irq(
					irq_find_mapping(chip->domain, i));
			}
		}
	}

	return IRQ_HANDLED;
}

static int leicaefi_irq_chip_init(struct leicaefi_irq_chip *chip)
{
	int rc = 0;

	dev_dbg(chip->dev, "%s - Registering %d interrupts\n", __func__,
		LEICAEFI_TOTAL_IRQ_COUNT);

	mutex_init(&chip->lock);

	chip->domain = irq_domain_add_linear(chip->dev->of_node,
					     LEICAEFI_TOTAL_IRQ_COUNT,
					     &leicaefi_irq_domain_ops, chip);
	if (!chip->domain) {
		dev_err(chip->dev, "%s - failed to create IRQ domain\n",
			__func__);
		return -ENOMEM;
	}

	rc = request_threaded_irq(chip->irq, NULL, leicaefi_irq_thread,
				  IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				  "leicaefi-irq", chip);
	if (rc != 0) {
		dev_err(chip->dev, "%s - failed to request IRQ %d: %d\n",
			__func__, chip->irq, rc);
		return rc;
	}
	chip->irq_requested = true;

	/* initialize the IRQs */
	{
		u16 value;

		/* - clear all interrupts */
		rc = leicaefi_chip_clear_bits(chip->efichip,
					      LEICAEFI_REG_MOD_IE, 0xFFFF);
		if (rc != 0) {
			dev_err(chip->dev,
				"%s - failed to clear enabled interrupts mask: %d\n",
				__func__, rc);
			return rc;
		}

		/* - clear pending interrupts */
		rc = leicaefi_chip_read(chip->efichip, LEICAEFI_REG_MOD_IFG,
					&value);
		if (rc != 0) {
			dev_err(chip->dev,
				"%s - failed to clear IFG register: %d\n",
				__func__, rc);
			return rc;
		}
		rc = leicaefi_chip_read(chip->efichip, LEICAEFI_REG_MOD_ERR,
					&value);
		if (rc != 0) {
			dev_err(chip->dev,
				"%s - failed to clear ERR register: %d\n",
				__func__, rc);
			return rc;
		}

		/* - enable ERR interrupt */
		rc = leicaefi_chip_set_bits(chip->efichip, LEICAEFI_REG_MOD_IE,
					    LEICAEFI_IRQBIT_ERR);
		if (rc != 0) {
			dev_err(chip->dev,
				"%s - failed to enable ERR interrupt: %d\n",
				__func__, rc);
			return rc;
		}
	}

	return 0;
}

int leicaefi_add_irq_chip(struct device *dev, int irq,
			  struct leicaefi_chip *efichip,
			  struct leicaefi_irq_chip **irqchip)
{
	struct leicaefi_irq_chip *chip = NULL;
	int rc = 0;

	dev_dbg(dev, "%s\n", __func__);

	if (!irqchip) {
		dev_err(dev, "%s - irqchip argument is NULL\n", __func__);
		return -EINVAL;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(dev, "%s - cannot allocate memory for IRQ\n", __func__);
		return -ENOMEM;
	}

	chip->dev = dev;
	chip->irq = irq;
	chip->efichip = efichip;

	rc = leicaefi_irq_chip_init(chip);
	if (rc != 0) {
		leicaefi_del_irq_chip(chip);
		return rc;
	}

	*irqchip = chip;

	return 0;
}

int leicaefi_del_irq_chip(struct leicaefi_irq_chip *irqchip)
{
	struct leicaefi_irq_chip *chip = irqchip;

	dev_dbg(chip->dev, "%s\n", __func__);

	if (chip->irq_requested) {
		free_irq(chip->irq, chip);
	}

	if (chip->domain) {
		int hwirq;

		/* Dispose all virtual irq from irq domain before removing it */
		for (hwirq = 0; hwirq < LEICAEFI_TOTAL_IRQ_COUNT; ++hwirq) {
			/*
             * Find the virtual irq of hwirq on chip and if it is
             * there then dispose it
             */
			unsigned int virq =
				irq_find_mapping(chip->domain, hwirq);
			if (virq) {
				irq_dispose_mapping(virq);
			}
		}

		/* remove the domain */
		irq_domain_remove(chip->domain);
		chip->domain = NULL;
	}

	kfree(chip);

	return 0;
}

struct irq_domain *leicaefi_irq_get_domain(struct leicaefi_irq_chip *irqchip)
{
	if (!irqchip) {
		return NULL;
	}

	dev_dbg(irqchip->dev, "%s\n", __func__);

	return irqchip->domain;
}

static void devm_leicaefi_irq_chip_release(struct device *dev, void *res)
{
	struct leicaefi_irq_chip *d = *(struct leicaefi_irq_chip **)res;

	dev_dbg(dev, "%s\n", __func__);

	leicaefi_del_irq_chip(d);
}

static int devm_leicaefi_irq_chip_match(struct device *dev, void *res,
					void *data)
{
	struct leicaefi_irq_chip **r = res;

	dev_dbg(dev, "%s\n", __func__);

	if (!r || !*r) {
		WARN_ON(!r || !*r);
		return 0;
	}
	return *r == data;
}

int devm_leicaefi_add_irq_chip(struct device *dev, int irq,
			       struct leicaefi_chip *efichip,
			       struct leicaefi_irq_chip **irqchip)
{
	struct leicaefi_irq_chip **res_ptr = NULL;
	struct leicaefi_irq_chip *chip = NULL;
	int rc = 0;

	dev_dbg(dev, "%s\n", __func__);

	if (!irqchip) {
		dev_err(dev, "%s - irqchip argument is NULL\n", __func__);
		return -EINVAL;
	}

	res_ptr = devres_alloc(devm_leicaefi_irq_chip_release, sizeof(*res_ptr),
			       GFP_KERNEL);
	if (!res_ptr) {
		dev_err(dev, "%s - cannot allocate memory\n", __func__);
		return -ENOMEM;
	}

	rc = leicaefi_add_irq_chip(dev, irq, efichip, &chip);
	if (rc != 0) {
		dev_err(dev, "%s - adding IRQ chip failed: %d\n", __func__, rc);
		devres_free(res_ptr);
		return rc;
	}

	*res_ptr = chip;
	devres_add(dev, res_ptr);

	*irqchip = chip;

	return 0;
}

void devm_leicaefi_del_irq_chip(struct device *dev, int irq,
				struct leicaefi_irq_chip *irqchip)
{
	int rc = 0;

	dev_dbg(dev, "%s\n", __func__);

	WARN_ON(irq != irqchip->irq);

	rc = devres_release(dev, devm_leicaefi_irq_chip_release,
			    devm_leicaefi_irq_chip_match, irqchip);
	if (rc != 0) {
		dev_err(dev, "%s - releasing resource failed: %d\n", __func__,
			rc);
		WARN_ON(rc);
	}
}
