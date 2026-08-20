import struct, math, pathlib

# Testa offset DC e degraus entre buffers.
#
# Piso grave e correlacionado (aspereza 0.055) sugere componente continua.
# DC constante e inaudivel; DC que muda de degrau a cada buffer produz
# descontinuidade periodica - o "pipoco" relatado. Medimos a media por
# janela (o DC) e o salto entre janelas vizinhas.
TAXA = 22047
ARQS = [("Project64", r"E:\projetos\project-wonder-j2-decomp\analise\oraculo\audio\deep\pj64_audio_oracle.wav"),
        ("nosso",     r"E:\projetos\project-wonder-j2-decomp\temp\projeto\testar\audio_rsp_exato\audio_capture.wav")]

def carregar(p):
    d = pathlib.Path(p).read_bytes()[44:]
    d = d[: len(d) // 4 * 4]
    a = struct.unpack("<%dh" % (len(d) // 2), d)
    return a[0::2], a[1::2]

for nome, caminho in ARQS:
    L, R = carregar(caminho)
    print("=== %s ===" % nome)
    for rot, canal in (("L", L), ("R", R)):
        dc_total = sum(canal) / len(canal)
        print("  %s  DC medio de toda a faixa: %+8.2f" % (rot, dc_total))
    # Janelas curtas: 180 quadros ~ tamanho tipico de buffer AI desta ROM.
    for jan in (180, 512, TAXA // 20):
        medias = [sum(L[i:i + jan]) / jan for i in range(0, len(L) - jan, jan)]
        saltos = [abs(medias[i] - medias[i - 1]) for i in range(1, len(medias))]
        saltos.sort()
        print("  janela=%5d quadros: |DC| medio=%7.2f  max=%8.2f |"
              " salto entre janelas: mediana=%7.2f  p99=%8.2f  max=%8.2f"
              % (jan,
                 sum(abs(m) for m in medias) / len(medias),
                 max(abs(m) for m in medias),
                 saltos[len(saltos) // 2],
                 saltos[int(len(saltos) * .99)],
                 saltos[-1]))
