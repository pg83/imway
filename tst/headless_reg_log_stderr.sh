#!/usr/bin/env bash
# The log's stderr copy loses lines rather than the compositor: a second
# compositor refusing a bad command line still exits with the usage code
# when its stderr is a pipe nobody reads any more (every write fails with
# EPIPE, and SIGPIPE must not kill it) and when it has no stderr at all
# (nothing to reopen, so the log falls back to fd 2 itself).
set -euo pipefail
. "$(dirname "$0")/lib.sh"

bin="$(dirname "$IMWAY_TESTS_BIN")/imway_test"

# python restores SIGPIPE's default disposition in the child, as a shell
# would hand it over
rc=0
python3 - "$bin" <<'PY' || rc=$?
import os, subprocess, sys
r, w = os.pipe()
os.close(r)
p = subprocess.run([sys.argv[1], "--scale", "0"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=w, timeout=60)
sys.exit(p.returncode if p.returncode >= 0 else 128 - p.returncode)
PY
[[ "$rc" == 2 ]] || { echo "with a dead stderr reader the refusal exited $rc, not 2"; exit 1; }

rc=0
timeout 60 "$bin" --scale 0 </dev/null >/dev/null 2>&- || rc=$?
[[ "$rc" == 2 ]] || { echo "with stderr closed the refusal exited $rc, not 2"; exit 1; }

expect_alive "the scenario's own compositor died"
echo "OK: a lost or missing stderr costs log lines, not the process"
