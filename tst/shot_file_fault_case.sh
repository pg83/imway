# The screenshot's file cannot be built (IMWAY_CHAOS=shot-file=K: the
# memfd, its header or its first chunk of pixels fails, as fd exhaustion
# or a memory-limited memfd would make it): the capture is dropped with a
# log line and no viewer, and the capture is free again, so the next Print
# saves as usual. Sourced by headless_reg_shot_file_fault_*.sh.
. "$(dirname "$0")/lib.sh"

shots="$XDG_RUNTIME_DIR/shots"
ctl "set applications.screenshot_directory $shots"
ctl "set applications.screenshot_format 1" # png
ctl "set applications.screenshot_action 1" # save, no window
await 20 in_log "control: set applications.screenshot_action" || { echo "settings are not reachable"; exit 1; }

ctl "set applications.screenshot_name lost"
ctl "key 99 press"; ctl "key 99 release" # Print
await 100 in_log "imway: screenshot readback failed" || { echo "the unbuildable file was not reported"; cat "$IMWAY_LOG"; exit 1; }
! in_log "imway: spawned " || { echo "a viewer was spawned for a file that was never built"; cat "$IMWAY_LOG"; exit 1; }

ctl "set applications.screenshot_name kept"
ctl "key 99 press"; ctl "key 99 release"
await 200 test -s "$shots/kept.png" || { echo "the capture stayed busy after the failed file"; cat "$IMWAY_LOG"; exit 1; }
[[ ! -e "$shots/lost.png" ]] || { echo "the failed file still produced a screenshot"; exit 1; }

expect_alive "compositor died on a screenshot file it could not build"
echo "OK: a screenshot file that cannot be built is dropped and the next one saves"
