"""Verifica se um endereco e alvo de `jal` dentro do segmento boot.

Antes de fundir duas funcoes vizinhas nos simbolos e preciso saber se a segunda
e realmente chamada por alguem: fundir um alvo de `jal` quebraria toda chamada
direta a ele. Se ninguem chama, o limite veio de um `jr $ra` interno e a fusao e
segura.
"""
import struct
import sys
from pathlib import Path

ROM_START = 0x001000
ROM_END = 0x0D7770
VRAM_START = 0x80000400

rom = Path(sys.argv[1]).read_bytes()
targets = [int(a, 16) for a in sys.argv[2:]]

hits = {t: [] for t in targets}
for off in range(ROM_START, ROM_END, 4):
    word = struct.unpack_from(">I", rom, off)[0]
    if word >> 26 != 3:                       # opcode 3 = jal
        continue
    dest = 0x80000000 | ((word & 0x03FFFFFF) << 2)
    if dest in hits:
        hits[dest].append(VRAM_START + off - ROM_START)

for t in targets:
    where = hits[t]
    print("0x%08X: %d chamada(s) jal%s" % (
        t, len(where), (" -> " + ", ".join("0x%08X" % w for w in where[:8])) if where else ""))
