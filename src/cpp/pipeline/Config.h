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
