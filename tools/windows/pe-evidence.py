#!/usr/bin/env python3
"""Collect static metadata from extracted Windows binaries without loading them."""

import datetime
import hashlib
import json
import re
import sys
from pathlib import Path

import pefile


def describe(path, root, output):
    pe = pefile.PE(str(path))
    base = pe.OPTIONAL_HEADER.ImageBase
    relative = path.relative_to(root)
    destination = output / relative
    destination.mkdir(parents=True, exist_ok=True)
    metadata = dict(
        file=str(relative), size=path.stat().st_size,
        sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        machine=hex(pe.FILE_HEADER.Machine), image_base=hex(base),
        entry_rva=hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint),
        linker_timestamp_utc=datetime.datetime.fromtimestamp(
            pe.FILE_HEADER.TimeDateStamp, datetime.timezone.utc
        ).isoformat(),
        sections=[], imports=[], exports=[], debug=[], functions=[], version={},
    )
    for section in pe.sections:
        metadata["sections"].append(dict(
            name=section.Name.rstrip(b"\0").decode("ascii", "replace"),
            rva=hex(section.VirtualAddress), virtual_size=section.Misc_VirtualSize,
            file_offset=section.PointerToRawData, raw_size=section.SizeOfRawData,
            flags=hex(section.Characteristics),
        ))
    for library in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
        for item in library.imports:
            metadata["imports"].append(dict(
                library=library.dll.decode("ascii", "replace"),
                name=item.name.decode("ascii", "replace") if item.name else f"#{item.ordinal}",
                address=hex(item.address),
            ))
    export_directory = getattr(pe, "DIRECTORY_ENTRY_EXPORT", None)
    if export_directory:
        for item in export_directory.symbols:
            metadata["exports"].append(dict(
                name=item.name.decode("ascii", "replace") if item.name else f"#{item.ordinal}",
                rva=hex(item.address),
            ))
    for item in getattr(pe, "DIRECTORY_ENTRY_DEBUG", []):
        entry = dict(type=item.struct.Type)
        cv = getattr(item, "entry", None)
        if cv and hasattr(cv, "PdbFileName"):
            entry["pdb"] = cv.PdbFileName.rstrip(b"\0").decode("utf-8", "replace")
        metadata["debug"].append(entry)
    for group in getattr(pe, "FileInfo", []):
        for item in group:
            if item.Key == b"StringFileInfo":
                for table in item.StringTable:
                    metadata["version"].update({
                        k.decode("utf-8", "replace"): v.decode("utf-8", "replace")
                        for k, v in table.entries.items()
                    })
    for item in getattr(pe, "DIRECTORY_ENTRY_EXCEPTION", []):
        if hasattr(item.struct, "BeginAddress") and hasattr(item.struct, "EndAddress"):
            metadata["functions"].append(dict(
                begin_rva=hex(item.struct.BeginAddress), end_rva=hex(item.struct.EndAddress),
            ))
    strings = []
    for section in pe.sections:
        data = section.get_data()
        for encoding, pattern in [
            ("ascii", rb"[\x20-\x7e]{5,}"),
            ("utf-16le", rb"(?:[\x20-\x7e]\x00){5,}"),
        ]:
            for match in re.finditer(pattern, data):
                value = match[0].decode(encoding)
                address = base + section.VirtualAddress + match.start()
                strings.append(f"0x{address:x} {encoding} {json.dumps(value)}\n")
    (destination / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (destination / "strings.txt").write_text("".join(strings))
    return {key: metadata[key] for key in [
        "file", "size", "sha256", "machine", "image_base", "version", "debug"
    ]}


def main():
    root, output = map(Path, sys.argv[1:])
    output.mkdir(parents=True, exist_ok=True)
    inventory = []
    for path in sorted(root.rglob("*")):
        if path.is_file() and re.search(r"\.(?:exe|dll|sys)(?:\$\d+)?$", path.name, re.I):
            try:
                inventory.append(describe(path, root, output))
            except pefile.PEFormatError as error:
                inventory.append(dict(file=str(path.relative_to(root)), error=str(error)))
    (output / "inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    print(f"Static PE inventory: {len(inventory)} files")


if __name__ == "__main__":
    main()
