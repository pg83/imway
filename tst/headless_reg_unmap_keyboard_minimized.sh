#!/usr/bin/env bash
# The keyboard's window unmaps, or its toplevel is destroyed, while the
# window mapped after it sits minimized: the keyboard goes to the next
# window still on screen, and the minimized one never gets it, not even for
# an instant.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

enters() { grep -c "keyboard enter $1" "$CLIENT_LOG" || true; }
last_enter_is() { [[ "$(grep "keyboard enter" "$CLIENT_LOG" | tail -1)" == "keyboard enter $1" ]]; }

phase() { # <client mode> <what the middle window does>
    local from top_enters
    from=$(wc -l <"$IMWAY_LOG")

    start_client "$1"
    wait_client "windows mapped"
    await 100 last_enter_is top || { echo "$2: the last mapped window never got the keyboard"; cat "$CLIENT_LOG"; exit 1; }

    # minimizing the top window hands the keyboard to the middle one
    kill -USR1 "$CLIENT_PID"
    wait_client "minimize requested"
    await 100 last_enter_is middle || { echo "$2: the keyboard did not leave the minimized window"; cat "$CLIENT_LOG"; exit 1; }
    top_enters=$(enters top)

    # the middle one goes: the next window on screen is the bottom one
    kill -USR2 "$CLIENT_PID"
    wait_client "unmap requested"
    await 100 last_enter_is bottom || { echo "$2: the keyboard did not reach the bottom window"; cat "$CLIENT_LOG"; exit 1; }
    [[ "$(enters top)" == "$top_enters" ]] || { echo "$2: the minimized window got the keyboard"; cat "$CLIENT_LOG"; exit 1; }
    ! tail -n +$((from + 1)) "$IMWAY_LOG" | sed -n '/unmapkb-middle \(unmapped\|destroyed\)/,$p' | grep -q "focus -> unmapkb-top" ||
        { echo "$2: the compositor focused the minimized window"; cat "$IMWAY_LOG"; exit 1; }

    kill "$CLIENT_PID"
    wait "$CLIENT_PID" 2>/dev/null || true
    gone() { [[ -z "$(dump_field 'app_id=unmapkb-' id)" ]]; }
    await 100 gone || { echo "$2: the client's windows outlived it"; dump_state; exit 1; }
}

phase unmap "unmap"
phase destroy "toplevel destroyed"

expect_alive "compositor died handing the keyboard on"
echo "OK: an unmap or a destroyed toplevel hands the keyboard past a minimized window"
