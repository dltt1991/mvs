#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_NAME="${RUN_NAME:-default}"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT_DIR}/outputs/${RUN_NAME}}"
if [[ -x "${ROOT_DIR}/bin/mvs_reconstruct" ]]; then
  MVS_RECONSTRUCT_BIN="${MVS_RECONSTRUCT_BIN:-${ROOT_DIR}/bin/mvs_reconstruct}"
  COLMAP_BIN="${COLMAP_BIN:-${ROOT_DIR}/bin/colmap}"
  OPENMVS_BIN_DIR="${OPENMVS_BIN_DIR:-${ROOT_DIR}/bin}"
else
  MVS_RECONSTRUCT_BIN="${MVS_RECONSTRUCT_BIN:-${ROOT_DIR}/build/mvs_reconstruct}"
  COLMAP_BIN="${COLMAP_BIN:-${ROOT_DIR}/build/third_party/colmap/src/colmap/exe/colmap}"
  OPENMVS_BIN_DIR="${OPENMVS_BIN_DIR:-${ROOT_DIR}/build/third_party/openmvs/bin}"
fi

"$MVS_RECONSTRUCT_BIN" \
  --images "${ROOT_DIR}/data/images" \
  --cameras "${ROOT_DIR}/data/cameras.json" \
  --output "$OUTPUT_DIR" \
  --colmap "$COLMAP_BIN" \
  --openmvs-bin "$OPENMVS_BIN_DIR"
