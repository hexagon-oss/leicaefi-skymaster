#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/leds.h>

#include <leicaefi.h>
#include <common/leicaefi-chip.h>
#include <common/leicaefi-device.h>

#define LEICAEFI_LED_VALUE_BIT_MASK 0x3 // two bits
#define LEICAEFI_LED_VALUE_OFF 0
#define LEICAEFI_LED_VALUE_FULLY_ON 1
#define LEICAEFI_LED_VALUE_DIMMED 2
#define LEICAEFI_LED_VALUE_DIMMED_BLINKING 3

struct leicaefi_leds_device;

struct leicaefi_led_desc {
	const char *name;
	u8 efi_reg_no;
	u16 efi_reg_offset;
};

struct leicaefi_led {
	struct leicaefi_leds_device *efidev;
	struct leicaefi_led_desc *desc;
	struct led_classdev lc;
	int id;
};

struct leicaefi_leds_device {
	struct platform_device *pdev;
	struct leicaefi_chip *efichip;

	struct leicaefi_led *leds;
};

static struct leicaefi_led_desc EFI_LED_DESCRIPTORS[] = {
	{ "efihmi:red:sd_write", LEICAEFI_REG_LED_CTRL1, 0 },
	{ "efihmi:green:sd_write", LEICAEFI_REG_LED_CTRL1, 2 },
	{ "efihmi:red:sd", LEICAEFI_REG_LED_CTRL1, 4 },
	{ "efihmi:green:sd", LEICAEFI_REG_LED_CTRL1, 6 },
	// Following EFI specification user application shall not control the batter LED
	// so it is not registered in the system.
	//   { "efihmi:red:battery", LEICAEFI_REG_LED_CTRL1, 8  },
	//   { "efihmi:green:battery", LEICAEFI_REG_LED_CTRL1, 10  },
	{ "efihmi:red:power", LEICAEFI_REG_LED_CTRL1, 12 },
	{ "efihmi:green:power", LEICAEFI_REG_LED_CTRL1, 14 },

	{ "efihmi:red:rtk_out", LEICAEFI_REG_LED_CTRL2, 0 },
	{ "efihmi:green:rtk_out", LEICAEFI_REG_LED_CTRL2, 2 },
	{ "efihmi:red:rtk_in", LEICAEFI_REG_LED_CTRL2, 4 },
	{ "efihmi:green:rtk_in", LEICAEFI_REG_LED_CTRL2, 6 },
	{ "efihmi:green:position", LEICAEFI_REG_LED_CTRL2, 8 },
	{ "efihmi:red:position", LEICAEFI_REG_LED_CTRL2, 10 },
	{ "efihmi:green:wireless", LEICAEFI_REG_LED_CTRL2, 12 },
	{ "efihmi:blue:wireless", LEICAEFI_REG_LED_CTRL2, 14 },
};

static struct leicaefi_led *leicaefi_led_cast(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct leicaefi_led, lc);
}

// TODO: future enhancement -> make the changes atomic (add synchronization)

static enum led_brightness
leicaefi_led_brightness_get(struct led_classdev *led_cdev)
{
	struct leicaefi_led *led = leicaefi_led_cast(led_cdev);
	int rv = 0;
	u16 int_value = 0;

	dev_dbg(&led->efidev->pdev->dev, "%s id=%d\n", __func__, led->id);

	rv = leicaefi_chip_read(led->efidev->efichip, led->desc->efi_reg_no,
				&int_value);
	if (rv != 0) {
		dev_warn(&led->efidev->pdev->dev,
			 "%s - getting brightness failed id=%d rv=%d\n",
			 __func__, led->id, rv);
		return rv;
	}

	int_value >>= led->desc->efi_reg_offset;
	int_value &= LEICAEFI_LED_VALUE_BIT_MASK;

	switch (int_value) {
	case LEICAEFI_LED_VALUE_FULLY_ON:
		return LED_FULL;
	case LEICAEFI_LED_VALUE_DIMMED_BLINKING:
	case LEICAEFI_LED_VALUE_DIMMED:
		return LED_HALF;
	case LEICAEFI_LED_VALUE_OFF:
	default:
		return LED_OFF;
	}
}

