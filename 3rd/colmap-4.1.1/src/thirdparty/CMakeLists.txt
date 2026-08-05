set(FOLDER_NAME "thirdparty")

# Only show moderate warnings for external library code.
if(IS_MSVC)
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE "/Wall" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/Wall" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
elseif(IS_GNU OR IS_CLANG)
    string(REGEX REPLACE "-Wall" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REGEX REPLACE "-Wall" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

if(IS_MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W0")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W0")
elseif(IS_GNU OR IS_CLANG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w")
endif()

set(CLANG_TIDY_EXE_TEMP ${CLANG_TIDY_EXE})
set(CLANG_TIDY_EXE "")
if(LSD_ENABLED)
    add_subdirectory(LSD)
endif()
add_subdirectory(PoissonRecon)
if(GPU_ENABLED)
    add_subdirectory(SiftGPU)
endif()
add_subdirectory(VLFeat)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.28")
    set(_fetch_content_declare_args "SYSTEM" "EXCLUDE_FROM_ALL")
endif()

# 依赖获取方式，按优先级：
#   1. 本地预置源码。scripts/fetch_3rdparty.sh 会把 poselib/faiss 拉到 3rd/ 下，
#      scripts/build.sh 检测到后通过 *_LOCAL_SOURCE_DIR 传进来，构建时完全离线。
#   2. git+ssh 克隆。国内网络直连 codeload.github.com 下载 .zip 归档常常超时，
#      而 SSH (git@github.com:22) 稳定可用。
#   3. HTTPS 归档，上游原始方式：-DFETCH_DEPS_OVER_SSH=OFF
option(FETCH_DEPS_OVER_SSH "Fetch FetchContent deps via git+ssh instead of HTTPS archives" ON)
set(POSELIB_LOCAL_SOURCE_DIR "" CACHE PATH "Pre-fetched PoseLib source tree; empty to download")
set(FAISS_LOCAL_SOURCE_DIR "" CACHE PATH "Pre-fetched faiss source tree; empty to download")

if(FETCH_POSELIB)
    include(FetchContent)
    if(POSELIB_LOCAL_SOURCE_DIR AND EXISTS "${POSELIB_LOCAL_SOURCE_DIR}/CMakeLists.txt")
        message(STATUS "Using local PoseLib source: ${POSELIB_LOCAL_SOURCE_DIR}")
        FetchContent_Declare(poselib
            SOURCE_DIR ${POSELIB_LOCAL_SOURCE_DIR}
            ${_fetch_content_declare_args}
        )
    elseif(FETCH_DEPS_OVER_SSH)
        FetchContent_Declare(poselib
            GIT_REPOSITORY git@github.com:PoseLib/PoseLib.git
            GIT_TAG fa7280fee27f97aff31ae7f98bab7f583fac7d08
            GIT_SHALLOW FALSE
            GIT_PROGRESS TRUE
            ${_fetch_content_declare_args}
        )
    else()
        FetchContent_Declare(poselib
            URL https://github.com/PoseLib/PoseLib/archive/fa7280fee27f97aff31ae7f98bab7f583fac7d08.zip
            URL_HASH SHA256=5408d4ae8ce367cb2f076bc6c5f0f6f78abd3573d2c015304b04e46f23455f5b
            ${_fetch_content_declare_args}
        )
    endif()
    message(STATUS "Configuring PoseLib...")
    set(MARCH_NATIVE OFF CACHE BOOL "")
    FetchContent_MakeAvailable(poselib)
    # Rename PoseLib's benchmark folder to avoid conflict with Google Benchmark's
    # benchmark/benchmark.h header (PoseLib exposes its entire source dir as include path)
    if(EXISTS "${poselib_SOURCE_DIR}/benchmark")
        file(RENAME "${poselib_SOURCE_DIR}/benchmark" "${poselib_SOURCE_DIR}/poselib_benchmark")
    endif()
    message(STATUS "Configuring PoseLib... done")
endif()

if(FETCH_FAISS)
    include(FetchContent)
    if(FAISS_LOCAL_SOURCE_DIR AND EXISTS "${FAISS_LOCAL_SOURCE_DIR}/CMakeLists.txt")
        message(STATUS "Using local faiss source: ${FAISS_LOCAL_SOURCE_DIR}")
        FetchContent_Declare(faiss
            SOURCE_DIR ${FAISS_LOCAL_SOURCE_DIR}
            ${_fetch_content_declare_args}
        )
    elseif(FETCH_DEPS_OVER_SSH)
        FetchContent_Declare(faiss
            GIT_REPOSITORY git@github.com:facebookresearch/faiss.git
            GIT_TAG v1.14.1
            GIT_SHALLOW TRUE
            GIT_PROGRESS TRUE
            ${_fetch_content_declare_args}
        )
    else()
        FetchContent_Declare(faiss
            URL https://github.com/facebookresearch/faiss/archive/refs/tags/v1.14.1.zip
            URL_HASH SHA256=4b1ae7e7a0a46385b4084f0e3945623a15fcf99d793bf44d82aae8e24f11e5f5
            ${_fetch_content_declare_args}
        )
    endif()
    message(STATUS "Configuring faiss...")
    # Use dynamic dispatch (dd) to automatically select the highest SIMD
    # instruction set supported by the target machine (e.g., AVX2, AVX512).
    # Dynamic dispatch is not supported on MSVC (faiss v1.14.1).
    if(NOT IS_MSVC)
        set(FAISS_OPT_LEVEL dd)
    endif()
    set(FAISS_ENABLE_GPU OFF)
    set(FAISS_ENABLE_PYTHON OFF)
    set(FAISS_ENABLE_MKL OFF)
    set(BUILD_TESTING OFF)
    # faiss 的 FAISS_ENABLE_EXTRAS 默认 ON，会 add_subdirectory(demos/benchs/tutorial)。
    # COLMAP 只需要 faiss 静态库，这些示例既无用又拖慢配置；关掉后 3rd/faiss 下
    # 对应目录可以从 vendored 源码中删除。
    set(FAISS_ENABLE_EXTRAS OFF)
    FetchContent_MakeAvailable(faiss)
    # Fix MSVC linker warning LNK4286: faiss headers use FAISS_API which
    # defaults to dllimport on Windows, but we build faiss as a static library.
    # Adding FAISS_MAIN_LIB as PUBLIC ensures consumers also get this defined.
    if(WIN32 AND TARGET faiss)
        target_compile_definitions(faiss PUBLIC FAISS_MAIN_LIB)
    endif()
    message(STATUS "Configuring faiss... done")
endif()

if(CASPAR_ENABLED AND NOT CUDA_ENABLED)
    message(FATAL_ERROR "Caspar requires CUDA")
endif()

if(CASPAR_ENABLED AND CUDA_ENABLED)

    # TODO(tordnat): Make Caspar set CASPAR_MIN_ARCH when generating build files
    set(CASPAR_MIN_ARCH 75)
    foreach(_arch ${CMAKE_CUDA_ARCHITECTURES})
        if(_arch LESS ${CASPAR_MIN_ARCH})
            message(FATAL_ERROR "Caspar requires CUDA architecture >= ${CASPAR_MIN_ARCH}, but ${_arch} was requested. "
                "Set CMAKE_CUDA_ARCHITECTURES to ${CASPAR_MIN_ARCH} or higher, or disable Caspar with -DCASPAR_ENABLED=OFF.")
        endif()
    endforeach()

    message(STATUS "Enabling Caspar support")

    if(CASPAR_USE_DOUBLE)
        set(_caspar_precision "f64")
    else()
        set(_caspar_precision "f32")
    endif()

    set(CASPAR_GEN_DIR
        "${CMAKE_SOURCE_DIR}/src/thirdparty/Symforce-Caspar/generated/${_caspar_precision}")

    if(NOT EXISTS "${CASPAR_GEN_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "Caspar: pre-generated code not found for precision '${_caspar_precision}'.\n"
            "Expected: ${CASPAR_GEN_DIR}/CMakeLists.txt\n"
            "Re-run generate_caspar.py to regenerate.")
    endif()

    include(FetchContent)
    FetchContent_Declare(caspar
        SOURCE_DIR ${CASPAR_GEN_DIR}
        ${_fetch_content_declare_args}
    )
    message(STATUS "Configuring Caspar...")
    FetchContent_MakeAvailable(caspar)
    # Wrap the source-tree include path in BUILD_INTERFACE so caspar_lib_core
    # can be part of install(EXPORT). Caspar's generated CMakeLists.txt uses a
    # raw ${CMAKE_CURRENT_SOURCE_DIR} which CMake rejects on exported targets.
    set_target_properties(caspar_lib_core PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CASPAR_GEN_DIR}>")
    # Silence warnings from auto-generated Caspar CUDA sources, matching the
    # blanket `-w`/`/W0` policy applied to other thirdparty C++ code above.
    target_compile_options(caspar_lib_core PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:-w>
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcudafe=--diag_suppress=550>
        $<$<COMPILE_LANGUAGE:CXX>:$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>>
    )
    if(IS_MSVC)
        set(_MSVC_COMPACT_HEADER "${CASPAR_GEN_DIR}/../../msvc_compact.h")
        set(_MSVC_CUDA_COMPACT_HEADER "${CASPAR_GEN_DIR}/../../msvc_cuda_compact.h")
        target_compile_options(caspar_lib_core PRIVATE
            $<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:/FI"${_MSVC_COMPACT_HEADER}">
            # Keep CUDA pre-includes minimal and use NVCC's native pre-include
            # option to avoid forcing C++ standard library headers into NVCC's
            # MSVC host pass.
            $<$<COMPILE_LANGUAGE:CUDA>:--pre-include="${_MSVC_CUDA_COMPACT_HEADER}">
        )
    endif()
    message(STATUS "Configuring Caspar... done")

    message(STATUS "Caspar precision:  ${_caspar_precision}")
    message(STATUS "Caspar source dir: ${CASPAR_GEN_DIR}")
endif()

set(CLANG_TIDY_EXE ${CLANG_TIDY_EXE_TEMP})
