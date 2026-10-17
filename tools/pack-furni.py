#!/usr/bin/env python3
"""Packs romfs/furni/*.png (one file per furniture definition page) into romfs/furni.pak.

Layout: [blob]* [index: u32 key, u32 offset, u32 size]* [u32 index_offset, u32 count]
with key = def | (page << 16). The Vita client reads each texture on demand, so the
VPK only carries one big file instead of ~1.900 tiny ones (VitaShell installs it by
copying instead of creating thousands of files)."""
import glob
import os
import struct
import sys

root = sys.argv[1] if len(sys.argv) > 1 else "romfs/furni"
out = sys.argv[2] if len(sys.argv) > 2 else "romfs/furni.pak"

entries = []
with open(out, "wb") as f:
    for path in sorted(glob.glob(os.path.join(root, "*.png"))):
        base = os.path.basename(path)[:-4]  # d<def>_<page>
        try:
            defn, page = base[1:].rsplit("_", 1)
            key = int(defn) | (int(page) << 16)
        except ValueError:
            continue
        with open(path, "rb") as src:
            data = src.read()
        offset = f.tell()
        f.write(data)
        entries.append((key, offset, len(data)))
    index_offset = f.tell()
    for key, offset, size in sorted(entries):
        f.write(struct.pack("<III", key, offset, size))
    f.write(struct.pack("<II", index_offset, len(entries)))

print(f"furni.pak: {len(entries)} textures, {os.path.getsize(out)} bytes")
