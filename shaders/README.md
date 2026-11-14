# Wired highlight fragment programs

`source/vita/highlight_gxp.h` contains two offline GXP programs, for ordinary
straight-alpha sprites and existing premultiplied additive artwork. Both apply
the shared source RGB matrix, retain texture/visibility alpha, and clamp through
GXM's UCHAR4 fragment output. Additive offsets are multiplied by sample alpha,
so transparent pixels contribute zero color. Vertex coordinates use libvita2d's
original texture vertex program and the same 960 × 544 orthographic projection.
No runtime shader compiler or additional firmware module is required.

The authoring input is `build-highlight.cpp`. It uses the MIT-licensed
[open-shacccg](https://github.com/frangarcj/open-shacccg) Machine IR encoder and
GXP writer at commit `26da8ebc44717147d002c60d4a80cd6604aace7f`.
The texture iterator/reflection layout comes from its `TextureTint2D` profile.
The emitted shader performs tint multiplication, three explicit channel
swizzles and column products, offset addition, alpha restoration, and F32→F16
packing. Explicit vector swizzles avoid VMOV's two-lane F32 behavior.

To regenerate, configure that compiler with its documented glslang, SPIRV-Cross,
and SPIRV-Tools development dependencies. Add an executable using this source,
include the compiler's `src` directory, and link `openshacccg_core`. Run it twice:

```
tweebbo_highlight highlight_f.gxp 0
tweebbo_highlight highlight_add_f.gxp 1
python3 embed-highlight.py highlight_f.gxp highlight_add_f.gxp ../source/vita/highlight_gxp.h
```

The local Ubuntu compiler needed `<array>` added to `shader_profiles.cpp` and a
protected `Compiler::ir` getter for its older SPIRV-Cross release; these are build
compatibility changes, independent of the Machine IR and GXP encoders.

`../../validation/wired-highlight-vita.c` is a small standalone Vita homebrew
probe linked to the actual `vita_highlight.c`. Its isolated Vita3K profile uses
existing installed firmware and contains no player profile or token. Pixel
values are sampled from a native-size X11 window capture; emulator CPU surface
readback returns stale black pixels in this setup. The actual GPU capture is
`../../review/images/vita-highlight-gpu-swatches.png`; results are recorded in
`../../tests-highlight-vita-pixels.log`. Identity, darken, both tint colors,
brighten, independent alpha, transparent additive art, and a following ordinary
vita2d draw pass. Physical Vita performance and installation remain unverified.

The renderer allocates shader programs only at startup. Sprite vertices use the
existing vita2d frame pool after its existing reserve check. It creates no
textures, heap allocations, pixel readbacks, or texture frees during a draw.
Shader programs are released after the existing rendering barrier.
