# 多视角三维重建项目

本项目用于基于单目相机拍摄的多视角图片，对物体或场景进行三维重建。核心重建流程由 C++ 可执行程序负责，驱动从源码编译的 COLMAP 4.1.1 和 OpenMVS 2.4.0；Python 只用于 I/O 校验和简单的命令行/UI 编排。

## 目录结构

- `data/images/`：输入图片，格式说明见 [`data/README.md`](data/README.md)。
- `data/cameras.json`：相机参数，用于初始化 COLMAP 相机模型，字段说明见 [`data/README.md`](data/README.md)。
- `3rd/colmap-4.1.1`：COLMAP 源码，固定在 `4.1.1` 标签。
- `3rd/openMVS-2.4.0`：OpenMVS 源码，固定在 `v2.4.0` 标签。
- `3rd/vcglib`：OpenMVS 需要的 VCG header-only 源码依赖。
- `3rd/nlohmann`：C++ 核心使用的 nlohmann/json 头文件。
- `3rd/PoseLib`、`3rd/faiss`：COLMAP 通过 FetchContent 依赖的两个库，预置在此避免构建时联网。
- `src/cpp`：C++ 编译核心，包括重建流程、命令规划、日志和 manifest 输出。
- `src/python`：Python I/O 和 CLI 辅助代码。
- `scripts`：拉取源码、构建和运行脚本。
- `docker`：Docker 构建配置和文档，支持 CUDA 加速的 Ubuntu 容器环境，详见 [`docker/README.md`](docker/README.md)。
- `outputs/<run-name>`：每次重建的产物和日志。

说明：Git 仓库中不提交 `data/` 下的实际数据、`packages/`、`build/` 和 `outputs/`。其中 `data/` 用于本地输入图片和相机参数，仅保留 [`data/README.md`](data/README.md)；`packages/` 是打包输出目录，`build/` 是本地编译目录，`outputs/` 是重建运行产物。

