#include "leicaefi-power.h"

static int leicaefi_charger_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val);

static int leicaefi_charger_set_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 const union power_supply_propval *val);

static int
leicaefi_charger_property_is_writeable(struct power_supply *psy,
				       enum power_supply_property psp);

static enum power_supply_property leicaefi_charger_properties[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
};

static char *leicaefi_battery_names[] = { LEICAEFI_POWER_SUPPLY_NAME_BAT1 };

static const struct leicaefi_charger_desc leicaefi_ext1_psy_desc = {
    .kernel_desc = {
        .name = LEICAEFI_POWER_SUPPLY_NAME_EXT1,
        .type = POWER_SUPPLY_TYPE_MAINS,
        .properties = leicaefi_charger_properties,
        .num_properties = ARRAY_SIZE(leicaefi_charger_properties),
        .get_property = leicaefi_charger_get_property,
        .set_property = leicaefi_charger_set_property,
        .property_is_writeable = leicaefi_charger_property_is_writeable,
    },
    .validity_bit = LEICAEFI_POWERSRCBIT_EXT1VAL,
    .voltage_register = LEICAEFI_REG_PWR_VEXT1,
};

static const struct leicaefi_charger_desc leicaefi_ext2_psy_desc = {
    .kernel_desc = {
        .name = LEICAEFI_POWER_SUPPLY_NAME_EXT2,
        .type = POWER_SUPPLY_TYPE_MAINS,
        .properties = leicaefi_charger_properties,
        .num_properties = ARRAY_SIZE(leicaefi_charger_properties),
        .get_property = leicaefi_charger_get_property,
        .set_property = leicaefi_charger_set_property,
        .property_is_writeable = leicaefi_charger_property_is_writeable,
    },
    .validity_bit = LEICAEFI_POWERSRCBIT_EXT2VAL,
    .voltage_register = LEICAEFI_REG_PWR_VEXT2,
};

static const struct leicaefi_charger_desc leicaefi_poe1_psy_desc = {
    .kernel_desc = {
        .name = LEICAEFI_POWER_SUPPLY_NAME_POE1,
        .type = POWER_SUPPLY_TYPE_MAINS,
        .properties = leicaefi_charger_properties,
        .num_properties = ARRAY_SIZE(leicaefi_charger_properties),
        .get_property = leicaefi_charger_get_property,
        .set_property = leicaefi_charger_set_property,
        .property_is_writeable = leicaefi_charger_property_is_writeable,
    },
    .validity_bit = LEICAEFI_POWERSRCBIT_POE1VAL,
    .voltage_register = LEICAEFI_REG_PWR_VPOE1,
};

static int leicaefi_charger_is_present(struct leicaefi_charger *charger,
				       int *val)
{
	u16 reg_value = 0;

	int rv = leicaefi_chip_read(charger->efidev->efichip,
				    LEICAEFI_REG_PWR_SRC_STATUS, &reg_value);
	*val = (reg_value & charger->desc->validity_bit) ? 1 : 0;

	dev_dbg(&charger->supply->dev, "%s value=%d rv=%d\n", __func__, *val,
		rv);

	return rv;
}

static int leicaefi_charger_get_voltage(struct leicaefi_charger *charger,
					int *val)
{
	u16 reg_value = 0;
	int value = 0;

	int rv =
		leicaefi_chip_read(charger->efidev->efichip,
				   charger->desc->voltage_register, &reg_value);

	static const int ADC_RESOLUTION = 0x03FF; // 10 bit

	static const int ADC_VREF = 2500;
	static const int ADC_DIVIDER_R1 = 104700;
	static const int ADC_DIVIDER_R2 = 10000;
	static const int ADC_MAX = ADC_RESOLUTION;
	static const int ADC_VREF_COMP =
		(ADC_VREF * (ADC_DIVIDER_R1 + ADC_DIVIDER_R2) / ADC_DIVIDER_R2);

	value = (reg_value & ADC_RESOLUTION);
	value *= ADC_VREF_COMP;
	value /= ADC_MAX;
	value *= 1000;

	*val = value;

	dev_dbg(&charger->supply->dev, "%s value=%d rv=%d\n", __func__, *val,
		rv);

	return rv;
}

static int leicaefi_charger_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct leicaefi_charger *charger = power_supply_get_drvdata(psy);

	dev_dbg(&psy->dev, "%s property=%d\n", __func__, psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		return leicaefi_charger_is_present(charger, &val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return leicaefi_charger_get_voltage(charger, &val->intval);
	default:
		break;
	}

	return -EINVAL;
}

static int leicaefi_charger_set_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 const union power_supply_propval *val)
{
	struct leicaefi_charger *charger = power_supply_get_drvdata(psy);

	dev_dbg(&psy->dev, "%s property=%d\n", __func__, psp);

	return -EINVAL;
}

static int
leicaefi_charger_property_is_writeable(struct power_supply *psy,
				       enum power_supply_property psp)
{
	return 0;
}

static int leicaefi_charger_register(struct leicaefi_power_device *efidev,
				     struct leicaefi_charger *charger,
				     const struct leicaefi_charger_desc *desc)
{
	struct power_supply_config config;

	memset(&config, 0, sizeof(config));

	config.drv_data = charger;
	config.supplied_to = leicaefi_battery_names;
	config.num_supplicants = ARRAY_SIZE(leicaefi_battery_names);

	charger->efidev = efidev;
	charger->desc = desc;

	charger->supply = devm_power_supply_register(
		&efidev->pdev->dev, &charger->desc->kernel_desc, &config);
	if (IS_ERR(charger->supply)) {
		dev_err(&efidev->pdev->dev,
			"Failed to register power supply %s\n",
			charger->desc->kernel_desc.name);
		return PTR_ERR(charger->supply);
	}
	return 0;
}

int leicaefi_power_init_ext1(struct leicaefi_power_device *efidev)
{
	return leicaefi_charger_register(efidev, &efidev->ext1_psy,
					 &leicaefi_ext1_psy_desc);
}

int leicaefi_power_init_ext2(struct leicaefi_power_device *efidev)
{
	return leicaefi_charger_register(efidev, &efidev->ext2_psy,
					 &leicaefi_ext2_psy_desc);
}

int leicaefi_power_init_poe1(struct leicaefi_power_device *efidev)
{
	return leicaefi_charger_register(efidev, &efidev->poe1_psy,
					 &leicaefi_poe1_psy_desc);
}
