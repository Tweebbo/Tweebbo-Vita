#!/usr/bin/env bash
# Copies the shared Tweebbo graphics into romfs/ from the 3DS debug build tree
# (which runs the web export and packs it). Re-run after a new export.
#   tools/fetch-assets.sh [path-to-Tweebbo-3DS]
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=${1:-../Tweebbo-3DS}
GFX="$SRC/build/gfx"

[ -d "$GFX" ] || { echo "No $GFX: build Tweebbo-3DS once (./build.sh) or pass its path."; exit 1; }
for file in source/assets.h source/gen/assets.c source/gen/font.c; do
  [ -f "$SRC/$file" ] || { echo "Missing generated asset input: $SRC/$file"; exit 1; }
done

mkdir -p romfs/gfx romfs/furni romfs/spaces
cp "$SRC"/romfs/spaces/*.mask "$SRC"/romfs/spaces/*.rgb romfs/spaces/
# Atlas pages, bitmap font and the classic room backgrounds/scenes.
# The export decides how many pages there are (atlas0..N and atlasg0..N).
cp "$GFX"/atlas*.png romfs/gfx/
cp "$GFX"/font0.png romfs/gfx/
cp "$GFX"/pets_*.png romfs/gfx/
cp "$GFX"/room_*.png romfs/gfx/ 2>/dev/null || true
cp "$GFX"/scene_*.png romfs/gfx/ 2>/dev/null || true
# Furniture textures (one PNG per definition page).
cp "$GFX"/furni/*.png romfs/furni/
# Sprite coordinates and texture pages must come from the same export.
mkdir -p source/gen
cp "$SRC/source/assets.h" source/assets.h
cp "$SRC/source/gen/assets.c" "$SRC/source/gen/font.c" "$SRC/source/gen/character_skins.h" source/gen/
echo "romfs ready: $(ls romfs/gfx | wc -l) gfx files, $(ls romfs/furni | wc -l) furniture textures"

mkdir -p romfs/snow
cp "$SRC"/build/snow/*.png "$SRC"/romfs/snow/game.js romfs/snow/
cp "$SRC"/source/gen/snow_assets.[ch] source/gen/
cp "$SRC"/romfs/snow/*.pcm romfs/snow/
