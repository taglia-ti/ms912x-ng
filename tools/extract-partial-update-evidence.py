#!/usr/bin/env python3
"""Extract address-indexed evidence from existing local vendor disassemblies."""

import hashlib
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "drivers/partial-update-analysis"
SOURCES = {
    "windows-1007": (
        "drivers/windows-analysis/unpacked/app/WinUsbDisplay.exe$0",
        "drivers/windows-analysis/pe/app/WinUsbDisplay.exe$0/annotated.asm",
        {
            "damage-history": (0x449833, 0x449930),
            "periodic-refresh": (0x449930, 0x4499EB),
            "refresh-setup": (0x4452E0, 0x445533),
            "initial-images": (0x446444, 0x446487),
            "rectangle-alignment": (0x446F8B, 0x4470E0),
            "batch-upload": (0x4474ED, 0x447650),
            "rectangle-packer": (0x44EFF0, 0x44F134),
            "region-intersection": (0x456900, 0x456A0C),
            "region-union": (0x456E40, 0x456F3A),
            "bypass-configuration": (0x44EAA8, 0x44EB6E),
            "scaled-path-selection": (0x446E2D, 0x446EB1),
            "scaled-packet": (0x446D2E, 0x446DCB),
            "video-in-out-serializers": (0x44D7C0, 0x44D8E0),
            "video-parameter-policy": (0x44E9E0, 0x44EEC9),
            "video-dispatch": (0x44EED0, 0x44EFE3),
            "video-timing-table": (0x44C6C2, 0x44CA90),
            "batch-buffer-budget": (0x444010, 0x44409B),
            "batch-buffer-allocation": (0x44DCCE, 0x44DD29),
            "multi-rectangle-sender": (0x446DF0, 0x447658),
            "bulk-write-wrapper": (0x44D470, 0x44D494),
            "region-vector-order": (0x455D00, 0x455DF8),
            "region-y-band-order": (0x456355, 0x4563CE),
            "region-x-span-order": (0x456B20, 0x456E33),
            "region-collector-order": (0x44A2DD, 0x44A318),
            "rectangle-vector-append": (0x460260, 0x460272),
        },
    ),
    "windows-1016": (
        "drivers/windows-analysis/unpacked/app/WinUsbDisplay.exe",
        "drivers/windows-analysis/pe/app/WinUsbDisplay.exe/annotated.asm",
        {
            "damage-history": (0x44921A, 0x449305),
            "video-in-serializer": (0x44D0B0, 0x44D140),
            "video-parameter-policy": (0x44E2D0, 0x44E7B9),
            "video-dispatch": (0x44E7C0, 0x44E8D3),
            "batch-buffer-budget": (0x4439F0, 0x443A7B),
            "batch-buffer-allocation": (0x44D5BE, 0x44D619),
            "multi-rectangle-sender": (0x446700, 0x446F60),
            "rectangle-packer": (0x44E8E0, 0x44EA24),
            "region-vector-order": (0x455390, 0x455488),
            "region-y-band-order": (0x455910, 0x455E20),
            "region-x-span-order": (0x4561B0, 0x4564C3),
        },
    ),
    "macos": (
        "drivers/macos-analysis/unpacked/usr/local/bin/screen_capture_msdisplay",
        "drivers/macos-analysis/native/annotated.asm",
        {
            "damage-history-input": (0x10009066F, 0x100090732),
            "rectangle-enumeration": (0x1000061D0, 0x100006310),
            "pixel-copy-and-packing": (0x100090E04, 0x100091053),
            "cursor-order": (0x100091125, 0x10009114A),
            "history-and-upload": (0x1000917A1, 0x100091944),
            "initial-packing": (0x10008FDF0, 0x10008FE50),
            "initial-upload-1": (0x10008FFBA, 0x10008FFEE),
            "initial-upload-2": (0x10009018E, 0x1000901C2),
            "bulk-wrapper": (0x1000776C0, 0x100077710),
            "rectangle-packer": (0x100077710, 0x100077970),
            "cursor-alignment": (0x1000AD513, 0x1000AD5C4),
            "scaling-test-mode-3": (0x100077A3E, 0x100077BC2),
            "normal-scale-policy": (0x1000A2F7F, 0x1000A3007),
            "normal-timing-selection": (0x1000A3165, 0x1000A3449),
            "normal-input-and-output": (0x1000A34AE, 0x1000A3716),
            "timing-table-constructor": (0x1000997C0, 0x10009AF95),
            "timing-lookup-functions": (0x1000A45E0, 0x1000A48EF),
            "video-in-out-serializers": (0x1000771D0, 0x100077480),
            "capture-geometry-policy": (0x1000A290A, 0x1000A29E6),
            "multi-rectangle-loop": (0x1000908BE, 0x100091125),
            "in-place-pixel-conversion": (0x10008F760, 0x10008F7F3),
            "region-sort-comparator": (0x10000BDB0, 0x10000BE36),
            "region-sort-caller": (0x10000B573, 0x10000B58A),
        },
    ),
    "windows-libusb0": (
        "drivers/windows-analysis/unpacked/app/libusb0.dll",
        "drivers/windows-analysis/pe/app/libusb0.dll/batching.asm",
        {"bulk-write-chunking": (0x404E1F, 0x404EF4)},
    ),
}


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    manifest = {"scope": "Static evidence only; no vendor code or USB I/O.",
                "sources": {}}
    for name, (binary_path, assembly_path, ranges) in SOURCES.items():
        binary = ROOT / binary_path
        assembly = ROOT / assembly_path
        if name == "windows-libusb0":
            # Disassemble bytes only; do not load or execute the DLL.
            assembly.write_text(subprocess.check_output([
                "llvm-objdump", "-d", "--x86-asm-syntax=intel",
                "--start-address=0x404e1f", "--stop-address=0x404ef4",
                str(binary)], text=True))
        instructions = []
        for line in assembly.read_text().splitlines():
            match = re.match(r"\s*([0-9a-f]+):", line)
            if match:
                instructions.append((int(match[1], 16), line))
        entry = {"binary": binary_path,
                 "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                 "assembly": assembly_path,
                 "assembly_sha256": hashlib.sha256(assembly.read_bytes()).hexdigest(),
                 "excerpts": {}}
        for label, (start, end) in ranges.items():
            lines = [line for address, line in instructions if start <= address < end]
            if not lines or not any(address == start for address, _ in instructions):
                raise ValueError(f"Missing evidence address: {name}/{label}: {start:#x}")
            filename = f"{name}-{label}.asm"
            (OUTPUT / filename).write_text("\n".join(lines) + "\n")
            entry["excerpts"][label] = {
                "file": filename, "start": hex(start),
                "end_exclusive": hex(end), "instructions": len(lines),
            }
        manifest["sources"][name] = entry
    (OUTPUT / "evidence.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Extracted {sum(len(s[2]) for s in SOURCES.values())} excerpts to {OUTPUT}")


if __name__ == "__main__":
    main()
