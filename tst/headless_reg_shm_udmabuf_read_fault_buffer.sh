#!/usr/bin/env bash
# imway-env: IMWAY_SHM_BACKEND=udmabuf-buffer IMWAY_CHAOS=udmabuf-read=1 IMWAY_SHM_TRACE=1
# The pool's udmabuf is bound to a buffer, but the kernel refuses the
# CPU-access bracket that lets the GPU read it: both udmabuf paths close.
set -euo pipefail
. "$(dirname "$0")/lib.sh"
fault_log=""
gate_log="disabling wl_shm UDMABUF after sync failure"
natural_log=""
. "$(dirname "$0")/shm_import_fault_case.sh"
