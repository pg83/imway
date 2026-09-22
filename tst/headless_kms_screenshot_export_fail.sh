#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS=1 IMWAY_CHAOS=scanout=15
# imway-args: --device auto
# The replacement is built, but the presented scanout buffer cannot be
# exported as a dma-buf for the viewer (the call after the boot's ten and
# the replacement's five): the spare is dropped and the capture reads the
# frame back instead.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
. "$(dirname "$0")/kms_screenshot_fault_case.sh"
