#!/usr/bin/env bash
# imway-env: IMWAY_SYSFS_POWER_SUPPLY=./power
# imway-pre: mkdir -p power/BAT0 power/BAT1 power/hid_a power/hid_b
# imway-pre: for b in BAT0 BAT1; do printf 'Battery\n' > power/$b/type; printf '55\n' > power/$b/capacity; printf 'Discharging\n' > power/$b/status; done
# imway-pre: for d in hid_a hid_b; do printf 'Battery\n' > power/$d/type; printf 'Device\n' > power/$d/scope; printf '30\n' > power/$d/capacity; printf 'Full\n' > power/$d/status; done
# Several supplies of each kind: once one system battery is picked the walk
# looks at no other entry, and of several device cells the first seen is
# kept as the fallback. Both pairs read alike, so the walk's order does not
# matter: the system reading, then with both system batteries gone the
# device one, then nothing. The directory itself gone is walked as empty,
# and a battery in it once it is back is read.
set -euo pipefail
. "$(dirname "$0")/lib.sh"

battery() { dump_field '^battery ' "$1"; }
reads() { # <pct> <discharging>
    [[ "$(battery pct)" == "$1" && "$(battery discharging)" == "$2" ]]
}

await 100 reads 55 1 || { echo "a system battery was not picked: $(dump_state | grep '^battery ')"; exit 1; }
rm -rf power/BAT0 power/BAT1
await 100 reads 30 0 || { echo "a device cell did not take over: $(dump_state | grep '^battery ')"; exit 1; }
rm -rf power/hid_a power/hid_b
await 100 reads -1 0 || { echo "a vanished supply left a stale reading: $(dump_state | grep '^battery ')"; exit 1; }

# the whole power-supply directory gone: the walk finds nothing to list,
# and once a battery appears again it is read
rm -rf power
sleep 2.5 # the walk runs every two seconds
mkdir -p power/BAT2
printf 'Battery\n' > power/BAT2/type; printf '80\n' > power/BAT2/capacity; printf 'Charging\n' > power/BAT2/status
await 100 reads 80 0 || { echo "a battery after the directory came back was not read: $(dump_state | grep '^battery ')"; exit 1; }

expect_alive "compositor died walking several power supplies"
echo "OK: several system batteries and device cells, one of each picked"
