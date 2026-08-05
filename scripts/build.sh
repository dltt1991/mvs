#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
THIRD_BUILD_DIR="${BUILD_DIR}/third_party"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"
COLMAP_VERSION="4.1.1"
OPENMVS_VERSION="2.4.0"
COLMAP_SRC_DIR="${ROOT_DIR}/3rd/colmap-${COLMAP_VERSION}"
OPENMVS_SRC_DIR="${ROOT_DIR}/3rd/openMVS-${OPENMVS_VERSION}"
# COLMAP 用 FetchContent 拉取的两个依赖。预置在 3rd/ 下时走本地源码，
# 目录不存在则回退到网络拉取（见 3rd/colmap-*/src/thirdparty/CMakeLists.txt）。
POSELIB_SRC_DIR="${ROOT_DIR}/3rd/PoseLib"
FAISS_SRC_DIR="${ROOT_DIR}/3rd/faiss"
PACKAGE_VERSION=""
PACKAGE_ONLY=0
CLEAN_ONLY=0
OPENMVS_TOOLS=(
  InterfaceCOLMAP
  DensifyPointCloud
  ReconstructMesh
  TextureMesh
)

usage() {
  cat <<EOF
Usage:
  scripts/build.sh
  scripts/build.sh --clean
  scripts/build.sh --package <version>
  scripts/build.sh --package-only <version>

Options:
  --clean         Remove local build outputs and exit.
  --package       Build first, then create packages/mvs-<version>.
  --package-only  Create packages/mvs-<version> from existing build outputs.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --package)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "--package requires a version value." >&2
        exit 1
      fi
      PACKAGE_VERSION="$2"
      shift 2
      ;;
    --package-only)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "--package-only requires a version value." >&2
        exit 1
      fi
      PACKAGE_ONLY=1
      PACKAGE_VERSION="$2"
      shift 2
      ;;
    --clean)
      CLEAN_ONLY=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -n "$PACKAGE_VERSION" && ! "$PACKAGE_VERSION" =~ ^[A-Za-z0-9._-]+$ ]]; then
  echo "Package version may only contain letters, numbers, dot, underscore, and dash." >&2
  exit 1
fi

if [[ "$CLEAN_ONLY" -eq 1 && -n "$PACKAGE_VERSION" ]]; then
  echo "--clean cannot be combined with packaging options." >&2
  exit 1
fi

brew_prefixes() {
  local prefixes=()
  local formula
  for formula in "$@"; do
    local prefix
    prefix="$(brew --prefix "$formula" 2>/dev/null || true)"
    if [[ -n "$prefix" && -d "$prefix" ]]; then
      prefixes+=("$prefix")
    fi
  done
  local joined=""
  for prefix in "${prefixes[@]}"; do
    if [[ -n "$joined" ]]; then
      joined+=";"
    fi
    joined+="$prefix"
  done
  printf '%s' "$joined"
}

