#!/usr/bin/env bash
# Inside the container: install the VPK once, then boot it and take timed screenshots.
set -uo pipefail
export XDG_DATA_HOME=$CACHE/home/share XDG_CONFIG_HOME=$CACHE/home/config HOME=$CACHE/home
Xvfb :97 -screen 0 1280x800x24 >/dev/null 2>&1 &
export DISPLAY=:97
sleep 1
V3K=$CACHE/squashfs-root/AppRun
LOG=$PWD/build/shots/vita3k.log
"$V3K" -B "${BACKEND:-Vulkan}" --frontend sdl "$VPK" >"$LOG" 2>&1 &
PID=$!
start=$(date +%s)
for t in $SHOTS; do
  while [ $(( $(date +%s) - start )) -lt "$t" ]; do sleep 1; done
  import -window root "build/shots/t$t.png" 2>/dev/null
done
# Optional key presses (xdotool names) after the shots, e.g. KEYS="Up Up Return".
for k in $KEYS; do
  xdotool search --name Vita3K windowactivate --sync 2>/dev/null || xdotool search --name . windowfocus 2>/dev/null
  xdotool keydown "$k"; sleep 0.3; xdotool keyup "$k"; sleep 1
done
# Optional taps on the emulated front touch screen (window pixels), e.g. CLICKS="640,462".
for c in ${CLICKS:-}; do
  xdotool mousemove "${c%,*}" "${c#*,}" mousedown 1; sleep 0.3; xdotool mouseup 1; sleep 1
done
xdotool search --name . getwindowname %@ > build/shots/windows.txt 2>/dev/null
while [ $(( $(date +%s) - start )) -lt "$SECONDS_RUN" ]; do sleep 1; done
import -window root build/shots/final.png 2>/dev/null
kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
grep -E "\|E\||\|C\||error|Error" "$LOG" | grep -v -i "shader" | tail -20
