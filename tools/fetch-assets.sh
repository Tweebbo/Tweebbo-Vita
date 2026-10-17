#!/usr/bin/env bash
# Copies the shared Tweebbo graphics into romfs/ from the 3DS debug build tree
# (which runs the web export and packs it). Re-run after a new export.
#   tools/fetch-assets.sh [path-to-Tweebbo-3DS]
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=${1:-../Tweebbo-3DS}
GFX="$SRC/build/gfx"

[ -d "$GFX" ] || { echo "No $GFX: build Tweebbo-3DS once (./build.sh) or pass its path."; exit 1; }

mkdir -p romfs/gfx romfs/furni
# Atlas pages, bitmap font and the classic room backgrounds/scenes.
cp "$GFX"/atlas0.png "$GFX"/atlasg0.png "$GFX"/atlasg1.png "$GFX"/atlasg2.png "$GFX"/atlasg3.png romfs/gfx/
cp "$GFX"/font0.png romfs/gfx/
cp "$GFX"/room_*.png romfs/gfx/ 2>/dev/null || true
cp "$GFX"/scene_*.png romfs/gfx/ 2>/dev/null || true
# Furniture textures (one PNG per definition page).
cp "$GFX"/furni/*.png romfs/furni/
echo "romfs ready: $(ls romfs/gfx | wc -l) gfx files, $(ls romfs/furni | wc -l) furniture textures"
