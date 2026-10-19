ms912x-y := \
	ms912x_registers.o \
	ms912x_connector.o \
	ms912x_debugfs.o \
	ms912x_convert.o \
	ms912x_damage.o \
	ms912x_kms.o \
	ms912x_params.o \
	ms912x_scan_methods.o \
	ms912x_stats.o \
	ms912x_transfer.o \
	ms912x_usb_transfer.o \
	ms912x_drv.o
ms912x-$(CONFIG_X86_64) += ms912x_sse2.o
ms912x-$(CONFIG_X86_64) += ms912x_ssse3.o
ms912x-$(CONFIG_X86_64) += ms912x_avx2.o
ms912x-$(CONFIG_X86_64) += ms912x_scan_avx2.o
ms912x-$(CONFIG_X86_64) += ms912x_scan_sse2.o
ms912x-$(CONFIG_X86_64) += ms912x_scan_sse41.o
ms912x-$(CONFIG_ARM64) += ms912x_neon.o
ms912x-$(CONFIG_ARM64) += ms912x_scan_neon.o
ms912x-$(CONFIG_ARM64) += ms912x_neon_glue.o
ifeq ($(CONFIG_ARM),y)
ms912x-$(CONFIG_KERNEL_MODE_NEON) += ms912x_neon.o ms912x_scan_neon.o ms912x_neon_glue.o
endif

CFLAGS_ms912x_sse2.o += -msse2 -mno-avx -fno-tree-vectorize \
	-isystem $(shell $(CC) -print-file-name=include)
CFLAGS_ms912x_ssse3.o += -mssse3 -mno-avx -fno-tree-vectorize \
	-isystem $(shell $(CC) -print-file-name=include)
CFLAGS_ms912x_avx2.o += -mavx2 -mno-avx512f -fno-tree-vectorize \
	-isystem $(shell $(CC) -print-file-name=include)
CFLAGS_ms912x_scan_avx2.o += -mavx2 -mno-avx512f -fno-tree-vectorize \
	-isystem $(shell $(CC) -print-file-name=include)
CFLAGS_ms912x_scan_sse2.o += -msse2 -mno-avx -fno-tree-vectorize \
	-isystem $(shell $(CC) -print-file-name=include)
CFLAGS_ms912x_scan_sse41.o += -msse4.1 -mno-avx -fno-tree-vectorize \
	-isystem $(shell $(CC) -print-file-name=include)
ifeq ($(CONFIG_ARM64),y)
ms912x-neon-flags := -march=armv8-a+simd
ms912x-neon-remove := -mgeneral-regs-only
else ifeq ($(CONFIG_ARM),y)
ms912x-neon-flags := -march=armv7-a -mfpu=neon -mfloat-abi=softfp
ms912x-neon-remove := -msoft-float
endif
CFLAGS_REMOVE_ms912x_neon.o += $(ms912x-neon-remove)
CFLAGS_REMOVE_ms912x_scan_neon.o += $(ms912x-neon-remove)
CFLAGS_ms912x_neon.o += $(ms912x-neon-flags) -ffreestanding -fno-tree-vectorize -fno-lto \
	-isystem $(shell $(CC) -print-file-name=include)
CFLAGS_ms912x_scan_neon.o += $(ms912x-neon-flags) -ffreestanding -fno-tree-vectorize -fno-lto \
	-isystem $(shell $(CC) -print-file-name=include)
# Keep the entry/exit code integer-only even when the kernel enables LTO.
CFLAGS_ms912x_neon_glue.o += -fno-lto

obj-m := ms912x.o

KVER ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KVER)/build
MS912X_SOURCE_DIR ?= $(if $(M),$(M),$(CURDIR))
MS912X_SOURCE_VERSION := $(shell { git -C "$(MS912X_SOURCE_DIR)" describe --tags --always --dirty 2>/dev/null || cat "$(MS912X_SOURCE_DIR)/VERSION" 2>/dev/null || echo unknown; } | sed -e 's/^v//' -e 's/[^[:alnum:].+_-]/./g')
MS912X_BUILD_VERSION := $(shell printf '%s+%s\n' '$(MS912X_SOURCE_VERSION)' '$(KVER)' | sed 's/[^[:alnum:].+_-]/_/g')

ccflags-y += -DMS912X_BUILD_VERSION=\"$(MS912X_BUILD_VERSION)\"

PYTHON ?= python3
SIMD_ARGS ?=

.PHONY: all modules clean tests benchmark

all:	modules

modules:
	$(MAKE) CHECK="/usr/bin/sparse" -C $(KSRC) M=$(PWD) KVER=$(KVER) modules

tests:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/simd_bench/run.py" $(SIMD_ARGS)

benchmark:
	$(PYTHON) "$(MS912X_SOURCE_DIR)/tools/simd_bench/run.py" --bench $(SIMD_ARGS)

clean:
	$(MAKE) -C $(KSRC) M=$(PWD) KVER=$(KVER) clean
	rm -f $(PWD)/Module.symvers $(PWD)/*.ur-safe
