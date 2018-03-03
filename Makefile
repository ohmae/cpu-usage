## @file Makefile
#
# Copyright (c) 2016 大前良介 (OHMAE Ryosuke)
#
# This software is released under the MIT License.
# http://opensource.org/licenses/MIT
#
# @breaf
# @author <a href="mailto:ryo@mm2d.net">大前良介 (OHMAE Ryosuke)</a>
# @date 2016/3/21

CC   = gcc
RM   = rm -rf
MAKE = make

CFLAGS = -Wall -g3 -O2
COPTS  = -D_DEBUG_
LDFLAGS =
# MODULES = $(patsubst %.c,%,$(wildcard *.c))
MODULES = cpus cpu cpup cput

.PHONY: all clean
all: $(MODULES)

clean:
	$(RM) $(MODULES)

%:%.c
	$(CC) $(CFLAGS) $(COPTS) $(LDFLAGS) $< -o $@
