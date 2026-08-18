import zlib, struct, sys, pathlib

# Converte os despejos PPM do rasterizador em PNG, para ilustrar o README.
# Escrito sem dependencia externa (so zlib/struct) porque o ambiente de build
# nao garante Pillow, e uma dependencia so para gerar tres imagens nao se
# justifica.

def ler_ppm(p):
    d = p.read_bytes()
    campos = []
    i = 0
    while len(campos) < 4:
        while i < len(d) and d[i:i+1].isspace():
            i += 1
        if d[i:i+1] == b"#":
            while i < len(d) and d[i] != 0x0A:
                i += 1
            continue
        j = i
        while j < len(d) and not d[j:j+1].isspace():
            j += 1
        campos.append(d[i:j])
        i = j
    i += 1
    larg, alt = int(campos[1]), int(campos[2])
    return larg, alt, d[i:i + larg * alt * 3]

def escrever_png(p, larg, alt, rgb):
    linhas = b"".join(b"\x00" + rgb[y * larg * 3:(y + 1) * larg * 3]
                      for y in range(alt))
    def bloco(tipo, dados):
        c = tipo + dados
        return struct.pack(">I", len(dados)) + c + struct.pack(">I", zlib.crc32(c))
    png = (b"\x89PNG\r\n\x1a\n"
           + bloco(b"IHDR", struct.pack(">IIBBBBB", larg, alt, 8, 2, 0, 0, 0))
           + bloco(b"IDAT", zlib.compress(linhas, 9))
           + bloco(b"IEND", b""))
    p.write_bytes(png)

for origem in sys.argv[1:]:
    o = pathlib.Path(origem)
    if not o.exists():
        print("faltando:", o)
        continue
    larg, alt, rgb = ler_ppm(o)
    destino = pathlib.Path("docs") / (o.stem + ".png")
    destino.parent.mkdir(exist_ok=True)
    escrever_png(destino, larg, alt, rgb)
    print("%s -> %s  (%dx%d)" % (o.name, destino, larg, alt))
