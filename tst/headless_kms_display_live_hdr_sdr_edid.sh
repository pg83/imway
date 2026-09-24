#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_EDID=sdr
# The display's EDID has no PQ and no BT.2020 RGB.
set -euo pipefail
reason="imway: display EDID does not advertise PQ + BT.2020 RGB"
. "$(dirname "$0")/display_live_hdr_refusal_case.sh"
