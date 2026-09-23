#!/usr/bin/env bash
# A wayland server that cannot be set up at boot: no display, a global the
# library cannot make (wl_shm, made on its own; xdg_wm_base, one of the
# rest), or no linux-dmabuf format table (its memfd, or an entry's write).
# Each refuses the session with exit code 1, the failed check on record,
# never a signal, and leaves no socket behind.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

imway_bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"

refused() { # <chaos> <failed check>
    local rc=0 out

    out=$(IMWAY_CHAOS="$1" timeout 60 "$imway_bin" --device headless --socket imway-refused --frames 3 2>&1) || rc=$?
    [[ "$rc" -eq 1 ]] || { echo "$1: exit $rc, expected 1"; echo "$out"; exit 1; }
    grep "imway: fatal" <<<"$out" | grep -qF "verify failed: $2" || { echo "$1: the refusal does not name $2"; echo "$out"; exit 1; }
    ! grep -q "imway: socket imway-refused\|clean exit after" <<<"$out" || { echo "$1: the boot went on"; echo "$out"; exit 1; }
    [[ ! -e "$XDG_RUNTIME_DIR/imway-refused" ]] || { echo "$1: the socket outlived the refusal"; exit 1; }
}

refused display=1 "display"
refused global=wl_shm "initWaylandShm(display, composer)"
refused global=xdg_wm_base "chaos.global(wl_global_create(display, iface, version, data, bind)"
refused format-table=0 "fbTableFd >= 0"
refused format-table=1 "composer->chaos->formatTableWrite(write(fbTableFd, &entry, sizeof(entry))) == sizeof(entry)"

expect_alive "the scenario's own compositor died"
echo "OK: a wayland server that cannot be set up refuses the boot cleanly"
