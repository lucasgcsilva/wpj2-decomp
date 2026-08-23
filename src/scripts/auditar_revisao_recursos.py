"""Aponta revisões PT-BR que precisam voltar ao modelo mais forte."""
from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

from processar_recursos_completos_lm import FIELDS, read_rows, runtime_length, visible_capacity, write_rows


ENGLISH = {
    "the", "and", "or", "but", "of", "to", "from",
    "with", "is", "are", "was", "were", "be", "been", "this",
    "that", "these", "those", "you", "your", "we", "our", "they",
    "she", "it", "what", "where", "when", "why", "how", "please",
    "hello", "sorry", "doctor", "girl", "boy", "island", "empire", "scene",
    "select", "save", "load", "game", "music", "voice",
    "production", "animation", "art", "character", "system",
    "confirmed", "heading", "target", "coming", "want", "could", "would",
    "will", "have", "has", "does", "listen", "look", "back", "away",
    "about", "into", "before", "after", "again", "still", "only", "all",
}
PRESERVED = {
    "josette", "corlo", "messala", "magiteka", "gijin", "proton", "seaba",
    "bird", "j2", "pokko", "karen", "gante", "gourmen", "pearl", "katze",
    "harben", "geppetto", "onee", "chan", "san", "spi1",
}
PRESERVED_SPELLING = (
    "Josette", "Corlo", "Messala", "Magiteka", "Gijin", "Proton", "Seaba",
    "Bird", "J2", "Pokko", "Karen", "Gante", "Gourmen", "Pearl", "Katze",
    "Harben", "Geppetto", "Onee-chan",
)


def words(text: str) -> list[str]:
    return [word.lower() for word in re.findall(r"[A-Za-z]+", text)]


def reasons(row: dict[str, str], check_runtime_limit: bool) -> list[str]:
    source = row["source_en"]
    translated = row["pt_br"]
    found: list[str] = []
    if not translated.strip():
        found.append("vazio")
    if check_runtime_limit and runtime_length(translated) > visible_capacity(row):
        found.append(f"longo:{runtime_length(translated)}>{visible_capacity(row)}")
    if "�" in translated or re.search(r"Ã[©£ªº]", translated):
        found.append("mojibake")
    if re.search(r"\bSilconians?\b|\bSiliconians?\b", translated, re.I):
        found.append("glossario")
    missing_names = [name for name in PRESERVED_SPELLING
                     if re.search(rf"\b{re.escape(name)}\b", source) and
                     not re.search(rf"\b{re.escape(name)}\b", translated)]
    if missing_names:
        found.append("nomes:" + ",".join(missing_names))
    residual = sorted({word for word in words(translated)
                       if word in ENGLISH and word not in PRESERVED})
    if residual:
        found.append("ingles:" + ",".join(residual))
    if source.casefold() == translated.casefold():
        source_words = {word for word in words(source) if word not in PRESERVED}
        if source_words & ENGLISH:
            found.append("igual_ao_original")
    source_length = runtime_length(source)
    translated_length = runtime_length(translated)
    if translated_length > max(source_length * 2, source_length + 50):
        found.append(f"expansao_suspeita:{translated_length}>{source_length}")
    if translated.count("\\n") > source.count("\\n") + 2:
        found.append("quebras_extras")
    return found


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--resources", type=Path,
                        default=Path("textos/recursos_completos_en.tsv"))
    parser.add_argument("--out", type=Path,
                        default=Path("textos/revisao_lm_auditoria.tsv"))
    parser.add_argument("--reopen", action="store_true")
    parser.add_argument("--check-runtime-limit", action="store_true")
    args = parser.parse_args()

    rows = read_rows(args.resources)
    flagged: list[tuple[str, str, str]] = []
    for row in rows:
        if row["status"] not in {"reviewed_lm", "reviewed_manual"}:
            continue
        row_reasons = reasons(row, args.check_runtime_limit)
        if row_reasons:
            flagged.append((row["source_en"], row["pt_br"], ";".join(row_reasons)))
            if args.reopen:
                row["status"] = "raw_lm"
    with args.out.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(("source_en", "pt_br", "reasons"))
        writer.writerows(flagged)
    if args.reopen:
        write_rows(args.resources, rows)
    print(f"revisadas auditadas={sum(row['status'] == 'reviewed_lm' for row in rows)}; "
          f"sinalizadas={len(flagged)}; reabertas={len(flagged) if args.reopen else 0}")


if __name__ == "__main__":
    main()
