# Docker 构建和运行环境

本目录提供基于 NVIDIA CUDA 的 Ubuntu 24.04 容器化构建环境，包含项目编译和运行所需的全部依赖。

## 文件说明

以下路径均相对仓库根目录：

- `Dockerfile`：镜像定义，基于 `nvcr.io/nvidia/cuda:12.6.3-devel-ubuntu24.04`。
- `docker-compose.yml`：便捷启动配置，自动映射 GPU 和源码目录。
- `docker/entrypoint.sh`：容器入口脚本，设置构建环境变量。
- `docker/README.md`：本文档。
- `.dockerignore`：排除重目录，加快镜像构建。

基础镜像走 NVIDIA 官方仓库 `nvcr.io` 而不是 Docker Hub 的 `nvidia/cuda`，两者内容一致，前者在部分网络环境下可达性更好。首次拉取的注意事项见下文「前置条件 → 基础镜像」。

## 前置条件

### 1. NVIDIA 驱动

宿主机需要 NVIDIA 驱动 >= 560（支持 CUDA 12.6）。检查：

```bash
nvidia-smi
```

如果显示驱动版本 < 560，参考 [NVIDIA 官方文档](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/) 升级。

### 2. NVIDIA Container Toolkit

Docker 容器访问 GPU 需要 NVIDIA Container Toolkit。如果 `docker run --gpus all nvcr.io/nvidia/cuda:12.6.3-base-ubuntu24.04 nvidia-smi` 失败，按以下步骤安装：

```bash
# 添加 NVIDIA 包仓库
distribution=$(. /etc/os-release;echo $ID$VERSION_ID) \
      && curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | sudo gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg \
      && curl -s -L https://nvidia.github.io/libnvidia-container/$distribution/libnvidia-container.list | \
            sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | \
            sudo tee /etc/apt/sources.list.d/nvidia-container-toolkit.list

# 安装并重启 Docker
sudo apt-get update
sudo apt-get install -y nvidia-container-toolkit
sudo systemctl restart docker
```

验证：

```bash
docker run --rm --gpus all nvcr.io/nvidia/cuda:12.6.3-base-ubuntu24.04 nvidia-smi
```

### 3. 基础镜像（国内网络需先准备）

`Dockerfile` 的基础镜像是 `nvcr.io/nvidia/cuda:12.6.3-devel-ubuntu24.04`，最大层约 2.4 GB。国内直连 nvcr.io 的 blob CDN 实测只有 ~95 KB/s，完整拉取需要 7 小时以上，表现为 `docker-compose build` 长时间停在某一层的 `pulling` 状态（不是卡死，是慢）。

需要注意 `/etc/docker/daemon.json` 里的 `registry-mirrors` 对此**不生效**：镜像加速只作用于 `docker.io`，且多数公共镜像站只缓存官方 `library/*` 命名空间，`nvidia/` 和 `nvcr.io` 都绕过镜像直连源站。

三种处理方式，按可用性选：

**a. 给 Docker daemon 配代理**（最彻底，需要可用代理）

```bash
sudo mkdir -p /etc/systemd/system/docker.service.d
sudo tee /etc/systemd/system/docker.service.d/proxy.conf << 'EOF'
[Service]
Environment="HTTP_PROXY=http://127.0.0.1:7890"
Environment="HTTPS_PROXY=http://127.0.0.1:7890"
Environment="NO_PROXY=localhost,127.0.0.1"
EOF
sudo systemctl daemon-reload
sudo systemctl restart docker
```

端口改成实际代理端口。注意必须配在 daemon 层，shell 里 `export HTTP_PROXY` 无效 —— 拉镜像的是 dockerd 而不是 CLI。

**b. 从已有该镜像的机器迁移**

```bash
# 有网的机器
docker pull nvcr.io/nvidia/cuda:12.6.3-devel-ubuntu24.04
docker save nvcr.io/nvidia/cuda:12.6.3-devel-ubuntu24.04 | gzip > cuda-devel.tar.gz

# 目标机器
docker load < cuda-devel.tar.gz
```

加载后 `docker-compose build` 会直接命中本地层，跳过 pull。

**c. 后台挂着拉**

```bash
nohup docker pull nvcr.io/nvidia/cuda:12.6.3-devel-ubuntu24.04 > pull.log 2>&1 &
```

pull 支持断点续传，中断后重跑会接着下。

确认基础镜像已就位：

```bash
docker images nvcr.io/nvidia/cuda:12.6.3-devel-ubuntu24.04
```

