"""Gera catálogo PT-BR externo a partir da ROM inglesa de Ryu V1.0.

Não altera ROM, executável ou recursos gráficos.
"""
from __future__ import annotations

import argparse
import binascii
import csv
import json
import re
import unicodedata
from pathlib import Path


PATCHED_ROM_CRC32 = 0xE1094E29
PATCHED_ROM_SIZE = 8_912_896
TEXT_BANK_START = 0x00800000
# O último texto ASCII do patch termina antes de 0x00820000. A região seguinte
# contém recursos comprimidos que por acaso formam pequenos trechos ASCII e
# geravam falsos positivos. A sonda de DMA confirmou, por exemplo, que
# 0x008249F0 é carregado como bloco binário de 80.880 bytes.
TEXT_BANK_END = 0x00820000
PRINTABLE = re.compile(rb"[\x20-\x7e]{3,}")
WORDS = re.compile(r"[A-Za-z]+")

GLOSSARY = {
    "Silconian": "Silconiano", "Siliconian": "Siliconiano", "Messala": "Messala",
    "Josette": "Josette", "Corlo": "Corlo", "Magiteka": "Magiteka",
    "Gijin": "Gijin", "Proton": "Proton", "Seaba": "Seaba", "Bird": "Bird", "J2": "J2",
}


def ascii_fallback(value: str) -> str:
    normalized = unicodedata.normalize("NFKD", value)
    normalized = "".join(ch for ch in normalized if not unicodedata.combining(ch))
    return normalized.replace("—", "-").replace("–", "-").replace("…", "...")


def confidence(text: str) -> str | None:
    """Separa fala provável de bytes comprimidos que parecem ASCII."""
    if len(text) > 180 or not re.fullmatch(r"[A-Za-z0-9 .,!?;:'\"()\-_/&]+", text):
        return None
    words = WORDS.findall(text)
    letters = sum(map(len, words))
    if letters < 3:
        return None
    lower = sum(ch.islower() for ch in text)
    has_space = " " in text.strip()
    has_punctuation = any(ch in text for ch in ".!?'")
    if len(words) >= 2 and (has_space or has_punctuation) and lower >= 1:
        return "high"
    if len(words) == 1 and (has_punctuation or text in GLOSSARY or text == "-san!"):
        return "review"
    if has_space and len(words) >= 2 and text.upper() == text:
        return "review"
    return None


def load_translations(path: Path) -> dict[str, str]:
    """Lê traduções revisadas; a chave é a cadeia inglesa exata da ROM."""
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != "source_en\tpt_br":
        raise SystemExit(f"Formato inválido em {path}; esperado source_en<TAB>pt_br.")
    translations: dict[str, str] = {}
    for line_number, line in enumerate(lines[1:], start=2):
        if not line:
            continue
        if "\t" not in line:
            raise SystemExit(f"Linha {line_number} sem TAB em {path}.")
        source, translated = line.split("\t", 1)
        if source and translated:
            translations[source] = translated
    return translations


def load_reviews(path: Path) -> set[str]:
    """Lê as chaves que já passaram por revisão contextual."""
    if not path.exists():
        return set()
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != "source_en\treviewed_at\tnotes":
        raise SystemExit(f"Formato inválido em {path}; esperado source_en<TAB>reviewed_at<TAB>notes.")
    reviewed: set[str] = set()
    for line_number, line in enumerate(lines[1:], start=2):
        if not line:
            continue
        parts = line.split("\t", 2)
        if len(parts) != 3:
            raise SystemExit(f"Linha {line_number} inválida em {path}.")
        if parts[0] and parts[1]:
            reviewed.add(parts[0])
    return reviewed


