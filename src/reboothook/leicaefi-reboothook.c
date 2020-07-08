#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/platform_device.h>

#include <asm/system_misc.h> // arm_pm_restart symbol

#include <leicaefi.h>
#include <common/leicaefi-device.h>
#include <common/leicaefi-chip.h>

struct leicaefi_reboothook_device {
	struct platform_device *pdev;
	struct leicaefi_chip *efichip;

	void (*prev_restart_hook)(enum reboot_mode reboot_mode,
				  const char *cmd);
	void (*prev_poweroff_hook)(void);
};

struct leicaefi_reboothook_device *leicaefi_reboothook_context_dev = NULL;

static void leicaefi_reboothook_restart_hook(enum reboot_mode reboot_mode,
					     const char *cmd)
{
	leicaefi_chip_set_bits(leicaefi_reboothook_context_dev->efichip,
			       LEICAEFI_REG_PWR_CTRL, 1 << 4);
}

static void leicaefi_reboothook_poweroff_hook(void)
{
	leicaefi_chip_set_bits(leicaefi_reboothook_context_dev->efichip,
			       LEICAEFI_REG_PWR_CTRL, 1 << 0);
}

static int leicaefi_reboothook_probe(struct platform_device *pdev)
{
	struct leicaefi_reboothook_device *efidev = NULL;
	struct leicaefi_platform_data *pdata = NULL;

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

	// store the device as hooks does not have any explicit 'context' argument
	leicaefi_reboothook_context_dev = efidev;

	dev_info(&efidev->pdev->dev,
		 "Hook before installation - restart:  %p\n", arm_pm_restart);
	dev_info(&efidev->pdev->dev,
		 "Hook before installation - poweroff: %p\n", pm_power_off);

	efidev->prev_restart_hook = arm_pm_restart;
	efidev->prev_poweroff_hook = pm_power_off;

	arm_pm_restart = leicaefi_reboothook_restart_hook;
	pm_power_off = leicaefi_reboothook_poweroff_hook;

	dev_info(&efidev->pdev->dev, "Hook after installation - restart:  %p\n",
		 arm_pm_restart);
	dev_info(&efidev->pdev->dev, "Hook after installation - poweroff: %p\n",
		 pm_power_off);

	return 0;
}

static int leicaefi_reboothook_remove(struct platform_device *pdev)
{
	struct leicaefi_reboothook_device *efidev = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	arm_pm_restart = efidev->prev_restart_hook;
	pm_power_off = efidev->prev_poweroff_hook;

	// resources allocated using devm are freed automatically

	return 0;
}

static const struct of_device_id leicaefi_reboothook_of_match[] = {
	{
		.compatible = "leica,efi-reboothook",
	},
	{},
};
MODULE_DEVICE_TABLE(of, leicaefi_reboothook_of_match);

static struct platform_driver leicaefi_reboothook_driver = {
    .driver =
        {
            .name = "leica-efi-reboothook",
            .of_match_table = leicaefi_reboothook_of_match,
        },
    .probe = leicaefi_reboothook_probe,
    .remove = leicaefi_reboothook_remove,
};

module_platform_driver(leicaefi_reboothook_driver);

// Module information
MODULE_DESCRIPTION("Leica EFI reboot hook driver");
MODULE_AUTHOR(
	"Krzysztof Kapuscik <krzysztof.kapuscik-ext@leica-geosystems.com>");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL v2");
