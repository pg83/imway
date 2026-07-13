# shellcheck shell=bash
# Общие переменные/функции dev-VM. Подключается через `source`.

set -euo pipefail

VM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "$VM_DIR")"
STATE_DIR="$VM_DIR/.state"

IMAGE_URL="https://cloud.debian.org/images/cloud/trixie/latest/debian-13-genericcloud-arm64.qcow2"
BASE_IMG="$STATE_DIR/debian-13-base.qcow2"
DISK_IMG="$STATE_DIR/disk.qcow2"
DISK_SIZE="40G"
SEED_ISO="$STATE_DIR/seed.iso"
SSH_KEY="$STATE_DIR/id_ed25519"
FW_VARS="$STATE_DIR/efi-vars.fd"
PIDFILE="$STATE_DIR/qemu.pid"
SERIAL_LOG="$STATE_DIR/serial.log"

VM_CPUS="${VM_CPUS:-4}"
VM_MEM="${VM_MEM:-8192}"
SSH_PORT="${SSH_PORT:-2222}"
VM_USER=dev

SSH_OPTS=(
    -p "$SSH_PORT" -i "$SSH_KEY"
    -o StrictHostKeyChecking=no
    -o UserKnownHostsFile=/dev/null
    -o LogLevel=ERROR
    -o ConnectTimeout=5
)
SSH_OPTS_STR="-p $SSH_PORT -i $SSH_KEY -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5"

QEMU_BIN="${QEMU_BIN:-qemu-system-aarch64}"

fw_code() {
    local prefix
    prefix="$(dirname "$(dirname "$(command -v "$QEMU_BIN")")")"
    local fd="$prefix/share/qemu/edk2-aarch64-code.fd"
    [[ -f "$fd" ]] || { echo "edk2-aarch64-code.fd не найден рядом с qemu ($fd)" >&2; return 1; }
    echo "$fd"
}

vm_ssh() {
    ssh "${SSH_OPTS[@]}" "$VM_USER@127.0.0.1" "$@"
}

vm_running() {
    [[ -f "$PIDFILE" ]] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null
}

wait_ssh() {
    local tries="${1:-120}" # ~10 мин
    echo "жду ssh на 127.0.0.1:$SSH_PORT ..."
    for ((i = 0; i < tries; i++)); do
        if vm_ssh -o BatchMode=yes true 2>/dev/null; then
            echo "ssh готов"
            return 0
        fi
        sleep 5
    done
    echo "ssh так и не поднялся; смотри $SERIAL_LOG" >&2
    return 1
}
