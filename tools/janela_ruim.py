"""Cruza a curva de correlacao com o log de ALists.

A comparacao com o oraculo mostra um trecho em que a correlacao cai e depois
volta sozinha, sem o alinhamento mudar. Alinhamento intacto elimina sincronismo
e buffer perdido: o que muda e o conteudo das amostras.

Este script separa as ALists executadas dentro da janela ruim das executadas nos
trechos limpos, e mostra o que aparece so na janela ruim. Se um comando ou uma
taxa de reamostragem existir de um lado e nao do outro, e ele o suspeito.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

PROJ = Path(__file__).resolve().parent.parent


def ler_wav(caminho):
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
    return sab / ((sa ** 0.5) * (sb ** 0.5)) if sa and sb else 0.0


def curva(nosso, orac, taxa, base=6000):
    JAN, BUSCA = 8192, 800
    d, t, pontos = base, 40000, []
    while t + JAN < len(nosso) and t + JAN + d + BUSCA < len(orac):
        melhor, melhor_c = d, -2.0
        for cand in range(d - BUSCA, d + BUSCA + 1, 8):
            if t + cand < 0 or t + cand + JAN >= len(orac):
                continue
            c = corr(nosso, orac, t, t + cand, JAN)
            if c > melhor_c:
                melhor, melhor_c = cand, c
        pontos.append((t / taxa, melhor, melhor_c))
        d = melhor
        t += taxa // 2      # meia janela por medida: resolucao melhor
    return pontos


def ler_alists(caminho):
    linhas = Path(caminho).read_text(encoding="utf-8", errors="replace").splitlines()
    cab = [c.strip() for c in linhas[0].lstrip("# ").split(";")]
    saida = []
    for l in linhas[1:]:
        if not l.strip():
            continue
        campos = l.split(";")
        if len(campos) < len(cab):
            continue
        d = {}
        for k, v in zip(cab, campos):
            d[k] = v
        saida.append(d)
    return cab, saida


def main():
    nosso_wav = sys.argv[1]
    orac_wav = sys.argv[2]
    alist_log = sys.argv[3]

    nosso, taxa = ler_wav(nosso_wav)
    orac, _ = ler_wav(orac_wav)
    print("medindo a correlacao ao longo do tempo...")
    pontos = curva(nosso, orac, taxa)

    cs = [c for _t, _d, c in pontos]
    media = sum(cs) / len(cs)
    limiar = media - 0.15
    ruins = [t for t, _d, c in pontos if c < limiar]
    bons = [t for t, _d, c in pontos if c >= media]

    print("\ncorrelacao media %.3f; limiar de 'ruim' em %.3f" % (media, limiar))
    if not ruins:
        print("nenhuma janela abaixo do limiar - nao ha trecho ruim a isolar.")
        return
    print("janela(s) ruim(ns): %.1f s a %.1f s (%d medidas)"
          % (min(ruins), max(ruins), len(ruins)))
    print("trechos limpos    : %d medidas" % len(bons))

    cab, alists = ler_alists(alist_log)
    def dentro(t, faixas, tol=0.6):
        return any(abs(t - f) <= tol for f in faixas)

    ruins_al = [a for a in alists if dentro(float(a["tempo_s"]), ruins)]
    bons_al = [a for a in alists if dentro(float(a["tempo_s"]), bons)]
    print("\nALists na janela ruim: %d   nos trechos limpos: %d"
          % (len(ruins_al), len(bons_al)))
    if not ruins_al or not bons_al:
        print("amostragem insuficiente para comparar.")
        return


    numericos = [c for c in cab if c not in ("tempo_s", "taxas")]
    print("\n%-14s %10s %10s   %s" % ("comando", "ruim", "limpo", "diferenca"))
    print("-" * 60)
    for c in numericos:
        mr = sum(float(a[c]) for a in ruins_al) / len(ruins_al)
        mb = sum(float(a[c]) for a in bons_al) / len(bons_al)
        if mr == 0 and mb == 0:
            continue
        if mb == 0:
            marca = "<== SO na janela ruim"
        elif mr == 0:
            marca = "so nos trechos limpos"
        else:
            r = mr / mb
            marca = ("<== %.1fx mais na janela ruim" % r) if r > 1.3 else (
                    "%.1fx menos" % (1 / r) if r < 0.77 else "")
        print("%-14s %10.1f %10.1f   %s" % (c, mr, mb, marca))

    # Taxas de reamostragem: um valor que so aparece no trecho ruim e o
    # candidato mais direto, porque taxa define o caminho de interpolacao.
    def taxas_de(conj):
        s = set()
        for a in conj:
            for t in a.get("taxas", "").split("/"):
                if t.strip():
                    s.add(int(t))
        return s
    tr, tb = taxas_de(ruins_al), taxas_de(bons_al)
    print("\ntaxas de reamostragem")
    print("  so na janela ruim : %s" % (sorted(tr - tb) or "nenhuma"))
    print("  so nos limpos     : %s" % (sorted(tb - tr) or "nenhuma"))
    print("  em ambos          : %d valores" % len(tr & tb))

    print("\nleitura: um comando presente so na janela ruim, ou uma taxa que so")
    print("aparece la, e o caminho de sintese a investigar. Se nada se separar,")
    print("a degradacao nao esta ligada ao tipo de comando e sim aos dados.")


if __name__ == "__main__":
    main()
