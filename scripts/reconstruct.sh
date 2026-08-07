#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_NAME="${RUN_NAME:-default}"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT_DIR}/outputs/${RUN_NAME}}"
CONFIG_FILE="${MVS_CONFIG:-${ROOT_DIR}/config/reconstruction.json}"
if [[ -x "${ROOT_DIR}/bin/mvs_reconstruct" ]]; then
  SOURCE_ROOT="$(cd "${ROOT_DIR}/../.." && pwd)"
  DATA_ROOT="${DATA_ROOT:-${SOURCE_ROOT}/data}"
  MVS_RECONSTRUCT_BIN="${MVS_RECONSTRUCT_BIN:-${ROOT_DIR}/bin/mvs_reconstruct}"
  COLMAP_BIN="${COLMAP_BIN:-${ROOT_DIR}/bin/colmap}"
  OPENMVS_BIN_DIR="${OPENMVS_BIN_DIR:-${ROOT_DIR}/bin}"
else
  DATA_ROOT="${DATA_ROOT:-${ROOT_DIR}/data}"
  MVS_RECONSTRUCT_BIN="${MVS_RECONSTRUCT_BIN:-${ROOT_DIR}/build/mvs_reconstruct}"
  COLMAP_BIN="${COLMAP_BIN:-${ROOT_DIR}/build/third_party/colmap/src/colmap/exe/colmap}"
  OPENMVS_BIN_DIR="${OPENMVS_BIN_DIR:-${ROOT_DIR}/build/third_party/openmvs/bin}"
fi

ARGS=(
  "$MVS_RECONSTRUCT_BIN"
)

if [[ -f "$CONFIG_FILE" || -n "${MVS_CONFIG:-}" ]]; then
  ARGS+=(--config "$CONFIG_FILE")
fi

ARGS+=(
  --images "${DATA_ROOT}/images"
  --cameras "${DATA_ROOT}/cameras.json"
  --output "$OUTPUT_DIR"
  --colmap "$COLMAP_BIN"
  --openmvs-bin "$OPENMVS_BIN_DIR"
)

if [[ -n "${MVS_MAX_THREADS:-}" ]]; then
  ARGS+=(--max-threads "$MVS_MAX_THREADS")
fi

if [[ -n "${UNDISTORT_COPY_POLICY:-}" ]]; then
  ARGS+=(--undistort-copy-policy "$UNDISTORT_COPY_POLICY")
fi

if [[ -n "${MVS_REUSE_EXISTING:-}" ]]; then
  ARGS+=(--reuse-existing "$MVS_REUSE_EXISTING")
fi

if [[ -n "${MVS_REMOVE_DEPTH_MAPS:-}" ]]; then
  ARGS+=(--remove-depth-maps "$MVS_REMOVE_DEPTH_MAPS")
fi

if [[ -n "${MVS_MATCHER:-}" ]]; then
  ARGS+=(--matcher "$MVS_MATCHER")
fi

if [[ -n "${MVS_SEQUENTIAL_OVERLAP:-}" ]]; then
  ARGS+=(--sequential-overlap "$MVS_SEQUENTIAL_OVERLAP")
fi

if [[ -n "${MVS_SEQUENTIAL_QUADRATIC_OVERLAP:-}" ]]; then
  ARGS+=(--sequential-quadratic-overlap "$MVS_SEQUENTIAL_QUADRATIC_OVERLAP")
fi

if [[ -n "${MVS_DENSIFY_NUMBER_VIEWS:-}" ]]; then
  ARGS+=(--densify-number-views "$MVS_DENSIFY_NUMBER_VIEWS")
fi

if [[ -n "${MVS_DENSIFY_NUMBER_VIEWS_FUSE:-}" ]]; then
  ARGS+=(--densify-number-views-fuse "$MVS_DENSIFY_NUMBER_VIEWS_FUSE")
fi

if [[ -n "${MVS_DENSIFY_GEOMETRIC_ITERS:-}" ]]; then
  ARGS+=(--densify-geometric-iters "$MVS_DENSIFY_GEOMETRIC_ITERS")
fi

if [[ -n "${MVS_DENSIFY_RESOLUTION_LEVEL:-}" ]]; then
  ARGS+=(--densify-resolution-level "$MVS_DENSIFY_RESOLUTION_LEVEL")
fi

if [[ -n "${MVS_DENSIFY_MAX_RESOLUTION:-}" ]]; then
  ARGS+=(--densify-max-resolution "$MVS_DENSIFY_MAX_RESOLUTION")
fi

if [[ -n "${MVS_DENSIFY_ITERS:-}" ]]; then
  ARGS+=(--densify-iters "$MVS_DENSIFY_ITERS")
fi

if [[ -n "${MVS_GENERATE_TEXTURE:-}" ]]; then
  ARGS+=(--generate-texture "$MVS_GENERATE_TEXTURE")
fi

if [[ -n "${MVS_TEXTURE_PATCH_PACKING_HEURISTIC:-}" ]]; then
  ARGS+=(--texture-patch-packing-heuristic "$MVS_TEXTURE_PATCH_PACKING_HEURISTIC")
fi

if [[ -n "${MVS_TEXTURE_GLOBAL_SEAM_LEVELING:-}" ]]; then
  ARGS+=(--texture-global-seam-leveling "$MVS_TEXTURE_GLOBAL_SEAM_LEVELING")
fi

if [[ -n "${MVS_TEXTURE_LOCAL_SEAM_LEVELING:-}" ]]; then
  ARGS+=(--texture-local-seam-leveling "$MVS_TEXTURE_LOCAL_SEAM_LEVELING")
fi

"${ARGS[@]}"
