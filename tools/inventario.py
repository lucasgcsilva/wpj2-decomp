import pathlib, collections, time

# Inventario para decidir o que limpar. Nao apaga nada - so mostra tamanho,
# quantidade e data do arquivo mais recente de cada pasta, para separar o que
# e fonte do que e artefato regeneravel.
RAIZ = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp")
IGNORAR = {".git"}

linhas = []
for d in sorted(RAIZ.iterdir()):
    if not d.is_dir() or d.name in IGNORAR:
        continue
    total = 0
    n = 0
    recente = 0
    ext = collections.Counter()
    for f in d.rglob("*"):
        if f.is_file():
            try:
                st = f.stat()
            except OSError:
                continue
            total += st.st_size
            n += 1
            recente = max(recente, st.st_mtime)
            ext[f.suffix.lower()] += 1
    principais = ", ".join("%s:%d" % (e or "(sem)", c) for e, c in ext.most_common(4))
    linhas.append((total, d.name, n, recente, principais))

linhas.sort(reverse=True)
print("%-26s %9s %7s  %-10s  %s" % ("pasta", "MB", "arqs", "modificado", "principais"))
for total, nome, n, recente, principais in linhas:
    data = time.strftime("%d/%m/%Y", time.localtime(recente)) if recente else "-"
    print("%-26s %9.1f %7d  %-10s  %s" % (nome, total / 1048576, n, data, principais))

# Arquivos soltos na raiz
soltos = [(f.stat().st_size, f.name) for f in RAIZ.iterdir() if f.is_file()]
soltos.sort(reverse=True)
print("\narquivos na raiz: %d" % len(soltos))
for tam, nome in soltos[:25]:
    print("  %9.2f MB  %s" % (tam / 1048576, nome))
