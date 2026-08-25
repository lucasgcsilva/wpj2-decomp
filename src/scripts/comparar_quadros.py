# Junta dois quadros lado a lado num PNG ampliado, para comparacao visual.
#
# Ampliacao por vizinho-mais-proximo de proposito: qualquer suavizacao aqui
# inventaria bordas suaves e mascararia exatamente o que se quer comparar.
import sys
from PIL import Image

esq, dir_, saida = sys.argv[1], sys.argv[2], sys.argv[3]
escala = int(sys.argv[4]) if len(sys.argv) > 4 else 2

a = Image.open(esq).convert("RGB")
b = Image.open(dir_).convert("RGB")
w, h = a.size
sep = 8
c = Image.new("RGB", (w * 2 + sep, h), (200, 0, 0))
c.paste(a, (0, 0))
c.paste(b, (w + sep, 0))
c = c.resize(((w * 2 + sep) * escala, h * escala), Image.NEAREST)
c.save(saida)
print(f"{saida}  esquerda={esq} direita={dir_}  {c.width}x{c.height}")
