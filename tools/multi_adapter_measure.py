#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Capture per-adapter USB display statistics for multi-adapter comparisons."""

import argparse
import datetime as dt
import json
import math
import os
from pathlib import Path
import platform
import re
import socket
import time


COUNTERS = (
    "input_updates",
    "scan_frames",
    "scan_compared_pixels",
    "scan_total_duration_ns",
    "conversion_attempts",
    "conversion_successes",
    "conversion_pixels",
    "conversion_total_duration_ns",
    "usb_requests",
    "usb_successful_transfers",
    "usb_bytes",
    "usb_request_total_duration_ns",
    "usb_errors",
    "usb_timeouts",
    "usb_short_transfers",
    "damage_replay_batches_queued",
    "damage_replay_batches_sent",
    "usb_requests_requeued_before_send",
    "present_wait_frames",
    "present_wait_total_duration_ns",
    "present_wait_timeouts",
)


def parse_stats(text):
    """Parse the debugfs key/value format, retaining nonnumeric identity fields."""
    values = {}
    for line in text.splitlines():
        key, separator, value = line.partition(":")
        if not separator:
            continue
        key = key.strip()
        value = value.strip()
        if not key:
            continue
        if re.fullmatch(r"-?\d+", value):
            values[key] = int(value)
        elif value in ("yes", "no"):
            values[key] = value == "yes"
        else:
            values[key] = value
    return values


def _sysfs_device_path(path_text, sysfs_root):
    if not path_text:
        return None
    path = Path(path_text)
    try:
        relative = path.relative_to("/sys")
    except ValueError:
        return None
    return Path(sysfs_root) / relative


def usb_topology(stats, sysfs_root="/sys"):
    """Return best-effort USB location data for the adapter's USB device."""
    device_path = _sysfs_device_path(stats.get("usb_device_path"), sysfs_root)
    if device_path is None:
        return {}
    try:
        resolved = device_path.resolve(strict=True)
    except OSError:
        resolved = device_path

    result = {"sysfs_realpath": str(resolved)}
    for name in ("busnum", "devpath", "speed"):
        try:
            result[name] = (device_path / name).read_text().strip()
        except OSError:
            continue

    busnum = result.get("busnum")
    devpath = result.get("devpath")
    if busnum and devpath:
        result["root_hub"] = f"usb{busnum}"
        first_port = devpath.split(".", 1)[0]
        result["root_port"] = (f"{busnum}-{first_port}"
                               if first_port != "0" else f"usb{busnum}-root")
    return result


def discover_adapters(debugfs_root="/sys/kernel/debug/dri", sysfs_root="/sys"):
    adapters = []
    for stats_path in sorted(Path(debugfs_root).glob("*/ms912x_stats")):
        try:
            stats = parse_stats(stats_path.read_text())
        except OSError as error:
            adapters.append({"debugfs_path": str(stats_path), "read_error": str(error)})
            continue
        interface = stats.get("usb_interface_path")
        device_path = stats.get("usb_device_path")
        adapter_id = interface or device_path or str(stats_path.parent)
        adapters.append({
            "adapter_id": adapter_id,
            "drm_debugfs_dir": stats_path.parent.name,
            "debugfs_path": str(stats_path),
            "identity": {
                key: stats[key] for key in (
                    "usb_device_path", "usb_interface_path", "usb_id",
                    "usb_serial", "usb_link_speed", "primary_plane_format",
                    "pixel_payload_format", "effective_profile", "scale_percent",
                ) if key in stats
            },
            "usb_topology": usb_topology(stats, sysfs_root),
            "stats": stats,
        })
    return adapters


def _percentile(values, percent):
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, math.ceil(percent * len(ordered)) - 1)
    return ordered[index]


