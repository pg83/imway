#!/usr/bin/env bash
# Поднять всё для интерактива: VM с окном QEMU → imway на KMS → foot + mc.
# Мышь/клавиатура работают прямо в окне QEMU (клик по окну захватывает ввод).

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# VM должна быть с GUI: если крутится headless — перезапуск
if vm_running; then
    if ! ps -p "$(cat "$PIDFILE")" -o command= | grep -q 'display cocoa'; then
        echo "VM запущена headless — перезапускаю с GUI"
        "$VM_DIR/stop.sh"
    fi
fi
vm_running || "$VM_DIR/run.sh" --gui
wait_ssh

echo "== пересобираю и запускаю imway на KMS =="
"$REPO_DIR/build.sh" >/dev/null

# pkill -x, не -f: -f матчит и сам удалённый шелл (его cmdline содержит «imway»)
vm_ssh 'sudo pkill -x imway 2>/dev/null; sudo pkill -x foot 2>/dev/null; true'
vm_ssh 'set -e
export XDG_RUNTIME_DIR=/tmp/imway-run
sudo rm -rf $XDG_RUNTIME_DIR; mkdir -p $XDG_RUNTIME_DIR
# root: DRM master + /dev/input + tty без плясок с правами (dev-VM)
sudo -b env XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR \
    ./imway/build/imway --backend kms --socket imway-0 >/tmp/imway-gui.log 2>&1
for i in $(seq 50); do [ -S $XDG_RUNTIME_DIR/imway-0 ] && break; sleep 0.1; done
[ -S $XDG_RUNTIME_DIR/imway-0 ] || { cat /tmp/imway-gui.log; exit 1; }
sudo -b env XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WAYLAND_DISPLAY=imway-0 LANG=C.UTF-8 \
    foot >/tmp/foot1.log 2>&1
sudo -b env XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WAYLAND_DISPLAY=imway-0 LANG=C.UTF-8 \
    foot mc >/tmp/foot2.log 2>&1
sleep 1
tail -5 /tmp/imway-gui.log'

echo
echo "Готово: окно QEMU показывает imway (кликни в окно, чтобы захватить мышь;"
echo "отпустить — Ctrl+Alt+G). Логи в VM: /tmp/imway-gui.log"
echo "Остановить: vm/ssh.sh 'sudo pkill -x imway'"