require_macos_formulae() {
  local missing=()
  local formula
  for formula in "$@"; do
    if ! brew list --versions "$formula" >/dev/null 2>&1; then
      missing+=("$formula")
    fi
  done

  if (( ${#missing[@]} > 0 )); then
    printf 'Missing Homebrew dependencies:\n\n' >&2
    printf '  %s\n' "${missing[@]}" >&2
    printf '\nInstall them with:\n\n  brew install %s\n\nThen rerun:\n\n  scripts/build.sh\n' "${missing[*]}" >&2
    exit 1
  fi
}

prepare_colmap_macos_deps() {
  if [[ "$(uname -s)" != "Darwin" ]]; then
    return
  fi

  require_macos_formulae \
    boost \
    eigen \
    nanoflann \
    opencv@4 \
    openimageio \
    jpeg-xl \
    curl \
    libomp \
    metis \
    glog \
    googletest \
    ceres-solver \
    suitesparse \
    qt \
    glew \
    cgal \
    sqlite3

  local libomp_prefix
  libomp_prefix="$(brew --prefix libomp)"
  if [[ ! -f "${libomp_prefix}/lib/libomp.dylib" || ! -f "${libomp_prefix}/include/omp.h" ]]; then
    echo "Homebrew libomp is installed, but omp.h or libomp.dylib was not found under ${libomp_prefix}" >&2
    echo "Try: brew reinstall libomp && brew link --force libomp" >&2
    exit 1
  fi

  export OpenMP_ROOT="$libomp_prefix"
}

openmvs_linker_flags() {
  if [[ "$(uname -s)" != "Darwin" ]]; then
    return
  fi

  local jpegxl_prefix
  jpegxl_prefix="$(brew --prefix jpeg-xl 2>/dev/null || true)"
  if [[ -n "$jpegxl_prefix" && -d "${jpegxl_prefix}/lib" ]]; then
    printf '%s' "-L${jpegxl_prefix}/lib"
  fi
}

find_local_onnxruntime() {
  # 只接受与当前平台匹配的本地包。macOS 的 onnxruntime 自带合法 CMake config，
  # 如果在 Linux 上被选中，COLMAP 会拿到 .dylib 并在链接阶段失败。
  local candidates=("${ROOT_DIR}/3rd/onnxruntime")
  case "$(uname -s)" in
    Darwin)
      candidates+=("${ROOT_DIR}/3rd/onnxruntime-osx-arm64-1.24.4")
      ;;
    Linux)
      case "$(uname -m)" in
        aarch64|arm64)
          candidates+=("${ROOT_DIR}/3rd/onnxruntime-linux-aarch64-1.24.4")
          ;;
        *)
          candidates+=(
            "${ROOT_DIR}/3rd/onnxruntime-linux-x64-gpu-1.24.4"
            "${ROOT_DIR}/3rd/onnxruntime-linux-x64-1.24.4"
          )
          ;;
      esac
      ;;
  esac

  local candidate
  for candidate in "${candidates[@]}"; do
    if [[ -d "${candidate}/include" && -d "${candidate}/lib" ]]; then
      printf '%s' "$candidate"
      return
    fi
  done
}

set_colmap_onnx_args() {
  COLMAP_ONNX_ARGS=()

  # ONNX 默认关闭（macOS 与 Linux 一致）。COLMAP 只在深度学习特征匹配等可选路径
  # 上用到它，本项目的重建流程不需要；关掉可以省去 onnxruntime 的下载、体积和
  # 平台相关的动态库/签名处理。需要时用 MVS_ENABLE_ONNX=1 显式开启。
  if [[ "${MVS_ENABLE_ONNX:-0}" != "1" ]]; then
    COLMAP_ONNX_ARGS=("-DONNX_ENABLED=OFF" "-DFETCH_ONNX=OFF")
    return
  fi

  local onnx_dir
  onnx_dir="$(find_local_onnxruntime)"
  if [[ -n "$onnx_dir" ]]; then
    local staged="${THIRD_BUILD_DIR}/onnxruntime"
    local compat_include="${staged}/include/onnxruntime"
    local staged_lib="${staged}/lib"
    local staged_cmake="${staged_lib}/cmake/onnxruntime"
    mkdir -p "$compat_include"
    mkdir -p "$staged_lib"
    cp -R "${onnx_dir}/include/." "$compat_include/"
    cp -R "${onnx_dir}/lib/." "$staged_lib/"
    if [[ "$(uname -s)" == "Darwin" ]]; then
      xattr -dr com.apple.quarantine "$staged" >/dev/null 2>&1 || true
      codesign --force --sign - "${staged_lib}"/libonnxruntime*.dylib >/dev/null 2>&1 || true
    fi
    COLMAP_ONNX_ARGS=(
      "-DONNX_ENABLED=ON"
      "-DFETCH_ONNX=OFF" \
      "-Donnxruntime_DIR=${staged_cmake}"
      "-Donnxruntime_CONFIG_DIR_HINTS=${staged_cmake}"
      "-Donnxruntime_INCLUDE_DIR_HINTS=${staged}/include"
      "-Donnxruntime_LIBRARY_DIR_HINTS=${staged_lib}"
    )
  else
    COLMAP_ONNX_ARGS=("-DONNX_ENABLED=OFF")
  fi
}

set_colmap_fetch_args() {
  # poselib / faiss 预置在 3rd/ 下时走本地源码，构建期不联网；
  # 目录不存在则留空，由 COLMAP 的 CMake 回退到 git+ssh 拉取。
  # 用 -U 清掉 CMake 缓存里的旧值，避免上次构建残留的路径在目录删除后继续生效。
  COLMAP_FETCH_ARGS=(-UPOSELIB_LOCAL_SOURCE_DIR -UFAISS_LOCAL_SOURCE_DIR)
  if [[ -f "${POSELIB_SRC_DIR}/CMakeLists.txt" ]]; then
    COLMAP_FETCH_ARGS+=("-DPOSELIB_LOCAL_SOURCE_DIR=${POSELIB_SRC_DIR}")
  fi
  if [[ -f "${FAISS_SRC_DIR}/CMakeLists.txt" ]]; then
    COLMAP_FETCH_ARGS+=("-DFAISS_LOCAL_SOURCE_DIR=${FAISS_SRC_DIR}")
  fi
}

