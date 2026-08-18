"""Extrai cadeias ASCII da ROM e cria um catalogo externo para localizacao.

Nao altera a ROM. Os offsets permitem associar cada cadeia ao recurso original
quando o pipeline de texto for conectado ao runtime recompilado.
"""
from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


PRINTABLE = re.compile(rb"[\x20-\x7e]{3,}")


def is_text_candidate(text: str) -> bool:
    if len(text) < 4 or len(text) > 180:
        return False
    if not re.fullmatch(r"[A-Za-z0-9 .,!?;:'\"()\-_/&]+", text):
        return False
    letters = sum(ch.isalpha() for ch in text)
    lowercase = sum(ch.islower() for ch in text)
    # Codigo MIPS e dados comprimidos produzem falsos positivos curtos, quase
    # sempre sem minusculas. Falas, nomes e mensagens da traducao inglesa usam
    # pelo menos duas minusculas; o caso com espaco preserva titulos em caixa alta.
    return letters >= 3 and (lowercase >= 2 or (" " in text and letters >= 5))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()

    data = args.rom.read_bytes()
    entries = []
    seen: dict[str, int] = {}
    for match in PRINTABLE.finditer(data):
        raw = match.group().decode("ascii")
        if not is_text_candidate(raw):
            continue
        ordinal = seen.get(raw, 0)
        seen[raw] = ordinal + 1
        entries.append({
            "id": f"rom_{match.start():08X}_{ordinal:02d}",
            "rom_offset": f"0x{match.start():08X}",
            "source_en": raw,
            "pt_br": "",
            "notes": "",
        })

    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "dialogos_en_extraidos.json").write_text(
        json.dumps({"format": 1, "language": "en", "entries": entries}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    with (args.out / "dialogos_en_extraidos.tsv").open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["id", "rom_offset", "source_en", "pt_br", "notes"], delimiter="\t")
        writer.writeheader()
        writer.writerows(entries)
    print(f"{len(entries)} cadeias exportadas para {args.out}")


if __name__ == "__main__":
    main()
