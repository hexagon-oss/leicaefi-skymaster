#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#include <leicaefi.h>
#include <common/leicaefi-chip.h>
#include <common/leicaefi-device.h>

#define LEICAEFI_LED_VALUE_BIT_MASK 0x3 // two bits
#define LEICAEFI_LED_VALUE_OFF 0
#define LEICAEFI_LED_VALUE_FULLY_ON 1
#define LEICAEFI_LED_VALUE_DIMMED 2
#define LEICAEFI_LED_VALUE_DIMMED_BLINKING 3

#define MAX_PATTERN_STEP 64

struct leicaefi_leds_device;

struct leicaefi_led_desc {
	const char *name;
	u8 efi_reg_no;
	u16 efi_reg_offset;
	int initial_brightness;
};

struct leicaefi_led {
	struct leicaefi_leds_device *efidev;
	const struct leicaefi_led_desc *desc;
	struct led_classdev lc;
	int id;

	unsigned long delay_on_intervals;
	unsigned long delay_off_intervals;
	unsigned long current_interval;
	bool prev_state_on;

#ifdef CONFIG_LEDS_TRIGGER_BITPATTERN

	u64 trigger_pattern;

#endif /* CONFIG_LEDS_TRIGGER_BITPATTERN */
};

struct leicaefi_leds_device {
	struct platform_device *pdev;
	struct leicaefi_chip *efichip;

	struct leicaefi_led *leds;

	struct mutex lock;
	struct task_struct *worker_tsk;

#ifdef CONFIG_LEDS_TRIGGER_BITPATTERN

	int current_pattern_step;

#endif /* CONFIG_LEDS_TRIGGER_BITPATTERN */
};

static const unsigned long STATE_REFRESH_INTERVAL_MS = 250;
static const unsigned long MAX_INTERVAL_COUNT =
	(60 * 1000) / STATE_REFRESH_INTERVAL_MS;

// Following EFI specification user application shall not control the battery LED
// but it is registered for test purposes
static const struct leicaefi_led_desc EFI_LED_DESCRIPTORS[] = {
	{ "leicaefi0:red:sd_write", LEICAEFI_REG_LED_CTRL1, 0, 0 },
	{ "leicaefi0:green:sd_write", LEICAEFI_REG_LED_CTRL1, 2, 0 },
	{ "leicaefi0:red:sd", LEICAEFI_REG_LED_CTRL1, 4, 0 },
	{ "leicaefi0:green:sd", LEICAEFI_REG_LED_CTRL1, 6, 0 },
	{ "leicaefi0:red:battery", LEICAEFI_REG_LED_CTRL1, 8, -1 },
	{ "leicaefi0:green:battery", LEICAEFI_REG_LED_CTRL1, 10, -1 },
	{ "leicaefi0:red:power", LEICAEFI_REG_LED_CTRL1, 12, 0 },
	{ "leicaefi0:green:power", LEICAEFI_REG_LED_CTRL1, 14, 1 },

	{ "leicaefi0:red:rtk_out", LEICAEFI_REG_LED_CTRL2, 0, 0 },
	{ "leicaefi0:green:rtk_out", LEICAEFI_REG_LED_CTRL2, 2, 0 },
	{ "leicaefi0:red:rtk_in", LEICAEFI_REG_LED_CTRL2, 4, 0 },
	{ "leicaefi0:green:rtk_in", LEICAEFI_REG_LED_CTRL2, 6, 0 },
	{ "leicaefi0:green:position", LEICAEFI_REG_LED_CTRL2, 8, 0 },
	{ "leicaefi0:red:position", LEICAEFI_REG_LED_CTRL2, 10, 0 },
	{ "leicaefi0:green:wireless", LEICAEFI_REG_LED_CTRL2, 12, 0 },
	{ "leicaefi0:blue:wireless", LEICAEFI_REG_LED_CTRL2, 14, 0 },
};
static const size_t EFI_LED_COUNT =
	sizeof(EFI_LED_DESCRIPTORS) / sizeof(EFI_LED_DESCRIPTORS[0]);

