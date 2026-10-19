#!/usr/bin/env python3
"""Annotate LLVM PE assembly using only the extracted binary's metadata."""

import json
import re
import sys
from collections import defaultdict
from pathlib import Path


def main():
    directory = Path(sys.argv[1])
    metadata = json.loads((directory / "metadata.json").read_text())
    names = {int(item["address"], 16): item["library"] + "!" + item["name"]
             for item in metadata["imports"]}
    base = int(metadata["image_base"], 16)
    executable_ranges = [
        (base + int(section["rva"], 16),
         base + int(section["rva"], 16) + section["virtual_size"])
        for section in metadata["sections"] if int(section["flags"], 16) & 0x20000000
    ]
    for line in (directory / "strings.txt").read_text().splitlines():
        address, encoding, value = line.split(" ", 2)
        address = int(address, 16)
        # Instruction bytes can accidentally look like printable strings.
        if not any(start <= address < end for start, end in executable_ranges):
            names.setdefault(address, encoding + " " + value[:240])
    references = defaultdict(list)
    annotated = []
    for line in (directory / "disassembly.asm").read_text().splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s", line)
        if match:
            origin = int(match[1], 16)
            comments = []
            for value in dict.fromkeys(re.findall(r"0x[0-9a-f]+", line)):
                target = int(value, 16)
                if target in names:
                    comments.append(names[target])
                    references[value].append(hex(origin))
            if comments:
                line += " ; " + " | ".join(comments)
        annotated.append(line + "\n")
    (directory / "annotated.asm").write_text("".join(annotated))
    (directory / "references.json").write_text(json.dumps(dict(references), indent=2) + "\n")


if __name__ == "__main__":
    main()
