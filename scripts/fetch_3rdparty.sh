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

# COLMAP 通过 FetchContent 拉取的两个依赖，预置到 3rd/ 后构建期不再联网。
# 版本必须与 3rd/colmap-${COLMAP_TAG}/src/thirdparty/CMakeLists.txt 里的一致。
POSELIB_REPO="git@github.com:PoseLib/PoseLib.git"
POSELIB_COMMIT="fa7280fee27f97aff31ae7f98bab7f583fac7d08"
POSELIB_DIR="PoseLib"
FAISS_REPO="git@github.com:facebookresearch/faiss.git"
FAISS_TAG="v1.14.1"
FAISS_DIR="faiss"

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

clone_or_update_commit() {
  # PoseLib 固定在一个 commit 而非 tag，浅克隆拿不到任意历史 commit，
  # 因此这里用完整克隆再 checkout。
  local name="$1"
  local repo="$2"
  local commit="$3"
  local path="${THIRD_DIR}/${name}"

  mkdir -p "$THIRD_DIR"
  if [[ -d "$path" && ! -d "$path/.git" ]]; then
    echo "Using existing vendored source at ${path} (no .git metadata)."
    echo "Delete this directory first if you want to fetch ${name} again."
    return
  fi

  if [[ ! -d "$path/.git" ]]; then
    git clone "$repo" "$path"
  else
    git -C "$path" fetch origin
  fi
  git -C "$path" checkout --detach "$commit"
  git -C "$path" submodule update --init --recursive
}

prepare_poselib() {
  # COLMAP 在配置阶段会把 PoseLib 的 benchmark/ 改名，避免和 Google Benchmark 的
  # benchmark/benchmark.h 冲突（PoseLib 把整个源码目录暴露为 include path）。
  # 预置源码时提前做掉，否则构建会修改 3rd/ 下的源码树，留下 git 脏状态。
  local path="${THIRD_DIR}/${POSELIB_DIR}"
  if [[ -d "${path}/benchmark" && ! -d "${path}/poselib_benchmark" ]]; then
    mv "${path}/benchmark" "${path}/poselib_benchmark"
    echo "Renamed PoseLib benchmark/ -> poselib_benchmark/ (matches COLMAP's build-time rename)."
  fi
}

if [[ "${1:-}" == "--check-only" ]]; then
  check_tag "$COLMAP_REPO" "$COLMAP_TAG"
  echo "COLMAP tag ${COLMAP_TAG} is available"
  check_tag "$OPENMVS_REPO" "$OPENMVS_TAG"
  echo "OpenMVS tag ${OPENMVS_TAG} is available"
  check_tag "$FAISS_REPO" "$FAISS_TAG"
  echo "faiss tag ${FAISS_TAG} is available"
  exit 0
fi

clone_or_update "$COLMAP_DIR" "$COLMAP_REPO" "$COLMAP_TAG"
clone_or_update "$OPENMVS_DIR" "$OPENMVS_REPO" "$OPENMVS_TAG"
clone_or_update_branch "vcglib" "$VCG_REPO" "main"
clone_or_update_commit "$POSELIB_DIR" "$POSELIB_REPO" "$POSELIB_COMMIT"
prepare_poselib
clone_or_update "$FAISS_DIR" "$FAISS_REPO" "$FAISS_TAG"

echo "Third-party sources are ready in ${THIRD_DIR}"
