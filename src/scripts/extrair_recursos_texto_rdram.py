"""Extrai recursos textuais completos de um dump de RDRAM do WPJ2.

O patch inglês separa registros com 01 01 e usa E0/E1/E2 + argumento como
controles internos. A extração ASCII antiga quebrava uma fala nesses controles
e em quebras de linha; esta ferramenta produz a mesma chave normalizada que o
runtime usa para procurar o TSV PT-BR.
"""
from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path


CONTROL_BYTES = {0xE0, 0xE1, 0xE2}
MAX_RAW = 768


def decode_record(data: bytes, start: int) -> tuple[str, int, str] | None:
    chars: list[str] = []
    controls: list[str] = []
    pos = start
    end = min(len(data), start + MAX_RAW)
    while pos < end:
        value = data[pos]
        if value == 0:
            text = "".join(chars).rstrip("\r\n")
            letters = sum(ch.isalpha() for ch in text)
            if len(text) >= 4 and letters >= 3:
                return text, pos - start, " ".join(controls)
            return None
        if value in CONTROL_BYTES:
            if pos + 1 >= end:
                return None
            argument = data[pos + 1]
            size = 4 if value == 0xE2 and argument == 0x06 else 2
            if pos + size > end:
                return None
            controls.append(
                f"{len(chars)}:" + "".join(f"{byte:02X}" for byte in data[pos:pos + size])
            )
            pos += size
            continue
        if value in (0x0A, 0x0D) or 0x20 <= value <= 0x7E:
            chars.append(chr(value))
            pos += 1
            continue
        return None
    return None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("dump", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--grouped-out", type=Path)
    parser.add_argument("--min-address", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--translations", type=Path)
    args = parser.parse_args()

    data = args.dump.read_bytes()
    rows: list[tuple[int, int, str, str]] = []
    seen: set[tuple[int, str]] = set()
    for marker in range(1, len(data) - 3):
        # Há registros iniciados por 01 e outros por 01 01. Em ambos os casos
        # o marcador vem imediatamente depois do NUL do recurso anterior.
        if data[marker] != 0x01 or data[marker - 1] != 0:
            continue
        start = marker
        while start < len(data) and data[start] == 0x01:
            start += 1
        decoded = decode_record(data, start)
        if decoded is None:
            continue
        text, raw_len, controls = decoded
        key = (start, text)
        if key not in seen:
            seen.add(key)
            rows.append((start, raw_len, text, controls))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(("rdram_phys", "raw_len", "source_en", "controls"))
        for address, raw_len, text, controls in rows:
            writer.writerow((f"0x{address:06X}", raw_len, text.replace("\n", "\\n"), controls))
    print(f"{len(rows)} recursos completos -> {args.out}")

    if args.grouped_out:
        translations: dict[str, str] = {}
        if args.translations:
            with args.translations.open(encoding="utf-8", newline="") as stream:
                for row in csv.DictReader(stream, delimiter="\t"):
                    translations[row["source_en"].replace("\\n", "\n")] = row["pt_br"]
        grouped: dict[str, list[tuple[int, int, str]]] = defaultdict(list)
        for address, raw_len, text, controls in rows:
            if address >= args.min_address:
                grouped[text].append((address, raw_len, controls))
        args.grouped_out.parent.mkdir(parents=True, exist_ok=True)
        with args.grouped_out.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
            writer.writerow(("source_en", "pt_br", "rdram_offsets", "occurrences",
                             "min_raw_len", "controls", "status"))
            for text, occurrences in grouped.items():
                translated = translations.get(text, "")
                status = "translated" if translated else "missing"
                if not translated and "\n" in text:
                    parts = text.split("\n")
                    translated_parts = [translations.get(part, "") for part in parts]
                    if all(translated_parts):
                        translated = "\\n".join(translated_parts)
                        status = "composed_lines"
                writer.writerow((
                    text.replace("\n", "\\n"), translated,
                    ";".join(f"0x{address:06X}" for address, _, _ in occurrences),
                    len(occurrences), min(raw_len for _, raw_len, _ in occurrences),
                    " | ".join(dict.fromkeys(control for _, _, control in occurrences if control)),
                    status,
                ))
        print(f"{len(grouped)} fontes únicas -> {args.grouped_out}")


if __name__ == "__main__":
    main()
