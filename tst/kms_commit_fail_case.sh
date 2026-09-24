# A page flip the driver refuses while no hardware cursor rides the commit
# (the device has no cursor plane, or the hardware cursor is off): there is
# no cursor to try the commit without, so the refusal is reported as it
# came, and the next frame flips. Sourced by headless_kms_commit_fail_*.sh.
. "$(dirname "$0")/lib.sh"

in_log "kms output" || { echo "no kms boot"; cat "$IMWAY_LOG"; exit 1; }

flips() { dump_field '^kms' flips; }

ctl "motion 200 200"
compose_frame
ctl "kms-fail-commit 22 1"
ctl "motion 220 220"
await 100 in_log "kms atomic commit failed, errno 22" || { echo "the refused flip was not reported"; cat "$IMWAY_LOG"; exit 1; }
! in_log "cursor plane rejected by this mode" || { echo "a commit with no cursor on it was retried without one"; cat "$IMWAY_LOG"; exit 1; }

f0=$(flips)
advanced() { [[ "$(flips)" -gt "$f0" ]]; }
for i in $(seq 1 20); do
    ctl "motion $((240 + i * 3)) 240"
    advanced && break
    sleep 0.05
done
advanced || { echo "flips stopped after the refused one"; exit 1; }

expect_alive "compositor died on a refused flip without a hardware cursor"
echo "OK: a refused flip with no hardware cursor on it is reported and the next one goes through"
