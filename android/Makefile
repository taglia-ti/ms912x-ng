ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
ANDROID_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
GRADLEW := ./gradlew
GRADLE_ARGS ?= --console=plain
MIRROR_ONLY ?= false
DEVICE_SERIAL ?=
ANDROID_SDK_DIR := $(if $(strip $(ANDROID_HOME)),$(ANDROID_HOME),$(ANDROID_SDK_ROOT))
CARGO ?= cargo

ifeq ($(MIRROR_ONLY),true)
GRADLE_MODE_ARGS := -PmirrorOnly=true
DEVICE_MODE_ARGS := --mirror-only
UNIT_TEST_TASKS := :app:testDebugUnitTest
else
UNIT_TEST_TASKS := :app:testDebugUnitTest :helper:testDebugUnitTest :ipc:testDebugUnitTest
endif

.PHONY: all app build assemble lint tests host-tests unit-tests instrumented-tests \
	install clean

all: app

app build:
	$(GRADLEW) $(GRADLE_ARGS) $(GRADLE_MODE_ARGS) :app:assembleDebug

assemble:
	$(GRADLEW) $(GRADLE_ARGS) $(GRADLE_MODE_ARGS) :app:assembleDebug :app:assembleDebugAndroidTest

lint:
	$(GRADLEW) $(GRADLE_ARGS) $(GRADLE_MODE_ARGS) :app:lintDebug

unit-tests:
	$(GRADLEW) $(GRADLE_ARGS) $(GRADLE_MODE_ARGS) $(UNIT_TEST_TASKS)

host-tests: unit-tests
	"$(ROOT_DIR)/tools/android/test_frame_pipeline.sh"
	bash "$(ROOT_DIR)/tools/android/test_mode_catalog.sh"
	bash "$(ROOT_DIR)/tools/android/test_ui_state.sh"
ifneq ($(MIRROR_ONLY),true)
	bash "$(ROOT_DIR)/tools/android/test_helper_control.sh"
	bash "$(ROOT_DIR)/tools/android/test_mouse_sharing_policy.sh"
	CARGO_TARGET_DIR="$(ANDROID_DIR)/build/rustHostTests" "$(CARGO)" test --locked --offline --manifest-path "$(ANDROID_DIR)/adb-rs/Cargo.toml"
endif

tests: host-tests

instrumented-tests:
	@test -n "$(DEVICE_SERIAL)" || { echo 'Set DEVICE_SERIAL to an emulator or device serial.' >&2; exit 2; }
	ANDROID_HOME="$(ANDROID_SDK_DIR)" ANDROID_SDK_ROOT="$(ANDROID_SDK_DIR)" \
		"$(ROOT_DIR)/tools/android/test_android.sh" $(DEVICE_MODE_ARGS) --device-only "$(DEVICE_SERIAL)"

install: app
	@test -n "$(DEVICE_SERIAL)" || { echo 'Set DEVICE_SERIAL to an emulator or device serial.' >&2; exit 2; }
	@test -n "$(ANDROID_SDK_DIR)" || { echo 'Set ANDROID_HOME or ANDROID_SDK_ROOT to the Android SDK.' >&2; exit 2; }
	"$(ANDROID_SDK_DIR)/platform-tools/adb" -s "$(DEVICE_SERIAL)" install -r "$(ANDROID_DIR)/app/build/outputs/apk/debug/app-debug.apk"

clean:
	$(GRADLEW) $(GRADLE_ARGS) clean
