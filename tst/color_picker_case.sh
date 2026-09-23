# The eyedropper on an output whose frame is read back in a way of its own
# (the scenario's backend or cursor): armed from the launcher, a click
# on a client window of a known colour samples it, posts it and shows the
# swatch. The swatch carries the sampled colour, the window's as the
# output's own capture shows it; on an HDR output ($swatch_white, the SDR
# white in nits) the swatch is itself SDR content, drawn through PQ.
IMWAY_CLIENT="$IMWAY_TESTS_BIN/client_reg_render_fault"
start_client
wait_client "render fault ready"
wait_rect 'title=render-fault-victim'

swatch_up() {
    [[ -n "$(dump_field '^imgui name=##pick' x)" ]]
}
notes_active() {
    dump_field '^notifications ' active
}

# the window's colour as the output shows it, once a frame carries it
sampled() {
    local x y
    x=$(dump_field 'title=render-fault-victim' imgx); y=$(dump_field 'title=render-fault-victim' imgy)
    [[ -n "$x" && -n "$y" ]] || return 1
    screenshot "$XDG_RUNTIME_DIR/before.ppm" || return 1
    read -r er eg eb < <(python3 - "$XDG_RUNTIME_DIR/before.ppm" "$((x + 120))" "$((y + 80))" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x, y = map(int, sys.argv[2:4])
p = (y * w + x) * 3
print(d[p], d[p + 1], d[p + 2])
PY
)
    (( er > eg + 30 && er > eb + 30 ))
}
er=0; eg=0; eb=0
await 50 sampled || { echo "the sample point never showed the red window: $er $eg $eb"; exit 1; }
echo "the window as the output shows it: $er $eg $eb"

# arm the picker from the launcher; a sanitized or loaded build takes frames
# for each step, so each waits for its own result instead of a fixed sleep
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "the launcher did not open"; dump_state; exit 1; }
ctl "type color picker"
await_input "color picker" || { echo "the query did not reach the launcher"; dump_state; exit 1; }
ctl "key 103 press"; ctl "key 103 release" # Up: into the action row
ctl "key 28 press"; ctl "key 28 release"   # Enter
await_no_imgui '##launcher' || { echo "the launcher did not run the picker"; dump_state; exit 1; }

# the click samples the window: re-aim at its current place for every try
for _ in $(seq 1 10); do
    x=$(dump_field 'title=render-fault-victim' imgx); y=$(dump_field 'title=render-fault-victim' imgy)
    click_at "$((x + 120))" "$((y + 80))"
    await 20 swatch_up && break
done
swatch_up || { echo "the picker swatch did not appear"; dump_state; exit 1; }
posted() { [[ "$(notes_active)" = 1 ]]; }
await 50 posted || { echo "the picked colour was not posted"; dump_state; exit 1; }

# the swatch as it is drawn now: its rect and a fresh frame, every try
swatch_shows() {
    local sx sy sw sh
    sx=$(dump_field '^imgui name=##pick' x); sy=$(dump_field '^imgui name=##pick' y)
    sw=$(dump_field '^imgui name=##pick' w); sh=$(dump_field '^imgui name=##pick' h)
    [[ -n "$sx" && -n "$sh" ]] || return 1
    screenshot "$XDG_RUNTIME_DIR/swatch.ppm" || return 1
    python3 - "$XDG_RUNTIME_DIR/swatch.ppm" "$sx" "$sy" "$sw" "$sh" "$er" "$eg" "$eb" "${swatch_white:-0}" <<'PY'
import sys
with open(sys.argv[1], 'rb') as f:
    assert f.readline().strip() == b'P6'
    w, h = map(int, f.readline().split())
    f.readline()
    d = f.read(w * h * 3)
x0, y0, sw, sh, r, g, b, white = map(int, sys.argv[2:10])
if white:
    # an HDR output draws the swatch's sRGB colour as SDR content: to linear,
    # BT.709 into BT.2020 primaries, SDR white at `white` nits, PQ encoded
    def lin(v):
        v /= 255
        return v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4
    m = ((.6274, .3293, .0433), (.0691, .9195, .0114), (.0164, .0880, .8956))
    l = [lin(v) for v in (r, g, b)]
    def pq(n):
        m1, m2, c1, c2, c3 = 2610 / 16384, 2523 / 32, 3424 / 4096, 2413 / 128, 2392 / 128
        y = (max(n, 0) / 10000) ** m1
        return round(((c1 + c2 * y) / (1 + c3 * y)) ** m2 * 255)
    r, g, b = (pq(sum(m[i][j] * l[j] for j in range(3)) * white) for i in range(3))
    print(f"expected swatch colour on the PQ output: {r} {g} {b}")
hits = 0
for yy in range(y0, y0 + sh):
    for xx in range(x0, x0 + sw):
        p = (yy * w + xx) * 3
        if all(abs(d[p + c] - v) <= 12 for c, v in enumerate((r, g, b))):
            hits += 1
print(f"swatch pixels of the sampled colour: {hits}")
assert hits > 50, "the swatch does not show the window's colour"
PY
}
await 30 swatch_shows || { echo "the swatch does not show the window's colour"; exit 1; }

ctl "key 1 press"; ctl "key 1 release" # Escape closes the swatch
expect_alive "the picker took the compositor with it"
echo "OK: the eyedropper reads the window's colour back from this output"
