"""Varredura: varias sondagens ao mesmo tempo, uma por configuracao.

Cada corrida leva 20 s de relogio. Testar seis hipoteses em sequencia custa dois
minutos; testar as seis de uma vez custa vinte segundos. Como cada instancia e um
processo independente com o proprio prefixo de saida, elas nao se atrapalham -
so o `Sleep(1)` do portao de retrace as coloca em concorrencia leve.

Cada linha da tabela de configuracoes vira um processo. No fim, a comparacao de
cobertura entre elas diz qual hipotese vale a pena perseguir, sem precisar
adivinhar antes.

Uso:
    sweep.py                 roda o conjunto padrao
    sweep.py 12              cada corrida com 12 s em vez de 20
"""
from __future__ import annotations

import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

proj = Path(__file__).resolve().parent.parent
exe = proj / "wpj2_probe.exe"
outdir = proj / "sweep"

# indices de evento, iguais aos da libultra
COUNTER, SP, SI, VI, PI, DP = 3, 4, 5, 7, 8, 9
TODOS = (1 << COUNTER) | (1 << SP) | (1 << SI) | (1 << VI) | (1 << PI) | (1 << DP)

# Botoes do controle do N64, em 16 bits.
A, B, Z, START = 0x8000, 0x4000, 0x2000, 0x1000

# nome, variaveis de ambiente, o que a corrida esta testando
EIXO_EVENTOS = [
    ("base",        {},                                       "conjunto atual"),
    ("sem_si",      {"WPJ2_EVENTS": hex(TODOS & ~(1 << SI))},  "controle nunca responde"),
    ("sem_counter", {"WPJ2_EVENTS": hex(TODOS & ~(1 << COUNTER))}, "sem temporizadores"),
    ("sem_sp",      {"WPJ2_EVENTS": hex(TODOS & ~(1 << SP))},  "tarefa de RSP nunca conclui"),
    ("sem_pi",      {"WPJ2_EVENTS": hex(TODOS & ~(1 << PI))},  "sem conclusao de DMA"),
    ("mem8",        {"WPJ2_MEMSIZE": "0x800000"},              "com pak de expansao"),
]

# O segundo eixo: e possivel que o jogo espere alguem apertar algo, ou que
# precise de mais tempo do que 20 s para sair da inicializacao. As duas
# hipoteses custam o mesmo relogio quando rodam juntas.
EIXO_ENTRADA = [
    ("base",        {},                                       "nada pressionado"),
    ("start",       {"WPJ2_BUTTONS": hex(START)},             "START segurado"),
    ("botao_a",     {"WPJ2_BUTTONS": hex(A)},                 "A segurado"),
    ("botao_b",     {"WPJ2_BUTTONS": hex(B)},                 "B segurado"),
    ("longo_60",    {"WPJ2_TIMEOUT": "60"},                   "tres vezes mais tempo"),
    ("longo_120",   {"WPJ2_TIMEOUT": "120"},                  "seis vezes mais tempo"),
]

CONFIGS = EIXO_ENTRADA if "--entrada" in sys.argv else EIXO_EVENTOS


def executar(cfg):
    nome, env_extra, _ = cfg
    env = dict(os.environ)
    env["WPJ2_OUT"] = str(outdir / (nome + "_"))
    env.setdefault("WPJ2_TIMEOUT", timeout)
    env.update(env_extra)

    log = outdir / (nome + ".log")
    t0 = time.time()
    with log.open("w", encoding="utf-8", errors="replace") as f:
        subprocess.run([str(exe)], cwd=proj, env=env, stdout=f, stderr=subprocess.STDOUT)
    return nome, time.time() - t0


def ler_metricas(nome):
    """Extrai da saida os numeros que interessam comparar entre corridas."""
    m = {"funcoes": 0, "chamadas": 0, "tarefas": 0, "dma": 0, "falhas": 0}
    log = outdir / (nome + ".log")
    if not log.exists():
        return m
    for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
        s = line.strip()
        if s.startswith("funcoes executadas"):
            m["funcoes"] = int(s.split(":")[1].split("de")[0])
        elif s.startswith("total de chamadas"):
            m["chamadas"] = int(s.split(":")[1])
        elif s.startswith("RSP  "):
            m["tarefas"] = int(s.split(":")[1].split("tarefa")[0])
        elif s.startswith("DMA do cartucho"):
            m["dma"] = int(s.split(":")[1].split("transferencia")[0])
        elif "[falha]" in s and "morreu" in s:
            m["falhas"] += 1
    return m


def conjunto(nome):
    p = outdir / (nome + "_executadas.txt")
    if not p.exists():
        return set()
    return {int(l.split()[0], 16) for l in p.read_text(encoding="utf-8").splitlines()
            if l and not l.startswith("#")}


args = [a for a in sys.argv[1:] if not a.startswith("--")]
timeout = args[0] if args else "20"
outdir.mkdir(exist_ok=True)

print("%d configuracoes em paralelo, %s s cada\n" % (len(CONFIGS), timeout))
t0 = time.time()
with ThreadPoolExecutor(max_workers=len(CONFIGS)) as pool:
    for nome, dur in pool.map(executar, CONFIGS):
        print("  %-12s terminou em %.1f s" % (nome, dur))
total = time.time() - t0
print("\ntempo de relogio: %.1f s (seria ~%.0f s em sequencia)\n"
      % (total, len(CONFIGS) * float(timeout)))

print("%-12s %8s %10s %8s %6s %7s  %s" %
      ("config", "funcoes", "chamadas", "tarefas", "dma", "falhas", "testando"))
base = conjunto("base")
for nome, _, descricao in CONFIGS:
    m = ler_metricas(nome)
    print("%-12s %8d %10d %8d %6d %7d  %s" %
          (nome, m["funcoes"], m["chamadas"], m["tarefas"], m["dma"], m["falhas"],
           descricao))

print("\ndiferenca de cobertura em relacao a 'base':")
for nome, _, _ in CONFIGS:
    if nome == "base":
        continue
    s = conjunto(nome)
    so_nela = sorted(s - base)
    so_base = sorted(base - s)
    print("  %-12s +%d funcao(oes) que a base nao alcanca, -%d que ela alcanca"
          % (nome, len(so_nela), len(so_base)))
    if so_nela:
        print("      novas: %s" % " ".join("%08X" % v for v in so_nela[:10]))
