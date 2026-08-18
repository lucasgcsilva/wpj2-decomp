"""Laboratorio: uma rodada inteira de experimentos, em paralelo, num comando.

Existe para trocar o ciclo lento — uma build, uma corrida, uma analise, repete —
por um unico disparo que cobre varias frentes ao mesmo tempo e resume tudo em
`RESULTADO.md`.

O resumo e curto de proposito: ele e feito para ser lido por inteiro e decidir o
proximo passo, nao para guardar tudo. Os logs completos ficam em `lab/`, e sao
consultados so quando o resumo apontar para um deles.

Frentes cobertas numa execucao:
  1. build do binario de sondagem
  2. varredura de eventos     - qual parte do HLE sustenta o boot
  3. varredura de entrada     - estados de botao e roteiros temporais
  4. varredura de duracao     - o jogo avanca com mais tempo?
  5. analise estatica         - callgraph, MMIO, COP0 (16 processos)
  6. fronteira e cobertura    - o que ficou a uma chamada de distancia
"""
from __future__ import annotations

import os
import re
import hashlib
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

PROJ = Path(__file__).resolve().parent.parent
LAB = PROJ / "lab"
EXE = PROJ / "wpj2_probe.exe"
ROM = next(Path("E:/projetos/n64-roms").glob("Wonder Project J2*.z64"))

COUNTER, SP, SI, VI, PI, DP = 3, 4, 5, 7, 8, 9
TODOS = (1 << COUNTER) | (1 << SP) | (1 << SI) | (1 << VI) | (1 << PI) | (1 << DP)
A, B, Z, START = 0x8000, 0x4000, 0x2000, 0x1000
CIMA, BAIXO, ESQ, DIR = 0x0800, 0x0400, 0x0200, 0x0100
BTN_L, BTN_R = 0x0020, 0x0010
C_CIMA, C_BAIXO, C_ESQ, C_DIR = 0x0008, 0x0004, 0x0002, 0x0001


def quantidade_workers():
    """Limite de instancias do jogo em cada lote.

    O retrace do HLE e deliberadamente sincronizado ao relogio real do host.
    Iniciar 63 processos mais a analise estatica ao mesmo tempo fazia o Windows
    atrasar cada instancia de modo diferente; a propria corrida-base entao
    mudava de estado. Lotes pequenos preservam esse relogio e tornam as tres
    repeticoes uma medicao de variancia do jogo, nao do escalonador do host.
    O usuario ainda pode escolher outro limite sem editar o laboratorio.
    """
    bruto = os.getenv("WPJ2_LAB_WORKERS", "6")
    try:
        return max(1, min(16, int(bruto)))
    except ValueError:
        return 6


