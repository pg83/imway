#!/usr/bin/env bash
set -uo pipefail

usage() {
    echo "usage: $0 [imway-supervisor-pid] [output.log]" >&2
    exit 2
}

load_stat() {
    local target="$1" line rest

    [[ -r "/proc/$target/stat" ]] || return 1
    IFS= read -r line < "/proc/$target/stat" || return 1
    rest="${line##*) }"
    read -r -a stat_fields <<< "$rest"
    [[ ${#stat_fields[@]} -ge 6 ]] || return 1
    stat_comm="${line#* (}"
    stat_comm="${stat_comm%)*}"
    stat_state="${stat_fields[0]}"
    stat_ppid="${stat_fields[1]}"
    stat_pgrp="${stat_fields[2]}"
    stat_session="${stat_fields[3]}"
    stat_tty="${stat_fields[4]}"
    stat_tpgid="${stat_fields[5]}"
}

process_table() {
    printf '%-8s %-8s %-8s %-8s %-10s %-8s %-5s %-24s %s\n' \
        PID PPID PGRP SID TTY_NR TPGID STATE WCHAN COMMAND

    local path target command wchan

    for path in /proc/[0-9]*/stat; do
        target="${path#/proc/}"
        target="${target%/stat}"
        load_stat "$target" || continue
        command="$(tr '\0' ' ' < "/proc/$target/cmdline" 2>/dev/null || true)"
        wchan="$(cat "/proc/$target/wchan" 2>/dev/null || true)"
        printf '%-8s %-8s %-8s %-8s %-10s %-8s %-5s %-24s %s\n' \
            "$target" "$stat_ppid" "$stat_pgrp" "$stat_session" "$stat_tty" \
            "$stat_tpgid" "$stat_state" "$wchan" "${command:-[$stat_comm]}"
    done
}

[[ $# -le 2 ]] || usage

pid="${1:-}"

if [[ -z "$pid" ]]; then
    candidates=()

    for comm in /proc/[0-9]*/comm; do
        if [[ "$(cat "$comm" 2>/dev/null || true)" == imway ]]; then
            candidate="${comm#/proc/}"
            candidates+=("${candidate%/comm}")
        fi
    done

    if [[ ${#candidates[@]} -ne 1 ]]; then
        echo "expected exactly one imway supervisor, got ${#candidates[@]}; pass its pid" >&2
        process_table | grep -E '(^ *PID|imway|zutty|htop)' >&2 || true
        exit 2
    fi

    pid="${candidates[0]}"
fi

[[ "$pid" =~ ^[0-9]+$ && -r "/proc/$pid/stat" ]] || usage
load_stat "$pid" || { echo "cannot read process state for $pid" >&2; exit 1; }
pgid="$stat_pgrp"
[[ "$pgid" =~ ^[0-9]+$ ]] || { echo "cannot determine process group for $pid" >&2; exit 1; }

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
output="${2:-imway-hang.${stamp}.log}"
gdb_bin="${GDB:-$(command -v gdb || true)}"

if [[ -z "$gdb_bin" || ! -x "$gdb_bin" ]]; then
    echo "gdb not found; set GDB=/absolute/path/to/gdb" >&2
    exit 1
fi

if [[ $(id -u) -ne 0 ]]; then
    echo "warning: not root; gdb attach may be denied by ptrace policy" >&2
fi

members=()

for stat in /proc/[0-9]*/stat; do
    member="${stat#/proc/}"
    member="${member%/stat}"
    load_stat "$member" || continue
    [[ "$stat_pgrp" == "$pgid" ]] && members+=("$member")
done

{
    echo "capture_utc=$stamp"
    echo "capture_uid=$(id -u)"
    echo "supervisor_pid=$pid"
    echo "process_group=$pgid"
    echo "gdb=$gdb_bin"
    echo
    uname -a
    echo
    echo "=== PROCESS TABLE ==="
    process_table
    echo
    echo "=== GROUP MEMBERS ==="
    printf '%s\n' "${members[@]}"

    for member in "${members[@]}"; do
        [[ -d "/proc/$member" ]] || continue
        echo
        echo "=== PROC $member ==="
        echo "cmdline:"
        tr '\0' ' ' < "/proc/$member/cmdline" 2>&1 || true
        echo
        echo "status:"
        sed -n '1,80p' "/proc/$member/status" 2>&1 || true
        echo "stat:"
        cat "/proc/$member/stat" 2>&1 || true
        echo "wchan:"
        cat "/proc/$member/wchan" 2>&1 || true
        echo
        echo "syscall:"
        cat "/proc/$member/syscall" 2>&1 || true
        echo
        echo "kernel stack:"
        cat "/proc/$member/stack" 2>&1 || true
        echo "fds:"
        ls -la "/proc/$member/fd" 2>&1 || true
        echo "task children:"
        for children in /proc/"$member"/task/*/children; do
            printf '%s: ' "$children"
            cat "$children" 2>&1 || true
            echo
        done
    done

    for member in "${members[@]}"; do
        [[ -d "/proc/$member" ]] || continue
        echo
        echo "=== GDB $member ==="
        timeout 30s "$gdb_bin" -nx -q -batch \
            -ex 'set pagination off' \
            -ex 'set confirm off' \
            -ex "attach $member" \
            -ex 'info program' \
            -ex 'info threads' \
            -ex 'info signals SIGTTIN' \
            -ex 'info signals SIGTTOU' \
            -ex 'info signals SIGTSTP' \
            -ex 'info signals SIGSTOP' \
            -ex 'info registers pc sp' \
            -ex 'x/i $pc' \
            -ex 'thread apply all bt full' \
            -ex 'detach' \
            -ex 'quit' 2>&1 || true
    done

    echo
    echo "=== PROCESS TABLE AFTER GDB ==="
    process_table
} >"$output" 2>&1

echo "$output"