## 构建镜像

### 使用 docker-compose（推荐）

```bash
docker-compose build
```

默认 CUDA 架构为 `75`（Quadro RTX 6000 / RTX 20xx / T4）。如果 GPU 不同，在 `docker-compose.yml` 的 `build.args` 修改 `CUDA_ARCHITECTURES`，或命令行覆盖：

```bash
# RTX 30xx / A100 / A6000
docker-compose build --build-arg CUDA_ARCHITECTURES=86

# RTX 40xx / L4 / L40
docker-compose build --build-arg CUDA_ARCHITECTURES=89

# 多架构（分号分隔）
docker-compose build --build-arg CUDA_ARCHITECTURES="75;86;89"
```

GPU 架构对照表：

| GPU 系列 | Compute Capability | CUDA_ARCHITECTURES |
|---------|-------------------|-------------------|
| GTX 10xx / Titan Xp | 6.1 | 61 |
| RTX 20xx / Quadro RTX 6000 / T4 | 7.5 | 75 |
| RTX 30xx / A100 / A6000 | 8.6 | 86 |
| RTX 40xx / L4 / L40 | 8.9 | 89 |
| H100 | 9.0 | 90 |

查询 GPU 架构：

```bash
nvidia-smi --query-gpu=name,compute_cap --format=csv
```

### 使用 docker build

在仓库根目录执行（`Dockerfile` 和 `.dockerignore` 都在根目录，不在 `docker/` 下）：

```bash
docker build \
  --build-arg CUDA_ARCHITECTURES=75 \
  --build-arg USER_ID=$(id -u) \
  --build-arg GROUP_ID=$(id -g) \
  -t mvs-build:cuda12.6-ubuntu24.04 \
  .
```

## 环境变量

| 变量 | 默认值 | 作用 |
| --- | --- | --- |
| `MVS_WORK_DIR` | `/data/taoguo/mvs-workspace` | 宿主机产物根目录，`build/`、`outputs/`、`packages/` 挂载到它下面。**必须是本地盘** |
| `DATA_ROOT` | `/workspace/data` | 容器内输入数据目录（图片 + `cameras.json`） |
| `RUN_NAME` | `default` | 本次重建的名字，产物落在 `outputs/<RUN_NAME>/` |
| `OUTPUT_DIR` | `outputs/<RUN_NAME>` | 直接指定输出目录，优先于 `RUN_NAME` |
| `MVS_ENABLE_ONNX` | `0` | 设为 `1` 启用 ONNX Runtime，见下文 |
| `JOBS` | 容器可见 CPU 数 | 编译并行度，由 `docker/entrypoint.sh` 设置 |
| `MVS_MAX_THREADS` | 配置文件值 | COLMAP/OpenMVS 运行时线程数 |

### `MVS_WORK_DIR` 必须指向本地盘

`build/`、`outputs/`、`packages/` 不放在项目目录里，而是统一挂到 `MVS_WORK_DIR` 下：

```yaml
- ${MVS_WORK_DIR:-/data/taoguo/mvs-workspace}/build:/workspace/build
- ${MVS_WORK_DIR:-/data/taoguo/mvs-workspace}/outputs:/workspace/outputs
- ${MVS_WORK_DIR:-/data/taoguo/mvs-workspace}/packages:/workspace/packages
```

原因有两条：

1. **可执行性**。NFS、yrfs 等网络文件系统可能不支持 `execve()`。编译出的 `colmap`、`mvs_reconstruct` 和 OpenMVS 工具放在上面无法运行，报 `Invalid argument`（不是权限问题，`chmod +x` 无效）。`build/` 和 `packages/` 里都是可执行文件。
2. **性能**。深度图和稠密点云体积大、随机写多，本地盘远快于网络盘。

排查某个路径是否支持执行：

```bash
cp /bin/true /path/to/check/t && chmod +x /path/to/check/t && /path/to/check/t \
  && echo "可执行" || echo "不可执行"; rm -f /path/to/check/t
```

如果项目源码本身也在这类文件系统上，shell 脚本不能直接执行，要显式用解释器：

```bash
bash scripts/build.sh        # 可以
./scripts/build.sh           # bad interpreter: Invalid argument
```

项目目录在本地盘时，把上面三行挂载注释掉即可让产物回到项目目录内。

## 运行容器

### 使用 docker-compose（推荐）

