#!/usr/bin/env bash
# expect-compositor-exit
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHAOS="scanout=10 output-target=13"
# imway-args: --device auto
# The display is swapped for one with other modes and the scanout rebuild
# at its size fails (scanout=10), so the output rebuilds its swapchain at
# the old size: new buffers the renderer's output targets have to follow
# with a view and a framebuffer each, at the same size. The first of those
# views fails; the session ends, and the teardown must not trip over the
# framebuffer the renderer had already destroyed for the old buffer.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
boot_calls=13
boot_line="scanout swapchain: 2 images"
fault_call='&scanViews.mut(i)'
. "$(dirname "$0")/output_target_fault_case.sh"
