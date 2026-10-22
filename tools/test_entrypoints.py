#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile small direct-call fixtures for the DRM-facing entry points."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", source)
    if match is None:
        raise ValueError(f"Function definition not found: {name}")
    start = source.rfind("\n", 0, match.start()) + 1
    brace = source.index("{", match.start())
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def compile_and_run(cc, flags, repo, build, name, source):
    binary = build / name
    subprocess.run([*shlex.split(cc), *flags, "-I" + str(repo / "tools"),
                    "-I" + str(build), "-I" + str(repo), str(repo / "tools" / source),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent

    transfer = (repo / "ms912x_transfer.c").read_text()
    damage = (repo / "ms912x_damage.c").read_text()
    kms = (repo / "ms912x_kms.c").read_text()
    params = (repo / "ms912x_params.c").read_text()
    connector = (repo / "ms912x_connector.c").read_text()
    usb_transfer = (repo / "ms912x_usb_transfer.c").read_text()
    with tempfile.TemporaryDirectory(prefix="ms912x-entrypoints-") as temporary:
        build = Path(temporary)
        (build / "scan-invalidate.h").write_text("".join(
            function(damage, name) for name in (
                "ms912x_damage_scan_invalidate_locked",
                "ms912x_damage_scan_invalidate")))
        (build / "submit-entry.h").write_text("".join(
            function(transfer, name) for name in (
                "ms912x_rect_vector_grow",
                "ms912x_rect_map_output_to_input",
                "ms912x_rect_align_x4",
                "ms912x_capture_pixel_rows",
                "ms912x_snapshot_capture_damage",
                "ms912x_submit_begin_frame_locked",
                "ms912x_submit_collect_damage_locked",
                "ms912x_submit_capture_damage",
                "ms912x_submit_publish_damage_locked",
                "ms912x_submit_end_cpu_access",
                "ms912x_submit_wait_for_present",
                "ms912x_framebuffer_submit")))
        (build / "baseline-copy.h").write_text(
            function(damage, "ms912x_damage_scan_copy_snapshot"))
        (build / "scan-entry.h").write_text(
            function(transfer, "ms912x_rect_vector_grow") + "".join(
                function(damage, name) for name in (
                    "ms912x_damage_scan_resolve_config_locked",
                    "ms912x_damage_scan_prepare_full_update",
                    "ms912x_damage_scan_prepare_baseline",
                    "ms912x_damage_scan_commit_locked",
                    "ms912x_damage_scan_find_snapshot_changes")))
        (build / "scan-worker.h").write_text("".join(
            function(transfer, name) for name in (
                "ms912x_scan_is_pending",
                "ms912x_submit_begin_frame_locked",
                "ms912x_scan_has_older_filling",
                "ms912x_scan_publish_frame_locked",
                "ms912x_scan_frame_worker")))
        (build / "scan-mode.h").write_text(
            function(kms, "ms912x_software_scan_enabled"))
        (build / "scan-queue.h").write_text("".join(
            function(transfer, name) for name in (
                "ms912x_capture_pixel_rows",
                "ms912x_scan_has_older_filling",
                "ms912x_scan_capture_pending",
                "ms912x_scan_enqueue_frame_locked")))
        (build / "registry.h").write_text("".join(
            function(params, name) for name in (
                "ms912x_devices_snapshot",
                "ms912x_notify_devices_hotplug",
                "ms912x_device_registry_add",
                "ms912x_device_registry_remove")))
        (build / "edid-invalidate.h").write_text(
            function(connector, "ms912x_edid_cache_invalidate"))
        (build / "failed-replay.h").write_text("".join(
            function(usb_transfer, name) for name in (
                "ms912x_usb_carry_failed_rect_locked",
                "ms912x_usb_request_covers_rect")))
        (build / "scan-config.h").write_text("".join(
            function(damage, name) for name in (
                "ms912x_damage_scan_invalidate_locked",
                "ms912x_damage_scan_resolve_config_locked")))
        flags = ["-std=gnu11", "-O2", "-g", "-fno-strict-aliasing", "-Wall", "-Wextra", "-Werror", "-pthread",
                 "-Wno-unused-parameter", "-Wno-sign-compare"]
        linux = build / "linux"
        linux.mkdir()
        for header in ("atomic.h", "jump_label.h", "types.h", "ktime.h"):
            (linux / header).write_text('#include "test_stats_compat.h"\n')
        compile_and_run(args.cc, flags, repo, build, "submit-entry",
                        "test_submit_entry.c")
        compile_and_run(args.cc, flags, repo, build, "scan-entry",
                        "test_scan_entry.c")
        compile_and_run(args.cc, flags, repo, build, "scan-queue",
                        "test_scan_queue.c")
        compile_and_run(args.cc, flags, repo, build, "registry",
                        "test_registry.c")
        compile_and_run(args.cc, flags, repo, build, "scan-config",
                        "test_scan_config.c")
        compile_and_run(args.cc, flags, repo, build, "replay-isolation",
                        "test_replay_isolation.c")
        stats_binary = build / "stats-isolation"
        subprocess.run([*shlex.split(args.cc), *flags,
                        "-include", str(repo / "tools" / "test_stats_compat.h"),
                        "-I" + str(repo / "tools"), "-I" + str(build),
                        "-I" + str(repo), str(repo / "tools" / "test_stats.c"),
                        str(repo / "ms912x_stats.c"), "-o", str(stats_binary)],
                       check=True)
        subprocess.run([str(stats_binary)], check=True)


if __name__ == "__main__":
    main()
