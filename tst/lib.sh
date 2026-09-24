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

# wait until the client's toplevel is on screen (the compositor logs
# "mapped"). Twenty seconds: a sanitized compositor on a software
# rasterizer takes its time.
wait_mapped() {
    await 200 in_log "mapped" || {
        echo "client window did not map"; cat "$CLIENT_LOG" "$IMWAY_LOG" 2>/dev/null; exit 1; }
}

# wait for a marker line the client itself prints
wait_client() { # <grep-pattern>
    await 200 grep -q "$1" "$CLIENT_LOG" || {
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

# Start a fresh client after a hostile input/data-device scenario and prove
# that both pointer and keyboard routing still work. A live process alone is
# not enough: a dead grab can leave the compositor running but input wedged.
input_health_probe() {
    local probe log pid x y rc=0

    probe="$(dirname "$IMWAY_CLIENT")/client_input_health_probe"
    log="$XDG_RUNTIME_DIR/input-health.log"
    "$probe" >"$log" 2>&1 &
    pid=$!

    await 100 grep -q "input-health ready" "$log" || {
        echo "input health probe did not map"; cat "$log" "$IMWAY_LOG" 2>/dev/null; return 1; }
    point_at_color 0 255 0 || {
        echo "input health probe window not found"; cat "$log" 2>/dev/null; return 1; }
    read -r x y < <(centroid "$XDG_RUNTIME_DIR/_pt.ppm" 0 255 0)
    click_at "$x" "$y"
    ctl "key 2 press"
    ctl "key 2 release"
    wait "$pid" || rc=$?

    [[ $rc -eq 0 ]] || {
        echo "pointer/keyboard input did not recover (rc=$rc)"; cat "$log" 2>/dev/null; return 1; }
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

# a frame composed with everything sent before it: the barrier input needs
# when it must land on what a frame drew (a hover, a combo just opened). A
# screenshot is one too, at the price of reading the whole output back and
# writing it out, which a loaded runner pays for in seconds
compose_frame() {
    ctl "frame"
    dump_state >/dev/null
}

# left-click exactly at (x,y): the pick runs on last-frame hover, so move,
# compose a frame, nudge, compose again, then press and release, each seen
# in a composed frame of its own. Prefer this over point_at_color when
# windows overlap — a color-bbox centroid can land on the occluding window.
click_at() { # <x> <y>
    ctl "motion $1 $2"
    compose_frame || return 1
    ctl "motion $(($1 + 1)) $2"
    compose_frame || return 1
    ctl "button left press"
    compose_frame || return 1
    ctl "button left release"
    compose_frame
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

# true once the compositor has laid the client's window out. The surface
# fields (imgx, client_w) are in the dump from the commit that maps the
# window, imgx still 0; the frame's own size (w) is 0 until the frame that
# places the window, so that is what tells a laid-out window
have_rect() { # <dump-pattern>
    local line w cw
    line=$(dump_state | grep -m1 -E -- "$1") || return 1
    [[ "$line" == *" imgx="* ]] || return 1
    w=$(awk '{ for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2 && kv[1] == "w") { print kv[2]; exit } }' <<<"$line")
    cw=$(awk '{ for (i = 1; i <= NF; i++) if (split($i, kv, "=") == 2 && kv[1] == "client_w") { print kv[2]; exit } }' <<<"$line")
    [[ -n "$w" && "$w" -gt 0 && -n "$cw" && "$cw" -gt 0 ]]
}

# Wait for the frame that lays a client's window out before reading its rect
# from the dump. The compositor logs the map, and the scenario's wait_mapped
# returns, before that frame: read the rect right after and it can still be
# missing -- and an empty field silently becomes 0 in the arithmetic that
# follows, parking the pointer at the origin instead of over the client. An
# instrumented build makes that window wide enough to hit regularly.
wait_rect() { # <dump-pattern>
    await 100 have_rect "$1" || {
        echo "the compositor never laid out a window matching: $1"
        dump_state
        exit 1
    }
}

# echo the dump line of one compositor ImGui window; nonzero when it is not
# on screen. The dump reports the windows drawn last frame, so this is the
# compositor's own answer to "is the dialog up", with no pixel threshold and
# no dependence on the rasterizer's colours.
imgui_win() { # <name>
    dump_state | awk -v n="$1" '$1 == "imgui" && $2 == "name=" n' | grep .
}

imgui_gone() { # <name>
    ! imgui_win "$1" >/dev/null 2>&1
}

# true once <name> is the window holding the keyboard AND a text field in it
# is taking input
imgui_typing() { # <name>
    local line
    line=$(dump_state | grep '^imgui focus ') || return 1
    [[ "$line" == *"name=$1 "* && "$line" == *"want_text=1"* ]]
}

# Poll until a compositor dialog is on screen, or off it. A fixed sleep
# asserts on whatever frame happened to be up: a dialog can be a frame or
# several away, and an instrumented build makes that window wide.
await_imgui()    { await 100 imgui_win "$1" >/dev/null; } # <name>
await_no_imgui() { await 100 imgui_gone "$1"; }           # <name>

# Wait until a dialog's text field is the one taking input, before typing at
# it. ImGui only trickles characters ahead of the keys queued behind them
# while a text field wants input; type into a window that has not got there
# yet and the arrow key can be applied in the same frame as the text, ahead
# of whatever the text was meant to change.
await_typing() { await 100 imgui_typing "$1"; }           # <name>

# true once the active text field holds exactly this text: characters
# trickle into ImGui a frame at a time, so Enter waits for all of them
imgui_input_is() { # <text>
    [[ "$(dump_state | sed -n 's/^imgui input len=[0-9]* text=//p')" == "$1" ]]
}
await_input() { await 100 imgui_input_is "$1"; }             # <text>

# Escape until <check> holds: a client window can be up (in the dump) before
# its keyboard focus arrives, and an Escape sent then goes nowhere
escape_until() { # <check...>
    escape_then() { ctl "key 1 press"; ctl "key 1 release"; sleep 0.2; "$@"; }
    await 50 escape_then "$@"
}

# Print rounded mean R G B and pixel count from the inset client content box.
# Averaging makes color assertions compatible with output dithering while
# retaining their sub-code luminance/chromaticity checks.
surface_mean() { # <ppm> <dump-pattern> [inset]
    local path=$1 pattern=$2 inset=${3:-16}
    local x y w h
    x=$(dump_field "$pattern" imgx); y=$(dump_field "$pattern" imgy)
    w=$(dump_field "$pattern" client_w); h=$(dump_field "$pattern" client_h)
    python3 - "$path" "$x" "$y" "$w" "$h" "$inset" <<'PY'
import sys
path = sys.argv[1]
x, y, cw, ch, inset = map(int, sys.argv[2:])
f = open(path, 'rb'); assert f.readline().strip() == b'P6'
w, h = map(int, f.readline().split()); assert f.readline().strip() == b'255'
d = f.read(w*h*3)
pixels = [d[(yy*w+xx)*3:(yy*w+xx)*3+3]
          for yy in range(y+inset, y+ch-inset)
          for xx in range(x+inset, x+cw-inset)]
assert pixels, 'empty client surface sample'
print(*(round(sum(p[c] for p in pixels) / len(pixels)) for c in range(3)),
      len(pixels))
PY
}

# Take fresh readbacks until the client surface's mean satisfies <cond>, a
# [[ ]] expression over r, g, b (means) and n (sampled pixels); echo the
# values it settled on. A bare sleep then screenshot asserts on whatever
# happened to be on screen: the frame carrying the client's content can be a
# configure round trip away, and a sanitized or instrumented build makes
# that window wide. Quote <cond> so it expands here, not at the call site.
await_mean() { # <ppm> <dump-pattern> <cond>
    local i r g b n

    for ((i = 0; i < 40; i++)); do
        if screenshot "$1"; then
            read -r r g b n < <(surface_mean "$1" "$2" 2>/dev/null) || true

            if [[ -n "${n:-}" ]]; then
                if eval "[[ $3 ]]"; then
                    echo "$r $g $b $n"

                    return 0
                fi
            fi
        fi

        sleep 0.2
    done

    echo "surface mean settled at ${r:-?} ${g:-?} ${b:-?}, n=${n:-0}" >&2

    return 1
}

# wait until a window stops moving: on a slow runner it can still be placed
# after it maps, and a rect read then aims at where it is no longer
wait_placed() { # <dump-pattern>
    local i last="" now

    for ((i = 0; i < 50; i++)); do
        now="$(dump_field "$1" imgx) $(dump_field "$1" imgy)"
        [[ "$now" != " " && "$now" == "$last" ]] && return 0
        last=$now
        sleep 0.3
    done

    return 1
}

# two screenshots for a check that they agree, a real moment apart: a
# separate client (the screenshot editor, a spawned tool) redraws a frame or
# more after the input that changed it, and two shots taken back to back
# can both still show the old view and agree on it. The caller compares
settle_pair() { # <first ppm> <second ppm> [gap seconds, 0.2]
    screenshot "$1" && sleep "${3:-0.2}" && screenshot "$2"
}

# screenshot until <check> passes on it: a buffer committed before the
# call can still be a frame away from the output, so one shot after a
# sleep is a coin toss on a loaded runner. The last try runs the check
# with its output, so a real failure still says why
shot_until() { # <ppm> <check-command...>
    local ppm=$1 i

    shift

    for ((i = 0; i < 50; i++)); do
        screenshot "$ppm"
        "$@" "$ppm" >/dev/null 2>&1 && break
        sleep 0.1
    done

    "$@" "$ppm"
}

# request a screenshot and wait for the whole file: the compositor writes
# it out while it runs the command, so a dump sent after it arrives once
# the file is complete (the control FIFO runs commands in order)
screenshot() {
    rm -f "$1"

    ctl "screenshot $1"
    dump_state >/dev/null || return 1

    [[ -s "$1" ]] || { echo "screenshot $1 was not written" >&2; return 1; }
}

# fd leak checks: the compositor's open fds by what they point at. The
# baseline follows a composed frame: the first frames open what they then
# keep (the driver's caches, the fences of the frame in flight), and on a
# slow runner they can come after the scenario has started. The check
# gives the compositor time to take down the clients that just exited: a
# disconnect it has not got to yet still holds the client's socket and
# every fd the client sent, which is not a leak
fd_targets() {
    local f

    for f in /proc/"$IMWAY_PID"/fd/*; do
        readlink "$f" 2>/dev/null || true
    done | sort
}
fd_baseline() { # <file>
    screenshot "$XDG_RUNTIME_DIR/_fd_settle.ppm"
    fd_targets > "$1"
}
fds_within() { # <baseline file> <slack>
    fd_targets > "$XDG_RUNTIME_DIR/_fd_now.txt"
    [[ $(wc -l < "$XDG_RUNTIME_DIR/_fd_now.txt") -le $(($(wc -l < "$1") + $2)) ]]
}
expect_fds_kept() { # <baseline file> <slack> <what>: at most <slack> more open than at the baseline
    await 50 fds_within "$1" "$2" || {
        echo "compositor leaked fds $3: before=$(wc -l < "$1") after=$(wc -l < "$XDG_RUNTIME_DIR/_fd_now.txt")"
        diff "$1" "$XDG_RUNTIME_DIR/_fd_now.txt" | grep '^[<>]' || true
        exit 1
    }
}

# the framebuffer at its own depth: maxval 1023 for a 10-bit one
screenshot_raw() {
    rm -f "$1"

    ctl "screenshot-raw $1"
    dump_state >/dev/null || return 1

    [[ -s "$1" ]] || { echo "screenshot $1 was not written" >&2; return 1; }
}

# A fixed-length run of a second compositor on the KMS emulator, next to
# the scenario's own: boots the display/driver shape the emulator's knobs
# describe, renders three frames and exits. Output lands in BOOT_OUT, the
# exit code in BOOT_RC; the direct seat keeps it off the scenario's VT.
kms_boot() { # [VAR=value...] -- [imway args...]
    local envs=() bin

    while [[ $# -gt 0 && "$1" != "--" ]]; do
        envs+=("$1")
        shift
    done

    shift
    bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"
    BOOT_RC=0
    BOOT_OUT=$(env IMWAY_FAKE_KMS=1 IMWAY_SETTINGS=advanced.seat_backend=2 "${envs[@]}" timeout 60 "$bin" --device auto --socket imway-boot --frames 3 "$@" 2>&1) || BOOT_RC=$?
}

# fail unless the last kms_boot printed (boot_has) or did not print
# (boot_lacks) the pattern, or exited with the code (boot_rc)
boot_has() { # <pattern> [what]
    grep -q -- "$1" <<<"$BOOT_OUT" || { echo "${2:-boot}: no '$1' (rc=$BOOT_RC)"; echo "$BOOT_OUT"; exit 1; }
}

boot_lacks() { # <pattern> [what]
    ! grep -q -- "$1" <<<"$BOOT_OUT" || { echo "${2:-boot}: unexpected '$1'"; echo "$BOOT_OUT"; exit 1; }
}

boot_rc() { # <code> [what]
    [[ "$BOOT_RC" -eq "$1" ]] || { echo "${2:-boot}: exit $BOOT_RC, expected $1"; echo "$BOOT_OUT"; exit 1; }
}
