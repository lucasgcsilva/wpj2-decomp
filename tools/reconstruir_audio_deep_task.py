"""Reconstrói a RDRAM mínima de uma AList gravada pela sonda profunda.

Os dumps por tarefa são deliberadamente os blocos que a AList lê/escreve.
Isso possibilita rodar `audio_oracle_test.exe` sem despejar 8 MiB por tarefa.
"""
from __future__ import annotations

import csv
import re
import shutil
import sys
from pathlib import Path


DEFAULT_ROOT = Path(r"E:\projetos\project-wonder-j2-decomp\analise\oraculo\audio\deep")
DEFAULT_OUTPUT = Path(r"E:\projetos\project-wonder-j2-decomp\temp\projeto\audio_deep_oracle")
ADDRESS = re.compile(r"_([0-9A-Fa-f]{8})\.bin$")


def be32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "big")


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_ROOT
    task_number = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    output = Path(sys.argv[3]) if len(sys.argv) > 3 else DEFAULT_OUTPUT / f"task_{task_number:06d}"
    task_dir = root / "tasks" / f"task_{task_number:06d}"
    task_bin = task_dir / "task.bin"
    alist_bin = task_dir / "alist.bin"
    if not task_bin.exists() or not alist_bin.exists():
        raise SystemExit(f"Tarefa ausente: {task_dir}")

    row = None
    with (root / "manifest.csv").open(encoding="utf-8", newline="") as handle:
        for candidate in csv.DictReader(handle):
            if candidate.get("kind") == "AI" and candidate.get("audio_task") == str(task_number):
                row = candidate
                break
    if row is None:
        raise SystemExit(f"PCM AI ausente para tarefa {task_number}")
    ai_index = int(row["index"])
    ai_path = root / "ai" / f"ai_{ai_index:06d}_task_{task_number:06d}.pcm"
    if not ai_path.exists():
        raise SystemExit(f"Arquivo PCM ausente: {ai_path}")

    task = task_bin.read_bytes()
    alist = alist_bin.read_bytes()
    list_address = be32(task, 0x30) & 0x1FFFFFFF
    if len(task) < 0x40 or list_address + len(alist) > 0x800000:
        raise SystemExit("OSTask/AList inválida")
    # Quando a sonda preservou uma imagem integral exatamente antes desta
    # tarefa, ela é uma base mais forte que a reconstrução mínima: inclui o
    # ucode_data e tabelas indiretas que uma AList grande pode alcançar sem
    # expô-las como LOADBUFF. Sem isso o RSP pode saltar para 0 e a falha seria
    # confundida com divergência de áudio.
    snapshot = root / "snapshots" / f"rdram_before_task_{task_number:06d}.bin"
    snapshots = sorted(
        candidate
        for candidate in (root / "snapshots").glob("rdram_before_task_*.bin")
        if candidate.stat().st_size == 0x800000
    )
    if snapshot.exists() and snapshot.stat().st_size == 0x800000:
        rdram = bytearray(snapshot.read_bytes())
        base = "snapshot integral exato"
    elif snapshots:
        # O ucode_data, a tabela FIR e outros blocos estáticos não aparecem
        # como LOADBUFF na AList. Um snapshot de outra tarefa fornece essa
        # base; todos os blocos mutáveis observados pela sonda desta tarefa
        # são sobrepostos logo abaixo com os arquivos *_cmd*.bin.
        snapshot = min(
            snapshots,
            key=lambda candidate: abs(
                int(candidate.stem.rsplit("_", 1)[-1]) - task_number
            ),
        )
        rdram = bytearray(snapshot.read_bytes())
        base = f"snapshot integral auxiliar {snapshot.stem.rsplit('_', 1)[-1]}"
    else:
        rdram = bytearray(0x800000)
        base = "reconstrução mínima"
    rdram[list_address:list_address + len(alist)] = alist
    blocks = 0
    for file in task_dir.glob("*.bin"):
        match = ADDRESS.search(file.name)
        if not match:
            continue
        address = int(match.group(1), 16)
        data = file.read_bytes()
        if address + len(data) > len(rdram):
            continue
        rdram[address:address + len(data)] = data
        blocks += 1

    output.mkdir(parents=True, exist_ok=True)
    (output / "rdram.bin").write_bytes(rdram)
    shutil.copyfile(task_bin, output / "task.bin")
    shutil.copyfile(ai_path, output / "ai_pcm.bin")
    (output / "manifest.txt").write_text(
        f"AI buffer=0x{int(row['list_or_addr'], 16):08X}\r\n"
        f"AI bytes={row['bytes']}\r\n"
        f"source=audio_deep task {task_number}; blocks={blocks}\r\n",
        encoding="ascii",
    )
    print(f"{output}: AList=0x{list_address:08X}, blocos={blocks}, AI={ai_index}, base={base}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
