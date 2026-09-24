#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_EDID=no-bt2020
# The display's EDID has PQ but no BT.2020 RGB colorimetry.
set -euo pipefail
reason="imway: display EDID does not advertise PQ + BT.2020 RGB"
. "$(dirname "$0")/display_live_hdr_refusal_case.sh"
