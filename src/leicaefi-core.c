#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <leicaefi-defs.h>
#include <leicaefi-utils.h>

#define LEICAEFI_IRQNO_ERR (0)
#define LEICAEFI_IRQNO_FLASH (1)
#define LEICAEFI_IRQNO_KEY (2)
#define LEICAEFI_IRQNO_GCC (3)
#define LEICAEFI_IRQNO_CBL (4)
#define LEICAEFI_IRQNO_SRC (5)
#define LEICAEFI_IRQNO_PWR (6)
#define LEICAEFI_IRQNO_DEV (7)
#define LEICAEFI_IRQNO_SMB (8)
#define LEICAEFI_IRQNO_OW (9)
#define LEICAEFI_IRQNO_COM (10)
#define LEICAEFI_IRQNO_LED (11)
#define LEICAEFI_TOTAL_IRQ_COUNT (12)

struct leicaefi_device {
	struct i2c_client *i2c;

	struct regmap *regmap;

	struct regmap_irq_chip_data *regmap_irq;
};

#ifdef READ_WRITE_DEBUG_ENABLED

static int leicaefi_regmap_reg_read(void *context, unsigned int reg,
				    unsigned int *val)
{
	struct leicaefi_device *efidev = context;
	s32 res = i2c_smbus_read_word_data(efidev->i2c, (u8)reg);
	if (res >= 0) {
		*val = (unsigned int)res;

		dev_dbg(&efidev->i2c->dev,
			"%s - reg=%c/%c/0x%X - success val=0x%X\n", __func__,
			(reg & LEICAEFI_RWBIT_READ) ? 'r' : 'w',
			(reg & LEICAEFI_SCBIT_SET) ? 's' : 'c',
			(reg & LEICAEFI_REGNO_MASK), *val);

		return 0;
	} else {
		dev_err(&efidev->i2c->dev,
			"%s - reg=%c/%c/0x%X - failure res=%d\n", __func__,
			(reg & LEICAEFI_RWBIT_READ) ? 'r' : 'w',
			(reg & LEICAEFI_SCBIT_SET) ? 's' : 'c',
			(reg & LEICAEFI_REGNO_MASK), res);

		return res;
	}
}

static int leicaefi_regmap_reg_write(void *context, unsigned int reg,
				     unsigned int val)
{
	struct leicaefi_device *efidev = context;
	s32 res = i2c_smbus_write_word_data(efidev->i2c, (u8)reg, (u16)val);
	if (res >= 0) {
		dev_dbg(&efidev->i2c->dev,
			"%s - reg=%c/%c/0x%X val=0x%X - success\n", __func__,
			(reg & LEICAEFI_RWBIT_READ) ? 'r' : 'w',
			(reg & LEICAEFI_SCBIT_SET) ? 's' : 'c',
			(reg & LEICAEFI_REGNO_MASK), val);

		return 0;
	} else {
		dev_err(&efidev->i2c->dev,
			"%s - reg=%c/%c/0x%X val=0x%X - failure res=%d\n",
			__func__, (reg & LEICAEFI_RWBIT_READ) ? 'r' : 'w',
			(reg & LEICAEFI_SCBIT_SET) ? 's' : 'c',
			(reg & LEICAEFI_REGNO_MASK), val, res);
	}
	return res;
}

#endif /*READ_WRITE_DEBUG_ENABLED*/

#if 0 /* MFD child devices, to be enabled by next user stories */
static struct resource leicaefi_keys_resources[] = {
	DEFINE_RES_IRQ_NAMED(LEICAEFI_IRQNO_KEY, "LEICAEFI_KEY"),
};
#endif

