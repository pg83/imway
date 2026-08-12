#!/usr/bin/env bash
# The xdg store loads png sizes alongside the scalable svg and prefers the
# png: the smallest raster that still covers the desired bucket wins, and the
# svg only fills the gap above the largest png. A 48px png reveals its source
# unambiguously — the svg would rasterize at a power-of-two bucket instead.
# imway-env: XDG_DATA_HOME=./xdg
# imway-pre: mkdir -p xdg/icons/hicolor/48x48/apps xdg/icons/hicolor/scalable/apps
# imway-pre: printf '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#00ff00"/></svg>' > xdg/icons/hicolor/scalable/apps/imway-pref.svg
# imway-pre: python3 -c "import zlib,struct;w=h=48;raw=b''.join(b'\x00'+b'\x00\xff\x00\xff'*w for _ in range(h));c=lambda t,d:struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff);open('xdg/icons/hicolor/48x48/apps/imway-pref.png','wb').write(b'\x89PNG\r\n\x1a\n'+c(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+c(b'IDAT',zlib.compress(raw))+c(b'IEND',b''))"
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "png-pref mapped"
wait_mapped
sleep 0.3

probe() { # <desired> -> icon_w
    ctl "icon-size $1"
    sleep 0.1
    dump_field 'app_id=imway-pref' icon_w
}

check() { # <desired> <expected_w> <why>
    local w
    w=$(probe "$1")
    [[ "$w" == "$2" ]] || { echo "desired $1: icon_w=$w, expected $2 ($3)"; exit 1; }
}

# the 48px png covers these buckets and wins over the svg
check 16 48 "png covers bucket 16"
check 32 48 "png covers bucket 32"
# bucket 64 exceeds the only png (48); the svg fills the gap, rasterized at 64
check 48 64 "svg fallback above the largest png"
check 300 512 "svg at the top bucket"

# install a brand-new size dir at runtime: its creation fires the hicolor
# parent watch (the NxN/apps leaf is not watched yet), the debounced reload
# re-enumerates and picks up the png. A 96px raster proves it — desired 64
# used to fall through to the svg at bucket 64.
mkdir -p "$XDG_RUNTIME_DIR/xdg/icons/hicolor/96x96/apps"
python3 -c "import zlib,struct;w=h=96;raw=b''.join(b'\x00'+b'\x00\xff\x00\xff'*w for _ in range(h));c=lambda t,d:struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff);open('$XDG_RUNTIME_DIR/xdg/icons/hicolor/96x96/apps/imway-pref.png','wb').write(b'\x89PNG\r\n\x1a\n'+c(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+c(b'IDAT',zlib.compress(raw))+c(b'IEND',b''))"

png96_live() { [[ "$(probe 64)" == 96 ]]; }
await 100 png96_live || { echo "runtime-installed png size dir was not picked up (icon_w=$(probe 64))"; exit 1; }

echo "OK: the store prefers png where it covers the size and falls back to svg above it"
