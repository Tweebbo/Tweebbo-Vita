#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
export SERVER=${SERVER:-ws://127.0.0.1:18997}
export ROOM=${ROOM:-casino}
# Separate objects keep the automated input hook out of the installable build.
podman run --rm -v "$PWD":/work:z -w /work -e TWEEBBO_SERVER="$SERVER" docker.io/vitasdk/vitasdk \
  bash -lc 'make -j4 BUILD=build/ui-check TARGET=tweebbo-ui-check EXTRA_CFLAGS=-DTWEEBBO_UI_TEST'
VPK=out/tweebbo-ui-check.vpk BACKEND=Vulkan SECONDS_RUN=35 SHOTS='5 8 12 20' tools/emu/run.sh
rg -q 'OK: native keyboard message echoed by hotel' tools/emu-cache/home/vita/ux0/data/Tweebbo/log.txt
rg -q 'OK: native pet preview input completed' tools/emu-cache/home/vita/ux0/data/Tweebbo/log.txt