static int leicaefi_led_brightness_set(struct led_classdev *led_cdev,
				       enum led_brightness value)
{
	struct leicaefi_led *led = leicaefi_led_cast(led_cdev);
	u16 clear_value = 0;
	u16 enable_value = (u16)value;
	u16 current_value = 0;
	int rv = 0;
	int kernel_int_value = (int)value;

	dev_dbg(&led->efidev->pdev->dev, "%s id=%d value=%d\n", __func__,
		led->id, kernel_int_value);

	rv = leicaefi_chip_read(led->efidev->efichip, led->desc->efi_reg_no,
				&current_value);
	if (rv != 0) {
		dev_warn(&led->efidev->pdev->dev,
			 "%s - getting brightness failed id=%d rv=%d\n",
			 __func__, led->id, rv);
		return rv;
	}

	current_value >>= led->desc->efi_reg_offset;
	current_value &= LEICAEFI_LED_VALUE_BIT_MASK;

	if (current_value == LEICAEFI_LED_VALUE_DIMMED_BLINKING) {
		// if currently blinking only allow switching off
		if (kernel_int_value >= LED_ON) {
			enable_value = LEICAEFI_LED_VALUE_DIMMED_BLINKING;
		} else {
			enable_value = LEICAEFI_LED_VALUE_OFF;
		}
	} else {
		if (kernel_int_value > LED_HALF) {
			enable_value = LEICAEFI_LED_VALUE_FULLY_ON;
		} else if (kernel_int_value >= LED_ON) {
			enable_value = LEICAEFI_LED_VALUE_DIMMED;
		} else {
			enable_value = LEICAEFI_LED_VALUE_OFF;
		}
	}

	clear_value = LEICAEFI_LED_VALUE_BIT_MASK;
	enable_value &= LEICAEFI_LED_VALUE_BIT_MASK;

	clear_value <<= led->desc->efi_reg_offset;
	enable_value <<= led->desc->efi_reg_offset;

	rv = leicaefi_chip_clear_bits(led->efidev->efichip,
				      led->desc->efi_reg_no, clear_value);
	if (rv != 0) {
		dev_warn(&led->efidev->pdev->dev,
			 "%s - clearing brightness bits failed id=%d rv=%d\n",
			 __func__, led->id, rv);
		return rv;
	}

	rv = leicaefi_chip_set_bits(led->efidev->efichip, led->desc->efi_reg_no,
				    enable_value);
	if (rv != 0) {
		dev_warn(&led->efidev->pdev->dev,
			 "%s - setting brightness bits failed id=%d rv=%d\n",
			 __func__, led->id, rv);
		return rv;
	}

	return rv;
}

static int leicaefi_led_blink_set(struct led_classdev *led_cdev,
				  unsigned long *delay_on,
				  unsigned long *delay_off)
{
	struct leicaefi_led *led = leicaefi_led_cast(led_cdev);

	dev_dbg(&led->efidev->pdev->dev,
		"%s id=%d delay_on=%lu delay_off=%lu\n", __func__, led->id,
		*delay_on, *delay_off);

	if ((*delay_on != 0) || (*delay_off != 0)) {
		// non-standard settings requested

		int rv = 0;
		u16 current_value = 0;

		rv = leicaefi_chip_read(led->efidev->efichip,
					led->desc->efi_reg_no, &current_value);
		if (rv != 0) {
			dev_warn(&led->efidev->pdev->dev,
				 "%s - getting brightness failed id=%d rv=%d\n",
				 __func__, led->id, rv);
			return rv;
		}

		current_value >>= led->desc->efi_reg_offset;
		current_value &= LEICAEFI_LED_VALUE_BIT_MASK;

		if (current_value == LEICAEFI_LED_VALUE_DIMMED_BLINKING) {
			u16 clear_value = 0;

			// if blinking - turn off the blinking as kernel will control it
			clear_value = LEICAEFI_LED_VALUE_BIT_MASK;
			clear_value <<= led->desc->efi_reg_offset;

			rv = leicaefi_chip_clear_bits(led->efidev->efichip,
						      led->desc->efi_reg_no,
						      clear_value);
			if (rv != 0) {
				dev_warn(
					&led->efidev->pdev->dev,
					"%s - clearing brightness bits failed id=%d rv=%d\n",
					__func__, led->id, rv);
				return rv;
			}
		}

		// return error, so kernel will do the rest
		return EINVAL;
	} else {
		int rv = 0;
		u16 enable_value = 0;

		// 2Hz, following current EFI firmware code settings
		// 250ms for on, 250ms for off state
		*delay_on = 250;
		*delay_off = 250;

		enable_value = LEICAEFI_LED_VALUE_DIMMED_BLINKING;
		enable_value <<= led->desc->efi_reg_offset;

		rv = leicaefi_chip_set_bits(led->efidev->efichip,
					    led->desc->efi_reg_no,
					    enable_value);
		if (rv != 0) {
			dev_warn(
				&led->efidev->pdev->dev,
				"%s - setting brightness bits failed id=%d rv=%d\n",
				__func__, led->id, rv);
			return rv;
		}

		return 0;
	}
}

