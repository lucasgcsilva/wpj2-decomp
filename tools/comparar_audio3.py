"""Terceira passada: o alinhamento e constante ou escorrega?

Um deslocamento fixo alinhou o inicio com correlacao 0,90, mas o erro medio ao
longo de 240 mil amostras ficou em 80% do sinal. As duas coisas so sao
compativeis se o alinhamento se perder com o tempo.

Se o deslocamento otimo cresce de forma linear, os dois lados consomem amostras
em ritmos diferentes - problema de taxa ou de quanto o AI_LEN diz que sobrou.
Se ele salta em degraus, e perda ou repeticao de buffers inteiros.
"""
from __future__ import annotations

import struct
import sys
import wave
from pathlib import Path


def ler(caminho):
    """Ignora o tamanho declarado no cabecalho e usa o tamanho real do arquivo.

    Captura interrompida pelo watchdog nunca passa por `audio_shutdown()`, que e
    quem corrige esse campo - o WAV fica com `data size = 0` mesmo tendo o audio
    inteiro gravado."""
    b = Path(caminho).read_bytes()
    canais = struct.unpack_from("<H", b, 22)[0]
    taxa = struct.unpack_from("<I", b, 24)[0]
    corpo = b[44:]
    corpo = corpo[:len(corpo) - (len(corpo) % (2 * canais))]
    a = struct.unpack("<%dh" % (len(corpo) // 2), corpo)
    return list(a[0::canais]), taxa


def corr(a, b, ia, ib, n):
    sa = sb = sab = 0.0
    for k in range(n):
        x = float(a[ia + k]); y = float(b[ib + k])
        sa += x * x; sb += y * y; sab += x * y
    if sa == 0 or sb == 0:
        return 0.0
    return sab / ((sa ** 0.5) * (sb ** 0.5))


nosso, taxa = ler(Path(sys.argv[1]))
orac, _ = ler(Path(sys.argv[2]))
base = int(sys.argv[3]) if len(sys.argv) > 3 else 3889

JAN = 8192          # janela de medida
BUSCA = 3000        # quanto procurar em volta do deslocamento anterior
print("janela de %d amostras, busca de +-%d em torno do ultimo achado\n"
      % (JAN, BUSCA))
print("  tempo    deslocamento   correlacao   variacao")

anterior = None
d = base
t = 40000
while t + JAN + d + BUSCA < len(orac) and t + JAN < len(nosso):
    melhor, melhor_c = d, -2.0
    for passo in (16, 1):
        ini = melhor - (BUSCA if passo == 16 else 24)
        fim = melhor + (BUSCA if passo == 16 else 24)
        for cand in range(ini, fim + 1, passo):
            if t + cand < 0 or t + cand + JAN >= len(orac):
                continue
            c = corr(nosso, orac, t, t + cand, JAN)
            if c > melhor_c:
                melhor, melhor_c = cand, c
    var = "" if anterior is None else "%+d" % (melhor - anterior)
    print("  %6.2f s   %+8d      %.3f       %s" % (t / taxa, melhor, melhor_c, var))
    anterior = melhor
    d = melhor
    t += taxa  # uma medida por segundo

print("\nSe a coluna de variacao for sempre positiva e de tamanho parecido, os"
      "\ndois lados correm em ritmos diferentes e a diferenca acumula.")
