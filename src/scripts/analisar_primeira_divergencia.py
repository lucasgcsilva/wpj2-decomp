"""Analisa a sonda passiva de continuidade RSP -> memória -> AI.

Alinha as ALists locais com a captura profunda do Project64, compara os
estados de entrada do microcódigo e distingue alteração entre tarefas de uma
provável cópia legítima entre slots (o hash já existia em outro endereço).
"""
from __future__ import annotations

import argparse
import csv
import re
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(r"E:\projetos\project-wonder-j2-decomp")
LOCAL_DEFAULT = ROOT / "temp/projeto/testar/audio_rsp_exato/first_divergence"
ORACLE_DEFAULT = ROOT / "analise/oraculo/audio/deep/tasks"
OUTPUT_DEFAULT = LOCAL_DEFAULT / "RELATORIO.md"
ORACLE_STATE = re.compile(
    r"^(adpcm|resample|envmix)_cmd(\d+)_([0-9A-Fa-f]{8})\.bin$"
)


def fnv1a(data: bytes) -> str:
    value = 14695981039346656037
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016X}"


def oracle_alists(root: Path) -> dict[int, str]:
    result: dict[int, str] = {}
    for directory in root.glob("task_*"):
        alist = directory / "alist.bin"
        if directory.is_dir() and alist.exists():
            result[int(directory.name.rsplit("_", 1)[-1])] = fnv1a(
                alist.read_bytes()
            )
    return result


def first_oracle_state(directory: Path, kind: str, address: str) -> Path | None:
    matches: list[tuple[int, Path]] = []
    for candidate in directory.glob(f"{kind}_cmd*_{address}.bin"):
        match = ORACLE_STATE.match(candidate.name)
        if match:
            matches.append((int(match.group(2)), candidate))
    return min(matches, default=(0, None))[1]


def detect_offset(tasks: dict[int, str], oracle: dict[int, str]) -> tuple[int, int]:
    best = (0, -1)
    for offset in range(-500, 501):
        matches = sum(
            oracle.get(task + offset) == digest for task, digest in tasks.items()
        )
        if matches > best[1] or (matches == best[1] and abs(offset) < abs(best[0])):
            best = (offset, matches)
    return best


def load_oracle_states(root: Path) -> dict[int, dict[tuple[str, str], str]]:
    """Carrega somente o primeiro uso de cada estado em cada tarefa."""
    result: dict[int, dict[tuple[str, str], tuple[int, str]]] = {}
    for directory in root.glob("task_*"):
        if not directory.is_dir():
            continue
        task = int(directory.name.rsplit("_", 1)[-1])
        current: dict[tuple[str, str], tuple[int, str]] = {}
        for candidate in directory.glob("*.bin"):
            match = ORACLE_STATE.match(candidate.name)
            if not match:
                continue
            key = (match.group(1), match.group(3).upper())
            command = int(match.group(2))
            if key not in current or command < current[key][0]:
                current[key] = (command, fnv1a(candidate.read_bytes()))
        result[task] = {key: value[1] for key, value in current.items()}
    return result


def detect_state_offset(
    grouped: dict[int, dict[tuple[str, str], str]],
    oracle: dict[int, dict[tuple[str, str], str]],
) -> tuple[int, int, list[tuple[int, int, int, int]]]:
    """Procura um deslocamento pela impressão dos estados, sem exigir AList igual."""
    mappings: list[tuple[int, int, int, int]] = []
    votes: dict[int, int] = defaultdict(int)
    for local_task, local_states in grouped.items():
        best: tuple[int, int, int] | None = None
        for oracle_task, oracle_states in oracle.items():
            shared = set(local_states) & set(oracle_states)
            exact = sum(local_states[key] == oracle_states[key] for key in shared)
            candidate = (exact, len(shared), oracle_task)
            if best is None or candidate > best:
                best = candidate
        if best is None:
            continue
        exact, shared, oracle_task = best
        mappings.append((local_task, oracle_task, exact, shared))
        if exact >= 3 and exact * 2 >= max(1, len(local_states)):
            votes[oracle_task - local_task] += 1
    if not votes:
        return 0, 0, mappings
    offset, count = max(votes.items(), key=lambda item: (item[1], -abs(item[0])))
    return offset, count, mappings


