import struct, math, pathlib

# Compara a nossa saida real com o oraculo do Project64.
#
# As duas gravacoes comecam em pontos diferentes da musica, entao comparar
# segundo a segundo nao vale. Usamos metricas independentes de alinhamento:
# distribuicao do RMS por segundo (percentis) e saturacao normalizada.
P_PJ64 = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\oraculo\pj64-rdram\audio_deep\pj64_audio_oracle.wav")
P_NOSSO = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\lab\ab_base.wav")
TAXA = 22047

def carregar(p):
    b = p.read_bytes()
    d = b[44:]
    d = d[: len(d) // 4 * 4]
    return struct.unpack("<%dh" % (len(d) // 2), d)

def rms(v):
    return math.sqrt(sum(float(x) * x for x in v) / len(v)) if v else 0.0

def perfil(a):
    passo = TAXA * 2
    return [rms(a[i:i + passo]) for i in range(0, len(a) - passo, passo)]

def pct(v, q):
    s = sorted(v)
    return s[min(len(s) - 1, int(q * len(s)))]

for nome, p in (("Project64", P_PJ64), ("nosso", P_NOSSO)):
    if not p.exists():
        print("FALTA:", p)
        continue
    a = carregar(p)
    seg = [x for x in perfil(a) if x > 100]   # descarta silencio/menu
    sat = sum(1 for x in a if x >= 32767 or x <= -32768)
    dur = len(a) / 2 / TAXA
    print("%-10s dur=%6.1fs  RMS=%7.1f  pico=%6d  satura=%3d (%.2f/s)"
          % (nome, dur, rms(a), max(abs(x) for x in a), sat, sat / dur))
    if seg:
        print("           RMS/s  p10=%7.1f  mediana=%7.1f  p90=%7.1f  max=%7.1f"
              % (pct(seg, .10), pct(seg, .50), pct(seg, .90), max(seg)))
    globals()[nome] = seg

if "Project64" in globals() and "nosso" in globals():
    a, b = globals()["Project64"], globals()["nosso"]
    print("\ndiferenca (nosso - PJ64), em dB, por percentil:")
    for q in (.10, .25, .50, .75, .90):
        x, y = pct(a, q), pct(b, q)
        print("  p%-3d  PJ64=%7.1f  nosso=%7.1f  %+6.2f dB"
              % (q * 100, x, y, 20 * math.log10(y / x)))
