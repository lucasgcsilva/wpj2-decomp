import struct, math, pathlib, sys

# Compara as variantes A/B do isolamento de reverb. Interessa o pico (estouro
# de 16 bits = "pipoco") e o RMS por segundo (energia somada em excesso).
# O cabecalho declara 0 quadros porque audio_shutdown nao roda sob
# TerminateProcess; lemos pelo tamanho do arquivo.
LAB = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\temp\projeto\audio_ab")
TAXA = 22047

def carregar(nome):
    p = LAB / nome
    if not p.exists() or p.stat().st_size < 1000:
        return None
    d = p.read_bytes()[44:]
    d = d[: len(d) // 4 * 4]
    return struct.unpack("<%dh" % (len(d) // 2), d)

def rms(v):
    return math.sqrt(sum(float(x) * x for x in v) / len(v)) if v else 0.0

nomes = ["ab_base.wav", "ab_no_wet.wav", "ab_sem_polef.wav", "ab_seco.wav"]
faixas = {n: carregar(n) for n in nomes}
faixas = {n: v for n, v in faixas.items() if v}

print("%-18s %9s %9s %8s %8s" % ("variante", "RMS", "pico", "satura", "dur(s)"))
for n, a in faixas.items():
    pico = max(abs(x) for x in a)
    sat = sum(1 for x in a if x >= 32767 or x <= -32768)
    print("%-18s %9.1f %9d %8d %8.1f"
          % (n, rms(a), pico, sat, len(a) / 2 / TAXA))

print("\nRMS por segundo:")
print("%6s" % "s", "".join("%18s" % n for n in faixas))
n0 = len(next(iter(faixas.values())))
for ini in range(0, n0 // 2 - TAXA, TAXA * 2):
    linha = "%6.0f" % (ini / TAXA)
    for n, a in faixas.items():
        linha += "%18.1f" % rms(a[ini * 2:(ini + TAXA) * 2])
    print(linha)