# Cada entrada: (grupo, nome, variaveis, o que a corrida esta testando).
# O grupo so organiza o relatorio; todas rodam juntas.
def montar_corridas(t):
    return [
        ("referencia", "base", {}, "nada pressionado"),

        # Qual parte do HLE realmente sustenta o boot.
        ("eventos", "sem_si",      {"WPJ2_EVENTS": hex(TODOS & ~(1 << SI))},      "sem leitura de controle"),
        ("eventos", "sem_counter", {"WPJ2_EVENTS": hex(TODOS & ~(1 << COUNTER))}, "sem temporizadores"),
        ("eventos", "sem_pi",      {"WPJ2_EVENTS": hex(TODOS & ~(1 << PI))},      "sem conclusao de DMA"),
        ("eventos", "sem_sp",      {"WPJ2_EVENTS": hex(TODOS & ~(1 << SP))},      "sem conclusao de tarefa"),

        # Estados de botao segurados.
        ("botao", "start",  {"WPJ2_BUTTONS": hex(START)}, "START segurado"),
        ("botao", "a",      {"WPJ2_BUTTONS": hex(A)},     "A segurado"),
        ("botao", "b",      {"WPJ2_BUTTONS": hex(B)},     "B segurado"),
        ("botao", "cima",   {"WPJ2_BUTTONS": hex(CIMA)},  "direcional para cima"),
        ("botao", "baixo",  {"WPJ2_BUTTONS": hex(BAIXO)}, "direcional para baixo"),
        ("botao", "esq",    {"WPJ2_BUTTONS": hex(ESQ)},   "direcional para esquerda"),
        ("botao", "dir",    {"WPJ2_BUTTONS": hex(DIR)},   "direcional para direita"),
        ("botao", "l",      {"WPJ2_BUTTONS": hex(BTN_L)}, "L segurado"),
        ("botao", "r",      {"WPJ2_BUTTONS": hex(BTN_R)}, "R segurado"),
        ("botao", "c_dir",  {"WPJ2_BUTTONS": hex(C_DIR)}, "C-direita segurado"),
        ("botao", "a_start",{"WPJ2_BUTTONS": hex(A | START)}, "A e START juntos"),

        # Entrada deterministica por leituras reais do Joybus. Cada passo e o
        # numero da leitura CMD_READ_BTN, e nao milissegundos do host.
        ("poll", "start_p2", {"WPJ2_INPUT_POLLS": "1:0000;2:1000"}, "START a partir da leitura 2"),
        ("poll", "start_p3", {"WPJ2_INPUT_POLLS": "1:0000;3:1000"}, "START a partir da leitura 3"),
        ("poll", "start_p4", {"WPJ2_INPUT_POLLS": "1:0000;4:1000"}, "START a partir da leitura 4"),
        ("poll", "start_p5", {"WPJ2_INPUT_POLLS": "1:0000;5:1000"}, "START a partir da leitura 5"),
        ("poll", "start_p6", {"WPJ2_INPUT_POLLS": "1:0000;6:1000"}, "START a partir da leitura 6"),
        ("poll", "start_p7", {"WPJ2_INPUT_POLLS": "1:0000;7:1000"}, "START a partir da leitura 7"),
        ("poll", "pulse_p2", {"WPJ2_INPUT_POLLS": "1:0000;2:1000;3:0000"}, "pulso START na leitura 2"),
        ("poll", "pulse_p3", {"WPJ2_INPUT_POLLS": "1:0000;3:1000;4:0000"}, "pulso START na leitura 3"),
        ("poll", "pulse_p4", {"WPJ2_INPUT_POLLS": "1:0000;4:1000;5:0000"}, "pulso START na leitura 4"),
        ("poll", "pulse_p5", {"WPJ2_INPUT_POLLS": "1:0000;5:1000;6:0000"}, "pulso START na leitura 5"),
        # A+START sustentado abriu o unico estado com mais fluxo de CPU/DMA.
        # Estas variantes dizem se A e START precisam estar juntos, ou apenas
        # em qual leitura o segundo botao e observado.
        ("a_start", "a_start_p2", {"WPJ2_INPUT_POLLS": "1:8000;2:9000"}, "A, depois A+START na leitura 2"),
        ("a_start", "a_start_p3", {"WPJ2_INPUT_POLLS": "1:8000;3:9000"}, "A, depois A+START na leitura 3"),
        ("a_start", "a_start_p4", {"WPJ2_INPUT_POLLS": "1:8000;4:9000"}, "A, depois A+START na leitura 4"),
        ("a_start", "a_start_soltar3", {"WPJ2_INPUT_POLLS": "1:9000;3:8000"}, "A+START, solta START na leitura 3"),
        ("a_start", "a_start_soltar4", {"WPJ2_INPUT_POLLS": "1:9000;4:8000"}, "A+START, solta START na leitura 4"),
        ("a_start", "a_start_soltar5", {"WPJ2_INPUT_POLLS": "1:9000;5:8000"}, "A+START, solta START na leitura 5"),
        ("a_start", "b_start", {"WPJ2_BUTTONS": hex(B | START)}, "B+START segurados"),
        ("a_start", "z_start", {"WPJ2_BUTTONS": hex(Z | START)}, "Z+START segurados"),
        ("navegar", "poll_start_dir_a",
         {"WPJ2_INPUT_POLLS": "1:1000;3:0000;4:0100;5:0000;6:8000;7:0000"},
         "START, direita, A em leituras consecutivas"),
        ("navegar", "poll_start_baixo_a",
         {"WPJ2_INPUT_POLLS": "1:1000;3:0000;4:0400;5:0000;6:8000;7:0000"},
         "START, baixo, A em leituras consecutivas"),

        # Com SI correto ha dez ou onze leituras de controle por rodada. Estas
        # entradas acontecem depois da transicao START, nas leituras 8--11;
        # antes a matriz so testava a tela de boot e soltava START cedo demais.
        ("pos_start", "start_a_p8",
         {"WPJ2_INPUT_POLLS": "1:1000;8:9000"}, "START; acrescenta A na leitura 8"),
        ("pos_start", "start_b_p8",
         {"WPJ2_INPUT_POLLS": "1:1000;8:5000"}, "START; acrescenta B na leitura 8"),
        ("pos_start", "start_dir_p8",
         {"WPJ2_INPUT_POLLS": "1:1000;8:1800"}, "START; acrescenta cima na leitura 8"),
        ("pos_start", "start_baixo_p8",
         {"WPJ2_INPUT_POLLS": "1:1000;8:1400"}, "START; acrescenta baixo na leitura 8"),
        ("pos_start", "start_soltar_p8",
         {"WPJ2_INPUT_POLLS": "1:1000;8:0000"}, "START; solta na leitura 8"),
        ("pos_start", "start_soltar_p9",
         {"WPJ2_INPUT_POLLS": "1:1000;9:0000"}, "START; solta na leitura 9"),
        ("pos_start", "start_reaperta_p8",
         {"WPJ2_INPUT_POLLS": "1:1000;4:0000;8:1000"}, "pulso inicial; reaperta START na leitura 8"),
        ("pos_start", "start_p8",
         {"WPJ2_INPUT_POLLS": "1:0000;8:1000"}, "START somente a partir da leitura 8"),

        # O PIF tambem entrega eixos analogicos, agora com START deterministico.
        ("analogico", "start_stick_dir",
         {"WPJ2_BUTTONS": hex(START), "WPJ2_STICK": "80,0"}, "START com analogico para direita"),
        ("analogico", "start_stick_esq",
         {"WPJ2_BUTTONS": hex(START), "WPJ2_STICK": "-80,0"}, "START com analogico para esquerda"),

        # Duracao e memoria.
        ("ambiente", "longo",   {"WPJ2_TIMEOUT": str(int(float(t) * 4))}, "quatro vezes mais tempo"),
        ("ambiente", "longo_start",
         {"WPJ2_TIMEOUT": str(int(float(t) * 4)), "WPJ2_BUTTONS": hex(START)},
         "quatro vezes mais tempo, com START segurado"),
        ("ambiente", "mem8",    {"WPJ2_MEMSIZE": "0x800000"},             "pak de expansao"),
        ("ambiente", "mem4",    {"WPJ2_MEMSIZE": "0x400000"},             "memoria original de 4 MiB"),

        # Taxa de quadros. O jogo carrega por etapas, e a etapa seguinte pode
        # depender de quantos quadros passaram. Acelerar o retrace comprime o
        # tempo de jogo sem alongar o tempo de relogio - e a forma barata de
        # perguntar "ele carrega mais se rodar mais quadros?".
        ("taxa", "hz15",   {"WPJ2_RETRACE": "15"},   "15 Hz"),
        ("taxa", "hz60",   {"WPJ2_RETRACE": "60"},   "60 Hz nominal"),
        ("taxa", "hz120",  {"WPJ2_RETRACE": "120"},  "120 Hz"),
        ("taxa", "hz300",  {"WPJ2_RETRACE": "300"},  "300 Hz"),
        ("taxa", "hz1000", {"WPJ2_RETRACE": "1000"}, "1000 Hz, o maximo pratico"),
        ("taxa", "hz300_longo",
         {"WPJ2_RETRACE": "300", "WPJ2_TIMEOUT": str(int(float(t) * 4))},
         "300 Hz por quatro vezes mais tempo"),
        ("taxa", "hz1000_longo",
         {"WPJ2_RETRACE": "1000", "WPJ2_TIMEOUT": str(int(float(t) * 4))},
         "1000 Hz por quatro vezes mais tempo"),

        # Pares de eventos removidos: um evento sozinho pode nao ser essencial,
        # e o par ser. Testar de dois em dois separa dependencia de redundancia.
        ("pares", "sem_sp_dp",
         {"WPJ2_EVENTS": hex(TODOS & ~((1 << SP) | (1 << DP)))}, "sem SP nem DP"),
        ("pares", "so_vi_pi",
         {"WPJ2_EVENTS": hex((1 << VI) | (1 << PI))}, "somente VI e PI"),
        ("pares", "so_counter_si",
         {"WPJ2_EVENTS": hex((1 << COUNTER) | (1 << SI))}, "somente COUNTER e SI"),
        ("pares", "sem_vi",
         {"WPJ2_EVENTS": hex(TODOS & ~(1 << VI))}, "sem retrace de video"),
        ("pares", "sem_dp",
         {"WPJ2_EVENTS": hex(TODOS & ~(1 << DP))}, "sem conclusao de RDP"),

        # Repeticoes da mesma configuracao. O retrace e ancorado no relogio do
        # host, entao duas corridas iguais nao dao exatamente o mesmo resultado;
        # medir essa variacao e o que separa um efeito real de ruido.
        ("variancia", "rep1", {}, "repeticao da base"),
        ("variancia", "rep2", {}, "repeticao da base"),
        ("variancia", "rep3", {}, "repeticao da base"),
        ("variancia", "rep_start1", {"WPJ2_BUTTONS": hex(START)}, "repeticao com START"),
        ("variancia", "rep_start2", {"WPJ2_BUTTONS": hex(START)}, "repeticao com START"),

        # Combinacoes que ainda nao foram cruzadas.
        ("cruz", "start_hz300",
         {"WPJ2_BUTTONS": hex(START), "WPJ2_RETRACE": "300"},
         "START e 300 Hz"),
        ("cruz", "start_mem8",
         {"WPJ2_BUTTONS": hex(START), "WPJ2_MEMSIZE": "0x800000"},
         "START e pak de expansao"),
        ("cruz", "hz300_mem8",
         {"WPJ2_RETRACE": "300", "WPJ2_MEMSIZE": "0x800000"}, "300 Hz e pak"),
        ("cruz", "tudo",
         {"WPJ2_BUTTONS": hex(START), "WPJ2_RETRACE": "300",
          "WPJ2_MEMSIZE": "0x800000", "WPJ2_TIMEOUT": str(int(float(t) * 4))},
         "START, 300 Hz, pak e tempo longo"),
    ]


