MODNAME ?= drop-tcp-sock

obj-m = $(MODNAME).o
$(MODNAME)-y = main.o

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$$PWD
clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$$PWD clean
