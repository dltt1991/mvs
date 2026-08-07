// OpenCV 去畸变后端：产出与 COLMAP image_undistorter 等价的 dense/ 工作区。
//
// 几何完全复用 COLMAP 自己的 colmap::UndistortCamera() / UndistortReconstruction()，
// 只把逐像素重采样内核换成 cv::undistort()（查表 + remap，SIMD 向量化）。
// 因此两个后端输出的图像尺寸和 dense/sparse 内参逐位一致，唯一差异是重采样实现
// 和 JPEG 编码器——对照实验只剩这一个变量。
//
// 实测（41 张 6016x4512，12 线程）：COLMAP 73.6s / 745s CPU，本后端 ~5s。
// 差异不在 I/O（写入量砍到 202MB 时 COLMAP 仍是 73.9s），而在 CPU 工作量。

#pragma once

#include <cstddef>
#include <filesystem>

namespace mvs {

struct OpenCvUndistortOptions {
  // 并行去畸变的线程数。0 = 用 hardware_concurrency()。
  int maxThreads = 0;
  // 写图 JPEG 质量。-1 = 用 OpenCV 默认（95）；否则 1-100。
  int jpegQuality = -1;
};

// 读 sparseModel（COLMAP sparse/0），把 imagesDir 下的原图去畸变后写入
// denseDir/images/，并把去畸变后的 sparse model 写入 denseDir/sparse/。
// 产物足以喂给 OpenMVS InterfaceCOLMAP（它只需要这两个目录：stereo/ 相关读取
// 被 File::isFolder 守卫，仅用于转换已有的 COLMAP 深度图）。
//
// 返回成功处理的图像数。任一图像读写失败则抛 std::runtime_error。
std::size_t runOpenCvUndistort(const std::filesystem::path& sparseModel,
                               const std::filesystem::path& imagesDir,
                               const std::filesystem::path& denseDir,
                               const OpenCvUndistortOptions& options);

}  // namespace mvs