def rodar_sondagem(item):
    _grupo, nome, extra, _desc = item
    env = dict(os.environ)
    env["WPJ2_OUT"] = str(LAB / (nome + "_"))
    env.setdefault("WPJ2_TIMEOUT", TEMPO)
    env.update(extra)
    with (LAB / (nome + ".log")).open("w", encoding="utf-8", errors="replace") as f:
        subprocess.run([str(EXE), str(ROM)], cwd=PROJ, env=env,
                       stdout=f, stderr=subprocess.STDOUT)
    return nome


NUM = re.compile(r"(\d+)")


def metricas(nome):
    m = dict(funcoes=0, chamadas=0, tarefas=0, dma=0, acmd=0, falhas=0,
             graficas=0, threads=0, motivo="", ultima="", vi_origin="?",
             frame="", pixels=0, bytes=0, faixas=0, textura_ok=False,
             imagem=False, texrects=0, texels=0, ignorados=0, controles=0)
    p = LAB / (nome + ".log")
    if not p.exists():
        return m
    for s in p.read_text(encoding="utf-8", errors="replace").splitlines():
        s = s.strip()
        if s.startswith("funcoes executadas"):   m["funcoes"] = int(NUM.search(s).group(1))
        elif s.startswith("total de chamadas"):  m["chamadas"] = int(NUM.search(s).group(1))
        elif s.startswith("RSP  "):              m["tarefas"] = int(NUM.search(s).group(1))
        elif s.startswith("DMA do cartucho"):
            n = NUM.findall(s)
            m["dma"] = int(n[0]); m["bytes"] = int(n[1]) if len(n) > 1 else 0
        elif s.startswith("PIF"):
            achou = re.search(r"(\d+) leitura\(s\) de controle", s)
            m["controles"] = int(achou.group(1)) if achou else 0
        elif s.startswith("faixas da RDRAM"):    m["faixas"] = int(NUM.search(s).group(1))
        elif "endereco de textura" in s:
            m["textura_ok"] = "DENTRO" in s
        elif s.startswith("comandos de audio"):  m["acmd"] = int(NUM.search(s).group(1))
        elif s.startswith("motivo da parada"):   m["motivo"] = s.split(":", 1)[1].strip()[:60]
        elif s.startswith("ultima funcao alcancada"): m["ultima"] = s.split(":")[1].strip()
        elif "[falha]" in s and "morreu" in s:   m["falhas"] += 1
        elif s.startswith("tarefas por tipo"):
            g = re.search(r"graficas=(\d+)", s)
            m["graficas"] = int(g.group(1)) if g else 0
        elif "[watch] final" in s and "VI_ORIGIN" in s:
            m["vi_origin"] = s.split("=")[1].split("(")[0].strip()
        elif s.startswith("framebuffer"):
            m["frame"] = s.split(":", 1)[1].strip()[:70]
            achou = re.search(r"(\d+) de (\d+) pixels", s)
            m["pixels"] = int(achou.group(1)) if achou else 0
        elif s.startswith("imagem dos sprites"):
            m["imagem"] = ": SIM" in s
        elif s.startswith("sprites"):
            n = NUM.findall(s)
            if len(n) >= 2: m["texrects"], m["texels"] = int(n[0]), int(n[1])
        elif s.startswith("rasterizador") and "comando(s) ignorado(s)" in s:
            n = NUM.findall(s)
            if len(n) >= 3: m["ignorados"] = int(n[2])
        elif s.startswith("   id="):             m["threads"] += 1
    return m


