cmake_minimum_required(VERSION 3.24)

project(MVSReconstruction LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter)

# OpenCvUndistorter 依赖：COLMAP 库（读 sparse model + 去畸变几何）+ OpenCV（重采样）
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc calib3d)
find_package(Eigen3 REQUIRED)
# COLMAP 的 VLFeat 用了 OpenMP（vl_imsmooth_f 经由 image/warp.cc 引入）
find_package(OpenMP REQUIRED)

set(COLMAP_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/colmap/src/colmap)

add_library(mvs_core
  src/cpp/pipeline/Config.cpp
  src/cpp/pipeline/CameraConfig.cpp
  src/cpp/pipeline/Manifest.cpp
  src/cpp/pipeline/ProcessRunner.cpp
  src/cpp/pipeline/ReconstructionPipeline.cpp
  src/cpp/pipeline/OpenCvUndistorter.cpp
)

target_include_directories(mvs_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/colmap-4.1.1/src
    ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/colmap/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/openMVS-2.4.0/libs
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(mvs_core
  PUBLIC
    ${OpenCV_LIBS}
    Eigen3::Eigen
    # COLMAP 库：读 sparse reconstruction + 复用 UndistortCamera/UndistortReconstruction。
    # 静态库顺序敏感：libcolmap_image.a 的对象被抽出后才会去找 Camera::Rescale 等符号，
    # 而那些在 libcolmap_scene_types.a 里，所以后者必须重复出现在 image 之后。
    # image/warp.cc 还引用了 VLFeat 的 vl_imsmooth_f。
    ${COLMAP_LIB_DIR}/scene/libcolmap_scene.a
    ${COLMAP_LIB_DIR}/scene/libcolmap_scene_types.a
    ${COLMAP_LIB_DIR}/image/libcolmap_image.a
    ${COLMAP_LIB_DIR}/scene/libcolmap_scene_types.a
    ${COLMAP_LIB_DIR}/sensor/libcolmap_sensor.a
    ${COLMAP_LIB_DIR}/util/libcolmap_util.a
    ${COLMAP_LIB_DIR}/geometry/libcolmap_geometry.a
    ${COLMAP_LIB_DIR}/math/libcolmap_math.a
    ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/colmap/src/thirdparty/VLFeat/libcolmap_vlfeat.a
    # 系统依赖（COLMAP 传递）
    OpenMP::OpenMP_CXX
    pthread curl crypto glog gflags OpenImageIO OpenImageIO_Util
)

add_executable(mvs_reconstruct
  src/cpp/main.cpp
)

target_link_libraries(mvs_reconstruct PRIVATE mvs_core)

enable_testing()

add_executable(mvs_config_test
  tests/cpp/config_test.cpp
)

target_link_libraries(mvs_config_test PRIVATE mvs_core)
set_target_properties(mvs_config_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/cpp
)
add_test(NAME mvs_config_test COMMAND mvs_config_test)

add_executable(mvs_camera_config_test
  tests/cpp/camera_config_test.cpp
)

target_link_libraries(mvs_camera_config_test PRIVATE mvs_core)
set_target_properties(mvs_camera_config_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/cpp
)
add_test(NAME mvs_camera_config_test COMMAND mvs_camera_config_test)

add_executable(mvs_process_manifest_test
  tests/cpp/process_manifest_test.cpp
)

target_link_libraries(mvs_process_manifest_test PRIVATE mvs_core)
set_target_properties(mvs_process_manifest_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/cpp
)
add_test(NAME mvs_process_manifest_test COMMAND mvs_process_manifest_test)

add_executable(mvs_pipeline_command_test
  tests/cpp/pipeline_command_test.cpp
)

target_link_libraries(mvs_pipeline_command_test PRIVATE mvs_core)
set_target_properties(mvs_pipeline_command_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/cpp
)
add_test(NAME mvs_pipeline_command_test COMMAND mvs_pipeline_command_test)

# 需要真实 sparse model + 原图；未设 MVS_TEST_SPARSE_MODEL/MVS_TEST_IMAGES_DIR 时自行跳过
add_executable(mvs_opencv_undistort_test
  tests/cpp/opencv_undistort_test.cpp
)

target_link_libraries(mvs_opencv_undistort_test PRIVATE mvs_core)
set_target_properties(mvs_opencv_undistort_test PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/cpp
)
add_test(NAME mvs_opencv_undistort_test COMMAND mvs_opencv_undistort_test)

if(Python3_Interpreter_FOUND)
  add_test(
    NAME mvs_report_run_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/report_run_test.py
  )
endif()
