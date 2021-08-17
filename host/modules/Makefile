
obj-m  := dragonprobe.o dragonprobe-char.o i2c-dragonprobe.o spi-dragonprobe.o dragonprobe-hwmon.o
KDIR   := /lib/modules/$(shell uname -r)/build
PWD    := $(shell pwd)

CFLAGS += -Wall -Wpedantic

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(RM) -v *.o *.ko *.mod *.mod.c Module.symvers modules.order .*.cmd .*.d

load:
	sudo insmod ./dragonprobe.ko
	sudo insmod ./dragonprobe-char.ko
	sudo insmod ./i2c-dragonprobe.ko
	sudo insmod ./spi-dragonprobe.ko
	sudo insmod ./dragonprobe-hwmon.ko

unload:
	sudo rmmod dragonprobe-hwmon
	sudo rmmod spi-dragonprobe
	sudo rmmod i2c-dragonprobe
	sudo rmmod dragonprobe-char
	sudo rmmod dragonprobe
