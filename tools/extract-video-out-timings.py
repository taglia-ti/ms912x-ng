#!/usr/bin/env python3
"""Inventory local vendor timing tables without executing vendor code.

The MS9132 Linux source is retained as untrusted hints only. The binary-table
reader propagates constants through the straight-line table
initializers in existing LLVM disassembly. It is not a CPU emulator.
"""

import csv
import hashlib
import json
from pathlib import Path
import re
import struct


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "drivers/video-out-analysis"
FIELDS = ("polarity", "htotal", "vtotal", "width", "height", "clock_10khz",
          "refresh_100hz", "hoffset", "voffset", "hsyncwidth", "vsyncwidth")


def instructions(path):
    for line in path.read_text().splitlines():
        match = re.match(r"\s*([0-9a-f]+):.*?\t([a-z]+)\t(.*)", line)
        if match:
            yield int(match[1], 16), match[2], match[3].split(" ;")[0].split(" ##")[0]


class Constants:
    def __init__(self, bits):
        self.regs = {"rbp": ("stack", 0), "rsp": ("args", 0),
                     "rcx": ("object", 0)}
        self.mem = {}
        self.pointers = {}
        self.bits = bits

    def reg(self, name):
        for stem in ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp"):
            if name in ("r" + stem, "e" + stem, stem):
                return "r" + stem, 64 if name[0] == "r" else 32 if name[0] == "e" else 16
        if name in ("al", "bl", "cl", "dl", "sil", "dil"):
            return {"al": "rax", "bl": "rbx", "cl": "rcx", "dl": "rdx",
                    "sil": "rsi", "dil": "rdi"}[name], 8
        match = re.fullmatch(r"(r\d+)([dwb]?)", name)
        if match:
            return match[1], {"": 64, "d": 32, "w": 16, "b": 8}[match[2]]
        if re.fullmatch(r"xmm\d+", name):
            return name, 128
        return None

    def address(self, operand):
        match = re.search(r"\[([a-z0-9]+)(?: ([+-]) (0x[0-9a-f]+))?\]", operand)
        if not match:
            return None
        base = self.value(match[1])
        if not isinstance(base, tuple):
            return None
        delta = int(match[3], 16) if match[3] else 0
        return base[0], base[1] + (-delta if match[2] == "-" else delta)

    def size(self, operand):
        return next((n for word, n in (("xmmword", 16), ("qword", 8),
                                      ("dword", 4), ("word", 2), ("byte", 1))
                     if operand.startswith(word + " ptr")), None)

    def read(self, address, size):
        if address is None:
            return None
        if (address, size) in self.pointers:
            return self.pointers[address, size]
        cells = [self.mem.get((address[0], address[1] + i)) for i in range(size)]
        return None if None in cells else int.from_bytes(bytes(cells), "little")

    def write(self, address, size, value):
        if address is None:
            return
        for key in list(self.pointers):
            (space, start), length = key
            if space == address[0] and start < address[1] + size and address[1] < start + length:
                del self.pointers[key]
        if isinstance(value, tuple):
            self.pointers[address, size] = value
        data = (value if isinstance(value, list) else
                (value & ((1 << (size * 8)) - 1)).to_bytes(size, "little")
                if isinstance(value, int) else [None] * size)
        for i, byte in enumerate(data):
            self.mem[address[0], address[1] + i] = byte

    def value(self, operand):
        operand = operand.strip()
        if re.fullmatch(r"-?(?:0x[0-9a-f]+|\d+)", operand):
            return int(operand, 0)
        reg = self.reg(operand)
        if reg:
            value = self.regs.get(reg[0])
            return value & ((1 << reg[1]) - 1) if isinstance(value, int) else value
        size = self.size(operand)
        return self.read(self.address(operand), size) if size else None

    def assign(self, operand, value):
        reg = self.reg(operand)
        if reg:
            self.regs[reg[0]] = (value & ((1 << reg[1]) - 1)
                                  if isinstance(value, int) else value)
        elif self.size(operand):
            self.write(self.address(operand), self.size(operand), value)

    def step(self, op, args):
        if op in ("mov", "movzx", "movsx", "movups", "movaps", "movq"):
            dst, src = map(str.strip, args.split(",", 1))
            value = self.value(src)
            if op in ("movups", "movaps") and self.address(src):
                space, start = self.address(src)
                value = [self.mem.get((space, start + i)) for i in range(16)]
            self.assign(dst, value)
        elif op == "lea":
            dst, src = map(str.strip, args.split(",", 1))
            value = self.address(src)
            match = re.fullmatch(r"\[([a-z0-9]+)(?: ([+-]) (0x[0-9a-f]+))?\]", src)
            if match and isinstance(self.value(match[1]), int):
                delta = int(match[3], 16) if match[3] else 0
                value = self.value(match[1]) + (-delta if match[2] == "-" else delta)
            self.assign(dst, value)
        elif op in ("add", "sub", "and", "or", "xor", "shl", "shr"):
            dst, src = map(str.strip, args.split(",", 1))
            a, b = self.value(dst), self.value(src)
            if op == "xor" and dst == src:
                value = 0
            elif isinstance(a, tuple) and isinstance(b, int) and op in ("add", "sub"):
                value = a[0], a[1] + (b if op == "add" else -b)
            elif isinstance(a, int) and isinstance(b, int):
                value = {"add": lambda: a + b, "sub": lambda: a - b,
                         "and": lambda: a & b, "or": lambda: a | b,
                         "xor": lambda: a ^ b, "shl": lambda: a << b,
                         "shr": lambda: a >> b}[op]()
            else:
                value = None
            # Initializers use a fixed stack frame; do not model prologue changes.
            if dst not in ("esp", "rsp"):
                self.assign(dst, value)


