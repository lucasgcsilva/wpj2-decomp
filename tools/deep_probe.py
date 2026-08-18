"""Sonda dirigida do caminho START, executada ao fim de RODAR.bat.

Ela e propositalmente separada da matriz: a protecao de pagina usada para ver
quem escreve a textura altera a temporizacao do jogo. A matriz mede o estado
normal; esta sonda curta responde uma pergunta de engenharia por rodada e deixa
uma sintese persistente para o chat ler sem abrir logs enormes.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

PROJ = Path(__file__).resolve().parent.parent
LAB = PROJ / "lab"
EXE = PROJ / "wpj2_probe.exe"
ROM = next(Path("E:/projetos/n64-roms").glob("Wonder Project J2*.z64"))

WRITE = re.compile(r"^\s*(0x[0-9A-F]+) = 0x([0-9A-F]{2}) por func_([0-9A-F]+)$")
SOURCE = re.compile(r"^\s*(0x[0-9A-F]+)\s+(\d+) carga\(s\), (\d+)/(\d+) byte")


def run_text(script: str, *args: str) -> str:
    p = subprocess.run([sys.executable, str(PROJ / "tools" / script), *args],
                       cwd=PROJ, capture_output=True, text=True, errors="replace")
    return ((p.stdout or "") + (p.stderr or "")).strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tempo", type=float, default=8.0)
    parser.add_argument("--nome", default="textura")
    parser.add_argument("--buttons", default="0x1000")
    parser.add_argument("--polls", default="",
                        help="roteiro WPJ2_INPUT_POLLS para reproduzir uma janela de entrada")
    args = parser.parse_args()
    timeout = max(6.0, min(args.tempo, 10.0))

    env = dict(os.environ)
    env.update({
        "WPJ2_TIMEOUT": str(timeout),
        # START sustentado e a referencia deterministica; a antiga janela por
        # milissegundos variava conforme a carga da matriz paralela no host.
        "WPJ2_BUTTONS": args.buttons,
        "WPJ2_WATCH_TEXTURE": "1",
        "WPJ2_OUT": str(LAB / ("profundo_" + args.nome + "_")),
    })
    if args.polls:
        env["WPJ2_INPUT_POLLS"] = args.polls
    log = LAB / ("profundo_" + args.nome + ".log")
    with log.open("w", encoding="utf-8", errors="replace") as f:
        proc = subprocess.run([str(EXE), str(ROM)], cwd=PROJ, env=env,
                              stdout=f, stderr=subprocess.STDOUT)

    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    writes = []
    sources = []
    image = "imagem dos sprites       : NAO (nenhum dado no log)"
    heap = []
    raster = []
    for line in lines:
        m = WRITE.match(line.strip())
        if m:
            writes.append(m.groups())
        m = SOURCE.match(line.strip())
        if m:
            sources.append(m.groups())
        if line.startswith("imagem dos sprites"):
            image = line.strip()
        if line.startswith("   0x") and "bytes=" in line and "<- func_" in line:
            heap.append(line.strip())
        if line.startswith("   pai=func_"):
            raster.append(line.strip())

    by_writer: dict[str, Counter[str]] = {}
    for _addr, value, func in writes:
        by_writer.setdefault(func, Counter())[value] += 1

    L: list[str] = []
    w = L.append
    w("# Analise profunda: buffer de textura")
    w("")
    w("Sonda %s de %.0f s; codigo de saida do processo: %d (6 = watchdog esperado)."
      % (args.nome, timeout, proc.returncode))
    w("")
    w("## Veredito de imagem")
    w("")
    w("`%s`" % image)
    w("")
    w(("PPMs foram gerados em `lab/profundo_%s_*.ppm`, mas so contam como imagem"
       " visivel quando houver texel RGB diferente de preto.") % args.nome)
    w("")
    w("## Escritas observadas na primeira pagina de textura")
    w("")
    if by_writer:
        for func, values in sorted(by_writer.items()):
            vals = ", ".join("0x%s x%d" % pair for pair in values.most_common())
            w("- `func_%s`: %s" % (func, vals))
    else:
        w("- Nenhuma escrita foi capturada.")
    w("")
    w("Primeiras oito escritas: " + (", ".join(
        "%s=0x%s/%s" % (a, v, f) for a, v, f in writes[:8]) or "nenhuma") + ".")
    w("")
    w("## Origem dos primeiros LOADBLOCKs")
    w("")
    if sources:
        w("| origem | cargas | bytes nao-zero | bytes totais |")
        w("|---|---:|---:|---:|")
        for addr, loads, nz, total in sources[:12]:
            w("| %s | %s | %s | %s |" % (addr, loads, nz, total))
    else:
        w("- Nenhuma origem de LOADBLOCK foi encontrada.")
    w("")
    w("## Alocacoes relacionadas")
    w("")
    if heap:
        w("```")
        w("\n".join(heap[:12]))
        w("```")
    else:
        w("- Nenhuma alocacao candidata foi encontrada.")
    w("")
    w("## Chamadas do rasterizador para o atlas")
    w("")
    if raster:
        w("```")
        w("\n".join(raster[:16]))
        w("```")
    else:
        w("- Nenhuma chamada para o atlas foi registrada.")
    w("")
    for func in sorted(by_writer):
        name = "func_" + func
        w("## Callgraph de `%s`" % name)
        w("")
        w("```")
        w(run_text("callers.py", name))
        w(run_text("callee_status.py", "0x" + func)[:5000])
        w("```")
        w("")
    w("## Proxima hipotese verificavel")
    w("")
    if raster:
        w("O modo `0x0600` seleciona escrita CI8; `valor=0x0001` habilita a"
          " escrita e `a0=0x10` e o indice gravado. Portanto o atlas preto e"
          " intencional neste estado, nao um LOADBLOCK ausente. A proxima frente"
          " e o estado de entrada que chama `func_80090E58`, nao a textura.")
    elif by_writer:
        w("Cruzar `a0/a1` e `base/off` acima com o valor observado: isso separa"
          " uma escolha intencional de indice preto de uma tabela/estado ausente.")
    else:
        w("Repetir com uma janela de entrada diferente; a escrita ainda nao ocorreu.")

    analysis = LAB / ("ANALISE_PROFUNDA_" + args.nome.upper() + ".md")
    analysis.write_text("\n".join(L) + "\n", encoding="utf-8")
    if args.nome == "textura":
        (LAB / "ANALISE_PROFUNDA.md").write_text("\n".join(L) + "\n", encoding="utf-8")
    print("analise profunda escrita em", analysis)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
