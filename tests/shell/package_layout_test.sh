#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="test-package"
PACKAGE_DIR="${ROOT_DIR}/packages/mvs-${VERSION}"

command rm -r "$PACKAGE_DIR" 2>/dev/null || true

"${ROOT_DIR}/scripts/build.sh" --package-only "$VERSION"

test -x "${PACKAGE_DIR}/bin/mvs_reconstruct"
test -x "${PACKAGE_DIR}/bin/colmap"
test -x "${PACKAGE_DIR}/bin/InterfaceCOLMAP"
test -x "${PACKAGE_DIR}/bin/DensifyPointCloud"
test -x "${PACKAGE_DIR}/bin/ReconstructMesh"
test -x "${PACKAGE_DIR}/bin/TextureMesh"
test -f "${PACKAGE_DIR}/scripts/reconstruct.sh"
test -x "${PACKAGE_DIR}/scripts/extract_video_frames.py"
test -f "${PACKAGE_DIR}/scripts/run_python_ui.py"
test -f "${PACKAGE_DIR}/src/python/mvs_io/run.py"
test ! -e "${PACKAGE_DIR}/data"
test -f "${PACKAGE_DIR}/README.md"
test -f "${PACKAGE_DIR}/manifest.json"
grep -q '"output": "./outputs/default"' "${PACKAGE_DIR}/config/reconstruction.json"
grep -q '"colmap": "./bin"' "${PACKAGE_DIR}/config/reconstruction.json"
grep -q '"openmvs_bin": "./bin"' "${PACKAGE_DIR}/config/reconstruction.json"
grep -q 'DATA_ROOT=' "${PACKAGE_DIR}/scripts/reconstruct.sh"
grep -q 'SOURCE_ROOT=' "${PACKAGE_DIR}/scripts/reconstruct.sh"
grep -q '\${DATA_ROOT}/images' "${PACKAGE_DIR}/scripts/reconstruct.sh"

command rm -r "$PACKAGE_DIR"