static struct leicaefi_led *leicaefi_led_cast(struct led_classdev *led_cdev)
{
	return container_of(led_cdev, struct leicaefi_led, lc);
}

static int leicaefi_led_brightness_get_unlocked(struct leicaefi_led *led)
{
	int rv = 0;
	u16 int_value_efi = 0;

	rv = leicaefi_chip_read(led->efidev->efichip, led->desc->efi_reg_no,
				&int_value_efi);
	if (rv != 0) {
		dev_warn(&led->efidev->pdev->dev,
			 "%s - getting brightness failed id=%d rv=%d\n",
			 __func__, led->id, rv);
		return 0;
	}

	int_value_efi >>= led->desc->efi_reg_offset;
	int_value_efi &= LEICAEFI_LED_VALUE_BIT_MASK;

	return (int_value_efi == LEICAEFI_LED_VALUE_OFF) ? 0 : 1;
}

static enum led_brightness
leicaefi_led_brightness_get(struct led_classdev *led_cdev)
{
	struct leicaefi_led *led = leicaefi_led_cast(led_cdev);
	int int_value_kernel = 0;

	dev_dbg(&led->efidev->pdev->dev, "%s id=%d\n", __func__, led->id);

	mutex_lock(&led->efidev->lock);

	int_value_kernel = leicaefi_led_brightness_get_unlocked(led);

	mutex_unlock(&led->efidev->lock);

	return (enum led_brightness)int_value_kernel;
}

static int
leicaefi_led_set_register_unlocked(struct leicaefi_leds_device *efidev,
				   u16 efi_reg_no, u16 new_value_efi,
				   u16 mask_value_efi)
{
	int rv = 0;

	if (mask_value_efi == 0) {
		/* no changes requested, skip the call */
		return 0;
	}

	dev_dbg(&efidev->pdev->dev, "%s\n", __func__);

	rv = leicaefi_chip_clear_bits(efidev->efichip, efi_reg_no,
				      mask_value_efi);
	if (rv != 0) {
		dev_warn(&efidev->pdev->dev,
			 "%s - clearing bits failed rv=%d\n", __func__, rv);
		return rv;
	}

	/* sleep for a moment, sending clear then set too fast results in an ugly gap when blinking */
	mdelay(1);

	rv = leicaefi_chip_set_bits(efidev->efichip, efi_reg_no,
				    new_value_efi & mask_value_efi);
	if (rv != 0) {
		dev_warn(&efidev->pdev->dev, "%s - setting bits failed rv=%d\n",
			 __func__, rv);
		return rv;
	}

	/* sleep for a moment, sending clear then set too fast results in an ugly gap when blinking */
	mdelay(1);

	dev_dbg(&efidev->pdev->dev, "%s - done\n", __func__);

	return 0;
}

static int leicaefi_led_brightness_set_unlocked(struct leicaefi_led *led,
						int int_value_kernel)
{
	u16 new_value_efi = (int_value_kernel > 0) ? LEICAEFI_LED_VALUE_DIMMED :
						     LEICAEFI_LED_VALUE_OFF;
	u16 mask_value_efi = LEICAEFI_LED_VALUE_BIT_MASK;

	/* workaround - do not change battery led status on device removal */
	if ((led->desc->initial_brightness < 0) && (!led->efidev->worker_tsk)) {
		return 0;
	}

	new_value_efi <<= led->desc->efi_reg_offset;
	mask_value_efi <<= led->desc->efi_reg_offset;

	return leicaefi_led_set_register_unlocked(led->efidev,
						  led->desc->efi_reg_no,
						  new_value_efi,
						  mask_value_efi);
}

static int leicaefi_led_brightness_set(struct led_classdev *led_cdev,
				       enum led_brightness value)
{
	struct leicaefi_led *led = leicaefi_led_cast(led_cdev);
	int rv = 0;
	int int_value_kernel = (int)value;

	dev_dbg(&led->efidev->pdev->dev, "%s id=%d value=%d\n", __func__,
		led->id, int_value_kernel);

	mutex_lock(&led->efidev->lock);

	dev_dbg(&led->efidev->pdev->dev, "%s id=%d value=%d - working\n",
		__func__, led->id, int_value_kernel);

