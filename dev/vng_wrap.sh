#!/usr/bin/env bash
# Runs its arguments inside a throwaway virtme-ng VM booted on the host's own
# kernel, with a virtio-gpu attached: a DRM device with timeline syncobjs and
# a render node, which a CI runner does not have. The host filesystem is
# shared read-write, so the command sees the build and writes its results
# where the host expects them. For build.py's -Dtest_wrap.
#
# needs: vng (virtme-ng), qemu-system-x86, /dev/kvm, the virtio-gpu module
# (linux-modules-extra on ubuntu's cloud kernels), a readable /boot/vmlinuz.
# blob resources let virtio-gpu import dma-bufs it did not export (udmabuf
# test buffers); VNG_GPU overrides the device
set -euo pipefail

printf -v command '%q ' "$@"
printf -v dir '%q' "$PWD"

setup='modprobe virtio_gpu; modprobe udmabuf 2>/dev/null; chmod 666 /dev/dri/* /dev/udmabuf 2>/dev/null; export HOME=/tmp'

# the VM starts from a clean environment: carry over what a run needs from
# the host's, a coverage build's profile destination above all
for name in LLVM_PROFILE_FILE; do
    if [[ -n "${!name:-}" ]]; then
        printf -v value '%q' "${!name}"
        setup+="; export $name=$value"
    fi
done

# blob resources want the guest's memory in a memfd qemu can hand to udmabuf
memory=${VNG_MEMORY:-2G}

# a VM that never powers off would hold its build node forever: the
# scenario's own timeout lives inside the guest and cannot reach it
exec timeout --kill-after=15 "${VNG_TIMEOUT:-300}" vng -r "/boot/vmlinuz-$(uname -r)" --rw --memory "$memory" --cpus "${VNG_CPUS:-2}" \
    --qemu-opts="-object memory-backend-memfd,id=imway-mem,size=$memory,share=on -machine memory-backend=imway-mem -device ${VNG_GPU:-virtio-gpu-pci,blob=true}" \
    -- "$setup; cd $dir && $command"
