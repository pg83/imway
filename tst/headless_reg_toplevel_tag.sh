#!/usr/bin/env bash
# xdg-toplevel-tag: the tag must be visible in the compositor state dump.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "tagged"
wait_mapped 'app_id=tag-test'
sleep 0.3

tag=$(dump_field 'app_id=tag-test' tag)
[[ "$tag" == "pip" ]] || { echo "tag not applied: '$tag'"; exit 1; }

echo "OK: toplevel tag landed in the compositor model"
