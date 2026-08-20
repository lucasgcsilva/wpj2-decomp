import wave, struct, math, pathlib

# Mede o piso de ruido do WAV entregue ao driver. Chiado aparece como energia
# constante nos trechos que deveriam ser silencio, e como diferenca entre os
# canais quando o conteudo e mono.
p = pathlib.Path(r"E:\projetos\project-wonder-j2-decomp\temp\projeto\audio_sonda\saida.wav")
# O cabecalho declara 0 quadros: audio_shutdown() nao roda sob
# TerminateProcess e nunca volta para corrigir o tamanho. Os dados estao
# integros, entao lemos pelo tamanho do arquivo e ignoramos o campo.
canais, larg, taxa = 2, 2, 22047
bruto = p.read_bytes()
dados = bruto[44:]
dados = dados[: len(dados) // 4 * 4]
quadros = len(dados) // 4
print("canais=%d bits=%d taxa=%d quadros=%d  dur=%.2fs"
      % (canais, larg * 8, taxa, quadros, quadros / taxa))

amostras = struct.unpack("<%dh" % (len(dados) // 2), dados)
L = amostras[0::2]
R = amostras[1::2]

def rms(v):
    if not v:
        return 0.0
    return math.sqrt(sum(float(x) * x for x in v) / len(v))

def db(v):
    return -999.0 if v <= 0 else 20 * math.log10(v / 32768.0)

print("\njanela(s)   RMS_L      RMS_R    dBFS_L  dBFS_R  |L-R|rms  pico")
passo = taxa  # 1 s
for ini in range(0, len(L) - passo, passo):
    l, r = L[ini:ini + passo], R[ini:ini + passo]
    dif = [a - b for a, b in zip(l, r)]
    pico = max(max(abs(x) for x in l), max(abs(x) for x in r))
    print("%6.1f  %9.1f %9.1f  %7.1f %7.1f  %8.1f  %6d"
          % (ini / taxa, rms(l), rms(r), db(rms(l)), db(rms(r)), rms(dif), pico))
