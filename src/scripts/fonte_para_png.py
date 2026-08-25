# Converte a spritesheet PGM da fonte (128x128, 16x16 glifos de 8x8) em um PNG
# ampliado e com grade, para inspecao visual.
#
# A ampliacao usa vizinho-mais-proximo de proposito: a fonte e 1bpp e qualquer
# suavizacao inventaria tons que nao existem no dado.
import sys
from PIL import Image, ImageDraw

entrada, saida = sys.argv[1], sys.argv[2]
escala = 6

img = Image.open(entrada).convert("RGB")
img = img.resize((img.width * escala, img.height * escala), Image.NEAREST)

# Grade a cada glifo, mais forte a cada 4, para dar para contar os codigos.
d = ImageDraw.Draw(img)
passo = 8 * escala
for i in range(0, 17):
    cor = (200, 60, 60) if i % 4 == 0 else (60, 60, 90)
    d.line([(i * passo, 0), (i * passo, img.height)], fill=cor)
    d.line([(0, i * passo), (img.width, i * passo)], fill=cor)

img.save(saida)
print(f"{saida}  {img.width}x{img.height}")
