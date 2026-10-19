#!/usr/bin/env python3
"""Generates the sce_sys / LiveArea images for the VPK.

The Vita promoter rejects a VPK whose LiveArea images are not 8-bit indexed PNGs
(install error 0x8010113D), so every image is quantised to a 256-colour palette
here. Run before ./build.sh (the build only packs what is already in sce_sys/)."""
from PIL import Image

import sys
sys.path.insert(0, "tools")
from brandkit import bust, scene, sky, title, up

def indexed(im: Image.Image, path: str) -> None:
    im = im.convert("RGB").quantize(colors=256, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    im.save(path, optimize=True)
    check = Image.open(path)
    assert check.mode == "P" and check.info.get("bits", 8) <= 8, f"{path} is not an 8-bit indexed PNG"
    print(f"{path}: {check.size[0]}x{check.size[1]} indexed PNG")


def icon0():
    # The bubble shows the icon as a circle: keep the whole bust inside it.
    im = sky((128, 128))
    b = bust(2)
    im.alpha_composite(b, ((128 - b.width) // 2, 20))
    return im


def background():
    im = up(scene(280, 167), 3).crop((0, 0, 840, 500))
    title(im, "Tweebbo", 64, (816, 22), outline=4)
    return im


def startup():
    im = scene(280, 158)
    title(im, "Tweebbo", 24, (272, 6))
    return im


indexed(icon0(), "sce_sys/icon0.png")
indexed(background(), "sce_sys/livearea/contents/bg.png")
indexed(startup(), "sce_sys/livearea/contents/startup.png")
print("sce_sys images ready (8-bit indexed)")
