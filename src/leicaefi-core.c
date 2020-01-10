#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <leicaefi-defs.h>
#include <common/leicaefi-device.h>
#include <core/leicaefi-irq.h>
#include <core/leicaefi-chip-internal.h>

struct leicaefi_device {
	struct device *dev;
	struct leicaefi_chip *efichip;
	struct leicaefi_irq_chip *irq_chip;
};

//------------------------

static const struct resource leicaefi_chr_resources[] = {
	DEFINE_RES_IRQ_NAMED(LEICAEFI_IRQNO_FLASH, "LEICAEFI_FLASH"),
	DEFINE_RES_IRQ_NAMED(LEICAEFI_IRQNO_ERR_FLASH, "LEICAEFI_FLASH_ERROR"),
};

#if 0 /* MFD child devices, to be enabled by next user stories */
static const struct resource leicaefi_keys_resources[] = {
	DEFINE_RES_IRQ_NAMED(LEICAEFI_IRQNO_KEY, "LEICAEFI_KEY"),
};
#endif

static const struct mfd_cell leicaefi_mfd_cells[] = {
	{
		.name = "leica-efi-chr",
		.of_compatible = "leica,efi-chr",
		.resources = leicaefi_chr_resources,
		.num_resources = ARRAY_SIZE(leicaefi_chr_resources),
	},
#if 0 /* MFD child devices, to be enabled by next user stories */
	{
		.name = "leica-efi-keys",
		.of_compatible = "leica,efi-keys",
		.resources = leicaefi_keys_resources,
		.num_resources = ARRAY_SIZE(leicaefi_keys_resources),
	},
    {
        .name = "leica-efi-leds",
        .of_compatible = "leica,efi-leds",
    },
#endif
};

static int leicaefi_i2c_hwcheck(struct i2c_client *i2c)
{
	u16 platform;
	u16 project;
	u16 processor;
	s32 mod_id;

	dev_dbg(&i2c->dev, "%s\n", __func__);

	mod_id = i2c_smbus_read_word_data(i2c, LEICAEFI_REG_MOD_ID);
	if (mod_id < 0) {
		return mod_id;
	}

	platform = (mod_id >> LEICAEFI_PLATFORM_SHIFT) & LEICAEFI_PLATFORM_MASK;
	if (platform == LEICAEFI_PLATFORM_SYSTEM1500) {
		dev_info(&i2c->dev, "Detected platform: System1500\n");
	} else {
		dev_info(&i2c->dev, "Unsupported platform: %d\n",
			 (int)platform);
		return -EINVAL;
	}

	project = (mod_id >> LEICAEFI_PROJECT_SHIFT) & LEICAEFI_PROJECT_MASK;
	if (project == LEICAEFI_PROJECT_SKYMASTER) {
		dev_info(&i2c->dev, "Detected project: Skymaster\n");
	} else {
		dev_info(&i2c->dev, "Unsupported project: %d\n", (int)project);
		return -EINVAL;
	}

	processor =
		(mod_id >> LEICAEFI_PROCESSOR_SHIFT) & LEICAEFI_PROCESSOR_MASK;
	if (processor == LEICAEFI_PROCESSOR_EFI) {
		dev_info(&i2c->dev, "Detected processor: EFI\n");
	} else {
		dev_info(&i2c->dev, "Unsupported processor: %d\n",
			 (int)processor);
		return -EINVAL;
	}

	return 0;
}

static void leicaefi_print_version(struct leicaefi_device *efidev,
				   const char *name, u16 value)
{
	u8 release = (value >> LEICAEFI_VERSION_RELEASE_SHIFT) &
		     LEICAEFI_VERSION_RELEASE_MASK;
	u8 version = (value >> LEICAEFI_VERSION_VERSION_SHIFT) &
		     LEICAEFI_VERSION_VERSION_MASK;
	u8 subversion = (value >> LEICAEFI_VERSION_SUBVERSION_SHIFT) &
			LEICAEFI_VERSION_SUBVERSION_MASK;

	dev_info(efidev->dev, "%s: %d.%d.%d\n", name, (int)release,
		 (int)version, (int)subversion);
}

static void leicaefi_print_hwinfo(struct leicaefi_device *efidev,
				  const char *name, u16 value)
{
	u8 variant = (value >> LEICAEFI_PCBA_VARIANT_SHIFT) &
		     LEICAEFI_PCBA_VARIANT_MASK;
	u8 version = (value >> LEICAEFI_PCBA_VERSION_SHIFT) &
		     LEICAEFI_PCBA_VERSION_MASK;

	dev_info(efidev->dev, "%s: variant %d version %d\n", name, (int)variant,
		 (int)version);
}