	if (int_value_kernel == 0) {
		led->delay_on_intervals = led->delay_off_intervals =
			led->current_interval = 0;
	}

	rv = leicaefi_led_brightness_set_unlocked(led, int_value_kernel);

	mutex_unlock(&led->efidev->lock);

	dev_dbg(&led->efidev->pdev->dev, "%s id=%d value=%d - done\n", __func__,
		led->id, int_value_kernel);

	return rv;
}

static void
leicaefi_led_update_blink_state_unlocked(struct leicaefi_leds_device *efidev)
{
	size_t i = 0;
	u16 reg_value_1 = 0;
	u16 reg_value_2 = 0;
	u16 reg_mask_1 = 0;
	u16 reg_mask_2 = 0;

	/* TODO: synchronize all LEDs together (so for same parameters they
	 * would blink at the same time and not shifted / doing this would
	 * probably require limiting the allowed delays to just a few values */

	for (i = 0; i < EFI_LED_COUNT; i++) {
		struct leicaefi_led *led = &efidev->leds[i];

		bool state_on = true;

		/* skip non-blinking LEDs */
		if ((led->delay_off_intervals == 0) ||
		    (led->delay_on_intervals == 0)) {
			continue;
		}

		/* extra handling for 'opposite blinking' of two-color LEDs */
		state_on = (led->current_interval < led->delay_on_intervals);

		/* logic to synchronize blinking the two LEDs under one front panel icon
		 * - they should blink in inverted cycle (if same parameters are set) so
		 *   it will look e.g. like red/green/red/green and not yellow/blank/yellow */
		if (i & 0x1) {
			struct leicaefi_led *other_led = &efidev->leds[i - 1];

			/* same rate, same on/off time */
			if ((led->delay_off_intervals ==
			     led->delay_on_intervals) &&
			    (led->delay_off_intervals ==
			     other_led->delay_off_intervals) &&
			    (led->delay_on_intervals ==
			     other_led->delay_on_intervals)) {
				/* state is opposite to the other led */
				state_on = (other_led->current_interval >=
					    other_led->delay_on_intervals);
			}
		}

		if (state_on != led->prev_state_on) {
			u16 new_value_efi = (state_on) ?
						    LEICAEFI_LED_VALUE_DIMMED :
						    LEICAEFI_LED_VALUE_OFF;
			u16 mask_value_efi = LEICAEFI_LED_VALUE_BIT_MASK;

			new_value_efi <<= led->desc->efi_reg_offset;
			mask_value_efi <<= led->desc->efi_reg_offset;

			if (led->desc->efi_reg_no == LEICAEFI_REG_LED_CTRL1) {
				reg_value_1 |= new_value_efi;
				reg_mask_1 |= mask_value_efi;
			} else {
				reg_value_2 |= new_value_efi;
				reg_mask_2 |= mask_value_efi;
			}

			led->prev_state_on = state_on;
		}
	}

	leicaefi_led_set_register_unlocked(efidev, LEICAEFI_REG_LED_CTRL1,
					   reg_value_1, reg_mask_1);
	leicaefi_led_set_register_unlocked(efidev, LEICAEFI_REG_LED_CTRL2,
					   reg_value_2, reg_mask_2);

	for (i = 0; i < EFI_LED_COUNT; i++) {
		struct leicaefi_led *led = &efidev->leds[i];

		/* skip non-blinking LEDs */
		if ((led->delay_off_intervals == 0) ||
		    (led->delay_on_intervals == 0)) {
			continue;
		}

		/* cycle interval */
		if (++led->current_interval >=
		    led->delay_on_intervals + led->delay_off_intervals) {
			led->current_interval = 0;
		}
	}
}

