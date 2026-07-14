#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
exec ssh "${SSH_OPTS[@]}" "$VM_USER@127.0.0.1" "$@"
