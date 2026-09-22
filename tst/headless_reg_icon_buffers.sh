#!/usr/bin/env bash
# xdg-toplevel-icon keeps the largest raster it was given, takes XRGB like
# ARGB, lets a second set_icon before the commit win, and clears on a null
# icon.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "icons set"

icon_is() { # <app_id> <icon_w>
    [[ "$(dump_field "app_id=$1 " icon_w)" == "$2" ]]
}

check() { # <app_id> <icon_w> <what>
    await 50 icon_is "$1" "$2" || {
        echo "$3: icon_w=$(dump_field "app_id=$1 " icon_w), expected $2"
        dump_state | grep app_id=
        exit 1
    }
}

check icon-keeps-big 64 "a smaller buffer replaced the larger raster"
check icon-xrgb 48 "the XRGB icon was not taken"
check icon-twice 40 "the second set_icon did not replace the first"
check icon-cleared 0 "a null set_icon left the old icon"

expect_alive "compositor died setting toplevel icons"
echo "OK: toplevel icons kept the right raster"
