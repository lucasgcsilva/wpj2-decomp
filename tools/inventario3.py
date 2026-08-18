import pathlib, time

# Detalha oraculo/pj64-rdram, onde estao 615 mil arquivos. Separar a captura
# de referencia que ainda vale (o WAV do Project64) dos despejos por buffer,
# que so fariam falta se a comparacao com o PJ64 fosse retomada - e ela foi
# descartada (ANALISE_AUDIO.md, secao 5b).
base = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\oraculo\pj64-rdram")

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
    itens.append((n, total, d.name, recente, d.is_dir()))

itens.sort(reverse=True)
for n, total, nome, recente, ehdir in itens[:18]:
    data = time.strftime("%d/%m", time.localtime(recente)) if recente else "-"
    print("%-46s %7d arq %8.1f MB  %s%s"
          % (nome, n, total / 1048576, data, "  [dir]" if ehdir else ""))
