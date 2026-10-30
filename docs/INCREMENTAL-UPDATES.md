# Incremental updates on Switch and Vita

Available starting with 1.4.4. This is an optional bridge update: versions through
1.4.3 still need a complete download to install the new reader. Later releases
can offer smaller downloads. There is no required immediate update or minimum
client-version change.

## Player flow

The update window shows the selected transfer size and, for a small update, the
full package size. Later keeps the current app running. Downloads resume after
interruption. Preparing the update copies unchanged files from the installed
application and combines them with downloaded changes.

If a patch or installed resource is incompatible or damaged, Full download is
offered explicitly. The updater never silently starts the larger transfer.
Storage errors retain the current app and explain the failure. Normal exit is
blocked during update work; OS termination/power-loss acceptance still needs
physical-device testing.

Switch prepares a complete verified NRO and replaces the previous file with a
recoverable backup. Close and reopen it from the Homebrew Menu after installing.
Vita prepares a complete verified VPK at `ux0:data/Tweebbo/tweebbo.vpk`; install it
with VitaShell as before. It does not rewrite the running `app0:` installation.
Accounts, room data and settings remain outside the updated package.

Network savings do not remove the need for space for the full reconstructed
package, temporary files and any retained backup. Download and preparation have
separate progress. This is not a hardware download-speed or FPS claim.

## Format and trust

The shared portable decoder uses bounded TUPD1 zlib recipes, matching the 3DS
recipe structure with platform kinds 3 (NRO) and 4 (VPK). It binds the source and
target versions, platform kind, 16-byte application identity, exact target size
and SHA-256. It verifies the patch and every reused file, rejects unsafe relative
paths, oversized output, truncated data and trailing compressed/inflated bytes,
and removes its failed reconstruction temporary.

Limits are 512 MiB output, 65,536 operations, 240-byte relative paths and 32 KiB
stream buffers. The Switch reads ordinary `romfs:/` files; Vita reads `app0:/`.
No previous downloaded package needs to be retained.

The existing GitHub latest-release API remains authoritative for the full asset,
its size and digest. A new reader optionally fetches `latest.json` from that same
versioned release, verifies its GitHub asset SHA-256, and requires its full offer
to match the API. A small offer must also match an uploaded release asset's URL,
size and digest. Missing, malformed, unknown or incompatible offers retain the
full download. Public clients accept versioned artifacts from their own GitHub
repository; only the desktop harness accepts a loopback fixture.

## Preparing a release

Build the normal public NRO/VPK with a new version first. Retain immutable older
published packages. The local preparation tool does not publish anything:

```sh
python3 tools/prepare-incremental.py --platform switch \
  --target out/tweebbo.nro --version 1.4.5 --output build/release-1.4.5 \
  --base 1.4.4=/path/to/published/tweebbo.nro --notes-file RELEASE.md
```

For Vita, select `--platform vita` and use the corresponding VPK paths. Up to
four unique `--base VERSION=PATH` arguments can be supplied. The tool checks NRO
NACP or Vita SFO identity/version and resource bounds. Vita retains the existing
ZIP_STORED format, allowing the reader to reuse ordinary installed entry bytes.
No generated asset tables need manual editing.

Upload the complete package, `latest.json` and each advertised `.tupd` together
in a draft release, verify their GitHub digests, then publish the stable release.
Older readers ignore the added assets and continue using the complete NRO/VPK.
Offers are included only when their patch is smaller. A skipped version can use
a patch only if that exact base was included; otherwise it gets the full offer.

## Verification

From the Switch checkout inside the existing desktop harness container (the
worker suite exercises both device branches using the POSIX host transport):

```sh
python3 tests/incremental.py
python3 tests/incremental-generator.py
python3 tests/incremental-worker.py
```

The first suite checks the production decoder against 31 positive/adversarial
cases. Structured NRO/VPK fixtures test identity, versions, empty shared-offset
files, paths and compression. Forty production-updater cases run both platform
branches against real isolated HTTP fixtures, covering resume, explicit full
fallback, wrong metadata, short writes, cached retries and atomic replacement.

`tools/host/run.sh` supports isolated `HOST_PROFILE` names. `UI_EXERCISE=update`
exercises Later, manual recheck, small preparation and the final instructions in
the actual shared UI. Keep the normal LAN-only harness and synthetic hotel.
Both portable source trees must remain byte-identical outside their platform
subdirectories. Native SDK compilation and host/renderer evidence do not replace
physical Switch/Vita installation, SD/memory-card and suspend/power-loss testing.
