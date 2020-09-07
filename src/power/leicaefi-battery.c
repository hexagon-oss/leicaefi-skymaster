#include "leicaefi-power.h"

static int leicaefi_battery_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val);

static int leicaefi_battery_set_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 const union power_supply_propval *val);

static int
leicaefi_battery_property_is_writeable(struct power_supply *psy,
				       enum power_supply_property psp);

static enum power_supply_property leicaefi_power_battery_properties[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
};

static const struct leicaefi_battery_desc leicaefi_bat1_psy_desc = {
    .kernel_desc = {
        .name = LEICAEFI_POWER_SUPPLY_NAME_BAT1,
        .type = POWER_SUPPLY_TYPE_BATTERY,
        .properties = leicaefi_power_battery_properties,
        .num_properties = ARRAY_SIZE(leicaefi_power_battery_properties),
        .get_property = leicaefi_battery_get_property,
        .set_property = leicaefi_battery_set_property,
        .property_is_writeable = leicaefi_battery_property_is_writeable,
    },
    .validity_bit = LEICAEFI_POWERSRCBIT_BAT1VAL,
};

static int leicaefi_battery_is_present(struct leicaefi_battery *battery,
				       int *val)
{
	u16 reg_value = 0;

	REMOVEME_dump_registers(battery->efidev);

	int rv = leicaefi_chip_read(battery->efidev->efichip,
				    LEICAEFI_REG_PWR_SRC_STATUS, &reg_value);
	*val = (reg_value & battery->desc->validity_bit) ? 1 : 0;

	dev_dbg(&battery->supply->dev, "%s value=%d rv=%d\n", __func__, *val,
		rv);

	return rv;
}

static int leicaefi_battery_get_capacity(struct leicaefi_battery *battery,
					 int *val)
{
	u16 reg_value = 0;

	int rv = leicaefi_chip_read(battery->efidev->efichip,
				    LEICAEFI_REG_BAT_1_RSOC, &reg_value);
	*val = reg_value;

	dev_dbg(&battery->supply->dev, "%s value=%d rv=%d\n", __func__, *val,
		rv);

	return rv;
}

static int leicaefi_battery_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct leicaefi_battery *battery = power_supply_get_drvdata(psy);

	dev_dbg(&psy->dev, "%s property=%d\n", __func__, psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		return leicaefi_battery_is_present(battery, &val->intval);
	case POWER_SUPPLY_PROP_CAPACITY:
		return leicaefi_battery_get_capacity(battery, &val->intval);
	default:
		break;
	}

	return -EINVAL;
}

static int leicaefi_battery_set_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 const union power_supply_propval *val)
{
	struct leicaefi_battery *battery = power_supply_get_drvdata(psy);

	dev_dbg(&psy->dev, "%s property=%d\n", __func__, psp);

	return -EINVAL;
}

static int
leicaefi_battery_property_is_writeable(struct power_supply *psy,
				       enum power_supply_property psp)
{
	return 0;
}

static int leicaefi_battery_register(struct leicaefi_power_device *efidev,
				     struct leicaefi_battery *battery,
				     const struct leicaefi_battery_desc *desc)
{
	struct power_supply_config config;

	memset(&config, 0, sizeof(config));

	config.drv_data = battery;

	battery->efidev = efidev;
	battery->desc = desc;

	battery->supply = devm_power_supply_register(
		&efidev->pdev->dev, &battery->desc->kernel_desc, &config);
	if (IS_ERR(battery->supply)) {
		dev_err(&efidev->pdev->dev,
			"Failed to register power supply %s\n",
			battery->desc->kernel_desc.name);
		return PTR_ERR(battery->supply);
	}
	return 0;
}

int leicaefi_power_init_bat1(struct leicaefi_power_device *efidev)
{
	return leicaefi_battery_register(efidev, &efidev->bat1_psy,
					 &leicaefi_bat1_psy_desc);
}