def cobertura(nome):
    p = LAB / (nome + "_executadas.txt")
    if not p.exists():
        return set()
    return {int(l.split()[0], 16) for l in p.read_text(encoding="utf-8").splitlines()
            if l and not l.startswith("#")}


def analise_estatica():
    """Roda em paralelo com as sondagens: nao depende delas."""
    with (LAB / "analise.log").open("w", encoding="utf-8", errors="replace") as f:
        subprocess.run([sys.executable, str(PROJ / "tools/analyze_all.py"),
                        str(ROM), str(PROJ / "wpj2.syms.toml"), str(PROJ / "analysis")],
                       cwd=PROJ, stdout=f, stderr=subprocess.STDOUT)
        subprocess.run([sys.executable, str(PROJ / "tools/classify_hw.py"),
                        str(ROM), str(PROJ / "wpj2.syms.toml"), str(PROJ / "hw_map.txt")],
                       cwd=PROJ, stdout=f, stderr=subprocess.STDOUT)
    return "analise"


def texto(cmd, args):
    r = subprocess.run([sys.executable, str(PROJ / "tools" / cmd)] + args,
                       cwd=PROJ, capture_output=True, text=True, errors="replace")
    return (r.stdout or "") + (r.stderr or "")


def construir():
    with (LAB / "build.log").open("w", encoding="utf-8", errors="replace") as f:
        r = subprocess.run(["cmd", "/c", str(PROJ / "tools/build_probe.cmd")],
                           cwd=PROJ, stdout=f, stderr=subprocess.STDOUT)
    txt = (LAB / "build.log").read_text(encoding="utf-8", errors="replace")
    erros = [l.strip() for l in txt.splitlines()
             if "error C" in l or "FALHOU" in l or "error LNK" in l]
    return r.returncode, erros


