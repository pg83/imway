#!/usr/bin/env bash
# The KMS session on a host where its VT cannot be had: when the query for
# the active VT fails, or the VT found cannot be opened, the session runs
# anyway, says input will leak to the console, and leaves the console
# alone on the way out. Needs a host whose VT the compositor can find in
# the first place (the CI runner's is opened up for this); elsewhere the
# fallback is what always runs and there is nothing to break.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

kms_boot --
boot_rc 0 "plain boot"
if grep -q "cannot find own vt\|unavailable, input will leak" <<<"$BOOT_OUT"; then
    echo "SKIP: this host gives the compositor no VT to lose"
    exit 127
fi

# both probes (/dev/tty, /dev/tty0) answer with an error
kms_boot IMWAY_CHAOS=vt-state=2 --
boot_rc 0 "vt query failed"
boot_has "cannot find own vt, input will leak to console"
boot_has "clean exit after"

kms_boot IMWAY_CHAOS=vt-open=1 --
boot_rc 0 "vt open failed"
boot_has "imway: /dev/tty[0-9]* unavailable, input will leak to console"
boot_has "clean exit after"

expect_alive "the scenario's own compositor died"
echo "OK: a VT the session cannot have leaves it running"