static int leicaefi_led_blink_set(struct led_classdev *led_cdev,
				  unsigned long *delay_on,
				  unsigned long *delay_off)
{
	struct leicaefi_led *led = leicaefi_led_cast(led_cdev);
	int rv = 0;

	dev_dbg(&led->efidev->pdev->dev,
		"%s id=%d delay_on=%lu delay_off=%lu\n", __func__, led->id,
		*delay_on, *delay_off);

	/* set defaults if not specified by the user */

	if (*delay_on == 0) {
		*delay_on = STATE_REFRESH_INTERVAL_MS;
	}
	if (*delay_off == 0) {
		*delay_off = STATE_REFRESH_INTERVAL_MS;
	}

	/* round the values */
	*delay_on += STATE_REFRESH_INTERVAL_MS - 1;
	*delay_off += STATE_REFRESH_INTERVAL_MS - 1;

	*delay_on /= STATE_REFRESH_INTERVAL_MS;
	*delay_off /= STATE_REFRESH_INTERVAL_MS;

	/* note: at this point the delays are in number of intervals! */

	if (*delay_on > MAX_INTERVAL_COUNT) {
		*delay_on = MAX_INTERVAL_COUNT;
	}
	if (*delay_off > MAX_INTERVAL_COUNT) {
		*delay_off = MAX_INTERVAL_COUNT;
	}

	mutex_lock(&led->efidev->lock);

	/* store the settings */
	led->delay_on_intervals = *delay_on;
	led->delay_off_intervals = *delay_off;
	led->current_interval = 0;

	/* turn the led off, timer will turn it on if needed */
	led->prev_state_on = false;
	rv = leicaefi_led_brightness_set_unlocked(led, 0);

	mutex_unlock(&led->efidev->lock);

	/* convert back to milliseconds */
	*delay_on *= STATE_REFRESH_INTERVAL_MS;
	*delay_off *= STATE_REFRESH_INTERVAL_MS;

	dev_dbg(&led->efidev->pdev->dev,
		"%s id=%d delay_on=%lu delay_off=%lu - done\n", __func__,
		led->id, *delay_on, *delay_off);

	return rv;
}

#ifdef CONFIG_LEDS_TRIGGER_BITPATTERN

static void
leicaefi_led_update_pattern_unlocked(struct leicaefi_leds_device *efidev)
{
	size_t i = 0;
	u16 reg_value_1 = 0;
	u16 reg_value_2 = 0;
	u16 reg_mask_1 = 0;
	u16 reg_mask_2 = 0;
	u64 current_bit_mask = ((u64)1 << efidev->current_pattern_step);

	for (i = 0; i < EFI_LED_COUNT; i++) {
		struct leicaefi_led *led = &efidev->leds[i];

		bool state_on = true;

		/* skip non-blinking LEDs */
		if (led->trigger_pattern == 0) {
			continue;
		}

		state_on = (led->trigger_pattern & current_bit_mask);

		if (state_on != led->prev_state_on) {
			u16 new_value_efi = (state_on) ?
						    LEICAEFI_LED_VALUE_DIMMED :
						    LEICAEFI_LED_VALUE_OFF;
			u16 mask_value_efi = LEICAEFI_LED_VALUE_BIT_MASK;

			new_value_efi <<= led->desc->efi_reg_offset;
			mask_value_efi <<= led->desc->efi_reg_offset;

			if (led->desc->efi_reg_no == LEICAEFI_REG_LED_CTRL1) {
				reg_value_1 |= new_value_efi;
				reg_mask_1 |= mask_value_efi;
			} else {
				reg_value_2 |= new_value_efi;
				reg_mask_2 |= mask_value_efi;
			}

			led->prev_state_on = state_on;
		}
	}

	leicaefi_led_set_register_unlocked(efidev, LEICAEFI_REG_LED_CTRL1,
					   reg_value_1, reg_mask_1);
	leicaefi_led_set_register_unlocked(efidev, LEICAEFI_REG_LED_CTRL2,
					   reg_value_2, reg_mask_2);

	if (++efidev->current_pattern_step == MAX_PATTERN_STEP) {
		efidev->current_pattern_step = 0;
	}
}

#endif /*CONFIG_LEDS_TRIGGER_BITPATTERN*/

