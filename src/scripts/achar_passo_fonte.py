# Localiza dois glifos conhecidos na regiao da fonte e devolve o passo.
#
# Saber o endereco de UMA letra nao basta: para escolher o indice de destino
# das acentuadas e preciso o passo entre glifos e a ordem. Com duas letras cuja
# distancia no alfabeto se conhece, o passo sai por divisao.
#
# Os padroes vem da transcricao do framebuffer (recortar_glifo.py), com o passo
# de 2 bytes ja medido entre linhas.
import sys

PASSO_LINHA = 2

# 'M' de "Menu", 8 linhas a partir de y=50
M = [0x82, 0xC6, 0xC6, 0xAA, 0xAA, 0xAA, 0x92, 0x92]
# 'e' de "Menu", mesma faixa de linhas (as duas primeiras vazias)
E = [0x00, 0x00, 0xE0, 0x88, 0xF8, 0x80, 0x88, 0x70]

def achar(d, padrao):
    fora = []
    for p in range(len(d) - 8 * PASSO_LINHA):
        ok = True
        for i, v in enumerate(padrao):
            if d[p + i * PASSO_LINHA] != v:
                ok = False
                break
        if ok:
            fora.append(p)
    return fora

d = open(sys.argv[1], "rb").read()
base = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0

# O 'e' nao apareceu com as duas linhas vazias do topo. Isso sugere que o
# glifo e guardado sem elas - a altura util fica noutro lugar, ou o desenho e
# alinhado pela base. Procurar so o nucleo responde sem depender disso.
E_NUCLEO = [0xE0, 0x88, 0xF8, 0x80, 0x88, 0x70]

pm = achar(d, M)
pe = achar(d, E)
if not pe:
    pe = achar(d, E_NUCLEO)
    print("(usando o nucleo de 6 linhas do 'e')")
print(f"'M': {[hex(base + x) for x in pm]}")
print(f"'e': {[hex(base + x) for x in pe]}")
for a in pm:
    for b in pe:
        dif = b - a
        # 'e'(0x65) - 'M'(0x4D) = 24 posicoes se a ordem for ASCII
        print(f"  delta e-M = {dif:+d} bytes; por 24 glifos = {dif/24:.2f} B/glifo")
