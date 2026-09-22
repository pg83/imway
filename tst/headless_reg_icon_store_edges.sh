#!/usr/bin/env bash
# private-session-bus
# imway-env: XDG_DATA_HOME=./xdg XDG_DATA_DIRS=./xdg2::./no-such-dir:/usr/local/share:/usr/share
# The icon store's odd corners, staged after startup and picked up by an
# icon-theme change (which reloads the store; setting it to hicolor itself
# must not index hicolor twice). Desktop files whose Icon= is an absolute
# svg, png or other path (one without a trailing newline), stray files and
# directories where desktop files, svgs and pngs belong, size directories
# that are not plain NxN, a png present in two data dirs, a name with only
# a smaller png, and pngs and svgs that cannot be decoded. Notification
# icons take the string paths: absolute files and a mixed-case name. The
# system data dirs stay at the end of XDG_DATA_DIRS: on a distribution
# install the Vulkan loader finds its drivers through them.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

stage() {
    python3 - "$XDG_RUNTIME_DIR" <<'PY'
import os, struct, sys, zlib
rt = sys.argv[1]

def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)

def png(path, w, h, data=True, truncate=False):
    raw = b''.join(b'\x00' + b'\xff\x00\x00\xff' * w for _ in range(h)) if data else b''
    idat = zlib.compress(raw) if data else b''
    if truncate:
        idat = idat[:len(idat) // 4]
    blob = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', idat) + chunk(b'IEND', b'')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(blob)

def text(path, body):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'w').write(body)

x = rt + '/xdg'
apps = x + '/applications'
hc = x + '/icons/hicolor'
svg = '<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="#ff0000"/></svg>'

text(rt + '/abs.svg', svg)
png(rt + '/abs.png', 48, 48)
text(rt + '/abs.xpm', '/* XPM */')

text(apps + '/imway-abs-svg.desktop', '[Desktop Entry]\nName=a\nIcon=%s/abs.svg\n' % rt)
text(apps + '/imway-abs-png.desktop', '[Other]\nIcon=nope\n[Desktop Entry]\nIcon=%s/abs.png' % rt)
text(apps + '/imway-abs-other.desktop', '[Desktop Entry]\nIcon=%s/abs.xpm\n' % rt)
text(apps + '/README', 'not a desktop file\n')
os.makedirs(apps + '/folder.desktop', exist_ok=True)

text(hc + '/scalable/apps/notes.txt', 'not an svg\n')
os.makedirs(hc + '/scalable/apps/folder.svg', exist_ok=True)
text(hc + '/scalable/apps/imway-badsvg.svg', 'this is not svg')
png(hc + '/48x48/apps/imway-small.png', 48, 48)
text(hc + '/48x48/apps/readme.txt', 'not a png\n')
os.makedirs(hc + '/48x48/apps/folder.png', exist_ok=True)
png(hc + '/48x48@2/apps/imway-scaled.png', 96, 96)
png(hc + '/4a4x4a4/apps/imway-letters.png', 16, 16)
png(hc + '/48x32/apps/imway-oblong.png', 48, 32)
text(hc + '/READ.ME', 'a file among the size dirs\n')
text(hc + '/64x64/apps/imway-garbage.png', 'not a png at all')
png(hc + '/128x128/apps/imway-huge.png', 2000, 2000, data=False)
png(hc + '/256x256/apps/imway-truncated.png', 256, 256, truncate=True)

# the same name and size again in the second data dir: the first one wins
png(rt + '/xdg2/icons/hicolor/48x48/apps/imway-small.png', 48, 48)
PY
}

icon_w() { # <app_id>
    dump_state | awk -v id="$1" '$1 == "toplevel" { for (i = 1; i <= NF; i++) if ($i == "app_id=" id) { for (j = 1; j <= NF; j++) if ($j ~ /^icon_w=/) { sub(/^icon_w=/, "", $j); print $j } } }'
}

icon_is() { [[ "$(icon_w "$1")" == "$2" ]]; }

fail() {
    echo "$1"
    dump_state
    cat "$CLIENT_LOG" "$IMWAY_LOG"
    exit 1
}

stage
start_client imway-abs-svg imway-abs-png imway-abs-other imway-small imway-garbage imway-huge imway-truncated imway-badsvg imway-letters
wait_client "windows mapped"

# reload through the theme setting: an empty theme, then hicolor itself
ctl "set appearance.icon_theme "
await 50 in_log "icon store reloaded" || fail "clearing the icon theme did not reload the store"
ctl "set appearance.icon_theme hicolor"
reloads() { [[ "$(grep -c "icon store reloaded" "$IMWAY_LOG")" -ge 2 ]]; }
await 50 reloads || fail "setting the icon theme did not reload the store"

await 100 icon_is imway-abs-svg 64 || fail "an absolute svg Icon= did not render at the bucket"
icon_is imway-abs-png 48 || fail "an absolute png Icon= did not load"
icon_is imway-abs-other 0 || fail "an Icon= path of another format produced an icon"
icon_is imway-small 48 || fail "a name with only a smaller png did not fall back to it"
for app in imway-garbage imway-huge imway-truncated imway-badsvg imway-letters; do
    icon_is "$app" 0 || fail "$app produced an icon"
done

# notification icons: taller toasts carry an icon
wait_client "notifications posted"
toast_h() { # <what>
    local id
    id=$(awk -v w="$1" '$1 == "notification" && $3 == w { print $2 }' "$CLIENT_LOG")
    dump_field "^imgui name=##toast$id " h
}
toasts_up() { [[ -n "$(toast_h none)" && -n "$(toast_h mixed-case)" ]]; }
await 100 toasts_up || fail "the notifications did not show"
bare=$(toast_h none)
for what in abs-png abs-svg mixed-case; do
    [[ "$(toast_h "$what")" -gt "$bare" ]] || fail "the $what notification icon did not show"
done
[[ "$(toast_h abs-other)" == "$bare" ]] || fail "a notification icon of another format showed"

expect_alive "compositor died on odd icon store content"
echo "OK: the icon store's odd corners resolve or fail cleanly"
