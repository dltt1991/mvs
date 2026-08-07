# 多视角三维重建项目构建/运行环境
#
# 基础镜像选择说明：
#   - Ubuntu 24.04：项目 CMakeLists.txt 要求 cmake >= 3.24，24.04 自带 3.28；
#     22.04 只有 3.22，不满足。
#   - CUDA 12.x：COLMAP 4.1.1 在 CUDA_ENABLED 时会拉取 onnxruntime 1.24.4 GPU 包，
#     该包要求 CUDA >= 12（见 3rd/colmap-4.1.1/cmake/FindDependencies.cmake:290）。
#   - devel 版本：需要 nvcc 编译 COLMAP/OpenMVS 的 CUDA kernel。
FROM nvcr.io/nvidia/cuda:12.6.3-devel-ubuntu24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG TZ=Asia/Shanghai

# 目标 GPU 架构。COLMAP 和 OpenMVS 的 CMake 默认都是 "native"，
# 而 docker build 阶段通常看不到 GPU，native 探测会失败或回退到错误架构。
# 75 = compute capability 7.5（Quadro RTX 6000 / RTX 20xx / T4）。
ARG CUDA_ARCHITECTURES=75

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN ln -snf /usr/share/zoneinfo/${TZ} /etc/localtime && echo ${TZ} > /etc/timezone

# 切换 Ubuntu 源到中科大镜像（默认的 ports.ubuntu.com 在国内速度慢）
RUN sed -i 's|http://ports.ubuntu.com/ubuntu-ports|https://mirrors.ustc.edu.cn/ubuntu-ports|g' /etc/apt/sources.list.d/ubuntu.sources

# 基础工具 + Python
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        git \
        wget \
        curl \
        unzip \
        vim \
        less \
        pkg-config \
        cmake \
        ninja-build \
        build-essential \
        python3 \
        python3-dev \
        python3-pip \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

# COLMAP 4.1.1 依赖
# Boost 组件：COLMAP 需要 program_options/graph/system，
# OpenMVS 需要 iostreams/program_options/serialization。
RUN apt-get update && apt-get install -y --no-install-recommends \
        libboost-program-options-dev \
        libboost-graph-dev \
        libboost-system-dev \
        libboost-filesystem-dev \
        libboost-iostreams-dev \
        libboost-serialization-dev \
        libeigen3-dev \
        libopenimageio-dev \
        openimageio-tools \
        libjxl-dev \
        libmetis-dev \
        libgoogle-glog-dev \
        libgflags-dev \
        libgtest-dev \
        libgmock-dev \
        libsqlite3-dev \
        libglew-dev \
        libcgal-dev \
        libceres-dev \
        libsuitesparse-dev \
        libcurl4-openssl-dev \
        libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# OpenCV：OpenMVS 的 FIND_PACKAGE(OpenCV REQUIRED) 是硬依赖
