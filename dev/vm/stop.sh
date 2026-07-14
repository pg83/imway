#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

vm_running || { echo "VM is not running"; exit 0; }

# gracefully via ssh, then finish it off
vm_ssh "sudo poweroff" 2>/dev/null || true
for _ in $(seq 1 20); do
    vm_running || { echo "VM stopped"; exit 0; }
    sleep 1
done
kill "$(cat "$PIDFILE")" 2>/dev/null || true
echo "VM killed"
