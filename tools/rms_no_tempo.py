import struct, pathlib

# A remocao do +0x4000 corrigiu o DC (+278 -> -24) mas o RMS caiu 10x.
# Isso pode ser: (a) atenuacao real do sinal, (b) a captura pegou um trecho
# silencioso, ou (c) a musica parou. Medir por segundo separa os tres.
TAXA = 22047
BASE = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\lab")
ARQS = ["ab_base.wav", "ab_semround.wav"]

def esq(p):
    d = p.read_bytes()[44:]
    d = d[: len(d) // 4 * 4]
    return struct.unpack("<%dh" % (len(d) // 2), d)[0::2]

faixas = []
for nome in ARQS:
    p = BASE / nome
    faixas.append(esq(p) if p.exists() and p.stat().st_size > 1000 else [])

print("%5s %12s %12s" % ("s", ARQS[0], ARQS[1]))
n = max(len(f) // TAXA for f in faixas if f)
for i in range(min(n, 30)):
    linha = "%5d" % i
    for f in faixas:
        if f and (i + 1) * TAXA <= len(f):
            b = f[i * TAXA:(i + 1) * TAXA]
            rms = (sum(float(x) * x for x in b) / len(b)) ** 0.5
            linha += "%12.0f" % rms
        else:
            linha += "%12s" % "-"
    print(linha)
