#!/usr/bin/env python3
"""Builds the Tweebbo VPK with ZIP_STORED (no compression).

VitaShell installs a VPK by inflating and writing every entry: deflate costs CPU
for PNGs that are already compressed and thousands of small files make the install
drag. Storing everything (and shipping the furniture in one furni.pak) turns the
install into a sequential copy of ~35 files."""
import glob
import os
import sys
import zipfile

out, sfo, eboot = sys.argv[1], sys.argv[2], sys.argv[3]

with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as zf:
    zf.write(eboot, "eboot.bin")
    zf.write(sfo, "sce_sys/param.sfo")
    for path in sorted(glob.glob("sce_sys/icon0.png")):
        zf.write(path, "sce_sys/icon0.png")
    for path in sorted(glob.glob("sce_sys/livearea/contents/*")):
        zf.write(path, "sce_sys/livearea/contents/" + os.path.basename(path))
    for path in sorted(glob.glob("romfs/gfx/*.png")):
        zf.write(path, "gfx/" + os.path.basename(path))
    zf.write("romfs/furni.pak", "furni.pak")

print(f"VPK stored: {out} ({os.path.getsize(out)} bytes)")
