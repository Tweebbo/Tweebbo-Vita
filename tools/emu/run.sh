#!/usr/bin/env bash
# Runs out/tweebbo.vpk (or $VPK) in Vita3K inside the headless tweebbo-3ds-emu container
# (Xvfb + Mesa) and saves screenshots in build/shots. The emulator and the official 3.74
# firmware live in tools/emu-cache (see tools/emu/README.md).
#   SERVER=ws://127.0.0.1:8096 SECONDS_RUN=40 tools/emu/run.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$PWD
CACHE=$ROOT/tools/emu-cache
exec 9>"$CACHE/session.lock"
flock -n 9 || { echo 'A Vita3K review session is already using this profile.' >&2; exit 1; }
ASYNC_PIPELINES=${ASYNC_PIPELINES:-keep}
SURFACE_SYNC=${SURFACE_SYNC:-keep}
python3 - "$CACHE/home/config/Vita3K/config.yml" "$ASYNC_PIPELINES" "$SURFACE_SYNC" <<'PY'
import re
import sys
from pathlib import Path
path = Path(sys.argv[1])
if any(v not in ('true', 'false', 'keep') for v in sys.argv[2:]):
    raise SystemExit('ASYNC_PIPELINES and SURFACE_SYNC must be true, false or keep')
config = path.read_text()
settings = {'async-pipeline-compilation': sys.argv[2],
            'disable-surface-sync': ('keep' if sys.argv[3] == 'keep' else
                                     'false' if sys.argv[3] == 'true' else 'true')}
for key, value in settings.items():
    if value == 'keep':
        matches = re.findall('^' + key + r': (true|false)$', config, flags=re.M)
        if len(matches) != 1:
            raise SystemExit('Expected one ' + key + ' setting in the local Vita3K profile')
        print('Vita3K ' + key + ': ' + matches[0]); continue
    config, count = re.subn('^' + key + r':.*$', key + ': ' + value, config, flags=re.M)
    if count != 1:
        raise SystemExit('Expected one ' + key + ' setting in the local Vita3K profile')
    print('Vita3K ' + key + ': ' + value)
path.write_text(config)
PY
VPK=${VPK:-out/tweebbo.vpk}
TITLE=${TITLE:-TWEE00001}
DATA=${DATA:-Tweebbo}
mkdir -p build/shots
rm -f build/shots/*.png
UX0=$CACHE/home/vita/ux0
mkdir -p "$UX0/data/$DATA"
rm -f "$UX0/data/$DATA/log.txt"
[ -n "${SERVER:-}" ] && printf '%s\n' "$SERVER" > "$UX0/data/$DATA/server.txt"
[ -n "${ROOM:-}" ] && printf '%s\n' "$ROOM" > "$UX0/data/$DATA/room.txt"
timeout $(( ${SECONDS_RUN:-40} + 120 )) podman run --rm --network host -v "$ROOT":"$ROOT":z -w "$ROOT" \
  -e CACHE="$CACHE" -e VPK="$VPK" -e TITLE="$TITLE" -e SECONDS_RUN="${SECONDS_RUN:-40}" -e SHOTS="${SHOTS:-10 20 30}" \
  -e BACKEND="${BACKEND:-Vulkan}" -e KEYS="${KEYS:-}" -e CLICKS="${CLICKS:-}" \
  -e STEPS="${STEPS:-}" \
  -e SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" tweebbo-3ds-emu bash "$ROOT/tools/emu/session.sh"
echo "--- app log"; cat "$UX0/data/$DATA/log.txt" 2>/dev/null || echo "(no log)"
