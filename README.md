# 多视角三维重建项目

本项目用于基于单目相机拍摄的多视角图片，对物体或场景进行三维重建。核心重建流程由 C++ 可执行程序负责，驱动从源码编译的 COLMAP 4.1.1 和 OpenMVS 2.4.0；Python 只用于 I/O 校验和简单的命令行/UI 编排。

## 目录结构

- `data/images/`：输入图片，格式说明见 [`data/README.md`](data/README.md)。
- `data/cameras.json`：相机参数，用于初始化 COLMAP 相机模型，字段说明见 [`data/README.md`](data/README.md)。
- `3rd/colmap-4.1.1`：COLMAP 源码，固定在 `4.1.1` 标签。
- `3rd/openMVS-2.4.0`：OpenMVS 源码，固定在 `v2.4.0` 标签。
- `3rd/vcglib`：OpenMVS 需要的 VCG header-only 源码依赖。
- `3rd/nlohmann`：C++ 核心使用的 nlohmann/json 头文件。
- `3rd/onnxruntime-osx-arm64-1.24.4`：macOS arm64 下可选的本地 ONNX Runtime 包，用于避免 COLMAP 构建时联网下载。
- `src/cpp`：C++ 编译核心，包括重建流程、命令规划、日志和 manifest 输出。
- `src/python`：Python I/O 和 CLI 辅助代码。
- `scripts`：拉取源码、构建和运行脚本。
- `outputs/<run-name>`：每次重建的产物和日志。

说明：Git 仓库中不提交 `data/` 下的实际数据、`packages/`、`build/` 和 `outputs/`。其中 `data/` 用于本地输入图片和相机参数，仅保留 [`data/README.md`](data/README.md)；`packages/` 是打包输出目录，`build/` 是本地编译目录，`outputs/` 是重建运行产物。

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
- `image_undistorter`：生成 COLMAP dense workspace。
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

当前 `3rd/` 中的第三方源码按固定版本 vendored 到项目里，并已清理 `.git` 元数据、文档、CI、Docker、示例和测试数据。`scripts/fetch_3rdparty.sh` 遇到已存在但没有 `.git` 的源码目录时会直接复用；需要重新下载时，先删除对应目录再执行脚本。

## 构建依赖

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
- `lib/onnxruntime`：本地 ONNX Runtime 动态库，如果构建时启用了本地 ONNX Runtime。
- `manifest.json`：包版本、COLMAP/OpenMVS 固定版本和目录布局。

打包产物不复制 `data/`。包内 `scripts/reconstruct.sh` 默认读取主项目目录下的 `data/images` 和 `data/cameras.json`，也可以通过 `DATA_ROOT=/path/to/data` 覆盖。

主项目 C++ 可执行文件位置：

```text
build/mvs_reconstruct
```

脚本会在 `3rd/` 下存在源码时尝试构建 COLMAP 和 OpenMVS。

如果本地已经下载 ONNX Runtime，macOS arm64 包可放在：

```text
3rd/onnxruntime-osx-arm64-1.24.4
```

目录中需要包含 `include/` 和 `lib/`。`scripts/build.sh` 会自动检测该目录，生成 COLMAP 需要的 include 兼容层，并关闭 COLMAP 的 ONNX Runtime 在线下载。

## 运行重建

```bash
scripts/reconstruct.sh
```

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

每个阶段的耗时也会写入 `outputs/<run-name>/manifest.json` 的 `stages` 数组。某一步失败时，终端会显示失败阶段、耗时、退出码和对应日志文件路径。

可选覆盖项：

```bash
RUN_NAME=my-run scripts/reconstruct.sh
COLMAP_BIN=/path/to/colmap OPENMVS_BIN_DIR=/path/to/openmvs/bin scripts/reconstruct.sh
```

Python 编排入口：

```bash
python3 scripts/run_python_ui.py --images data/images --cameras data/cameras.json --output outputs/default
```

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

## 快速验证

```bash
bash -n scripts/fetch_3rdparty.sh scripts/build.sh scripts/reconstruct.sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m py_compile src/python/mvs_io/__init__.py src/python/mvs_io/cameras.py src/python/mvs_io/run.py scripts/run_python_ui.py
```
