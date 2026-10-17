# Tweebbo for PlayStation Vita (showcase proof of concept)

Native PlayStation Vita client of [Tweebbo](https://tweebbo.lol) (C11 + VitaSDK + vita2d +
mbedTLS). It plays **in the same hotel as the web, the 3DS and the Switch**: the same rooms,
characters, credits and chat. This is a **showcase proof of concept**: the room, the avatars, the
classic interface and the cross-play badges, with the five brand models rotating over your
character. It is the base to grow the full port (see
`../Tweebbo-designs/PORTS-SHOWCASE-2026-10.md`).

## What it does

- **Online**: WebSocket over TLS (mbedTLS) to `wss://tweebbo.lol` (or another server), protocol v6,
  with the same reconnection and renewal as the 3DS client. `HELLO` carries `platform = 1` plus
  the trailing device byte `3` (Vita).
- **Room**: isometric rendering at a 2× integer canvas (480×272 logical on the 960×544 screen),
  room background or layered public room, furniture with its own textures (LRU cache), avatars
  composed with the shared compositor, chat bubbles and the classic bottom bar.
- **Showcase**: the five brand models (Bobba, Cyber, Director, Street, VIP Girl) rotate every
  ~5 s with `SET_LOOK`, the character waves/dances and walks a little, and a banner names the
  model. Every console and the web see the same character change.
- **Cross-play**: the server announces each native device (`s2c DEVICE`); the pixel-art badge of
  the 3DS, the Switch and the Vita floats over the players.
- **Offline**: Select switches to the local simulation (bots in the lobby) for photos without
  Wi-Fi; Select again returns to the server.

## Controls

| Control | Action |
| --- | --- |
| Left stick / D-pad | Walk |
| Cross | Contextual: sit on a nearby seat or use furniture; otherwise wave |
| Square | Wave |
| Triangle | Quick chat ("Hello from PlayStation Vita!") |
| Circle | Next showcase model |
| L / R | Zoom 1× / 2× |
| Select | Offline simulation on/off |
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

To play against a local hotel (the same one the 3DS debug uses):

```bash
printf 'ws://192.168.1.20:8092' > /path/to/ux0/data/Tweebbo/server.txt
```

## Layout

```
source/            shared C11 core (also compiled by the Switch client)
  proto.c world.c avatar.c ws.c net.c sim.c path.c catalog.c text.c show.c
  gen/             generated tables from tools/pack.py (assets.c, font.c)
  vita/            PlayStation Vita platform layer
    conn.c         SceNet sockets + mbedTLS (entropy from the process clock)
    gfx.c          vita2d renderer (2× canvas, nearest, LRU furniture cache)
    main.c         SceCtrl and SceTouch input, ux0:data storage
sce_sys/           LiveArea (icon0, bg, startup, template.xml)
romfs/             PNG assets fetched from the 3DS export (gfx/ and furni/)
```

## Notes

- **Fast-install VPK**: the package is built with `ZIP_STORED` (VitaShell copies instead of
  inflating) and the 1909 furniture textures travel inside a single `furni.pak` read on demand
  (`tools/pack-furni.py`), so the VPK carries 29 files instead of 1937. That is what made the
  classic VitaShell install crawl. The size barely changes because the PNGs were already
  compressed.
- The assets are the same PNGs the web export produces, so the art is identical to the other
  clients.
- The mirror for left/right facing sprites uses a negative x scale in vita2d; it is verified on
  Vita3K during development but still needs the physical-console photo pass (OLED/LCD/PS TV).
- Not in this proof of concept (roadmap): build mode, catalogue, inventory, friends, pets, games,
  casino, Snow Storm, audio, LiveArea updates and in-game updates.
