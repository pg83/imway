#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_FAKE_KMS_NO_PRIME=1 IMWAY_CHAOS=output-target=27
# imway-args: --device auto
# On the dumb-buffer path the renderer draws into an offscreen target of
# its own; the rebuild for the new mode fails at the offscreen target's framebuffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
boot_calls=14
fault_call='&framebuffer'
boot_line="dumb-buffer path (no zero-copy scanout)"
. "$(dirname "$0")/output_target_fault_case.sh"
