#!/usr/bin/env bash
# Post-process an llvm source-based coverage test run into .coverage/:
# merged profdata, an lcov export for Codecov, a text summary and a browsable
# HTML report. Mirrors what the imway_test binary and LLVM_PROFILE_FILE
# (%b-%16m: keyed by build id) produced during `./build test`.
#
# usage: ci_coverage.sh <build-dir> <profile-dir>
set -euo pipefail

build_dir=$1
profile_dir=$2
llvm=${LLVM:-21}

profdata="llvm-profdata-$llvm"
cov="llvm-cov-$llvm"
readelf="llvm-readelf-$llvm"
command -v "$profdata" >/dev/null || { profdata=llvm-profdata; cov=llvm-cov; readelf=llvm-readelf; }

binary="$build_dir/imway_test"
ignore='(^|/)(tst|ext/libstd|\.build[^/]*)/'
out=.coverage

mkdir -p "$out/html"

# The profile runtime's %b naming is authoritative and can disagree with
# what readelf prints; ask the binary itself by flushing one probe profile.
probe_dir=$(mktemp -d)
LLVM_PROFILE_FILE="$probe_dir/%b.profraw" "$binary" screenshot /nonexistent >/dev/null 2>&1 || true
probe=("$probe_dir"/*.profraw)
[[ -e "${probe[0]}" ]] || { echo "no probe profile from $binary" >&2; exit 1; }
build_id=$(basename "${probe[0]}" .profraw)
rm -rf "$probe_dir"
echo "coverage binary runtime id: $build_id"

profiles=("$profile_dir/$build_id"-*.profraw)
[[ -e "${profiles[0]}" ]] || {
    echo "coverage binary produced no profiles: $binary (runtime id $build_id)" >&2
    echo "profiles total: $(ls "$profile_dir" | wc -l), distinct ids: $(ls "$profile_dir" | cut -d- -f1 | sort -u | wc -l)" >&2
    # replicate the supervisor->composer chain with a frames-limited
    # self-exit: does THAT flush a profile?
    rtdir=$(mktemp -d /tmp/iwprobe.XXXXXX)
    chmod 700 "$rtdir"
    env XDG_RUNTIME_DIR="$rtdir" LLVM_PROFILE_FILE="$rtdir/sup-%b.profraw" \
        timeout 60 "$binary" --device headless --socket iw-covprobe --frames 5 \
        >"$rtdir/log" 2>&1 || true
    echo "supervisor-mode probe profiles: $(ls "$rtdir" | grep -c '^sup-')" >&2
    tail -3 "$rtdir/log" >&2
    # and the test runner's actual teardown: SIGTERM to the process group
    env XDG_RUNTIME_DIR="$rtdir" LLVM_PROFILE_FILE="$rtdir/sig-%b.profraw" \
        "$binary" --device headless --socket iw-sigprobe >"$rtdir/siglog" 2>&1 &
    spid=$!
    sleep 10
    kill -TERM "-$spid" 2>/dev/null || kill -TERM "$spid" 2>/dev/null || true
    wait "$spid" 2>/dev/null || true
    echo "sigterm probe profiles: $(ls "$rtdir" | grep -c '^sig-')" >&2
    tail -3 "$rtdir/siglog" >&2
    # the whole real harness: one run_test invocation end to end, with the
    # graph node's deep TMPDIR
    work="$PWD/$build_dir/tmp/probe0000000000000000000000000000"
    mkdir -p "$work"
    before=$(ls "$profile_dir" | grep -c "^$build_id" || true)
    env TMPDIR="$work" python3 dev/run_test.py --scenario "$PWD/tst/headless_shm.sh" \
        --imway "$PWD/$binary" --client "$PWD/$build_dir/tests/client_shm" \
        --out "$rtdir/shm.json" || true
    after=$(ls "$profile_dir" | grep -c "^$build_id" || true)
    echo "run_test probe: composer profiles before=$before after=$after" >&2
    python3 -c "import json; r = json.load(open('$rtdir/shm.json')); print('run_test probe verdict:', r['status'], r.get('detail', ''))" >&2 || true
    # and through the graph runner itself: runs=2 makes run1 a fresh node
    before=$(ls "$profile_dir" | grep -c "^$build_id" || true)
    python3 ./build -B "$build_dir" -j 1 -k -Druns=2 -Dfilter='headless_shm' test >&2 || true
    after=$(ls "$profile_dir" | grep -c "^$build_id" || true)
    echo "graph-node probe: composer profiles before=$before after=$after" >&2
    # per-pid naming, no merge pool: separates a lost env from merge
    # mechanics from a process that never reaches the write
    env LLVM_PROFILE_FILE="$profile_dir/px-%b-%p.profraw" \
        python3 ./build -B "$build_dir" -j 1 -k -Druns=3 -Dfilter='headless_shm' test >&2 || true
    echo "pid-profile probe: total $(ls "$profile_dir" | grep -c '^px-'), composer $(ls "$profile_dir" | grep -c "^px-$build_id")" >&2
    exit 1
}

"$profdata" merge -sparse "${profiles[@]}" -o "$out/coverage.profdata"
"$cov" export "$binary" -instr-profile="$out/coverage.profdata" -format=lcov \
    -ignore-filename-regex="$ignore" > "$out/coverage.info"
"$cov" report "$binary" -instr-profile="$out/coverage.profdata" \
    -ignore-filename-regex="$ignore" > "$out/summary.txt"
"$cov" show "$binary" -instr-profile="$out/coverage.profdata" -format=html \
    -output-dir="$out/html" -show-branches=percent -coverage-watermark=80,50 \
    -ignore-filename-regex="$ignore"

sed -i "s|^SF:$PWD/|SF:|" "$out/coverage.info"
if grep -q '^SF:/' "$out/coverage.info"; then
    echo "coverage report contains absolute source paths" >&2
    grep '^SF:/' "$out/coverage.info" | head -10 >&2
    exit 1
fi
grep -q '^SF:' "$out/coverage.info" || { echo "coverage report contains no source files" >&2; exit 1; }

cat "$out/summary.txt"
