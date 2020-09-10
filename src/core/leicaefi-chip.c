#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include <core/leicaefi-chip-internal.h>
#include <leicaefi-defs.h>
#include <leicaefi.h>

#include "leicaefi-irq.h"

//----------------------

enum leicaefi_gencmd_state {
	LEICAEFI_GENCMD_IDLE,
	LEICAEFI_GENCMD_PENDING,
	LEICAEFI_GENCMD_DONE,
	LEICAEFI_GENCMD_FAILED,
};

struct leicaefi_chip {
	struct i2c_client *i2c;

	unsigned int complete_irq;
	unsigned int error_irq;

	struct mutex gencmd_lock;
	atomic_t gencmd_state;
	wait_queue_head_t gencmd_wq;
};

static int leicaefi_chip_gencmd_exclusive_lock(struct leicaefi_chip *efichip)
{
	return mutex_lock_interruptible(&efichip->gencmd_lock);
}

static void leicaefi_chip_gencmd_exclusive_unlock(struct leicaefi_chip *efichip)
{
	return mutex_unlock(&efichip->gencmd_lock);
}

static int leicaefi_chip_set_gencmd_state(struct leicaefi_chip *efichip,
					  const char *op_info,
					  int expected_state, int next_state)
{
	int prev_value = atomic_cmpxchg(&efichip->gencmd_state, expected_state,
					next_state);
	if (prev_value != expected_state) {
		dev_err(&efichip->i2c->dev,
			"%s - cannot set operation state for %s (state: %d, target: %d)\n",
			__func__, op_info, prev_value, next_state);
		return -LEICAEFI_EINTERNAL;
	}

	return 0;
}

static bool leicaefi_chip_is_valid_register_number(u8 reg_no)
{
	// bits other than register number must not be set
	return (reg_no & ~LEICAEFI_REGNO_MASK) == 0;
}

static u8 leicaefi_chip_make_command(u8 reg_no, u8 rwbit, u8 scbit)
{
	return (reg_no & LEICAEFI_REGNO_MASK) | rwbit | scbit;
}

int leicaefi_chip_set_bits(struct leicaefi_chip *efichip, u8 reg_no, u16 mask)
{
	struct device *dev = &efichip->i2c->dev;
	int rc = 0;
	u8 reg = leicaefi_chip_make_command(reg_no, LEICAEFI_RWBIT_WRITE,
					    LEICAEFI_SCBIT_SET);

	if (!leicaefi_chip_is_valid_register_number(reg_no)) {
		return -EINVAL;
	}

	rc = i2c_smbus_write_word_data(efichip->i2c, reg, mask);

	dev_dbg(dev, "%s - reg=0x%02X val=0x%04X - rc=%d\n", __func__,
		(unsigned)(reg_no), (unsigned)mask, rc);

	return rc;
}
EXPORT_SYMBOL(leicaefi_chip_set_bits);

int leicaefi_chip_clear_bits(struct leicaefi_chip *efichip, u8 reg_no, u16 mask)
{
	struct device *dev = &efichip->i2c->dev;
	int rc = 0;
	u8 reg = leicaefi_chip_make_command(reg_no, LEICAEFI_RWBIT_WRITE,
					    LEICAEFI_SCBIT_CLEAR);

	if (!leicaefi_chip_is_valid_register_number(reg_no)) {
		return -EINVAL;
	}

	rc = i2c_smbus_write_word_data(efichip->i2c, reg, mask);

	dev_dbg(dev, "%s - reg=0x%02X val=0x%04X - rc=%d\n", __func__,
		(unsigned)(reg_no), (unsigned)mask, rc);

	return rc;
}
EXPORT_SYMBOL(leicaefi_chip_clear_bits);

int leicaefi_chip_write(struct leicaefi_chip *efichip, u8 reg_no, u16 value)
{
	struct device *dev = &efichip->i2c->dev;
	int rc = 0;
	u8 reg = leicaefi_chip_make_command(reg_no, LEICAEFI_RWBIT_WRITE,
					    LEICAEFI_SCBIT_UNUSED);

	if (!leicaefi_chip_is_valid_register_number(reg_no)) {
		return -EINVAL;
	}

	rc = i2c_smbus_write_word_data(efichip->i2c, reg, value);

	dev_dbg(dev, "%s - reg=0x%02X val=0x%04X - rc=%d\n", __func__,
		(unsigned)(reg_no), (unsigned)value, rc);

	return rc;
}
EXPORT_SYMBOL(leicaefi_chip_write);

