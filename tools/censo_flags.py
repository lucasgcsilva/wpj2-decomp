import re, collections, pathlib

# Apura o censo [acmd-flags] gravado em toda a execucao (nao so nas primeiras
# tarefas, como fazia o dump [acmd]). Interessa saber quais opcodes aparecem
# de fato nas listas musicais e com que flags.
LAB = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\lab")
RX = re.compile(r"\[acmd-flags\]\s+(\S+)\s+flags=0x([0-9A-Fa-f]{2}).*tarefa de audio (\d+)")

combos = collections.defaultdict(lambda: [10**9, 0])
for log in LAB.glob("*.log"):
    for linha in log.read_text(encoding="utf-8", errors="replace").splitlines():
        m = RX.search(linha)
        if not m:
            continue
        chave = (m.group(1), int(m.group(2), 16))
        tarefa = int(m.group(3))
        combos[chave][0] = min(combos[chave][0], tarefa)
        combos[chave][1] += 1

print("combinacoes distintas:", len(combos))
print("%-11s %-7s %-14s %s" % ("opcode", "flags", "1a tarefa", "logs"))
for (nome, fl), (prim, n) in sorted(combos.items()):
    print("%-11s 0x%02X    %-14d %d" % (nome, fl, prim, n))