static struct mfd_cell leicefi_mfd_cells[] = {
	{
		.name = "leica-efi-chr",
		.of_compatible = "leica,efi-chr",
	},
#if 0 /* MFD child devices, to be enabled by next user stories */
	{
		.name = "leica-efi-leds",
		.of_compatible = "leica,efi-leds",
	},
	{
		.name = "leica-efi-keys",
		.of_compatible = "leica,efi-keys",
		.resources = leicaefi_keys_resources,
		.num_resources = ARRAY_SIZE(leicaefi_keys_resources),
	},
#endif
};

static bool leicaefi_is_volatile_reg(struct device *dev, unsigned int reg)
{
	dev_dbg(dev, "%s 0x%X\n", __func__, reg);

	// all registers are volatile for simplicity (disable cache)
	return true;
}

static bool leicaefi_is_precious_reg(struct device *dev, unsigned int reg)
{
	u8 reg_no = (reg & LEICAEFI_REGNO_MASK);

	dev_dbg(dev, "%s 0x%X\n", __func__, reg);

	switch (reg_no) {
	case LEICAEFI_REG_MOD_IFG:
	case LEICAEFI_REG_MOD_ERR:
	case LEICAEFI_REG_KEY_DATA:
		/* registers are cleared on read */
		return true;
	default:
		return false;
	}
}

static struct regmap_config leicaefi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
#ifdef READ_WRITE_DEBUG_ENABLED
	.reg_read = leicaefi_regmap_reg_read,
	.reg_write = leicaefi_regmap_reg_write,
#endif /*READ_WRITE_DEBUG_ENABLED*/
	.volatile_reg = leicaefi_is_volatile_reg,
	.precious_reg = leicaefi_is_precious_reg,
};

static struct regmap_irq leicaefi_irq_table[LEICAEFI_TOTAL_IRQ_COUNT] = {
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_ERR, 0, LEICAEFI_IRQBIT_ERR),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_FLASH, 0, LEICAEFI_IRQBIT_FLASH),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_KEY, 0, LEICAEFI_IRQBIT_KEY),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_GCC, 0, LEICAEFI_IRQBIT_GCC),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_CBL, 0, LEICAEFI_IRQBIT_CBL),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_SRC, 0, LEICAEFI_IRQBIT_SRC),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_PWR, 0, LEICAEFI_IRQBIT_PWR),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_DEV, 0, LEICAEFI_IRQBIT_DEV),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_SMB, 0, LEICAEFI_IRQBIT_SMB),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_OW, 0, LEICAEFI_IRQBIT_OW),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_COM, 0, LEICAEFI_IRQBIT_COM),
	REGMAP_IRQ_REG(LEICAEFI_IRQNO_LED, 0, LEICAEFI_IRQBIT_LED),
};

static struct regmap_irq_chip leicaefi_irq_chip = {
	.name = "leicaefi-irq",
	.status_base = (LEICAEFI_REG_MOD_IFG | LEICAEFI_SCBIT_UNUSED),
	.mask_base = (LEICAEFI_REG_MOD_IE | LEICAEFI_SCBIT_SET),
	.unmask_base = (LEICAEFI_REG_MOD_IE | LEICAEFI_SCBIT_CLEAR),
	.num_regs = 1,
	.irqs = leicaefi_irq_table,
	.num_irqs = LEICAEFI_TOTAL_IRQ_COUNT,
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

	dev_info(&efidev->i2c->dev, "%s: %d.%d.%d\n", name, (int)release,
		 (int)version, (int)subversion);
}

static void leicaefi_print_hwinfo(struct leicaefi_device *efidev,
				  const char *name, u16 value)
{
	u8 variant = (value >> LEICAEFI_PCBA_VARIANT_SHIFT) &
		     LEICAEFI_PCBA_VARIANT_MASK;
	u8 version = (value >> LEICAEFI_PCBA_VERSION_SHIFT) &
		     LEICAEFI_PCBA_VERSION_MASK;

	dev_info(&efidev->i2c->dev, "%s: variant %d version %d\n", name,
		 (int)variant, (int)version);
}

