#!/usr/bin/env bash
# Post-process an llvm source-based coverage test run into .coverage/:
# merged profdata, an lcov export for Codecov, a text summary and a browsable
# HTML report.
#
# Every instrumented process (the compositor and each test client) drops a
# profile into the profile dir. Binary-id file naming proved unstable across
# link/exec flavors, so merge everything: llvm-cov maps records through the
# function hashes present in imway_test and simply ignores the rest.
#
# usage: ci_coverage.sh <build-dir> <profile-dir>
set -euo pipefail

build_dir=$1
profile_dir=$2
llvm=${LLVM:-21}

profdata="llvm-profdata-$llvm"
cov="llvm-cov-$llvm"
command -v "$profdata" >/dev/null || { profdata=llvm-profdata; cov=llvm-cov; }

binary="$build_dir/imway_test"
# our test sources, the vendored stdlib, generated files in any build dir
# view, and system headers carry no coverage of interest
ignore='(^|/)(tst|ext/libstd|\.b[^/]*)/|^/usr/'
out=.coverage

mkdir -p "$out/html"

profiles=("$profile_dir"/*.profraw)
[[ -e "${profiles[0]}" ]] || { echo "no profiles in $profile_dir" >&2; exit 1; }
echo "merging ${#profiles[@]} profiles"

# clients killed mid-write leave truncated profiles behind; skip those
# and merge the rest
"$profdata" merge -sparse -failure-mode=warn "${profiles[@]}" -o "$out/coverage.profdata"
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

# an empty compositor profile would still produce a report of zeros; a
# floor on total line coverage keeps the report honest
total=$(awk '/^TOTAL/ { gsub("%", "", $10); print int($10) }' "$out/summary.txt")
[[ -n "$total" && "$total" -ge 20 ]] || {
    echo "total line coverage suspiciously low: ${total:-?}%" >&2
    tail -3 "$out/summary.txt" >&2
    exit 1
}

cat "$out/summary.txt"
