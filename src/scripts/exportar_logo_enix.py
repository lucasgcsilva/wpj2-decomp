#!/usr/bin/env python3
"""Exporta a composição nativa da ENIX observada no framebuffer 320x240."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("frame", type=Path, help="PPM nativo capturado em 8/1")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    image = Image.open(args.frame).convert("RGB")
    if image.size != (320, 240):
        raise SystemExit(f"frame inesperado: {image.size}")

    visible = [
        (x, y)
        for y in range(image.height)
        for x in range(image.width)
        if max(image.getpixel((x, y))) > 30
    ]
    if not visible:
        raise SystemExit("nenhum pixel da logo encontrado")
    box = (
        min(x for x, _ in visible),
        min(y for _, y in visible),
        max(x for x, _ in visible) + 1,
        max(y for _, y in visible) + 1,
    )
    crop = image.crop(box)
    rgba = Image.new("RGBA", crop.size)
    pixels = []
    native = bytearray()
    for r, g, b in crop.getdata():
        alpha = 0 if max(r, g, b) <= 16 else 255
        pixels.append((r, g, b, alpha))
        r5 = (r * 31 + 127) // 255
        g5 = (g * 31 + 127) // 255
        b5 = (b * 31 + 127) // 255
        value = (r5 << 11) | (g5 << 6) | (b5 << 1) | (1 if alpha else 0)
        native.extend(value.to_bytes(2, "big"))
    rgba.putdata(pixels)

    args.output.mkdir(parents=True, exist_ok=True)
    image.save(args.output / "enix_frame_native_320x240.png")
    rgba.save(args.output / "enix_logo_native.png")
    rgba.resize((crop.width * 4, crop.height * 4), Image.Resampling.NEAREST).save(
        args.output / "enix_logo_native_4x_nearest.png"
    )
    (args.output / "enix_logo_native.rgba5551").write_bytes(native)
    metadata = {
        "source": str(args.frame.resolve()),
        "state": "8/1",
        "gfx_reference": 200,
        "frame_size": [320, 240],
        "crop": list(box),
        "sprite_size": list(crop.size),
        "native_format": "RGBA5551 big-endian, composed output",
        "note": "A logo é composta por geometria/texturas; não é um único PNG armazenado linearmente na ROM.",
    }
    (args.output / "metadata.json").write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"ENIX: {crop.size[0]}x{crop.size[1]} pixels, crop={box}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
