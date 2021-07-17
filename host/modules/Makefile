
obj-m  := dmj.o dmj-char.o i2c-dmj.o spi-dmj.o dmj-hwmon.o
KDIR   := /lib/modules/$(shell uname -r)/build
PWD    := $(shell pwd)

CFLAGS += -Wall -Wpedantic

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(RM) -v *.o *.ko *.mod *.mod.c Module.symvers modules.order

load:
	sudo insmod ./dmj.ko
	sudo insmod ./dmj-char.ko
	sudo insmod ./i2c-dmj.ko
	sudo insmod ./spi-dmj.ko
	sudo insmod ./dmj-hwmon.ko

unload:
	sudo rmmod dmj-hwmon
	sudo rmmod spi-dmj
	sudo rmmod i2c-dmj
	sudo rmmod dmj-char
	sudo rmmod dmj
