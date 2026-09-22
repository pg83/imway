# Locks the session, has a password refused, then unlocks with the test
# build's own password: the refusal must leave the field working.
ctl "key 125 press"; ctl "key 38 press"; ctl "key 38 release"; ctl "key 125 release" # Super+L
await_imgui '##lock-overlay' || { echo "the session did not lock"; dump_state; exit 1; }
await_typing '##lock-overlay' || { echo "the field never took the keyboard"; dump_state; exit 1; }

ctl "type nope"
sleep 0.4 # let ImGui's trickle queue consume the text before Enter
ctl "key 28 press"; ctl "key 28 release"

await 300 in_log "lockscreen rejected" || {
    echo "the broken authentication never came back"
    cat "$IMWAY_LOG"
    exit 1
}

! in_log "lockscreen accepted" || { echo "a broken authentication let the session in"; exit 1; }

await_typing '##lock-overlay' || { echo "the field did not come back"; dump_state; exit 1; }

for _ in 1 2 3; do
    ctl "key 45 press"; ctl "key 45 release" # KEY_X
    sleep 0.2
done

sleep 0.5
ctl "key 28 press"; ctl "key 28 release"
await 200 in_log "lockscreen closed" || { echo "xxx did not unlock"; cat "$IMWAY_LOG"; exit 1; }

expect_alive "compositor died over a broken authentication"
echo "OK: a broken authentication is a refusal and the lock screen recovers"
