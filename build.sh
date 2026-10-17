#!/usr/bin/env bash
# Builds Tweebbo for PlayStation Vita inside the vitasdk/vitasdk container.
#   ./build.sh            builds out/tweebbo.vpk
#   ./build.sh clean
# The VitaSDK (gcc, vita2d, mbedTLS, packaging tools) comes from the image;
# nothing is installed on the host.
set -euo pipefail
cd "$(dirname "$0")"
IMAGE=docker.io/vitasdk/vitasdk:latest

if [ "${1:-}" = clean ]; then
  podman run --rm -v "$PWD":/work:z -w /work "$IMAGE" bash -lc 'make clean'
  exit 0
fi

[ -f romfs/gfx/atlas0.png ] || { echo "Assets are missing: run tools/fetch-assets.sh first"; exit 1; }
[ -f sce_sys/icon0.png ] || { echo "sce_sys assets are missing (icon0.png, livearea)."; exit 1; }

podman run --rm -v "$PWD":/work:z -w /work "$IMAGE" bash -lc 'make -j"$(nproc)"'
ls -la out/
