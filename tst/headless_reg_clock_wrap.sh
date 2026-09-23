#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=clock-ms=4294952296
# The 32-bit millisecond clock starts 15 seconds short of its wrap round
# zero. A bell that last rang just before the wrap still fades out after
# it, instead of reading a ring stamp above the wrapped clock as a flash
# that has only just started, and relative-pointer timestamps keep rising
# across the wrap.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "set appearance.visual_bell_seconds 3"
ctl "set appearance.visual_bell_strength 1"
await 100 in_log "control: set appearance.visual_bell_strength" || { echo "the bell settings were not taken"; exit 1; }

clock() { dump_field '^clock ' ms; }
before_wrap() { (( $(clock) >= 2147483648 )); }
near_wrap() { (( $(clock) >= 4294967296 - 2500 )); }
wrapped() { (( $(clock) < 2147483648 )); }
before_wrap || { echo "the clock did not start just short of its wrap ($(clock))"; exit 1; }

start_client
wait_client "ringing"
wait_rect 'app_id=clock-wrap'
wait_placed 'app_id=clock-wrap' || { echo "the window never settled"; exit 1; }
shot="$XDG_RUNTIME_DIR/bell.ppm"
await_mean "$shot" 'app_id=clock-wrap' '$r -gt 120 && $g -gt 120 && $b -gt 120' >/dev/null \
    || { echo "the ringing bell never lit the screen"; exit 1; }

# the pointer onto the window: relative motion goes to it
x=$(( $(dump_field 'app_id=clock-wrap' imgx) + 150 )); y=$(( $(dump_field 'app_id=clock-wrap' imgy) + 100 ))
rel_count() { grep -c "^rel " "$CLIENT_LOG" || true; }
rel_after() { (( $(rel_count) > $1 )); }
for _ in $(seq 20); do
    ctl "motion $x $y"
    screenshot "$XDG_RUNTIME_DIR/_p.ppm"
    n=$(rel_count)
    ctl "relmotion 1 0"
    await 10 rel_after "$n" && break
done
[[ "$(rel_count)" -gt 0 ]] || { echo "no relative motion reached the window"; exit 1; }
before=$(grep "^rel " "$CLIENT_LOG" | tail -n 1 | awk '{print $2}')

# the last ring just short of the wrap
await 200 near_wrap || { echo "the clock never came near its wrap ($(clock))"; exit 1; }
touch "$XDG_RUNTIME_DIR/go-stop"
wait_client "stopped"
before_wrap || { echo "the last ring came after the wrap: the scenario started too late ($(clock))"; exit 1; }

await 100 wrapped || { echo "the clock did not wrap ($(clock))"; exit 1; }
n=$(rel_count)
ctl "relmotion 1 0"
await 50 rel_after "$n" || { echo "no relative motion after the wrap"; exit 1; }
after=$(grep "^rel " "$CLIENT_LOG" | tail -n 1 | awk '{print $2}')
(( after > before )) || { echo "the relative-motion timestamp went back across the wrap: $before -> $after"; exit 1; }

# a flash three seconds long, rung at most 2.5 seconds before the wrap
await_mean "$shot" 'app_id=clock-wrap' '$r -lt 20 && $g -lt 20 && $b -lt 20' >/dev/null \
    || { echo "the bell rung before the wrap never faded after it"; exit 1; }

touch "$XDG_RUNTIME_DIR/go-exit"
expect_client_ok "the clock-wrap client failed"
expect_alive "compositor died across the clock's wrap"
echo "OK: the bell fades and relative timestamps rise across the clock's wrap"