def android():
    path = ROOT / "drivers/android-analysis/jadx/sources/com/ms/ms2160/Util/Util.java"
    source = path.read_text()
    constants = {k: int(v) for k, v in re.findall(r"public static int (\w+) = (\d+);", source)}
    simple = []
    for name, w, h, clock, rate in re.findall(
            r"new AS7160_SIMPLE_TIMING\(_E_AS7160_VIDEO_FORMAT_\.(\w+), (\d+), (\d+), (\d+), (\d+)\)", source):
        simple.append(dict(vic=constants[name], width=int(w), height=int(h),
                           clock_10khz=int(clock), refresh=int(rate)))
    detailed = []
    for name, polarity, values in re.findall(
            r"new AS7160_MISC_TIMING\(\(byte\) _E_AS7160_VIDEO_FORMAT_\.(\w+), new AS7160_VIDEO_TIMING\(\(byte\) _E_SYNC_POLARITY_\.(\w+), ([^)]+)\)\)", source):
        values = values.replace("PathInterpolatorCompat.MAX_NUM_POINTS", "3000")
        row = dict(zip(FIELDS, [constants[polarity]] + [int(v) for v in values.split(", ")]))
        detailed.append(dict(vic=constants[name], **row))
    selections = {}
    for name, body in re.findall(r"Select_tim\[\] (\w+) = \{(.+?)\};", source):
        selections[name] = [dict(vic=constants[k], label=label) for k, label in
                            re.findall(r'new Select_tim\(_E_AS7160_VIDEO_FORMAT_\.(\w+), "([^"]+)"\)', body)]
    return dict(path=str(path.relative_to(ROOT)), simple=simple, detailed=detailed,
                selections=selections,
                enum_only=[dict(vic=v, name=k) for k, v in constants.items()
                           if k.startswith("VFMT_") and v not in {r["vic"] for r in simple + detailed}])


def windows(name):
    path = ROOT / "drivers/windows-analysis/pe/app" / name / "annotated.asm"
    code = list(instructions(path))
    # Both variants initialize the same object layout; locate the first full timing.
    anchor = next(i for i, (_, op, args) in enumerate(code)
                  if op == "mov" and args == "dword ptr [ebp - 0x28], 0x20d035a")
    state = Constants(32)
    state.regs["rsi"] = ("object", 0)
    for _, op, args in code[anchor - 20:]:
        if op == "call":
            break
        state.step(op, args)
    simple, detailed = [], []
    for index in range(26):
        base = 0x288 + index * 12
        vic = state.read(("object", base), 4)
        w = state.read(("object", base + 4), 2)
        h = state.read(("object", base + 6), 2)
        clock = state.read(("object", base + 8), 2)
        rate = state.read(("object", base + 10), 1)
        if None in (vic, w, h, clock, rate):
            raise ValueError(f"Unresolved {name} simple record {index}")
        simple.append(dict(vic=vic, width=w, height=h, clock_10khz=clock, refresh=rate))
        base = 0x18 + index * 24
        vic = state.read(("object", base), 1)
        values = [state.read(("object", base + 2), 1)]
        values += [state.read(("object", base + offset), 2) for offset in range(4, 24, 2)]
        if vic is None or None in values:
            raise ValueError(f"Unresolved {name} detailed record {index}")
        detailed.append(dict(vic=vic, **dict(zip(FIELDS, values))))
    return dict(path=str(path.relative_to(ROOT)), simple=simple, detailed=detailed)


