#!/usr/bin/env python3
"""Generates the sce_sys / LiveArea images for the VPK.

The Vita promoter rejects a VPK whose LiveArea images are not 8-bit indexed PNGs
(install error 0x8010113D), so every image is quantised to a 256-colour palette
here. Run before ./build.sh (the build only packs what is already in sce_sys/)."""
from PIL import Image

ICON = "assets/icon.jpg"
VOID = (11, 13, 14)
SLATE = (82, 109, 118)


def indexed(im: Image.Image, path: str) -> None:
    im = im.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    im.save(path, optimize=True)
    check = Image.open(path)
    assert check.mode == "P" and check.info.get("bits", 8) <= 8, f"{path} is not an 8-bit indexed PNG"
    print(f"{path}: {check.size[0]}x{check.size[1]} indexed PNG")


def composite(size, box, bg):
    im = Image.new("RGB", size, bg)
    a = Image.open(ICON).convert("RGB")
    a.thumbnail(box, Image.NEAREST)
    im.paste(a, ((size[0] - a.width) // 2, (size[1] - a.height) // 2))
    return im


indexed(composite((128, 128), (104, 104), VOID), "sce_sys/icon0.png")
indexed(composite((840, 500), (300, 300), SLATE), "sce_sys/livearea/contents/bg.png")
indexed(composite((280, 158), (120, 120), SLATE), "sce_sys/livearea/contents/startup.png")
print("sce_sys images ready (8-bit indexed)")
