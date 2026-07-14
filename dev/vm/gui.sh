#!/usr/bin/env bash
# Bring up everything for interactive use: VM with a QEMU window → imway on KMS → foot + mc.
# Mouse/keyboard work right in the QEMU window (clicking the window grabs input).

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# The VM must have a GUI: if it's running headless — restart it
if vm_running; then
    if ! ps -p "$(cat "$PIDFILE")" -o command= | grep -q 'display cocoa'; then
        echo "VM is running headless — restarting with GUI"
        "$VM_DIR/stop.sh"
    fi
fi
vm_running || "$VM_DIR/run.sh" --gui
wait_ssh

echo "== rebuilding and starting imway on KMS =="
"$REPO_DIR/build.sh" >/dev/null

# pkill -x, not -f: -f also matches the remote shell itself (its cmdline contains "imway")
vm_ssh 'sudo pkill -x imway 2>/dev/null; sudo pkill -x foot 2>/dev/null; true'
vm_ssh 'set -e
export XDG_RUNTIME_DIR=/tmp/imway-run
sudo rm -rf $XDG_RUNTIME_DIR; mkdir -p $XDG_RUNTIME_DIR
# root: DRM master + /dev/input + tty without permission gymnastics (dev VM)
sudo -b env XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR \
    ./imway/build/imway --device auto --socket imway-0 >/tmp/imway-gui.log 2>&1
for i in $(seq 50); do [ -S $XDG_RUNTIME_DIR/imway-0 ] && break; sleep 0.1; done
[ -S $XDG_RUNTIME_DIR/imway-0 ] || { cat /tmp/imway-gui.log; exit 1; }
sudo -b env XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WAYLAND_DISPLAY=imway-0 LANG=C.UTF-8 \
    foot >/tmp/foot1.log 2>&1
sudo -b env XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WAYLAND_DISPLAY=imway-0 LANG=C.UTF-8 \
    foot mc >/tmp/foot2.log 2>&1
sleep 1
tail -5 /tmp/imway-gui.log'

echo
echo "Done: the QEMU window is showing imway (click the window to grab the mouse;"
echo "release with Ctrl+Alt+G). Logs in the VM: /tmp/imway-gui.log"
echo "To stop: vm/ssh.sh 'sudo pkill -x imway'"
