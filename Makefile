#
# Makefile of v4l2-sample
#

TARGET = v4l2-sample

#CROSS_COMPILE ?= arm-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc
#CFLAGS = -Wall -Wextra -O3
CFLAGS = -g
LDLIBS = -lnetpbm

cc-option = $(shell if $(CC) $(CFLAGS) $(1) -S -o /dev/null -xc /dev/null \
		    > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi ;)
extra_CFLAGS :=
#extra_CFLAGS += $(call cc-option,-mcpu=cortex-a9)
#extra_CFLAGS += $(call cc-option,-march=armv7-a)
#extra_CFLAGS += $(call cc-option,-mfpu=neon)

all: $(TARGET)

v4l2-sample: camera2ppm.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(extra_CFLAGS) -c -o $@ $<

clean:
	$(RM) *~ *.o $(TARGET)

romfs: $(TARGET)
	$(ROMFSINST) /usr/bin/v4l2-sample
	$(ROMFSINST) /usr/$(CROSS_COMPILE:-=)/lib/libnetpbm.so /lib/libnetpbm.so
