#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_output=$(mktemp -d)
trap 'rm -rf -- "$test_output"' EXIT
python3 -B "$repo_root/tools/android/test_helper_dump_parser.py"
helper_sources="$repo_root/android/helper/src/main/java/io/github/ms912x/helper"
javac --release 11 -d "$test_output" \
    "$helper_sources/BootstrapEnvelope.java" "$repo_root/android/helper/src/test/java/io/github/ms912x/helper/BootstrapEnvelopeTests.java" \
    "$helper_sources/DisplayGeometry.java" "$repo_root/android/helper/src/test/java/io/github/ms912x/helper/DisplayGeometryTests.java" \
    "$helper_sources/CaptureRequests.java" "$helper_sources/ControlQueue.java" "$helper_sources/HelperLifetime.java" \
    "$repo_root/android/helper/src/test/java/io/github/ms912x/helper/HelperControlTests.java"
java -cp "$test_output" io.github.ms912x.helper.HelperControlTests
java -cp "$test_output" io.github.ms912x.helper.BootstrapEnvelopeTests
java -cp "$test_output" io.github.ms912x.helper.DisplayGeometryTests
