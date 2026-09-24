#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_DROP_PROPS=Colorspace
# The connector has no Colorspace property to signal BT.2020 with.
set -euo pipefail
reason="imway: hdr: the connector carries no BT2020_RGB colorspace or HDR metadata"
. "$(dirname "$0")/display_live_hdr_refusal_case.sh"
