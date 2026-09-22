#!/usr/bin/env bash
# Explicit-sync points through a sync subsurface's cache: a cached dmabuf
# replaced before the parent commit has its release point signaled, the
# replacement applies with its points, and after set_desync a commit
# applies its points directly and releases the buffer it replaces.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

rc=0
"$IMWAY_CLIENT" || rc=$?

if [[ $rc -eq 77 ]]; then
    echo "SKIP: explicit sync unavailable"
    exit 127
fi

[[ $rc -eq 0 ]] || { echo "syncobj cached client failed: $rc"; exit "$rc"; }
expect_alive "compositor died on cached explicit-sync commits"
echo "OK: sync points follow their buffers through the subsurface cache"
