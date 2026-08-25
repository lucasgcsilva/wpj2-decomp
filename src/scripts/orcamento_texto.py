# Orcamento de bytes para uma traducao, medido na ROM.
#
# A troca de texto e in-place: a traducao nao pode passar do espaco da cadeia
# inglesa mais o enchimento de zeros que a segue. Chutar o limite gera entradas
# que o runtime recusa em silencio - foi o que aconteceu com "Copy Diary", que
# esta no catalogo e mesmo assim aparece em ingles.
#
# Uso: orcamento_texto.py <rom> "texto" ["texto" ...]
import sys

rom = open(sys.argv[1], "rb").read()

for alvo in sys.argv[2:]:
    b = alvo.encode("ascii")
    achou = False
    inicio = 0
    while True:
        p = rom.find(b, inicio)
        if p < 0:
            break
        inicio = p + 1
        # Mostrar tambem as nao terminadas por NUL: se a cadeia existe mas o
        # byte seguinte nao e zero, o terminador e outro (ou ha continuacao) -
        # e isso muda o orcamento. Exigir NUL de saida escondia isso.
        seguinte = rom[p + len(b)] if p + len(b) < len(rom) else -1
        if seguinte != 0:
            print(f"{alvo!r:34} rom 0x{p:06X}  seguinte=0x{seguinte:02X}"
                  f" (nao e NUL)")
            achou = True
            continue
        folga = 0
        while (p + len(b) + folga < len(rom)
               and rom[p + len(b) + folga] == 0
               and folga < 64):
            folga += 1
        # O NUL final e obrigatorio, entao o limite util e len + folga - 1.
        print(f"{alvo!r:34} rom 0x{p:06X}  original {len(b):3d}  "
              f"folga {folga:3d}  limite {len(b) + folga - 1:3d}")
        achou = True
    if not achou:
        print(f"{alvo!r:34} nao encontrado terminado por NUL")
