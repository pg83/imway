#!/usr/bin/env bash
# Start the VM (daemonized). --gui adds a window with virtio-gpu (for the KMS ring),
# headless by default: ssh only (127.0.0.1:$SSH_PORT).

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

if vm_running; then
    echo "VM is already running (pid $(cat "$PIDFILE"))"
    exit 0
fi

[[ -f "$DISK_IMG" ]] || { echo "no disk, run vm/create.sh first" >&2; exit 1; }

args=(
    -machine virt,accel=hvf
    -cpu host -smp "$VM_CPUS" -m "$VM_MEM"
    -drive if=pflash,format=raw,readonly=on,file="$(fw_code)"
    -drive if=pflash,format=raw,file="$FW_VARS"
    -drive if=virtio,format=qcow2,file="$DISK_IMG"
    -device virtio-net-pci,netdev=n0
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22"
    -device virtio-rng-pci
    -serial "file:$SERIAL_LOG"
    -pidfile "$PIDFILE"
)

# always attach the seed: instance-id stays the same, so cloud-init does not re-run
[[ -f "$SEED_ISO" ]] && args+=(-drive if=virtio,format=raw,readonly=on,file="$SEED_ISO")

# virtio-gpu is always present (the KMS backend is tested headless too), --gui adds a window
args+=(-device virtio-gpu-pci -device qemu-xhci -device usb-kbd -device usb-tablet)
rm -f "$PIDFILE"
if [[ "${1:-}" == "--gui" ]]; then
    # cocoa cannot be combined with -daemonize (fork after ObjC init = crash),
    # so background it by hand; qemu writes -pidfile even without -daemonize
    args+=(-display cocoa)
    "$QEMU_BIN" "${args[@]}" &
    disown
else
    args+=(-display none -daemonize)
    "$QEMU_BIN" "${args[@]}"
fi
for _ in $(seq 50); do [[ -s "$PIDFILE" ]] && break; sleep 0.1; done
[[ -s "$PIDFILE" ]] || { echo "qemu failed to start" >&2; exit 1; }
echo "VM started (pid $(cat "$PIDFILE")), ssh: vm/ssh.sh, serial: $SERIAL_LOG"
