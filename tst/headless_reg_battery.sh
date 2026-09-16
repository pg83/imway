#!/usr/bin/env bash
# imway-env: IMWAY_SYSFS_POWER_SUPPLY=./power
# imway-pre: mkdir -p power/AC power/BAT0 power/hidpp_battery_0
# imway-pre: printf 'Mains\n' > power/AC/type; printf '1\n' > power/AC/online
# imway-pre: printf 'Battery\n' > power/BAT0/type; printf '77\n' > power/BAT0/capacity; printf 'Discharging\n' > power/BAT0/status
# imway-pre: printf 'Battery\n' > power/hidpp_battery_0/type; printf 'Device\n' > power/hidpp_battery_0/scope; printf '20\n' > power/hidpp_battery_0/capacity; printf 'Full\n' > power/hidpp_battery_0/status
# The power-supply walk: the mains entry is not a battery, the mouse's own
# cell is scope=Device and only a fallback, and the system battery wins. Pull
# the system one out from under it and the next tick re-enumerates onto the
# fallback instead of going blind.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

battery() { # <field>
    dump_field '^battery ' "$1"
}
reads() { # <pct> <discharging>
    [[ "$(battery pct)" == "$1" && "$(battery discharging)" == "$2" ]]
}

# the supplies are re-read on a ~2s tick, so every check here polls
await 100 reads 77 1 || {
    echo "the system battery was not picked up: $(dump_state | grep '^battery ')"
    exit 1
}

rm -rf power/BAT0

await 100 reads 20 0 || {
    echo "the device battery did not take over: $(dump_state | grep '^battery ')"
    exit 1
}

rm -rf power/hidpp_battery_0

await 100 reads -1 0 || {
    echo "a vanished supply left a stale reading: $(dump_state | grep '^battery ')"
    exit 1
}

expect_alive "compositor died enumerating power supplies"
echo "OK: the desktop follows the power supplies it can see"