用 Docker 时，`build/`、`outputs/`、`packages/` 这三个目录是挂载点，实际内容在宿主机的 `MVS_WORK_DIR`（默认 `/data/taoguo/mvs-workspace`）下，不在项目目录里。文档中出现的这些相对路径在容器内依然有效。详见下面的[产物目录 `MVS_WORK_DIR`](#产物目录-mvs_work_dir)。

## 核心组件职责

### COLMAP / pycolmap 4.1.1

COLMAP 负责 SfM（Structure from Motion，运动恢复结构）阶段，用多张普通图片恢复相机位姿和稀疏三维结构。`pycolmap` 是 COLMAP 的 Python 绑定源码，本项目从 COLMAP 4.1.1 源码仓库中获取该版本对应的绑定代码；当前核心计算仍由 C++ 可执行程序驱动，Python 只做 I/O 和 UI 辅助。

COLMAP / pycolmap 对应的主要能力：

- 读取多视角图片。
- 提取图像特征点。
- 匹配不同图片之间的特征点。
- 估计每张图片的相机姿态，即位置和朝向。
- 估计或优化相机内参。
- 三角化生成稀疏点云。
- 输出 COLMAP sparse model。

简单理解：COLMAP / pycolmap 解决“每张照片是从哪里、朝哪个方向拍的，以及场景中大概有哪些三维点”。

### OpenMVS 2.4.0

OpenMVS 负责 MVS（Multi-View Stereo，多视角立体）阶段，接收 COLMAP 输出的相机位姿、相机参数和稀疏模型，继续生成稠密点云、网格和纹理模型。

OpenMVS 对应的主要能力：

- 将 COLMAP 输出转换为 `.mvs` 场景文件。
- 基于多视角图片生成稠密点云。
- 从稠密点云重建三角网格。
- 可选地细化网格。
- 为网格生成纹理。
- 输出可查看或继续后处理的三维模型文件。

简单理解：OpenMVS 解决“把 COLMAP 的稀疏结果变成完整、可视化、可贴纹理的三维模型”。

## 算法流程

整体流程如下：

```text
多张输入图片
  ↓
读取相机参数 data/cameras.json
  ↓
COLMAP / pycolmap：特征提取
  ↓
COLMAP / pycolmap：特征匹配
  ↓
COLMAP / pycolmap：增量 SfM、相机位姿估计、稀疏点云重建
  ↓
COLMAP：图像去畸变和 dense workspace 准备
  ↓
OpenMVS：InterfaceCOLMAP 转换为 scene.mvs
  ↓
OpenMVS：DensifyPointCloud 生成稠密点云
  ↓
OpenMVS：ReconstructMesh 生成三角网格
  ↓
OpenMVS：TextureMesh 生成纹理模型
  ↓
输出 manifest.json、日志、稠密点云、网格和纹理模型
```

对应到当前 C++ pipeline，主要阶段是：

- `feature_extractor`：提取图片特征。
- `exhaustive_matcher`：对图片进行穷举特征匹配。
- `mapper`：执行增量 SfM，生成稀疏模型。
- `image_undistorter`：生成 COLMAP dense workspace。默认调 COLMAP 子进程（41 张图约 74s）；
  加 `--use-opencv-undistort true` 换成进程内 OpenCV 实现（约 5s），几何与 COLMAP 一致
  （复用 `colmap::UndistortCamera()`，同样裁掉去畸变黑边），下游阶段完全不变。
- `InterfaceCOLMAP`：把 COLMAP dense workspace 转为 OpenMVS 场景。
- `DensifyPointCloud`：生成稠密点云。
- `ReconstructMesh`：生成三角网格。
- `TextureMesh`：生成带纹理的模型。

## 拉取第三方源码

```bash
scripts/fetch_3rdparty.sh
```

只检查固定版本标签是否存在：

```bash
scripts/fetch_3rdparty.sh --check-only
```

说明：COLMAP 4.1.1 从 `git@github.com:colmap/colmap.git` 拉取。这个仓库包含该版本对应的 pycolmap 绑定源码；独立的 `colmap/pycolmap` 仓库没有 `4.1.1` 标签。

OpenMVS 2.4.0 从 `git@github.com:cdcseacave/openMVS.git` 拉取，标签为 `v2.4.0`。

OpenMVS 还需要 VCG header-only 源码依赖，脚本会拉取到：

```text
3rd/vcglib
```

COLMAP 自身还会通过 CMake `FetchContent` 拉取 PoseLib 和 faiss。脚本一并预置到：

```text
3rd/PoseLib    固定 commit fa7280fe
3rd/faiss      固定 tag v1.14.1
```

`scripts/build.sh` 检测到这两个目录就把它们作为源码传给 COLMAP，构建期不再联网；目录不存在时回退到网络拉取（默认走 git+ssh，见[依赖拉取方式](#依赖拉取方式)）。两个目录齐备时整个构建可以完全离线。

版本必须与 `3rd/colmap-4.1.1/src/thirdparty/CMakeLists.txt` 里声明的一致，升级 COLMAP 时要同步更新 `scripts/fetch_3rdparty.sh` 里的 `POSELIB_COMMIT` 和 `FAISS_TAG`。

当前 `3rd/` 中的第三方源码按固定版本 vendored 到项目里，并已清理 `.git` 元数据、文档、CI、Docker、示例和测试数据。`scripts/fetch_3rdparty.sh` 遇到已存在但没有 `.git` 的源码目录时会直接复用；需要重新下载时，先删除对应目录再执行脚本。

### 依赖拉取方式

PoseLib 和 faiss 的获取方式按优先级依次是：

1. **本地预置源码**（默认，最快且离线）。`3rd/PoseLib` 和 `3rd/faiss` 存在时直接使用。
2. **git+ssh 克隆**。国内网络直连 `codeload.github.com` 下载 `.zip` 归档常常超时，SSH 稳定得多，因此这是网络拉取时的默认方式，需要可用的 GitHub SSH 密钥。
3. **HTTPS 归档**，上游原始方式。用 `-DFETCH_DEPS_OVER_SSH=OFF` 切回。

## 构建依赖

### Docker（推荐）

提供开箱即用的 Ubuntu 24.04 + CUDA 12.6 容器环境，包含所有编译和运行依赖。详见 [`docker/README.md`](docker/README.md)。

快速开始：

```bash
# 构建镜像（需要 NVIDIA Container Toolkit）
docker-compose build

# 一次性执行构建
docker-compose run --rm mvs bash scripts/build.sh

# 或进入容器交互操作
docker-compose run --rm mvs
```

注意容器内用 `bash scripts/build.sh` 而不是 `./scripts/build.sh`，原因见下面的
「产物目录 `MVS_WORK_DIR`」。

#### 产物目录 `MVS_WORK_DIR`

`build/`、`outputs/`、`packages/` 三个目录不落在项目目录里，而是统一挂载到
`MVS_WORK_DIR`（默认 `/data/taoguo/mvs-workspace`）下的同名子目录：

```bash
# 换到别的位置，三个目录会一起跟过去
MVS_WORK_DIR=/your/local/path docker-compose run --rm mvs bash scripts/build.sh
```

**`MVS_WORK_DIR` 必须指向本地磁盘（ext4/xfs 等），不能是网络文件系统。**
两个原因：

- 可执行性。部分网络文件系统（NFS、yrfs 等）不支持 `execve()`，编译出的
  `colmap`、`mvs_reconstruct` 和 OpenMVS 工具放在上面无法运行，报
  `bad interpreter: Invalid argument` 或 `Invalid argument`。`build/` 和
  `packages/` 里都是可执行文件。同样的原因，如果项目源码本身也在这类文件系统上，
  shell 脚本不能直接 `./scripts/build.sh` 执行，要写成 `bash scripts/build.sh`。
- 性能。重建过程中的深度图和稠密点云体积大、随机写多，本地盘远快于网络盘。

如果你的项目目录本来就在本地盘上，也可以把 `docker-compose.yml` 里这三行挂载
注释掉，让产物回到项目目录内。

#### 数据目录 `DATA_ROOT`

输入图片和相机参数默认从项目根的 `data/` 读取，容器里是 `/workspace/data`。
用别处的数据集时通过 `DATA_ROOT` 指定：

```bash
DATA_ROOT=/workspace/data/my-dataset docker-compose run --rm mvs bash scripts/reconstruct.sh
```

`DATA_ROOT` 是容器内的路径。指向项目外的数据时，记得先在
`docker-compose.yml` 里加一条对应的挂载，否则容器里看不到。

打包产物（`scripts/build.sh --package <version>`）里的 `scripts/reconstruct.sh`
默认把 `DATA_ROOT` 解析为包目录的上两级 `../../data` —— 这是为了让包不必自带数据。
把包拷到别的位置部署时，这个相对路径通常不再成立，需要显式传 `DATA_ROOT`。

### macOS

macOS 上源码构建 COLMAP 需要 Homebrew 依赖。可按 COLMAP 官方文档安装：

```bash
brew install \
  cmake \
  ninja \
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
brew link --force libomp
```

OpenMVS 还会使用 Eigen、OpenCV、CGAL、Boost、Ceres 等依赖。当前脚本会优先构建主项目，然后尝试配置和构建第三方源码；如果本机缺依赖，会在终端列出需要安装的 Homebrew formula。

### Linux，以 Ubuntu 为例

Ubuntu 上源码构建 COLMAP 可按官方文档安装以下 apt 依赖：

```bash
sudo apt-get update
sudo apt-get install -y \
  git \
  cmake \
  ninja-build \
  build-essential \
  libboost-program-options-dev \
  libboost-graph-dev \
  libboost-system-dev \
  libeigen3-dev \
  libopenimageio-dev \
  openimageio-tools \
  libjxl-dev \
  libmetis-dev \
  libgoogle-glog-dev \
  libgtest-dev \
  libgmock-dev \
  libsqlite3-dev \
  libglew-dev \
  qt6-base-dev \
  libqt6opengl6-dev \
  libqt6openglwidgets6 \
  qt6-svg-dev \
  libcgal-dev \
  libceres-dev \
  libsuitesparse-dev \
  libcurl4-openssl-dev \
  libssl-dev \
  libmkl-full-dev
```

COLMAP 官方文档还建议为 Ubuntu 的 OpenImageIO CMake 配置创建 OpenCV include 目录：

```bash
sudo mkdir -p /usr/include/opencv4
```

如果系统没有 Qt 6，也可以改装 Qt 5 相关依赖：

```bash
sudo apt-get install -y qtbase5-dev libqt5opengl5-dev libqt5svg5-dev
```

OpenMVS 在 Debian/Ubuntu 环境下还需要常见构建工具和图形/系统库：

```bash
sudo apt-get install -y \
  autoconf \
  autoconf-archive \
  automake \
  libtool \
  bison \
  gfortran \
  pkg-config \
  libxi-dev \
  libx11-dev \
  libxft-dev \
  libxtst-dev \
  libxext-dev \
  libxrandr-dev \
  libxinerama-dev \
  libxcursor-dev \
  xorg-dev \
  libgl-dev \
  libglu1-mesa-dev \
  nasm
```

如果需要 CUDA 加速，可额外安装 Ubuntu 默认 CUDA 包，或手动安装 NVIDIA CUDA：

```bash
sudo apt-get install -y \
  nvidia-cuda-toolkit \
  nvidia-cuda-toolkit-gcc
```

## 构建

构建主项目和第三方依赖：

```bash
scripts/build.sh
```

构建完成后同时打包：

```bash
scripts/build.sh --package 0.1.0
```

只使用已有构建产物打包，不重新编译：

```bash
scripts/build.sh --package-only 0.1.0
```

清理本地历史编译文件：

```bash
scripts/build.sh --clean
```

`--clean` 只删除根目录下的 `build/`，不会删除 `3rd/` 源码、`data/` 输入数据、`packages/` 打包产物或 `outputs/` 重建结果。

包会输出到：

```text
packages/mvs-0.1.0
```

包内主要内容：

- `bin/mvs_reconstruct`：主项目 C++ 可执行程序。
- `bin/colmap`：COLMAP 4.1.1 命令行程序。
- `bin/InterfaceCOLMAP`、`bin/DensifyPointCloud`、`bin/ReconstructMesh`、`bin/TextureMesh`：OpenMVS 2.4.0 所需工具。
- `scripts/reconstruct.sh`：包内可直接运行的重建脚本。
- `scripts/run_python_ui.py`：Python 编排入口脚本。
- `src/python`：Python I/O 和 CLI 辅助代码。
- `lib/onnxruntime`：本地 ONNX Runtime 动态库，仅在 `MVS_ENABLE_ONNX=1` 构建时存在，默认没有这个目录。
- `manifest.json`：包版本、COLMAP/OpenMVS 固定版本和目录布局。

打包产物不复制 `data/`。包内 `scripts/reconstruct.sh` 把数据目录解析为包目录的上两级 `../../data`，也就是包放在项目 `packages/` 下时能命中主项目的 `data/`。

把包拷到其它位置部署时，这个相对路径通常不再成立，需要显式指定：

```bash
DATA_ROOT=/path/to/data /opt/mvs-0.1.0/scripts/reconstruct.sh
```

包里的 `bin/` 是可执行文件，解压位置必须支持执行（不能是部分网络文件系统），否则会报 `Invalid argument`。

主项目 C++ 可执行文件位置：

```text
build/mvs_reconstruct
```

脚本会在 `3rd/` 下存在源码时尝试构建 COLMAP 和 OpenMVS。

### ONNX Runtime（默认关闭）

COLMAP 的 ONNX Runtime 用于深度学习特征提取（如 SuperPoint），本项目流程（SIFT + SfM + MVS）不依赖，因此 macOS 和 Linux 都默认关闭，构建时不下载 onnxruntime，打包产物里也不含 `lib/onnxruntime`。

仓库不再随附 onnxruntime 预编译包（macOS arm64 那份含两个 35MB dylib 和调试符号，共 120MB，默认关闭后属于无用体积）。需要启用时自行从 [onnxruntime releases](https://github.com/microsoft/onnxruntime/releases/tag/v1.24.4) 下载对应平台的包，解压到 `3rd/`（目录中需包含 `include/` 和 `lib/`）：

```text
3rd/onnxruntime-osx-arm64-1.24.4        # macOS arm64
3rd/onnxruntime-linux-x64-gpu-1.24.4    # Linux x64，CUDA 12
3rd/onnxruntime-linux-x64-1.24.4        # Linux x64，CPU
```

然后用 `MVS_ENABLE_ONNX=1` 构建。只放置文件不会自动启用：

```bash
MVS_ENABLE_ONNX=1 bash scripts/build.sh
```

启用后 `scripts/build.sh` 会检测到本地包，生成 COLMAP 需要的 include 兼容层，并关闭 COLMAP 的 ONNX Runtime 在线下载。找不到匹配当前平台的本地包时，仍会回退到关闭状态。

## 运行重建

```bash
scripts/reconstruct.sh
```

默认配置文件位于：

```text
config/reconstruction.json
```

直接运行 `build/mvs_reconstruct` 且不传参数时，会读取这个配置文件。配置文件支持 JSONC 风格的 `//` 注释；每个配置项都写有含义、候选值和命令行覆盖项。`scripts/reconstruct.sh` 也会把它作为基础配置，再用脚本计算出的数据目录、输出目录和二进制路径覆盖；`MVS_CONFIG=/path/to/config.json scripts/reconstruct.sh` 可以换用其它配置文件。配置文件适合放常用效率参数，例如 `max_threads`、`undistort_copy_policy`、`reuse_existing`、`remove_depth_maps`、`matcher` 和 `densify_geometric_iters`。

效率优化过程、基线和阶段成果记录在 [`EFFICIENCY_OPTIMIZATION.md`](EFFICIENCY_OPTIMIZATION.md)。

运行时终端会按阶段输出进度、日志路径和耗时，例如：

```text
== 多视角三维重建开始 ==
[1/11] 多张输入图片: data/images
[2/11] 读取相机参数: data/cameras.json 完成 (0.01s)，图片数: 40，已注册: 40
[3/11] COLMAP / pycolmap：特征提取
      日志: outputs/default/logs/feature_extractor.log
      完成 (12.34s)
...
[11/11] 输出 manifest.json、日志、稠密点云、网格和纹理模型
== 多视角三维重建完成，总耗时 123.45s ==
```

每个阶段的耗时、子进程 CPU 时间和峰值内存也会写入 `outputs/<run-name>/manifest.json` 的 `stages` 数组。某一步失败时，终端会显示失败阶段、耗时、退出码和对应日志文件路径。

pipeline 会在 `outputs/<run-name>/colmap/image_list.txt` 写入按文件名排序的图片列表，并传给 COLMAP `feature_extractor` 和 `mapper`。这不会修改 `data/`，但可以避免目录遍历顺序导致的 image ID、mapper 初始化和性能结果波动。

可选覆盖项：

```bash
RUN_NAME=my-run scripts/reconstruct.sh
MVS_CONFIG=/path/to/reconstruction.json scripts/reconstruct.sh
COLMAP_BIN=/path/to/colmap OPENMVS_BIN_DIR=/path/to/openmvs/bin scripts/reconstruct.sh
MVS_MAX_THREADS=8 scripts/reconstruct.sh
UNDISTORT_COPY_POLICY=COPY scripts/reconstruct.sh
MVS_REUSE_EXISTING=1 scripts/reconstruct.sh
MVS_REMOVE_DEPTH_MAPS=0 scripts/reconstruct.sh
MVS_MATCHER=exhaustive MVS_DENSIFY_GEOMETRIC_ITERS=1 scripts/reconstruct.sh
MVS_MATCHER=sequential MVS_SEQUENTIAL_OVERLAP=40 MVS_SEQUENTIAL_QUADRATIC_OVERLAP=0 scripts/reconstruct.sh
MVS_DENSIFY_MAX_RESOLUTION=2048 scripts/reconstruct.sh
MVS_DENSIFY_RESOLUTION_LEVEL=2 scripts/reconstruct.sh
MVS_DENSIFY_ITERS=2 scripts/reconstruct.sh
MVS_DENSIFY_NUMBER_VIEWS_FUSE=3 scripts/reconstruct.sh
MVS_GENERATE_TEXTURE=0 scripts/reconstruct.sh
MVS_TEXTURE_PATCH_PACKING_HEURISTIC=100 scripts/reconstruct.sh
```

`image_undistorter` 默认使用 `HARD_LINK` 生成 dense workspace，减少不必要的图片复制和磁盘 IO；如果输出目录和输入图片不在同一文件系统，或需要完全独立的输出目录，可以把 `UNDISTORT_COPY_POLICY` 设置为 `COPY`。`MVS_MAX_THREADS` 会传给 COLMAP/OpenMVS 的线程参数；不设置时沿用各工具默认线程策略。

`MVS_REUSE_EXISTING=1` 会复用同一输出目录里已经成功生成的阶段产物。复用需要同时满足阶段 marker、命令签名、输入图片/相机文件快照和目标产物都匹配；默认关闭，避免意外使用旧结果。

`DensifyPointCloud` 默认使用 `--remove-dmaps 1` 删除融合后的 `.dmap` 中间文件，减少输出目录体积；如果需要保留深度图用于调试或后续分析，可以设置 `MVS_REMOVE_DEPTH_MAPS=0`。

当前默认 matcher 仍使用 `exhaustive`。虽然这组图片有明显拍摄顺序，实测 `sequential_matcher` 默认 quadratic overlap 采样在 overlap 12 和 40 下都会明显减少可用位姿/稀疏点，最终质量风险较高。若图片有顺序但前后位姿变化较大，可以试 `MVS_MATCHER=sequential MVS_SEQUENTIAL_OVERLAP=40 MVS_SEQUENTIAL_QUADRATIC_OVERLAP=0`，它会关闭远邻稀疏采样，匹配更多顺序邻域 pair，但耗时接近 exhaustive。当前推荐的 Densify 默认值是 `MVS_DENSIFY_GEOMETRIC_ITERS=1`，在本数据集上把总耗时从约 21m34s 降到约 16m37s，dense point 数量下降约 3.1%。

Texture 阶段默认使用 `MVS_TEXTURE_PATCH_PACKING_HEURISTIC=100`。在同一 `geom1` mesh 上单独 A/B，OpenMVS 默认值 3 的 TextureMesh wall time 为 `239.90s`，候选值 100 为 `187.11s`，输出顶点/面数和纹理文件数量一致。当前默认配置端到端复测为约 `19m00s`；由于 Densify 和 mesh 规模会随运行环境/重建结果波动，单阶段收益不一定等比例反映到总耗时。

Densify 还支持 `MVS_DENSIFY_MAX_RESOLUTION`、`MVS_DENSIFY_RESOLUTION_LEVEL`、`MVS_DENSIFY_ITERS` 和 `MVS_DENSIFY_NUMBER_VIEWS_FUSE` 做质量受控 A/B。当前默认 `MVS_DENSIFY_MAX_RESOLUTION=2560` 保持 OpenMVS 基线质量；`2048` 实测很快但 dense/mesh 指标下降约三成，不作为默认。`MVS_DENSIFY_ITERS=2` 实测 Densify 快约 18%，但 TextureMesh 因 patch 数增大导致端到端变慢，因此默认仍保持 3。

`MVS_GENERATE_TEXTURE=0` 会在 `scene_mesh.ply` 生成后停止，不运行 TextureMesh。默认仍为 `1`，用于完整 textured mesh；关闭后适合 UI 先展示可用几何、调试 mesh 或把贴图作为后台任务单独排队。

Python 编排入口：

```bash
python3 scripts/run_python_ui.py --images data/images --cameras data/cameras.json --output outputs/default
python3 scripts/run_python_ui.py --config config/reconstruction.json
python3 scripts/run_python_ui.py --max-threads 8 --undistort-copy-policy COPY
python3 scripts/run_python_ui.py --reuse-existing 1
python3 scripts/run_python_ui.py --remove-depth-maps 0
python3 scripts/run_python_ui.py --matcher sequential --sequential-overlap 40 --sequential-quadratic-overlap 0
python3 scripts/run_python_ui.py --densify-max-resolution 2048
python3 scripts/run_python_ui.py --densify-resolution-level 2 --densify-iters 2
python3 scripts/run_python_ui.py --generate-texture 0
python3 scripts/run_python_ui.py --texture-patch-packing-heuristic 100
```

运行报告：

```bash
python3 scripts/report_run.py outputs/default
python3 scripts/report_run.py outputs/default --json
python3 scripts/report_run.py outputs/default --output outputs/default/report.md
python3 scripts/report_run.py outputs/baseline --compare outputs/candidate
```

报告会汇总 `manifest.json`、stage 日志、OpenMVS 点云/mesh 指标和关键产物体积，方便做参数 A/B 对比。

包内也会包含 `scripts/run_python_ui.py`。在包目录中运行时，它默认使用包内 `bin/` 下的可执行文件，并读取主项目目录下的 `data/`；也可以通过 `--data-root /path/to/data` 覆盖输入数据目录。

## 输出产物

每次运行会写入：

- `outputs/<run-name>/colmap/database.db`
- `outputs/<run-name>/colmap/sparse/`
- `outputs/<run-name>/colmap/dense/`
- `outputs/<run-name>/openmvs/scene.mvs`
- `outputs/<run-name>/openmvs/scene_dense.mvs`
- `outputs/<run-name>/openmvs/scene_mesh.ply`
- `outputs/<run-name>/openmvs/scene_texture.ply` 及纹理图 `scene_texture*.png`
- `outputs/<run-name>/logs/*.log`
- `outputs/<run-name>/manifest.json`

如果设置 `MVS_GENERATE_TEXTURE=0`，输出会停在 `scene_mesh.ply`，不会生成 `scene_texture.ply` 和纹理图。

## 快速验证

```bash
bash -n scripts/fetch_3rdparty.sh scripts/build.sh scripts/reconstruct.sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m py_compile src/python/mvs_io/__init__.py src/python/mvs_io/cameras.py src/python/mvs_io/run.py scripts/run_python_ui.py
```
