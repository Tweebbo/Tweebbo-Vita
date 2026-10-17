# Tweebbo for PlayStation Vita (showcase proof of concept)

Native PlayStation Vita client of [Tweebbo](https://tweebbo.lol) (C11 + VitaSDK + vita2d +
mbedTLS). It plays **in the same hotel as the web, the 3DS and the Switch**: the same rooms,
characters, credits and chat. This is a **showcase proof of concept**: the room, the avatars, the
classic interface and the cross-play badges, with the five brand models rotating over your
character. It is the base to grow the full port (see
`../Tweebbo-designs/PORTS-SHOWCASE-2026-10.md`).

## What it does

- **Local hotel only**: this proof of concept never talks to `tweebbo.lol`. It plays on the hotel
  of the Wi-Fi it was built for: `./build.sh` bakes in `ws://<this PC's LAN IP>:8096` (or
  `TWEEBBO_SERVER`), and only private LAN `ws://` addresses are accepted (a `server.txt` with
  anything else is ignored). Protocol v6 with the same reconnection as the 3DS client; `HELLO`
  carries `platform = 1` plus the trailing device byte `3` (Vita).
- **Room**: isometric rendering at a 2× integer canvas (480×272 logical on the 960×544 screen),
  room background or layered public room, furniture with its own textures (LRU cache), avatars
  composed with the shared compositor, chat bubbles and the classic bottom bar.
- **Showcase**: the five brand models (Bobba, Cyber, Director, Street, VIP Girl) rotate every
  ~5 s with `SET_LOOK`, the character waves/dances and walks a little, and a banner names the
  model. Every console and the web see the same character change.
- **Cross-play**: the server announces each native device (`s2c DEVICE`); the pixel-art badge of
  the 3DS, the Switch and the Vita floats over the players.
- **Offline in the same build**: when the hotel does not answer, the connection screen offers
  **Select - play offline** (also a tap on it): the local simulation with bots in the lobby, where the
  models keep rotating. Select again goes back to the hotel. Every launch tries the hotel first.

## Controls

| Control | Action |
| --- | --- |
| Left stick / D-pad | Walk |
| Cross | Contextual: sit on a nearby seat or use furniture; otherwise wave |
| Square | Wave |
| Triangle | Quick chat ("Hello from PlayStation Vita!") |
| Circle | Next showcase model |
| L / R | Zoom 1× (whole room, default) / 2× (close-up) |
| Select | Play offline / back to the hotel |
| Start | Quit |
| Front touch | Walk to the tapped tile (also works on PS TV with a controller only) |

## Building

The VitaSDK is not needed on the host: everything builds inside the official `vitasdk/vitasdk`
container (gcc 15, vita2d, mbedTLS, libpng and the packaging tools come from the image).

```bash
tools/fetch-assets.sh      # copies the shared graphics from a Tweebbo-3DS build tree
./build.sh                 # podman/docker: out/tweebbo.vpk
./build.sh clean
```

Install: copy `out/tweebbo.vpk` to the console (`ux0:` via VitaShell USB/FTP), open it in
VitaShell and install. Settings and the character token live in `ux0:data/Tweebbo/`
(`server.txt`, `room.txt`, `token-<server>.txt` — **private** — and `log.txt`).

The hotel is the showcase hotel of this PC (`~/Tweebbo-Showcase-2026-10-17/INSTALL.md`, port
8096). If the PC's address changes, rebuild, or point a console at the new one without rebuilding
(private LAN `ws://` addresses only):

```bash
printf 'ws://192.168.1.20:8096' > /path/to/ux0/data/Tweebbo/server.txt
```

## Layout

```
source/            shared C11 core (also compiled by the Switch client)
  proto.c world.c avatar.c ws.c net.c sim.c path.c catalog.c text.c show.c
  gen/             generated tables from tools/pack.py (assets.c, font.c)
  vita/            PlayStation Vita platform layer
    conn.c         SceNet sockets + mbedTLS (entropy from the process clock)
    gfx.c          vita2d renderer (2× canvas, nearest, furniture cache, frame sync)
    main.c         SceCtrl and SceTouch input, ux0:data storage
sce_sys/           LiveArea (icon0, bg, startup, template.xml)
romfs/             PNG assets fetched from the 3DS export (gfx/ and furni/)
```

## Notes

- **LiveArea images must be 8-bit indexed PNGs** (the promoter rejects 24-bit ones with install
  error `0x8010113D`): `tools/make-sce_sys.py` regenerates `sce_sys/` quantised; run it after
  changing `assets/icon.jpg` and before `./build.sh`.
- **Fast-install VPK**: the package is built with `ZIP_STORED` (VitaShell copies instead of
  inflating) and the 1909 furniture textures travel inside a single `furni.pak` read on demand
  (`tools/pack-furni.py`), so the VPK carries 29 files instead of 1937. That is what made the
  classic VitaShell install crawl. The size barely changes because the PNGs were already
  compressed.
- The assets are the same PNGs the web export produces, so the art is identical to the other
  clients.
- **GPU lifetime (the `GPUCRASH` fix)**: `gfx_present` waits for the GPU (`sceGxmFinish`) after
  every frame. Textures are freed between frames (room changes, furniture cache) and vita2d
  rewinds its vertex pool at the start of each frame; doing either while the GPU still read the
  previous frame is what powered the console off with `GPUCRASH` when the lobby was drawn (the
  64-slot furniture cache evicted textures of the frame in flight every frame). The cache now has
  512 slots in main memory, the vertex pool is 4 MB and every draw is skipped when it runs low.
- The mirror for left/right facing sprites uses a negative x scale in vita2d (GXM does not cull).
- **Emulator**: `tools/emu/run.sh` runs the VPK in Vita3K inside the headless `tweebbo-3ds-emu`
  container and saves screenshots in `build/shots` (`SERVER=ws://127.0.0.1:8096` points the app at
  a hotel). The emulator AppImage and the official 3.74 firmware live in `tools/emu-cache/`
  (ignored by git): download `Vita3K-x86_64.AppImage` (continuous release) and `PSVUPDAT.PUP`,
  run `./Vita3K.AppImage --appimage-extract`, then install the firmware once with
  `XDG_DATA_HOME=$PWD/home/share XDG_CONFIG_HOME=$PWD/home/config ./Vita3K.AppImage --firmware PSVUPDAT.PUP`
  after setting `pref-path` in `home/config/Vita3K/config.yml` to `tools/emu-cache/home/vita/`.
  Vita3K does not reproduce GPU faults, so the frame-sync rule above must stay.
- Not in this proof of concept (roadmap): build mode, catalogue, inventory, friends, pets, games,
  casino, Snow Storm, audio, LiveArea updates and in-game updates.
