#ifndef _LINUX_LEICAEFI_UTILS_H
#define _LINUX_LEICAEFI_UTILS_H

#include <linux/types.h>

struct leicaefi_chip;

// TODO?: add 'bool user_access' and protect important registers (interrupts, flash)
//        from direct access by the user

int leicaefi_chip_set_bits(struct leicaefi_chip *efichip, u8 reg_no, u16 mask);

int leicaefi_chip_clear_bits(struct leicaefi_chip *efichip, u8 reg_no,
			     u16 mask);

int leicaefi_chip_write(struct leicaefi_chip *efichip, u8 reg_no, u16 value);

int leicaefi_chip_read(struct leicaefi_chip *efichip, u8 reg_no,
		       u16 *value_ptr);

int leicaefi_chip_gencmd(struct leicaefi_chip *efichip, u16 cmd, u16 input_data,
			 u16 *output_data_ptr);

#endif /*_LINUX_LEICAEFI_UTILS_H*/
