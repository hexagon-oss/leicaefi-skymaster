# The headers are in 'include' subfolder
ccflags-y := -I$(PWD)/include
ccflags-y += -I$(PWD)/src

# For development purposes - enable debugging/logging functionality
# ccflags-y += -DDEBUG=1

# Modules
obj-m += leicaefi-core.o
obj-m += leicaefi-chr.o
obj-m += leicaefi-reboothook.o
obj-m += leicaefi-leds.o
obj-m += leicaefi-keys.o
obj-m += leicaefi-power.o

leicaefi-core-y := src/core/leicaefi-core.o
leicaefi-core-y += src/core/leicaefi-chip.o
leicaefi-core-y += src/core/leicaefi-irq.o

leicaefi-chr-y := src/chr/leicaefi-chr.o
leicaefi-chr-y += src/chr/leicaefi-chr-reg.o
leicaefi-chr-y += src/chr/leicaefi-chr-flash.o
leicaefi-chr-y += src/chr/leicaefi-chr-power.o

leicaefi-reboothook-y := src/reboothook/leicaefi-reboothook.o

leicaefi-leds-y := src/leds/leicaefi-leds.o

leicaefi-keys-y := src/keys/leicaefi-keys.o

leicaefi-power-y := src/power/leicaefi-power.o
leicaefi-power-y += src/power/leicaefi-charger.o
leicaefi-power-y += src/power/leicaefi-battery.o
