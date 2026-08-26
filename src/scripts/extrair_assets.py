#!/usr/bin/env python3
"""Extrai, sem conversão destrutiva, os assets descritos no manifesto local."""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / "assets" / "manifest.json"
DEFAULT_OUTPUT = ROOT / "assets" / "generated"


def number(value: int | str) -> int:
    if isinstance(value, int):
        return value
    return int(value, 0)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extrai faixas mapeadas da ROM e gera um índice com hashes."
    )
    parser.add_argument("rom", type=Path)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--id", action="append", dest="ids",
                        help="extrai somente este id; pode ser repetido")
    parser.add_argument("--strict", action="store_true",
                        help="falha se qualquer faixa estiver fora da ROM")
    args = parser.parse_args()

    rom = args.rom.read_bytes()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    selected = set(args.ids or ())
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    results: list[dict[str, object]] = []
    failures = 0
    for asset in manifest.get("assets", []):
        asset_id = str(asset["id"])
        if selected and asset_id not in selected:
            continue
        offset = number(asset["rom_offset"])
        size = number(asset["size"])
        end = offset + size
        result: dict[str, object] = {
            "id": asset_id,
            "rom_offset": f"0x{offset:08X}",
            "size": size,
            "output": asset["output"],
        }
        if offset < 0 or size <= 0 or end > len(rom):
            result["status"] = "outside_rom"
            result["rom_bytes"] = len(rom)
            failures += 1
            results.append(result)
            continue

        data = rom[offset:end]
        destination = output / str(asset["output"])
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        result["status"] = "extracted"
        result["sha256"] = hashlib.sha256(data).hexdigest()
        results.append(result)
        print(f"{asset_id}: {size} bytes -> {destination}")

    unknown = selected - {str(a["id"]) for a in manifest.get("assets", [])}
    if unknown:
        raise SystemExit(f"ids ausentes do manifesto: {', '.join(sorted(unknown))}")

    index = {
        "schema_version": 1,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "rom_size": len(rom),
        "rom_sha256": hashlib.sha256(rom).hexdigest(),
        "manifest": str(args.manifest.resolve()),
        "assets": results,
    }
    (output / "index.json").write_text(
        json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"índice: {output / 'index.json'}")
    if failures:
        print(f"aviso: {failures} faixa(s) fora do tamanho desta ROM")
    return 1 if failures and args.strict else 0


if __name__ == "__main__":
    raise SystemExit(main())

