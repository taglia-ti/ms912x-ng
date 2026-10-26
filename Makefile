MS912X_SOURCE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
MS912X_SOURCE_VERSION := $(shell { git -C "$(MS912X_SOURCE_DIR)" describe --tags --always --dirty 2>/dev/null || cat "$(MS912X_SOURCE_DIR)/VERSION" 2>/dev/null || echo unknown; } | sed -e 's/^v//' -e 's/[^[:alnum:].+_-]/./g')
MS912X_BUILD_VERSION := $(shell printf '%s+%s\n' '$(MS912X_SOURCE_VERSION)' '$(KVER)' | sed 's/[^[:alnum:].+_-]/_/g')

# Kbuild enters this file when the repository root is used as M= for the
# external module. Keep the module object list in linux/Kbuild.
ifneq ($(KBUILD_EXTMOD),)
include $(src)/linux/Kbuild
else

PYTHON ?= python3
CC ?= cc
KVER ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KVER)/build
SIMD_ARGS ?=
DOWNSCALE_ARGS ?=
PREFIX ?= /usr
UDEV_RULES_DIR ?= $(PREFIX)/lib/udev/rules.d

LINUX_TESTS := test-rgbx test-uyvy test-batching test-damage test-cursor \
	test-downscale-5to4 test-entrypoints test-multi-adapter-measure

.PHONY: all help android app linux module modules clean clean-android clean-linux \
	tests linux-tests android-tests instrumented-tests android-test-device dkms-package \
	$(LINUX_TESTS) generate-fake-edid test-capabilities benchmark \
	install-udev test-udev

# Preserve the historical default: build the Linux module for the running kernel.
all: linux

help:
	@printf '%s\n' \
		'make android          Build the Android debug APK' \
		'make linux            Build the module for KVER (default: running kernel)' \
		'make tests            Run Linux and Android host test suites' \
		'make android-test-device DEVICE_SERIAL=...  Run Android instrumentation tests' \
		'make dkms-package     Create a DKMS source tarball under build/dkms/' \
		'make clean            Clean Linux and Android build outputs'

android app:
	$(MAKE) -C android app

linux module modules:
	$(MAKE) -C linux module KVER="$(KVER)" KSRC="$(KSRC)"

clean: clean-linux clean-android

clean-linux:
	$(MAKE) -C linux clean KVER="$(KVER)" KSRC="$(KSRC)"

clean-android:
	$(MAKE) -C android clean

dkms-package:
	$(MAKE) -C linux dkms-package

android-tests:
	$(MAKE) -C android tests

instrumented-tests:
	$(MAKE) -C android instrumented-tests DEVICE_SERIAL="$(DEVICE_SERIAL)" MIRROR_ONLY="$(MIRROR_ONLY)"

android-test-device: instrumented-tests

linux-tests: $(LINUX_TESTS)

tests: linux-tests android-tests
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/simd_bench/run.py" $(SIMD_ARGS)

install-udev:
	install -Dm644 "$(MS912X_SOURCE_DIR)/linux/udev/60-ms912x.rules" "$(DESTDIR)$(UDEV_RULES_DIR)/60-ms912x.rules"

test-udev:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/test_udev.py" --cc "$(CC)"

test-uyvy:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/test_uyvy_pipeline.py" --cc "$(CC)"

test-rgbx:
	CC="$(CC)" "$(MS912X_SOURCE_DIR)/tools/test_rgbx.sh"

test-damage:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/test_damage.py" --cc "$(CC)"

test-cursor:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/test_cursor.py" --cc "$(CC)"

test-batching:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/test_batching.py" --cc "$(CC)"

test-downscale-5to4:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/test_downscale_5to4.py" $(DOWNSCALE_ARGS)

test-entrypoints:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/test_entrypoints.py" --cc "$(CC)"

test-multi-adapter-measure:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/test_multi_adapter_measure.py"

generate-fake-edid:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/gen_fake_edid.py"

test-capabilities:
	@set -e; tmp=$$(mktemp); edid=$$(mktemp); \
	trap 'rm -f "$$tmp" "$$edid"' EXIT; \
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/gen_fake_edid.py" --check; \
	$(CC) -std=c11 -Wall -Wextra -Werror -I"$(MS912X_SOURCE_DIR)" -I"$(MS912X_SOURCE_DIR)/linux" \
		"$(MS912X_SOURCE_DIR)/tools/test_capabilities.c" -o "$$tmp"; \
	"$$tmp" "$(MS912X_SOURCE_DIR)/ms912x_mode_catalog.h" "$$edid"; \
	if command -v edid-decode >/dev/null 2>&1; then \
		edid-decode "$$edid"; \
	fi

benchmark:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/test_damage.py" --cc "$(CC)" --benchmark
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/test_downscale_5to4.py" --benchmark $(DOWNSCALE_ARGS)
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/simd_bench/run.py" --bench $(SIMD_ARGS)

endif
