import pathlib, re

# Cruza os simbolos do wonder-source (decompilacao do MESMO jogo) com o que
# nos deduzimos por engenharia reversa. Serve para confirmar ou corrigir as
# nossas deducoes e medir quanto do mapa ainda esta anonimo do nosso lado.
RAIZ = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp")
WS = RAIZ / "tools" / "wonder-source"

def linhas(p):
    return p.read_text(encoding="utf-8", errors="replace").splitlines() \
        if p.exists() else []

# wonder-source: "nome = 0x800XXXXX;"
ext = {}
for arq in ["libultra_symbols.txt", "symbol_addrs.txt"]:
    for l in linhas(WS / arq):
        m = re.search(r"(\w+)\s*=\s*0x([0-9A-Fa-f]{6,8})", l)
        if m:
            ext[int(m.group(2), 16)] = m.group(1)

# nosso: "0x800XXXXX = nome   # comentario"
nosso = {}
for l in linhas(RAIZ / "libultra_names.txt"):
    m = re.search(r"0x([0-9A-Fa-f]{6,8})\s*=\s*(\w+)", l)
    if m:
        nosso[int(m.group(1), 16)] = m.group(2)

print("simbolos no wonder-source : %d" % len(ext))
print("nomes deduzidos por nos   : %d" % len(nosso))

iguais, divergentes, ausentes = [], [], []
for addr, meu in sorted(nosso.items()):
    deles = ext.get(addr)
    if deles is None:
        ausentes.append((addr, meu))
    elif deles == meu:
        iguais.append((addr, meu))
    else:
        divergentes.append((addr, meu, deles))

print("\nconfirmados: %d | divergentes: %d | ausentes na referencia: %d"
      % (len(iguais), len(divergentes), len(ausentes)))

if divergentes:
    print("\n--- DIVERGENTES (a referencia manda) ---")
    for addr, meu, deles in divergentes:
        print("  0x%08X  nosso=%-24s wonder=%s" % (addr, meu, deles))

if ausentes:
    print("\n--- so nossos ---")
    for addr, meu in ausentes:
        print("  0x%08X  %s" % (addr, meu))

print("\n--- funcoes de controle na referencia ---")
for addr, nome in sorted(ext.items()):
    if "Cont" in nome or "Si" in nome[:4] or "PackRequest" in nome:
        print("  0x%08X  %s" % (addr, nome))