def summarize_samples(samples):
    """Summarize interval deltas by USB interface, preserving reset events."""
    by_adapter = {}
    for sample in samples:
        for adapter in sample["adapters"]:
            if "stats" not in adapter:
                continue
            by_adapter.setdefault(adapter["adapter_id"], []).append(
                (sample["elapsed_s"], adapter))

    summaries = []
    for adapter_id, observations in sorted(by_adapter.items()):
        deltas = {key: 0 for key in COUNTERS}
        valid_seconds = {key: 0.0 for key in COUNTERS}
        reset_counts = {key: 0 for key in COUNTERS}
        interval_count = 0
        for (previous_time, previous), (current_time, current) in zip(
                observations, observations[1:]):
            elapsed = current_time - previous_time
            if elapsed <= 0:
                continue
            interval_count += 1
            for key in COUNTERS:
                old = previous["stats"].get(key)
                new = current["stats"].get(key)
                if not isinstance(old, int) or not isinstance(new, int):
                    continue
                if new < old:
                    reset_counts[key] += 1
                else:
                    deltas[key] += new - old
                    valid_seconds[key] += elapsed

        first_time, first = observations[0]
        last_time, last = observations[-1]
        duration = max(0.0, last_time - first_time)
        rates = {}
        if duration > 0:
            rates["input_updates_per_s"] = (
                deltas["input_updates"] / valid_seconds["input_updates"]
                if valid_seconds["input_updates"] else None)
            rates["usb_requests_per_s"] = (
                deltas["usb_requests"] / valid_seconds["usb_requests"]
                if valid_seconds["usb_requests"] else None)
            rates["usb_successful_transfers_per_s"] = (
                deltas["usb_successful_transfers"] /
                valid_seconds["usb_successful_transfers"]
                if valid_seconds["usb_successful_transfers"] else None)
            rates["usb_bytes_per_s"] = (
                deltas["usb_bytes"] / valid_seconds["usb_bytes"]
                if valid_seconds["usb_bytes"] else None)
            rates["usb_mib_per_s"] = (
                rates["usb_bytes_per_s"] / (1024 * 1024)
                if rates["usb_bytes_per_s"] is not None else None)
            rates["scan_mpixels_per_s"] = (
                deltas["scan_compared_pixels"] * 1000 /
                deltas["scan_total_duration_ns"]
                if deltas["scan_total_duration_ns"] else None)
            rates["conversion_mpixels_per_s"] = (
                deltas["conversion_pixels"] * 1000 /
                deltas["conversion_total_duration_ns"]
                if deltas["conversion_total_duration_ns"] else None)

        averages = {
            "usb_request_ms": (
                deltas["usb_request_total_duration_ns"] /
                deltas["usb_requests"] / 1e6
                if deltas["usb_requests"] else None),
            "scan_ms_per_frame": (
                deltas["scan_total_duration_ns"] /
                deltas["scan_frames"] / 1e6
                if deltas["scan_frames"] else None),
            "conversion_ms_per_attempt": (
                deltas["conversion_total_duration_ns"] /
                deltas["conversion_attempts"] / 1e6
                if deltas["conversion_attempts"] else None),
            "present_wait_ms_per_frame": (
                deltas["present_wait_total_duration_ns"] /
                deltas["present_wait_frames"] / 1e6
                if deltas["present_wait_frames"] else None),
        }
        pressure = []
        above_high = below_low = between_watermarks = 0
        watermark_samples = 0
        for _, adapter in observations:
            stats = adapter["stats"]
            pressure_ns = stats.get("present_queue_pressure_ns")
            if not isinstance(pressure_ns, int):
                pressure_ms = stats.get("present_queue_pressure_ms")
                pressure_ns = pressure_ms * 1_000_000 if isinstance(pressure_ms, int) else None
            if isinstance(pressure_ns, int):
                pressure.append(pressure_ns / 1_000_000)
            low_ns = stats.get("present_queue_low_watermark_ns")
            high_ns = stats.get("present_queue_high_watermark_ns")
            if (isinstance(pressure_ns, int) and isinstance(low_ns, int) and
                    isinstance(high_ns, int) and high_ns >= low_ns):
                watermark_samples += 1
                if pressure_ns > high_ns:
                    above_high += 1
                elif pressure_ns < low_ns:
                    below_low += 1
                else:
                    between_watermarks += 1
        backpressure_samples = [adapter["stats"].get("present_backpressured")
                                for _, adapter in observations]
        backpressure_samples = [value for value in backpressure_samples
                                if isinstance(value, bool)]
        pending = [adapter["stats"].get("pending_rect_count")
                   for _, adapter in observations]
        pending = [value for value in pending if isinstance(value, int)]
        summaries.append({
            "adapter_id": adapter_id,
            "identity": first["identity"],
            "usb_topology": first["usb_topology"],
            "duration_s": duration,
            "intervals": interval_count,
            "counter_deltas": deltas,
            "counter_resets": {key: value for key, value in reset_counts.items()
                                if value},
            "rates": rates,
            "average_latencies": averages,
            "queue_pressure_ms": {
                "samples": len(pressure),
                "p50": _percentile(pressure, 0.50),
                "p95": _percentile(pressure, 0.95),
                "max": max(pressure) if pressure else None,
                "above_high_watermark_samples": above_high,
                "below_low_watermark_samples": below_low,
                "between_watermark_samples": between_watermarks,
                "watermark_samples": watermark_samples,
                "backpressured_sample_fraction": (
                    sum(backpressure_samples) / len(backpressure_samples)
                    if backpressure_samples else None),
                "pending_rects_max": max(pending) if pending else None,
            },
        })
    return summaries


