"""Converte um RVA do relatorio de falha em nome de funcao, via arquivo .map.

O runtime imprime o deslocamento da instrucao dentro do modulo justamente para
que ele sobreviva ao ASLR e case com o mapa do linker. Aqui procuramos o simbolo
de maior endereco que ainda seja menor ou igual ao RVA.
"""
import re
import sys
from pathlib import Path

LINE = re.compile(r"^\s*\S+:(?P<off>[0-9A-Fa-f]{8})\s+(?P<name>\S+)\s+(?P<va>[0-9A-Fa-f]{16})\s")

mapfile = Path(sys.argv[1])
targets = [int(a, 16) for a in sys.argv[2:]]

# O preferred base do linker; o RVA e relativo a ele.
base = 0x140000000
for line in mapfile.read_text(encoding="utf-8", errors="replace").splitlines():
    if "Preferred load address is" in line:
        base = int(line.strip().split()[-1], 16)
        break

syms = []
for line in mapfile.read_text(encoding="utf-8", errors="replace").splitlines():
    m = LINE.match(line)
    if m:
        syms.append((int(m.group("va"), 16) - base, m.group("name")))
syms.sort()

for rva in targets:
    best = None
    for addr, name in syms:
        if addr <= rva:
            best = (addr, name)
        else:
            break
    if best:
        print("RVA 0x%X -> %s (+0x%X)" % (rva, best[1], rva - best[0]))
    else:
        print("RVA 0x%X -> nenhum simbolo abaixo" % rva)
