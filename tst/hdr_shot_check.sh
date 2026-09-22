# Checks for a screenshot saved from an HDR (BT.2100 PQ) session, against two
# captures of the output taken around the save (only pixels equal in both
# count, the rest of the desktop may have moved on).

# the PNG is the SDR mapping of the frame: every stable neutral pixel is its
# PQ code decoded to nits, tone-mapped to the 203-nit SDR peak and sRGB
# encoded, within <tolerance> codes
hdr_png_check() { # <png> <before.ppm> <after.ppm> <tolerance>
    python3 - "$@" <<'PY'
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

tolerance = int(sys.argv[4])
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
        if all(abs(c - want) <= tolerance for c in got):
            matched += 1
        if all(abs(c - r) <= 1 for c in got) and abs(want - r) > 3:
            pq_like += 1

print(f"neutral samples={checked} matched={matched} pq-coded={pq_like}")
assert checked >= 50, "too few stable neutral pixels to compare"
assert matched >= checked * .9, "the PNG is not the SDR mapping of the HDR frame"
PY
}

# the JPEG XL (decoded to 8 bits without color management) keeps the PQ code
# values themselves, within <tolerance> codes
hdr_jxl_check() { # <jxl as ppm> <before.ppm> <after.ppm> <tolerance>
    python3 - "$@" <<'PY'
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
tolerance = int(sys.argv[4])
same = sum(1 for i in stable if all(abs(jxl[i + c] - before[i + c]) <= tolerance for c in range(3)))
print(f"jxl stable samples={len(stable)} pq-exact={same}")
assert len(stable) >= 1000 and same >= len(stable) * .95, "the JPEG XL lost the PQ code values"
PY
}
