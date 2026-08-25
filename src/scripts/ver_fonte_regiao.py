# Renderiza um bloco cru de RDRAM como imagem 1bpp de largura fixa.
#
# A fonte do dialogo tem passo de 2 bytes entre linhas de glifo, ou seja e uma
# imagem de 16 pixels de largura. Vendo a regiao inteira assim, a ordem dos
# glifos se le de olho - que e o que falta para escolher o indice de destino
# das acentuadas, ja que a tabela nao e indexada por ASCII.
#
# Uso: ver_fonte_regiao.py <bin> <saida.png> [bytes_por_linha] [escala]
import sys
from PIL import Image, ImageDraw

entrada, saida = sys.argv[1], sys.argv[2]
bpl = int(sys.argv[3], 0) if len(sys.argv) > 3 else 2
esc = int(sys.argv[4], 0) if len(sys.argv) > 4 else 3

dados = open(entrada, "rb").read()
larg = bpl * 8
alt = len(dados) // bpl
img = Image.new("RGB", (larg * esc, alt * esc), (0, 0, 0))
px = img.load()
for y in range(alt):
    for bx in range(bpl):
        b = dados[y * bpl + bx]
        for k in range(8):
            if b & (0x80 >> k):
                x = bx * 8 + k
                for dy in range(esc):
                    for dx in range(esc):
                        px[x * esc + dx, y * esc + dy] = (255, 255, 255)

# Linha a cada 8 pixels verticais: e a altura de um glifo, e ajuda a contar.
d = ImageDraw.Draw(img)
for y in range(0, alt, 8):
    d.line([(0, y * esc), (img.width, y * esc)], fill=(90, 40, 40))
img.save(saida)
print(f"{saida}  {larg}x{alt} px  ({len(dados)} bytes, {bpl} por linha)")
