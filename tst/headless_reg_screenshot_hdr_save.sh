#!/usr/bin/env bash
# imway-args: --hdr 203
# Saving from an HDR session. A PNG is an SDR image: the viewer decodes the
# captured PQ code values to nits, maps them to the SDR range with the
# display mapping and writes sRGB, SDR white (203 nits) landing on 255, so
# the PNG's neutral pixels must match that transform of the output's own PQ
# pixels, not the PQ codes themselves. A JPEG XL keeps the frame HDR.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_name hdr"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 20 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

screenshot "$XDG_RUNTIME_DIR/before.ppm"
ctl "key 99 press"; ctl "key 99 release" # Print

saved() {
    [[ -s "$shots/hdr.png" ]] && in_log "exited with status 0"
}
await 200 saved || { echo "the HDR save produced no PNG"; cat "$IMWAY_LOG"; exit 1; }
screenshot "$XDG_RUNTIME_DIR/after.ppm"

python3 - "$shots/hdr.png" "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" <<'PY'
import struct
import sys
import zlib

def load_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', "not a PNG"
    pos, idat = 8, b''
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            w, h, depth, color = struct.unpack('>IIBB', body[:10])
            assert depth == 8 and color == 6, "expected 8-bit RGBA"
        elif kind == b'IDAT':
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = w * 4
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, rows

def load_ppm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        f.readline()
        return w, h, f.read(w * h * 3)

m1, m2 = 2610 / 16384, 2523 / 32
c1, c2, c3 = 3424 / 4096, 2413 / 128, 2392 / 128

def nits(code):
    p = max(code / 255, 0) ** (1 / m2)
    return (max(p - c1, 0) / (c2 - c3 * p)) ** (1 / m1) * 10000

def tone(v):
    # the SDR target of the display mapping: peak 203, knee at 90%
    peak, knee = 203.0, 203.0 * .9
    if v <= knee:
        return min(v, peak)
    return peak - (peak - knee) ** 2 / ((peak - knee) + (v - knee))

def srgb8(v):
    v = max(0.0, min(1.0, v))
    e = v * 12.92 if v <= .0031308 else 1.055 * v ** (1 / 2.4) - .055
    return round(e * 255)

pw, ph, png = load_png(sys.argv[1])
w, h, before = load_ppm(sys.argv[2])
_, _, after = load_ppm(sys.argv[3])
assert (pw, ph) == (w, h), f"png {pw}x{ph} is not the {w}x{h} output"

checked = matched = pq_like = 0
for y in range(0, h, 7):
    row = png[y]
    for x in range(0, w, 7):
        i = (y * w + x) * 3
        r, g, b = before[i:i + 3]
        if (r, g, b) != tuple(after[i:i + 3]) or not (r == g == b) or r < 16:
            continue
        want = srgb8(tone(nits(r)) / 203)
        got = row[x * 4:x * 4 + 3]
        checked += 1
        if all(abs(c - want) <= 3 for c in got):
            matched += 1
        if all(abs(c - r) <= 1 for c in got) and abs(want - r) > 3:
            pq_like += 1

print(f"neutral samples={checked} matched={matched} pq-coded={pq_like}")
assert checked >= 50, "too few stable neutral pixels to compare"
assert matched >= checked * .9, "the PNG is not the SDR mapping of the HDR frame"
PY

# JPEG XL keeps the HDR frame as it is: lossless, tagged BT.2100 PQ, so the
# decoded code values are the output's own
ctl "set applications.screenshot_name hdr-jxl"
ctl "set applications.screenshot_format 0" # jxl
ctl "set applications.screenshot_lossless true"
ctl "key 99 press"; ctl "key 99 release"

jxl_saved() {
    [[ -s "$shots/hdr-jxl.jxl" && $(grep -c "exited with status 0" "$IMWAY_LOG") -ge 2 ]]
}
await 200 jxl_saved || { echo "the HDR save produced no JPEG XL"; cat "$IMWAY_LOG"; exit 1; }
"$IMWAY_TESTS_BIN/client_jxl_dump" "$shots/hdr-jxl.jxl" "$XDG_RUNTIME_DIR/jxl.ppm" >/dev/null || {
    echo "the HDR JPEG XL does not decode"
    exit 1
}

python3 - "$XDG_RUNTIME_DIR/jxl.ppm" "$XDG_RUNTIME_DIR/before.ppm" "$XDG_RUNTIME_DIR/after.ppm" <<'PY'
import sys

def load_ppm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        f.readline()
        return w, h, f.read(w * h * 3)

jw, jh, jxl = load_ppm(sys.argv[1])
w, h, before = load_ppm(sys.argv[2])
_, _, after = load_ppm(sys.argv[3])
assert (jw, jh) == (w, h), f"jxl {jw}x{jh} is not the {w}x{h} output"
stable = [i for i in range(0, w * h * 3, 3 * 11) if before[i:i + 3] == after[i:i + 3]]
same = sum(1 for i in stable if all(abs(jxl[i + c] - before[i + c]) <= 1 for c in range(3)))
print(f"jxl stable samples={len(stable)} pq-exact={same}")
assert len(stable) >= 1000 and same >= len(stable) * .95, "the JPEG XL lost the PQ code values"
PY

expect_alive "compositor died saving an HDR screenshot"
echo "OK: an HDR session saves an sRGB PNG mapped from its PQ pixels and a PQ JPEG XL"
