"""Renderiza uma tabela CI8 exportada pelo probe como atlas PPM.

Uso: python tools/render_ci_table.py entrada.bin paleta.bin saida.ppm
"""
import sys
from pathlib import Path
from PIL import Image


def main() -> None:
    raw = Path(sys.argv[1]).read_bytes()
    tlut = Path(sys.argv[2]).read_bytes()
    out = Path(sys.argv[3])
    width = 256
    height = len(raw) // width
    palette = []
    for i in range(256):
        value = (tlut[i * 2] << 8) | tlut[i * 2 + 1]
        palette.append(((value >> 11) << 3, ((value >> 6) & 0x1F) << 3,
                        ((value >> 1) & 0x1F) << 3))
    pixels = bytearray()
    for index in raw:
        pixels.extend(palette[index])
    Image.frombytes("RGB", (width, height), bytes(pixels)).save(out)


if __name__ == "__main__":
    main()
