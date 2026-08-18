import struct, math, pathlib

# O sinal excedente e ruido ou conteudo?
#
# Chiado e de banda larga: varia muito entre amostras vizinhas. Musica e
# passa-baixa: varia pouco. A razao rms(x[n]-x[n-1]) / rms(x[n]) separa os
# dois sem FFT - perto de 2 indica energia concentrada no agudo (ruido),
# perto de 0 indica sinal grave e correlacionado (musica).
#
# Nao depende de alinhamento entre as gravacoes, so da natureza do sinal.
TAXA = 22047
JAN = TAXA // 20          # janelas de 50 ms
ARQS = [("Project64", r"E:\projetos\project-wonder-j2-decomp\oraculo\pj64-rdram\audio_deep\pj64_audio_oracle.wav"),
        ("nosso",     r"E:\projetos\project-wonder-j2-decomp\lab\ab_base.wav")]

def carregar(p):
    d = pathlib.Path(p).read_bytes()[44:]
    d = d[: len(d) // 4 * 4]
    a = struct.unpack("<%dh" % (len(d) // 2), d)
    return a[0::2]        # canal esquerdo basta

def rms(v):
    return math.sqrt(sum(float(x) * x for x in v) / len(v)) if v else 0.0

def aspereza(v):
    """Energia da diferenca sobre energia do sinal: alto = agudo/ruidoso."""
    r = rms(v)
    if r <= 0:
        return 0.0
    d = [v[i] - v[i - 1] for i in range(1, len(v))]
    return rms(d) / r

for nome, caminho in ARQS:
    a = carregar(caminho)
    jan = [a[i:i + JAN] for i in range(0, len(a) - JAN, JAN)]
    med = [(rms(j), aspereza(j)) for j in jan]
    med = [m for m in med if m[0] > 0]
    med.sort(key=lambda m: m[0])
    n = len(med)
    quietas = med[: max(1, n // 20)]        # 5% mais silenciosas
    altas   = med[-max(1, n // 20):]        # 5% mais altas
    print("%-10s janelas=%d" % (nome, n))
    print("   5%% mais quietas : RMS medio=%8.1f   aspereza=%.3f"
          % (sum(m[0] for m in quietas) / len(quietas),
             sum(m[1] for m in quietas) / len(quietas)))
    print("   5%% mais altas   : RMS medio=%8.1f   aspereza=%.3f"
          % (sum(m[0] for m in altas) / len(altas),
             sum(m[1] for m in altas) / len(altas)))
    print("   janela minima   : RMS=%8.1f   aspereza=%.3f"
          % (med[0][0], med[0][1]))
