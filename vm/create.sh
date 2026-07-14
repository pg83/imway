#!/usr/bin/env bash
# Create and provision the dev VM from scratch: download the image, cloud-init, first boot.
# Idempotent: leaves an already-provisioned VM alone, --fresh recreates the disk.

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

if [[ "${1:-}" == "--fresh" ]]; then
    "$VM_DIR/stop.sh" || true
    rm -f "$DISK_IMG" "$SEED_ISO" "$FW_VARS"
fi

mkdir -p "$STATE_DIR"

command -v "$QEMU_BIN" >/dev/null || { echo "$QEMU_BIN not found: brew install qemu" >&2; exit 1; }

# 1. Base image
if [[ ! -f "$BASE_IMG" ]]; then
    echo "downloading $IMAGE_URL"
    curl -fL -C - -o "$BASE_IMG.part" "$IMAGE_URL"
    mv "$BASE_IMG.part" "$BASE_IMG"
fi

# 2. Working disk (overlay on top of the base image)
if [[ ! -f "$DISK_IMG" ]]; then
    qemu-img create -f qcow2 -b "$BASE_IMG" -F qcow2 "$DISK_IMG" "$DISK_SIZE"
fi

# 3. ssh key
[[ -f "$SSH_KEY" ]] || ssh-keygen -t ed25519 -N '' -C imway-dev-vm -f "$SSH_KEY"

# 4. cloud-init seed (NoCloud: iso labeled cidata)
if [[ ! -f "$SEED_ISO" ]]; then
    seed_dir="$(mktemp -d)"
    trap 'rm -rf "$seed_dir"' EXIT
    pubkey="$(cat "$SSH_KEY.pub")"
    sed "s|@SSH_PUBKEY@|$pubkey|" "$VM_DIR/cloud-init/user-data" >"$seed_dir/user-data"
    cat >"$seed_dir/meta-data" <<EOF
instance-id: imway-dev-1
local-hostname: imway-dev
EOF
    hdiutil makehybrid -iso -joliet -default-volume-name cidata \
        -o "$SEED_ISO" "$seed_dir" >/dev/null
fi

# 5. EFI vars
[[ -f "$FW_VARS" ]] || dd if=/dev/zero of="$FW_VARS" bs=1m count=64 2>/dev/null

# 6. First boot + wait for provisioning
vm_running || "$VM_DIR/run.sh"
wait_ssh
echo "waiting for cloud-init (package installation, takes minutes on first run)..."
vm_ssh "cloud-init status --wait" || {
    echo "cloud-init failed; log:" >&2
    vm_ssh "sudo tail -50 /var/log/cloud-init-output.log" >&2 || true
    exit 1
}

echo
echo "VM is ready. Next steps:"
echo "  ./build.sh      — build and run the tests in the VM"
echo "  vm/ssh.sh       — get a shell inside"
echo "  vm/stop.sh      — shut it down"
