import struct, pathlib

# Discrimina a ORIGEM do offset DC, que o teste auditivo apontou como o
# defeito que se ouve (ANALISE_AUDIO.md, 5h).
#
#   DC cresce ao longo do tempo  -> acumulo num laco de realimentacao. O anel
#                                   de reverb realimenta o proprio viés e ele
#                                   sobe ate saturar.
#   DC constante desde o inicio  -> viés por amostra, aplicado de forma
#                                   uniforme; nao depende de historico.
#
# As duas causas exigem correcoes completamente diferentes, e ate agora nunca
# foram separadas.
TAXA = 22047
ARQS = [("nosso  ", r"E:\projetos\project-wonder-j2-decomp\temp\projeto\testar\audio_rsp_exato\audio_capture.wav"),
        ("PJ64   ", r"E:\projetos\project-wonder-j2-decomp\analise\oraculo\audio\deep\pj64_audio_oracle.wav")]

def canal_esq(p):
    d = pathlib.Path(p).read_bytes()[44:]
    d = d[: len(d) // 4 * 4]
    return struct.unpack("<%dh" % (len(d) // 2), d)[0::2]

print("DC medio por segundo (canal esquerdo)\n")
print("%6s %s" % ("s", "".join("%10s" % n for n, _ in ARQS)))

faixas = []
for nome, caminho in ARQS:
    try:
        faixas.append(canal_esq(caminho))
    except OSError:
        faixas.append([])

n = min(len(f) // TAXA for f in faixas if f)
for i in range(min(n, 30)):
    linha = "%6d" % i
    for f in faixas:
        if f:
            bloco = f[i * TAXA:(i + 1) * TAXA]
            linha += "%10.1f" % (sum(bloco) / len(bloco))
        else:
            linha += "%10s" % "-"
    print(linha)
