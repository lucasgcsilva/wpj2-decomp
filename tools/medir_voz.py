import struct, pathlib

# Mede o DC de vozes isoladas. Se UMA voz sozinha ja tiver media nao-nula, a
# origem do offset esta no decodificador ADPCM ou na mistura por voz; se todas
# forem centradas, o DC nasce do acumulo entre vozes ou do anel de reverb.
TAXA = 22047
BASE = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\lab")

def esq(p):
    d = p.read_bytes()[44:]
    d = d[: len(d) // 4 * 4]
    return struct.unpack("<%dh" % (len(d) // 2), d)[0::2]

print("%-14s %10s %10s %10s %8s" % ("faixa", "DC total", "DC 10-18s", "RMS", "pico"))
for nome in ["voz_0.wav", "voz_1.wav", "voz_2.wav",
             "ab_base.wav", "ab_no_wet.wav", "ab_semround.wav"]:
    p = BASE / nome
    if not p.exists() or p.stat().st_size < 1000:
        continue
    a = esq(p)
    dc = sum(a) / len(a)
    janela = a[10 * TAXA:18 * TAXA]
    dcj = sum(janela) / len(janela) if janela else 0.0
    rms = (sum(float(x) * x for x in a) / len(a)) ** 0.5
    print("%-14s %10.1f %10.1f %10.1f %8d"
          % (nome, dc, dcj, rms, max(abs(x) for x in a)))
