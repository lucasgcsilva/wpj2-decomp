# Imprime os registros de 12 bytes da tabela de objetos, em hexadecimal.
#
# A renderizacao como bitmap 8x12 saiu ruido, o que sugere que os doze bytes
# nao sao pixels e sim um descritor - largura, altura, ponteiro. Ver os bytes
# crus de alguns objetos vizinhos revela a estrutura: campos que variam pouco
# entre glifos consecutivos sao dimensao, e campos que sobem de forma regular
# sao deslocamento de dados.
import sys

arq, obj0, n = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
d = open(arq, "rb").read()
for k in range(n):
    obj = obj0 + k
    b = d[obj * 12: obj * 12 + 12]
    if len(b) < 12:
        break
    be32 = lambda o: int.from_bytes(b[o:o+4], "big")
    be16 = lambda o: int.from_bytes(b[o:o+2], "big")
    print(f"obj 0x{obj:04X}  " + " ".join(f"{x:02X}" for x in b) +
          f"   u16: {be16(0):5d} {be16(2):5d} {be16(4):5d} {be16(6):5d}"
          f" {be16(8):5d} {be16(10):5d}   u32@4: 0x{be32(4):08X}")
