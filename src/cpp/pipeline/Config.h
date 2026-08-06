#pragma once

#include <string>

namespace mvs {

struct Config {
  std::string imagesDir;
  std::string camerasJson;
  std::string outputDir;
  std::string colmapBinary;
  std::string openMvsBinDir;
  int maxThreads = 0;
  std::string undistortCopyPolicy = "HARD_LINK";
  bool reuseExisting = false;
  bool removeDepthMaps = true;
  std::string matcher = "exhaustive";
  int sequentialOverlap = 10;
  bool sequentialQuadraticOverlap = true;
  // COLMAP mapper 的 Bundle Adjustment GPU 加速。默认关闭，且即使开启，
  // 图片数低于 min_num_images_gpu_solver（默认 50）时 COLMAP 仍会回退到 CPU。
  // 特征提取和匹配的 GPU 加速无需此选项，它们默认已启用（use_gpu=1）。
  bool mapperBundleAdjustmentGpu = false;
  // OpenMVS 各工具使用的 CUDA 设备号。-1 = 自动选最佳 GPU（OpenMVS 默认），
  // >=0 = 指定设备号，-2 = 强制 CPU。
  // 注意：OpenMVS 每个工具进程只用一块 GPU，且本管线各阶段是顺序执行的，
  // 因此指定不同设备号不会带来并行加速；它的用途是把多个并发的重建任务
  // 分散到不同 GPU 上，或避开某块被占用的卡。
  int cudaDevice = -1;
  int densifyNumberViews = 5;
  int densifyNumberViewsFuse = 2;
  int densifyGeometricIters = 2;
  int densifyResolutionLevel = 1;
  int densifyMaxResolution = 2560;
  int densifyIters = 3;
  bool generateTexture = true;
  int texturePatchPackingHeuristic = 3;

  std::string runName() const;
};

Config parseArgs(int argc, char** argv);

}  // namespace mvs
