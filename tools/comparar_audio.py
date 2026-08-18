"""Compara duas capturas de audio e diz *onde* elas divergem.

A pergunta que isto responde nao e "sao diferentes" - sao, obviamente. E:

  - Ha um deslocamento constante entre elas? Entao o problema e alinhamento de
    buffer, e a correcao e a mascara do AI_LEN.
  - A divergencia comeca sempre na mesma posicao dentro de um bloco de 16
    amostras? Entao e o decodificador ADPCM, que trabalha em blocos de 16.
  - Os canais batem quando trocados? Entao e ordem estereo.
  - O nosso tem energia em frequencias que o oraculo nao tem? Entao ha ruido
    somado, e a distribuicao diz se e branco (lixo) ou harmonico (coeficiente).

Cada uma dessas leva a um conserto diferente, e nenhuma se confirma de ouvido.
"""
from __future__ import annotations

import struct
import sys
import wave
from pathlib import Path


def ler(caminho):
    with wave.open(str(caminho), "rb") as w:
        canais, larg, taxa, quadros = (w.getnchannels(), w.getsampwidth(),
                                       w.getframerate(), w.getnframes())
        dados = w.readframes(quadros)
    if larg != 2:
        raise SystemExit("%s: esperava 16 bits, veio %d" % (caminho, larg * 8))
    amostras = struct.unpack("<%dh" % (len(dados) // 2), dados)
    esq = list(amostras[0::canais]) if canais > 1 else list(amostras)
    dir_ = list(amostras[1::canais]) if canais > 1 else list(amostras)
    return dict(caminho=caminho, canais=canais, taxa=taxa, esq=esq, dir=dir_,
                quadros=len(esq))


def energia(v):
    if not v:
        return 0.0
    return (sum(float(x) * x for x in v) / len(v)) ** 0.5


def correlacao(a, b, desloc, n):
    """Correlacao normalizada entre a[i] e b[i+desloc], sobre n amostras."""
    sa = sb = sab = 0.0
    usados = 0
    for i in range(n):
        j = i + desloc
        if j < 0 or j >= len(b) or i >= len(a):
            continue
        x, y = float(a[i]), float(b[j])
        sa += x * x
        sb += y * y
        sab += x * y
        usados += 1
    if usados < 64 or sa == 0 or sb == 0:
        return 0.0
    return sab / ((sa ** 0.5) * (sb ** 0.5))


def melhor_deslocamento(a, b, limite=4096, n=32768):
    """Procura o deslocamento que melhor alinha os dois sinais."""
    # Busca grossa e depois fina, para nao varrer 8192 posicoes amostra a amostra.
    melhor, melhor_c = 0, -2.0
    for d in range(-limite, limite + 1, 32):
        c = correlacao(a, b, d, n)
        if c > melhor_c:
            melhor, melhor_c = d, c
    for d in range(melhor - 40, melhor + 41):
        c = correlacao(a, b, d, n)
        if c > melhor_c:
            melhor, melhor_c = d, c
    return melhor, melhor_c


def perfil_no_bloco(a, b, desloc, bloco=16, n=200000):
    """Erro medio por posicao dentro de um bloco de N amostras.

    O ADPCM do N64 decodifica em blocos de 16 amostras a partir de um estado
    preditivo. Se o estado nao sobrevive entre blocos, o erro tem forma de
    dente de serra: pequeno no inicio do bloco, crescente ate o fim. Se o erro
    for plano, o problema nao esta no ADPCM."""
    soma = [0.0] * bloco
    cont = [0] * bloco
    for i in range(min(n, len(a))):
        j = i + desloc
        if j < 0 or j >= len(b):
            continue
        e = float(a[i]) - float(b[j])
        soma[i % bloco] += e * e
        cont[i % bloco] += 1
    return [(soma[k] / cont[k]) ** 0.5 if cont[k] else 0.0 for k in range(bloco)]


def espectro_grosso(v, taxa, n=16384):
    """Energia em quatro faixas, via soma de diferencas - suficiente para
    distinguir ruido branco (energia espalhada no agudo) de sinal musical."""
    v = v[:n]
    if len(v) < 1024:
        return (0, 0, 0, 0)
    d1 = sum(abs(v[i] - v[i - 1]) for i in range(1, len(v))) / len(v)
    d2 = sum(abs(v[i] - 2 * v[i - 1] + v[i - 2]) for i in range(2, len(v))) / len(v)
    med = sum(abs(x) for x in v) / len(v)
    # d1/med alto = muita variacao amostra a amostra = conteudo agudo/ruidoso.
    return (med, d1, d2, (d1 / med) if med else 0.0)


def main():
    nosso = ler(Path(sys.argv[1]))
    orac = ler(Path(sys.argv[2]))

    print("nosso   : %s" % nosso["caminho"])
    print("          %d canais, %d Hz, %d quadros (%.1f s)"
          % (nosso["canais"], nosso["taxa"], nosso["quadros"],
             nosso["quadros"] / max(1, nosso["taxa"])))
    print("oraculo : %s" % orac["caminho"])
    print("          %d canais, %d Hz, %d quadros (%.1f s)"
          % (orac["canais"], orac["taxa"], orac["quadros"],
             orac["quadros"] / max(1, orac["taxa"])))
    if nosso["taxa"] != orac["taxa"]:
        print("\n!! taxas diferentes: %d contra %d. Qualquer comparacao amostra a"
              " amostra fica sem sentido antes de igualar isso."
              % (nosso["taxa"], orac["taxa"]))

    print("\n-- nivel --")
    for nome, d in (("nosso", nosso), ("oraculo", orac)):
        print("  %-8s RMS esq=%8.1f  dir=%8.1f  pico=%6d"
              % (nome, energia(d["esq"][:200000]), energia(d["dir"][:200000]),
                 max((abs(x) for x in d["esq"][:200000]), default=0)))

    print("\n-- alinhamento --")
    d_ee, c_ee = melhor_deslocamento(nosso["esq"], orac["esq"])
    d_ed, c_ed = melhor_deslocamento(nosso["esq"], orac["dir"])
    print("  nosso.esq x oraculo.esq : deslocamento %+d, correlacao %.3f" % (d_ee, c_ee))
    print("  nosso.esq x oraculo.dir : deslocamento %+d, correlacao %.3f" % (d_ed, c_ed))
    if c_ed > c_ee + 0.05:
        print("  => os canais estao TROCADOS: o nosso esquerdo casa com o direito"
              " do oraculo.")
    elif max(c_ee, c_ed) < 0.3:
        print("  => nao ha alinhamento em nenhum deslocamento: os sinais nao sao"
              " a mesma musica, ou o nosso e ruido dominante.")
    else:
        print("  => alinham com deslocamento de %d amostras (%.1f ms)."
              % (d_ee, 1000.0 * d_ee / max(1, nosso["taxa"])))

    print("\n-- erro por posicao dentro do bloco de 16 (ADPCM) --")
    perfil = perfil_no_bloco(nosso["esq"], orac["esq"], d_ee)
    pico = max(perfil) or 1.0
    for k, e in enumerate(perfil):
        print("  %2d  %8.1f  %s" % (k, e, "#" * int(40 * e / pico)))
    if perfil[0] and perfil[-1] / perfil[0] > 1.8:
        print("  => erro cresce ao longo do bloco: assinatura de estado preditivo"
              " perdido entre blocos, que e exatamente o ADPCM.")
    elif max(perfil) and (max(perfil) - min(perfil)) / max(perfil) < 0.25:
        print("  => erro plano dentro do bloco: o ADPCM nao e o culpado.")

    print("\n-- textura do sinal --")
    for nome, d in (("nosso", nosso), ("oraculo", orac)):
        med, d1, d2, raz = espectro_grosso(d["esq"], d["taxa"])
        print("  %-8s |media|=%7.1f  variacao=%7.1f  razao=%.2f" % (nome, med, d1, raz))
    _, _, _, r_n = espectro_grosso(nosso["esq"], nosso["taxa"])
    _, _, _, r_o = espectro_grosso(orac["esq"], orac["taxa"])
    if r_o and r_n > r_o * 1.5:
        print("  => o nosso varia muito mais entre amostras vizinhas: ha conteudo"
              " de alta frequencia que o oraculo nao tem. Isso e o chiado.")


if __name__ == "__main__":
    main()
