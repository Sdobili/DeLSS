#!/usr/bin/env bash
# End-to-end test of the X11 overlay on Xvfb + Mesa llvmpipe (no GPU needed).
#   e2e.sh <delss-overlay> <x11-testpattern> <delss-imgdiff> <repo root> <work dir>
# Checks, in order:
#   1. golden:      first overlay frame over the test pattern == the WebGL
#                   preset golden for that image (same shaders, color only)
#   2. no feedback: after many frames the capture still equals the pattern
#                   (overlay excluded from its own capture), the screen does not
#   3. click-through: xdotool clicks land on the window under the overlay
#   4. hotkeys:     Ctrl+Alt+P cycles presets, Ctrl+Alt+D hides (screen is the
#                   plain pattern again), Ctrl+Alt+Esc exits cleanly
#   5. settings:    the last preset is persisted to the config file
set -euo pipefail
OVERLAY=$1 PATTERN=$2 IMGDIFF=$3 ROOT=$4 WORK=$5
PHOTO="$ROOT/tests/fixtures/photo.png"
GOLDEN="$ROOT/tests/golden/web"
rm -rf "$WORK" && mkdir -p "$WORK"
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
DISPLAY_NUM=${E2E_DISPLAY:-:97}
export DISPLAY=$DISPLAY_NUM

Xvfb "$DISPLAY_NUM" -screen 0 320x180x24 +extension Composite -nolisten tcp >"$WORK/xvfb.log" 2>&1 &
XVFB=$!
PIDS=("$XVFB")
cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT
for _ in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.1; done

fail() { echo "FAIL: $*"; exit 1; }
pass() { echo "ok   $*"; }

"$PATTERN" "$PHOTO" 0 0 >"$WORK/pattern.log" &
PIDS+=($!)
for _ in $(seq 50); do grep -q ready "$WORK/pattern.log" && break; sleep 0.1; done
grep -q ready "$WORK/pattern.log" || fail "test pattern window did not map"

# 1. Golden: first frame of each preset (temporal history is empty on frame 1).
for preset in cel pixel gameboy crt oldphoto; do
  "$OVERLAY" --enable --preset "$preset" --no-hud --no-save --config "$WORK/none.ini" \
    --dump "$WORK/$preset" --dump-frame 1 --exit-after-frames 2 >"$WORK/$preset.log" 2>&1 ||
    { cat "$WORK/$preset.log"; fail "overlay run ($preset)"; }
  "$IMGDIFF" "$WORK/$preset/capture_0.png" "$PHOTO" >/dev/null || fail "$preset: capture != test pattern"
  "$IMGDIFF" "$WORK/$preset/output_0.png" "$GOLDEN/preset_${preset}_photo.png" ||
    fail "$preset: overlay output != pipeline golden"
  pass "golden $preset"
done

# Negative control: with self-exclusion disabled the capture must pick up the
# overlay's own output (feedback), proving check 2 below can actually fail.
"$OVERLAY" --enable --preset gameboy --no-hud --no-save --config "$WORK/none.ini" --debug-no-exclude \
  --dump "$WORK/control" --dump-frame 10 --exit-after-frames 11 >"$WORK/control.log" 2>&1 || fail "control run"
"$IMGDIFF" "$WORK/control/capture_0.png" "$PHOTO" --different || fail "control: feedback not detectable"
pass "negative control: without exclusion the capture contains the overlay"

# 2-5. One long-running overlay driven by hotkeys.
CONFIG="$WORK/overlay.ini"
"$OVERLAY" --enable --preset cel --hud --config "$CONFIG" --dump "$WORK/long" --dump-frame 40 \
  --exit-after-seconds 60 >"$WORK/long.log" 2>&1 &
OV=$!
PIDS+=("$OV")
for _ in $(seq 100); do [ -f "$WORK/long/output_0.png" ] && break; sleep 0.1; done
[ -f "$WORK/long/output_0.png" ] || { cat "$WORK/long.log"; fail "long run never reached frame 40"; }
sleep 0.5
"$IMGDIFF" "$WORK/long/capture_0.png" "$PHOTO" || fail "feedback: capture at frame 40 is not the plain pattern"
import -window root "$WORK/screen_on.png"
"$IMGDIFF" "$WORK/screen_on.png" "$PHOTO" --different >/dev/null || fail "screen does not show the overlay"
pass "no feedback after 40 frames (capture == pattern, screen == stylized)"

PATTERN_WIN=$(awk '/ready/ {print $2; exit}' "$WORK/pattern.log")
xdotool mousemove 120 70
sleep 0.2
eval "$(xdotool getmouselocation --shell)"
[ "$(printf '0x%x' "$WINDOW")" = "$PATTERN_WIN" ] || fail "pointer is over window $WINDOW, not the pattern $PATTERN_WIN"
xdotool click 1
sleep 0.3
grep -q "click 120 70 1" "$WORK/pattern.log" || fail "click did not reach the window under the overlay"
pass "click-through (pointer window = pattern, click delivered at 120,70)"

xdotool key ctrl+alt+p
sleep 0.5
grep -q "preset=comic" "$WORK/long.log" || fail "Ctrl+Alt+P did not cycle to the next preset"
pass "Ctrl+Alt+P cycles presets"

xdotool key ctrl+alt+d
sleep 0.8
grep -q "enabled=0" "$WORK/long.log" || fail "Ctrl+Alt+D did not disable"
import -window root "$WORK/screen_off.png"
"$IMGDIFF" "$WORK/screen_off.png" "$PHOTO" || fail "overlay still visible after Ctrl+Alt+D"
pass "Ctrl+Alt+D hides the overlay (screen == pattern)"

xdotool key ctrl+alt+d
sleep 0.8
xdotool key ctrl+alt+Escape
for _ in $(seq 50); do kill -0 "$OV" 2>/dev/null || break; sleep 0.1; done
kill -0 "$OV" 2>/dev/null && fail "Ctrl+Alt+Esc did not exit"
wait "$OV" || fail "overlay exited with an error"
grep -q "exited cleanly" "$WORK/long.log" || fail "no clean exit message"
pass "Ctrl+Alt+Esc exits cleanly"

grep -q "preset = comic" "$CONFIG" || fail "settings not persisted"
pass "settings persisted ($CONFIG)"
echo "all overlay e2e checks passed"
