"""Captura a mesma cena com tres tratamentos de fila e compara com o oraculo.

O teste que interessa nao e "o audio melhorou" - e se a correlacao com o
Project64 **para de cair** depois dos primeiros segundos. Na medicao anterior
ela ficava em 0,95-0,98 ate ~11 s e caia para 0,10-0,17 dali em diante, que e
quando a fila de oito slots enche.

Se a hipotese estiver certa, `sem_espera` repete aquela queda e as outras duas
sustentam a correlacao. Se as tres cairem igual, a causa e outra e o descarte
era so um sintoma.
"""
from __future__ import annotations

import os
import struct
import subprocess
import sys
import wave
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

PROJ = Path(__file__).resolve().parent.parent
EXE = PROJ / "wpj2_visual.exe"
ORACULO = PROJ / "analise" / "oraculo" / "audio" / "referencia" / "wpj2_audio_oracle.wav"
ROM = next(Path("E:/projetos/n64-roms").glob("Wonder Project J2*.z64"))
SAIDA = PROJ / "temp" / "projeto" / "comparar_audio"

DUR = sys.argv[1] if len(sys.argv) > 1 else "25"

CONFIGS = [
    ("sem_espera", "0",  "descarta na hora (comportamento antigo)"),
    ("espera12",   "12", "espera ate 12 ms (novo padrao)"),
    ("espera40",   "40", "espera ate 40 ms"),
]


def rodar(cfg):
    nome, espera, _desc = cfg
    dest = SAIDA / nome
    dest.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env["WPJ2_AUDIO_WAIT_MS"] = espera
    env["WPJ2_TIMEOUT"] = DUR
    env["WPJ2_OUT"] = str(dest) + os.sep
    # Nomes conferidos em runtime/audio.c; a versao anterior deste script usava
    # variaveis que nao existem e por isso nenhuma captura saiu.
    env["WPJ2_AUDIO"] = "1"
    env["WPJ2_AUDIO_PLAY"] = "1"
    env["WPJ2_AUDIO_WAV"] = str(dest / "audio_capture.wav")
    env["WPJ2_CAPTURE_DIR"] = str(dest)
    with (dest / "corrida.log").open("w", encoding="utf-8", errors="replace") as f:
        subprocess.run([str(EXE), str(ROM)], cwd=PROJ, env=env, stdout=f,
                       stderr=subprocess.STDOUT)
    return nome


