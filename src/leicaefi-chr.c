#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#include <leicaefi.h>
#include <leicaefi-utils.h>

struct leicaefi_chr_device {
	struct platform_device *pdev;
	struct regmap *regmap;

	bool chr_region_allocated;
	dev_t chr_dev;
	struct cdev chr_cdev;
	bool chr_cdev_added;
	bool chr_device_created;
	struct file_operations chr_file_ops;
	int chr_major;
};

struct class *leicaefi_chr_class = NULL;

static int leicaefi_chr_open(struct inode *inode, struct file *filep)
{
	struct leicaefi_chr_device *efidev = container_of(
		inode->i_cdev, struct leicaefi_chr_device, chr_cdev);

	dev_dbg(&efidev->pdev->dev, "Parent device not available\n");

	// store pointer to the device in file for later use
	filep->private_data = efidev;

	return 0;
}

static int leicaefi_chr_release(struct inode *inode, struct file *filep)
{
	return 0;
}

static ssize_t leicaefi_chr_read(struct file *filep, char __user *buffer,
				 size_t length, loff_t *offset)
{
	// TODO: planned feature - interrupts support
	// TODO: the file could be used to pass interrupts to be handled to the listeners (as in UIO).
	// TODO: for example by using blocking i/o as in https://www.oreilly.com/library/view/linux-device-drivers/0596005903/ch06.html

	return 0;
}

static ssize_t leicaefi_chr_write(struct file *filep, const char __user *buffer,
				  size_t length, loff_t *offset)
{
	return -EINVAL;
}

