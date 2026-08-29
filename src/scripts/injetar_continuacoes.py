"""Gera uma variante retomável do C produzido pelo N64Recomp.

O arquivo de referência nunca é alterado. Cada chamada de função convidada
ganha uma frame {função, endereço do JAL/JALR}. No início da função, um switch
salta diretamente para a chamada gravada durante a reconstrução da pilha.

Esta primeira etapa instrumenta chamadas. Polls/preempção e pause_self serão
ligados quando o dispatcher sem fibers entrar no runtime principal.
"""

from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path

FUNC = re.compile(
    r"^RECOMP_FUNC void (func_([0-9A-F]{8})(?:__replaced)?|recomp_entrypoint)"
    r"\(uint8_t\* rdram, recomp_context\* ctx\) \{\s*$"
)
ANY_FUNC = re.compile(
    r"^RECOMP_FUNC void (func_[0-9A-F]{8}(?:__replaced)?|recomp_entrypoint)"
    r"\(uint8_t\* rdram, recomp_context\* ctx\) \{\s*$"
)
COMMENT = re.compile(r"^\s*// 0x([0-9A-F]{8}):\s+(.+)$")
DIRECT = re.compile(r"^(\s*)(func_[0-9A-F]{8})\(rdram, ctx\);\s*$")
INDIRECT = re.compile(
    r"^(\s*)(LOOKUP_FUNC\((ctx->r[0-9]+)\))\(rdram, ctx\);\s*$"
)
POLL = re.compile(r"^(\s*)RECOMP_POLL\(\);\s*$")
PAUSE = re.compile(r"^(\s*)pause_self\(rdram\);\s*$")
LABEL = re.compile(r"^L_([0-9A-F]{8}):\s*$")


def function_vram(name: str, hex_part: str | None) -> int:
    return 0x80000400 if name == "recomp_entrypoint" else int(hex_part, 16)


