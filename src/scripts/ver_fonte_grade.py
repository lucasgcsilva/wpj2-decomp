# Dispoe a regiao da fonte em grade de blocos de 16x8, para ler a ordem.
#
# A fonte tem passo de 2 bytes entre linhas, entao um bloco de 8 linhas ocupa
# 16 bytes e mede 16 pixels de largura. Mostrando os blocos lado a lado da para
# ler a sequencia de glifos - que e o que falta, ja que a tabela nao e indexada
# por ASCII.
#
# Uso: ver_fonte_grade.py <bin> <saida.png> [colunas] [escala]
import sys
from PIL import Image, ImageDraw

entrada, saida = sys.argv[1], sys.argv[2]
cols = int(sys.argv[3], 0) if len(sys.argv) > 3 else 16
esc = int(sys.argv[4], 0) if len(sys.argv) > 4 else 3

dados = open(entrada, "rb").read()
CELULA, LARG, ALT = 16, 16, 8
n = len(dados) // CELULA
linhas = (n + cols - 1) // cols

img = Image.new("RGB", (cols * LARG * esc, linhas * ALT * esc), (0, 0, 0))
px = img.load()
for g in range(n):
    ox, oy = (g % cols) * LARG * esc, (g // cols) * ALT * esc
    for y in range(ALT):
        for bx in range(2):
            b = dados[g * CELULA + y * 2 + bx]
            for k in range(8):
                if b & (0x80 >> k):
                    x = bx * 8 + k
                    for dy in range(esc):
                        for dx in range(esc):
                            px[ox + x * esc + dx, oy + y * esc + dy] = (255, 255, 255)

d = ImageDraw.Draw(img)
for i in range(cols + 1):
    d.line([(i * LARG * esc, 0), (i * LARG * esc, img.height)], fill=(80, 40, 40))
for j in range(linhas + 1):
    d.line([(0, j * ALT * esc), (img.width, j * ALT * esc)], fill=(80, 40, 40))
img.save(saida)
print(f"{saida}  {n} blocos, {cols} por linha  {img.width}x{img.height}")
