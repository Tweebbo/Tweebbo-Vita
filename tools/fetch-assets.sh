#!/usr/bin/env bash
# Packs the shared Tweebbo graphics for this console from the 3DS export tree (build/export,
# made by `./build.sh export` there) with the HD profile of tools/pack.py: the whole classic
# interface at its original size, not the 3DS's memory-saving subset. Re-run after a new export.
#   tools/fetch-assets.sh [path-to-Tweebbo-3DS]
set -euo pipefail
cd "$(dirname "$0")/.."
SRC=${1:-../Tweebbo-3DS}
[ -d "$SRC/build/export/data" ] || { echo "No $SRC/build/export: run ./build.sh export in Tweebbo-3DS first."; exit 1; }
rm -rf build/pack-hd
PACK_OUT="$PWD/build/pack-hd" PACK_HD=1 PACK_STATE=1 python3 "$SRC/tools/pack.py" "$SRC/build/export"
GFX="$PWD/build/pack-hd/gfx"
for file in source/gen/font.c; do
  [ -f "$SRC/$file" ] || { echo "Missing generated asset input: $SRC/$file"; exit 1; }
done

mkdir -p romfs/gfx romfs/furni romfs/spaces
cp build/pack-hd/spaces/*.mask build/pack-hd/spaces/*.rgb romfs/spaces/
rm -f romfs/gfx/atlas*.png romfs/furni/*.png
# Atlas pages, bitmap font and the classic room backgrounds/scenes.
# The export decides how many pages there are (atlas0..N and atlasg0..N).
cp "$GFX"/atlas*.png romfs/gfx/
cp "$SRC"/build/gfx/font0.png romfs/gfx/
cp "$GFX"/pets_*.png romfs/gfx/
cp "$GFX"/room_*.png romfs/gfx/ 2>/dev/null || true
cp "$GFX"/scene_*.png romfs/gfx/ 2>/dev/null || true
# Furniture textures (one PNG per definition page).
cp "$GFX"/furni/*.png romfs/furni/
# Sprite coordinates and texture pages must come from the same export. The local
# assets.h stays because the console RoomItem also carries the offline item state.
mkdir -p source/gen
cp build/pack-hd/gen/assets.c build/pack-hd/gen/character_skins.h build/pack-hd/gen/igloo.h "$SRC/source/gen/font.c" source/gen/
echo "romfs ready: $(ls romfs/gfx | wc -l) gfx files, $(ls romfs/furni | wc -l) furniture textures"

mkdir -p romfs/snow
cp "$SRC"/build/snow/*.png "$SRC"/romfs/snow/game.js romfs/snow/
cp "$SRC"/source/gen/snow_assets.[ch] source/gen/
cp "$SRC"/romfs/snow/*.pcm romfs/snow/
