#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_output=$(mktemp -d)
trap 'rm -rf -- "$test_output"' EXIT
javac --release 11 -d "$test_output" \
    "$repo_root/android/app/src/main/java/io/github/taglia_ti/ms912x_ng/desktop/AdbCredentialAvailability.java" \
    "$repo_root/tools/android/AdbCredentialAvailabilityTest.java"
java -cp "$test_output" io.github.taglia_ti.ms912x_ng.desktop.AdbCredentialAvailabilityTest