opencv_dir_arg() {
  local opencv_prefix
  opencv_prefix="$(brew --prefix opencv@4 2>/dev/null || brew --prefix opencv 2>/dev/null || true)"
  if [[ -z "$opencv_prefix" ]]; then
    return
  fi

  local config
  config="$(find "$opencv_prefix" -path '*/OpenCVConfig.cmake' -print -quit 2>/dev/null || true)"
  if [[ -n "$config" ]]; then
    printf '%s' "-DOpenCV_DIR=$(dirname "$config")"
  fi
}

require_vcglib() {
  local vcg_root="${ROOT_DIR}/3rd/vcglib"
  if [[ ! -f "${vcg_root}/vcg/complex/complex.h" ]]; then
    cat >&2 <<EOF
OpenMVS requires VCG headers, but they were not found at:

  ${vcg_root}

Fetch them with:

  scripts/fetch_3rdparty.sh

Or manually clone:

  git clone --recursive git@github.com:cnr-isti-vclab/vcglib.git ${vcg_root}
EOF
    exit 1
  fi
}

reset_build_dir_if_source_changed() {
  local source_dir="$1"
  local build_dir="$2"
  local cache_file="${build_dir}/CMakeCache.txt"

  if [[ ! -f "$cache_file" ]]; then
    return
  fi

  local cached_source
  cached_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache_file" | head -n 1)"
  if [[ -n "$cached_source" && "$cached_source" != "$source_dir" ]]; then
    echo "Resetting ${build_dir}: CMake cache points to ${cached_source}"
    command rm -r "$build_dir"
  fi
}

fix_colmap_onnx_runtime_path() {
  if [[ "$(uname -s)" != "Darwin" ]]; then
    return
  fi

  local colmap_bin="${THIRD_BUILD_DIR}/colmap/src/colmap/exe/colmap"
  local onnx_lib="${THIRD_BUILD_DIR}/onnxruntime/lib/libonnxruntime.1.24.4.dylib"
  if [[ -x "$colmap_bin" && -f "$onnx_lib" ]]; then
    install_name_tool -change "@rpath/libonnxruntime.1.24.4.dylib" "$onnx_lib" "$colmap_bin" || true
    install_name_tool -change "/opt/homebrew/opt/onnxruntime/lib/libonnxruntime.1.24.4.dylib" "$onnx_lib" "$colmap_bin" || true
    codesign --force --sign - "$colmap_bin" >/dev/null 2>&1 || true
  fi
}

require_file() {
  local path="$1"
  local description="$2"
  if [[ ! -e "$path" ]]; then
    echo "Missing ${description}: ${path}" >&2
    echo "Run scripts/build.sh before packaging, or use scripts/build.sh --package <version>." >&2
    exit 1
  fi
}

copy_dir_contents() {
  local src="$1"
  local dst="$2"
  mkdir -p "$dst"
  cp -R "${src}/." "$dst/"
}

copy_openmvs_tool() {
  local tool="$1"
  local src="${THIRD_BUILD_DIR}/openmvs/bin/${tool}"
  require_file "$src" "OpenMVS tool ${tool}"
  cp "$src" "${PACKAGE_DIR}/bin/${tool}"
}

clean_package_metadata() {
  find "$PACKAGE_DIR" \( -name .DS_Store -o -name __pycache__ \) -prune -exec command rm -r {} + 2>/dev/null || true
  find "$PACKAGE_DIR" -name '*.pyc' -delete 2>/dev/null || true
}