static int leicaefi_print_info(struct leicaefi_device *efidev)
{
	u16 value;
	int ret;

	ret = leicaefi_chip_read(efidev->efichip, LEICAEFI_REG_MOD_FWV, &value);
	if (ret != 0) {
		dev_err(efidev->dev,
			"Failed to read firmware version; error %d\n", ret);
		return ret;
	}

	leicaefi_print_version(efidev, "Firmware version", value);

	ret = leicaefi_chip_read(efidev->efichip, LEICAEFI_REG_MOD_LDRV,
				 &value);
	if (ret != 0) {
		dev_err(efidev->dev,
			"Failed to read loader version; error %d\n", ret);
		return ret;
	}

	leicaefi_print_version(efidev, "Loader version", value);

	ret = leicaefi_chip_read(efidev->efichip, LEICAEFI_REG_MOD_HW, &value);
	if (ret != 0) {
		dev_err(efidev->dev, "Failed to read hardware info; error %d\n",
			ret);
		return ret;
	}

	leicaefi_print_hwinfo(efidev, "PCB info", value);

	return 0;
}

static int leicaefi_add_mfd_devices(struct leicaefi_device *efidev)
{
	// TODO: the logic below is to pass the efichip to child devices
	//       to be investigated if it could be done via (named?) resources
	//       which seems to be more logical, but not sure if supported
	int i = 0;
	int ret = 0;
	struct irq_domain *irq_domain =
		leicaefi_irq_get_domain(efidev->irq_chip);
	int cells_count = ARRAY_SIZE(leicaefi_mfd_cells);
	struct mfd_cell *cells = NULL;
	struct leicaefi_platform_data pdata;

	memset(&pdata, 0, sizeof(pdata));
	pdata.efichip = efidev->efichip;

	/* copying as the original is const */
	cells = devm_kzalloc(efidev->dev, sizeof(leicaefi_mfd_cells),
			     GFP_KERNEL);
	if (cells == NULL) {
		return -ENOMEM;
	}

	memcpy(cells, leicaefi_mfd_cells, sizeof(leicaefi_mfd_cells));
	for (i = 0; i < cells_count; ++i) {
		/* assigning local variable as this memory will be copied */
		cells[i].platform_data = &pdata;
		cells[i].pdata_size = sizeof(pdata);
	}

	ret = devm_mfd_add_devices(efidev->dev, PLATFORM_DEVID_NONE, cells,
				   cells_count, NULL, /* mem_base */
				   0, /* irq_base */
				   irq_domain);

	devm_kfree(efidev->dev, cells);

	return ret;
}

static int leicaefi_i2c_probe(struct i2c_client *i2c)
{
	struct leicaefi_device *efidev = NULL;
	int ret = 0;

	dev_dbg(&i2c->dev, "%s\n", __func__);

	ret = leicaefi_i2c_hwcheck(i2c);
	if (ret != 0) {
		dev_err(&i2c->dev, "HW check failed: %d\n", ret);
		return ret;
	}

	if (!i2c->irq) {
		dev_err(&i2c->dev, "No IRQ configured\n");
		return -EINVAL;
	}

	efidev = devm_kzalloc(&i2c->dev, sizeof(*efidev), GFP_KERNEL);
	if (efidev == NULL) {
		dev_err(&i2c->dev, "Cannot allocate memory for the driver\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(i2c, efidev);
	efidev->dev = &i2c->dev;

	ret = devm_leicaefi_add_chip(i2c, &efidev->efichip);
	if (ret) {
		dev_err(efidev->dev, "Failed to add EFI chip: %d\n", ret);
		return ret;
	}

	ret = devm_leicaefi_add_irq_chip(efidev->dev, i2c->irq, efidev->efichip,
					 &efidev->irq_chip);
	if (ret) {
		dev_err(efidev->dev, "Failed to add IRQ chip for irq %d: %d\n",
			i2c->irq, ret);
		return ret;
	}

	ret = leicaefi_add_mfd_devices(efidev);
	if (ret) {
		dev_err(efidev->dev, "Failed to add mfd devices: %d\n", ret);
		return ret;
	}

	ret = leicaefi_print_info(efidev);
	if (ret != 0) {
		dev_err(efidev->dev, "Failed to print device info: %d\n", ret);
		return ret;
	}

	return 0;
}

static int leicaefi_i2c_remove(struct i2c_client *i2c)
{
	dev_dbg(&i2c->dev, "%s\n", __func__);

	// nothing to do now, devm used to handle resources

	return 0;
}

// Driver compatibility table (vendor,device)
static const struct of_device_id leicaefi_of_match[] = {
	{
		.compatible = "leica,efi",
	},
	{
		.compatible = "leica,skymaster-efi",
	},
	{},
};

MODULE_DEVICE_TABLE(of, leicaefi_of_match);

// I2C driver definition
static struct i2c_driver leicaefi_i2c_driver = {
	.driver =
		{
			.name = "leica-efi",
			.of_match_table = leicaefi_of_match,
		},
	.probe_new = leicaefi_i2c_probe,
	.remove = leicaefi_i2c_remove,
};

module_i2c_driver(leicaefi_i2c_driver);

// Module information
MODULE_DESCRIPTION("Leica EFI Driver");
MODULE_AUTHOR(
	"Krzysztof Kapuscik <krzysztof.kapuscik-ext@leica-geosystems.com>");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL v2");
