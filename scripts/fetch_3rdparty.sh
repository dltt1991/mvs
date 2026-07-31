#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_DIR="${ROOT_DIR}/3rd"

COLMAP_REPO="git@github.com:colmap/colmap.git"
COLMAP_TAG="4.1.1"
COLMAP_DIR="colmap-${COLMAP_TAG}"
OPENMVS_REPO="git@github.com:cdcseacave/openMVS.git"
OPENMVS_TAG="v2.4.0"
OPENMVS_VERSION="${OPENMVS_TAG#v}"
OPENMVS_DIR="openMVS-${OPENMVS_VERSION}"
VCG_REPO="git@github.com:cnr-isti-vclab/vcglib.git"

check_tag() {
  local repo="$1"
  local tag="$2"
  if ! perl -e 'alarm shift; exec @ARGV' 60 git ls-remote --exit-code --tags "$repo" "refs/tags/${tag}" >/dev/null; then
    echo "Unable to verify tag ${tag} at ${repo}. Check network access and retry." >&2
    return 1
  fi
}

clone_or_update() {
  local name="$1"
  local repo="$2"
  local tag="$3"
  local path="${THIRD_DIR}/${name}"

  mkdir -p "$THIRD_DIR"
  if [[ -d "$path" && ! -d "$path/.git" ]]; then
    echo "Using existing vendored source at ${path} (no .git metadata)."
    echo "Delete this directory first if you want to fetch ${name} again."
    return
  fi

  if [[ ! -d "$path/.git" ]]; then
    git clone --recursive --branch "$tag" --depth 1 "$repo" "$path"
    return
  fi

  git -C "$path" fetch --tags --depth 1 origin "$tag"
  git -C "$path" checkout --detach "refs/tags/${tag}"
  git -C "$path" submodule update --init --recursive
}

clone_or_update_branch() {
  local name="$1"
  local repo="$2"
  local branch="$3"
  local path="${THIRD_DIR}/${name}"

  mkdir -p "$THIRD_DIR"
  if [[ -d "$path" && ! -d "$path/.git" ]]; then
    echo "Using existing vendored source at ${path} (no .git metadata)."
    echo "Delete this directory first if you want to fetch ${name} again."
    return
  fi

  if [[ ! -d "$path/.git" ]]; then
    git clone --recursive --branch "$branch" --depth 1 "$repo" "$path"
    return
  fi

  git -C "$path" fetch origin "$branch" --depth 1
  git -C "$path" checkout "$branch"
  git -C "$path" pull --ff-only origin "$branch"
  git -C "$path" submodule update --init --recursive
}

if [[ "${1:-}" == "--check-only" ]]; then
  check_tag "$COLMAP_REPO" "$COLMAP_TAG"
  echo "COLMAP tag ${COLMAP_TAG} is available"
  check_tag "$OPENMVS_REPO" "$OPENMVS_TAG"
  echo "OpenMVS tag ${OPENMVS_TAG} is available"
  exit 0
fi

clone_or_update "$COLMAP_DIR" "$COLMAP_REPO" "$COLMAP_TAG"
clone_or_update "$OPENMVS_DIR" "$OPENMVS_REPO" "$OPENMVS_TAG"
clone_or_update_branch "vcglib" "$VCG_REPO" "main"

echo "Third-party sources are ready in ${THIRD_DIR}"