```bash
# 一次性执行构建 / 重建
docker-compose run --rm mvs bash scripts/build.sh
docker-compose run --rm mvs bash scripts/reconstruct.sh

# 进入容器 shell 交互操作
docker-compose run --rm mvs

# 指定产物目录和数据集
MVS_WORK_DIR=/mnt/fast docker-compose run --rm mvs bash scripts/build.sh
docker-compose run --rm -e RUN_NAME=exp1 -e DATA_ROOT=/workspace/data/exp1 \
  mvs bash scripts/reconstruct.sh
```

`docker-compose run` 默认不退出（配了 `tty`），交互模式下记得用 `exit` 退出，
容器会因 `--rm` 自动删除。

### 使用 docker run

`docker run` 需要手动补齐 compose 里的挂载，否则产物会落到不可执行的位置：

```bash
docker run --rm -it \
  --gpus all \
  --shm-size 8g \
  -v $(pwd):/workspace \
  -v /data/taoguo/mvs-workspace/build:/workspace/build \
  -v /data/taoguo/mvs-workspace/outputs:/workspace/outputs \
  -v /data/taoguo/mvs-workspace/packages:/workspace/packages \
  -v $HOME/.ssh:/home/ubuntu/.ssh:ro \
  -w /workspace \
  mvs-build:cuda12.6-ubuntu24.04 \
  bash
```

`~/.ssh` 只读挂载是因为 COLMAP 的 poselib/faiss 依赖走 git+ssh 拉取，需要
GitHub 私钥。已有 `3rd/` 完整源码和 `build/` 缓存时可以省略。

参数说明：

- `--gpus all`：映射所有 GPU（需要 NVIDIA Container Toolkit）。
- `--shm-size 8g`：OpenMVS/COLMAP 部分阶段使用共享内存，默认 64MB 可能不足。
- `-v $(pwd):/workspace`：挂载源码目录，构建产物留在宿主机。

## 构建项目

容器内执行：

```bash
# 拉取第三方源码（如果 3rd/ 下源码不存在）
bash scripts/fetch_3rdparty.sh

# 构建主项目和第三方依赖
bash scripts/build.sh

# 构建并打包
bash scripts/build.sh --package 0.1.0
```

`scripts/build.sh` 会自动检测：

- **CUDA 架构**：从 `CMAKE_TOOLCHAIN_FILE` 环境变量读取，已在 Dockerfile 中配置为构建参数传入的值。
- **并行任务数**：从 `JOBS` 环境变量读取，entrypoint 自动设置为 `nproc`。
- **PoseLib / faiss**：如果 `3rd/PoseLib` 和 `3rd/faiss` 存在，作为本地源码传给 COLMAP，构建期不联网；否则由 COLMAP 通过 git+ssh 拉取（需要挂载 `~/.ssh`）。
- **ONNX Runtime**：默认关闭。用 `MVS_ENABLE_ONNX=1` 且 `3rd/` 下有匹配平台的包时才启用，见下文。

### 离线构建

`3rd/` 下源码齐备时（COLMAP、OpenMVS、vcglib、PoseLib、faiss）构建完全不需要网络，也不需要挂载 `~/.ssh`。在有网的机器上执行一次 `bash scripts/fetch_3rdparty.sh`，把 `3rd/` 一起带到目标机器即可。

## ONNX Runtime（可选，默认关闭）

COLMAP 的 ONNX Runtime 支持用于深度学习特征提取（如 SuperPoint），本项目核心流程（SIFT 特征 + SfM + MVS）不依赖，因此 `scripts/build.sh` 默认传 `-DONNX_ENABLED=OFF -DFETCH_ONNX=OFF`，macOS 和 Linux 一致。关闭后不下载 onnxruntime，打包产物里也不含 `lib/onnxruntime`。

如需启用，先下载对应平台的包到 `3rd/`，再用 `MVS_ENABLE_ONNX=1` 构建：

### CPU 版本（约 8MB）

```bash
cd 3rd
wget https://github.com/microsoft/onnxruntime/releases/download/v1.24.4/onnxruntime-linux-x64-1.24.4.tgz
tar xzf onnxruntime-linux-x64-1.24.4.tgz
rm onnxruntime-linux-x64-1.24.4.tgz
```

### GPU 版本（约 250MB，需要 CUDA 12）

```bash
cd 3rd
wget https://github.com/microsoft/onnxruntime/releases/download/v1.24.4/onnxruntime-linux-x64-gpu-1.24.4.tgz
tar xzf onnxruntime-linux-x64-gpu-1.24.4.tgz
rm onnxruntime-linux-x64-gpu-1.24.4.tgz
```

