"""Exibe uma faixa de palavras da ROM como instruções MIPS."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

import rabbitizer


def number(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("--rom-offset", type=number, required=True)
    parser.add_argument("--vram", type=number, required=True)
    parser.add_argument("--count", type=int, default=32)
    args = parser.parse_args()

    data = args.rom.read_bytes()
    for index in range(args.count):
        offset = args.rom_offset + index * 4
        raw = struct.unpack_from(">I", data, offset)[0]
        address = args.vram + index * 4
        decoded = rabbitizer.Instruction(raw, vram=address)
        print(f"{address:08X}  {raw:08X}  {decoded.disassemble()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