def _utc_now():
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")


def collect(debugfs_root, sysfs_root, duration_s, interval_s, scenario):
    started = time.monotonic()
    samples = []
    deadline = started
    while True:
        elapsed = time.monotonic() - started
        adapters = discover_adapters(debugfs_root, sysfs_root)
        samples.append({
            "timestamp_utc": _utc_now(),
            "elapsed_s": elapsed,
            "adapters": adapters,
        })
        if elapsed >= duration_s:
            break
        deadline += interval_s
        time.sleep(max(0.0, deadline - time.monotonic()))
    return {
        "schema_version": 1,
        "scenario": scenario,
        "host": {
            "hostname": socket.gethostname(),
            "kernel": platform.release(),
            "architecture": platform.machine(),
            "cpu_count": os.cpu_count(),
        },
        "started_utc": samples[0]["timestamp_utc"] if samples else _utc_now(),
        "duration_requested_s": duration_s,
        "interval_requested_s": interval_s,
        "debugfs_root": str(debugfs_root),
        "sysfs_root": str(sysfs_root),
        "samples": samples,
        "summary": summarize_samples(samples),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--debugfs-root", default="/sys/kernel/debug/dri")
    parser.add_argument("--sysfs-root", default="/sys")
    parser.add_argument("--duration", type=float, default=30.0,
                        help="capture duration in seconds (default: 30)")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="sample interval in seconds (default: 1)")
    parser.add_argument("--scenario", required=True,
                        help="workload/topology label for this capture")
    parser.add_argument("--output", required=True,
                        help="new JSON output path; existing files are not overwritten")
    args = parser.parse_args()
    if args.duration <= 0 or args.interval <= 0:
        parser.error("--duration and --interval must be positive")

    result = collect(args.debugfs_root, args.sysfs_root, args.duration,
                     args.interval, args.scenario)
    if not any("stats" in adapter
               for sample in result["samples"]
               for adapter in sample["adapters"]):
        parser.error("no readable ms912x_stats files found under --debugfs-root")

    output = Path(args.output)
    try:
        with output.open("x", encoding="utf-8") as stream:
            json.dump(result, stream, indent=2, sort_keys=True)
            stream.write("\n")
    except FileExistsError:
        parser.error(f"output already exists: {output}")

    print(f"Wrote {len(result['samples'])} samples to {output}")
    for adapter in result["summary"]:
        rate = adapter["rates"].get("usb_mib_per_s")
        pressure = adapter["queue_pressure_ms"]
        print(f"{adapter['adapter_id']}: "
              f"{rate:.2f} MiB/s" if rate is not None else
              f"{adapter['adapter_id']}: no USB byte delta")
        print(f"  queue pressure ms p50/p95/max: "
              f"{pressure['p50']}/{pressure['p95']}/{pressure['max']}; "
              f"backpressured sample fraction: "
              f"{pressure['backpressured_sample_fraction']}")


if __name__ == "__main__":
    main()