# Aceita flags sem exigir um tempo antes delas (por exemplo, `RODAR.bat
# --sem-build`). O formato anterior tentava converter a flag em segundos.
posicionais = [arg for arg in sys.argv[1:] if not arg.startswith("--")]
TEMPO = posicionais[0] if posicionais else "20"
PULAR_BUILD = "--sem-build" in sys.argv
PULAR_PROFUNDO = "--sem-profundo" in sys.argv

LAB.mkdir(exist_ok=True)
inicio = time.time()
print("== laboratorio Wonder Project J2 ==")

if PULAR_BUILD:
    rc, erros = 0, []
    print("build: pulada por pedido")
else:
    print("build: compilando...")
    rc, erros = construir()
    print("build: %s" % ("ok" if rc == 0 and not erros else "FALHOU"))

if erros:
    (PROJ / "RESULTADO.md").write_text(
        "# Resultado\n\n## A build falhou\n\n```\n%s\n```\n\nDetalhes em `lab/build.log`.\n"
        % "\n".join(erros[:20]), encoding="utf-8")
    print("\nbuild falhou; RESULTADO.md tem os erros.")
    raise SystemExit(1)

CORRIDAS = montar_corridas(TEMPO)
WORKERS = quantidade_workers()
print("sondagens: %d configuracoes, %s s cada, em lotes de %d" %
      (len(CORRIDAS), TEMPO, WORKERS))

with ThreadPoolExecutor(max_workers=WORKERS) as pool:
    futuros = [pool.submit(rodar_sondagem, c) for c in CORRIDAS]
    for f in futuros:
        f.result()

# A analise estatica usa varios processos. Executa-la junto da matriz anulava o
# isolamento dos lotes e era a principal fonte de jitter do retrace real.
print("analise estatica: executando depois das sondagens...")
analise_estatica()

# O watchpoint de textura e deliberadamente fora do paralelo: ele usa excecoes
# por escrita e seria uma fonte de ruido para as metricas normais da matriz.
if not PULAR_PROFUNDO:
    print("sonda profunda: rastreando o preenchimento do buffer grafico...")
    with (LAB / "profundo.log").open("w", encoding="utf-8", errors="replace") as f:
        subprocess.run([sys.executable, str(PROJ / "tools" / "deep_probe.py"),
                        "--tempo", TEMPO], cwd=PROJ, stdout=f, stderr=subprocess.STDOUT)
    subprocess.run([sys.executable, str(PROJ / "tools" / "josette_reference.py"),
                    str(ROM), str(LAB / "profundo_textura_tlut.bin"),
                    str(LAB / "JOSSETTE_REFERENCIA.md")], cwd=PROJ)
    with (LAB / "profundo_a_start.log").open("w", encoding="utf-8", errors="replace") as f:
        subprocess.run([sys.executable, str(PROJ / "tools" / "deep_probe.py"),
                        "--tempo", TEMPO, "--nome", "a_start", "--buttons", "0x9000"],
                       cwd=PROJ, stdout=f, stderr=subprocess.STDOUT)
    subprocess.run([sys.executable, str(PROJ / "tools" / "josette_reference.py"),
                    str(ROM), str(LAB / "profundo_a_start_tlut.bin"),
                    str(LAB / "JOSSETTE_A_START_REFERENCIA.md")], cwd=PROJ)
    # A leitura 8 ocorre depois da transicao para START; e a primeira janela
    # onde uma escolha de jogo pode ser diferente da confirmacao de boot.
    with (LAB / "profundo_start_a_p8.log").open("w", encoding="utf-8", errors="replace") as f:
        subprocess.run([sys.executable, str(PROJ / "tools" / "deep_probe.py"),
                        "--tempo", TEMPO, "--nome", "start_a_p8",
                        "--buttons", "0x1000", "--polls", "1:1000;8:9000"],
                       cwd=PROJ, stdout=f, stderr=subprocess.STDOUT)
    subprocess.run([sys.executable, str(PROJ / "tools" / "josette_reference.py"),
                    str(ROM), str(LAB / "profundo_start_a_p8_tlut.bin"),
                    str(LAB / "JOSSETTE_START_A_P8_REFERENCIA.md")], cwd=PROJ)

