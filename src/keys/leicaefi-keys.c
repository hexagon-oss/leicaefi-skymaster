#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/input.h>
#include <linux/interrupt.h>

#include <leicaefi.h>
#include <common/leicaefi-chip.h>
#include <common/leicaefi-device.h>

struct leicaefi_keys_device {
	struct platform_device *pdev;
	struct leicaefi_chip *efichip;

	struct input_dev *input_dev;
	int irq;
};

static void leicaefi_process_key_event(struct leicaefi_keys_device *efidev,
				       u8 event_value)
{
	int up_down_value = (event_value & 0x80) ? 0 : 1;
	u8 key_code = event_value & 0x7F;

	/* no event - skip */
	if (event_value == 0) {
		return;
	}

	if (key_code == 1) {
		dev_info(&efidev->pdev->dev, "Power key %s\n",
			 up_down_value ? "pressed" : "released");

		input_report_key(efidev->input_dev, KEY_POWER, up_down_value);
		input_sync(efidev->input_dev);
	} else if (key_code == 2) {
		dev_info(&efidev->pdev->dev, "Function key %s\n",
			 up_down_value ? "pressed" : "released");

		input_report_key(efidev->input_dev, KEY_F1, up_down_value);
		input_sync(efidev->input_dev);
	} else {
		dev_warn(&efidev->pdev->dev, "Invalid key code: %d\n",
			 (int)key_code);
	}
}

static irqreturn_t leicaefi_keys_irq_handler(int irq, void *dev_efi)
{
	struct leicaefi_keys_device *efidev =
		(struct leicaefi_keys_device *)dev_efi;
	u16 key_data_value = 0;
	int up_down_value = 0;
	int rv = 0;

	dev_dbg(&efidev->pdev->dev, "interrupt detected\n");

	rv = leicaefi_chip_read(efidev->efichip, LEICAEFI_REG_KEY_DATA,
				&key_data_value);
	if (rv != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - reading key data failed rv=%d\n", __func__, rv);
		return IRQ_HANDLED;
	}

	dev_dbg(&efidev->pdev->dev, "Key data read => 0x%X\n",
		(int)key_data_value);

	leicaefi_process_key_event(efidev, (u8)((key_data_value >> 8) & 0xFF));
	leicaefi_process_key_event(efidev, (u8)((key_data_value >> 0) & 0xFF));

	return IRQ_HANDLED;
}

static int leicaefi_keys_probe(struct platform_device *pdev)
{
	struct leicaefi_keys_device *efidev = NULL;
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

	/* init input device */
	efidev->input_dev = devm_input_allocate_device(&efidev->pdev->dev);
	if (!efidev->input_dev) {
		dev_err(&efidev->pdev->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	efidev->input_dev->name = "efi-onkey";
	efidev->input_dev->phys = "efi-onkey/input0";
	efidev->input_dev->dev.parent = &efidev->pdev->dev;
	input_set_capability(efidev->input_dev, EV_KEY, KEY_POWER);
	input_set_capability(efidev->input_dev, EV_KEY, KEY_F1);

	rv = input_register_device(efidev->input_dev);
	if (rv != 0) {
		dev_err(&efidev->pdev->dev,
			"Unable to register input device, %d\n", rv);
		return rv;
	}

	efidev->irq = platform_get_irq_byname(pdev, "LEICAEFI_KEY");
	if (efidev->irq <= 0) {
		dev_err(&efidev->pdev->dev,
			"failed: cannot find irq (error :%d)\n", efidev->irq);
		return -EINVAL;
	}

	rv = devm_request_threaded_irq(&efidev->pdev->dev, efidev->irq, NULL,
				       leicaefi_keys_irq_handler, IRQF_ONESHOT,
				       NULL, efidev);
	if (rv < 0) {
		dev_err(&efidev->pdev->dev,
			"failed: irq request (IRQ: %d, error :%d)\n",
			efidev->irq, rv);
		return rv;
	}

	return 0;
}

static int leicaefi_keys_remove(struct platform_device *pdev)
{
	struct leicaefi_keys_device *efidev = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	return 0;
}

static const struct of_device_id leicaefi_keys_of_id_table[] = {
	{
		.compatible = "leica,efi-keys",
	},
	{},
};
MODULE_DEVICE_TABLE(of, leicaefi_keys_of_id_table);

static struct platform_driver leicaefi_keys_driver = {
    .driver =
        {
            .name = "leica-efi-keys",
            .of_match_table = leicaefi_keys_of_id_table,
        },
    .probe = leicaefi_keys_probe,
    .remove = leicaefi_keys_remove,
};

module_platform_driver(leicaefi_keys_driver);

// Module information
MODULE_DESCRIPTION("Leica EFI keys driver");
MODULE_AUTHOR(
	"Krzysztof Kapuscik <krzysztof.kapuscik-ext@leica-geosystems.com>");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL v2");
