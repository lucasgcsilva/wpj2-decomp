"""Segunda passada: onde o erro se concentra no tempo.

A primeira comparacao mostrou correlacao 0,90 e erro plano dentro do bloco de
16 amostras. Isso elimina o ADPCM e diz que a musica esta certa, com um erro
grande somado por cima.

Falta saber *quando* esse erro acontece. Se ele for uniforme, e ruido somado em
todo lugar. Se ele tiver picos periodicos, o periodo denuncia a origem: um pico
a cada N amostras aponta para a costura entre buffers de AI, e N diz qual e o
tamanho do buffer envolvido.
"""
from __future__ import annotations

import struct
import sys
import wave
from pathlib import Path


def ler(caminho):
    with wave.open(str(caminho), "rb") as w:
        canais, taxa, quadros = w.getnchannels(), w.getframerate(), w.getnframes()
        dados = w.readframes(quadros)
    a = struct.unpack("<%dh" % (len(dados) // 2), dados)
    return list(a[0::canais]), taxa


def rms(v):
    return (sum(float(x) * x for x in v) / len(v)) ** 0.5 if v else 0.0


nosso, taxa = ler(Path(sys.argv[1]))
orac, _ = ler(Path(sys.argv[2]))
desloc = int(sys.argv[3]) if len(sys.argv) > 3 else 3889

# Trecho com conteudo, ja alinhado.
ini = 60000
n = 240000
a = nosso[ini:ini + n]
b = orac[ini + desloc:ini + desloc + n]
n = min(len(a), len(b))
a, b = a[:n], b[:n]

print("comparando %d amostras alinhadas (deslocamento %+d)\n" % (n, desloc))

# Ganho otimo: se o erro cair muito ao escalar, a diferenca e so de volume.
num = sum(float(x) * float(y) for x, y in zip(a, b))
den = sum(float(x) * float(x) for x in a) or 1.0
g = num / den
err_bruto = rms([float(x) - float(y) for x, y in zip(a, b)])
err_ganho = rms([g * float(x) - float(y) for x, y in zip(a, b)])
print("ganho otimo do nosso para casar com o oraculo : %.3f" % g)
print("erro RMS sem correcao de ganho                : %.1f" % err_bruto)
print("erro RMS depois de aplicar o ganho            : %.1f" % err_ganho)
print("sinal RMS do oraculo                          : %.1f" % rms(b))
print("erro relativo depois do ganho                 : %.1f%%\n"
      % (100.0 * err_ganho / (rms(b) or 1)))

# Onde o erro mora no tempo.
erro = [abs(g * float(x) - float(y)) for x, y in zip(a, b)]
janela = 512
blocos = [rms(erro[i:i + janela]) for i in range(0, len(erro) - janela, janela)]
med = sum(blocos) / len(blocos)
piores = sorted(range(len(blocos)), key=lambda i: -blocos[i])[:10]
print("erro medio por janela de %d amostras: %.1f" % (janela, med))
print("janelas piores (indice, erro, x da media):")
for i in piores:
    print("   %5d  %8.1f  %.1fx  em %.2f s"
          % (i, blocos[i], blocos[i] / med, (ini + i * janela) / taxa))

# Periodicidade: o erro se repete a cada quantas amostras?
print("\nerro medio por posicao, para varios periodos candidatos:")
for periodo in (160, 184, 320, 368, 512, 640, 720, 736, 1024, 1472):
    soma = [0.0] * periodo
    cont = [0] * periodo
    for i, e in enumerate(erro):
        soma[i % periodo] += e * e
        cont[i % periodo] += 1
    perfil = [(soma[k] / cont[k]) ** 0.5 for k in range(periodo) if cont[k]]
    if not perfil:
        continue
    pico, vale = max(perfil), min(perfil)
    print("   periodo %4d : pico/vale = %.2f %s" % (
        periodo, pico / (vale or 1),
        "<== concentrado" if pico > vale * 1.6 else ""))
