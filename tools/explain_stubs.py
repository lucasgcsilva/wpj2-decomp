"""Registra, para cada stub, a mensagem exata que o N64Recomp emite por ele.

Um stub e uma funcao que o runtime tera de substituir a mao. Sem o motivo
gravado, a lista vira um ponto cego: nada distingue "instrucao de cache, sem
efeito no host" de "instrucao que faz algo que o port precisa reproduzir".

Para cada funcao da lista, roda o recompilador com todos os outros stubs no
lugar e so ela de fora, e guarda o que o recompilador reclama.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

STUB_BLOCK = re.compile(r"(?ms)^stubs = \[\n.*?^\]\n")


def write_config(config: Path, template: str, stubs: list[str]) -> None:
    body = "stubs = [\n" + "".join(f'    "{s}",\n' for s in stubs) + "]\n"
    config.write_text(STUB_BLOCK.sub(body, template, count=1), encoding="utf-8", newline="\n")


def main() -> int:
    recompiler, config, stubs_file, out_file = (Path(a) for a in sys.argv[1:5])
    template = config.read_text(encoding="utf-8")
    stubs = [l.strip() for l in stubs_file.read_text(encoding="utf-8").splitlines() if l.strip()]

    rows = []
    for target in stubs:
        write_config(config, template, [s for s in stubs if s != target])
        r = subprocess.run([str(recompiler), str(config)], cwd=config.parent,
                           text=True, errors="replace", capture_output=True)
        out = r.stdout + r.stderr
        reason = "recompilou sem erro (stub desnecessario?)"
        if r.returncode != 0:
            lines = [l.strip() for l in out.splitlines() if l.strip()]
            hit = [l for l in lines if "instruction" in l.lower()]
            reason = hit[0] if hit else (lines[0] if lines else "sem saida")
        rows.append((target, reason))
        print("%-16s %s" % (target, reason))

    write_config(config, template, stubs)   # restaura a lista completa
    out_file.write_text(
        "# funcao            motivo do stub (mensagem do N64Recomp)\n" +
        "".join("%-18s %s\n" % row for row in rows), encoding="utf-8", newline="\n")
    print("escrevi", out_file)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
