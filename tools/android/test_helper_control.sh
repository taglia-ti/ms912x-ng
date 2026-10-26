#!/usr/bin/env bash
set -euo pipefail
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
test_output=$(mktemp -d)
trap 'rm -rf -- "$test_output"' EXIT
python3 -B "$repo_root/tools/android/test_helper_dump_parser.py"
helper_sources="$repo_root/android/helper/src/main/java/io/github/taglia_ti/ms912x_ng/helper"
javac --release 11 -d "$test_output" \
    "$helper_sources/BootstrapEnvelope.java" "$repo_root/android/helper/src/test/java/io/github/taglia_ti/ms912x_ng/helper/BootstrapEnvelopeTests.java" \
    "$helper_sources/DisplayGeometry.java" "$repo_root/android/helper/src/test/java/io/github/taglia_ti/ms912x_ng/helper/DisplayGeometryTests.java" \
    "$helper_sources/CaptureRequests.java" "$helper_sources/ControlQueue.java" "$helper_sources/HelperLifetime.java" \
    "$repo_root/android/helper/src/test/java/io/github/taglia_ti/ms912x_ng/helper/HelperControlTests.java"
java -cp "$test_output" io.github.taglia_ti.ms912x_ng.helper.HelperControlTests
java -cp "$test_output" io.github.taglia_ti.ms912x_ng.helper.BootstrapEnvelopeTests
java -cp "$test_output" io.github.taglia_ti.ms912x_ng.helper.DisplayGeometryTests
javac --release 11 -d "$test_output" \
    "$repo_root/tools/android/TopologyPlacement.java" \
    "$repo_root/tools/android/TopologyPlacementTest.java" \
    "$repo_root/tools/android/TopologyLease.java" \
    "$repo_root/tools/android/TopologyLeaseTest.java"
java -cp "$test_output" io.github.taglia_ti.ms912x_ng.helper.TopologyPlacementTest
java -cp "$test_output" io.github.taglia_ti.ms912x_ng.helper.TopologyLeaseTest
javac --release 11 -d "$test_output" \
    "$helper_sources/MouseRouteGeometry.java" \
    "$repo_root/android/helper/src/test/java/io/github/taglia_ti/ms912x_ng/helper/MouseRouteGeometryTests.java"
java -cp "$test_output" io.github.taglia_ti.ms912x_ng.helper.MouseRouteGeometryTests
javac --release 11 -d "$test_output" \
    "$helper_sources/MouseReportQueue.java" "$helper_sources/MouseRouteGeometry.java" \
    "$repo_root/android/helper/src/test/java/io/github/taglia_ti/ms912x_ng/helper/MouseReportQueueTests.java"
java -cp "$test_output" io.github.taglia_ti.ms912x_ng.helper.MouseReportQueueTests