fix_packaged_colmap_onnx_runtime_path() {
  if [[ "$(uname -s)" != "Darwin" ]]; then
    return
  fi

  local colmap_bin="${PACKAGE_DIR}/bin/colmap"
  local onnx_lib="${PACKAGE_DIR}/lib/onnxruntime/libonnxruntime.1.24.4.dylib"
  if [[ -x "$colmap_bin" && -f "$onnx_lib" ]]; then
    install_name_tool -change "@rpath/libonnxruntime.1.24.4.dylib" "@loader_path/../lib/onnxruntime/libonnxruntime.1.24.4.dylib" "$colmap_bin" >/dev/null 2>&1 || true
    install_name_tool -change "${THIRD_BUILD_DIR}/onnxruntime/lib/libonnxruntime.1.24.4.dylib" "@loader_path/../lib/onnxruntime/libonnxruntime.1.24.4.dylib" "$colmap_bin" >/dev/null 2>&1 || true
    install_name_tool -change "/opt/homebrew/opt/onnxruntime/lib/libonnxruntime.1.24.4.dylib" "@loader_path/../lib/onnxruntime/libonnxruntime.1.24.4.dylib" "$colmap_bin" >/dev/null 2>&1 || true
    codesign --force --sign - "$colmap_bin" >/dev/null 2>&1 || true
    codesign --force --sign - "${PACKAGE_DIR}/lib/onnxruntime"/libonnxruntime*.dylib >/dev/null 2>&1 || true
  fi
}

package_artifacts() {
  local package_root="${ROOT_DIR}/packages"
  PACKAGE_DIR="${package_root}/mvs-${PACKAGE_VERSION}"

  require_file "${BUILD_DIR}/mvs_reconstruct" "project binary"
  require_file "${THIRD_BUILD_DIR}/colmap/src/colmap/exe/colmap" "COLMAP binary"
  require_file "${ROOT_DIR}/scripts/reconstruct.sh" "reconstruct script"
  require_file "${ROOT_DIR}/scripts/run_python_ui.py" "Python UI script"
  require_file "${ROOT_DIR}/scripts/report_run.py" "run report script"
  require_file "${ROOT_DIR}/config/reconstruction.json" "reconstruction config"
  require_file "${ROOT_DIR}/README.md" "README"

  command rm -r "$PACKAGE_DIR" 2>/dev/null || true
  mkdir -p "${PACKAGE_DIR}/bin" "${PACKAGE_DIR}/config" "${PACKAGE_DIR}/scripts" "${PACKAGE_DIR}/src"

  cp "${BUILD_DIR}/mvs_reconstruct" "${PACKAGE_DIR}/bin/mvs_reconstruct"
  cp "${THIRD_BUILD_DIR}/colmap/src/colmap/exe/colmap" "${PACKAGE_DIR}/bin/colmap"
  for tool in "${OPENMVS_TOOLS[@]}"; do
    copy_openmvs_tool "$tool"
  done

  cp "${ROOT_DIR}/scripts/reconstruct.sh" "${PACKAGE_DIR}/scripts/reconstruct.sh"
  cp "${ROOT_DIR}/scripts/run_python_ui.py" "${PACKAGE_DIR}/scripts/run_python_ui.py"
  cp "${ROOT_DIR}/scripts/report_run.py" "${PACKAGE_DIR}/scripts/report_run.py"
  cp "${ROOT_DIR}/config/reconstruction.json" "${PACKAGE_DIR}/config/reconstruction.json"
  copy_dir_contents "${ROOT_DIR}/src/python" "${PACKAGE_DIR}/src/python"
  cp "${ROOT_DIR}/README.md" "${PACKAGE_DIR}/README.md"

  # ONNX 默认关闭，产物里不带 onnxruntime 动态库。显式开启时才打包，
  # 避免历史构建残留在 build/third_party/onnxruntime 下的库被误打包。
  if [[ "${MVS_ENABLE_ONNX:-0}" == "1" && -d "${THIRD_BUILD_DIR}/onnxruntime/lib" ]]; then
    copy_dir_contents "${THIRD_BUILD_DIR}/onnxruntime/lib" "${PACKAGE_DIR}/lib/onnxruntime"
    fix_packaged_colmap_onnx_runtime_path
  fi

  clean_package_metadata
  chmod +x "${PACKAGE_DIR}/bin/"* "${PACKAGE_DIR}/scripts/reconstruct.sh" "${PACKAGE_DIR}/scripts/run_python_ui.py" "${PACKAGE_DIR}/scripts/report_run.py"

  cat > "${PACKAGE_DIR}/manifest.json" <<EOF
{
  "package_version": "${PACKAGE_VERSION}",
  "colmap_version": "${COLMAP_VERSION}",
  "openmvs_version": "${OPENMVS_VERSION}",
  "layout": {
    "bin": "bin",
    "scripts": "scripts",
    "python": "src/python",
    "data": "../../data"
  }
}
EOF

  echo "Package created: ${PACKAGE_DIR}"
}