decorrido = time.time() - inicio
print("tudo terminou em %.0f s" % decorrido)


# ---------------------------------------------------------------- relatorio
base = cobertura("base")
todas = {n: cobertura(n) for _g, n, _e, _d in CORRIDAS}
uniao = set().union(*todas.values()) if todas else set()

L = []
w = L.append
w("# Resultado do laboratorio")
w("")
w("Gerado em %s, %d sondagens de %s s em lotes de %d, %.0f s de relogio."
  % (time.strftime("%Y-%m-%d %H:%M"), len(CORRIDAS), TEMPO, WORKERS, decorrido))
w("")
w("## Resumo")
w("")
w("- cobertura da corrida base: **%d** funcoes" % len(base))
w("- uniao de todas as corridas: **%d** funcoes (%d a mais que a base)"
  % (len(uniao), len(uniao - base)))
w("- maximo de tarefas graficas em uma corrida: **%d**; maximo de sprites: **%d TEXRECT**"
  % (max(metricas(n)["graficas"] for _g, n, _e, _d in CORRIDAS),
     max(metricas(n)["texrects"] for _g, n, _e, _d in CORRIDAS)))
w("- maximo de leituras reais do controle: **%d**"
  % max(metricas(n)["controles"] for _g, n, _e, _d in CORRIDAS))
w("- imagem visivel produzida pelo decompilador: **%s**"
  % ("SIM" if any(metricas(n)["imagem"] for _g, n, _e, _d in CORRIDAS) else "NAO"))
w("  - PPMs diagnosticos sao gravados mesmo quando o resultado e NAO; preto opaco"
  " nao e considerado uma imagem visivel.")
w("- isolamento temporal: **%d** instancia(s) do jogo por lote; a analise estatica"
  " roda depois da matriz." % WORKERS)
w("")
w("## Corridas")
w("")
w("| grupo | corrida | funcoes | +base | chamadas | PIF | gfx | TEXRECT | KB carregados | faixas | pixels | testando |")
w("|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|")
for grupo, nome, _e, desc in CORRIDAS:
    m = metricas(nome)
    novas = len(todas[nome] - base)
    w("| %s | %s | %d | +%d | %d | %d | %d | %d | %d | %d | %d | %s |"
      % (grupo, nome, m["funcoes"], novas, m["chamadas"], m["controles"],
         m["graficas"], m["texrects"], m["bytes"] // 1024, m["faixas"],
         m["pixels"], desc))
w("")
w("`pixels` = quantos pixels do framebuffer apresentado nao sao pretos. Cada")
w("corrida grava o quadro em `lab/<nome>_frame.ppm`, que abre em qualquer visor")
w("de imagem. E a resposta direta para \"ja aparece alguma coisa?\".")
w("")

# Cobertura por si so pode esconder dois estados com o mesmo numero de funcoes.
# A assinatura usa os enderecos, tarefas, DMA e TEXRECT para mostrar quanto da
# matriz realmente explora fluxo novo e quanto e repeticao/controle de ruido.
estados = {}
for _g, nome, _e, _d in CORRIDAS:
    m = metricas(nome)
    cob = ",".join("%08X" % v for v in sorted(todas[nome])).encode()
    ident = (hashlib.sha1(cob).hexdigest()[:10], m["graficas"], m["bytes"], m["texrects"])
    estados.setdefault(ident, []).append(nome)
w("## Estados de execucao distintos")
w("")
w("A matriz produziu **%d** assinaturas; corridas no mesmo grupo tiveram cobertura e"
  " atividade grafica/DMA equivalentes." % len(estados))
