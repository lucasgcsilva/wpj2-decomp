"""Compara as entradas da primeira tarefa de áudio divergente.

A captura local contém a RDRAM lógica anterior à tarefa. O oráculo Project64
contém os bytes vistos por cada LOADBUFF/LOADADPCM. Entradas que já haviam sido
sobrescritas por SAVEBUFF na própria AList são marcadas como dinâmicas e não
são comparadas contra o snapshot inicial.
"""
from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path


ROOT = Path(r"E:\projetos\project-wonder-j2-decomp")
PROBE_DEFAULT = ROOT / "temp/projeto/testar/audio_rsp_exato/first_divergence"
ORACLE_DEFAULT = ROOT / "analise/oraculo/audio/deep/tasks/task_000058"
INPUT_FILE = re.compile(
    r"^(load|book)_cmd(\d+)(?:_dmem[0-9A-Fa-f]+)?_([0-9A-Fa-f]{8})\.bin$"
)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def diff_stats(left: bytes, right: bytes) -> tuple[int, int | None]:
    limit = min(len(left), len(right))
    positions = [index for index in range(limit) if left[index] != right[index]]
    different = len(positions) + abs(len(left) - len(right))
    first = positions[0] if positions else (limit if len(left) != len(right) else None)
    return different, first


def saved_before_commands(alist: bytes) -> dict[int, list[tuple[int, int]]]:
    """Retorna intervalos SAVEBUFF anteriores a cada índice de comando."""
    saved: list[tuple[int, int]] = []
    result: dict[int, list[tuple[int, int]]] = {}
    count = 0
    for command in range(len(alist) // 8):
        result[command] = list(saved)
        w0 = int.from_bytes(alist[command * 8:command * 8 + 4], "big")
        w1 = int.from_bytes(alist[command * 8 + 4:command * 8 + 8], "big")
        opcode = w0 >> 24
        if opcode == 8:
            count = w1 & 0xFFFF
        elif opcode == 6 and count:
            address = w1 & 0x1FFFFFFF
            saved.append((address, address + count))
    return result


def overlaps(start: int, end: int, intervals: list[tuple[int, int]]) -> bool:
    return any(start < other_end and end > other_start for other_start, other_end in intervals)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, default=PROBE_DEFAULT)
    parser.add_argument("--oracle", type=Path, default=ORACLE_DEFAULT)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    suspects = sorted(args.probe.glob("suspect_task_*"))
    if not suspects:
        raise SystemExit(f"Captura suspect_task ausente em {args.probe}")
    suspect = suspects[0]
    output = args.out or suspect / "COMPARACAO_ENTRADAS.md"
    local_alist = (suspect / "alist.bin").read_bytes()
    oracle_alist = (args.oracle / "alist.bin").read_bytes()
    if local_alist != oracle_alist:
        different, first = diff_stats(local_alist, oracle_alist)
        output.write_text(
            "# Comparação da entrada suspeita\n\n"
            "INCONCLUSIVO: as ALists não são idênticas.\n\n"
            f"- bytes diferentes: {different}\n- primeiro byte: {first}\n",
            encoding="utf-8",
        )
        print(output)
        return 2

    rdram = (suspect / "rdram_before.bin").read_bytes()
    if len(rdram) != 0x800000:
        raise SystemExit(f"RDRAM local incompleta: {len(rdram)} bytes")
    prior_saves = saved_before_commands(local_alist)

    rows: list[dict[str, object]] = []
    for candidate in sorted(args.oracle.iterdir()):
        match = INPUT_FILE.match(candidate.name)
        if not match:
            continue
        kind, command_text, address_text = match.groups()
        command = int(command_text)
        address = int(address_text, 16)
        expected = candidate.read_bytes()
        local = rdram[address:address + len(expected)]
        dynamic = kind == "load" and overlaps(
            address, address + len(expected), prior_saves.get(command, [])
        )
        different, first = diff_stats(local, expected)
        rows.append({
            "command": command,
            "kind": kind,
            "address": address,
            "bytes": len(expected),
            "local_hash": sha(local),
            "oracle_hash": sha(expected),
            "different": different,
            "first": first,
            "dynamic": dynamic,
        })

    static = [row for row in rows if not row["dynamic"]]
    static_bad = [row for row in static if row["different"]]
    dynamic = [row for row in rows if row["dynamic"]]
    first_bad = min(static_bad, key=lambda row: int(row["command"])) if static_bad else None

    lines = [
        "# Comparação da entrada suspeita",
        "",
        f"- captura local: `{suspect.name}`",
        f"- oráculo: `{args.oracle.name}`",
        f"- AList: idêntica ({len(local_alist)} bytes)",
        f"- entradas comparáveis no snapshot inicial: {len(static)}",
        f"- entradas estáticas divergentes: {len(static_bad)}",
        f"- LOADBUFF dinâmicos (dependem de SAVEBUFF anterior): {len(dynamic)}",
        "",
        "## Entradas estáticas",
        "",
        "| comando | tipo | endereço | bytes | diferenças | primeiro byte | hash local | hash Project64 |",
        "|---:|---|---|---:|---:|---:|---|---|",
    ]
    for row in static:
        first = "—" if row["first"] is None else str(row["first"])
        lines.append(
            f"| {row['command']} | {row['kind']} | `0x{row['address']:08X}` | "
            f"{row['bytes']} | {row['different']} | {first} | "
            f"`{str(row['local_hash'])[:16]}` | `{str(row['oracle_hash'])[:16]}` |"
        )
    lines += ["", "## Resultado", ""]
    if first_bad:
        lines.append(
            f"CONFIRMADO: a primeira entrada estática divergente é "
            f"{str(first_bad['kind']).upper()} no comando {first_bad['command']}, "
            f"endereço `0x{first_bad['address']:08X}`, com "
            f"{first_bad['different']} bytes diferentes."
        )
    elif dynamic:
        lines.append(
            "As entradas estáticas são idênticas. A próxima comparação deve "
            "reproduzir a AList e inspecionar o primeiro LOADBUFF dinâmico."
        )
    else:
        lines.append("Todas as entradas capturadas são idênticas.")

    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(output)
    print(
        f"static={len(static)} static_bad={len(static_bad)} "
        f"dynamic={len(dynamic)} first_bad="
        f"{first_bad['command'] if first_bad else '-'}"
    )
    return 1 if first_bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
