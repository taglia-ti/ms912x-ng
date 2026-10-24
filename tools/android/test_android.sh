#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mirror_only=false
mode_args=()
if [[ "${1:-}" == --mirror-only ]]; then
    mirror_only=true; mode_args=(-PmirrorOnly=true); shift
fi
device_only=false
if [[ "${1:-}" == --device-only ]]; then
    device_only=true; shift
    [[ $# == 1 ]] || { printf '%s\n' 'Usage: test_android.sh --device-only DEVICE_SERIAL' >&2; exit 2; }
fi
if ! $device_only; then
    "$repo_root/tools/android/test_rgbx_conversion.sh"
    "$repo_root/tools/android/test_frame_pipeline.sh"
    bash "$repo_root/tools/android/test_mode_catalog.sh"
    if ! $mirror_only; then
        bash "$repo_root/tools/android/test_helper_control.sh"
        CARGO_TARGET_DIR="$repo_root/android/app/build/rustHostTests" \
            "${CARGO:-cargo}" test --locked --offline --manifest-path "$repo_root/android/adb-rs/Cargo.toml"
    fi
fi
if [[ $# == 0 ]]; then
    printf '%s\n' 'Host checks passed. Pass an Android device serial to build/install and run instrumented fixtures.'
    exit 0
fi
if [[ $# != 1 ]]; then printf '%s\n' 'Usage: test_android.sh [--device-only] [DEVICE_SERIAL]' >&2; exit 2; fi
sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
[[ -n "$sdk" ]] || { printf '%s\n' 'Set ANDROID_HOME to the installed SDK.' >&2; exit 2; }
export ANDROID_HOME="$sdk" ANDROID_SDK_ROOT="$sdk"
"$repo_root/android/gradlew" -p "$repo_root/android" \
    "${mode_args[@]}" :app:assembleDebug :app:assembleDebugAndroidTest :app:lintDebug
python3 - "$repo_root" "$sdk" "$1" "$mirror_only" <<'PY'
import hashlib, json, pathlib, re, subprocess, sys
root, sdk, serial, mirror_only = sys.argv[1:]
mirror_only = mirror_only == "true"
package = "io.github.ms912x.desktop" + (".mirror" if mirror_only else "")
root = pathlib.Path(root)
adb = [str(pathlib.Path(sdk)/'platform-tools/adb'), '-s', serial]
def call(*args): return subprocess.check_output(adb + list(args), text=True, timeout=180)
api = call('shell', 'getprop', 'ro.build.version.sdk').strip()
assert int(api) >= (24 if mirror_only else 30), "Android API below build minimum"
report = root/'android/app/build/reports/android-fixtures'/(('mirror-' if mirror_only else '')+'api'+api+'-'+re.sub(r'[^\w.-]', '_', serial))
report.mkdir(parents=True, exist_ok=True)
apk = root/'android/app/build/outputs/apk/debug/app-debug.apk'
print(call('install', '-r', str(apk)), end='')
print(call('install', '-r', str(root/'android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk')), end='')
output = call('shell', 'am', 'instrument', '-w', '-r',
    package+'.test/io.github.ms912x.desktop.CaptureNativeTestRunner')
(report/'instrumentation.txt').write_text(output)
print(output, end='')
expected_groups = 6 if mirror_only else 16
passed = ('INSTRUMENTATION_CODE: -1' in output and 'FAIL:' not in output
    and f'{expected_groups} groups passed; 0 failed' in output)
(report/'results.json').write_text(json.dumps({'api': int(api), 'serial': serial,
    'apk_sha256': hashlib.sha256(apk.read_bytes()).hexdigest(),
    'passed': passed, 'groups': expected_groups, 'package': package, 'limitation': 'USB responses are fixtures; physical presentation and real Wireless ADB pairing are not verified'}, indent=2)+'\n')
assert passed, 'Instrumented fixtures failed; see '+str(report)
PY
