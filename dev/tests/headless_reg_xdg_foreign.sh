#!/usr/bin/env bash
# xdg-foreign-v2: the imported handle parents a foreign toplevel; revoking
# the export breaks the link and notifies the importer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

start_client
wait_client "attached"
wait_mapped 'app_id=foreign-child'
sleep 0.3

pid=$(dump_field 'app_id=foreign-parent' id)
cparent=$(dump_field 'app_id=foreign-child' parent)
[[ "$cparent" == "$pid" ]] || {
    echo "child not attached to the exported toplevel: parent=$cparent expected=$pid"; exit 1; }

# revoke the export
ctl "key 57 press"
ctl "key 57 release"
wait_client "revoked"
sleep 0.3

cparent=$(dump_field 'app_id=foreign-child' parent)
[[ "$cparent" == "0" ]] || { echo "parent link survived the revoke: $cparent"; exit 1; }

echo "OK: xdg-foreign attached and revoked the foreign parent"
