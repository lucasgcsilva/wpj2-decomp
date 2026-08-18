"""Gera símbolos sintéticos para o segmento de CPU residente do Wonder J2.

O arquivo resultante é um ponto de partida para N64Recomp. Funções são
separadas por retornos e por destinos de ``jal``; nomes reais e overlays só
serão acrescentados após validação independente.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

import rabbitizer


ROM_START = 0x001000
ROM_END = 0x0D7770
VRAM_START = 0x80000400
PADDING = {0x00000000, 0x00000025}


def decode(data: bytes) -> list[rabbitizer.Instruction]:
    return [rabbitizer.Instruction(struct.unpack_from(">I", data, offset)[0],
                                   vram=VRAM_START + offset - ROM_START)
            for offset in range(ROM_START, ROM_END, 4)]


def branch_target(instruction: rabbitizer.Instruction) -> int | None:
    try:
        return instruction.getBranchVramGeneric() if instruction.isBranch() else None
    except (RuntimeError, ValueError):
        return None


def return_boundaries(instructions: list[rabbitizer.Instruction]) -> list[int]:
    starts = [VRAM_START]
    current = VRAM_START
    furthest = VRAM_START
    index = 0
    while index < len(instructions):
        instruction = instructions[index]
        address = VRAM_START + index * 4
        target = branch_target(instruction)
        if target is not None and target > furthest:
            furthest = target
        if instruction.isJrRa():
            end = address + 8  # inclui o delay slot
            if furthest < end:
                next_index = index + 2
                while (next_index < len(instructions)
                       and instructions[next_index].getRaw() == 0
                       and (VRAM_START + next_index * 4) % 16 != 0):
                    next_index += 1
                next_start = VRAM_START + next_index * 4
                if next_start > current:
                    starts.append(next_start)
                    current = next_start
                    furthest = current
                index = next_index
                continue
            index += 2
            continue
        index += 1
    return starts


def jal_targets(instructions: list[rabbitizer.Instruction]) -> set[int]:
    return {instruction.getInstrIndexAsVram() for instruction in instructions
            if instruction.getOpcodeName() == "jal"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = args.rom.read_bytes()
    if len(data) < ROM_END:
        parser.error("a ROM é menor que o intervalo de CPU esperado")

    instructions = decode(data)
    vram_end = VRAM_START + ROM_END - ROM_START
    targets = jal_targets(instructions)
    external = sorted(target for target in targets if not VRAM_START <= target < vram_end)
    if external:
        print("recusado: há destinos jal fora do segmento residente")
        for target in external[:20]:
            print(f"  0x{target:08X}")
        return 2

    starts = sorted(set(return_boundaries(instructions)) | targets | {VRAM_START})
    functions: list[tuple[int, int]] = []
    for index, start in enumerate(starts):
        end = starts[index + 1] if index + 1 < len(starts) else vram_end
        first_word = (start - VRAM_START) // 4
        last_word = (end - VRAM_START) // 4
        if end > start and not all(instructions[i].getRaw() in PADDING
                                   for i in range(first_word, last_word)):
            functions.append((start, end))

    output = args.output
    with output.open("w", encoding="utf-8", newline="\n") as file:
        file.write("# Gerado por tools/gen_boot_symbols.py; nomes são sintéticos.\n")
        file.write("# CPU residente; RSP e overlays não estão incluídos.\n\n")
        file.write("[[section]]\nname = \"boot\"\n")
        file.write(f"rom = 0x{ROM_START:08X}\n")
        file.write(f"vram = 0x{VRAM_START:08X}\n")
        file.write(f"size = 0x{ROM_END - ROM_START:X}\n\n")
        for start, end in functions:
            file.write("[[section.functions]]\n")
            file.write(f'name = "func_{start:08X}"\n')
            file.write(f"vram = 0x{start:08X}\n")
            file.write(f"size = 0x{end - start:X}\n")

    covered = len(targets & {start for start, _ in functions})
    print(f"segmento CPU: rom 0x{ROM_START:06X}-0x{ROM_END:06X}")
    print(f"funções: {len(functions)}")
    print(f"destinos jal: {len(targets)}; alinhados: {covered}/{len(targets)}")
    print(f"símbolos: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