for (assinatura, gfx, bytes_, rects), nomes in sorted(estados.items(), key=lambda item: (-len(item[1]), item[1][0])):
    mostra = ", ".join(nomes[:10]) + (" ..." if len(nomes) > 10 else "")
    w("- `%s`: %d corrida(s), gfx=%d, DMA=%d KiB, TEXRECT=%d — %s"
      % (assinatura, len(nomes), gfx, bytes_ // 1024, rects, mostra))
w("")

# O que so aparece fora da base: e onde esta a informacao nova de cada rodada.
w("## Funcoes que so aparecem fora da base")
w("")
achou = False
for _g, nome, _e, desc in CORRIDAS:
    novas = sorted(todas[nome] - base)
    if not novas:
        continue
    achou = True
    w("**%s** (%s) — %d novas:" % (nome, desc, len(novas)))
    w("")
    w("```")
    for i in range(0, min(len(novas), 40), 8):
        w(" ".join("%08X" % v for v in novas[i:i + 8]))
    w("```")
    w("")
if not achou:
    w("Nenhuma corrida alcancou funcao que a base ja nao alcance.")
    w("")

# Falhas: o texto exato, que e o que permite decidir sem abrir o log.
w("## Falhas observadas")
w("")
achou = False
for _g, nome, _e, _d in CORRIDAS:
    p = LAB / (nome + ".log")
    if not p.exists():
        continue
    linhas = p.read_text(encoding="utf-8", errors="replace").splitlines()
    for i, s in enumerate(linhas):
        if "[falha]" in s and "morreu" in s:
            achou = True
            w("- **%s**: %s" % (nome, s.strip()))
            for extra in linhas[max(0, i - 2):i]:
                if "instrucao em" in extra:
                    w("  - %s" % extra.strip())
if not achou:
    w("Nenhuma thread morreu em nenhuma corrida.")
w("")

# Estado das threads na melhor corrida: onde cada uma parou e em que fila.
melhor = max(todas, key=lambda n: len(todas[n]))
w("## Threads na melhor corrida (`%s`)" % melhor)
w("")
w("```")
p = LAB / (melhor + ".log")
if p.exists():
    dentro = False
    for s in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if s.startswith("threads:"):
            dentro = True
        elif dentro and not s.startswith(" "):
            break
        if dentro:
            w(s.rstrip())
w("```")
w("")

# Fronteira sobre a uniao: o que nenhuma corrida alcancou.
(PROJ / "executadas.txt").write_text(
    "# uniao de todas as corridas\n" +
    "".join("%08X 1\n" % v for v in sorted(uniao)), encoding="utf-8")
# Leituras repetidas do cartucho: um bloco lido dezenas de vezes e retentativa,
# nao carregamento.
w("## Leituras do cartucho e laco quente (`%s`)" % melhor)
w("")
w("```")
p = LAB / (melhor + ".log")
if p.exists():
    linhas = p.read_text(encoding="utf-8", errors="replace").splitlines()
    for marca in ("leituras do cartucho", "funcoes mais chamadas"):
        dentro = False
        for s in linhas:
            if s.startswith(marca):
                dentro = True
            elif dentro and not s.startswith("   "):
                break
            if dentro:
                w(s.rstrip())
w("```")
w("")

# A primeira tarefa grafica e a lista dela: e o material do renderizador.
w("## Primeira tarefa grafica (`%s`)" % melhor)
w("")
w("```")
p = LAB / (melhor + ".log")
if p.exists():
    linhas = p.read_text(encoding="utf-8", errors="replace").splitlines()
    achou_gfx = False
    for i, s in enumerate(linhas):
        if "tipo=1" in s:
            for extra in linhas[i:i + 22]:
                if extra.startswith("  [task]") or extra.startswith("  [gfx]"):
                    w(extra.rstrip())
            achou_gfx = True
            break
    if not achou_gfx:
        w("(nenhuma tarefa grafica nesta corrida)")
w("```")
w("")

# Opcodes graficos: e o orcamento do rasterizador.
w("## Comandos graficos usados (`%s`)" % melhor)
w("")
w("```")
p = LAB / (melhor + ".log")
if p.exists():
    dentro = False
    for s in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if (s.startswith("comandos graficos") or s.startswith("alvos de desenho")
                or s.startswith("rasterizador") or s.startswith("sprites")
                or s.startswith("paleta") or s.startswith("alvo")):
            w(s.rstrip()); dentro = False
        elif s.startswith("formatos de textura"):
            w(s.rstrip()); dentro = True
        elif s.startswith("opcodes usados"):
            w(s.rstrip()); dentro = True
        elif dentro and not s.startswith("   "):
            dentro = False
        elif dentro:
            w(s.rstrip())
w("```")
w("")
dl = LAB / (melhor + "_displaylist.txt")
if dl.exists():
    linhas_dl = dl.read_text(encoding="utf-8", errors="replace").splitlines()
    w("Lista de exibicao completa em `lab/%s_displaylist.txt` (%d comandos, "
      "sublistas incluidas). Primeiros 30:" % (melhor, len(linhas_dl)))
    w("")
    w("```")
    for s in linhas_dl[:30]:
        w(s.rstrip())
    w("```")
    w("")

# Evolucao do quadro ao longo da corrida.
w("## Quadros ao longo do tempo (`%s`)" % melhor)
w("")
w("```")
if p.exists():
    for s in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if s.startswith("framebuffer"):
            w(s.rstrip())
w("```")
w("")

# Ranking de carregamento: a pergunta corrente e "qual configuracao traz mais
# dado do cartucho", porque a textura que falta esta numa faixa nunca escrita.
w("## Quem carrega mais do cartucho")
w("")
por_bytes = sorted(((metricas(n)["bytes"], metricas(n)["faixas"], n)
                    for _g, n, _e, _d in CORRIDAS), reverse=True)
w("| corrida | KB | faixas |")
w("|---|---:|---:|")
for b, fx, n in por_bytes[:10]:
    w("| %s | %d | %d |" % (n, b // 1024, fx))
w("")
melhor_dma = por_bytes[0][2] if por_bytes else "base"
w("Faixas da corrida que mais carregou (`%s`):" % melhor_dma)
w("")
w("```")
p = LAB / (melhor_dma + ".log")
if p.exists():
    dentro = False
    for s in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if s.startswith("faixas da RDRAM"):
            dentro = True
        elif dentro and not s.startswith("   "):
            break
        if dentro:
            w(s.rstrip())
w("```")
w("")

w("## Fronteira sobre a uniao de todas as corridas")
w("")
w("O que o codigo ja alcancado chama e nenhuma corrida alcancou.")
w("")
w("```")
w(texto("frontier.py", ["14"]).strip())
w("```")
w("")
w("## Onde olhar")
w("")
w("- logs completos por corrida: `lab/<nome>.log`")
w("- cobertura por corrida: `lab/<nome>_executadas.txt`")
w("- callgraph e MMIO: `analysis/`, `hw_map.txt`")
w("- inspecionar uma funcao: `python tools/callee_status.py <vram>`")

profundo = LAB / "ANALISE_PROFUNDA.md"
if profundo.exists() and not PULAR_PROFUNDO:
    linhas = profundo.read_text(encoding="utf-8", errors="replace").splitlines()
    w("")
    w("## Sonda profunda do buffer grafico")
    w("")
    w("Relatorio completo: `lab/ANALISE_PROFUNDA.md`.")
    for titulo in ("## Veredito de imagem", "## Escritas observadas na primeira pagina de textura",
                   "## Chamadas do rasterizador para o atlas", "## Proxima hipotese verificavel"):
        try:
            i = linhas.index(titulo)
        except ValueError:
            continue
        w("")
        w(titulo[3:])
        w("")
        for s in linhas[i + 1:i + 8]:
            if s.startswith("## "):
                break
            w(s)

referencia = LAB / "JOSSETTE_REFERENCIA.md"
if referencia.exists() and not PULAR_PROFUNDO:
    w("")
    w("## Comparacao com o extrator josette")
    w("")
    w("Relatorio completo: `lab/JOSSETTE_REFERENCIA.md`.")
    for s in referencia.read_text(encoding="utf-8", errors="replace").splitlines():
        if s.startswith("- entrada `0x10`") or s.startswith("- melhor paleta") or s.startswith("A TLUT") or s.startswith("Nenhuma coincidencia"):
            w(s)

profundo_a = LAB / "ANALISE_PROFUNDA_A_START.md"
if profundo_a.exists() and not PULAR_PROFUNDO:
    w("")
    w("## Sonda profunda de A+START")
    w("")
    w("Relatorio completo: `lab/ANALISE_PROFUNDA_A_START.md`.")
    for s in profundo_a.read_text(encoding="utf-8", errors="replace").splitlines():
        if s.startswith("`imagem dos sprites") or s.startswith("- `func_") or s.startswith("O modo `"):
            w(s)

referencia_a = LAB / "JOSSETTE_A_START_REFERENCIA.md"
if referencia_a.exists() and not PULAR_PROFUNDO:
    for s in referencia_a.read_text(encoding="utf-8", errors="replace").splitlines():
        if s.startswith("- melhor paleta") or s.startswith("Nenhuma coincidencia"):
            w(s)

profundo_pos = LAB / "ANALISE_PROFUNDA_START_A_P8.md"
if profundo_pos.exists() and not PULAR_PROFUNDO:
    w("")
    w("## Sonda profunda apos START (A na leitura 8)")
    w("")
    w("Relatorio completo: `lab/ANALISE_PROFUNDA_START_A_P8.md`.")
    for s in profundo_pos.read_text(encoding="utf-8", errors="replace").splitlines():
        if s.startswith("`imagem dos sprites") or s.startswith("- `func_") or s.startswith("O modo `"):
            w(s)

referencia_pos = LAB / "JOSSETTE_START_A_P8_REFERENCIA.md"
if referencia_pos.exists() and not PULAR_PROFUNDO:
    for s in referencia_pos.read_text(encoding="utf-8", errors="replace").splitlines():
        if s.startswith("- melhor paleta") or s.startswith("Nenhuma coincidencia"):
            w(s)

(PROJ / "RESULTADO.md").write_text("\n".join(L), encoding="utf-8")
print("\nRESULTADO.md escrito (%d linhas). Logs completos em lab\\." % len(L))
