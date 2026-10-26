#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run production scan coverage, scan-index and queued-payload regression tests."""
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
    start = match.start()
    start = source.rfind("\n", 0, start) + 1
    # Include the return type on the previous line for multiline declarations.
    if source[start:start + 7] == "ms912x_":
        start = source.rfind("\n", 0, start - 1) + 1
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--benchmark", action="store_true")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="ms912x-damage-") as temporary:
        build = Path(temporary)
        # Share basic DRM/type shims with the batch planner fixture.
        base = (repo / "tools/test_rect_compat.h").read_text()
        (build / "compat.h").write_text(base + (repo / "tools/test_damage_compat.h").read_text())
        for name in ("linux/types.h", "linux/kernel.h", "linux/minmax.h", "linux/sort.h", "linux/slab.h",
                     "linux/string.h", "drm/drm_rect.h"):
            path = build / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "compat.h"\n')
        source = (repo / 'linux/ms912x_transfer.c').read_text()
        (build / "rect-grow.h").write_text(function(source, "ms912x_rect_vector_grow"))
        (build / "stage.h").write_text("".join(function(source, name) for name in (
            "ms912x_rect_valid_for_frame", "ms912x_snapshot_stage_conversion",
            "ms912x_rect_list_transfer_len")))
        names = ("ms912x_present_queue_bytes_locked",
                 "ms912x_cursor_snapshot_request_locked", "ms912x_request_finish_batch",
                 "ms912x_request_pack",
                 "ms912x_request_include_cursor_locked",
                 "ms912x_usb_request_refresh", "ms912x_rect_align_x4",
                 "ms912x_rects_collapse_bounds", "ms912x_conversion_prepare_batch",
                 "ms912x_conversion_reserve_locked", "ms912x_conversion_stage_locked",
                 "ms912x_conversion_update_replay_locked",
                 "ms912x_conversion_requeue_full_locked", "ms912x_conversion_complete_locked",
                 "ms912x_conversion_worker")
        batch_start = source.index("struct ms912x_conversion_batch {")
        batch_end = source.index("\n};", batch_start) + len("\n};")
        production = source[batch_start:batch_end] + "\n"
        production += "".join(function(source, name) for name in names)
        usb_source = (repo / 'linux/ms912x_usb_transfer.c').read_text()
        usb_helpers = ("ms912x_usb_request_release_locked",
                       "ms912x_usb_request_covers_rect",
                       "ms912x_usb_requeue_request_rects_locked",
                       "ms912x_usb_rate_update_locked",
                       "ms912x_usb_carry_failed_rect_locked",
                       "ms912x_usb_request_prepare_locked",
                       "ms912x_usb_request_complete_locked")
        production += "".join(function(usb_source, name) for name in usb_helpers)
        (build / "refresh.h").write_text(
            production + function(usb_source, "ms912x_usb_request_worker"))
        convert = (repo / 'linux/ms912x_convert.c').read_text()
        color = (repo / 'linux/ms912x_color.h').read_text()
        protocol = (repo / 'linux/ms912x_protocol.h').read_text()
        frame_header = re.search(r"struct ms912x_frame_update_header \{.*?\} __packed;", protocol, re.S).group()
        (build / "packet-production.h").write_text(frame_header + "\n" + "".join(
            function(color, name) for name in ("ms912x_rgb_to_y", "ms912x_rgb_pair_to_u", "ms912x_rgb_pair_to_v")) + "".join(
            function(convert, name) for name in (
                "ms912x_blend_u8", "ms912x_blend_pair_chroma", "ms912x_cursor_composite_uyvy",
                "ms912x_debug_draw_uyvy_border", "ms912x_pack_uyvy_batch")))
        damage = (repo / 'linux/ms912x_damage.c').read_text()
        (build / "scan-rows.h").write_text("".join(function(damage, name) for name in (
            "ms912x_damage_scan_process_row", "ms912x_damage_scan_extend_bottom")))
        # Instrument comparison boundaries without changing production code.
        (build / "payload.c").write_text(
            '#include "compat.h"\n#define memcmp ms912x_test_payload_memcmp\n' +
            (repo / 'linux/ms912x_payload.c').read_text())
        os.sync()
        flags = ["-std=gnu11", "-O2", "-g", "-fno-strict-aliasing", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare"]
        if args.sanitize:
            flags += ["-fsanitize=undefined,bounds", "-fno-omit-frame-pointer"]
        for name, sources in (
            ("scan-rects", ['linux/ms912x_scan_rects.c', "shared/ms912x_scan_rects_core.c", "tools/test_scan_rects.c"]),
            ("scan-rows", ['linux/ms912x_scan_rects.c', "shared/ms912x_scan_rects_core.c", "tools/test_scan_rows.c"]),
            ("payload", ["ms912x_batch.c", "ms912x_mode3.c", str(build / "payload.c"), "tools/test_payload.c"]),
        ):
            binary = build / name
            fixture_flags = ["-DMS912X_TEST_SORT_HOOK", "-DMS912X_TEST_PAYLOAD_COMPARE_HOOK",
                             "-pthread"] if name == "payload" else []
            subprocess.run([*shlex.split(args.cc), *flags, *fixture_flags,
                            "-include", str(build / "compat.h"),
                            "-I" + str(build), "-I" + str(repo), "-I" + str(repo / "linux"),
                            *(str(repo / s) for s in sources), "-o", str(binary)], check=True)
            subprocess.run([str(binary), *(["--benchmark"] if args.benchmark else [])], check=True)


if __name__ == "__main__":
    main()