然后重新构建，注意必须带 `MVS_ENABLE_ONNX=1`，只放置文件不会自动启用：

```bash
bash scripts/build.sh --clean
MVS_ENABLE_ONNX=1 bash scripts/build.sh
```

## 运行重建

```bash
# 容器内运行（需要先 docker-compose run --rm mvs 进入容器）
scripts/reconstruct.sh

# 或在容器外通过 docker-compose 一次性执行
docker-compose run --rm mvs scripts/reconstruct.sh
```

默认读取 `data/images` 和 `data/cameras.json`，输出到 `outputs/default`。覆盖配置：

```bash
docker-compose run --rm mvs bash -c "
  RUN_NAME=my-run \
  MVS_CONFIG=/workspace/config/reconstruction.json \
  MVS_MAX_THREADS=32 \
  scripts/reconstruct.sh
"
```

## 常见问题

### 1. `docker: Error response from daemon: could not select device driver "nvidia"`

**原因**：NVIDIA Container Toolkit 未安装或 Docker daemon 未重启。

**解决**：参考上文「前置条件 → NVIDIA Container Toolkit」安装并重启 Docker。

### 2. `CMake Error: CMAKE_CUDA_ARCHITECTURES is empty for language "CUDA"`

**原因**：镜像构建时 `CUDA_ARCHITECTURES` 参数未传递，或 toolchain 文件未生效。

**解决**：

```bash
docker-compose build --build-arg CUDA_ARCHITECTURES=75
```

或直接在 `docker-compose.yml` 的 `build.args` 修改。

### 3. 构建产物属主为 root

**原因**：容器内用户 UID/GID 与宿主机不匹配。

**解决**：在 `docker-compose.yml` 的 `build.args` 设置正确的 `USER_ID` 和 `GROUP_ID`：

```yaml
build:
  args:
    USER_ID: "1000"  # 改为你的 $(id -u)
    GROUP_ID: "1005" # 改为你的 $(id -g)
```

重新构建镜像：

```bash
docker-compose build
```

### 3b. `bad interpreter: Invalid argument` 或二进制 `Invalid argument`

**原因**：文件所在的文件系统不支持 `execve()`，常见于 NFS、yrfs 等网络存储。
注意这不是权限问题，`chmod +x` 没用；报错码是 `EINVAL` 而非 `EACCES`。

