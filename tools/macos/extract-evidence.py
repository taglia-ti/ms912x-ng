#!/usr/bin/env python3
"""Annotate LLVM output and extract evidence from the supplied little-endian Mach-O.

No disassembler, emulator or decompiler is implemented here. Instructions come
from llvm-objdump; annotations resolve its import stubs and C-string references.
"""

import json
import re
import struct
import sys
from pathlib import Path


def main():
    binary, out = map(Path, sys.argv[1:])
    data = binary.read_bytes()
    if data[:4] != b"\xcf\xfa\xed\xfe":
        raise ValueError("Expected the supplied 64-bit little-endian Mach-O")
    sections = []
    position = 32
    for _ in range(struct.unpack_from("<I", data, 16)[0]):
        command, size = struct.unpack_from("<II", data, position)
        if command == 0x19:  # LC_SEGMENT_64
            count = struct.unpack_from("<I", data, position + 64)[0]
            for i in range(count):
                section, segment, address, length, offset = struct.unpack_from(
                    "<16s16sQQI", data, position + 72 + 80 * i
                )
                sections.append(dict(
                    section=section.rstrip(b"\0").decode(),
                    segment=segment.rstrip(b"\0").decode(),
                    address=address, size=length, offset=offset,
                ))
        position += size
    (out / "sections.json").write_text(json.dumps(sections, indent=2) + "\n")

    def read_address(address, size):
        for section in sections:
            if (section["offset"] and section["address"] <= address
                    and address + size <= section["address"] + section["size"]):
                offset = section["offset"] + address - section["address"]
                return data[offset:offset + size]
        raise ValueError(f"Address outside file-backed sections: {address:#x}")

    imports = {
        int(match[1], 16): match[2]
        for match in re.finditer(
            r"^(0x[0-9a-f]+)\s+\d+\s+(\S+)",
            (out / "indirect-symbols.txt").read_text(), re.M,
        )
    }

    def annotate(match):
        address = int(match[1], 16)
        if address in imports:
            return f"0x{address:x} <{imports[address]}>"
        for section in sections:
            if (section["section"] == "__cstring"
                    and section["address"] <= address < section["address"] + section["size"]):
                offset = section["offset"] + address - section["address"]
                end = data.index(0, offset)
                value = data[offset:end].decode("utf-8", "replace")
                return f"0x{address:x} " + json.dumps(value, ensure_ascii=False)
        return match[0]

    assembly = re.sub(
        r"0x([0-9a-f]+) <dyld_stub_binder\+0x[0-9a-f]+>", annotate,
        (out / "screen_capture_msdisplay.asm").read_text(),
    )
    (out / "annotated.asm").write_text(assembly)
    functions = out / "functions"
    functions.mkdir(exist_ok=True)
    headers = list(re.finditer(r"^([0-9a-f]+) <(.+)>:\n", assembly, re.M))
    selected = {}
    for i, match in enumerate(headers):
        name = match[2]
        if not (
            name.startswith(("_ms2160", "_xdata", "_set_", "_change"))
            or name in ("_main", "_xusb_main", "_test_device", "_ARGBToUYVY", "createframegrabber()")
            or name.startswith(("frame_data_convert(", "thread_func_usb_transfer(", "CopyToBufferRect("))
            or (name.startswith("createframegrabber()::$_") and "operator()" in name)
            or name.startswith("SL::Screen_Capture::GetDifs(")
        ):
            continue
        end = headers[i + 1].start() if i + 1 < len(headers) else len(assembly)
        body = assembly[match.start():end]
        stem = re.sub(r"[^A-Za-z0-9_]+", "_", name).strip("_")
        filename = stem[:200] + ".asm"
        if len(stem) > 200:
            filename = stem[:180] + "_" + match[1] + ".asm"
        (functions / filename).write_text(body)
        selected[name] = dict(address="0x" + match[1], file=filename, lines=body.count("\n"))
    (functions / "index.json").write_text(json.dumps(selected, indent=2) + "\n")

    constants = []
    for line in (out / "symbols.txt").read_text().splitlines():
        match = re.match(r"([0-9a-f]+) . (.*)", line)
        if not match:
            continue
        address, name = int(match[1], 16), match[2]
        if name.startswith(("_HIDCMD", "_REG_")) or name in (
            "run_mode", "res_bpp", "res_w", "res_h", "g_zoom", "res_idx"
        ):
            size = 1 if name.startswith("_HIDCMD") else 4
            value = read_address(address, size)
            constants.append(
                f"0x{address:x} {name} = 0x{int.from_bytes(value, 'little'):x} ({value.hex(' ')})\n"
            )
    (out / "constants.txt").write_text("".join(constants))


if __name__ == "__main__":
    main()
