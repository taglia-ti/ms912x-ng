#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ms912x_mode_catalog.h"
OUTPUT = ROOT / 'linux/ms912x_fake_edid.h'
BLOCK_SIZE = 128
MAX_BLOCKS = 8
BASE_DTD_SLOTS = 2
CTA_DTD_SLOTS = 6

BASE_EDID = bytes.fromhex(
    "00 ff ff ff ff ff ff 00 36 78 20 91 01 00 00 00 "
    "01 22 01 04 80 34 1d 78 03 ae 50 a3 54 4c 99 26 "
    "0f 50 54 00 00 00 61 40 45 40 31 40 81 80 d1 c0 "
    "01 01 59 c0 01 01 02 3a 80 18 71 38 2d 40 58 2c "
    "45 00 08 22 21 00 00 1e 00 00 00 fc 00 4d 53 39 "
    "31 32 78 0a 20 20 20 20 20 20 00 00 00 fd 00 01 "
    "4b 1e 5a 14 00 00 00 00 00 00 00 00 00 00 00 ff "
    "00 4d 53 39 31 32 58 30 30 30 30 30 31 0a 01 0f"
)


def parse_modes():
    pattern = re.compile(
        r"MS912X_MODE_TIMED\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,"
        r"\s*(0x[0-9a-fA-F]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,"
        r"\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,"
        r"\s*(\d+)\s*,\s*(\d+)\s*\)"
    )
    modes = []
    for match in pattern.finditer(SOURCE.read_text()):
        values = match.groups()
        modes.append({
            "width": int(values[0]),
            "height": int(values[1]),
            "hz": int(values[2]),
            "vic": int(values[3], 16),
            "clock": int(values[4]),
            "htotal": int(values[5]),
            "vtotal": int(values[6]),
            "hoffset": int(values[7]),
            "voffset": int(values[8]),
            "hsync": int(values[9]),
            "vsync": int(values[10]),
            "polarity": int(values[11]),
        })

    if not modes:
        raise ValueError(f"no timed mode entries found in {SOURCE}")
    if len({(mode["width"], mode["height"], mode["hz"])
            for mode in modes}) != len(modes):
        raise ValueError("fixed mode catalog has duplicate geometry/refresh entries")
    preferred = next((mode for mode in modes if mode["vic"] == 0x81), None)
    if preferred is None:
        raise ValueError("fixed catalog has no 1920x1080@60 VIC 0x81 preferred mode")
    modes.remove(preferred)
    modes.insert(0, preferred)
    if len(modes) > BASE_DTD_SLOTS + (MAX_BLOCKS - 1) * CTA_DTD_SLOTS:
        raise ValueError("fixed mode catalog exceeds EDID DTD capacity")
    return modes


def encode_dtd(mode):
    width, height = mode["width"], mode["height"]
    htotal, vtotal = mode["htotal"], mode["vtotal"]
    hsync_start = htotal - mode["hoffset"]
    vsync_start = vtotal - mode["voffset"]
    hblank, vblank = htotal - width, vtotal - height
    hsync_offset, vsync_offset = hsync_start - width, vsync_start - height
    clock = mode["clock"]
    hsync_width, vsync_width = mode["hsync"], mode["vsync"]

    if clock % 10:
        raise ValueError(f"VIC 0x{mode['vic']:02x}: clock is not in 10 kHz units")
    if hsync_start < width or hsync_start + hsync_width > htotal:
        raise ValueError(f"VIC 0x{mode['vic']:02x}: invalid horizontal timing")
    if vsync_start < height or vsync_start + vsync_width > vtotal:
        raise ValueError(f"VIC 0x{mode['vic']:02x}: invalid vertical timing")
    if max(width, height, hblank, vblank) > 4095:
        raise ValueError(f"VIC 0x{mode['vic']:02x}: active/blanking field overflow")
    if hsync_offset > 1023 or hsync_width > 1023:
        raise ValueError(f"VIC 0x{mode['vic']:02x}: horizontal sync field overflow")
    if vsync_offset > 63 or vsync_width > 63:
        raise ValueError(f"VIC 0x{mode['vic']:02x}: vertical sync field overflow")

    dtd = bytearray(18)
    pixel_clock = clock // 10
    dtd[0] = pixel_clock & 0xff
    dtd[1] = pixel_clock >> 8
    dtd[2] = width & 0xff
    dtd[3] = hblank & 0xff
    dtd[4] = ((width >> 8) << 4) | (hblank >> 8)
    dtd[5] = height & 0xff
    dtd[6] = vblank & 0xff
    dtd[7] = ((height >> 8) << 4) | (vblank >> 8)
    dtd[8] = hsync_offset & 0xff
    dtd[9] = hsync_width & 0xff
    dtd[10] = ((vsync_offset & 0x0f) << 4) | (vsync_width & 0x0f)
    dtd[11] = (((hsync_offset >> 8) & 0x03) << 6) | \
        (((hsync_width >> 8) & 0x03) << 4) | \
        (((vsync_offset >> 4) & 0x03) << 2) | \
        ((vsync_width >> 4) & 0x03)
    dtd[17] = 0x18
    if mode["polarity"] & 4:
        dtd[17] |= 1 << 2
    if mode["polarity"] & 2:
        dtd[17] |= 1 << 1
    if not mode["polarity"] & 1:
        dtd[17] |= 1 << 7
    return dtd