**解决**：脚本用解释器显式调用，产物目录指向本地盘（见上文
[`MVS_WORK_DIR` 必须指向本地盘](#mvs_work_dir-必须指向本地盘)）。

```bash
bash scripts/build.sh   # 而不是 ./scripts/build.sh
```

### 3c. 挂载目录 `Permission denied`，或 CMake 报 `Unable to (re)create the private pkgRedirects directory`

**原因**：Docker 创建宿主机上不存在的挂载点时用的是 root 属主，容器内非 root
用户写不进去。CMake 抛出的
`Unable to (re)create the private pkgRedirects directory: /workspace/build/CMakeFiles/pkgRedirects`
是同一个问题，只是错误信息看不出根因。

改了 `MVS_WORK_DIR`（或改了 `docker-compose.yml` 里的默认值）而新路径还没建过，
最容易踩到这个坑。entrypoint 现在会在启动时检查这三个目录的可写性并直接给出
修复命令，不会再让它以 CMake 错误的形式暴露出来。

**解决**：先在宿主机建好目录（属主为你自己），再启动容器。

```bash
install -d /data/taoguo/mvs-workspace/{build,outputs,packages}
```

目录已经被 docker 建成 root 属主了，用 `chown` 修：

```bash
sudo chown -R "$(id -u):$(id -g)" /data/taoguo/mvs-workspace
```

没有 sudo 权限时，只要目录是空的就可以直接重建（删除权限来自父目录，不需要
对目录本身有写权限）：

```bash
rmdir /data/taoguo/mvs-workspace/{build,outputs,packages}
mkdir -p /data/taoguo/mvs-workspace/{build,outputs,packages}
```

镜像里 `/workspace/build`、`/workspace/outputs`、`/workspace/packages` 已经预建
并设为构建时传入的 `USER_ID:GROUP_ID`，但 bind mount 会用宿主机目录的属主覆盖
镜像里的设置，所以宿主机侧的属主才是决定性的。新增其他挂载点时需要同样处理。

### 3d. `Error while fetching server API version: ... PermissionError(13)`

**原因**：当前用户不在 `docker` 组，或刚加入但登录会话还没重新加载组成员关系
（`id` 看不到 `docker`，但 `getent group docker` 里有你）。

**解决**：重新登录使其生效。不想重登时可以临时开一个带该组的子 shell：

```bash
sg docker -c "docker-compose run --rm mvs bash scripts/build.sh"
```

### 4. `nvcc fatal: Unsupported gpu architecture 'compute_XX'`

**原因**：`CUDA_ARCHITECTURES` 与 CUDA 12.6 不兼容（如 `compute_30`/`35` 已移除）。

**解决**：CUDA 12.x 最低支持 `compute_50`（Maxwell），检查 GPU 型号并设置合适的架构值。

### 5. `fatal error: opencv2/opencv.hpp: No such file or directory`

**原因**：OpenCV 未安装或路径未正确配置。

**解决**：Dockerfile 已包含 `libopencv-dev`，重新构建镜像：

```bash
docker-compose build --no-cache
```

### 6. 共享内存不足导致 `Bus error (core dumped)`

**原因**：默认 `/dev/shm` 只有 64MB，OpenMVS 密集点云阶段可能需要数 GB。

**解决**：在 `docker-compose.yml` 已设置 `shm_size: "8gb"`，如需调整：

```yaml
shm_size: "16gb"
```

或 `docker run` 加 `--shm-size 16g`。

### 7. `docker-compose build` 长时间停在某一层 `pulling`

**原因**：基础镜像最大层 2.4 GB，国内直连 nvcr.io 的 blob CDN 实测 ~95 KB/s，需要 7 小时以上。进度条看似不动，实际在龟速下载。

**确认方式**：

```bash
# registry API 能通但 blob 慢，说明是 CDN 限速而非封锁
curl -sS -o /dev/null -w 'http=%{http_code} connect=%{time_connect}s\n' https://nvcr.io/v2/
```

**解决**：参考上文「前置条件 → 基础镜像」，配 daemon 代理或从其他机器 `docker save`/`load` 迁移。`registry-mirrors` 对 nvcr.io 无效。

## 镜像内容

- **基础镜像**：`nvcr.io/nvidia/cuda:12.6.3-devel-ubuntu24.04`（NVIDIA NGC）
- **操作系统**：Ubuntu 24.04 LTS
- **CUDA**：12.6.3（nvcc + cuBLAS + cuRAND + ...）
- **CMake**：3.28（满足项目 >= 3.24 要求）
- **编译器**：GCC 13
- **依赖库**：
  - Boost 1.83（program_options / graph / system / filesystem / iostreams / serialization）
  - Eigen 3.4
  - OpenCV 4.9
  - OpenImageIO 2.4
  - Ceres Solver 2.1
  - CGAL 5.6
  - Qt6 6.4
  - OpenBLAS + LAPACK（faiss 依赖）
  - 其他：glog, gflags, gtest, glew, sqlite3, metis, suitesparse, jpeg-xl, curl, openssl

## 脚本修改说明

为支持 Linux 容器构建，`scripts/build.sh` 做了两处修改（不影响 macOS 使用）：

1. **JOBS 探测**：`build.sh:7` 原始代码在 Linux 上回退到固定值 4，现已改为 `nproc` 自动探测。
2. **ONNX Runtime 平台过滤**：`build.sh:166` `find_local_onnxruntime()` 原始代码会在 Linux 上优先选中 macOS arm64 包（因其在候选列表前部），导致链接失败。现已改为按 `uname -s` 和 `uname -m` 过滤平台匹配的候选。

这两处修改已提交到项目仓库，不需要额外打补丁。

## 性能建议

- **多 GPU 场景**：COLMAP/OpenMVS 默认使用 `CUDA_VISIBLE_DEVICES=0`，如需指定 GPU：
  
  ```bash
  docker-compose run --rm -e CUDA_VISIBLE_DEVICES=1,2 mvs scripts/reconstruct.sh
  ```

- **线程数调优**：容器默认使用全部 CPU 核心，如需限制：

  ```bash
  docker-compose run --rm mvs bash -c "MVS_MAX_THREADS=16 scripts/reconstruct.sh"
  ```

- **网络代理**：FetchContent 拉取 PoseLib/faiss 需要访问 GitHub。如有代理：

  ```bash
  docker-compose run --rm -e http_proxy=http://host.docker.internal:7890 mvs scripts/build.sh
  ```

## 清理

```bash
# 删除镜像
docker rmi mvs-build:cuda12.6-ubuntu24.04

# 删除容器和卷
docker-compose down -v

# 删除构建缓存
docker builder prune
```
