#!/usr/bin/env bash
# Inside the container: install the VPK once, then boot it and take timed screenshots.
set -uo pipefail
export XDG_DATA_HOME=$CACHE/home/share XDG_CONFIG_HOME=$CACHE/home/config HOME=$CACHE/home
Xvfb :97 -screen 0 1280x800x24 >/dev/null 2>&1 &
export DISPLAY=:97
sleep 1
V3K=$CACHE/squashfs-root/AppRun
LOG=$PWD/build/shots/vita3k.log
"$V3K" -B OpenGL --frontend sdl "$VPK" >"$LOG" 2>&1 &
PID=$!
start=$(date +%s)
for t in $SHOTS; do
  while [ $(( $(date +%s) - start )) -lt "$t" ]; do sleep 1; done
  import -window root "build/shots/t$t.png" 2>/dev/null
done
# Optional key presses (xdotool names) after the shots, e.g. KEYS="Up Up Return".
for k in $KEYS; do xdotool key "$k"; sleep 1; done
while [ $(( $(date +%s) - start )) -lt "$SECONDS_RUN" ]; do sleep 1; done
import -window root build/shots/final.png 2>/dev/null
kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
grep -E "\|E\||\|C\||error|Error" "$LOG" | grep -v -i "shader" | tail -20