def ler(caminho):
    """Le um WAV de 16 bits ignorando o tamanho declarado no cabecalho.

    O runtime so corrige esse campo em `audio_shutdown()`, e o watchdog encerra
    o processo com `TerminateProcess` - entao toda captura interrompida fica com
    `data size = 0` apesar de ter o audio inteiro no arquivo. Ler pelo tamanho
    real do arquivo recupera a captura em vez de descarta-la."""
    dados_brutos = Path(caminho).read_bytes()
    if dados_brutos[:4] != b"RIFF" or dados_brutos[8:12] != b"WAVE":
        raise ValueError("%s nao e WAV" % caminho)
    canais = struct.unpack_from("<H", dados_brutos, 22)[0]
    taxa = struct.unpack_from("<I", dados_brutos, 24)[0]
    bits = struct.unpack_from("<H", dados_brutos, 34)[0]
    if bits != 16:
        raise ValueError("%s: esperava 16 bits" % caminho)
    corpo = dados_brutos[44:]
    corpo = corpo[:len(corpo) - (len(corpo) % (2 * canais))]
    a = struct.unpack("<%dh" % (len(corpo) // 2), corpo)
    return list(a[0::canais]), taxa


def corr(a, b, ia, ib, n):
    sa = sb = sab = 0.0
    for k in range(n):
        x = float(a[ia + k]); y = float(b[ib + k])
        sa += x * x; sb += y * y; sab += x * y
    return sab / ((sa ** 0.5) * (sb ** 0.5)) if sa and sb else 0.0


def curva(caminho, orac, taxa_o):
    """Correlacao com o oraculo, segundo a segundo, seguindo o alinhamento."""
    try:
        nosso, taxa = ler(caminho)
    except Exception as e:
        return None, str(e)
    if taxa != taxa_o:
        return None, "taxa %d != %d" % (taxa, taxa_o)
    JAN, BUSCA = 8192, 3000
    d, t, pontos = 3889, 40000, []
    while t + JAN < len(nosso) and t + JAN + d + BUSCA < len(orac):
        melhor, melhor_c = d, -2.0
        for passo in (16, 1):
            raio = BUSCA if passo == 16 else 24
            for cand in range(melhor - raio, melhor + raio + 1, passo):
                if t + cand < 0 or t + cand + JAN >= len(orac):
                    continue
                c = corr(nosso, orac, t, t + cand, JAN)
                if c > melhor_c:
                    melhor, melhor_c = cand, c
        pontos.append((t / taxa, melhor, melhor_c))
        d = melhor
        t += taxa
    return pontos, None


def achar_wav(pasta):
    for p in sorted(Path(pasta).rglob("*.wav"), key=lambda q: -q.stat().st_size):
        return p
    return None


SAIDA.mkdir(exist_ok=True)
# Uma de cada vez, de proposito. O que esta sob teste e o comportamento da fila
# de saida quando ela enche; tres instancias disputando o mesmo dispositivo de
# audio encheriam as filas umas das outras e mediriam a disputa, nao a mudanca.
# O resto do laboratorio roda em paralelo porque la a interferencia nao existe.
print("rodando %d capturas em sequencia, %s s cada" % (len(CONFIGS), DUR))
for cfg in CONFIGS:
    print("  %-12s ..." % cfg[0], flush=True)
    rodar(cfg)
print("\ncapturas prontas; comparando com o oraculo\n")

orac, taxa_o = ler(ORACULO)

L = []
w = L.append
w("# Comparacao de audio com o oraculo do Project64")
w("")
w("Tres capturas da mesma cena, %s s cada, mudando so o que acontece quando a" % DUR)
w("fila de saida do host enche. O oraculo e `%s`." % ORACULO.name)
w("")
w("A pergunta: a correlacao **para de cair** depois dos primeiros segundos?")
w("")

resumo = []
for nome, espera, desc in CONFIGS:
    wav = achar_wav(SAIDA / nome)
    w("## %s — %s" % (nome, desc))
    w("")
    if not wav:
        w("Nenhum WAV foi produzido. Veja `temp/projeto/comparar_audio/%s/corrida.log`." % nome)
        w("")
        continue
    pontos, erro = curva(wav, orac, taxa_o)
    if erro:
        w("Nao deu para comparar: %s" % erro)
        w("")
        continue

    log = (SAIDA / nome / "corrida.log").read_text(encoding="utf-8", errors="replace")
    for linha in log.splitlines():
        if "fila do host" in linha or "buffer(s), " in linha:
            w("`%s`" % linha.strip())
            w("")

    w("| tempo | deslocamento | correlacao |")
    w("|---:|---:|---:|")
    for t, d, c in pontos:
        w("| %.1f s | %+d | %.3f |" % (t, d, c))
    w("")
    inicio = [c for t, _d, c in pontos if t < 10]
    fim = [c for t, _d, c in pontos if t >= 12]
    mi = sum(inicio) / len(inicio) if inicio else 0
    mf = sum(fim) / len(fim) if fim else 0
    w("Media da correlacao: **%.3f** antes de 10 s, **%.3f** depois de 12 s." % (mi, mf))
    w("")
    resumo.append((nome, mi, mf))

w("## Resumo")
w("")
w("| corrida | antes de 10 s | depois de 12 s | queda |")
w("|---|---:|---:|---:|")
for nome, mi, mf in resumo:
    w("| %s | %.3f | %.3f | %.0f%% |" % (nome, mi, mf,
                                         100 * (1 - mf / mi) if mi else 0))
w("")
w("Se `sem_espera` cair muito e as outras nao, a causa do chiado era o descarte")
w("de buffers. Se as tres cairem igual, era outra coisa e o descarte era sintoma.")

(SAIDA / "COMPARACAO_AUDIO.md").write_text("\n".join(L), encoding="utf-8")
print("temp/projeto/comparar_audio/COMPARACAO_AUDIO.md escrito (%d linhas)." % len(L))
