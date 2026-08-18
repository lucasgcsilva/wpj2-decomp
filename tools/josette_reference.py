"""Compara uma TLUT observada com a referencia de paletas do projeto josette.

O extrator de Ruin0x11 localiza 96 paletas RGB5551 na ROM, a partir de
0x47F9A0. Este programa reaproveita somente esse mapa, sem extrair ou incluir
assets da ROM: ele responde se os 64 primeiros indices que o RDP usou parecem
pertencer a uma das paletas documentadas.
"""
from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path

PALETTE_BASE = 0x47F9A0
PALETTE_BYTES = 0x200
PALETTE_COUNT = 0x60


def words(blob: bytes) -> list[int]:
    return [int.from_bytes(blob[i:i + 2], "big") for i in range(0, len(blob) - 1, 2)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("tlut", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()

    rom = args.rom.read_bytes()
    tlut = args.tlut.read_bytes() if args.tlut.exists() else b""
    L: list[str] = ["# Referencia de paletas (josette)", ""]
    if PALETTE_BASE + PALETTE_COUNT * PALETTE_BYTES > len(rom):
        raise SystemExit("ROM menor que a area de paletas documentada pelo josette")

    palettes = [words(rom[PALETTE_BASE + i * PALETTE_BYTES:
                          PALETTE_BASE + (i + 1) * PALETTE_BYTES])
                for i in range(PALETTE_COUNT)]
    c10 = Counter(p[0x10] for p in palettes)
    L += [
        "O mapa vem de `tools/josette/src/obj.rs`: 96 paletas RGB5551 de 256 entradas.",
        "", "## Indice 0x10 nas paletas de referencia", "",
        "| valor RGB5551 | paletas |", "|---|---:|",
    ]
    for value, count in c10.most_common(12):
        L.append("| `0x%04X` | %d |" % (value, count))

    if len(tlut) < 128:
        L += ["", "## TLUT observada", "", "- Nenhum dump de TLUT foi encontrado."]
    else:
        observed = words(tlut[:128])
        ranked = []
        for i, palette in enumerate(palettes):
            equal = sum(a == b for a, b in zip(observed, palette[:64]))
            ranked.append((equal, i, palette[0x10]))
        ranked.sort(reverse=True)
        best, index, color10 = ranked[0]
        L += ["", "## TLUT observada", "",
              "- entrada `0x10` observada: `0x%04X`" % observed[0x10],
              "- melhor paleta de referencia: **%d** — %d/64 entradas identicas; "
              "sua entrada `0x10` e `0x%04X`." % (index, best, color10),
              "", "| candidata | entradas identicas (de 64) | cor[0x10] |",
              "|---:|---:|---:|"]
        for equal, i, col in ranked[:6]:
            L.append("| %d | %d | `0x%04X` |" % (i, equal, col))
        if best == 64:
            L += ["", "A TLUT coincide exatamente com uma paleta documentada pelo extrator."]
        else:
            L += ["", "Nenhuma coincidencia completa: esta TLUT pode ser uma variante "
                  "montada em RAM, uma paleta parcial de 64 cores, ou pertencer a outro "
                  "conjunto de assets."]

    args.out.write_text("\n".join(L) + "\n", encoding="utf-8")
    print("referencia josette escrita em", args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
