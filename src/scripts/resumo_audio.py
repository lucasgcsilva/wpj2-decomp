import sys, struct, pathlib

# Resumo de um WAV de execucao: DC, RMS e saturacao.
#
# Existe por causa de uma licao cara (ANALISE_AUDIO.md, 5k): a metrica de
# divergencia contra o microcodigo PREMIA SILENCIO. Uma alteracao que atenue o
# sinal "melhora" o numero. Por isso nivel e offset entram sempre junto, e o
# TESTAR.bat chama este resumo ao fim de qualquer perfil que grave audio.
#
# Referencia medida do Project64 na mesma trilha: DC ~ -20.
TAXA = 22047

def canais(p):
    b = pathlib.Path(p).read_bytes()
    if len(b) < 45:
        return [], []
    # O cabecalho declara 0 quadros: audio_shutdown nao roda quando a janela e
    # fechada a forca. Lemos pelo tamanho do arquivo.
    d = b[44:]
    d = d[: len(d) // 4 * 4]
    a = struct.unpack("<%dh" % (len(d) // 2), d)
    return a[0::2], a[1::2]

def rms(v):
    return (sum(float(x) * x for x in v) / len(v)) ** 0.5 if v else 0.0

if len(sys.argv) < 2:
    sys.exit(0)

L, R = canais(sys.argv[1])
if not L:
    print("  [audio] WAV vazio ou curto demais")
    sys.exit(0)

dc = sum(L) / len(L)
pico = max(max(abs(x) for x in L), max(abs(x) for x in R))
sat = sum(1 for x in L if x >= 32767 or x <= -32768)
dur = len(L) / TAXA

print("  [audio] %.1fs  DC=%+.1f (PJ64 ~ -20)  RMS=%.0f  pico=%d  satura=%d"
      % (dur, dc, rms(L), pico, sat))

# O DC nao e constante: ele acompanha a densidade de vozes (5i). Mostrar por
# trecho evita concluir a partir de uma media que esconde a janela ruim.
if dur >= 12:
    print("  [audio] DC por trecho:", end="")
    passo = int(TAXA * 4)
    for i in range(0, min(len(L) - passo, passo * 7), passo):
        print(" %+.0f" % (sum(L[i:i + passo]) / passo), end="")
    print()
