#!/usr/bin/env bash
# imway-env: IMWAY_FAKE_KMS_DROP_PROPS=HDR_OUTPUT_METADATA
# The connector has no HDR_OUTPUT_METADATA property to carry the metadata.
set -euo pipefail
reason="imway: hdr: the connector carries no BT2020_RGB colorspace or HDR metadata"
. "$(dirname "$0")/display_live_hdr_refusal_case.sh"
