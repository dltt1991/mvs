#!/usr/bin/env bash
# 容器入口：补齐 scripts/build.sh 在 Linux 下拿不到的环境值，然后执行传入命令。
set -euo pipefail

# scripts/build.sh 用 `sysctl -n hw.ncpu` 探测核数，这是 macOS 专有命令，
# 在 Linux 上会失败并回退到 JOBS=4。这里显式按容器可见 CPU 数设置。
if [[ -z "${JOBS:-}" ]]; then
  JOBS="$(nproc)"
  export JOBS
fi

# 挂载点可写性检查。bind mount 的宿主机目录不存在时，docker 守护进程会以 root
# 身份创建它，容器内的非 root 用户就写不进去；CMake 报出来的是
# "Unable to (re)create the private pkgRedirects directory" 这类看不出根因的错误。
# 这里提前给出可操作的提示。
for d in build outputs packages; do
  target="/workspace/${d}"
  [[ -d "$target" ]] || continue
  if ! touch "${target}/.write-test" 2>/dev/null; then
    owner="$(stat -c '%u:%g' "$target" 2>/dev/null || echo '?')"
    mode="$(stat -c '%a' "$target" 2>/dev/null || echo '?')"
    cat >&2 <<EOF
错误：/workspace/${d} 不可写
  当前用户 $(id -u):$(id -g)，目录属主 ${owner}，权限 ${mode}

常见原因：宿主机上对应的挂载目录不存在时，docker 会以 root 创建它。
属主已经和当前用户一致时，检查权限位是否缺少写位。

修复：在宿主机上执行（\$MVS_WORK_DIR 默认 /data/taoguo/mvs-workspace）
  sudo chown -R $(id -u):$(id -g) "\${MVS_WORK_DIR:-/data/taoguo/mvs-workspace}"
没有 sudo 权限时，若目录为空可以直接重建（删除权限来自父目录）：
  rmdir "\${MVS_WORK_DIR:-/data/taoguo/mvs-workspace}"/{build,outputs,packages}
  mkdir -p "\${MVS_WORK_DIR:-/data/taoguo/mvs-workspace}"/{build,outputs,packages}
EOF
    exit 1
  fi
  rm -f "${target}/.write-test"
done

exec "$@"
