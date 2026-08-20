import pathlib, struct

# Descobre a leitura correta do bloco de 80 bytes do Project64 por INVARIANTE
# ESTRUTURAL, nao por faixa de valores.
#
# A tentativa anterior usava criterio absoluto ("exp_rate entre 60000 e 70000")
# tirado de UMA voz do nosso dump. Isso rejeitou as quatro leituras, mas o
# criterio e que estava errado: exp_rate varia com o estado da voz - no nosso
# proprio dump aparece 65505 numa voz e -1 (0xFFFFFFFF) em outra.
#
# Invariante que vale para qualquer voz, observada no nosso dump:
#     exp_seq (+24) e value (+32) sao quase iguais
#     exp_seq_r (+28) e value_r (+36) idem
# No nosso caso: 400317499 vs 400317496, diferenca de 3 em 400 milhoes.
# So a leitura correta reproduz essa proximidade; as erradas embaralham os
# bytes e destroem a relacao.
D = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\analise\oraculo\audio\replay")

def campos(b, fmt, troca):
    d = bytes(b)
    if troca:
        d = b"".join(d[i:i+4][2:4] + d[i:i+4][0:2] for i in range(0, len(d), 4))
    return struct.unpack(fmt, d[:40])

def proximos(a, b):
    """Mesma ordem de grandeza e diferenca pequena em termos relativos."""
    if a == 0 and b == 0:
        return False                      # par zerado nao informa nada
    m = max(abs(a), abs(b))
    return m > 1000 and abs(a - b) < m // 1000

LEITURAS = [(">10i", False, "big-endian"),
            ("<10i", False, "little-endian"),
            (">10i", True,  "big-endian + swap"),
            ("<10i", True,  "little-endian + swap")]

arquivos = [p for p in sorted(D.glob("estado_*.bin")) if any(p.read_bytes()[:40])]
print("estados com conteudo:", len(arquivos))

placar = {n: 0 for _, _, n in LEITURAS}
for p in arquivos:
    b = p.read_bytes()
    for fmt, troca, nome in LEITURAS:
        w = campos(b, fmt, troca)
        if proximos(w[6], w[8]) or proximos(w[7], w[9]):
            placar[nome] += 1

print("\n%-24s %s  (de %d)" % ("leitura", "exp_seq ~ value", len(arquivos)))
for _, _, nome in LEITURAS:
    print("%-24s %10d" % (nome, placar[nome]))

melhor = max(LEITURAS, key=lambda x: placar[x[2]])
print("\n=== %s ===" % melhor[2])
for p in arquivos[:5]:
    w = campos(p.read_bytes(), melhor[0], melhor[1])
    print("%-18s wet=%-8d dry=%-8d target=%d/%d" % (p.name, w[0], w[1], w[2], w[3]))
    print("%-18s taxa=%-12d exp_seq=%-12d value=%-12d dif=%d"
          % ("", w[4], w[6], w[8], w[6] - w[8]))