# （3rd/openMVS-2.4.0/CMakeLists.txt:259）
RUN apt-get update && apt-get install -y --no-install-recommends \
        libopencv-dev \
    && rm -rf /var/lib/apt/lists/*

# BLAS/LAPACK：COLMAP 用 FetchContent 拉取 faiss v1.14.1 并设置
# FAISS_ENABLE_MKL=OFF，因此需要系统 BLAS。用 OpenBLAS 替代 README 里
# 体积巨大的 libmkl-full-dev。
RUN apt-get update && apt-get install -y --no-install-recommends \
        libopenblas-dev \
        liblapack-dev \
    && rm -rf /var/lib/apt/lists/*

# Qt6：COLMAP GUI_ENABLED 默认 ON，需要 Core/OpenGL/Svg/Widgets/OpenGLWidgets
RUN apt-get update && apt-get install -y --no-install-recommends \
        qt6-base-dev \
        qt6-base-dev-tools \
        libqt6opengl6-dev \
        libqt6svg6-dev \
    && rm -rf /var/lib/apt/lists/*

# OpenMVS 额外的构建工具和 X11/GL 库
RUN apt-get update && apt-get install -y --no-install-recommends \
        autoconf \
        autoconf-archive \
        automake \
        libtool \
        bison \
        gfortran \
        nasm \
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
        # OpenMVS Viewer 依赖。glad/imgui/portable-file-dialogs 已 vendored 在 3rd/ 下，
        # 只有 glfw3 是系统依赖；缺它时 apps/Viewer/CMakeLists.txt 会 RETURN() 静默跳过。
        libglfw3-dev \
    && rm -rf /var/lib/apt/lists/*

# COLMAP 官方文档建议为 Ubuntu 下 OpenImageIO 的 CMake 配置准备该目录
RUN mkdir -p /usr/include/opencv4

# CUDA 架构 toolchain 文件。
# scripts/build.sh 不接受额外的 CMake 参数，而 COLMAP / OpenMVS 的 CMake 都是
# 「未定义 CMAKE_CUDA_ARCHITECTURES 时设为 native」。这里通过 CMake 3.21+ 支持的
# CMAKE_TOOLCHAIN_FILE 环境变量在 project() 之前把架构定下来，
# 不需要修改项目脚本。只设置架构，不设置 CMAKE_SYSTEM_NAME，避免触发交叉编译模式。
RUN printf '%s\n' \
        '# 由 Dockerfile 生成：固定 CUDA 架构，避免 docker build 阶段 native 探测失败' \
        'if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)' \
        "  set(CMAKE_CUDA_ARCHITECTURES ${CUDA_ARCHITECTURES} CACHE STRING \"目标 CUDA 架构\")" \
        'endif()' \
        > /opt/cuda-arch-toolchain.cmake

ENV CMAKE_TOOLCHAIN_FILE=/opt/cuda-arch-toolchain.cmake \
    CUDAARCHS=${CUDA_ARCHITECTURES} \
    CUDA_HOME=/usr/local/cuda \
    PATH=/usr/local/cuda/bin:${PATH} \
    LD_LIBRARY_PATH=/usr/local/cuda/lib64:${LD_LIBRARY_PATH} \
    PYTHONUNBUFFERED=1

# 让 nvidia-container-runtime 暴露完整能力（compute 用于 CUDA，utility 用于 nvidia-smi）
ENV NVIDIA_VISIBLE_DEVICES=all \
    NVIDIA_DRIVER_CAPABILITIES=compute,utility

# 与宿主机 UID/GID 一致的非 root 用户，避免 build/ outputs/ 产出 root 属主文件
ARG USER_NAME=mvs
ARG USER_ID=1000
ARG GROUP_ID=1005
RUN if ! getent group ${GROUP_ID} >/dev/null; then groupadd -g ${GROUP_ID} ${USER_NAME}; fi \
    && if ! getent passwd ${USER_ID} >/dev/null; then \
         useradd -m -u ${USER_ID} -g ${GROUP_ID} -s /bin/bash ${USER_NAME}; \
       fi \
    && install -d -o ${USER_ID} -g ${GROUP_ID} \
         /workspace /workspace/build /workspace/outputs /workspace/packages

# 追加依赖。放在靠后的层，避免重建时让上面的重依赖层缓存失效。
#   - openssh-client：COLMAP 的 FetchContent 依赖（poselib/faiss）改走 git+ssh
#     拉取（见 3rd/colmap-4.1.1/src/thirdparty/CMakeLists.txt），git 需要 ssh
#     二进制，基础镜像里没有。
#   - libnanoflann-dev：OpenMVS 的 FIND_PACKAGE(nanoflann REQUIRED) 是硬依赖
#     （3rd/openMVS-2.4.0/libs/Common/CMakeLists.txt:2）。
RUN apt-get update && apt-get install -y --no-install-recommends \
        openssh-client \
        libnanoflann-dev \
    && rm -rf /var/lib/apt/lists/*

COPY docker/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

WORKDIR /workspace
USER ${USER_ID}:${GROUP_ID}

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["/bin/bash"]
