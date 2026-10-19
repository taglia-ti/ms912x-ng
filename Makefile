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
CFLAGS_REMOVE_ms912x_neon.o += -mgeneral-regs-only
CFLAGS_ms912x_neon.o += -march=armv8-a+simd -fno-tree-vectorize
CFLAGS_REMOVE_ms912x_scan_neon.o += -mgeneral-regs-only
CFLAGS_ms912x_scan_neon.o += -march=armv8-a+simd -fno-tree-vectorize

obj-m := ms912x.o

KVER ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KVER)/build
MS912X_SOURCE_DIR ?= $(if $(M),$(M),$(CURDIR))
MS912X_SOURCE_VERSION := $(shell { git -C "$(MS912X_SOURCE_DIR)" describe --tags --always --dirty 2>/dev/null || cat "$(MS912X_SOURCE_DIR)/VERSION" 2>/dev/null || echo unknown; } | sed -e 's/^v//' -e 's/[^[:alnum:].+_-]/./g')
MS912X_BUILD_VERSION := $(shell printf '%s+%s\n' '$(MS912X_SOURCE_VERSION)' '$(KVER)' | sed 's/[^[:alnum:].+_-]/_/g')

ccflags-y += -DMS912X_BUILD_VERSION=\"$(MS912X_BUILD_VERSION)\"

all:	modules

modules:
	$(MAKE) CHECK="/usr/bin/sparse" -C $(KSRC) M=$(PWD) KVER=$(KVER) modules

clean:
	$(MAKE) -C $(KSRC) M=$(PWD) KVER=$(KVER) clean
	rm -f $(PWD)/Module.symvers $(PWD)/*.ur-safe
