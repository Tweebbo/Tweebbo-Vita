"""Console artwork drawn from the game's own avatars and classic room (shared by the Vita and
Switch repositories). Needs the 3DS export: in ../Tweebbo-3DS run `./build.sh brand` (or at least
`node tools/export/brand.mjs` for build/brand/avatar*.png) after `./build.sh export`."""
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

DS = Path(__file__).resolve().parents[2] / 'Tweebbo-3DS'
INK, GOLD = (0x11, 0x19, 0x20), (0xff, 0xd3, 0x4d)
WALL = (0xe8, 0xe8, 0xe0)


def avatars():
    return [Image.open(DS / f'build/brand/avatar{i}.png').convert('RGBA') for i in range(4)]


def room():
    return Image.open(DS / 'build/export/data/classic/model_a.png').convert('RGBA')


def font(px):
    for name in ('DejaVuSans-Bold.ttf', 'NotoSans-Bold.ttf'):
        try:
            return ImageFont.truetype(name, px)
        except OSError:
            pass
    path = subprocess.run(['fc-match', '-f', '%{file}', 'sans:bold'], capture_output=True, text=True).stdout
    return ImageFont.truetype(path, px)


def title(img, text, px, xy, anchor='ra', outline=2):
    """Gold name with a dark outline, like the 3DS banner."""
    d, f = ImageDraw.Draw(img), font(px)
    for dx in range(-outline, outline + 1):
        for dy in range(-outline, outline + 1):
            d.text((xy[0] + dx, xy[1] + dy), text, font=f, fill=INK, anchor=anchor)
    d.text(xy, text, font=f, fill=GOLD, anchor=anchor)


def up(img, s):
    return img.resize((img.width * s, img.height * s), Image.NEAREST)


def sky(size):
    """Light sky gradient, like the web background."""
    im = Image.new('RGBA', size)
    d = ImageDraw.Draw(im)
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        d.line([(0, y), (size[0], y)], fill=(int(0xcf + 0x25 * t), int(0xe9 + 0x11 * t), 255, 255))
    return im


def bust(scale):
    """Head and shoulders of the first avatar, whole-pixel scaled."""
    a = avatars()[0]
    x0, y0, x1, _ = a.getchannel('A').getbbox()
    cx = (x0 + x1) // 2
    return up(a.crop((cx - 24, y0 - 6, cx + 24, y0 + 42)), scale)


def scene(w, h):
    """The classic room behind the windows with four Tweebbos, at 1x, w×h pixels."""
    # Centred on the back wall with the windows, like the 3DS banner (crop 372..628 × 52..180).
    cx, bottom = 500, 180 + max(0, (h - 128) // 3)
    im = Image.new('RGBA', (w, h), WALL)
    im.alpha_composite(room().crop((cx - w // 2, bottom - h, cx - w // 2 + w, bottom)))
    gap = 52
    left = w // 2 - (gap * 3 + 48) // 2
    for i, a in enumerate(avatars()):
        ax0, _, _, ay1 = a.getchannel('A').getbbox()
        im.alpha_composite(a, (left + gap * i - ax0, h - ay1 - 4))
    return im
