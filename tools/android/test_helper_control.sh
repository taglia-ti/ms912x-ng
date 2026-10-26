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
javac --release 11 -d "$test_output" \
    "$repo_root/tools/android/TopologyPlacement.java" \
    "$repo_root/tools/android/TopologyPlacementTest.java" \
    "$repo_root/tools/android/TopologyLease.java" \
    "$repo_root/tools/android/TopologyLeaseTest.java"
java -cp "$test_output" io.github.ms912x.helper.TopologyPlacementTest
java -cp "$test_output" io.github.ms912x.helper.TopologyLeaseTest
javac --release 11 -d "$test_output" \
    "$helper_sources/MouseRouteGeometry.java" \
    "$repo_root/android/helper/src/test/java/io/github/ms912x/helper/MouseRouteGeometryTests.java"
java -cp "$test_output" io.github.ms912x.helper.MouseRouteGeometryTests
javac --release 11 -d "$test_output" \
    "$helper_sources/MouseReportQueue.java" "$helper_sources/MouseRouteGeometry.java" \
    "$repo_root/android/helper/src/test/java/io/github/ms912x/helper/MouseReportQueueTests.java"
java -cp "$test_output" io.github.ms912x.helper.MouseReportQueueTests
