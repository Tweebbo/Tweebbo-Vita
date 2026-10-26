# Vita3K LAN review

Use only a LAN-restricted VPK and the existing local emulator/firmware cache. Do not run the public client against test accounts.

```sh
BACKEND=OpenGL SERVER=ws://192.168.1.165:18877 ROOM=snowstorm \
  SECONDS_RUN=45 SHOTS='15 30 40' tools/emu/run.sh
```

The runner locks its isolated profile to prevent overlapping Xvfb sessions. It requires the process to remain alive through the last step; an emulator exit or an unmet `awaitlog` condition fails the run. Captures are in `build/shots`; archive them before another run. Do not share SD character keys or unfiltered emulator file-access logs.

`ASYNC_PIPELINES=true|false` and `SURFACE_SYNC=true|false` are optional overrides; by default the runner preserves the profile. Surface synchronization is represented by the inverse `disable-surface-sync` setting. Changing these settings alone did not reliably resolve the first-frame CJK crash and is not counted as a fix. The current font loader waits only before retiring an existing CJK texture, preserving the per-frame and replacement fences. Final startup repetitions use the original profile values (`ASYNC_PIPELINES=true SURFACE_SYNC=false`); the candidate review records their exact VPK hash. These emulator results do not certify a physical Vita.

`steps-languages` attempts mouse input on the normal VPK, but injected Vita3K input has not been reliable in this environment. Shared UI input is tested through the SDL host at 480×272. `steps-games-backend` requires a separate LAN-only `TWEEBBO_GAMES_TEST` build and checks the actual Vita rendering/cleanup paths; keep its instrumentation out of `out/tweebbo.vpk`. See the candidate's `docs/games-expansion/PORT-REVIEW.md` for the exact tested artifacts and remaining acceptance gates.