def opcode_counts(path: Path) -> Counter[int]:
    data = path.read_bytes()
    return Counter(data[0::8])


def detect_structural_offset(
    probe: Path, oracle_root: Path
) -> tuple[int, int, int]:
    """Alinha ALists brutas pela contagem de opcodes, ignorando operandos."""
    local: dict[int, Counter[int]] = {}
    for alist in probe.glob("div_*/alist.bin"):
        parts = alist.parent.name.split("_")
        try:
            task = int(parts[3])
        except (IndexError, ValueError):
            continue
        local.setdefault(task, opcode_counts(alist))
    for alist in probe.glob("baseline_task_*/alist.bin"):
        try:
            task = int(alist.parent.name.rsplit("_", 1)[-1])
        except ValueError:
            continue
        local.setdefault(task, opcode_counts(alist))
    oracle: dict[int, Counter[int]] = {}
    for alist in oracle_root.glob("task_*/alist.bin"):
        oracle[int(alist.parent.name.rsplit("_", 1)[-1])] = opcode_counts(alist)
    if len(local) < 2:
        return 0, 0, 0
    best: tuple[int, int, int] | None = None
    for offset in range(-500, 501):
        pairs = [(value, oracle.get(task + offset)) for task, value in local.items()]
        pairs = [(left, right) for left, right in pairs if right is not None]
        if len(pairs) != len(local):
            continue
        distance = sum(
            sum(abs(left[key] - right[key]) for key in set(left) | set(right))
            for left, right in pairs
        )
        candidate = (distance, abs(offset), offset)
        if best is None or candidate < best:
            best = candidate
    return (best[2], best[0], len(local)) if best else (0, 0, 0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, default=LOCAL_DEFAULT)
    parser.add_argument("--oracle", type=Path, default=ORACLE_DEFAULT)
    parser.add_argument("--out", type=Path, default=OUTPUT_DEFAULT)
    args = parser.parse_args()

    state_csv = args.probe / "state_continuity.csv"
    pcm_csv = args.probe / "pcm_lifetime.csv"
    with state_csv.open(encoding="ascii", newline="") as handle:
        states = list(csv.DictReader(handle))
    with pcm_csv.open(encoding="ascii", newline="") as handle:
        pcm = list(csv.DictReader(handle))

    local_tasks: dict[int, str] = {}
    grouped_states: dict[int, dict[tuple[str, str], str]] = defaultdict(dict)
    for row in states:
        task = int(row["task"])
        local_tasks[task] = row["alist_fnv"].upper()
        grouped_states[task][(row["type"], row["address"].upper())] = row[
            "before_fnv"
        ].upper()
    oracle_tasks = oracle_alists(args.oracle)
    offset, anchors = detect_offset(local_tasks, oracle_tasks)
    alignment = "AList"
    state_votes = 0
    state_mappings: list[tuple[int, int, int, int]] = []
    if anchors == 0:
        oracle_state_sets = load_oracle_states(args.oracle)
        state_offset, state_votes, state_mappings = detect_state_offset(
            grouped_states, oracle_state_sets
        )
        minimum_state_votes = max(10, len(grouped_states) // 20)
        if state_votes >= minimum_state_votes:
            offset = state_offset
            alignment = "estados"
        else:
            alignment = "nenhum"
    structural_offset, structural_distance, structural_samples = (
        detect_structural_offset(args.probe, args.oracle)
    )
    if (
        anchors == 0
        and alignment == "nenhum"
        and structural_samples >= 4
        and structural_distance / structural_samples <= 25
    ):
        offset = structural_offset
        alignment = "estrutura"

    exact_alists = 0
    compared_states = 0
    divergences: list[dict[str, str]] = []
    missing = 0
    prior_after: dict[str, list[tuple[int, str, str]]] = defaultdict(list)
    moved_between: list[tuple[dict[str, str], tuple[int, str, str] | None]] = []

    exact_alist_tasks: set[int] = set()
    for row in sorted(states, key=lambda item: (int(item["task"]), item["type"], item["address"])):
        task = int(row["task"])
        oracle_task = task + offset
        if oracle_tasks.get(oracle_task) == row["alist_fnv"].upper():
            exact_alist_tasks.add(task)

        if row["changed_between"] == "1":
            candidates = [
                item for item in prior_after[row["before_fnv"].upper()]
                if item[1] != row["address"] or item[2] != row["type"]
            ]
            moved_between.append((row, candidates[-1] if candidates else None))

        if alignment != "nenhum":
            oracle_dir = args.oracle / f"task_{oracle_task:06d}"
            oracle_state = first_oracle_state(
                oracle_dir, row["type"], row["address"].upper()
            )
            if oracle_state is None:
                missing += 1
            else:
                compared_states += 1
                oracle_digest = fnv1a(oracle_state.read_bytes())
                if oracle_digest != row["before_fnv"].upper():
                    divergences.append({
                        **row,
                        "oracle_task": str(oracle_task),
                        "oracle_fnv": oracle_digest,
                    })
        prior_after[row["after_fnv"].upper()].append(
            (task, row["address"], row["type"])
        )

    rsp = [row for row in pcm if row["stage"] == "rsp"]
    ai = [row for row in pcm if row["stage"] == "ai"]
    ai_exact = [row for row in ai if row["changed_or_unmatched"] == "0"]
    ai_bad = [row for row in ai if row["changed_or_unmatched"] == "1"]
    first_by_kind: dict[str, dict[str, str]] = {}
    for row in divergences:
        first_by_kind.setdefault(row["type"], row)

    lines = [
        "# Primeira divergência de áudio",
        "",
        f"- tarefas locais com estados: {len(local_tasks)}",
        f"- deslocamento Project64: {offset:+d}",
        f"- método de alinhamento: {alignment}",
        f"- âncoras AList exatas: {anchors}/{len(local_tasks)}",
        f"- votos do alinhamento por estados: {state_votes}",
        f"- amostras estruturais brutas: {structural_samples}",
        f"- distância estrutural total: {structural_distance}",
        f"- tarefas com AList exata: {len(exact_alist_tasks)}/{len(local_tasks)}",
        f"- estados comparados: {compared_states}",
        f"- estados divergentes do Project64: {len(divergences)}",
        f"- estados ausentes no oráculo: {missing}",
        f"- buffers produzidos pelo RSP: {len(rsp)}",
        f"- buffers AI idênticos ao término do RSP: {len(ai_exact)}",
        f"- buffers AI sem par ou alterados: {len(ai_bad)}",
        "",
        "## Primeira divergência por estado",
        "",
        "| tipo | local | Project64 | endereço | antes local | Project64 |",
        "|---|---:|---:|---|---|---|",
    ]
    for kind in ("adpcm", "resample", "envmix"):
        row = first_by_kind.get(kind)
        if row:
            lines.append(
                f"| {kind} | {row['task']} | {row['oracle_task']} | "
                f"`{row['address']}` | `{row['before_fnv']}` | "
                f"`{row['oracle_fnv']}` |"
            )
        else:
            status = "não comparado" if alignment == "nenhum" else "igual"
            lines.append(f"| {kind} | — | — | — | {status} | {status} |")

    lines += ["", "## Alterações entre tarefas", ""]
    cross_slot = 0
    for row, source in moved_between:
        if source:
            cross_slot += 1
            source_text = f"mesmo hash em {source[2]} {source[1]} após tarefa {source[0]}"
        else:
            source_text = "sem origem anterior encontrada"
        lines.append(
            f"- tarefa {row['task']} {row['type']} `{row['address']}`: "
            f"{source_text}."
        )
    lines += [
        "",
        f"Mudanças compatíveis com cópia entre slots: {cross_slot}/{len(moved_between)}.",
        "",
        "## Interpretação automática",
        "",
    ]
    baseline_dirs = sorted(args.probe.glob("baseline_task_*"))
    if baseline_dirs and alignment != "nenhum":
        baseline = baseline_dirs[0]
        baseline_task = int(baseline.name.rsplit("_", 1)[-1])
        oracle_task = baseline_task + offset
        local_alist = (baseline / "alist.bin").read_bytes()
        oracle_path = args.oracle / f"task_{oracle_task:06d}" / "alist.bin"
        if oracle_path.exists():
            oracle_alist = oracle_path.read_bytes()
            first_command = None
            for command in range(min(len(local_alist), len(oracle_alist)) // 8):
                left = local_alist[command * 8:(command + 1) * 8]
                right = oracle_alist[command * 8:(command + 1) * 8]
                if left != right:
                    first_command = (command, left.hex().upper(), right.hex().upper())
                    break
            lines += ["## AList musical inicial", ""]
            lines.append(
                f"- local {baseline_task}: {len(local_alist)} bytes; "
                f"Project64 {oracle_task}: {len(oracle_alist)} bytes."
            )
            if first_command:
                command, left, right = first_command
                lines.append(
                    f"- primeiro comando diferente: {command}; local `{left}`, "
                    f"Project64 `{right}`."
                )
            elif len(local_alist) != len(oracle_alist):
                lines.append("- prefixo comum; a primeira diferença é o tamanho da lista.")
            else:
                lines.append("- ALists integralmente idênticas.")
            lines.append("")
    if ai_exact and not any(
        row["changed_or_unmatched"] == "1" and row["rsp_after_fnv"] != "0000000000000000"
        for row in ai
    ):
        lines.append(
            "Todo buffer AI que encontrou sua tarefa produtora permaneceu byte a byte "
            "idêntico; corrupção entre o término do RSP e o envio ao dispositivo foi rejeitada."
        )
    if alignment == "nenhum":
        lines.append(
            "A execução não possui âncora suficiente com esta captura do Project64; "
            "comparações de estado contra tarefas de mesmo número foram deliberadamente "
            "omitidas para não criar uma falsa primeira divergência."
        )
    elif alignment == "estrutura":
        first_task = min(local_tasks)
        first_oracle = first_task + offset
        first_rows = [row for row in states if int(row["task"]) == first_task]
        first_equal = 0
        for row in first_rows:
            state = first_oracle_state(
                args.oracle / f"task_{first_oracle:06d}",
                row["type"], row["address"].upper(),
            )
            if state and fnv1a(state.read_bytes()) == row["before_fnv"].upper():
                first_equal += 1
        lines.append(
            f"O alinhamento estrutural associa a primeira AList musical local "
            f"{first_task} à tarefa Project64 {first_oracle}. Seus estados iniciais "
            f"coincidem em {first_equal}/{len(first_rows)}, mas o hash completo da "
            "AList já difere; portanto a primeira causa observável está na construção "
            "ou no conteúdo da AList, antes da persistência dos históricos."
        )
    if divergences:
        first = divergences[0]
        lines.append(
            f"A primeira divergência contra o Project64 ocorre na tarefa local "
            f"{first['task']} ({first['type']} em {first['address']})."
        )
    elif anchors > 0:
        lines.append(
            "Nenhum estado comparável divergiu do Project64 nesta janela; a causa está "
            "depois do microcódigo ou fora do intervalo capturado."
        )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(args.out)
    print(
        f"offset={offset:+d} anchors={anchors} states={compared_states} "
        f"divergences={len(divergences)} ai_exact={len(ai_exact)} ai_bad={len(ai_bad)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
