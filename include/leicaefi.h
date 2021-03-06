#ifndef _LINUX_LEICAEFI_H
#define _LINUX_LEICAEFI_H

#include <linux/types.h>
#include <leicaefi-defs.h>

#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

/* Error codes */

/* Base number for error codes */
#define LEICAEFI_ERRNO_BASE (1000)
/* Error - Internal driver problem. */
#define LEICAEFI_EINTERNAL (LEICAEFI_ERRNO_BASE + 0)
/* Error - Access to flash memory is denied (e.g. write not enabled) */
#define LEICAEFI_EFLASHACCESS (LEICAEFI_ERRNO_BASE + 1)
/* Error - Bad mode (e.g. checking loader CRC when running lodaer mode) */
#define LEICAEFI_EBADMODE (LEICAEFI_ERRNO_BASE + 2)
/* Error - Requested operation failed */
#define LEICAEFI_EOPFAIL (LEICAEFI_ERRNO_BASE + 3)
/* Error - Generic command operation failed */
#define LEICAEFI_EGENCMDFAIL (LEICAEFI_ERRNO_BASE + 4)

/* Software mode: loader */
#define LEICAEFI_SOFTWARE_MODE_LOADER ((__u8)1)
/* Software mode: firmware */
#define LEICAEFI_SOFTWARE_MODE_FIRMWARE ((__u8)2)

/* Synchronized LED state refresh rate */
#define LEICAEFI_LED_SYNC_REFRESH_RATE_MS (250)

/* Power source: unknown source */
#define LEICAEFI_POWER_SOURCE_UNKOWN_SOURCE ((__u8)0)
/* Power source: internal battery 1 */
#define LEICAEFI_POWER_SOURCE_INTERNAL_BATTERY1 ((__u8)1)
/* Power source: external source 1 */
#define LEICAEFI_POWER_SOURCE_EXTERNAL_SOURCE1 ((__u8)2)
/* Power source: external source 2 */
#define LEICAEFI_POWER_SOURCE_EXTERNAL_SOURCE2 ((__u8)3)
/* Power source: power over ethernet 1 */
#define LEICAEFI_POWER_SOURCE_POE_SOURCE1 ((__u8)4)

/* One wire: com 1 port */
#define LEICAEFI_ONEWIRE_PORT_COM1 ((__u8)0)
/* One wire: com 2 port */
#define LEICAEFI_ONEWIRE_PORT_COM2 ((__u8)1)
/* One wire: com 3 port */
#define LEICAEFI_ONEWIRE_PORT_COM3 ((__u8)2)

struct leicaefi_ioctl_regrw {
	__u8 reg_no;
	__u16 reg_value;
};

struct leicaefi_ioctl_flash_checksum {
	/* in: flash partition to check (separate partitions are defined for each mode) */
	__u8 mode;
	/* out: result of check - 0 => failed, <>0 => success */
	__u8 check_result;
};

struct leicaefi_ioctl_flash_rw {
	/* in: address to read/write */
	__u16 address;
	/* [write] in: value to write; [read] out: value read */
	__u16 value;
};

struct leicaefi_ioctl_flash_erase {
	/* in: segment address to erase */
	__u16 address;
};

struct leicaefi_ioctl_flash_write_enable {
	/* in: zero to disable, non-zero to enable flash write/erase */
	__u8 enable;
};

struct leicaefi_ioctl_mode {
	/* [set] in: target mode; [get] out: current mode */
	__u8 mode;
};

struct leicaefi_ioctl_power_source {
	/* out: current power source */
	__u8 power_source;
};

struct leicaefi_ioctl_led_test_mode_enable {
	/* in: zero to disable, non-zero to enable led test mode */
	__u8 enable;
};

struct leicaefi_ioctl_onewire_device {
	/* in: communication port */
	__u8 port;
	/*  out: device id */
	__u8 id;
	/*  out: device family code */
	__u8 family_code;
};

/* write means user -> write-to -> kernel */
#define LEICAEFI_IOCTL_MAGIC 0xDF

#define LEICAEFI_IOCTL_READ                                                    \
	_IOC(_IOC_READ | _IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 1,                  \
	     sizeof(struct leicaefi_ioctl_regrw))
#define LEICAEFI_IOCTL_WRITE                                                   \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 2,                              \
	     sizeof(struct leicaefi_ioctl_regrw))
#define LEICAEFI_IOCTL_BITS_SET                                                \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 3,                              \
	     sizeof(struct leicaefi_ioctl_regrw))
#define LEICAEFI_IOCTL_BITS_CLEAR                                              \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 4,                              \
	     sizeof(struct leicaefi_ioctl_regrw))
#define LEICAEFI_IOCTL_WRITE_RAW                                               \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 5,                              \
	     sizeof(struct leicaefi_ioctl_regrw))

#define LEICAEFI_IOCTL_FLASH_CHECK_CHECKSUM                                    \
	_IOC(_IOC_READ | _IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 6,                  \
	     sizeof(struct leicaefi_ioctl_flash_checksum))
#define LEICAEFI_IOCTL_FLASH_WRITE                                             \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 7,                              \
	     sizeof(struct leicaefi_ioctl_flash_rw))
#define LEICAEFI_IOCTL_FLASH_READ                                              \
	_IOC(_IOC_READ | _IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 8,                  \
	     sizeof(struct leicaefi_ioctl_flash_rw))
#define LEICAEFI_IOCTL_FLASH_ERASE_SEGMENT                                     \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 9,                              \
	     sizeof(struct leicaefi_ioctl_flash_erase))
#define LEICAEFI_IOCTL_FLASH_WRITE_ENABLE                                      \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 10,                             \
	     sizeof(struct leicaefi_ioctl_flash_write_enable))

#define LEICAEFI_IOCTL_GET_MODE                                                \
	_IOC(_IOC_READ, LEICAEFI_IOCTL_MAGIC, 11,                              \
	     sizeof(struct leicaefi_ioctl_mode))
#define LEICAEFI_IOCTL_SET_MODE                                                \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 12,                             \
	     sizeof(struct leicaefi_ioctl_mode))

#define LEICAEFI_IOCTL_IFLASH_READ                                             \
	_IOC(_IOC_READ | _IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 13,                 \
	     sizeof(struct leicaefi_ioctl_flash_rw))

#define LEICAEFI_IOCTL_GET_ACTIVE_POWER_SOURCE                                 \
	_IOC(_IOC_READ, LEICAEFI_IOCTL_MAGIC, 14,                              \
	     sizeof(struct leicaefi_ioctl_power_source))

#define LEICAEFI_IOCTL_LED_SET_TEST_MODE                                       \
	_IOC(_IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 15,                             \
	     sizeof(struct leicaefi_ioctl_led_test_mode_enable))

#define LEICAEFI_IOCTL_ONE_WIRE_DEVICE_INFO                                    \
	_IOC(_IOC_READ | _IOC_WRITE, LEICAEFI_IOCTL_MAGIC, 16,                 \
	     sizeof(struct leicaefi_ioctl_onewire_device))

#endif /*_LINUX_LEICAEFI_H*/
