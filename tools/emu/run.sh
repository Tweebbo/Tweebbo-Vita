#!/usr/bin/env bash
# Runs out/tweebbo.vpk (or $VPK) in Vita3K inside the headless tweebbo-3ds-emu container
# (Xvfb + Mesa) and saves screenshots in build/shots. The emulator and the official 3.74
# firmware live in tools/emu-cache (see tools/emu/README.md).
#   SERVER=ws://127.0.0.1:8096 SECONDS_RUN=40 tools/emu/run.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$PWD
CACHE=$ROOT/tools/emu-cache
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
  -e SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-}" tweebbo-3ds-emu bash "$ROOT/tools/emu/session.sh"
echo "--- app log"; cat "$UX0/data/$DATA/log.txt" 2>/dev/null || echo "(no log)"
