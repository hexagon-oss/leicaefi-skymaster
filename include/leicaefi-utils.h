#ifndef _LINUX_LEICAEFI_UTILS_H
#define _LINUX_LEICAEFI_UTILS_H

#include <linux/types.h>
#include <linux/regmap.h>

#include "leicaefi-defs.h"

static inline bool leicaefi_is_valid_register_number(u8 reg_no)
{
	// bits other than register number must not be set
	return (reg_no & ~LEICAEFI_REGNO_MASK) == 0;
}

static inline int leicaefi_set_bits(struct regmap *regmap, u8 reg_no, u16 mask)
{
	unsigned int reg = reg_no & LEICAEFI_REGNO_MASK;
	reg |= LEICAEFI_RWBIT_WRITE;
	reg |= LEICAEFI_SCBIT_SET;

	if (!leicaefi_is_valid_register_number(reg_no)) {
		return -EINVAL;
	}

	return regmap_write(regmap, reg, mask);
}

static inline int leicaefi_clear_bits(struct regmap *regmap, u8 reg_no,
				      u16 mask)
{
	unsigned int reg = reg_no & LEICAEFI_REGNO_MASK;
	reg |= LEICAEFI_RWBIT_WRITE;
	reg |= LEICAEFI_SCBIT_CLEAR;

	if (!leicaefi_is_valid_register_number(reg_no)) {
		return -EINVAL;
	}

	return regmap_write(regmap, reg, mask);
}

static inline int leicaefi_write(struct regmap *regmap, u8 reg_no, u16 value)
{
	unsigned int reg = reg_no & LEICAEFI_REGNO_MASK;
	reg |= LEICAEFI_RWBIT_WRITE;
	reg |= LEICAEFI_SCBIT_UNUSED;

	if (!leicaefi_is_valid_register_number(reg_no)) {
		return -EINVAL;
	}

	return regmap_write(regmap, reg, value);
}

static inline int leicaefi_read(struct regmap *regmap, u8 reg_no,
				u16 *value_ptr)
{
	unsigned int value = 0;
	unsigned int reg = reg_no & LEICAEFI_REGNO_MASK;
	int ret = 0;

	reg |= LEICAEFI_RWBIT_READ;
	reg |= LEICAEFI_SCBIT_UNUSED;

	if (!leicaefi_is_valid_register_number(reg_no)) {
		return -EINVAL;
	}

	if (!value_ptr) {
		return -EINVAL;
	}

	ret = regmap_read(regmap, reg, &value);
	if (ret == 0) {
		*value_ptr = (u16)value;
	}

	return ret;
}

#endif /*_LINUX_LEICAEFI_UTILS_H*/
