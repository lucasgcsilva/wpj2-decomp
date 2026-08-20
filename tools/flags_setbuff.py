import re, collections, pathlib

# Verificacao independente do printf: le o dump [acmd] dos logs e apura,
# por opcode, quais bytes de flags (w0 >> 16 & 0xFF) a ROM realmente emite.
# O HLE decide ramos por igualdade em alguns comandos; o Project64 decide por
# teste de bit. Qualquer flags fora do conjunto tratado vira comando ignorado.
LAB = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\temp\projeto\laboratorio")
RX = re.compile(r"\[acmd\]\s+\d+\s+(\S+)\s+w0=0x([0-9A-Fa-f]{8})\s+w1=0x([0-9A-Fa-f]{8})")

por_op = collections.defaultdict(collections.Counter)
total = 0
for log in LAB.glob("*.log"):
    for linha in log.read_text(encoding="utf-8", errors="replace").splitlines():
        m = RX.search(linha)
        if not m:
            continue
        nome, w0 = m.group(1), int(m.group(2), 16)
        por_op[nome][(w0 >> 16) & 0xFF] += 1
        total += 1

print("comandos lidos:", total)
for nome in sorted(por_op):
    itens = sorted(por_op[nome].items())
    print("%-11s %s" % (nome, "  ".join("0x%02X=%d" % (f, n) for f, n in itens)))
