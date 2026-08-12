run_death_phase() { # enter|motion|drop|finish
    local phase=$1 target_pid target_log source_pid source_log sx sy tx ty

    CLIENT_LOG="$XDG_RUNTIME_DIR/${VICTIM}-${phase}-target.log"
    start_client target "$phase"
    wait_client "target ready"
    target_pid=$CLIENT_PID
    target_log=$CLIENT_LOG

    CLIENT_LOG="$XDG_RUNTIME_DIR/${VICTIM}-${phase}-source.log"
    start_client source "$phase"
    wait_client "source ready"
    source_pid=$CLIENT_PID
    source_log=$CLIENT_LOG
    sleep 0.2
    screenshot "$XDG_RUNTIME_DIR/_death.ppm"
    sx=$(dump_field 'app_id=kill-source' imgx); sy=$(dump_field 'app_id=kill-source' imgy)
    tx=$(dump_field 'app_id=kill-target' imgx); ty=$(dump_field 'app_id=kill-target' imgy)
    ctl "motion $((sx + 110)) $((sy + 75))"
    screenshot "$XDG_RUNTIME_DIR/_death.ppm"
    ctl "motion $((sx + 111)) $((sy + 75))"
    screenshot "$XDG_RUNTIME_DIR/_death.ppm"
    ctl "button left press"
    CLIENT_LOG="$source_log" wait_client "dragging"
    ctl "motion $((tx + 10)) $((ty + 10))"
    screenshot "$XDG_RUNTIME_DIR/_death.ppm"
    ctl "motion $((tx + 11)) $((ty + 10))"
    screenshot "$XDG_RUNTIME_DIR/_death.ppm"
    CLIENT_LOG="$target_log" wait_client "target enter"

    if [[ $phase == motion ]]; then
        ctl "motion $((tx + 13)) $((ty + 12))"
        CLIENT_LOG="$target_log" wait_client "target motion"
    fi

    if [[ $phase == drop || $phase == finish ]]; then
        ctl "button left release"
        if [[ $phase == drop ]]; then
            CLIENT_LOG="$target_log" wait_client "target drop"
        else
            CLIENT_LOG="$target_log" wait_client "target finish"
            CLIENT_LOG="$source_log" wait_client "source finish"
        fi
    fi

    if [[ $VICTIM == source ]]; then
        kill -9 "$source_pid" 2>/dev/null || true
        wait "$source_pid" 2>/dev/null || true
    else
        kill -9 "$target_pid" 2>/dev/null || true
        wait "$target_pid" 2>/dev/null || true
    fi

    if [[ $phase == enter || $phase == motion ]]; then
        sleep 0.2
        ctl "button left release"
    fi
    sleep 0.3
    kill "$source_pid" "$target_pid" 2>/dev/null || true
    wait "$source_pid" 2>/dev/null || true
    wait "$target_pid" 2>/dev/null || true
    expect_alive "compositor died when $VICTIM client was killed during $phase"
    input_health_probe
}

run_death_phase "$PHASE"
