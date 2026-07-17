# Sourced by every headless_*.sh. dev/test.sh has ALREADY brought the
# compositor fully up (socket bound, control FIFO open, init complete) before
# the scenario runs — tests never poll for compositor startup. The environment:
#   XDG_RUNTIME_DIR  per-test scratch dir, removed after the test
#   WAYLAND_DISPLAY  socket of the per-test compositor (already running)
#   IMWAY_CTL        control FIFO of that compositor
#   IMWAY_LOG        compositor log
#   IMWAY_PID        compositor pid
#   IMWAY_CLIENT     the test's client binary, empty if it has none
#   IMWAY_CLIENT_LOG default stdout/stderr sink for the client (start_client)
# Exit 127 = skip. The runner quits the compositor itself and fails the
# test if it died or hung — tests only drive the scenario.

# background clients die with the test
trap 'kill $(jobs -p) 2>/dev/null || true' EXIT

# a stray SIGPIPE (e.g. the compositor briefly cycling the FIFO) must not kill
# the scenario mid-run
trap '' PIPE

CLIENT_LOG="${IMWAY_CLIENT_LOG:-$XDG_RUNTIME_DIR/client.log}"

# Hold one writer open on the control FIFO for the whole scenario. Opening and
# closing it per command makes the compositor see EOF and reopen the FIFO every
# time, and that reopen races the next write into a SIGPIPE under load.
exec 3>"$IMWAY_CTL"

ctl() {
    echo "$1" >&3
}

in_log() {
    grep -q "$1" "$IMWAY_LOG"
}

# launch the test's client in the background; its pid lands in CLIENT_PID and
# its output in CLIENT_LOG. Extra args are passed through.
start_client() {
    "$IMWAY_CLIENT" "$@" >"$CLIENT_LOG" 2>&1 &
    CLIENT_PID=$!
}

# wait until the client's toplevel is on screen (the compositor logs "mapped")
wait_mapped() {
    await 100 in_log "mapped" || {
        echo "client window did not map"; cat "$CLIENT_LOG" "$IMWAY_LOG" 2>/dev/null; exit 1; }
}

# wait for a marker line the client itself prints
wait_client() { # <grep-pattern>
    await 100 grep -q "$1" "$CLIENT_LOG" || {
        echo "client did not reach: $1"; cat "$CLIENT_LOG" "$IMWAY_LOG" 2>/dev/null; exit 1; }
}

# wait for the background client to exit; fail the test on a nonzero code
expect_client_ok() { # [message]
    local rc=0
    wait "$CLIENT_PID" || rc=$?
    [[ $rc -eq 0 ]] || { echo "${1:-client failed} (rc=$rc)"; cat "$CLIENT_LOG" 2>/dev/null; exit 1; }
}

# fail unless the compositor is still alive (survival tests)
expect_alive() { # [message]
    kill -0 "$IMWAY_PID" 2>/dev/null || { echo "${1:-compositor died}"; cat "$IMWAY_LOG" 2>/dev/null; exit 1; }
}

# await <tries> <cmd...> — poll at 0.1s until the command succeeds
await() {
    local i

    for ((i = 0; i < $1; i++)); do
        "${@:2}" && return 0
        sleep 0.1
    done

    return 1
}

# echo "X Y" — centroid of the pixels matching an RGB color (±50 per channel)
centroid() { # <ppm> <r> <g> <b>
    python3 - "$@" <<'PY'
import sys
path, R, G, B = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
f = open(path, 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); f.readline(); d = f.read(w*h*3)
pts = [(x, y) for y in range(h) for x in range(w)
       if abs(d[(y*w+x)*3]-R) < 50 and abs(d[(y*w+x)*3+1]-G) < 50 and abs(d[(y*w+x)*3+2]-B) < 50]
assert pts, 'color not found'
print((min(x for x, _ in pts)+max(x for x, _ in pts))//2,
      (min(y for _, y in pts)+max(y for _, y in pts))//2)
PY
}

# count pixels that differ between two ppms inside a box; echo the count
region_diff() { # <ppm1> <ppm2> <x0> <y0> <x1> <y1>
    python3 - "$@" <<'PY'
import sys
a, b = sys.argv[1], sys.argv[2]
x0, y0, x1, y1 = map(int, sys.argv[3:7])
def load(p):
    f = open(p, 'rb'); assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split()); f.readline()
    return w, h, f.read(w*h*3)
w, h, da = load(a); _, _, db = load(b)
n = 0
for y in range(max(0, y0), min(h, y1)):
    for x in range(max(0, x0), min(w, x1)):
        i = (y*w+x)*3
        if abs(da[i]-db[i]) + abs(da[i+1]-db[i+1]) + abs(da[i+2]-db[i+2]) > 40:
            n += 1
print(n)
PY
}

# screenshot, find a color, and move the pointer onto its centroid. Retries:
# the first frame may not be painted yet when the window has only just mapped.
point_at_color() { # <r> <g> <b>
    local xy i
    for ((i = 0; i < 30; i++)); do
        screenshot "$XDG_RUNTIME_DIR/_pt.ppm" || return 1
        if xy=$(centroid "$XDG_RUNTIME_DIR/_pt.ppm" "$@" 2>/dev/null); then
            ctl "motion $xy"
            return 0
        fi
        sleep 0.1
    done
    return 1
}

# left-click exactly at (x,y): the pick runs on last-frame hover, so move,
# render a frame, nudge, render again, then press. Prefer this over
# point_at_color when windows overlap — a color-bbox centroid can land on
# the occluding window.
click_at() { # <x> <y>
    ctl "motion $1 $2"
    screenshot "$XDG_RUNTIME_DIR/_click.ppm" || return 1
    ctl "motion $(($1 + 1)) $2"
    screenshot "$XDG_RUNTIME_DIR/_click.ppm" || return 1
    ctl "button left press"
    sleep 0.1
    ctl "button left release"
    sleep 0.2
}

# dump compositor state (toplevels/popups/focus, see control.cpp dumpState)
# to stdout. The compositor renames the file into place, so existence means
# a complete read.
dump_state() {
    local out="$XDG_RUNTIME_DIR/_dump.$$"
    rm -f "$out"
    ctl "dump $out"
    await 100 test -e "$out" || { echo "state dump did not arrive" >&2; return 1; }
    cat "$out"
    rm -f "$out"
}

# echo the value of <field> from the dump line matching <pattern> (first hit)
dump_field() { # <pattern> <field>
    dump_state | awk -v pat="$1" -v f="$2" '
        $0 ~ pat { for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2 && kv[1] == f) { print kv[2]; exit } }'
}

# request a screenshot and wait until the file settles: it appears at
# open() and fills up afterwards, so mere existence is a truncated read
screenshot() {
    rm -f "$1"
    ctl "screenshot $1"

    local prev=-1 size

    for _ in $(seq 1 100); do
        size=$(stat -c %s "$1" 2>/dev/null || echo -1)
        [[ "$size" -gt 0 && "$size" == "$prev" ]] && return 0
        prev=$size
        sleep 0.1
    done

    echo "screenshot $1 did not settle" >&2

    return 1
}
