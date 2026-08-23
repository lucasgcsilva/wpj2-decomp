"""Consolida um banco estável de recursos e capturas dinâmicas adicionais.

Recursos do banco limpo têm prioridade por endereço. Entradas adicionais só
são incorporadas quando nenhum de seus endereços já existe no banco estável;
isso evita reimportar texto que o próprio runtime de tradução modificou.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path


FIELDS = ("source_en", "pt_br", "rdram_offsets", "occurrences",
          "min_raw_len", "controls", "status")


def load(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if rows and tuple(rows[0]) != FIELDS:
        raise SystemExit(f"Cabeçalho inesperado em {path}")
    return rows


def translations(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines()[1:]:
        if "\t" in line:
            source, pt_br = line.split("\t", 1)
            if source and pt_br:
                result[source] = pt_br
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("stable", type=Path)
    parser.add_argument("--extra", type=Path, action="append", default=[])
    parser.add_argument("--extra-source", action="append", default=[],
                        help="Fonte dinâmica confirmada que pode vir de --extra")
    parser.add_argument("--translations", type=Path,
                        default=Path("textos/traducao_ptbr.tsv"))
    parser.add_argument("--out", type=Path,
                        default=Path("textos/recursos_completos_en.tsv"))
    args = parser.parse_args()

    rows = load(args.stable)
    occupied = {offset for row in rows for offset in row["rdram_offsets"].split(";")}
    additions = 0
    allowed_extras = set(args.extra_source)
    for extra_path in args.extra:
        for row in load(extra_path):
            if row["source_en"] not in allowed_extras:
                continue
            offsets = set(row["rdram_offsets"].split(";"))
            if offsets & occupied:
                continue
            rows.append(row)
            occupied.update(offsets)
            additions += 1

    known = translations(args.translations)
    for row in rows:
        if row["source_en"] in known:
            row["pt_br"] = known[row["source_en"]]
            row["status"] = "translated"
        elif not row["pt_br"]:
            row["status"] = "missing"
    rows.sort(key=lambda row: min(int(value, 16)
                                  for value in row["rdram_offsets"].split(";")))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t",
                                lineterminator="\n", quoting=csv.QUOTE_MINIMAL)
        writer.writeheader()
        writer.writerows(rows)
    print(f"estáveis={len(rows) - additions}; dinâmicos adicionados={additions}; total={len(rows)}")


if __name__ == "__main__":
    main()
