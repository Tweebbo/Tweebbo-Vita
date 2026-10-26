#!/usr/bin/env bash
# Inside the container: install the VPK once, then boot it and take timed screenshots.
set -uo pipefail
export XDG_DATA_HOME=$CACHE/home/share XDG_CONFIG_HOME=$CACHE/home/config HOME=$CACHE/home
Xvfb :97 -screen 0 1280x800x24 >/dev/null 2>&1 &
XVFB=$!
export DISPLAY=:97
sleep 1
V3K=$CACHE/squashfs-root/AppRun
LOG=$PWD/build/shots/vita3k.log
"$V3K" -B "${BACKEND:-Vulkan}" --frontend sdl "$VPK" >"$LOG" 2>&1 &
PID=$!
printf 'running\n' > build/shots/emulator-exit.txt
cleanup() { kill "$PID" "$XVFB" 2>/dev/null || true; }
trap cleanup EXIT
check_alive() {
  if ! kill -0 "$PID" 2>/dev/null; then
    wait "$PID"; status=$?
    printf 'unexpected-exit status=%s\n' "$status" > build/shots/emulator-exit.txt
    exit 1
  fi
}
start=$(date +%s)
if [ -n "${STEPS:-}" ]; then
  while read -r command a b; do
    check_alive
    case "$command" in
      wait) sleep "$a" ;;
      shot) import -window root "build/shots/$a.png" ;;
      awaitlog)
        deadline=$(( $(date +%s) + ${b:-40} ))
        until grep -q "$a" "$CACHE/home/vita/ux0/data/Tweebbo/log.txt" 2>/dev/null; do
          check_alive
          if [ "$(date +%s)" -ge "$deadline" ]; then
            printf 'step-timeout: %s\n' "$a" > build/shots/emulator-exit.txt
            echo "Timed out waiting for $a"; exit 1
          fi
          sleep 1
        done ;;
      key) window=$(xdotool search --onlyvisible --name Vita3K | tail -1)
        if [ -z "$window" ] || ! timeout 5 xdotool windowfocus --sync "$window"; then
          printf 'input-window-unavailable\n' > build/shots/emulator-exit.txt; exit 1
        fi
        xdotool keydown "$a"; sleep "${b:-0.15}"; xdotool keyup "$a" ;;
      click) xdotool mousemove "$a" "$b" mousedown 1; sleep 0.2; xdotool mouseup 1 ;;
      "#"*|"") ;;
    esac
  done < "$STEPS"
else
for t in $SHOTS; do
  while [ $(( $(date +%s) - start )) -lt "$t" ]; do check_alive; sleep 1; done
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
fi
xdotool search --name . getwindowname %@ > build/shots/windows.txt 2>/dev/null
while [ $(( $(date +%s) - start )) -lt "$SECONDS_RUN" ]; do check_alive; sleep 1; done
import -window root build/shots/final.png 2>/dev/null
check_alive
printf 'alive-after-steps; planned-stop=SIGTERM\n' > build/shots/emulator-exit.txt
kill $PID 2>/dev/null; sleep 1; kill -9 $PID 2>/dev/null
grep -E "\|E\||\|C\||error|Error" "$LOG" | grep -v -i "shader" | tail -20
exit 0