def macos():
    path = ROOT / "drivers/macos-analysis/native/annotated.asm"
    state = Constants(64)
    simple, detailed = [], []
    last_timing = None
    for va, op, args in instructions(path):
        if not 0x1000997C0 <= va < 0x10009AF95:
            continue
        if op != "call":
            state.step(op, args)
            continue
        if "AS7160_VIDEO_TIMING::AS7160_VIDEO_TIMING(" in args:
            values = [state.value(r) for r in ("esi", "edx", "ecx", "r8d", "r9d")]
            values += [state.read(("args", offset), 4) for offset in range(0, 48, 8)]
            if None in values:
                raise ValueError(f"Unresolved macOS detailed timing at {va:#x}: {values}")
            last_timing = dict(zip(FIELDS, values), address=hex(va))
        elif "AS7160_MISC_TIMING::AS7160_MISC_TIMING(" in args:
            detailed.append(dict(vic=state.value("esi"), **last_timing))
        elif "AS7160_SIMPLE_TIMING::AS7160_SIMPLE_TIMING(" in args:
            values = [state.value(r) for r in ("esi", "edx", "ecx", "r8d", "r9d")]
            if None in values:
                raise ValueError(f"Unresolved macOS simple timing at {va:#x}: {values}")
            simple.append(dict(zip(("vic", "width", "height", "clock_10khz", "refresh"), values), address=hex(va)))
        for reg in ("rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"):
            state.regs[reg] = None
    return dict(path=str(path.relative_to(ROOT)), simple=simple, detailed=detailed)


def linux():
    path = ROOT / "drivers/ms9132-drm-linux-master/usb_hal/ms9132.c"
    source = path.read_text()
    constants = {k: int(v) for k, v in re.findall(r"#define (VIC_VESA_\w+)\s+(\d+)", source)}
    rows = [dict(vic=constants[k], refresh=int(rate), width=int(w), height=int(h))
            for k, rate, w, h in re.findall(r"\{(VIC_VESA_\w+), (\d+), (\d+), (\d+)\}", source)]
    return dict(path=str(path.relative_to(ROOT)), simple=rows, detailed=[])


def windows_desktop_tables():
    """Read IDD desktop descriptors separately: these contain no USB VIC."""
    result = {}
    inventory = ROOT / "drivers/windows-analysis/pe/app/idd"
    for meta_path in sorted(inventory.rglob("metadata.json")):
        strings_path = meta_path.with_name("strings.txt")
        label_match = re.search(r'(0x[0-9a-f]+) ascii "640\*480\*60"', strings_path.read_text())
        if label_match is None:
            continue
        metadata = json.loads(meta_path.read_text())
        path = ROOT / "drivers/windows-analysis/unpacked" / metadata["file"]
        data = path.read_bytes()
        image_base = int(metadata["image_base"], 16)
        is_64 = metadata["machine"] == "0x8664"
        layout = "<Q6IQ" if is_64 else "<6IQ"
        stride = struct.calcsize(layout)

        def offset(va):
            rva = va - image_base
            for section in metadata["sections"]:
                start = int(section["rva"], 16)
                if start <= rva < start + section["raw_size"]:
                    return section["file_offset"] + rva - start
            raise ValueError(f"Unmapped address {va:#x} in {path}")

        label_va = int(label_match[1], 16)
        needle = label_va.to_bytes(8 if is_64 else 4, "little")
        candidates = []
        for match in re.finditer(re.escape(needle), data):
            rows = []
            try:
                for index in range(45):
                    values = struct.unpack_from(layout, data, match.start() + index * stride)
                    ptr, ht, vt, w, h, rate = values[:6]
                    clock = values[-1]
                    label_offset = offset(ptr)
                    label = data[label_offset:data.index(b"\0", label_offset)].decode("ascii")
                    if not (1 <= rate <= 240 and w <= ht and h <= vt and 0 < clock < 1_000_000_000):
                        break
                    rows.append(dict(label=label, width=w, height=h, refresh=rate,
                                     htotal=ht, vtotal=vt, clock_hz=clock,
                                     file_offset=hex(match.start() + index * stride)))
            except (ValueError, struct.error, UnicodeError):
                continue
            if len(rows) == 45:
                candidates.append(rows)
        if len(candidates) != 1:
            raise ValueError(f"Expected one complete 45-record desktop table in {path}")
        result[metadata["file"]] = dict(path=str(path.relative_to(ROOT)),
                                       sha256=hashlib.sha256(data).hexdigest(),
                                       records=candidates[0])
    if len(result) != 6:
        raise ValueError("Expected x86/x64 variants of all three IDD packages")
    return result