static int leicaefi_leds_thread_loop(void *data)
{
	struct leicaefi_leds_device *efidev =
		(struct leicaefi_leds_device *)data;

	dev_dbg(&efidev->pdev->dev, "%s - started\n", __func__);

	while (!kthread_should_stop()) {
		mutex_lock(&efidev->lock);

		dev_dbg(&efidev->pdev->dev, "%s - working\n", __func__);

		leicaefi_led_update_blink_state_unlocked(efidev);

#ifdef CONFIG_LEDS_TRIGGER_BITPATTERN
		leicaefi_led_update_pattern_unlocked(efidev);
#endif /*CONFIG_LEDS_TRIGGER_BITPATTERN*/

		mutex_unlock(&efidev->lock);

		msleep(STATE_REFRESH_INTERVAL_MS);
	}

	dev_dbg(&efidev->pdev->dev, "%s - finished\n", __func__);

	return 0;
}

#ifdef CONFIG_LEDS_TRIGGER_BITPATTERN

static int leicaefi_led_bit_pattern_set(struct led_classdev *led_cdev,
					unsigned long step_delay, u64 pattern,
					int pattern_len)
{
	struct leicaefi_led *led = leicaefi_led_cast(led_cdev);
	u64 mask = 0;
	int i = 0;
	int current_pattern_len = 0;

	dev_dbg(&led->efidev->pdev->dev,
		"%s delay=%lu pattern=%llu pattern_len=%d\n", __func__,
		step_delay, pattern, pattern_len);

	if ((step_delay != STATE_REFRESH_INTERVAL_MS) || (pattern_len < 1)) {
		dev_warn(&led->efidev->pdev->dev,
			 "%s - unsupported arguments\n", __func__);

		return -EINVAL;
	}

	for (i = 0; i < pattern_len; ++i) {
		mask <<= 1;
		mask |= 1;
	}

	pattern &= mask;

	current_pattern_len = pattern_len;
	while (current_pattern_len < MAX_PATTERN_STEP) {
		u64 temp = pattern << current_pattern_len;
		pattern |= temp;
		current_pattern_len *= 2;
	}

	if (current_pattern_len != MAX_PATTERN_STEP) {
		dev_warn(&led->efidev->pdev->dev,
			 "%s - unsupported pattern len\n", __func__);

		return -EINVAL;
	}

	dev_dbg(&led->efidev->pdev->dev, "%s settings pattern=%llu\n", __func__,
		pattern);

	mutex_lock(&led->efidev->lock);

	dev_dbg(&led->efidev->pdev->dev, "%s settings pattern=%llu - working\n",
		__func__, pattern);

	led->trigger_pattern = pattern;
	led->prev_state_on = false;
	leicaefi_led_brightness_set_unlocked(led, 0);

	mutex_unlock(&led->efidev->lock);

	dev_dbg(&led->efidev->pdev->dev, "%s settings pattern=%llu - done\n",
		__func__, pattern);

	return 0;
}

void leicaefi_led_bit_pattern_clear(struct led_classdev *led_cdev)
{
	struct leicaefi_led *led = leicaefi_led_cast(led_cdev);

	dev_dbg(&led->efidev->pdev->dev, "%s\n", __func__);

	mutex_lock(&led->efidev->lock);

	dev_dbg(&led->efidev->pdev->dev, "%s - working\n", __func__);

	led->trigger_pattern = 0;
	led->prev_state_on = false;
	leicaefi_led_brightness_set_unlocked(led, 0);

	mutex_unlock(&led->efidev->lock);

	dev_dbg(&led->efidev->pdev->dev, "%s - done\n", __func__);
}

#endif /* CONFIG_LEDS_TRIGGER_BITPATTERN */

