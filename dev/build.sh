#!/usr/bin/env bash
# Single entry point for builds. On Linux — builds natively.
# On macOS — syncs sources into the dev VM and builds there.

set -euo pipefail
cd "$(dirname "$0")/.."

if [[ "$(uname)" == "Linux" ]]; then
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    cmake --build build
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
echo "== build in the VM =="
vm_ssh "set -e
    cmake -S imway/src -B imway/build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    cmake --build imway/build"
