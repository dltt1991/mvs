#pragma once

#include <filesystem>
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

  // 分批流水线模式：将 undistorter 和 DensifyPointCloud 拆分成多批并行执行。
  // undistorter (CPU 主导) 和 DensifyPointCloud (GPU 主导) 资源不冲突，
  // 且速度接近，可以通过分批流水线实现 overlap，预期节省 10% 总耗时。
  // 默认关闭（false），使用传统串行流程。
  bool enableBatchedPipeline = false;

  // 每批处理的图像数量。默认 10（40 张图分 4 批）。
  // 太小：批次过多，同步开销大。太大：并行度低。
  int batchSize = 10;

  // 批次重叠数量：为了让 DensifyPointCloud 能找到邻居视图，
  // 每批 undistort 时会额外处理前后各 overlap 张图。
  // 默认 5（邻居视图搜索范围通常 < 5）。
  int batchOverlap = 5;
  // 用进程内多线程实现取代 COLMAP image_undistorter + OpenMVS InterfaceCOLMAP。
  // true（默认）：buildScene(56ms) + undistortParallel(12线程,4.7s) 取代串行74s。
  // false：保留原有子进程调用，用于对比或排查问题。
  bool useStreamingUndistort = true;

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

void applyConfigFile(Config& config, const std::filesystem::path& path);

Config parseArgs(int argc, char** argv);

}  // namespace mvs
