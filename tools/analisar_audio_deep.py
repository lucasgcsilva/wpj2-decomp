"""Resume uma captura feita por wpj2_audio_deep_oracle.js sem alterar dados."""
from __future__ import annotations

import csv
from collections import Counter
from pathlib import Path
import sys


ROOT = Path(r"E:\projetos\project-wonder-j2-decomp\oraculo\pj64-rdram\audio_deep")


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT
    manifest = root / "manifest.csv"
    output = root / "relatorio_audio_deep.md"
    if not manifest.exists():
        print(f"Captura ausente: {manifest}")
        return 1

    rows = list(csv.DictReader(manifest.open(encoding="utf-8", newline="")))
    tasks = [row for row in rows if row.get("kind") == "TASK"]
    ai = [row for row in rows if row.get("kind") == "AI"]
    snapshots = [row for row in rows if row.get("kind") == "SNAPSHOT"]
    opcodes: Counter[str] = Counter()
    for row in tasks:
        for item in row.get("detail", "").split("|"):
            if "x" in item:
                name, count = item.rsplit("x", 1)
                try:
                    opcodes[name] += int(count)
                except ValueError:
                    pass
    ai_bytes = sum(int(row.get("bytes", "0") or 0) for row in ai)
    tree_files = [p for p in root.rglob("*") if p.is_file()]
    total_bytes = sum(p.stat().st_size for p in tree_files)
    lines = [
        "# Relatório da sonda profunda de áudio",
        "",
        f"- Tarefas RSP de áudio: **{len(tasks)}**",
        f"- Buffers PCM entregues ao AI: **{len(ai)}** ({ai_bytes:,} bytes)",
        f"- Snapshots completos de RDRAM: **{len(snapshots)}**",
        f"- Arquivos da coleta: **{len(tree_files)}** ({total_bytes / 1024 / 1024:.1f} MiB)",
        "",
        "## Operações observadas",
        "",
    ]
    lines += [f"- `{name}`: {count}" for name, count in opcodes.most_common()]
    lines += [
        "",
        "## Validação",
        "",
        "- A coleta é útil quando há tarefas, buffers AI e pelo menos uma AList por tarefa.",
        "- As ALists e estados foram capturados antes do RSP; os arquivos `ai/*.pcm` são a saída após ele.",
    ]
    if not tasks or not ai:
        lines += ["", "> A captura está incompleta: rode o script, reinicie a ROM e deixe a música tocar antes de fechar."]
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
