"""Quem toca estes enderecos? Varredura da ROM inteira, distribuida por processo.

Uma thread parada numa fila de mensagens so volta a rodar quando alguem escreve
nela. Descobrir *quem* e uma pergunta estatica: no MIPS um endereco de 32 bits e
formado por um par `lui` + (`addiu`|`ori`), ou por um `lui` seguido de um acesso
com deslocamento. Procuramos as tres formas em todas as funcoes de uma vez.

Uso:
    xref_addr.py <rom> <syms> 800F9C20 8010A230 ...
"""
from __future__ import annotations

import os
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

ROM_START = 0x001000
VRAM_START = 0x80000400

LOADS = {0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37}
STORES = {0x28, 0x29, 0x2B, 0x3F}


def s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def scan(job):
    """Procura os enderecos alvo dentro de uma funcao.

    Rastreia o valor constante de cada registrador formado por `lui`, e resolve
    tres formas: `lui`+`addiu`, `lui`+`ori` e `lui` seguido de acesso com
    deslocamento. Devolve, para cada alvo encontrado, o VRAM da instrucao e se
    foi leitura, escrita ou apenas formacao de endereco."""
    vram, size, blob, targets = job
    hits = []
    known = {}

    for i in range(0, min(size, len(blob)), 4):
        w = struct.unpack_from(">I", blob, i)[0]
        op = w >> 26
        rs, rt = (w >> 21) & 0x1F, (w >> 16) & 0x1F
        imm = w & 0xFFFF
        here = vram + i

        if op == 0x0F:                                   # lui
            known[rt] = (imm << 16) & 0xFFFFFFFF
        elif op == 0x09 and rs in known:                 # addiu
            v = (known[rs] + s16(imm)) & 0xFFFFFFFF
            known[rt] = v
            if v in targets:
                hits.append((here, v, "endereco"))
        elif op == 0x0D and rs in known:                 # ori
            v = (known[rs] | imm) & 0xFFFFFFFF
            known[rt] = v
            if v in targets:
                hits.append((here, v, "endereco"))
        elif (op in LOADS or op in STORES) and rs in known:
            v = (known[rs] + s16(imm)) & 0xFFFFFFFF
            if v in targets:
                hits.append((here, v, "leitura" if op in LOADS else "escrita"))
        elif op in (0x02, 0x03):
            known.clear()

    return (vram, hits)


def load_functions(syms: Path):
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
    return funcs


def main() -> int:
    rom = Path(sys.argv[1]).read_bytes()
    funcs = load_functions(Path(sys.argv[2]))
    targets = frozenset(int(a, 16) for a in sys.argv[3:])

    jobs = [(v, s, rom[ROM_START + v - VRAM_START: ROM_START + v - VRAM_START + s],
             targets) for v, s in funcs]

    workers = os.cpu_count() or 4
    print("procurando %d endereco(s) em %d funcoes, %d processos\n"
          % (len(targets), len(jobs), workers))
    with ProcessPoolExecutor(max_workers=workers) as pool:
        results = [r for r in pool.map(scan, jobs, chunksize=64) if r[1]]

    by_target = {}
    for vram, hits in results:
        for where, target, kind in hits:
            by_target.setdefault(target, []).append((vram, where, kind))

    for target in sorted(targets):
        rows = by_target.get(target, [])
        print("0x%08X: %d referencia(s)" % (target, len(rows)))
        for vram, where, kind in rows:
            print("   func_%08X em 0x%08X  (%s)" % (vram, where, kind))
        if not rows:
            print("   nenhuma - o endereco nao e formado por nenhum par lui/addiu")
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