def transform_function(lines: list[str], name: str, vram: int) -> tuple[list[str], int]:
    sites: list[int] = []
    pending_call_instruction: tuple[int, str] | None = None
    last_instruction: tuple[int, str] | None = None
    current_label: int | None = None
    # index, chave persistente, tipo. Bits baixos distinguem chamada (0),
    # poll (1) e pause (2), pois todo endereço MIPS é alinhado em quatro.
    resume_rows: list[tuple[int, int, str]] = []

    for index, line in enumerate(lines):
        comment = COMMENT.match(line)
        if comment:
            disasm = comment.group(2)
            last_instruction = (int(comment.group(1), 16), disasm)
            if re.match(r"(?:j|jal|jalr|jr)\b", disasm):
                pending_call_instruction = last_instruction
            continue
        label = LABEL.match(line)
        if label:
            current_label = int(label.group(1), 16)
            continue
        if POLL.match(line):
            if current_label is None:
                raise ValueError(f"{name}: RECOMP_POLL sem label MIPS")
            key = current_label | 1
            if key in sites:
                raise ValueError(f"{name}: ponto poll duplicado 0x{key:08X}")
            sites.append(key)
            resume_rows.append((index, key, "poll"))
            continue
        if PAUSE.match(line):
            if last_instruction is None:
                raise ValueError(f"{name}: pause_self sem instrução de origem")
            key = last_instruction[0] | 2
            if key in sites:
                raise ValueError(f"{name}: ponto pause duplicado 0x{key:08X}")
            sites.append(key)
            resume_rows.append((index, key, "pause"))
            continue
        if not (DIRECT.match(line) or INDIRECT.match(line)):
            continue
        if pending_call_instruction is None:
            raise ValueError(f"{name}: chamada sem instrução de origem na linha {index + 1}")
        site, disasm = pending_call_instruction
        key = site
        if key in sites:
            raise ValueError(f"{name}: callsite duplicado 0x{key:08X}")
        sites.append(key)
        direct = DIRECT.match(line)
        # __osDispatchThread e um ponto de cessao: na primeira passagem a
        # chamada precisa ocorrer, mas na retomada a OSThread continua depois
        # dela. Reinvoca-la faria a thread ceder eternamente no mesmo ponto.
        kind = "yield" if direct and direct.group(2) == "func_800CCAE4" else "call"
        resume_rows.append((index, key, kind))
        pending_call_instruction = None

    if not sites:
        return lines, 0

    switch = [
        "#ifdef RECOMP_STATEFUL\n",
        "    uint32_t __wpj2_cont_site = 0, __wpj2_cont_frame = 0, __wpj2_cont_target = 0;\n",
        f"    int __wpj2_cont_enter = wpj2_cont_enter_current_ex(0x{vram:08X}u, "
        "&__wpj2_cont_site, &__wpj2_cont_frame, &hi, &lo, &result, &c1cs, "
        "&__wpj2_cont_target, ctx);\n",
        "    if (__wpj2_cont_enter < 0) return;\n",
        "    if (__wpj2_cont_enter > 0) {\n",
        "        switch (__wpj2_cont_site) {\n",
    ]
    for site in sites:
        switch.append(
            f"        case 0x{site:08X}u: goto __wpj2_cont_resume_{site:08X};\n"
        )
    switch += ["        default: return;\n", "        }\n", "    }\n", "#endif\n"]

    # Todas as declarações locais do gerador vêm imediatamente após a
    # assinatura. Insira o switch depois de c1cs para não saltar sobre VLA ou
    # inicializações futuras caso o compilador endureça as regras de goto.
    insert_at = next(
        (i + 1 for i, line in enumerate(lines) if "int c1cs = 0;" in line), None
    )
    if insert_at is None:
        raise ValueError(f"{name}: prólogo padrão do N64Recomp não encontrado")

    resumes = {index: (key, kind) for index, key, kind in resume_rows}
    output: list[str] = []
    for index, line in enumerate(lines):
        output.append(line)
        if index + 1 == insert_at:
            output.extend(switch)
        if index not in resumes:
            continue
        # A linha da chamada acabou de ser emitida; substitua-a pelo bloco
        # completo, mantendo a indentação e a expressão original.
        output.pop()
        site, kind = resumes[index]
        match = DIRECT.match(line) or INDIRECT.match(line) or POLL.match(line) or PAUSE.match(line)
        assert match is not None
        indent = match.group(1)
        indirect = INDIRECT.match(line)
        target = f"(uint32_t){indirect.group(3)}" if indirect else "0u"
        prefix = [
            "#ifdef RECOMP_STATEFUL\n",
            f"{indent}if (!wpj2_cont_before_call_current_ex(0x{vram:08X}u, "
            f"0x{site:08X}u, hi, lo, result, c1cs, {target}, ctx, "
            "&__wpj2_cont_frame)) return;\n",
        ]
        if kind in ("poll", "yield"):
            prefix += [
                f"{indent}goto __wpj2_cont_invoke_{site:08X};\n",
                f"__wpj2_cont_resume_{site:08X}:\n",
                f"{indent}goto __wpj2_cont_after_{site:08X};\n",
                f"__wpj2_cont_invoke_{site:08X}:\n",
            ]
        else:
            prefix += [
                f"{indent}goto __wpj2_cont_invoke_{site:08X};\n",
                f"__wpj2_cont_resume_{site:08X}:\n",
            ]
            if indirect:
                prefix.append(
                    f"{indent}{indirect.group(3)} = (gpr)(int32_t)__wpj2_cont_target;\n"
                )
            prefix += [
                f"__wpj2_cont_invoke_{site:08X}:\n",
            ]
        output += prefix + ["#endif\n", line,
            "#ifdef RECOMP_STATEFUL\n",
            f"{indent}if (wpj2_cont_yielding_current()) return;\n",
            f"__wpj2_cont_after_{site:08X}:\n",
            f"{indent}if (!wpj2_cont_after_call_current(__wpj2_cont_frame)) return;\n",
            "#endif\n",
        ]
    return output, len(sites)


def transform_file(source: Path, target: Path) -> tuple[int, int]:
    lines = source.read_text(encoding="utf-8", errors="strict").splitlines(True)
    output = ['#include "continuation.h"\n']
    functions = calls = 0
    start = 0
    while start < len(lines):
        any_match = ANY_FUNC.match(lines[start])
        if not any_match:
            output.append(lines[start])
            start += 1
            continue
        end = start + 1
        while end < len(lines) and not ANY_FUNC.match(lines[end]):
            end += 1
        match = FUNC.match(lines[start])
        if not match:
            output.extend(lines[start:end])
            start = end
            continue
        name = match.group(1)
        transformed, count = transform_function(
            lines[start:end], name, function_vram(name, match.group(2))
        )
        output.extend(transformed)
        functions += 1
        calls += count
        start = end
    target.write_text("".join(output), encoding="utf-8", newline="\n")
    return functions, calls


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    args = parser.parse_args()
    if args.target.exists():
        shutil.rmtree(args.target)
    args.target.mkdir(parents=True)

    total_functions = total_calls = 0
    for source in sorted(args.source.iterdir()):
        target = args.target / source.name
        if source.suffix != ".c":
            shutil.copyfile(source, target)
            continue
        functions, calls = transform_file(source, target)
        total_functions += functions
        total_calls += calls
    print(
        f"continuations: {total_functions} funções, {total_calls} pontos retomáveis; "
        f"saída em {args.target}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
