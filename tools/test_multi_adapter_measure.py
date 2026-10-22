#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only

import importlib.util
import json
from pathlib import Path
import sys
import tempfile


sys.dont_write_bytecode = True
SCRIPT = Path(__file__).with_name("multi_adapter_measure.py")
SPEC = importlib.util.spec_from_file_location("multi_adapter_measure", SCRIPT)
measure = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(measure)


def test_parse_stats():
    stats = measure.parse_stats("""usb_interface_path: /sys/bus/usb/devices/1-2:1.0
usb_id: 534d:6021
input_updates: 42
present_backpressured: yes
cursor_state: enabled 32x32 at 1,2
""")
    assert stats["usb_interface_path"] == "/sys/bus/usb/devices/1-2:1.0"
    assert stats["usb_id"] == "534d:6021"
    assert stats["input_updates"] == 42
    assert stats["present_backpressured"] is True
    assert stats["cursor_state"] == "enabled 32x32 at 1,2"


def test_topology_and_discovery():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        sysfs = root / "sys"
        debugfs = root / "debugfs"
        device = sysfs / "devices/pci0000:00/usb1/1-2"
        device.mkdir(parents=True)
        for name, value in (("busnum", "1"), ("devpath", "2.1"),
                            ("speed", "480")):
            (device / name).write_text(value)
        usb_links = sysfs / "bus/usb/devices"
        usb_links.mkdir(parents=True)
        (usb_links / "1-2").symlink_to(device)
        (usb_links / "1-2:1.0").symlink_to(device)
        stats_file = debugfs / "card3/ms912x_stats"
        stats_file.parent.mkdir(parents=True)
        stats_file.write_text("""usb_device_path: /sys/bus/usb/devices/1-2
usb_interface_path: /sys/bus/usb/devices/1-2:1.0
usb_id: 534d:6021
usb_link_speed: high-speed
input_updates: 3
""")

        adapters = measure.discover_adapters(debugfs, sysfs)
        assert len(adapters) == 1
        adapter = adapters[0]
        assert adapter["adapter_id"] == "/sys/bus/usb/devices/1-2:1.0"
        assert adapter["drm_debugfs_dir"] == "card3"
        assert adapter["usb_topology"]["root_hub"] == "usb1"
        assert adapter["usb_topology"]["root_port"] == "1-2"
        assert adapter["usb_topology"]["speed"] == "480"


def test_interval_summary_and_counter_reset():
    def observation(elapsed, updates, pixels, scan_ns, bytes_sent, pressure,
                    backpressured):
        return {
            "elapsed_s": elapsed,
            "adapters": [{
                "adapter_id": "usb-interface-A",
                "identity": {"usb_id": "534d:6021"},
                "usb_topology": {"root_port": "1-2"},
                "stats": {
                    "input_updates": updates,
                    "scan_frames": updates,
                    "scan_compared_pixels": pixels,
                    "scan_total_duration_ns": scan_ns,
                    "conversion_attempts": updates,
                    "conversion_successes": updates,
                    "conversion_pixels": pixels,
                    "conversion_total_duration_ns": scan_ns,
                    "usb_requests": updates,
                    "usb_successful_transfers": updates,
                    "usb_bytes": bytes_sent,
                    "usb_request_total_duration_ns": updates * 10_000_000,
                    "usb_errors": 0,
                    "usb_timeouts": 0,
                    "usb_short_transfers": 0,
                    "damage_replay_batches_queued": 0,
                    "damage_replay_batches_sent": 0,
                    "usb_requests_requeued_before_send": 0,
                    "present_wait_frames": updates,
                    "present_wait_total_duration_ns": updates * 1_000_000,
                    "present_wait_timeouts": 0,
                    "present_queue_pressure_ms": pressure,
                    "present_queue_pressure_ns": pressure * 1_000_000,
                    "present_queue_low_watermark_ns": 1_500_000,
                    "present_queue_high_watermark_ns": 3_000_000,
                    "present_backpressured": backpressured,
                    "pending_rect_count": updates,
                },
            }],
        }

    samples = [
        observation(0.0, 10, 100_000, 1_000_000, 1_000_000, 1, False),
        observation(1.0, 20, 300_000, 2_000_000, 3_000_000, 4, True),
        # A counter reset is excluded from the delta and reported explicitly.
        observation(2.0, 5, 400_000, 3_000_000, 4_000_000, 2, False),
    ]
    summary = measure.summarize_samples(samples)
    assert len(summary) == 1
    result = summary[0]
    assert result["counter_deltas"]["input_updates"] == 10
    assert result["counter_resets"]["input_updates"] == 1
    assert result["rates"]["input_updates_per_s"] == 10
    assert result["rates"]["usb_bytes_per_s"] == 1_500_000
    assert result["rates"]["scan_mpixels_per_s"] == 150
    assert result["average_latencies"]["usb_request_ms"] == 10
    assert result["queue_pressure_ms"] == {
        "samples": 3,
        "p50": 2,
        "p95": 4,
        "max": 4,
        "above_high_watermark_samples": 1,
        "below_low_watermark_samples": 1,
        "between_watermark_samples": 1,
        "watermark_samples": 3,
        "backpressured_sample_fraction": 1 / 3,
        "pending_rects_max": 20,
    }
    json.dumps(summary)


if __name__ == "__main__":
    test_parse_stats()
    test_topology_and_discovery()
    test_interval_summary_and_counter_reset()
    print("PASS multi-adapter measurement: parsing, topology, interval metrics and resets")