def extract(rom: bytes, translations: dict[str, str], reviewed: set[str]) -> list[dict[str, str]]:
    entries: list[dict[str, str]] = []
    occurrences: dict[str, int] = {}
    for match in PRINTABLE.finditer(rom, TEXT_BANK_START, TEXT_BANK_END):
        source = match.group().decode("ascii")
        certainty = confidence(source)
        if certainty is None:
            continue
        ordinal = occurrences.get(source, 0)
        occurrences[source] = ordinal + 1
        translated = translations.get(source, "")
        stage = "reviewed" if source in reviewed else ("draft" if translated else certainty)
        entries.append({
            "id": f"rom_{match.start():08X}_{ordinal:02d}",
            "rom_offset": f"0x{match.start():08X}",
            "source_en": source,
            "pt_br": translated,
            "pt_br_ascii": ascii_fallback(translated) if translated else "",
            "status": stage,
            "notes": "" if source in reviewed else "Revisar tradução no contexto." if translated else "Revisar contexto antes de traduzir.",
        })
    return entries


def extract_supplemental(
    rom: bytes, path: Path, translations: dict[str, str], reviewed: set[str]
) -> list[dict[str, str]]:
    """Expande as fontes suplementares agrupadas para suas ocorrências na ROM."""
    if not path.exists():
        return []
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    entries: list[dict[str, str]] = []
    for row in rows:
        source = row["source_en"]
        translated = translations.get(source, row.get("pt_br", ""))
        for ordinal, offset_text in enumerate(row["rom_offsets"].split(";")):
            offset = int(offset_text, 16)
            raw = source.encode("ascii")
            if rom[offset:offset + len(raw)] != raw:
                raise SystemExit(f"Offset suplementar incompatível: {offset_text} {source!r}")
            stage = "reviewed" if source in reviewed or row.get("status") == "reviewed" else "draft"
            entries.append({
                "id": f"rom_{offset:08X}_supp_{ordinal:02d}",
                "rom_offset": f"0x{offset:08X}",
                "source_en": source,
                "pt_br": translated,
                "pt_br_ascii": ascii_fallback(translated) if translated else "",
                "status": stage,
                "category": row.get("category", "suplementar"),
                "notes": row.get("notes", ""),
            })
    return entries


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    parser.add_argument("--out", type=Path,
                        default=Path("textos/legado/dialogos_ptbr.json"))
    parser.add_argument("--translations", type=Path, default=Path("textos/traducao_ptbr.tsv"))
    parser.add_argument("--reviews", type=Path,
                        default=Path("textos/legado/revisao_ptbr.tsv"))
    parser.add_argument("--supplemental", type=Path,
                        default=Path("textos/legado/textos_suplementares_en.tsv"))
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    crc32 = binascii.crc32(rom) & 0xFFFFFFFF
    if len(rom) != PATCHED_ROM_SIZE or crc32 != PATCHED_ROM_CRC32:
        raise SystemExit(
            f"ROM incompatível: esperado Ryu V1.0 CRC32 {PATCHED_ROM_CRC32:08X}; "
            f"recebido {len(rom)} bytes, CRC32 {crc32:08X}."
        )
    translations = load_translations(args.translations)
    reviewed = load_reviews(args.reviews)
    primary_entries = extract(rom, translations, reviewed)
    supplemental_entries = extract_supplemental(rom, args.supplemental, translations, reviewed)
    entries = primary_entries + supplemental_entries
    payload = {
        "format": 2,
        "language": "pt-BR",
        "fallback_language": "en",
        "source": {
            "translation": "Wonder Project J2 English Translation V1.0 (Ryu, 2007)",
            "rom_crc32": f"{crc32:08X}",
            "text_bank": [f"0x{TEXT_BANK_START:08X}", f"0x{TEXT_BANK_END:08X}"],
            "supplemental_occurrences": len(supplemental_entries),
        },
        "rendering": {
            "preferred": "pt_br",
            "ascii_fallback": "pt_br_ascii",
            "font_status": "Acentos dependem do mapeamento futuro da fonte; use fallback ASCII enquanto isso.",
        },
        "glossary": GLOSSARY,
        "entries": entries,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    translated = sum(bool(entry["pt_br"]) for entry in entries)
    print(
        f"{len(entries)} entradas ({len(primary_entries)} principais + "
        f"{len(supplemental_entries)} suplementares); {translated} traduzidas; saída: {args.out}"
    )


if __name__ == "__main__":
    main()