static int leicaefi_print_info(struct leicaefi_device *efidev)
{
	u16 value;
	int ret;

	ret = leicaefi_read(efidev->regmap, LEICAEFI_REG_MOD_FWV, &value);
	if (ret != 0) {
		dev_err(&efidev->i2c->dev,
			"Failed to read firmware version; error %d\n", ret);
		return ret;
	}

	leicaefi_print_version(efidev, "Firmware version", value);

	ret = leicaefi_read(efidev->regmap, LEICAEFI_REG_MOD_LDRV, &value);
	if (ret != 0) {
		dev_err(&efidev->i2c->dev,
			"Failed to read loader version; error %d\n", ret);
		return ret;
	}

	leicaefi_print_version(efidev, "Loader version", value);

	ret = leicaefi_read(efidev->regmap, LEICAEFI_REG_MOD_HW, &value);
	if (ret != 0) {
		dev_err(&efidev->i2c->dev,
			"Failed to read hardware info; error %d\n", ret);
		return ret;
	}

	leicaefi_print_hwinfo(efidev, "PCB info", value);

	return 0;
}

static int leicaefi_i2c_probe(struct i2c_client *i2c)
{
	struct leicaefi_device *efidev = NULL;
	int ret = 0;

	dev_dbg(&i2c->dev, "%s\n", __func__);

	efidev = devm_kzalloc(&i2c->dev, sizeof(*efidev), GFP_KERNEL);
	if (efidev == NULL) {
		dev_err(&efidev->i2c->dev,
			"Cannot allocate memory for the driver\n");
		return -ENOMEM;
	}

	ret = leicaefi_i2c_hwcheck(i2c);
	if (ret != 0) {
		dev_err(&efidev->i2c->dev, "HW check failed: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, efidev);
	efidev->i2c = i2c;

#ifdef READ_WRITE_DEBUG_ENABLED
	efidev->regmap = devm_regmap_init(&efidev->i2c->dev, NULL, efidev,
					  &leicaefi_regmap_config);
#else /*READ_WRITE_DEBUG_ENABLED*/
	efidev->regmap =
		devm_regmap_init_i2c(efidev->i2c, &leicaefi_regmap_config);
#endif /*READ_WRITE_DEBUG_ENABLED*/
	if (IS_ERR(efidev->regmap)) {
		ret = PTR_ERR(efidev->regmap);
		dev_err(&efidev->i2c->dev,
			"Failed to allocate register map: %d\n", ret);
		return ret;
	}
	if (!i2c->irq) {
		dev_err(&efidev->i2c->dev, "No IRQ configured\n");
		return -EINVAL;
	}

	ret = devm_regmap_add_irq_chip(&efidev->i2c->dev, efidev->regmap,
				       efidev->i2c->irq,
				       IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				       0 /* irq_base */, &leicaefi_irq_chip,
				       &efidev->regmap_irq);
	if (ret != 0) {
		dev_err(&efidev->i2c->dev,
			"Failed to request IRQ chip for irq %d: %d\n",
			efidev->i2c->irq, ret);
		return ret;
	}
	/* read register to init interrupts */
	{
		u16 val;
		leicaefi_read(efidev->regmap, LEICAEFI_REG_MOD_IFG, &val);
	}

	ret = devm_mfd_add_devices(&efidev->i2c->dev, PLATFORM_DEVID_NONE,
				   leicefi_mfd_cells,
				   ARRAY_SIZE(leicefi_mfd_cells),
				   NULL, /* mem_base */
				   0, /* irq_base */
				   regmap_irq_get_domain(efidev->regmap_irq));
	if (ret) {
		dev_err(&efidev->i2c->dev, "Failed to add mfd devices: %d\n",
			ret);
		return ret;
	}

	ret = leicaefi_print_info(efidev);
	if (ret != 0) {
		dev_err(&efidev->i2c->dev, "Failed to print device info: %d\n",
			ret);
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