int leicaefi_chip_read(struct leicaefi_chip *efichip, u8 reg_no, u16 *value_ptr)
{
	struct device *dev = &efichip->i2c->dev;
	int rc = 0;
	s32 value = 0;
	u8 reg = leicaefi_chip_make_command(reg_no, LEICAEFI_RWBIT_READ,
					    LEICAEFI_SCBIT_UNUSED);

	if (!leicaefi_chip_is_valid_register_number(reg_no)) {
		return -EINVAL;
	}

	if (!value_ptr) {
		return -EINVAL;
	}

	value = i2c_smbus_read_word_data(efichip->i2c, reg);
	if (value >= 0) {
		*value_ptr = (u16)value;
		rc = 0;
	} else {
		*value_ptr = 0;
		rc = (int)value;
	}

	dev_dbg(dev, "%s - reg=0x%02X - rc=%d (val=0x%04X)\n", __func__,
		(unsigned)(reg_no), rc, (unsigned)*value_ptr);

	return rc;
}
EXPORT_SYMBOL(leicaefi_chip_read);

static int leicaefi_chip_gencmd_request(struct leicaefi_chip *efichip, u16 cmd,
					u16 input_data, u16 *output_data_ptr)
{
	int rc = 0;
	int state_value = 0;

	rc = leicaefi_chip_set_gencmd_state(efichip, "gencmd_start",
					    LEICAEFI_GENCMD_IDLE,
					    LEICAEFI_GENCMD_PENDING);
	if (rc) {
		return rc;
	}

	if ((leicaefi_chip_write(efichip, LEICAEFI_REG_CMD_DATA, input_data) !=
	     0) ||
	    (leicaefi_chip_write(efichip, LEICAEFI_REG_CMD_CTRL, cmd) != 0)) {
		dev_warn(&efichip->i2c->dev, "%s - request failed\n", __func__);

		rc = leicaefi_chip_set_gencmd_state(efichip, "gencmd_reqfail",
						    LEICAEFI_GENCMD_PENDING,
						    LEICAEFI_GENCMD_IDLE);
		if (rc) {
			return rc;
		}

		return -EIO;
	}

	/* non-interruptible - it must be finished */
	wait_event(efichip->gencmd_wq, atomic_read(&efichip->gencmd_state) !=
					       LEICAEFI_GENCMD_PENDING);

	state_value = atomic_read(&efichip->gencmd_state);
	if (state_value == LEICAEFI_GENCMD_DONE) {
		rc = leicaefi_chip_set_gencmd_state(efichip, "gencmd_done",
						    LEICAEFI_GENCMD_DONE,
						    LEICAEFI_GENCMD_IDLE);
		if (rc) {
			return rc;
		}

		if (output_data_ptr != NULL) {
			if (leicaefi_chip_read(efichip, LEICAEFI_REG_CMD_DATA,
					       output_data_ptr) != 0) {
				dev_warn(&efichip->i2c->dev,
					 "%s - read failed\n", __func__);
				return -EIO;
			}
		}

		return 0;
	} else if (state_value == LEICAEFI_GENCMD_FAILED) {
		rc = leicaefi_chip_set_gencmd_state(efichip, "gencmd_fail",
						    LEICAEFI_GENCMD_FAILED,
						    LEICAEFI_GENCMD_IDLE);
		if (rc) {
			return rc;
		}

		return -LEICAEFI_EGENCMDFAIL;
	} else {
		dev_err(&efichip->i2c->dev,
			"%s - execution error (state: %d)\n", __func__,
			state_value);
		return -LEICAEFI_EINTERNAL;
	}
}

int leicaefi_chip_gencmd(struct leicaefi_chip *efichip, u16 cmd, u16 input_data,
			 u16 *output_data_ptr)
{
	int rc = 0;

	rc = leicaefi_chip_gencmd_exclusive_lock(efichip);
	if (rc == 0) {
		rc = leicaefi_chip_gencmd_request(efichip, cmd, input_data,
						  output_data_ptr);

		leicaefi_chip_gencmd_exclusive_unlock(efichip);
	}

	return rc;
}
EXPORT_SYMBOL(leicaefi_chip_gencmd);

static irqreturn_t leicaefi_chip_gencmd_complete_irq_handler(int irq,
							     void *context)
{
	struct leicaefi_chip *efichip = context;

	dev_dbg(&efichip->i2c->dev, "%s\n", __func__);

	leicaefi_chip_set_gencmd_state(efichip, "complete_irq",
				       LEICAEFI_GENCMD_PENDING,
				       LEICAEFI_GENCMD_DONE);
	wake_up(&efichip->gencmd_wq);

	return IRQ_HANDLED;
}

static irqreturn_t leicaefi_chip_gencmd_error_irq_handler(int irq,
							  void *context)
{
	struct leicaefi_chip *efichip = context;

	dev_dbg(&efichip->i2c->dev, "%s\n", __func__);

	leicaefi_chip_set_gencmd_state(efichip, "error_irq",
				       LEICAEFI_GENCMD_PENDING,
				       LEICAEFI_GENCMD_FAILED);
	wake_up(&efichip->gencmd_wq);

	return IRQ_HANDLED;
}

