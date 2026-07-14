#!/usr/bin/env bash
# Единая точка сборки. На Linux — собирает нативно.
# На macOS — синхронизирует исходники в dev-VM, собирает и гоняет тесты там.

set -euo pipefail
cd "$(dirname "$0")"

STD_DIR="${STD_DIR:-$(cd "$(dirname "$0")/../std" && pwd)}"

if [[ "$(uname)" == "Linux" ]]; then
    make -C "$STD_DIR" -j"$(nproc)" CXX=clang++ std/libstd.a
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSTD_ROOT="$STD_DIR"
    cmake --build build
    ctest --test-dir build --output-on-failure
    exit 0
fi

source vm/common.sh

vm_running || vm/run.sh
wait_ssh

echo "== rsync исходников =="
vm_ssh "mkdir -p imway/src"
rsync -az --delete \
    --exclude 'vm/.state' --exclude 'build/' --exclude '.claude/' \
    -e "ssh $SSH_OPTS_STR" \
    ./ "$VM_USER@127.0.0.1:imway/src/"
rsync -az --delete --exclude '.git' \
    -e "ssh $SSH_OPTS_STR" \
    "$STD_DIR/" "$VM_USER@127.0.0.1:std/"

echo "== сборка и тесты в VM =="
vm_ssh "set -e
    make -C std -j4 CXX=clang++ std/libstd.a
    cmake -S imway/src -B imway/build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DSTD_ROOT=/home/dev/std
    cmake --build imway/build
    ctest --test-dir imway/build --output-on-failure"
