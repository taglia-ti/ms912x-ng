ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

KVER ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KVER)/build
CHECK ?=

DKMS_PACKAGE_NAME ?= ms912x-ng
DKMS_PACKAGE_VERSION ?= $(shell \
	root='$(ROOT_DIR)'; \
	if git -C "$$root" rev-parse --verify HEAD >/dev/null 2>&1; then \
		revision=$$(git -C "$$root" rev-list --count HEAD); \
		commit=$$(git -C "$$root" rev-parse --short=7 HEAD); \
		version="r$$revision.$$commit"; \
		if test -n "$$(git -C "$$root" status --porcelain)"; then version="$${version}.dirty"; fi; \
		printf '%s' "$$version"; \
	else cat "$$root/VERSION"; fi)
DKMS_OUTPUT ?= $(ROOT_DIR)/build/dkms
DKMS_ARCHIVE := $(DKMS_OUTPUT)/$(DKMS_PACKAGE_NAME)-$(DKMS_PACKAGE_VERSION).tar.gz

.PHONY: all module modules clean dkms-package

# This Makefile is deliberately limited to the external kernel module and its
# DKMS source package. The repository root owns Android and project-wide tests.
all: module

module modules:
	$(MAKE) -C "$(KSRC)" M="$(ROOT_DIR)" KVER="$(KVER)" CHECK="$(CHECK)" modules

clean:
	@if test -f "$(KSRC)/Makefile"; then \
		$(MAKE) -C "$(KSRC)" M="$(ROOT_DIR)" KVER="$(KVER)" clean; \
	else \
		printf 'Kernel build tree not found at %s; removing module artifacts directly.\n' "$(KSRC)"; \
	fi
	@find "$(ROOT_DIR)" -maxdepth 1 -type f \( -name '*.o' -o -name '*.cmd' -o -name '*.ko' \
		-o -name '*.mod' -o -name '*.mod.c' -o -name '*.ur-safe' \) -delete
	@find "$(ROOT_DIR)/linux" "$(ROOT_DIR)/shared" -type f \( -name '*.o' -o -name '*.cmd' \
		-o -name '*.ko' -o -name '*.mod' -o -name '*.mod.c' -o -name '*.ur-safe' \) -delete
	@rm -rf "$(ROOT_DIR)/.tmp_versions" "$(ROOT_DIR)/linux/.tmp_versions" "$(ROOT_DIR)/shared/.tmp_versions"
	@rm -f "$(ROOT_DIR)/Module.symvers" "$(ROOT_DIR)/modules.order"

dkms-package:
	@set -eu; \
	package_dir='$(DKMS_PACKAGE_NAME)-$(DKMS_PACKAGE_VERSION)'; \
	mkdir -p "$(DKMS_OUTPUT)"; \
	temporary_dir=$$(mktemp -d "$(DKMS_OUTPUT)/.dkms-source.XXXXXX"); \
	trap 'rm -rf "$$temporary_dir"' EXIT; \
	mkdir -p "$$temporary_dir/$$package_dir"; \
	(cd "$(ROOT_DIR)" && tar \
		--exclude='*.o' --exclude='*.cmd' --exclude='*.ko' --exclude='*.mod' \
		--exclude='*.mod.c' --exclude='*.ur-safe' --exclude='Module.symvers' \
		--exclude='modules.order' --exclude='.tmp_versions' \
		-cf - Makefile dkms.conf VERSION linux shared ms912x_*.c ms912x_*.h) \
		| tar -xf - -C "$$temporary_dir/$$package_dir"; \
	printf '%s\n' '$(DKMS_PACKAGE_VERSION)' > "$$temporary_dir/$$package_dir/VERSION"; \
	tar -czf "$(DKMS_ARCHIVE)" -C "$$temporary_dir" "$$package_dir"; \
	printf 'Created %s\n' "$(DKMS_ARCHIVE)"