def checksum_into(edid, offset):
    edid[offset + BLOCK_SIZE - 1] = (-sum(edid[offset:offset + BLOCK_SIZE - 1])) & 0xff


def generate():
    modes = parse_modes()
    edid = bytearray(MAX_BLOCKS * BLOCK_SIZE)
    edid[:BLOCK_SIZE] = BASE_EDID
    edid[24] |= 1 << 1
    edid[35:38] = b"\0\0\0"
    edid[38:54] = b"\x01\x01" * 8
    edid[54:126] = bytes(72)
    edid[90 + 3] = 0xfc
    edid[90 + 5:90 + 16] = b"MS912X USB\n"

    hfreqs = []
    clocks = []
    for index, mode in enumerate(modes):
        dtd = encode_dtd(mode)
        if index < BASE_DTD_SLOTS:
            offset = 54 + index * 18
        else:
            relative = index - BASE_DTD_SLOTS
            block = 1 + relative // CTA_DTD_SLOTS
            slot = relative % CTA_DTD_SLOTS
            offset = block * BLOCK_SIZE + 4 + slot * 18
        edid[offset:offset + 18] = dtd
        hfreqs.append((mode["clock"] + mode["htotal"] - 1) // mode["htotal"])
        clocks.append(mode["clock"])

    min_v = min(mode["hz"] for mode in modes)
    max_v = max(mode["hz"] for mode in modes)
    min_h, max_h = min(hfreqs), max(hfreqs)
    max_clock_10mhz = (max(clocks) + 9999) // 10000
    if max(min_v, max_v, min_h, max_h, max_clock_10mhz) <= 255:
        edid[108 + 3] = 0xfd
        edid[108 + 5:108 + 10] = bytes((min_v, max_v, min_h, max_h,
                                       max_clock_10mhz))

    extension_count = (max(0, len(modes) - BASE_DTD_SLOTS) +
                       CTA_DTD_SLOTS - 1) // CTA_DTD_SLOTS
    if len(modes) + 2 > BASE_DTD_SLOTS + extension_count * CTA_DTD_SLOTS:
        raise ValueError("fixed EDID has insufficient DTD slots for two custom timings")
    edid[126] = extension_count
    checksum_into(edid, 0)
    for block in range(1, extension_count + 1):
        extension = block * BLOCK_SIZE
        edid[extension] = 0x02
        edid[extension + 1] = 0x03
        edid[extension + 2] = 4
        edid[extension + 3] = 0
        checksum_into(edid, extension)
    return modes, bytes(edid[:(extension_count + 1) * BLOCK_SIZE])


def render_header(modes, edid):
    lines = [
        "/* Generated by tools/gen_fake_edid.py; do not edit manually. */",
        "#ifndef MS912X_FAKE_EDID_H",
        "#define MS912X_FAKE_EDID_H",
        "",
        f"#define MS912X_FAKE_EDID_FIXED_MODE_COUNT {len(modes)}",
        f"#define MS912X_FAKE_EDID_BLOCK_COUNT {len(edid) // BLOCK_SIZE}",
        f"#define MS912X_FAKE_EDID_MODE_CAPACITY {BASE_DTD_SLOTS + ((len(edid) // BLOCK_SIZE) - 1) * CTA_DTD_SLOTS}",
        "",
        "struct ms912x_fake_edid_mode_info {",
        "\tunsigned short width;",
        "\tunsigned short height;",
        "\tunsigned int hz;",
        "\tunsigned int hfreq_khz;",
        "\tunsigned int clock_10mhz;",
        "\tunsigned char vic;",
        "};",
        "",
        "static const struct ms912x_fake_edid_mode_info ms912x_fake_edid_modes[] = {",
    ]
    for mode in modes:
        hfreq = (mode["clock"] + mode["htotal"] - 1) // mode["htotal"]
        pixel_clock = (mode["clock"] + 9999) // 10000
        lines.append("\t{%u, %u, %u, %u, %u, 0x%02x}," %
                 (mode["width"], mode["height"], mode["hz"], hfreq,
                  pixel_clock, mode["vic"]))
    lines.extend([
        "};",
        "",
        "static const unsigned char ms912x_fake_edid_data[] = {",
    ])
    for offset in range(0, len(edid), 16):
        chunk = edid[offset:offset + 16]
        lines.append("\t" + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    lines.extend(["};", "", "#endif", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Generate fixed MS912x EDID data")
    parser.add_argument("--check", action="store_true",
                        help="fail if the checked-in header is stale")
    args = parser.parse_args()
    try:
        modes, edid = generate()
        header = render_header(modes, edid)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1

    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != header:
            print(f"{OUTPUT} is stale; run {Path(__file__).name}", file=sys.stderr)
            return 1
        return 0

    OUTPUT.write_text(header)
    print(f"wrote {OUTPUT} ({len(modes)} modes, {len(edid) // BLOCK_SIZE} blocks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())