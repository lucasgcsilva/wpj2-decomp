# Audita o catalogo inteiro contra o orcamento de bytes da ROM.
#
# Por que existe. A troca de texto e in-place: a traducao nao pode passar do
# espaco da cadeia inglesa mais o enchimento de zeros que a segue. Quando
# passa, o runtime recusa EM SILENCIO - a entrada esta no catalogo e a tela
# mostra ingles. Descobrir isso frase a frase custa um ciclo por frase; esta
# auditoria devolve a lista inteira de uma vez, com o limite exato de cada uma.
#
# O criterio reproduz o do runtime (runtime/legendas.c):
#   - so conta ocorrencia em que o byte seguinte NAO e ASCII imprimivel,
#     senao estariamos casando um prefixo de algo maior;
#   - folga = zeros consecutivos apos a cadeia;
#   - capacidade = tamanho + folga - 1 quando ha folga, senao o tamanho;
#   - a traducao e dobrada para ASCII antes de medir, como make_ascii faz.
#
# Uso: auditar_orcamento.py <rom> <traducao.tsv> [saida.tsv]
import io, sys, unicodedata

def dobrar(texto):
    """Aproxima make_ascii: acentuada vira um byte, como no runtime."""
    saida = []
    for ch in texto:
        if ord(ch) < 0x80:
            saida.append(ch)
        else:
            # Qualquer nao-ASCII ocupa exatamente um byte no destino.
            saida.append("?")
    return "".join(saida)

def imprimivel(b):
    return 0x20 <= b < 0x7F

rom = open(sys.argv[1], "rb").read()
tsv = sys.argv[2]
saida = sys.argv[3] if len(sys.argv) > 3 else None

# Indice das ocorrencias validas por cadeia, com a menor capacidade.
def capacidade(origem):
    b = origem.encode("ascii", errors="replace")
    menor = None
    i = 0
    while True:
        p = rom.find(b, i)
        if p < 0:
            break
        i = p + 1
        seg = rom[p + len(b)] if p + len(b) < len(rom) else 0
        if imprimivel(seg):
            continue
        folga = 0
        while (p + len(b) + folga < len(rom)
               and rom[p + len(b) + folga] == 0 and folga < 64):
            folga += 1
        cap = len(b) + folga - 1 if folga else len(b)
        if menor is None or cap < menor:
            menor = cap
    return menor

linhas = io.open(tsv, encoding="utf-8").read().split("\n")
ok = curtas = longas = ausentes = 0
falhas = []
for l in linhas:
    if "\t" not in l:
        continue
    origem, destino = l.split("\t", 1)
    if origem == "source_en":
        continue
    cap = capacidade(origem)
    if cap is None:
        ausentes += 1
        continue
    n = len(dobrar(destino).encode("ascii", errors="replace"))
    if n > cap:
        longas += 1
        falhas.append((n - cap, cap, n, origem, destino))
    else:
        ok += 1

falhas.sort(key=lambda t: (t[0], t[1]))
print(f"cabem            : {ok}")
print(f"NAO cabem        : {longas}")
print(f"fora da ROM      : {ausentes}  (nao localizadas; podem ser dinamicas)")
print()
print("as 25 que faltam por menos - melhor custo/beneficio para encurtar:")
for excesso, cap, n, origem, destino in falhas[:25]:
    print(f"  +{excesso:<3} limite {cap:<3} tem {n:<3}  {origem!r} -> {destino!r}")

if saida:
    with io.open(saida, "w", encoding="utf-8", newline="\n") as f:
        f.write("excesso\tlimite\tatual\tsource_en\tpt_br\n")
        for excesso, cap, n, origem, destino in falhas:
            f.write(f"{excesso}\t{cap}\t{n}\t{origem}\t{destino}\n")
    print(f"\nlista completa em {saida}")
