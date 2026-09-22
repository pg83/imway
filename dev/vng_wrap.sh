#!/usr/bin/env bash
# Runs its arguments inside a throwaway virtme-ng VM booted on the host's own
# kernel, with a virtio-gpu attached: a DRM device with timeline syncobjs and
# a render node, which a CI runner does not have. The host filesystem is
# shared read-write, so the command sees the build and writes its results
# where the host expects them. For build.py's -Dtest_wrap.
#
# needs: vng (virtme-ng), qemu-system-x86, /dev/kvm, the virtio-gpu module
# (linux-modules-extra on ubuntu's cloud kernels), a readable /boot/vmlinuz
set -euo pipefail

printf -v command '%q ' "$@"
printf -v dir '%q' "$PWD"

setup='modprobe virtio_gpu; modprobe udmabuf 2>/dev/null; chmod 666 /dev/dri/* /dev/udmabuf 2>/dev/null; export HOME=/tmp'

exec vng -r "/boot/vmlinuz-$(uname -r)" --rw --memory "${VNG_MEMORY:-2G}" --cpus "${VNG_CPUS:-2}" \
    --qemu-opts="-device virtio-gpu-pci" -- "$setup; cd $dir && $command"
