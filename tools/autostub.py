"""Executa N64Recomp iterativamente e registra funções não traduzíveis.

Stubs são somente uma fronteira explícita entre o código recompilado e o
runtime futuro; eles não constituem uma implementação do comportamento N64.
"""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


FAILED_FUNCTION = re.compile(r"Error recompiling (\S+)")
STUB_BLOCK = re.compile(r"(?ms)^stubs = \[\n.*?^\]\n")


def write_config(config: Path, stubs: list[str]) -> None:
    text = config.read_text(encoding="utf-8")
    body = "stubs = [\n" + "".join(f'    "{stub}",\n' for stub in stubs) + "]\n"
    updated, count = STUB_BLOCK.subn(body, text, count=1)
    if count != 1:
        raise RuntimeError("bloco stubs não encontrado em " + str(config))
    config.write_text(updated, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("recompiler", type=Path)
    parser.add_argument("config", type=Path)
    parser.add_argument("stubs_file", type=Path)
    parser.add_argument("--limit", type=int, default=64)
    args = parser.parse_args()

    stubs = []
    if args.stubs_file.exists():
        stubs = [line.strip() for line in args.stubs_file.read_text(encoding="utf-8").splitlines()
                 if line.strip() and not line.lstrip().startswith("#")]

    for attempt in range(1, args.limit + 1):
        write_config(args.config, stubs)
        result = subprocess.run([str(args.recompiler), str(args.config)],
                                cwd=args.config.parent, text=True,
                                errors="replace", capture_output=True)
        output = result.stdout + result.stderr
        if result.returncode == 0:
            args.stubs_file.write_text("\n".join(stubs) + ("\n" if stubs else ""),
                                       encoding="utf-8", newline="\n")
            print(f"sucesso após {attempt} tentativa(s); {len(stubs)} stub(s)")
            return 0
        match = FAILED_FUNCTION.search(output)
        if not match:
            print(output[-4000:])
            print("falha não associada a uma função; a lista de stubs não foi alterada")
            return result.returncode or 1
        function = match.group(1)
        if function in stubs:
            print(output[-4000:])
            print(f"{function} já era stub; interrompido para evitar mascarar erro")
            return 1
        reason = next((line for line in output.splitlines()
                       if line.startswith("Unhandled instruction:")), "motivo não informado")
        stubs.append(function)
        print(f"[{attempt:02d}] {function}: {reason}")

    print(f"limite de {args.limit} stubs atingido")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
