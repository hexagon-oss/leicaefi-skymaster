#ifndef _LINUX_LEICAEFI_POWER_H
#define _LINUX_LEICAEFI_POWER_H

#include <linux/power_supply.h>
#include <linux/platform_device.h>

#include <leicaefi.h>
#include <common/leicaefi-chip.h>
#include <common/leicaefi-device.h>

#define LEICAEFI_POWER_SUPPLY_NAME_EXT1 "leicaefi-pwr-ext1"
#define LEICAEFI_POWER_SUPPLY_NAME_EXT2 "leicaefi-pwr-ext2"
#define LEICAEFI_POWER_SUPPLY_NAME_POE1 "leicaefi-pwr-poe1"
#define LEICAEFI_POWER_SUPPLY_NAME_BAT1 "leicaefi-pwr-bat1"

struct leicaefi_power_device;

struct leicaefi_charger_desc {
	const struct power_supply_desc kernel_desc;
	u8 validity_bit;
};

struct leicaefi_battery_desc {
	const struct power_supply_desc kernel_desc;
	u8 validity_bit;
};

struct leicaefi_charger {
	struct leicaefi_power_device *efidev;
	const struct leicaefi_charger_desc *desc;
	struct power_supply *supply;
};

struct leicaefi_battery {
	struct leicaefi_power_device *efidev;
	const struct leicaefi_battery_desc *desc;
	struct power_supply *supply;
};

struct leicaefi_power_device {
	struct platform_device *pdev;
	struct leicaefi_chip *efichip;

	struct leicaefi_charger ext1_psy;
	struct leicaefi_charger ext2_psy;
	struct leicaefi_charger poe1_psy;
	struct leicaefi_battery bat1_psy;
};

int leicaefi_power_init_ext1(struct leicaefi_power_device *efidev);

int leicaefi_power_init_ext2(struct leicaefi_power_device *efidev);

int leicaefi_power_init_poe1(struct leicaefi_power_device *efidev);

int leicaefi_power_init_bat1(struct leicaefi_power_device *efidev);

void REMOVEME_dump_registers(struct leicaefi_power_device *efidev);

#endif /*_LINUX_LEICAEFI_POWER_H*/