static long chr_ioctl_read(struct leicaefi_chr_device *efidev,
			   unsigned long arg)
{
	struct leicaefi_ioctl_regrw __user *user_data = (void __user *)arg;
	struct leicaefi_ioctl_regrw kernel_data;

	if ((arg == 0) || (copy_from_user(&kernel_data, user_data,
					  sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data from user space\n", __func__);
		return -EACCES;
	}

	if (leicaefi_read(efidev->regmap, kernel_data.reg_no,
			  &kernel_data.reg_value) != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d)\n", __func__,
			 (int)kernel_data.reg_no);
		return -EIO;
	}

	if ((arg == 0) ||
	    (copy_to_user(user_data, &kernel_data, sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data to user space\n", __func__);
		return -EACCES;
	}

	return 0;
}

static long chr_ioctl_write(struct leicaefi_chr_device *efidev,
			    unsigned long arg)
{
	struct leicaefi_ioctl_regrw __user *user_data = (void __user *)arg;
	struct leicaefi_ioctl_regrw kernel_data;

	if ((arg == 0) || (copy_from_user(&kernel_data, user_data,
					  sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data from user space\n", __func__);
		return -EACCES;
	}

	if (leicaefi_write(efidev->regmap, kernel_data.reg_no,
			   kernel_data.reg_value) != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d, value: %d)\n",
			 __func__, (int)kernel_data.reg_no,
			 (int)kernel_data.reg_value);
		return -EIO;
	}

	return 0;
}

static long chr_ioctl_write_raw(struct leicaefi_chr_device *efidev,
				unsigned long arg)
{
	struct leicaefi_ioctl_regrw __user *user_data = (void __user *)arg;
	struct leicaefi_ioctl_regrw kernel_data;

	if ((arg == 0) || (copy_from_user(&kernel_data, user_data,
					  sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data from user space\n", __func__);
		return -EACCES;
	}

	if (kernel_data.reg_no & LEICAEFI_SCBIT_SET) {
		if (leicaefi_set_bits(efidev->regmap,
				      kernel_data.reg_no & LEICAEFI_REGNO_MASK,
				      kernel_data.reg_value) != 0) {
			dev_warn(
				&efidev->pdev->dev,
				"%s - I/O operation failed (regno: %d, value: %d)\n",
				__func__, (int)kernel_data.reg_no,
				(int)kernel_data.reg_value);
			return -EIO;
		}
	} else {
		if (leicaefi_clear_bits(efidev->regmap,
					kernel_data.reg_no &
						LEICAEFI_REGNO_MASK,
					kernel_data.reg_value) != 0) {
			dev_warn(
				&efidev->pdev->dev,
				"%s - I/O operation failed (regno: %d, value: %d)\n",
				__func__, (int)kernel_data.reg_no,
				(int)kernel_data.reg_value);
			return -EIO;
		}
	}

	return 0;
}

static long chr_ioctl_bits_set(struct leicaefi_chr_device *efidev,
			       unsigned long arg)
{
	struct leicaefi_ioctl_regrw __user *user_data = (void __user *)arg;
	struct leicaefi_ioctl_regrw kernel_data;

	if ((arg == 0) || (copy_from_user(&kernel_data, user_data,
					  sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data from user space\n", __func__);
		return -EACCES;
	}

	if (leicaefi_set_bits(efidev->regmap, kernel_data.reg_no,
			      kernel_data.reg_value) != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d, value: %d)\n",
			 __func__, (int)kernel_data.reg_no,
			 (int)kernel_data.reg_value);
		return -EIO;
	}

	return 0;
}

static long chr_ioctl_bits_clear(struct leicaefi_chr_device *efidev,
				 unsigned long arg)
{
	struct leicaefi_ioctl_regrw __user *user_data = (void __user *)arg;
	struct leicaefi_ioctl_regrw kernel_data;

	if ((arg == 0) || (copy_from_user(&kernel_data, user_data,
					  sizeof(kernel_data)) != 0)) {
		dev_warn(&efidev->pdev->dev,
			 "%s - Cannot copy data from user space\n", __func__);
		return -EACCES;
	}

	if (leicaefi_clear_bits(efidev->regmap, kernel_data.reg_no,
				kernel_data.reg_value) != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - I/O operation failed (regno: %d, value: %d)\n",
			 __func__, (int)kernel_data.reg_no,
			 (int)kernel_data.reg_value);
		return -EIO;
	}

	return 0;
}

static long leicaefi_chr_unlocked_ioctl(struct file *filep, unsigned int cmd,
					unsigned long arg)
{
	long result = -EINVAL;

	struct leicaefi_chr_device *efidev = filep->private_data;

	switch (cmd) {
	case LEICAEFI_IOCTL_READ:
		result = chr_ioctl_read(efidev, arg);
		break;
	case LEICAEFI_IOCTL_WRITE:
		result = chr_ioctl_write(efidev, arg);
		break;
	case LEICAEFI_IOCTL_BITS_SET:
		result = chr_ioctl_bits_set(efidev, arg);
		break;
	case LEICAEFI_IOCTL_BITS_CLEAR:
		result = chr_ioctl_bits_clear(efidev, arg);
		break;
	case LEICAEFI_IOCTL_WRITE_RAW:
		result = chr_ioctl_write_raw(efidev, arg);
		break;
	}

	return result;
}

static int leicaefi_chr_create_device(struct leicaefi_chr_device *efidev)
{
	int error = 0;
	int major = 0;

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
	error = alloc_chrdev_region(&efidev->chr_dev, 0, 1, "leicaefi");
	if (error < 0) {
		dev_err(&efidev->pdev->dev, "Major number allocation failed\n");
		return error;
	}
	efidev->chr_region_allocated = true;

	major = MAJOR(efidev->chr_dev);
	printk(KERN_INFO "The major number is %d", major);

	cdev_init(&efidev->chr_cdev, &efidev->chr_file_ops);

	error = cdev_add(&efidev->chr_cdev, efidev->chr_dev, 1);
	if (error < 0) {
		dev_err(&efidev->pdev->dev, "Failed to add cdev\n");
		return error;
	}
	efidev->chr_cdev_added = true;

	if (!device_create(leicaefi_chr_class, &efidev->pdev->dev,
			   efidev->chr_dev, NULL, "leicaefi%d",
			   MINOR(efidev->chr_dev))) {
		dev_err(&efidev->pdev->dev, "Failed to create device\n");
		return -EINVAL;
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

	dev_dbg(&pdev->dev, "%s\n", __func__);

	efidev = devm_kzalloc(&pdev->dev, sizeof(*efidev), GFP_KERNEL);
	if (efidev == NULL) {
		dev_err(&pdev->dev, "Cannot allocate memory for device\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, efidev);
	efidev->pdev = pdev;

	if (efidev->pdev->dev.parent == NULL) {
		dev_err(&efidev->pdev->dev, "Parent device not available\n");
		return -ENODEV;
	}

	efidev->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!efidev->regmap) {
		dev_err(&efidev->pdev->dev, "Parent regmap unavailable.\n");
		return -ENODEV;
	}

	return leicaefi_chr_create_device(efidev);
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
