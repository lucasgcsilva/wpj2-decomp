"""Compara a extração bruta da ROM Ryu com o catálogo PT-BR canônico.

Uso:
  python src/scripts/auditar_cobertura_traducao.py [saida.tsv]

O relatório não afirma que todo ASCII seja texto exibido: a varredura bruta
também encontra nomes internos e pedaços de dados. Ele serve para impedir que
fragmentos legítimos, como ``blue`` e ``green``, desapareçam silenciosamente
entre a extração e a promoção para ``textos/traducao_ptbr.tsv``.
"""

from __future__ import annotations

import csv
import re
import sys
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "textos" / "legado" / "dialogos_en_extraidos.tsv"
CATALOG = ROOT / "textos" / "traducao_ptbr.tsv"
DEFAULT_OUT = ROOT / "temp" / "cobertura_traducao.tsv"
TEXT = re.compile(r"^[\x20-\x7e]+$")
TEXT_BANK_START = 0x00800000
TEXT_BANK_END = 0x00820000


def main() -> int:
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUT
    output.parent.mkdir(parents=True, exist_ok=True)

    # Não use csv.reader na entrada: aspas são caracteres exibidos pelo jogo,
    # não delimitadores CSV, e espaços nas bordas são parte estrutural das
    # frases fragmentadas.
    with CATALOG.open(encoding="utf-8") as stream:
        next(stream, None)
        catalog = {
            line.rstrip("\r\n").split("\t", 1)[0]
            for line in stream
            if "\t" in line
        }

    missing: dict[str, list[str]] = defaultdict(list)
    # O legado, ao contrário do catálogo ativo, foi gravado por csv.writer:
    # nele aspas literais estão duplicadas e precisam ser decodificadas.
    with RAW.open(encoding="utf-8", newline="") as stream:
        for row in csv.DictReader(stream, delimiter="\t"):
            offset_text = row.get("rom_offset", "")
            source = row.get("source_en", "")
            try:
                offset = int(offset_text, 16)
            except ValueError:
                continue
            if not (TEXT_BANK_START <= offset < TEXT_BANK_END):
                continue
            if source and source not in catalog and TEXT.fullmatch(source):
                missing[source].append(offset_text)

    with output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(("source_en", "ocorrencias", "rom_offsets"))
        for source, offsets in sorted(
            missing.items(), key=lambda item: (-len(item[1]), item[0].casefold())
        ):
            writer.writerow((source, len(offsets), " ".join(offsets)))

    print(f"catálogo: {len(catalog)} chaves")
    print(
        "extraídos sem chave canônica no banco "
        f"0x{TEXT_BANK_START:08X}..0x{TEXT_BANK_END - 1:08X}: {len(missing)}"
    )
    print(f"relatório: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
