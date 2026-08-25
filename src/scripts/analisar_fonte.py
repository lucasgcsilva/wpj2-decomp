# Analisa a spritesheet da fonte (128x128, 16x16 glifos de 8x8) para responder
# se da para compor letras acentuadas a partir dos acentos que ja existem.
#
# Tres perguntas, nesta ordem:
#   1. Que codigo esta em cada posicao que o usuario apontou? (contagem
#      1-indexada erra por um com facilidade; melhor conferir do que supor)
#   2. Quantas linhas livres cada minuscula tem no topo? E la que o acento
#      precisa caber, dentro da mesma celula de 8x8.
#   3. Quais codigos estao vazios e portanto disponiveis para as compostas?
import sys

def carregar(caminho):
    with open(caminho, "rb") as f:
        dados = f.read()
    # Cabecalho PNM: magia, largura, altura, [maxval]. Comentarios com '#'.
    campos, i = [], 0
    magia = None
    while len(campos) < (3 if dados[:2] == b"P4" else 4):
        while i < len(dados) and dados[i:i+1].isspace():
            i += 1
        if dados[i:i+1] == b"#":
            while i < len(dados) and dados[i] != 0x0A:
                i += 1
            continue
        j = i
        while j < len(dados) and not dados[j:j+1].isspace():
            j += 1
        campos.append(dados[i:j])
        i = j
        if magia is None:
            magia = campos[0]
    i += 1  # o unico byte de espaco depois do cabecalho
    larg, alt = int(campos[1]), int(campos[2])
    corpo = dados[i:]

    pix = [[0] * larg for _ in range(alt)]
    if magia == b"P4":                       # 1 bit por pixel, 1 = preto
        passo = (larg + 7) // 8
        for y in range(alt):
            for x in range(larg):
                b = corpo[y * passo + (x >> 3)]
                pix[y][x] = 1 if (b >> (7 - (x & 7))) & 1 else 0
    else:                                    # P5: 1 byte por pixel
        for y in range(alt):
            for x in range(larg):
                pix[y][x] = 1 if corpo[y * larg + x] >= 128 else 0
    return pix, magia

def glifo(pix, codigo):
    gx, gy = (codigo % 16) * 8, (codigo // 16) * 8
    return [[pix[gy + y][gx + x] for x in range(8)] for y in range(8)]

def arte(g):
    return ["".join("#" if v else "." for v in linha) for linha in g]

def vazio(g):
    return not any(any(l) for l in g)

def linhas_livres_topo(g):
    n = 0
    for linha in g:
        if any(linha):
            break
        n += 1
    return n

caminho = sys.argv[1]
pix, magia = carregar(caminho)
print(f"{caminho}  formato={magia.decode()}")

# Nota: no PBM (P4) 1 = preto. Se a imagem do usuario veio com fundo branco e
# tinta preta, a leitura acima ja da tinta=1. No PGM exportado por nos, tinta
# e branco (255) e o teste >=128 tambem da tinta=1. Os dois batem.

print("\n--- posicoes apontadas (coluna/linha 1-indexadas) ---")
for nome, col, lin in [("agudo", 8, 3), ("circunflexo", 14, 6), ("til", 14, 8),
                       ("masculino º", 7, 16), ("feminino ª", 8, 16)]:
    codigo = (lin - 1) * 16 + (col - 1)
    g = glifo(pix, codigo)
    marca = "VAZIO" if vazio(g) else ""
    print(f"\n{nome}: coluna {col} linha {lin} -> codigo 0x{codigo:02X} {marca}")
    for l in arte(g):
        print("   " + l)

print("\n--- espaco no topo das minusculas (onde o acento precisa caber) ---")
for c in "aeiouc":
    g = glifo(pix, ord(c))
    print(f"  '{c}' 0x{ord(c):02X}: {linhas_livres_topo(g)} linha(s) livre(s) no topo")

for extra in sys.argv[2:]:
    codigo = int(extra, 16)
    g = glifo(pix, codigo)
    print(f"\ncodigo 0x{codigo:02X}{'  VAZIO' if vazio(g) else ''}")
    for l in arte(g):
        print("   " + l)

livres = [c for c in range(256) if vazio(glifo(pix, c))]
print(f"\n--- {len(livres)} codigos vazios ---")
print("  " + " ".join(f"{c:02X}" for c in livres))
