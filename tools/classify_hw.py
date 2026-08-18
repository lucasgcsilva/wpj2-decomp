"""Classifica cada funcao do segmento boot pelo hardware que ela toca.

A libultra e ligada depois do jogo, entao ela ocupa o fim do segmento. Em vez de
supor onde fica a fronteira, esta varredura procura a evidencia: toda funcao que
carrega um endereco de MMIO (0xA3F00000-0xA4900000) e, por definicao, codigo de
SDK ou de driver. Onde essas funcoes comecam a aparecer e onde a libultra
comeca.

A deteccao e por par lui/(lw|sw|addiu): o MIPS forma um endereco de 32 bits em
duas instrucoes, e o `lui` sozinho ja fixa o bloco de hardware.
"""
from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

ROM_START = 0x001000
VRAM_START = 0x80000400

BLOCKS = [
    (0xA3F00000, "RDRAM"), (0xA4000000, "SP_MEM"), (0xA4040000, "SP"),
    (0xA4080000, "SP_PC"), (0xA4100000, "DPC"), (0xA4200000, "DPS"),
    (0xA4300000, "MI"), (0xA4400000, "VI"), (0xA4500000, "AI"),
    (0xA4600000, "PI"), (0xA4700000, "RI"), (0xA4800000, "SI"),
]

SYM = re.compile(r'^name = "func_([0-9A-F]{8})"|^vram = 0x([0-9A-F]+)|^size = 0x([0-9A-F]+)')


def block_for(addr: int) -> str | None:
    best = None
    for base, name in BLOCKS:
        if addr >= base and (best is None or base > best[0]):
            best = (base, name)
    if best and addr < best[0] + 0x100000:
        return best[1]
    return None


def load_functions(syms: Path) -> list[tuple[int, int]]:
    # O cabecalho [[section]] tambem tem vram e size; so contam os blocos que
    # comecam com um nome de funcao, senao a secao inteira entra como funcao.
    funcs, vram, named = [], None, False
    for line in syms.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith('name = "func_'):
            named = True
        elif line.startswith("name ="):
            named = False
        elif line.startswith("vram = 0x") and named:
            vram = int(line.split("0x")[1], 16)
        elif line.startswith("size = 0x") and vram is not None:
            funcs.append((vram, int(line.split("0x")[1], 16)))
            vram = None
    return [f for f in funcs if f[0] >= VRAM_START]


def main() -> int:
    rom = Path(sys.argv[1]).read_bytes()
    funcs = load_functions(Path(sys.argv[2]))
    out = Path(sys.argv[3])

    rows = []
    for vram, size in funcs:
        off = ROM_START + (vram - VRAM_START)
        touched: dict[str, int] = {}
        for i in range(0, size, 4):
            if off + i + 4 > len(rom):
                break
            w = struct.unpack_from(">I", rom, off + i)[0]
            if w >> 26 != 0x0F:                     # lui
                continue
            addr = (w & 0xFFFF) << 16
            name = block_for(addr)
            if name:
                touched[name] = touched.get(name, 0) + 1
        if touched:
            rows.append((vram, size, touched))

    first = rows[0][0] if rows else 0
    below = sum(1 for v, _ in funcs if v < first)
    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# funcao      tam    blocos de hardware referenciados\n")
        for vram, size, t in rows:
            f.write("func_%08X  0x%-5X %s\n" % (
                vram, size, " ".join("%s x%d" % (k, n) for k, n in sorted(t.items()))))

    print("funcoes no segmento boot        : %d" % len(funcs))
    print("funcoes que tocam MMIO          : %d" % len(rows))
    print("primeira delas                  : 0x%08X" % first)
    print("funcoes inteiramente abaixo dela: %d (candidatas a codigo de jogo)" % below)
    print("escrevi %s" % out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
