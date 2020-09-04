#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

#include <chr/leicaefi-chr.h>
#include <leicaefi.h>
#include <common/leicaefi-chip.h>

struct class *leicaefi_chr_class = NULL;

static int leicaefi_chr_open(struct inode *inode, struct file *filep)
{
	struct leicaefi_chr_device *efidev = container_of(
		inode->i_cdev, struct leicaefi_chr_device, chr_cdev);

	dev_dbg(&efidev->pdev->dev, "%s\n", __func__);

	// store pointer to the device in file for later use
	filep->private_data = efidev;

	return 0;
}

static int leicaefi_chr_release(struct inode *inode, struct file *filep)
{
	struct leicaefi_chr_device *efidev = filep->private_data;
	dev_dbg(&efidev->pdev->dev, "%s\n", __func__);
	return 0;
}

static ssize_t leicaefi_chr_read(struct file *filep, char __user *buffer,
				 size_t length, loff_t *offset)
{
	struct leicaefi_chr_device *efidev = filep->private_data;
	dev_dbg(&efidev->pdev->dev, "%s\n", __func__);

	// TODO: planned feature - interrupts support
	// TODO: the file could be used to pass interrupts to be handled to the listeners (as in UIO).
	// TODO: for example by using blocking i/o as in https://www.oreilly.com/library/view/linux-device-drivers/0596005903/ch06.html

	return 0;
}

static ssize_t leicaefi_chr_write(struct file *filep, const char __user *buffer,
				  size_t length, loff_t *offset)
{
	struct leicaefi_chr_device *efidev = filep->private_data;
	dev_dbg(&efidev->pdev->dev, "%s\n", __func__);
	return -EPERM;
}

static long leicaefi_chr_unlocked_ioctl(struct file *filep, unsigned int cmd,
					unsigned long arg)
{
	struct leicaefi_chr_device *efidev = filep->private_data;
	long result = 0;
	bool handled = false;

	result = leicaefi_chr_reg_handle_ioctl(efidev, cmd, arg, &handled);
	if (handled) {
		return result;
	}

	result = leicaefi_chr_flash_handle_ioctl(efidev, cmd, arg, &handled);
	if (handled) {
		return result;
	}

	result = leicaefi_chr_power_handle_ioctl(efidev, cmd, arg, &handled);
	if (handled) {
		return result;
	}

	dev_warn(&efidev->pdev->dev, "%s - IOCTL call %u not handled", __func__,
		 cmd);

	return -EINVAL;
}

static int leicaefi_chr_create_device(struct leicaefi_chr_device *efidev)
{
	int rc = 0;
	int major = 0;
	struct device *new_dev = NULL;

	dev_dbg(&efidev->pdev->dev, "%s\n", __func__);

	efidev->chr_file_ops.owner = THIS_MODULE;
	efidev->chr_file_ops.open = leicaefi_chr_open;
	efidev->chr_file_ops.release = leicaefi_chr_release;
	efidev->chr_file_ops.read = leicaefi_chr_read;
	efidev->chr_file_ops.write = leicaefi_chr_write;
	efidev->chr_file_ops.unlocked_ioctl = leicaefi_chr_unlocked_ioctl;

	// this does not work currently for multiple devices (driver instances)
	// as they are assigned the same minor number leading to an issue in device_create
	//
	// to be done when we will need more than 1 instance of device
	rc = alloc_chrdev_region(&efidev->chr_dev, 0, 1, "leicaefi");
	if (rc) {
		dev_err(&efidev->pdev->dev, "Major number allocation failed\n");
		return rc;
	}
	efidev->chr_region_allocated = true;

	major = MAJOR(efidev->chr_dev);
	printk(KERN_INFO "The major number is %d", major);

	cdev_init(&efidev->chr_cdev, &efidev->chr_file_ops);

	rc = cdev_add(&efidev->chr_cdev, efidev->chr_dev, 1);
	if (rc < 0) {
		dev_err(&efidev->pdev->dev, "Failed to add cdev\n");
		return rc;
	}
	efidev->chr_cdev_added = true;

	new_dev = device_create(leicaefi_chr_class, &efidev->pdev->dev,
				efidev->chr_dev, NULL, "leicaefi%d",
				MINOR(efidev->chr_dev));
	if (IS_ERR(new_dev)) {
		dev_err(&efidev->pdev->dev, "Failed to create device\n");
		return PTR_ERR(new_dev);
	}
	efidev->chr_device_created = true;

	return 0;
}

static void leicaefi_chr_remove_device(struct leicaefi_chr_device *efidev)
{
	dev_dbg(&efidev->pdev->dev, "%s\n", __func__);

	if (efidev->chr_device_created) {
		device_destroy(leicaefi_chr_class, efidev->chr_dev);
		efidev->chr_device_created = false;
	}

	if (efidev->chr_cdev_added) {
		cdev_del(&efidev->chr_cdev);
		efidev->chr_cdev_added = false;
	}

	if (efidev->chr_region_allocated) {
		unregister_chrdev_region(efidev->chr_dev, 1);
		efidev->chr_region_allocated = false;
	}
}

static int leicaefi_chr_probe(struct platform_device *pdev)
{
	struct leicaefi_chr_device *efidev = NULL;
	struct leicaefi_platform_data *pdata = NULL;
	int rc = 0;

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

	rc = leicaefi_chr_flash_init(efidev);
	if (rc != 0) {
		dev_err(&efidev->pdev->dev,
			"Flash component initialization failed.\n");
		return rc;
	}

	rc = leicaefi_chr_create_device(efidev);
	if (rc != 0) {
		dev_err(&efidev->pdev->dev, "Cannot create CHR device.\n");
		return rc;
	}

	return 0;
}

static int leicaefi_chr_remove(struct platform_device *pdev)
{
	struct leicaefi_chr_device *efidev = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	leicaefi_chr_remove_device(efidev);

	// resources allocated using devm are freed automatically

	return 0;
}

static const struct of_device_id leicaefi_chr_of_match[] = {
	{
		.compatible = "leica,efi-chr",
	},
	{},
};
MODULE_DEVICE_TABLE(of, leicaefi_chr_of_match);

static struct platform_driver leicaefi_chr_driver = {
	.driver =
		{
			.name = "leica-efi-chr",
			.of_match_table = leicaefi_chr_of_match,
		},
	.probe = leicaefi_chr_probe,
	.remove = leicaefi_chr_remove,
};

static int __init leicaefi_chr_driver_init(void)
{
	int result = 0;

	leicaefi_chr_class = class_create(THIS_MODULE, "leicaefi");
	if (leicaefi_chr_class == NULL) {
		pr_err("%s Failed to create class\n", __func__);
		return -EEXIST;
	}

	result = platform_driver_register(&leicaefi_chr_driver);
	if (result != 0) {
		pr_err("%s Failed to register driver\n", __func__);
		class_destroy(leicaefi_chr_class);
		leicaefi_chr_class = NULL;
		return result;
	}

	return 0;
}

static void __exit leicaefi_chr_driver_exit(void)
{
	platform_driver_unregister(&leicaefi_chr_driver);

	class_destroy(leicaefi_chr_class);
}

// We need to register EFI class so we do not use:
// module_platform_driver(leicaefi_chr_driver);
module_init(leicaefi_chr_driver_init);
module_exit(leicaefi_chr_driver_exit);

// Module information
MODULE_DESCRIPTION("Leica EFI general I/O driver");
MODULE_AUTHOR(
	"Krzysztof Kapuscik <krzysztof.kapuscik-ext@leica-geosystems.com>");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL v2");
