#!/usr/bin/env bash
# Reuse the Switch HD pack, which has the same asset profile as Vita.
set -euo pipefail
cd "$(dirname "$0")/.."
src=${1:-../switch}
test -f "$src/build/pack-hd/gen/assets.c"
cmp "$src/build/pack-hd/gen/assets.c" "$src/source/gen/assets.c"
mkdir -p build/pack-hd source/gen
cp -a --reflink=auto "$src/build/pack-hd/." build/pack-hd/
cp -a --reflink=auto "$src/romfs/fxpeople.pak" romfs/fxpeople.pak
for folder in gfx furni spaces lang snow fastfood battle; do
  test -d "$src/romfs/$folder"
  mkdir -p "romfs/$folder"
  cp -a --reflink=auto "$src/romfs/$folder/." "romfs/$folder/"
done
for file in "$src"/source/gen/*.c "$src"/source/gen/*.h; do
  test -f "$file" && cp -a "$file" source/gen/
done
python3 - "$src" <<'PY'
import hashlib,json,sys
from pathlib import Path
src=Path(sys.argv[1]).resolve()
target=Path.cwd()
files=[p for p in (src/'source/gen').iterdir() if p.suffix in ('.c','.h')]
assert all(p.read_bytes()==(target/'source/gen'/p.name).read_bytes() for p in files)
receipt={'source':str(src),'sharedGeneratedFiles':len(files),'assetsSha256':hashlib.sha256((target/'source/gen/assets.c').read_bytes()).hexdigest(),
         'catalogueSha256':hashlib.sha256((target.parent/'3ds/build/export/data/models/catalogue.json').read_bytes()).hexdigest()}
(target.parent/'shared-hd-pack.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt))
PY
