"""Encontra na ROM referências ``jal`` para uma faixa conhecida de VRAM."""

from __future__ import annotations

import argparse
import struct
from collections import Counter, defaultdict
from pathlib import Path


def integer(value: str) -> int:
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("--start", type=integer, required=True,
                        help="início inclusivo da faixa VRAM")
    parser.add_argument("--end", type=integer, required=True,
                        help="fim exclusivo da faixa VRAM")
    parser.add_argument("--block", type=integer, default=0x1000)
    parser.add_argument("--rom-start", type=integer, default=0)
    parser.add_argument("--rom-end", type=integer)
    parser.add_argument("--exclude-start", type=integer)
    parser.add_argument("--exclude-end", type=integer)
    parser.add_argument("--show", action="store_true",
                        help="também exibe cada offset ROM encontrado")
    args = parser.parse_args()

    data = args.rom.read_bytes()
    by_block: dict[int, list[tuple[int, int]]] = defaultdict(list)
    rom_end = min(len(data), args.rom_end if args.rom_end is not None else len(data))
    for offset in range(args.rom_start, rom_end - 3, 4):
        if (args.exclude_start is not None and args.exclude_end is not None
                and args.exclude_start <= offset < args.exclude_end):
            continue
        raw = struct.unpack_from(">I", data, offset)[0]
        if raw >> 26 != 0x03:  # jal
            continue
        target = 0x80000000 | ((raw & 0x03FFFFFF) << 2)
        if args.start <= target < args.end:
            by_block[offset & ~(args.block - 1)].append((offset, target))

    total = sum(len(values) for values in by_block.values())
    print(f"referências encontradas: {total}; blocos: {len(by_block)}")
    for block, refs in sorted(by_block.items(), key=lambda item: (-len(item[1]), item[0])):
        targets = Counter(target for _, target in refs)
        summary = ", ".join(f"0x{target:08X}:{count}"
                            for target, count in targets.most_common(4))
        print(f"rom 0x{block:08X}-0x{block + args.block:08X}  "
              f"refs={len(refs):3}  {summary}")
        if args.show:
            for offset, target in refs:
                print(f"    rom 0x{offset:08X} -> 0x{target:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
