# Le um patch IPS e resume o que ele altera.
#
# Pergunta que motiva: vale mais encurtar as traducoes ate o limite, ou mexer
# no limite? O patch ingles do Ryu ja enfrentou exatamente esse problema ao
# traduzir do japones, entao o que ele fez e a melhor evidencia disponivel.
#
# O IPS diz, por construcao: quais faixas foram reescritas, se houve expansao
# alem do fim original, e onde estao os maiores blocos novos. Regiao grande e
# contigua sugere realocacao de texto; muitas faixas pequenas sugerem troca
# in-place, que e o que estamos fazendo.
import sys, collections

dados = open(sys.argv[1], "rb").read()
assert dados[:5] == b"PATCH", "nao parece um IPS"

i = 5
regs = []            # (offset, tamanho, rle)
truncar = None
while i + 3 <= len(dados):
    off = dados[i] << 16 | dados[i+1] << 8 | dados[i+2]
    i += 3
    if off == 0x454F46:                      # 'EOF'
        if i + 3 <= len(dados):              # extensao opcional de truncamento
            truncar = dados[i] << 16 | dados[i+1] << 8 | dados[i+2]
        break
    tam = dados[i] << 8 | dados[i+1]
    i += 2
    if tam == 0:                             # bloco RLE
        n = dados[i] << 8 | dados[i+1]
        i += 3                               # tamanho + byte repetido
        regs.append((off, n, True))
    else:
        i += tam
        regs.append((off, tam, False))

total = sum(r[1] for r in regs)
fim = max(r[0] + r[1] for r in regs)
print(f"registros            : {len(regs)}")
print(f"bytes alterados      : {total} ({total/1024:.1f} KiB)")
print(f"maior offset tocado  : 0x{fim:06X}")
if truncar:
    print(f"truncamento indicado : 0x{truncar:06X}")

# Faixas contiguas, para ver se ha blocos grandes novos.
regs.sort()
faixas = []
ini, ult = regs[0][0], regs[0][0] + regs[0][1]
for off, tam, _ in regs[1:]:
    if off <= ult + 0x40:                    # tolera pequenos vaos
        ult = max(ult, off + tam)
    else:
        faixas.append((ini, ult))
        ini, ult = off, off + tam
faixas.append((ini, ult))

faixas.sort(key=lambda f: f[1] - f[0], reverse=True)
print(f"\nfaixas contiguas: {len(faixas)}; as 12 maiores:")
for a, b in faixas[:12]:
    print(f"  0x{a:06X}..0x{b:06X}   {b-a:7d} bytes")
