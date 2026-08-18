import pathlib, shutil, sys, time

# Limpeza conservadora: apaga apenas artefato regeneravel, com alvos
# explicitos. Nunca toca em fonte, documentacao ou captura de referencia.
#
# Rode primeiro sem argumento para ver o que seria apagado.
# Rode com "aplicar" para executar.
RAIZ = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp")
APLICAR = len(sys.argv) > 1 and sys.argv[1] == "aplicar"

# --- Pastas inteiras, regeneraveis -----------------------------------------
PASTAS = [
    "build",        # objetos e exes; build_probe.cmd refaz
    "lab_test",     # saidas de sondas de 13-14/08, substituidas pelas atuais
    "sweep",        # logs de varredura antiga
    "prototipos",   # exes de prototipo, ja superados
]

# --- Padroes de arquivo, por pasta -----------------------------------------
PADROES = [
    ("lab", "*.ppm"),        # despejos de framebuffer, regeneraveis
    ("lab", "*.log"),
    ("lab", "*.txt"),
    ("lab", "*.bin"),
    ("lab", "*.png"),
    (".",   "*.ppm"),        # despejos soltos na raiz
    (".",   "rebuild.log"),
]

# --- Arquivos especificos ---------------------------------------------------
ARQUIVOS = [
    "wpj2_visual.exe", "wpj2_visual.map",
    "wpj2_visual_antes_audio_compat.exe", "wpj2_visual_antes_audio_compat.map",
    "audio_oracle_test.obj", "audio_native_oracle_test.obj",
    "lab/wpj2_trace.exe", "lab/wpj2_trace.map",
]

# --- Preservar sempre, mesmo que casem com padrao acima ---------------------
PRESERVAR = {
    "ab_base.wav", "ab_no_wet.wav", "ab_sem_polef.wav", "ab_corrigido.wav",
    "saida.wav", "audio_capture.wav",
}

total_bytes = 0
total_arqs = 0
acoes = []

def medir(p):
    b = n = 0
    if p.is_dir():
        for f in p.rglob("*"):
            if f.is_file():
                try:
                    b += f.stat().st_size
                    n += 1
                except OSError:
                    pass
    elif p.is_file():
        b, n = p.stat().st_size, 1
    return b, n

for nome in PASTAS:
    p = RAIZ / nome
    if p.exists():
        b, n = medir(p)
        total_bytes += b; total_arqs += n
        acoes.append(("dir", p, b, n))

for pasta, padrao in PADROES:
    base = RAIZ if pasta == "." else RAIZ / pasta
    if not base.exists():
        continue
    for f in base.glob(padrao):
        if f.is_file() and f.name not in PRESERVAR:
            b, n = medir(f)
            total_bytes += b; total_arqs += n
            acoes.append(("arq", f, b, n))

for rel in ARQUIVOS:
    f = RAIZ / rel
    if f.is_file():
        b, n = medir(f)
        total_bytes += b; total_arqs += n
        acoes.append(("arq", f, b, n))

print("%s: %d alvos, %.1f MB, %d arquivos"
      % ("APLICANDO" if APLICAR else "SIMULACAO", len(acoes),
         total_bytes / 1048576, total_arqs))

for tipo, p, b, n in acoes:
    if b > 1048576 or tipo == "dir":
        print("  %-6s %-40s %8.1f MB %6d arq" % (tipo, p.name, b / 1048576, n))

if APLICAR:
    erros = 0
    for tipo, p, b, n in acoes:
        try:
            if tipo == "dir":
                shutil.rmtree(p, ignore_errors=True)
            else:
                p.unlink()
        except OSError:
            erros += 1
    print("\nconcluido, %d erro(s)" % erros)
else:
    print("\nNada foi apagado. Rode com 'aplicar' para executar.")
