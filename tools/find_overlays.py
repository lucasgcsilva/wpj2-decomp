"""Localiza candidatos a overlays sem assumir seus endereços de carga.

Os destinos de ``jal`` fora do boot revelam a faixa de VRAM procurada. As
sequências decodificáveis no restante da ROM são apenas candidatas: a relação
ROM→VRAM deve ser confirmada antes de entrar em ``wpj2.syms.toml``.
"""

from __future__ import annotations

import argparse
import struct
from collections import Counter
from pathlib import Path

import rabbitizer


def instruction(data: bytes, offset: int, vram: int) -> rabbitizer.Instruction:
    raw = struct.unpack_from(">I", data, offset)[0]
    return rabbitizer.Instruction(raw, vram=vram)


def external_jals(data: bytes, boot_start: int, boot_end: int, boot_vram: int) -> Counter[int]:
    targets: Counter[int] = Counter()
    for offset in range(boot_start, boot_end - 3, 4):
        vram = boot_vram + offset - boot_start
        decoded = instruction(data, offset, vram)
        if decoded.getOpcodeName() == "jal":
            target = decoded.getInstrIndexAsVram()
            if not boot_vram <= target < boot_vram + boot_end - boot_start:
                targets[target] += 1
    return targets


def valid_runs(data: bytes, start: int, minimum_words: int) -> list[tuple[int, int]]:
    runs: list[tuple[int, int]] = []
    run_start: int | None = None
    count = 0
    for offset in range(start, len(data) - 3, 4):
        if instruction(data, offset, 0x80000000).isValid():
            if run_start is None:
                run_start = offset
                count = 0
            count += 1
        else:
            if run_start is not None and count >= minimum_words:
                runs.append((run_start, offset))
            run_start = None
            count = 0
    if run_start is not None and count >= minimum_words:
        runs.append((run_start, len(data)))
    return runs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("--boot-end", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--boot-start", type=lambda value: int(value, 0), default=0x1000)
    parser.add_argument("--boot-vram", type=lambda value: int(value, 0), default=0x80000400)
    parser.add_argument("--min-words", type=int, default=256)
    args = parser.parse_args()

    data = args.rom.read_bytes()
    if not args.boot_start < args.boot_end <= len(data):
        parser.error("--boot-start/--boot-end precisam estar dentro da ROM")
    targets = external_jals(data, args.boot_start, args.boot_end, args.boot_vram)
    print("=== destinos de jal fora do boot (hipótese de boot fornecida) ===")
    print(f"chamadas: {sum(targets.values())}; destinos distintos: {len(targets)}")
    for target, count in targets.most_common(40):
        print(f"0x{target:08X}  {count:4} chamada(s)")
    if targets:
        print(f"faixa bruta: 0x{min(targets):08X}-0x{max(targets):08X}")

    print("\n=== trechos pós-boot decodificáveis (candidatos, não seções) ===")
    for start, end in valid_runs(data, args.boot_end, args.min_words):
        print(f"rom 0x{start:08X}-0x{end:08X}  ({(end - start) / 1024:.1f} KiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
