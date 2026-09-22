#!/usr/bin/env bash
# The cursor plane's buffers are the first the output allocates: when its
# dumb buffer or its framebuffer cannot be made, the session boots on the
# software cursor.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

# the cursor's dumb buffer and its framebuffer are the first the output
# allocates
kms_boot IMWAY_FAKE_KMS_FAIL_DUMB=1 --
boot_rc 0 "no cursor buffer"
boot_has "cursor plane setup failed: .*, software cursor"
boot_has "clean exit after"

kms_boot IMWAY_FAKE_KMS_FAIL_ADDFB=1 --
boot_rc 0 "no cursor framebuffer"
boot_has "cursor plane setup failed: .*, software cursor"
boot_has "clean exit after"

expect_alive "the scenario's own compositor died"
echo "OK: a cursor plane without buffers falls back to software"
