# The headers are in 'include' subfolder
ccflags-y := -I$(PWD)/include

# For development purposes - enable debugging/logging functionality
ccflags-y += -DDEBUG=1

# For development purposes - enable hooks for register read/write to see operations
ccflags-y += -DREAD_WRITE_DEBUG_ENABLED=1

# Modules
obj-m += leicaefi-core.o
obj-m += leicaefi-chr.o

leicaefi-core-y := src/leicaefi-core.o

leicaefi-chr-y := src/leicaefi-chr.o
