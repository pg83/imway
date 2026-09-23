#!/usr/bin/env bash
# The control FIFO's own edges, each answered in the log and survived: an
# unknown verb, an unknown setting, a line longer than the reader's buffer
# (cut at 1023 bytes, the rest dropped until the newline), a dump whose
# final path is a directory (the temporary file cannot be renamed onto it),
# the fake-KMS verbs without the fake device, input verbs short of their
# arguments, gesture phases that do not exist, an empty line, malformed
# rule and notify lines, a rule app name longer than a rule holds (cut to
# it), "type" text with characters no key produces, which are skipped:
# the launcher still finds settings from the letters around them, and a
# character only a shifted key produces, typed with Shift.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

ctl "no-such-verb 1 2"
await 50 in_log "imway: unknown command: no-such-verb 1 2" || { echo "an unknown verb was not reported"; cat "$IMWAY_LOG"; exit 1; }

ctl "set no.such.setting 1"
await 50 in_log "imway: control: unknown setting no.such.setting" || { echo "an unknown setting was not reported"; exit 1; }

long=$(printf 'x%.0s' $(seq 1100))
ctl "$long"
cut_line() {
    local n
    n=$(grep -o "imway: unknown command: x*" "$IMWAY_LOG" | awk '{ print length($4) }' | tail -1)
    [[ "$n" == 1023 ]]
}
await 50 cut_line || { echo "an overlong line was not cut at 1023 bytes"; exit 1; }

mkdir -p "$XDG_RUNTIME_DIR/dumpdir/inside"
ctl "dump $XDG_RUNTIME_DIR/dumpdir"
await 50 in_log "imway: dump: cannot rename" || { echo "a dump onto a directory was not reported"; cat "$IMWAY_LOG"; exit 1; }
[[ ! -e "$XDG_RUNTIME_DIR/dumpdir.tmp" ]] || { echo "the failed dump left its temporary file"; exit 1; }

# the fake-KMS verbs on a compositor without the fake device are verbs it
# does not have
for v in kms-connector kms-fail-commit kms-fail-new-fb kms-fail-prime kms-fail-addfb kms-reject-cursor kms-modes kms-lease-fault kms-fail-lookup; do
    ctl "$v 1 1"
    await 50 in_log "imway: unknown command: $v 1 1" || { echo "$v on a headless compositor was not refused"; exit 1; }
done

# input verbs short of their arguments, phases a gesture does not have, an
# empty line and malformed rule and notify lines do nothing; the sentinel
# after them shows each was read and survived
ctl "motion"
ctl "button left"
ctl "key 30"
ctl "relmotion 5"
ctl "swipe update 5"
ctl "swipe bogus"
ctl "pinch bogus"
ctl "hold bogus"
ctl ""
ctl "rule 0"
ctl "rule 0 1"
ctl "notify lonely 0"
ctl "notify lonely 0 1"
ctl "tablet motion 5"
ctl "rule 99 1 far-past-the-rule-slots"
ctl "notify lonely"
ctl "set notifications.timeout 7"
await 50 in_log "control: set notifications.timeout" || { echo "the malformed lines stopped the FIFO"; exit 1; }
! in_log "control: rule 99" || { echo "a rule past the last slot was taken"; exit 1; }
! in_log "control: notification" || { echo "a malformed notify posted"; exit 1; }
[[ "$(dump_field '^notifications ' history)" == 0 ]] || { echo "a malformed notify reached the notifier"; exit 1; }

# a rule for slot 0 while more slots are in use keeps the count; an app
# name longer than a rule holds is cut to it, and the cut name is the rule
ctl "set notifications.rule_count 2"
long=$(printf 'm%.0s' $(seq 150))
ctl "rule 0 2 $long"
await 50 in_log "control: rule 0" || { echo "the long rule was not taken"; exit 1; }
ctl "notify ${long:0:127} 0 0 cut"
await 50 in_log "control: notification" || { echo "the notification was not taken"; exit 1; }
[[ "$(dump_field '^notifications ' active)" == 0 ]] || { echo "the rule under the cut name did not mute it"; exit 1; }

ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "the launcher did not open"; exit 1; }
ctl "type sett§ings"
ctl "key 103 press"; ctl "key 103 release"
ctl "key 28 press"; ctl "key 28 release"
await_imgui settings || { echo "the text with an untypeable character did not reach the launcher"; dump_state; exit 1; }

# a character only a shifted key produces: typed as that key with Shift,
# it lands in the launcher's field and in the command Enter runs
ctl "key 125 press"; ctl "key 60 press"; ctl "key 60 release"; ctl "key 125 release" # Super+F2
await_typing '##launcher' || { echo "the launcher did not open again"; exit 1; }
ctl "type touch sh!ft.out"
field_holds() { [[ "$(dump_state | grep '^imgui input ' | sed 's/.* text=//')" == "touch sh!ft.out" ]]; }
await 100 field_holds || { echo "the shifted character did not reach the field: $(dump_state | grep '^imgui input ')"; exit 1; }
ctl "key 28 press"; ctl "key 28 release"
await 100 test -e 'sh!ft.out' || { echo "the command with the shifted character did not run"; exit 1; }

expect_alive "compositor died on the control FIFO's edges"
echo "OK: unknown verbs and settings, an overlong line, a failed dump rename and untypeable text"
