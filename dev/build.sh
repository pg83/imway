#!/usr/bin/env bash
# Single entry point for builds. On Linux — builds natively.
# On macOS — syncs sources into the dev VM, builds and runs the tests there.

set -euo pipefail
cd "$(dirname "$0")/.."

STD_DIR="${STD_DIR:-$(cd ../std && pwd)}"

if [[ "$(uname)" == "Linux" ]]; then
    make -C "$STD_DIR" -j"$(nproc)" CXX=clang++ std/libstd.a
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTD_ROOT="$STD_DIR" \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    cmake --build build
    ctest --test-dir build --output-on-failure
    exit 0
fi

source dev/vm/common.sh

vm_running || dev/vm/run.sh
wait_ssh

echo "== rsync sources =="
vm_ssh "mkdir -p imway/src"
rsync -az --delete \
    --exclude 'dev/vm/.state' --exclude 'build/' --exclude '.claude/' \
    -e "ssh $SSH_OPTS_STR" \
    ./ "$VM_USER@127.0.0.1:imway/src/"
rsync -az --delete --exclude '.git' \
    -e "ssh $SSH_OPTS_STR" \
    "$STD_DIR/" "$VM_USER@127.0.0.1:std/"

echo "== build and tests in the VM =="
vm_ssh "set -e
    make -C std -j4 CXX=clang++ std/libstd.a
    cmake -S imway/src -B imway/build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DSTD_ROOT=/home/dev/std
    cmake --build imway/build
    ctest --test-dir imway/build --output-on-failure"
