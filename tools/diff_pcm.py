"""Compara o PCM que o HLE e o microcodigo produziram a partir da mesma AList.

Este e o teste que faltava desde o comeco: mesma entrada, mesma RDRAM, mesmo
estado de voz, duas implementacoes. Sem fila de saida, sem taxa de amostragem e
sem API do Windows no caminho.

Se as duas saidas forem iguais, a divergencia observada na captura completa vem
de fora do sintetizador. Se forem diferentes, o ponto onde elas comecam a
divergir aponta o comando responsavel.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

d = Path(sys.argv[1])
hle = d / "hle_ai_pcm.bin"
nat = d / "native_ai_pcm.bin"
if not nat.exists():
    nat = d / "nativo_ai_pcm.bin"

if not hle.exists() or not nat.exists():
    achados = sorted(p.name for p in d.glob("*ai_pcm*.bin"))
    print("  faltou saida para comparar; arquivos presentes: %s" % (achados or "nenhum"))
    raise SystemExit(0)

a = hle.read_bytes()
b = nat.read_bytes()
n = min(len(a), len(b))
if n < 4:
    print("  saidas vazias")
    raise SystemExit(0)

sa = [struct.unpack_from(">h", a, i)[0] for i in range(0, n - 1, 2)]
sb = [struct.unpack_from(">h", b, i)[0] for i in range(0, n - 1, 2)]

difs = [i for i, (x, y) in enumerate(zip(sa, sb)) if x != y]
if not difs:
    print("  IGUAIS: %d amostras identicas." % len(sa))
    print("  => o sintetizador nao e a origem da divergencia.")
    raise SystemExit(0)

erro = max(abs(x - y) for x, y in zip(sa, sb))
rms_a = (sum(float(x) * x for x in sa) / len(sa)) ** 0.5
rms_e = (sum(float(x - y) ** 2 for x, y in zip(sa, sb)) / len(sa)) ** 0.5
print("  DIFEREM: %d de %d amostras (%.1f%%)" % (len(difs), len(sa),
                                                 100.0 * len(difs) / len(sa)))
print("  primeira divergencia na amostra %d de %d" % (difs[0], len(sa)))
print("  erro maximo %d; RMS do erro %.1f contra sinal %.1f (%.1f%%)"
      % (erro, rms_e, rms_a, 100.0 * rms_e / (rms_a or 1)))
print("  primeiras divergencias (indice: hle vs nativo):")
for i in difs[:8]:
    print("    %4d: %7d  %7d   delta %+d" % (i, sa[i], sb[i], sb[i] - sa[i]))