if [[ "$CLEAN_ONLY" -eq 1 ]]; then
  # BUILD_DIR 可能是挂载点（容器里 build/ 绑到宿主机 ext4，因为项目所在的
  # yrfs 不支持 execve）。对挂载点 rm -r 会失败，所以清内容而不是删目录本身。
  if [[ -d "$BUILD_DIR" ]]; then
    find "$BUILD_DIR" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
  fi
  echo "Cleaned build outputs: ${BUILD_DIR}"
  exit 0
fi

if [[ "$PACKAGE_ONLY" -eq 0 ]]; then
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR" --parallel "$JOBS"

  if [[ -d "$COLMAP_SRC_DIR" ]]; then
    prepare_colmap_macos_deps
    set_colmap_onnx_args
    set_colmap_fetch_args
    reset_build_dir_if_source_changed "$COLMAP_SRC_DIR" "${THIRD_BUILD_DIR}/colmap"
    cmake -S "$COLMAP_SRC_DIR" -B "${THIRD_BUILD_DIR}/colmap" \
      -Uonnxruntime_DIR \
      -Uonnxruntime_CONFIG_DIR_HINTS \
      -Uonnxruntime_INCLUDE_DIR_HINTS \
      -Uonnxruntime_LIBRARY_DIR_HINTS \
      -DCMAKE_BUILD_TYPE=Release \
      "${COLMAP_ONNX_ARGS[@]}" \
      "${COLMAP_FETCH_ARGS[@]}" \
      -DCMAKE_PREFIX_PATH="$(brew_prefixes boost eigen nanoflann opencv@4 opencv openimageio jpeg-xl curl libomp metis glog googletest ceres-solver suitesparse qt glew cgal sqlite3)" \
      -DCMAKE_INSTALL_PREFIX="${THIRD_BUILD_DIR}/colmap/install"
    cmake --build "${THIRD_BUILD_DIR}/colmap" --parallel "$JOBS" --target colmap_main
    fix_colmap_onnx_runtime_path
  else
    echo "Skipping COLMAP build: 3rd/colmap-${COLMAP_VERSION} is missing. Run scripts/fetch_3rdparty.sh first." >&2
  fi

  if [[ -d "$OPENMVS_SRC_DIR" ]]; then
    require_vcglib
    OPENCV_DIR_ARG="$(opencv_dir_arg)"
    OPENMVS_LINKER_FLAGS="$(openmvs_linker_flags)"
    export VCG_ROOT="${ROOT_DIR}/3rd/vcglib"
    reset_build_dir_if_source_changed "$OPENMVS_SRC_DIR" "${THIRD_BUILD_DIR}/openmvs"
    cmake -S "$OPENMVS_SRC_DIR" -B "${THIRD_BUILD_DIR}/openmvs" \
      -DCMAKE_BUILD_TYPE=Release \
      -DBoost_NO_BOOST_CMAKE=ON \
      -DOpenMVS_USE_PYTHON=OFF \
      -DVCG_ROOT="${VCG_ROOT}" \
      ${OPENMVS_LINKER_FLAGS:+"-DCMAKE_EXE_LINKER_FLAGS=${OPENMVS_LINKER_FLAGS}"} \
      ${OPENCV_DIR_ARG:+"$OPENCV_DIR_ARG"} \
      -DCMAKE_PREFIX_PATH="$(brew_prefixes boost eigen nanoflann opencv@4 opencv openimageio jpeg-xl curl libomp metis glog googletest ceres-solver suitesparse qt glew cgal sqlite3)" \
      -DCMAKE_INSTALL_PREFIX="${THIRD_BUILD_DIR}/openmvs/install"
    cmake --build "${THIRD_BUILD_DIR}/openmvs" --parallel "$JOBS"
  else
    echo "Skipping OpenMVS build: 3rd/openMVS-${OPENMVS_VERSION} is missing. Run scripts/fetch_3rdparty.sh first." >&2
  fi

  echo "Project binary: ${BUILD_DIR}/mvs_reconstruct"
  echo "Expected COLMAP binary: ${THIRD_BUILD_DIR}/colmap/src/colmap/exe/colmap"
  echo "Expected OpenMVS tools: ${THIRD_BUILD_DIR}/openmvs/bin"
fi

if [[ -n "$PACKAGE_VERSION" ]]; then
  package_artifacts
fi
