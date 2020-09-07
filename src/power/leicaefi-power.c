#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "leicaefi-power.h"

void REMOVEME_dump_registers(struct leicaefi_power_device *efidev)
{
	struct entry {
		const char *name;
		u8 reg_id;
	};

	struct entry ENTRIES[] = {
		{
			"PWR_SRC_STATUS ",
			LEICAEFI_REG_PWR_SRC_STATUS,
		},
		{
			"PWR_STATUS     ",
			LEICAEFI_REG_PWR_STATUS,
		},
		{
			"PWR_CTRL       ",
			LEICAEFI_REG_PWR_CTRL,
		},
		{
			"PWR_SETTINGS   ",
			LEICAEFI_REG_PWR_SETTINGS,
		},
		{
			"PWR_SRC_STATUS2",
			LEICAEFI_REG_PWR_SRC_STATUS2,
		},
		{
			"PWR_VPOE1      ",
			LEICAEFI_REG_PWR_VPOE1,
		},
		{
			"PWR_VEXT1      ",
			LEICAEFI_REG_PWR_VEXT1,
		},
		{
			"PWR_VEXT2      ",
			LEICAEFI_REG_PWR_VEXT2,
		},
		{
			"PWR_VBAT1      ",
			LEICAEFI_REG_PWR_VBAT1,
		},
		{
			"PWR_VLINE      ",
			LEICAEFI_REG_PWR_VLINE,
		},
		{
			"BAT_1_STATUS   ",
			LEICAEFI_REG_BAT_1_STATUS,
		},
		{
			"BAT_1_RSOC     ",
			LEICAEFI_REG_BAT_1_RSOC,
		},
		{
			NULL,
			0,
		},
	};

	int i;

	for (i = 0; ENTRIES[i].name; ++i) {
		u16 reg_value = 0;
		int rv = 0;

		rv = leicaefi_chip_read(efidev->efichip, ENTRIES[i].reg_id,
					&reg_value);
		dev_dbg(&efidev->pdev->dev, "%s STATUS %s: value=%X rv=%d\n",
			__func__, ENTRIES[i].name, reg_value, rv);
	}
}

static int leicaefi_power_probe(struct platform_device *pdev)
{
	struct leicaefi_power_device *efidev = NULL;
	struct leicaefi_platform_data *pdata = NULL;
	int rv = 0;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	efidev = devm_kzalloc(&pdev->dev, sizeof(*efidev), GFP_KERNEL);
	if (efidev == NULL) {
		dev_err(&pdev->dev, "Cannot allocate memory for device\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, efidev);
	efidev->pdev = pdev;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&efidev->pdev->dev, "Platform data not available.\n");
		return -ENODEV;
	}

	efidev->efichip = pdata->efichip;
	if (!efidev->efichip) {
		dev_err(&efidev->pdev->dev, "Chip not available.\n");
		return -ENODEV;
	}

	rv = leicaefi_power_init_ext1(efidev);
	if (rv != 0) {
		dev_err(&efidev->pdev->dev,
			"Cannot initialize power supply: EXT1 (error: %d).\n",
			rv);
		return rv;
	}

	rv = leicaefi_power_init_ext2(efidev);
	if (rv != 0) {
		dev_err(&efidev->pdev->dev,
			"Cannot initialize power supply: EXT2 (error: %d).\n",
			rv);
		return rv;
	}

	rv = leicaefi_power_init_poe1(efidev);
	if (rv != 0) {
		dev_err(&efidev->pdev->dev,
			"Cannot initialize power supply: POE1 (error: %d).\n",
			rv);
		return rv;
	}

	rv = leicaefi_power_init_bat1(efidev);
	if (rv != 0) {
		dev_err(&efidev->pdev->dev,
			"Cannot initialize power supply: BAT1 (error: %d).\n",
			rv);
		return rv;
	}

	return 0;
}

static int leicaefi_power_remove(struct platform_device *pdev)
{
	// struct leicaefi_power_device *efidev = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	return 0;
}

static const struct of_device_id leicaefi_power_of_id_table[] = {
	{
		.compatible = "leica,efi-power",
	},
	{},
};
MODULE_DEVICE_TABLE(of, leicaefi_power_of_id_table);

static struct platform_driver leicaefi_power_driver =
{ .driver =
{ .name = "leica-efi-power", .of_match_table = leicaefi_power_of_id_table, },
        .probe = leicaefi_power_probe, .remove = leicaefi_power_remove, };

module_platform_driver(leicaefi_power_driver);

// Module information
MODULE_DESCRIPTION("Leica EFI power supply driver");
MODULE_AUTHOR(
	"Krzysztof Kapuscik <krzysztof.kapuscik-ext@leica-geosystems.com>");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL v2");
