#!/usr/bin/env bash
# Запустить VM (демоном). --gui добавляет окно с virtio-gpu (для KMS-кольца),
# по умолчанию headless: только ssh (127.0.0.1:$SSH_PORT).

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

if vm_running; then
    echo "VM уже запущена (pid $(cat "$PIDFILE"))"
    exit 0
fi

[[ -f "$DISK_IMG" ]] || { echo "нет диска, сначала vm/create.sh" >&2; exit 1; }

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

# seed прикладываем всегда: instance-id не меняется, cloud-init повторно не бежит
[[ -f "$SEED_ISO" ]] && args+=(-drive if=virtio,format=raw,readonly=on,file="$SEED_ISO")

# virtio-gpu есть всегда (KMS-бэкенд тестируется и headless), --gui добавляет окно
args+=(-device virtio-gpu-pci -device qemu-xhci -device usb-kbd -device usb-tablet)
rm -f "$PIDFILE"
if [[ "${1:-}" == "--gui" ]]; then
    # cocoa нельзя совмещать с -daemonize (fork после инициализации ObjC = креш),
    # поэтому руками в фон; -pidfile qemu пишет и без -daemonize
    args+=(-display cocoa)
    "$QEMU_BIN" "${args[@]}" &
    disown
else
    args+=(-display none -daemonize)
    "$QEMU_BIN" "${args[@]}"
fi
for _ in $(seq 50); do [[ -s "$PIDFILE" ]] && break; sleep 0.1; done
[[ -s "$PIDFILE" ]] || { echo "qemu не стартанул" >&2; exit 1; }
echo "VM запущена (pid $(cat "$PIDFILE")), ssh: vm/ssh.sh, serial: $SERIAL_LOG"
