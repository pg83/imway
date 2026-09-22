#!/usr/bin/env bash
# Explicit sync, acquire point signaled before the commit. The frame waits on the point's fence and draws the buffer.
# Needs timeline syncobjs: a real GPU, or the virtio-gpu of dev/vng_wrap.sh.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log=""
. "$(dirname "$0")/syncobj_signaled_case.sh"
