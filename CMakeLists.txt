cmake_minimum_required(VERSION 3.24)

project(MVSReconstruction LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter)

# StreamingPipeline 依赖：COLMAP 库（读 sparse model）+ OpenCV（去畸变）
find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc calib3d)
find_package(Eigen3 REQUIRED)
find_package(Boost REQUIRED)
find_package(glog CONFIG QUIET)
find_package(gflags REQUIRED)
find_package(Ceres REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(OpenImageIO REQUIRED CONFIG)

set(COLMAP_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/colmap/src/colmap)

add_library(mvs_core
  src/cpp/pipeline/Config.cpp
  src/cpp/pipeline/CameraConfig.cpp
  src/cpp/pipeline/Manifest.cpp
  src/cpp/pipeline/ProcessRunner.cpp
  src/cpp/pipeline/ReconstructionPipeline.cpp
  src/cpp/pipeline/StreamingPipeline.cpp
)

target_include_directories(mvs_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/colmap-4.1.1/src
    ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/colmap/src
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd/openMVS-2.4.0/libs
    ${OpenCV_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${CERES_INCLUDE_DIRS}
)

if(DEFINED glog_VERSION_MAJOR)
  target_compile_definitions(mvs_core
    PUBLIC
      GLOG_VERSION_MAJOR=${glog_VERSION_MAJOR}
      GLOG_VERSION_MINOR=${glog_VERSION_MINOR}
  )
endif()

target_link_libraries(mvs_core
  PUBLIC
    ${OpenCV_LIBS}
    Eigen3::Eigen
    Boost::headers
    Ceres::ceres
    # COLMAP 库（只用于读取 sparse reconstruction）
    ${COLMAP_LIB_DIR}/scene/libcolmap_scene.a
    ${COLMAP_LIB_DIR}/scene/libcolmap_scene_types.a
    ${COLMAP_LIB_DIR}/image/libcolmap_image.a
    ${COLMAP_LIB_DIR}/sensor/libcolmap_sensor.a
    ${COLMAP_LIB_DIR}/util/libcolmap_util.a
    ${COLMAP_LIB_DIR}/geometry/libcolmap_geometry.a
    ${COLMAP_LIB_DIR}/math/libcolmap_math.a
    # 系统依赖（COLMAP 传递）
    pthread curl OpenSSL::Crypto glog::glog gflags OpenImageIO::OpenImageIO OpenImageIO::OpenImageIO_Util
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

if(Python3_Interpreter_FOUND)
  add_test(
    NAME mvs_report_run_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/report_run_test.py
  )
endif()
