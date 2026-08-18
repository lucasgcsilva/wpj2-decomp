"""Extrai o atlas CI8 e a TLUT de um snapshot de RDRAM do Project64.

Os snapshots do Project64 mantem o arranjo little-endian do emulador; os
acessos de byte e meio-word seguem as mesmas trocas (^3/^2) do runtime.
"""
import struct
import sys
from pathlib import Path

from PIL import Image

ATLAS = 0x002CEF20
TLUT = 0x002CECC0
WIDTH, HEIGHT = 256, 240


def read8(memory: bytes, address: int) -> int:
    return memory[address ^ 3]


def read16(memory: bytes, address: int) -> int:
    return struct.unpack_from("<H", memory, address ^ 2)[0]


def rgb5551(value: int) -> tuple[int, int, int]:
    return ((value >> 11) << 3, ((value >> 6) & 0x1F) << 3,
            ((value >> 1) & 0x1F) << 3)


def main() -> None:
    snapshot = Path(sys.argv[1])
    output = Path(sys.argv[2]) if len(sys.argv) > 2 else snapshot.parent
    output.mkdir(parents=True, exist_ok=True)
    memory = snapshot.read_bytes()
    if len(memory) < ATLAS + WIDTH * HEIGHT:
        raise SystemExit("snapshot menor que a area de atlas esperada")

    palette = [read16(memory, TLUT + i * 2) for i in range(256)]
    pixels = bytearray()
    used: dict[int, int] = {}
    colored = 0
    for i in range(WIDTH * HEIGHT):
        index = read8(memory, ATLAS + i)
        used[index] = used.get(index, 0) + 1
        color = palette[index]
        if color & 0xFFFE:
            colored += 1
        pixels.extend(rgb5551(color))
    name = output / f"atlas_{snapshot.stem}.png"
    Image.frombytes("RGB", (WIDTH, HEIGHT), bytes(pixels)).save(name)
    report = output / f"atlas_{snapshot.stem}.txt"
    with report.open("w", encoding="utf-8") as f:
        f.write(f"snapshot={snapshot}\n")
        f.write(f"atlas=0x{ATLAS:08X} {WIDTH}x{HEIGHT}\n")
        f.write(f"texels_rgb={colored}/{WIDTH * HEIGHT}\n")
        for index, count in sorted(used.items(), key=lambda item: item[1], reverse=True)[:16]:
            f.write(f"index=0x{index:02X} count={count} tlut=0x{palette[index]:04X}\n")
    print(name)
    print(report)


if __name__ == "__main__":
    main()