static int leicaefi_leds_probe(struct platform_device *pdev)
{
	struct leicaefi_leds_device *efidev = NULL;
	struct leicaefi_platform_data *pdata = NULL;
	size_t i = 0;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	efidev = devm_kzalloc(&pdev->dev, sizeof(*efidev), GFP_KERNEL);
	if (efidev == NULL) {
		dev_err(&pdev->dev, "Cannot allocate memory for device\n");
		return -ENOMEM;
	}

	efidev->leds = devm_kzalloc(&pdev->dev,
				    EFI_LED_COUNT * sizeof(struct leicaefi_led),
				    GFP_KERNEL);
	if (efidev->leds == NULL) {
		dev_err(&pdev->dev,
			"Cannot allocate memory for led instances\n");
		return -ENOMEM;
	}

	mutex_init(&efidev->lock);

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

	/* register leds */
	for (i = 0; i < EFI_LED_COUNT; i++) {
		int ret = 0;

		memset(&efidev->leds[i], 0, sizeof(efidev->leds[i]));
		efidev->leds[i].desc = &EFI_LED_DESCRIPTORS[i];
		efidev->leds[i].efidev = efidev;
		efidev->leds[i].id = (int)i;
		efidev->leds[i].lc.name = EFI_LED_DESCRIPTORS[i].name;
		efidev->leds[i].lc.max_brightness = 1;
		efidev->leds[i].lc.brightness_get = leicaefi_led_brightness_get;
		efidev->leds[i].lc.brightness_set_blocking =
			leicaefi_led_brightness_set;
		efidev->leds[i].lc.blink_set = leicaefi_led_blink_set;

#ifdef CONFIG_LEDS_TRIGGER_BITPATTERN

		efidev->leds[i].lc.bit_pattern_set =
			leicaefi_led_bit_pattern_set;
		efidev->leds[i].lc.bit_pattern_clear =
			leicaefi_led_bit_pattern_clear;

#endif /* CONFIG_LEDS_TRIGGER_BITPATTERN */

		efidev->leds[i].delay_on_intervals = 0;
		efidev->leds[i].delay_off_intervals = 0;

		ret = devm_led_classdev_register(&efidev->pdev->dev,
						 &efidev->leds[i].lc);
		if (ret) {
			dev_err(&efidev->pdev->dev,
				"Failed to register led %d\n", (int)i);
			return ret;
		}
	}

	mutex_lock(&efidev->lock);

	/* init leds */
	for (i = 0; i < EFI_LED_COUNT; i++) {
		if (efidev->leds[i].desc->initial_brightness >= 0) {
			leicaefi_led_brightness_set_unlocked(
				&efidev->leds[i],
				efidev->leds[i].desc->initial_brightness);
		}
	}

	mutex_unlock(&efidev->lock);

	efidev->worker_tsk = kthread_create(leicaefi_leds_thread_loop, efidev,
					    "leicaefi_leds_worker");
	if (IS_ERR(efidev->worker_tsk)) {
		int rv = PTR_ERR(efidev->worker_tsk);

		dev_err(&efidev->pdev->dev, "Failed to create thread\n");
		efidev->worker_tsk = NULL;

		return rv;
	}

	dev_dbg(&pdev->dev, "%s - starting thread %p\n", __func__,
		efidev->worker_tsk);
	wake_up_process(efidev->worker_tsk);

	dev_dbg(&pdev->dev, "%s - done\n", __func__);

	return 0;
}

static int leicaefi_leds_remove(struct platform_device *pdev)
{
	struct leicaefi_leds_device *efidev = platform_get_drvdata(pdev);
	int i = 0;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	if (efidev->worker_tsk) {
		dev_dbg(&pdev->dev, "%s - stopping thread %p\n", __func__,
			efidev->worker_tsk);

		kthread_stop(efidev->worker_tsk);
		efidev->worker_tsk = NULL;
	}

	/* unregister leds */
	for (i = 0; i < EFI_LED_COUNT; i++) {
		int ret = 0;

		devm_led_classdev_unregister(&efidev->pdev->dev,
					     &efidev->leds[i].lc);
	}

	if (efidev->efichip) {
		// turn off all leds (except battery)
		leicaefi_chip_clear_bits(efidev->efichip,
					 LEICAEFI_REG_LED_CTRL1, 0xF0FF);
		leicaefi_chip_clear_bits(efidev->efichip,
					 LEICAEFI_REG_LED_CTRL2, 0xFFFF);
		// turn on the green power led
		leicaefi_chip_set_bits(efidev->efichip, LEICAEFI_REG_LED_CTRL1,
				       1 << 14);
	}

	dev_dbg(&pdev->dev, "%s - done\n", __func__);

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
MODULE_VERSION("0.2");
MODULE_LICENSE("GPL v2");
