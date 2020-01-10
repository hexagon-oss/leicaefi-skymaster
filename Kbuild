# The headers are in 'include' subfolder
ccflags-y := -I$(PWD)/include
ccflags-y += -I$(PWD)/src

# For development purposes - enable debugging/logging functionality
ccflags-y += -DDEBUG=1

# Modules
obj-m += leicaefi-core.o
obj-m += leicaefi-chr.o

leicaefi-core-y := src/leicaefi-core.o
leicaefi-core-y += src/core/leicaefi-chip.o
leicaefi-core-y += src/core/leicaefi-irq.o

leicaefi-chr-y := src/chr/leicaefi-chr.o
leicaefi-chr-y += src/chr/leicaefi-chr-reg.o
