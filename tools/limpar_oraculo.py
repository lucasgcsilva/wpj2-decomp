import pathlib, shutil, sys

# Segunda fase: os despejos por buffer do oraculo do Project64.
#
# Sao 623 mil arquivos para 161 MB. Duas das tres pastas sao coletas
# arquivadas ("_anterior_"), e a comparacao com o PJ64 foi descartada por
# medicao (ANALISE_AUDIO.md, 5b: o endereco do estado estava errado).
#
# PRESERVA o pj64_audio_oracle.wav e os manifestos - sao a referencia de
# audio que ainda usamos em comparar_pj64.py.
BASE = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\oraculo\pj64-rdram")
APLICAR = len(sys.argv) > 1 and sys.argv[1] == "aplicar"

alvos = []

# Coletas arquivadas: inteiras.
for d in BASE.glob("audio_deep_anterior_*"):
    if d.is_dir():
        alvos.append(d)

# Coleta atual: so os despejos por buffer, preservando WAV e manifestos.
atual = BASE / "audio_deep"
if atual.is_dir():
    for sub in atual.iterdir():
        if sub.is_dir():
            alvos.append(sub)

total_b = total_n = 0
for p in alvos:
    for f in p.rglob("*"):
        if f.is_file():
            try:
                total_b += f.stat().st_size
                total_n += 1
            except OSError:
                pass

print("%s: %d pasta(s), %.1f MB, %d arquivos"
      % ("APLICANDO" if APLICAR else "SIMULACAO", len(alvos),
         total_b / 1048576, total_n))
for p in alvos:
    print("  %s" % p.relative_to(BASE))

if atual.is_dir():
    print("\npreservados em audio_deep:")
    for f in sorted(atual.iterdir()):
        if f.is_file():
            print("  %-36s %8.2f MB" % (f.name, f.stat().st_size / 1048576))

if APLICAR:
    for p in alvos:
        shutil.rmtree(p, ignore_errors=True)
    print("\nconcluido")
else:
    print("\nNada foi apagado. Rode com 'aplicar'.")
