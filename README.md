# Tweebbo for PlayStation Vita

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
