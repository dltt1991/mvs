# 流式管线：完全绕过 COLMAP undistorter 和 InterfaceCOLMAP
# 直接去畸变 + 写入 OpenMVS 格式 + 并行 DensifyPointCloud

find_package(OpenCV REQUIRED COMPONENTS core imgcodecs imgproc calib3d)
find_package(Eigen3 REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options filesystem)

add_executable(mvs_streaming
  src/cpp/mvs_streaming_main.cpp
  src/cpp/pipeline/StreamingPipeline.cpp
)

target_include_directories(mvs_streaming PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/3rd/colmap-4.1.1/src
  ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/colmap/src
  ${CMAKE_CURRENT_SOURCE_DIR}/3rd/openMVS-2.4.0/libs
  ${OpenCV_INCLUDE_DIRS}
)

# COLMAP 和 OpenMVS 库路径
set(COLMAP_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/colmap/src/colmap)
set(OPENMVS_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/third_party/openmvs/lib)

target_link_libraries(mvs_streaming PRIVATE
  mvs_core
  ${OpenCV_LIBS}
  Eigen3::Eigen
  Boost::program_options
  Boost::filesystem
  # COLMAP 库（只用于加载 reconstruction）
  ${COLMAP_LIB_DIR}/scene/libcolmap_scene.a
  ${COLMAP_LIB_DIR}/scene/libcolmap_scene_types.a
  ${COLMAP_LIB_DIR}/image/libcolmap_image.a
  ${COLMAP_LIB_DIR}/sensor/libcolmap_sensor.a
  ${COLMAP_LIB_DIR}/util/libcolmap_util.a
  ${COLMAP_LIB_DIR}/geometry/libcolmap_geometry.a
  ${COLMAP_LIB_DIR}/math/libcolmap_math.a
  # OpenMVS 库（Interface.h 是 header-only）
  # 系统依赖
  pthread
  curl
  crypto
  glog
  gflags
  OpenImageIO
  OpenImageIO_Util
)

# 设置输出目录
set_target_properties(mvs_streaming PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
