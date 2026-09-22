#!/usr/bin/env bash
# The night light's schedule, and the SDR white setting. With the schedule on
# and the manual switch off, the output warms exactly when the local time is
# inside [start, end), a window that wraps midnight when start > end. The
# windows are laid out around the current minute with an hour of margin, so
# the answer cannot change while the scenario runs. Raising display.sdr_nits
# brightens SDR content on the HDR output.
# imway-args: --hdr 203 --hdr-peak 600
set -euo pipefail
. "$(dirname "$0")/lib.sh"

IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_night_light"
start_client
wait_client "night-light ready"
wait_rect 'app_id=night-light'

shot="$XDG_RUNTIME_DIR/s.ppm"
neutral() { await_mean "$shot" 'app_id=night-light' '$r -le $((b + 3))' >/dev/null; }
warm() { await_mean "$shot" 'app_id=night-light' '$r -ge $((b + 12))' >/dev/null; }

neutral || { echo "the gray client is not neutral to begin with"; exit 1; }
ctl "set color.temperature 2500"
ctl "set color.night_scheduled true"

# the local minute of day, the compositor's clock: both run in this TZ
now=$(date +%H:%M)
m=$((10#${now%%:*} * 60 + 10#${now##*:}))
wrap() {
    echo $(((($1) % 1440 + 1440) % 1440))
}

# <start offset> <end offset>, relative to now; the expectation follows the
# compositor's own rule for a window that does or does not wrap midnight
window() {
    local s e expect
    s=$(wrap "m + $1"); e=$(wrap "m + $2")
    if ((s <= e)); then
        expect=$((m >= s && m < e))
    else
        expect=$((m >= s || m < e))
    fi
    ctl "set color.night_start $s"
    ctl "set color.night_end $e"
    if ((expect)); then
        warm || { echo "[$s, $e) holds minute $m but the output did not warm"; exit 1; }
    else
        neutral || { echo "[$s, $e) misses minute $m but the output warmed"; exit 1; }
    fi
    echo "window [$s, $e) at $m: warm=$expect"
}

# alternating, so no answer can be left over from the window before it
window 60 120    # later today
window -60 60    # around now
window 60 -60    # wraps, and now falls in the gap it leaves
window 120 60    # wraps, and now is before its end
window 0 0       # empty
window -60 -120  # wraps, and now is past its start
window 0 0

# the manual switch still wins over an empty schedule
ctl "set color.night_light true"
warm || { echo "the manual switch did not warm the output"; exit 1; }
ctl "set color.night_light false"
ctl "set color.night_scheduled false"
neutral || { echo "switching the schedule off did not neutralize the output"; exit 1; }

read -r r0 _ _ _ < <(await_mean "$shot" 'app_id=night-light' '$n -gt 0')
ctl "set display.sdr_nits 400"
brighter() { await_mean "$shot" 'app_id=night-light' "\$r -ge $((r0 + 10))" >/dev/null; }
brighter || { echo "raising the SDR white did not brighten SDR content (was $r0)"; exit 1; }

expect_alive "compositor died applying the night light schedule"
echo "OK: the night light follows its schedule window, and sdr_nits moves SDR white"
