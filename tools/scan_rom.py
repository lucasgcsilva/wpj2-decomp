"""Mapeia regiões plausíveis de código MIPS em uma ROM N64.

O resultado é exploratório: ele ajuda a formular segmentos para validação,
mas não produz uma tabela de símbolos nem assume que todo bloco decodificável
seja código. Execute com o Rabbitizer disponível em PYTHONPATH.
"""

from __future__ import annotations

import argparse
import struct
from collections import Counter
from pathlib import Path

import rabbitizer


def word(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def block_stats(data: bytes, start: int, size: int) -> dict[str, int]:
    end = min(start + size, len(data) - (len(data) - start) % 4)
    stats = Counter(total=0, valid=0, zero=0, jr_ra=0, jal=0)
    for offset in range(start, end, 4):
        raw = word(data, offset)
        stats["total"] += 1
        if raw == 0:
            stats["valid"] += 1
            stats["zero"] += 1
            continue
        instruction = rabbitizer.Instruction(raw, vram=0x80000000 + offset - 0x1000)
        if instruction.isValid():
            stats["valid"] += 1
        if instruction.isJrRa():
            stats["jr_ra"] += 1
        if instruction.getOpcodeName() == "jal":
            stats["jal"] += 1
    return stats


def likely_code(stats: dict[str, int]) -> bool:
    if not stats["total"]:
        return False
    valid = 100 * stats["valid"] / stats["total"]
    zero = 100 * stats["zero"] / stats["total"]
    return valid >= 97 and zero < 60 and (stats["jr_ra"] > 0 or stats["jal"] > 0)


def print_runs(rows: list[tuple[int, dict[str, int]]], block_size: int) -> None:
    runs: list[tuple[int, int]] = []
    current: list[int] | None = None
    for offset, stats in rows:
        if likely_code(stats):
            if current is None:
                current = [offset, offset + block_size]
            else:
                current[1] = offset + block_size
        elif current is not None:
            runs.append((current[0], current[1]))
            current = None
    if current is not None:
        runs.append((current[0], current[1]))

    print("\n=== grupos de blocos com código plausível ===")
    for start, end in runs:
        print(f"rom 0x{start:08X}-0x{end:08X}  ({(end - start) / 1024:.1f} KiB)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("--block", type=lambda value: int(value, 0), default=0x1000)
    parser.add_argument("--start", type=lambda value: int(value, 0), default=0x1000)
    parser.add_argument("--end", type=lambda value: int(value, 0))
    args = parser.parse_args()

    data = args.rom.read_bytes()
    header = data[:0x40]
    title = header[0x20:0x34].decode("ascii", "replace").rstrip(" \0")
    print(f"rom: {args.rom.name}")
    print(f"tamanho: 0x{len(data):X} ({len(data) / 1048576:.2f} MiB)")
    print(f"titulo: {title}; entrypoint: 0x{word(header, 8):08X}")
    print(f"bloco: 0x{args.block:X}\n")
    print("offset      valido%  zero%  jr_ra  jal  candidato")

    rows: list[tuple[int, dict[str, int]]] = []
    start = max(0x1000, args.start)
    end = min(len(data), args.end if args.end is not None else len(data))
    for offset in range(start, end - 3, args.block):
        stats = block_stats(data, offset, args.block)
        rows.append((offset, stats))
        valid = 100 * stats["valid"] / max(1, stats["total"])
        zero = 100 * stats["zero"] / max(1, stats["total"])
        candidate = "sim" if likely_code(stats) else ""
        print(f"0x{offset:08X}  {valid:6.1f}  {zero:5.1f}  {stats['jr_ra']:5}"
              f"  {stats['jal']:3}  {candidate}")

    print_runs(rows, args.block)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
