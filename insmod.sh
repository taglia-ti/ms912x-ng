#!/bin/sh
set -eu

make clean
make all -j
sudo -A make install-udev
sudo -A udevadm control --reload-rules
if [ -d /sys/module/ms912x ]; then
	sudo -A rmmod ms912x
fi
sudo -A insmod ms912x.ko
