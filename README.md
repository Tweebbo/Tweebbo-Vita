# Tweebbo for PlayStation Vita

Version 1.4.1 adds the Football catalogue, fixes glowing furniture and labels the owner's furniture light controls. It retains Game Center, the classic game lobbies and six languages. See [release notes](RELEASE.md) for installation and validation limits.

Release 1.3.5, 2026-10-23: room layouts P, Q, R, S, X and Z use regenerated Kepler-style doorways (room images and maps repacked from the 3DS export). Builds verified; physical hardware still pending.

Published follow-up, 2026-10-23: version 1.3.4 adds doorbell admission, profile badge selection and the Tweebbo Club catalogue shortcut, and fixes furniture animation and bear/terrier/pig rendering. The update is optional; existing clients remain admitted. Release builds and uploaded artifact hashes were verified. Physical-device checks remain pending. Older local-only notes below are historical.

Notice follow-up, 2026-10-23: the thick black stripe above purchase notices is removed. The shared content-frame renderer now uses one row of the original top-border sprite and fills its remaining interior with the panel background.

Local follow-up, 2026-10-23: Lia’s profile opens Suggestions, with validation and retained drafts. Social gestures preserve seated posture. Shared window corners and Vita rounded fills are corrected with GPU fences and vertex-pool checks retained. Vita3K captures cover Lia, the form and the white notice outline; isolated desktop tests also verify submission. Physical Vita validation remains pending. The server recognizes Vita separately. Review: `../Tweebbo-Vercel/docs/reviews/console-polish-20261023/`.

Local candidate, 2026-10-22: F3 and all four regional lobbies are packed. The navigator uses English PC featured priority. Keep a Camera in inventory, select it and choose Take photo for capture, zoom, caption and save. Saved photos can be placed on walls; opening their stored image still requires the web client. The VPK builds and the shared camera flow passes at Vita resolution; framebuffer capture still requires a physical Vita check. This is not yet a published release. Team evidence is in `../Tweebbo-Vercel/docs/reviews/console-polish-20261023/`.

Play Tweebbo on your PlayStation Vita, in the same hotel as [tweebbo.lol](https://tweebbo.lol).

Also on [Nintendo 3DS](https://github.com/Tweebbo/tweebbo-3ds) and [Nintendo Switch](https://github.com/Tweebbo/TweebboNX).

![Download QR](download-qr.png)

## Download

**[Download Tweebbo for PlayStation Vita](https://github.com/Tweebbo/Tweebbo-Vita/releases/latest)**

Download `tweebbo.vpk` from the latest release.

## How to install

1. You need a PlayStation Vita with homebrew enabled (HENkaku or Enso).
2. Download `tweebbo.vpk`.
3. Copy it to your Vita using USB or FTP.
4. Open VitaShell, select `tweebbo.vpk` and install it.
5. Launch **Tweebbo** from the LiveArea and connect to the internet. Offline play is also available.

## Controls

The room shows a small button legend in the top-left corner.

- **Cross**: talk or use furniture
- **Triangle**: chat · **Square**: wave · **Circle**: rooms or close
- **L/R**: room zoom · **START**: quit

In the Navigator and the large furniture windows:

- **D-pad**: move · **Cross**: choose or place · **Circle**: close or cancel
- **L/R**: turn pages, or rotate and change height while placing
- **Right stick up/down**: in the room, open Tools; change catalogue section; in the Navigator, also turn pages

Unpublished local update: new direction presses and release/repress respond immediately,
while held input still repeats at a bounded rate. In Chat, L shows older messages and
R shows newer messages; incoming messages preserve the reading position. The matching
local server supports the Igloo and its animated visitors. See
[validation and limitations](../Tweebbo-Vercel/docs/performance/console-input/README.md).

**Support** is in the bottom toolbar and in Tools. The $5 donation gives Tweebbo Club
for life, the VIP badge and 10,000 credits the first time. In a changing booth,
**D-pad** moves between the arrows and buttons, **L/R** changes the swimsuit colour,
**Cross** confirms the focused control and **Circle** exits in normal clothes.

## Updates

When you enter the hotel, Tweebbo checks GitHub for a newer release and offers **Update now**. The
download resumes if the connection drops and is verified against the release SHA-256. The VPK is
saved to `ux0:data/Tweebbo/tweebbo.vpk`; open VitaShell and install it over the old version.
**Help → Search for updates** checks again at any time.

## Notes

- Free to play. No PlayStation Network account is needed.
- Tweebbo is not affiliated with Sony.

### Local room names and Classic missions (2026-10-20)

Owners can rename rooms without changing their links or contents; the five-room creation limit is removed on the server. Room titles update live through optional server packet 144. My info in Friends edits the personal mission (up to 50 Unicode characters, including an empty mission). These changes are local only and require the updated local server.

Enter your room and open Hotel → Room settings → Rename room. Friends → My info edits the mission; the selected contact displays their mission.
