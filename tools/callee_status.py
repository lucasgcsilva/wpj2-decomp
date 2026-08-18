"""Para uma funcao dada, mostra cada destino de `jal` na ordem em que aparece no
codigo, marcando quais executaram.

LIMITACAO IMPORTANTE: o traco e por *funcao*, nao por *local de chamada*. Uma
funcao aparece como "ok" se executou em qualquer lugar do programa, mesmo que a
chamada listada aqui nunca tenha acontecido. Foi exatamente o que confundiu a
primeira leitura de func_80000C90: `func_800B1B04` marcava "ok" com 7 execucoes
vindas de outro chamador, enquanto o bloco em linha reta que a chama ali nunca
rodou. Trechos em linha reta com "ok" e "--" alternados sao o sintoma desse
falso positivo, nao um despachante.

Distinguir de verdade exigiria um contador por instrucao `jal`. Ate la, use esta
saida como pista, e confirme no desassemblador.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

ROM_START = 0x001000
VRAM_START = 0x80000400

proj = Path(__file__).resolve().parent.parent

executed = set()
for line in (proj / "executadas.txt").read_text(encoding="utf-8").splitlines():
    if line.startswith("#") or not line.strip():
        continue
    executed.add(int(line.split()[0], 16))

funcs = {}
vram, named = None, False
for line in (proj / "wpj2.syms.toml").read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if line.startswith('name = "func_'):
        named = True
    elif line.startswith("name ="):
        named = False
    elif line.startswith("vram = 0x") and named:
        vram = int(line.split("0x")[1], 16)
    elif line.startswith("size = 0x") and vram is not None:
        funcs[vram] = int(line.split("0x")[1], 16)
        vram = None


rom = None
for cand in Path("E:/projetos/n64-roms").glob("Wonder Project J2*.z64"):
    rom = cand.read_bytes()
    break
if rom is None:
    raise SystemExit("ROM nao encontrada em E:/projetos/n64-roms")

target = int(sys.argv[1].replace("func_", ""), 16)
size = funcs.get(target)
if size is None:
    raise SystemExit("func_%08X nao esta na tabela de simbolos" % target)

off = ROM_START + target - VRAM_START
seq = []
for i in range(0, size, 4):
    w = struct.unpack_from(">I", rom, off + i)[0]
    if w >> 26 == 3:                          # jal
        seq.append((target + i, 0x80000000 | ((w & 0x03FFFFFF) << 2)))

vistos, faltando = 0, 0
primeiro_nao = None
print("func_%08X: %d chamadas `jal` no corpo\n" % (target, len(seq)))
for k, (where, dest) in enumerate(seq):
    ok = dest in executed
    if ok:
        vistos += 1
    else:
        faltando += 1
        if primeiro_nao is None:
            primeiro_nao = (k, where, dest)
    print("  %3d  0x%08X -> func_%08X  %s" % (k, where, dest, "ok" if ok else "--"))

print("\nalcancados %d, nao alcancados %d" % (vistos, faltando))
if primeiro_nao:
    k, where, dest = primeiro_nao
    depois = sum(1 for _, d in seq[k:] if d in executed)
    print("primeiro nao alcancado: chamada %d, em 0x%08X -> func_%08X" % (k, where, dest))
    print("depois dele ainda ha %d chamada(s) que executaram" % depois)
    print("=> %s" % ("prefixo: a funcao parou no meio de uma sequencia"
                     if depois == 0 else
                     "alternado - pode ser despachante, mas confirme no "
                     "desassemblador: o traco nao distingue local de chamada"))
