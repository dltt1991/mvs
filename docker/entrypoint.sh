#!/usr/bin/env bash
# 容器入口：补齐 scripts/build.sh 在 Linux 下拿不到的环境值，然后执行传入命令。
set -euo pipefail

# scripts/build.sh 用 `sysctl -n hw.ncpu` 探测核数，这是 macOS 专有命令，
# 在 Linux 上会失败并回退到 JOBS=4。这里显式按容器可见 CPU 数设置。
if [[ -z "${JOBS:-}" ]]; then
  JOBS="$(nproc)"
  export JOBS
fi

exec "$@"