static int leicaefi_add_chip(struct i2c_client *i2c,
			     struct leicaefi_chip **efichip)
{
	struct leicaefi_chip *chip = NULL;

	dev_dbg(&i2c->dev, "%s\n", __func__);

	if (!efichip) {
		return -EINVAL;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		return -ENOMEM;
	}

	chip->i2c = i2c;

	mutex_init(&chip->gencmd_lock);
	atomic_set(&chip->gencmd_state, LEICAEFI_GENCMD_IDLE);
	init_waitqueue_head(&chip->gencmd_wq);

	*efichip = chip;

	return 0;
}

static int leicaefi_del_chip(struct leicaefi_chip *efichip)
{
	dev_dbg(&efichip->i2c->dev, "%s\n", __func__);

	// there should be no need to dispose irq mapping
	// as irq chip will clean it up anyway

	kfree(efichip);

	return 0;
}

int leicaefi_chip_init(struct leicaefi_chip *efichip,
		       struct leicaefi_irq_chip *irqchip)
{
	struct device *dev = &efichip->i2c->dev;
	int rv = 0;

	efichip->complete_irq =
		irq_create_mapping(leicaefi_irq_get_domain(irqchip),
				   LEICAEFI_IRQNO_GENCMD_COMPLETE);
	if (efichip->complete_irq == 0) {
		dev_err(dev,
			"%s - cannot find mapping for command complete IRQ\n",
			__func__);
		return -ENOENT;
	}

	efichip->error_irq = irq_create_mapping(
		leicaefi_irq_get_domain(irqchip), LEICAEFI_IRQNO_GENCMD_ERROR);
	if (efichip->error_irq == 0) {
		dev_err(dev, "%s - cannot find mapping for command error IRQ\n",
			__func__);
		return -ENOENT;
	}

	rv = devm_request_threaded_irq(
		dev, efichip->complete_irq, NULL,
		leicaefi_chip_gencmd_complete_irq_handler, IRQF_ONESHOT, NULL,
		efichip);
	if (rv < 0) {
		dev_err(dev, "failed: irq request (IRQ: %d, error :%d)\n",
			efichip->complete_irq, rv);
		return rv;
	}

	rv = devm_request_threaded_irq(dev, efichip->error_irq, NULL,
				       leicaefi_chip_gencmd_error_irq_handler,
				       IRQF_ONESHOT, NULL, efichip);
	if (rv < 0) {
		dev_err(dev, "failed: irq request (IRQ: %d, error :%d)\n",
			efichip->error_irq, rv);
		return rv;
	}

	return 0;
}

static void devm_leicaefi_chip_release(struct device *dev, void *res)
{
	struct leicaefi_chip *d = *(struct leicaefi_chip **)res;

	dev_dbg(dev, "%s\n", __func__);

	leicaefi_del_chip(d);
}

static int devm_leicaefi_chip_match(struct device *dev, void *res, void *data)
{
	struct leicaefi_chip **r = res;

	dev_dbg(dev, "%s\n", __func__);

	if (!r || !*r) {
		WARN_ON(!r || !*r);
		return 0;
	}
	return *r == data;
}

int devm_leicaefi_add_chip(struct i2c_client *i2c,
			   struct leicaefi_chip **efichip)
{
	struct device *dev = &i2c->dev;
	struct leicaefi_chip **res_ptr = NULL;
	struct leicaefi_chip *chip = NULL;
	int rc = 0;

	dev_dbg(dev, "%s\n", __func__);

	if (!efichip) {
		dev_err(dev, "%s - efichip argument is NULL\n", __func__);
		return -EINVAL;
	}

	res_ptr = devres_alloc(devm_leicaefi_chip_release, sizeof(*res_ptr),
			       GFP_KERNEL);
	if (!res_ptr) {
		dev_err(dev, "%s - cannot allocate memory\n", __func__);
		return -ENOMEM;
	}

	rc = leicaefi_add_chip(i2c, &chip);
	if (rc != 0) {
		dev_err(dev, "%s - adding chip failed: %d\n", __func__, rc);
		devres_free(res_ptr);
		return rc;
	}

	*res_ptr = chip;
	devres_add(dev, res_ptr);

	*efichip = chip;

	return 0;
}

void devm_leicaefi_del_chip(struct device *dev, struct leicaefi_chip *efichip)
{
	int rc = 0;

	dev_dbg(dev, "%s\n", __func__);

	rc = devres_release(dev, devm_leicaefi_chip_release,
			    devm_leicaefi_chip_match, efichip);
	if (rc != 0) {
		dev_err(dev, "%s - releasing resource failed: %d\n", __func__,
			rc);
		WARN_ON(rc);
	}
}
