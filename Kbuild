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

leicaefi-core-y := src/core/leicaefi-core.o
leicaefi-core-y += src/core/leicaefi-chip.o
leicaefi-core-y += src/core/leicaefi-irq.o

leicaefi-chr-y := src/chr/leicaefi-chr.o
leicaefi-chr-y += src/chr/leicaefi-chr-reg.o
leicaefi-chr-y += src/chr/leicaefi-chr-flash.o

leicaefi-reboothook-y := src/reboothook/leicaefi-reboothook.o

leicaefi-leds-y := src/leds/leicaefi-leds.o
