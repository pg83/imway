#!/usr/bin/env bash
# imway-env: IMWAY_CHAOS=sync-wait=0
# Explicit sync, acquire point signaled before the commit. The point's sync file cannot be imported into the frame's semaphore.
# Needs timeline syncobjs: a real GPU, or the virtio-gpu of dev/vng_wrap.sh.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log="acquire point unavailable, sampling unsynchronized"
. "$(dirname "$0")/syncobj_signaled_case.sh"
