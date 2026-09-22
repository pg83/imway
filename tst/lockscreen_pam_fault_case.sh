# One PAM conversation goes wrong the way the scenario's IMWAY_CHAOS says: a
# module sending a prompt the lock screen does not answer with the password,
# one it cannot answer at all, or an allocation failing mid-answer. Every
# shape of it is a refusal, and the lock screen stays usable afterwards.
[[ -r /etc/pam.d/imway-test ]] || {
    echo "SKIP: no imway-test PAM service on this host"
    exit 127
}

ctl "set advanced.pam_service imway-test"
await 20 in_log "control: set advanced.pam_service" || { echo "settings are not reachable"; exit 1; }
. "$(dirname "$0")/lockscreen_refusal_case.sh"
