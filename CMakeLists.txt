cmake_minimum_required(VERSION 3.24)

project(MVSReconstruction LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python3 COMPONENTS Interpreter)

add_library(mvs_core
  src/cpp/pipeline/Config.cpp
  src/cpp/pipeline/CameraConfig.cpp
  src/cpp/pipeline/Manifest.cpp
  src/cpp/pipeline/ProcessRunner.cpp
  src/cpp/pipeline/ReconstructionPipeline.cpp
)

target_include_directories(mvs_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/3rd
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

# 融合 undistorter + DensifyPointCloud 程序（实验性）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FusedDensify.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FusedDensify.cmake)
endif()

# 流式管线：完全重写去畸变 + OpenMVS 转换（方案3完整版）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StreamingPipeline.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/StreamingPipeline.cmake)
endif()

if(Python3_Interpreter_FOUND)
  add_test(
    NAME mvs_report_run_test
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/python/report_run_test.py
  )
endif()
