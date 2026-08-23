"""Aplica correções manuais validadas ao catálogo de recursos completos."""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

from processar_recursos_completos_lm import read_rows, write_rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("changes", type=Path)
    parser.add_argument("--resources", type=Path,
                        default=Path("textos/recursos_completos_en.tsv"))
    args = parser.parse_args()
    with args.changes.open(encoding="utf-8", newline="") as stream:
        changes = {row["source_en"]: row["pt_br"]
                   for row in csv.DictReader(stream, delimiter="\t")}
    rows = read_rows(args.resources)
    applied = 0
    for row in rows:
        if row["source_en"] in changes:
            row["pt_br"] = changes[row["source_en"]]
            row["status"] = "reviewed_manual"
            applied += 1
    missing = set(changes) - {row["source_en"] for row in rows}
    if missing:
        raise SystemExit(f"fontes ausentes: {sorted(missing)}")
    write_rows(args.resources, rows)
    print(f"correções aplicadas={applied}")


if __name__ == "__main__":
    main()