def main():
    sources = {"android": android(), "windows-1007": windows("WinUsbDisplay.exe$0"),
               "windows-1016": windows("WinUsbDisplay.exe"), "macos": macos(), "linux": linux()}
    for name, source in sources.items():
        source["sha256"] = hashlib.sha256((ROOT / source["path"]).read_bytes()).hexdigest()
        source["evidence_role"] = "hint_only" if name == "linux" else "vendor_implementation"
    binaries = {
        "android": "drivers/MSDisplay_Android_v2.0.0.3.apk",
        "windows-1007": "drivers/windows-analysis/unpacked/app/WinUsbDisplay.exe$0",
        "windows-1016": "drivers/windows-analysis/unpacked/app/WinUsbDisplay.exe",
        "macos": "drivers/macos-analysis/unpacked/usr/local/bin/screen_capture_msdisplay",
    }
    for name, path in binaries.items():
        sources[name]["binary"] = path
        sources[name]["binary_sha256"] = hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
    desktop_tables = windows_desktop_tables()
    assert sources["android"]["simple"] == sources["windows-1007"]["simple"]
    assert sources["windows-1007"]["simple"] == sources["windows-1016"]["simple"]
    assert sources["windows-1007"]["detailed"] == sources["windows-1016"]["detailed"]
    assert [len(sources[name]["simple"]) for name in sources] == [26, 26, 26, 25, 16]
    assert [len(sources[name]["detailed"]) for name in sources] == [26, 26, 26, 25, 0]
    primary_vics = {row["vic"] for name, s in sources.items() if name != "linux"
                    for k in ("simple", "detailed") for row in s[k]}
    assert len(primary_vics) == 30
    assert {row["vic"] for row in sources["linux"]["simple"]} <= primary_vics
    descriptors = [[{k: v for k, v in row.items() if k != "file_offset"}
                    for row in table["records"]] for table in desktop_tables.values()]
    assert all(rows == descriptors[0] for rows in descriptors)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "timings.json").write_text(json.dumps(sources, indent=2) + "\n")
    (OUT / "windows-desktop-timings.json").write_text(json.dumps(desktop_tables, indent=2) + "\n")
    with (OUT / "timings.csv").open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(("source", "evidence_role", "table", "vic_hex", "width", "height", "refresh_hz", "clock_mhz"))
        for name, source in sources.items():
            for kind in ("simple", "detailed"):
                for row in source[kind]:
                    writer.writerow((name, source["evidence_role"], kind, f"0x{row['vic']:02x}", row["width"], row["height"],
                                     row.get("refresh", row.get("refresh_100hz", 0) / 100),
                                     row["clock_10khz"] / 100 if "clock_10khz" in row else ""))
    for name, source in sources.items():
        print(f"{name} ({source['evidence_role']}): {len(source['simple'])} lookup records, {len(source['detailed'])} full timing records")
    print(f"Windows desktop tables: {len(desktop_tables)} binaries, 45 descriptors each (no USB VIC)")


if __name__ == "__main__":
    main()
