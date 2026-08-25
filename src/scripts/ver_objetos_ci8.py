# Renderiza objetos_ci8.bin como grade de glifos de 8x12, 1bpp.
#
# O formato veio da instrumentacao de func_80094230: o indice de objeto e
# multiplicado por 12 e usado como deslocamento na tabela apontada por
# 0x8015F880. Doze bytes com oito pixels de largura sao doze linhas de 1 bit -
# nao cabe 8bpp em doze bytes, entao apesar do nome o dado e monocromatico.
#
# Uso: ver_objetos_ci8.py <arquivo.bin> <saida.png> [obj_inicial] [quantos]
import sys
from PIL import Image, ImageDraw

entrada = sys.argv[1]
saida = sys.argv[2]
obj0 = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0
n = int(sys.argv[4], 0) if len(sys.argv) > 4 else 256

dados = open(entrada, "rb").read()
COLS, LARG, ALT, ESC = 16, 8, 12, 4
linhas = (n + COLS - 1) // COLS

img = Image.new("RGB", (COLS * LARG * ESC, linhas * ALT * ESC), (0, 0, 0))
px = img.load()
for k in range(n):
    obj = obj0 + k
    base = obj * 12
    if base + 12 > len(dados):
        break
    gx, gy = (k % COLS) * LARG * ESC, (k // COLS) * ALT * ESC
    for y in range(ALT):
        b = dados[base + y]
        for x in range(LARG):
            if b & (0x80 >> x):
                for dy in range(ESC):
                    for dx in range(ESC):
                        px[gx + x * ESC + dx, gy + y * ESC + dy] = (255, 255, 255)

d = ImageDraw.Draw(img)
for i in range(COLS + 1):
    d.line([(i * LARG * ESC, 0), (i * LARG * ESC, img.height)], fill=(70, 70, 110))
for j in range(linhas + 1):
    d.line([(0, j * ALT * ESC), (img.width, j * ALT * ESC)], fill=(70, 70, 110))
img.save(saida)
print(f"{saida}  obj {obj0:#x}..{obj0+n-1:#x}  {img.width}x{img.height}")
