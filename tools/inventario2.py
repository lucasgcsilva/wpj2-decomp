import pathlib, time

# Detalha as pastas candidatas a limpeza, um nivel abaixo, para separar
# captura de referencia (preservar) de artefato regeneravel (apagar).
RAIZ = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp")

for alvo in ["oraculo", "lab", "lab_test", "comparacao"]:
    base = RAIZ / alvo
    if not base.exists():
        continue
    print("\n===== %s =====" % alvo)
    itens = []
    for d in sorted(base.iterdir()):
        total = n = 0
        recente = 0
        if d.is_dir():
            for f in d.rglob("*"):
                if f.is_file():
                    try:
                        st = f.stat()
                    except OSError:
                        continue
                    total += st.st_size
                    n += 1
                    recente = max(recente, st.st_mtime)
        else:
            st = d.stat()
            total, n, recente = st.st_size, 1, st.st_mtime
        itens.append((total, d.name, n, recente, d.is_dir()))
    itens.sort(reverse=True)
    for total, nome, n, recente, ehdir in itens[:14]:
        data = time.strftime("%d/%m", time.localtime(recente)) if recente else "-"
        print("  %-42s %8.1f MB %7d arq  %s%s"
              % (nome, total / 1048576, n, data, "  [dir]" if ehdir else ""))
