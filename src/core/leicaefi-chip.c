#include <linux/kernel.h>
#include <linux/errno.h>

#include <core/leicaefi-chip-internal.h>
#include <leicaefi-defs.h>

//----------------------

struct leicaefi_chip {
	struct i2c_client *i2c;
};

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

int devm_leicaefi_add_chip(struct i2c_client *i2c,
			   struct leicaefi_chip **efichip)
{
	struct leicaefi_chip *chip = NULL;

	if (!efichip) {
		return -EINVAL;
	}

	chip = devm_kzalloc(&i2c->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		return -ENOMEM;
	}

	chip->i2c = i2c;

	*efichip = chip;

	return 0;
}
