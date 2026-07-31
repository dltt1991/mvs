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
test -f "${PACKAGE_DIR}/src/python/mvs_io/run.py"
test -f "${PACKAGE_DIR}/data/cameras.json"
test -f "${PACKAGE_DIR}/README.md"
test -f "${PACKAGE_DIR}/manifest.json"

command rm -r "$PACKAGE_DIR"
