"""Compara estados musicais locais com a sonda profunda do Project64.

Uso padrão após executar TESTAR.bat no perfil audio_rsp_exato:

    python src/scripts/comparar_audio_deep_local.py

As duas capturas usam bytes lógicos big-endian do N64. A captura local guarda
hashes dos estados únicos anteriores à AList; o Project64 guarda cada uso, por
isso a comparação seleciona o primeiro arquivo ``*_cmdNNNN`` do endereço e
calcula o mesmo hash.
"""
from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


ROOT = Path(r"E:\projetos\project-wonder-j2-decomp")
LOCAL_DEFAULT = ROOT / "temp/projeto/testar/audio_rsp_exato/audio_deep_local.csv"
ORACLE_DEFAULT = ROOT / "analise/oraculo/audio/deep/tasks"
OUTPUT_DEFAULT = ROOT / "temp/projeto/testar/audio_rsp_exato/audio_deep_compare.md"
ORACLE_STATE = re.compile(
    r"^(adpcm|resample|envmix)_cmd(\d+)_([0-9A-Fa-f]{8})\.bin$"
)


def first_oracle_state(directory: Path, kind: str, address: str) -> Path | None:
    matches: list[tuple[int, Path]] = []
    for candidate in directory.glob(f"{kind}_cmd*_{address}.bin"):
        match = ORACLE_STATE.match(candidate.name)
        if match:
            matches.append((int(match.group(2)), candidate))
    return min(matches, default=(0, None))[1]


def fnv1a(data: bytes) -> str:
    value = 14695981039346656037
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016X}"


def parse_states(value: str) -> list[tuple[str, str]]:
    parsed: list[tuple[str, str]] = []
    for item in value.split("/"):
        if not item:
            continue
        address, digest = item.split("=", 1)
        parsed.append((address.upper(), digest.upper()))
    return parsed


def detect_offset(local_tasks: list[dict[str, str]], oracle: Path) -> tuple[int, int]:
    oracle_hashes: dict[int, str] = {}
    for directory in oracle.glob("task_*"):
        if not directory.is_dir():
            continue
        alist = directory / "alist.bin"
        if alist.exists():
            oracle_hashes[int(directory.name.rsplit("_", 1)[-1])] = fnv1a(
                alist.read_bytes()
            )
    best_offset = 0
    best_matches = -1
    for offset in range(-200, 201):
        matches = sum(
            oracle_hashes.get(int(row["task"]) + offset) == row["alist_fnv"]
            for row in local_tasks
        )
        if matches > best_matches or (
            matches == best_matches and abs(offset) < abs(best_offset)
        ):
            best_offset = offset
            best_matches = matches
    return best_offset, best_matches


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--local", type=Path, default=LOCAL_DEFAULT)
    parser.add_argument("--oracle", type=Path, default=ORACLE_DEFAULT)
    parser.add_argument(
        "--offset", type=int,
        help="deslocamento Project64; se omitido, detecta por ALists idênticas",
    )
    parser.add_argument("--out", type=Path, default=OUTPUT_DEFAULT)
    args = parser.parse_args()

    if not args.local.exists():
        raise SystemExit(f"Traço local ausente: {args.local}")
    with args.local.open(encoding="ascii", newline="") as handle:
        local_tasks = list(csv.DictReader(handle))
    if not local_tasks:
        raise SystemExit(f"Nenhuma tarefa local em {args.local}")
    if args.offset is None:
        args.offset, anchor_matches = detect_offset(local_tasks, args.oracle)
        if anchor_matches <= 0:
            raise SystemExit(
                "Não foi possível alinhar as execuções: nenhuma AList local "
                "é idêntica ao oráculo na faixa de deslocamento ±200."
            )
        print(
            f"deslocamento detectado={args.offset:+d}; "
            f"âncoras exatas={anchor_matches}"
        )

    compared = 0
    exact_alists = 0
    missing: list[str] = []
    divergences: list[tuple[int, int, str, str, int, int]] = []
    first_by_kind: dict[str, tuple[int, int, str, int, int]] = {}

    states_compared = 0
    for local_row in local_tasks:
        local_number = int(local_row["task"])
        oracle_number = local_number + args.offset
        oracle_dir = args.oracle / f"task_{oracle_number:06d}"
        if not oracle_dir.is_dir():
            missing.append(f"local {local_number}: oráculo {oracle_number} ausente")
            continue
        compared += 1
        oracle_alist = oracle_dir / "alist.bin"
        if oracle_alist.exists() and fnv1a(oracle_alist.read_bytes()) == local_row["alist_fnv"]:
            exact_alists += 1

        for kind in ("adpcm", "resample", "envmix"):
            for address, local_digest in parse_states(local_row[kind]):
                oracle_state = first_oracle_state(oracle_dir, kind, address)
                if oracle_state is None:
                    missing.append(
                        f"local {local_number}/oráculo {oracle_number}: "
                        f"{kind} {address} ausente"
                    )
                    continue
                states_compared += 1
                oracle_digest = fnv1a(oracle_state.read_bytes())
                if local_digest != oracle_digest:
                    item = (
                        local_number, oracle_number, kind, address,
                        local_digest, oracle_digest,
                    )
                    divergences.append(item)
                    first_by_kind.setdefault(
                        kind,
                        (local_number, oracle_number, address, local_digest, oracle_digest),
                    )

    lines = [
        "# Comparação de estados de áudio ao vivo",
        "",
        f"- tarefas locais capturadas: {len(local_tasks)}",
        f"- tarefas comparadas: {compared}",
        f"- deslocamento Project64: {args.offset:+d}",
        f"- ALists byte a byte idênticas: {exact_alists}/{compared}",
        f"- estados comparados: {states_compared}",
        f"- estados divergentes: {len(divergences)}",
        "",
        "## Primeira divergência por tipo",
        "",
        "| tipo | local | Project64 | endereço | hash local | hash Project64 |",
        "|---|---:|---:|---|---|---|",
    ]
    for kind in ("adpcm", "resample", "envmix"):
        value = first_by_kind.get(kind)
        if value:
            local_number, oracle_number, address, local_digest, oracle_digest = value
            lines.append(
                f"| {kind} | {local_number} | {oracle_number} | `{address}` | "
                f"`{local_digest}` | `{oracle_digest}` |"
            )
        else:
            lines.append(f"| {kind} | — | — | — | igual | igual |")

    lines += ["", "## Primeiros estados divergentes", ""]
    for local_number, oracle_number, kind, address, local_digest, oracle_digest in divergences[:40]:
        lines.append(
            f"- local {local_number} / Project64 {oracle_number}: {kind} "
            f"`{address}`, `{local_digest}` != `{oracle_digest}`."
        )
    if missing:
        lines += ["", "## Dados ausentes", ""]
        lines.extend(f"- {item}" for item in missing[:40])

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(args.out)
    print(
        f"comparadas={compared}; alists_exatas={exact_alists}; "
        f"divergencias={len(divergences)}; ausencias={len(missing)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
