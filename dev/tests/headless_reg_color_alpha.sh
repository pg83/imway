#!/usr/bin/env bash
# imway-args: --hdr 203 --hdr-peak 10000
# Wayland ARGB8888 is electrical-premultiplied. Verify that both sRGB and PQ
# are unpremultiplied before EOTF and then blended in the linear-nits scene.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

sample_stripes() { # <ppm>; locate the base surface and sample four stripe centres
    python3 - "$@" <<'PY'
import collections
import sys

path = sys.argv[1]
with open(path, "rb") as source:
    assert source.readline().strip() == b"P6"
    width, height = map(int, source.readline().split())
    assert source.readline().strip() == b"255"
    data = source.read(width * height * 3)

pixels = list(zip(data[::3], data[1::3], data[2::3]))
colors = collections.Counter(pixels)
for base, count in colors.most_common():
    if 43000 <= count <= 47000:
        break
else:
    raise SystemExit("base surface color not found")

positions = [(i % width, i // width) for i, color in enumerate(pixels) if color == base]
x0, y0 = min(x for x, _ in positions), min(y for _, y in positions)
result = []
for offset in (25, 75, 125, 175):
    result.extend(pixels[(y0 + 100) * width + x0 + 50 + offset])
print(*result)
PY
}

expected() { # sdr | pq; print four RGB triples for alpha 0, 64, 128, 255
    python3 - "$1" <<'PY'
import math
import sys

kind = sys.argv[1]

def srgb_eotf(value):
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4

def pq_eotf(value):
    m1, m2 = 0.1593017578125, 78.84375
    c1, c2, c3 = 0.8359375, 18.8515625, 18.6875
    power = max(value, 0.0) ** (1.0 / m2)
    return (max(power - c1, 0.0) / (c2 - c3 * power)) ** (1.0 / m1) * 10000.0

def pq_oetf(nits):
    m1, m2 = 0.1593017578125, 78.84375
    c1, c2, c3 = 0.8359375, 18.8515625, 18.6875
    power = max(nits / 10000.0, 0.0) ** m1
    return ((c1 + c2 * power) / (1.0 + c3 * power)) ** m2

matrix = (
    (0.627404, 0.329283, 0.043313),
    (0.069097, 0.919540, 0.011362),
    (0.016391, 0.088013, 0.895595),
)
background_bytes = (32, 64, 128) if kind == "sdr" else (80, 100, 120)
straight_bytes = (220, 100, 40) if kind == "sdr" else (200, 160, 100)
decode = srgb_eotf if kind == "sdr" else pq_eotf
background = [decode(value / 255.0) for value in background_bytes]
result = []

for alpha_byte in (0, 64, 128, 255):
    alpha = alpha_byte / 255.0
    premultiplied = [(value * alpha_byte + 127) // 255 for value in straight_bytes]
    electrical = [value / alpha_byte if alpha_byte else 0.0 for value in premultiplied]
    foreground = [decode(value) for value in electrical]
    mixed = [foreground[i] * alpha + background[i] * (1.0 - alpha) for i in range(3)]
    if kind == "sdr":
        nits = [sum(matrix[row][column] * mixed[column] for column in range(3)) * 203.0
                for row in range(3)]
    else:
        nits = mixed
    result.extend(round(pq_oetf(value) * 1023.0) // 4 for value in nits)

# A2R10G10B10 is rounded to ten bits; the PPM readback keeps its high eight.
print(*result)
PY
}

assert_stripes() { # actual array name, expected array name, label
    local -n actual=$1 expected_values=$2
    local label=$3

    for index in $(seq 0 11); do
        local delta=$((actual[index] - expected_values[index]))
        [[ "$delta" -ge -3 && "$delta" -le 3 ]] || {
            echo "$label channel $index: got ${actual[index]}, expected ${expected_values[index]}"; exit 1; }
    done
}

start_client
wait_client "sdr-alpha"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/sdr-alpha.ppm"
read -r -a sdr_actual < <(sample_stripes "$XDG_RUNTIME_DIR/sdr-alpha.ppm")
read -r -a sdr_expected < <(expected sdr)
assert_stripes sdr_actual sdr_expected "sRGB electrical alpha"

wait_client "pq-alpha"
sleep 0.3
screenshot "$XDG_RUNTIME_DIR/pq-alpha.ppm"
read -r -a pq_actual < <(sample_stripes "$XDG_RUNTIME_DIR/pq-alpha.ppm")
read -r -a pq_expected < <(expected pq)
assert_stripes pq_actual pq_expected "PQ electrical alpha"

echo "OK: electrical premultiplication is removed before SDR/PQ EOTF"