static int leicaefi_leds_probe(struct platform_device *pdev)
{
	struct leicaefi_leds_device *efidev = NULL;
	struct leicaefi_platform_data *pdata = NULL;
	size_t i = 0;
	size_t count =
		sizeof(EFI_LED_DESCRIPTORS) / sizeof(EFI_LED_DESCRIPTORS[0]);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	efidev = devm_kzalloc(&pdev->dev, sizeof(*efidev), GFP_KERNEL);
	if (efidev == NULL) {
		dev_err(&pdev->dev, "Cannot allocate memory for device\n");
		return -ENOMEM;
	}

	efidev->leds = devm_kzalloc(
		&pdev->dev, count * sizeof(struct leicaefi_led), GFP_KERNEL);
	if (efidev->leds == NULL) {
		dev_err(&pdev->dev,
			"Cannot allocate memory for led instances\n");
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

	/* init leds */
	for (i = 0; i < count; i++) {
		int ret = 0;

		memset(&efidev->leds[i], 0, sizeof(efidev->leds[i]));
		efidev->leds[i].desc = &EFI_LED_DESCRIPTORS[i];
		efidev->leds[i].efidev = efidev;
		efidev->leds[i].id = (int)i;
		efidev->leds[i].lc.name = EFI_LED_DESCRIPTORS[i].name;
		efidev->leds[i].lc.max_brightness = LED_FULL;
		efidev->leds[i].lc.brightness_get = leicaefi_led_brightness_get;
		efidev->leds[i].lc.brightness_set_blocking =
			leicaefi_led_brightness_set;
		efidev->leds[i].lc.blink_set = leicaefi_led_blink_set;

		ret = devm_led_classdev_register(&efidev->pdev->dev,
						 &efidev->leds[i].lc);
		if (ret) {
			dev_err(&efidev->pdev->dev,
				"Failed to register led %d\n", (int)i);
			return ret;
		}
	}

	return 0;
}

static int leicaefi_leds_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s\n", __func__);

	// kernel automatically sets brightness of all leds to zero

	return 0;
}

static const struct of_device_id leicaefi_leds_of_id_table[] = {
	{
		.compatible = "leica,efi-leds",
	},
	{},
};
MODULE_DEVICE_TABLE(of, leicaefi_leds_of_id_table);

static struct platform_driver leicaefi_leds_driver = {
    .driver =
        {
            .name = "leica-efi-leds",
            .of_match_table = leicaefi_leds_of_id_table,
        },
    .probe = leicaefi_leds_probe,
    .remove = leicaefi_leds_remove,
};

module_platform_driver(leicaefi_leds_driver);

// Module information
MODULE_DESCRIPTION("Leica EFI leds driver");
MODULE_AUTHOR(
	"Krzysztof Kapuscik <krzysztof.kapuscik-ext@leica-geosystems.com>");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL v2");
