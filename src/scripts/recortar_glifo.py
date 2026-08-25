# Imprime uma regiao do framebuffer como arte ASCII, para transcrever o
# desenho exato de uma letra que esta na tela.
#
# Serve a uma busca livre de atribuicao: em vez de descobrir QUEM desenha o
# texto - caminho em que trace_last_func() ja se mostrou nao confiavel com
# fibers - pega-se o bitmap que apareceu e procura-se esse padrao na RDRAM. A
# fonte precisa conter o desenho literalmente.
#
# Uso: recortar_glifo.py <frame.ppm> <x0> <y0> <larg> <alt> [limiar]
import sys
from PIL import Image

arq, x0, y0, w, h = sys.argv[1], *[int(v) for v in sys.argv[2:6]]
limiar = int(sys.argv[6]) if len(sys.argv) > 6 else 128
# O painel do menu tem texto escuro sobre claro; a caixa de dialogo tem o
# contrario. Sem inverter, a transcricao da caixa sai como negativo e o padrao
# procurado na memoria nunca casa.
inverter = len(sys.argv) > 7 and sys.argv[7] != "0"

img = Image.open(arq).convert("L")
px = img.load()
print(f"{arq}  regiao ({x0},{y0}) {w}x{h}  limiar={limiar}")
print("    " + "".join(str((x0 + i) % 10) for i in range(w)))
for y in range(y0, y0 + h):
    linha = ""
    for x in range(x0, x0 + w):
        v = px[x, y] if 0 <= x < img.width and 0 <= y < img.height else 0
        # Texto escuro sobre painel claro: tinta e o que fica ABAIXO do limiar.
        tinta = (v > limiar) if inverter else (v < limiar)
        linha += "#" if tinta else "."
    print(f"{y:3d} {linha}")
