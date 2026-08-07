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
  // 去畸变图像的 JPEG 质量，两个后端都生效。-1 = 沿用各后端默认
  // （COLMAP 近无损；OpenCV 95）；否则 1-100。
  int undistortJpegQuality = -1;
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
  // image_undistorter 阶段的去畸变后端。
  // false：COLMAP image_undistorter 子进程。
  // true（默认）：进程内 OpenCV 后端。几何完全复用 colmap::UndistortCamera()，与 COLMAP
  //   后端逐位一致（同样裁掉去畸变黑边），只把重采样内核换成 cv::undistort()。
  //   实测该阶段 73.6s → ~5s；后续 InterfaceCOLMAP 照常执行。
  bool useOpenCvUndistort = true;

  // SIFT 特征提取的 first_octave 参数（传给 COLMAP feature_extractor）。
  // -1（默认）：上采样 2 倍后再提取，捕获高频特征，但处理像素增加 4 倍。
  // 0：在降采样后的图像（max_image_size=3200 时为 3200×2400）直接提取，
  //    预期耗时减半（~45s → ~20s），特征数略降但对车载场景影响小。
  // 候选值：-1（默认，最全特征）、0（标准尺度）、1（粗尺度，不推荐）。
  int featureFirstOctave = -1;

  int densifyNumberViews = 5;
  int densifyNumberViewsFuse = 2;
  int densifyGeometricIters = 2;
  int densifyResolutionLevel = 1;
  int densifyMaxResolution = 2560;
  int densifyIters = 3;
  bool generateTexture = true;
  int texturePatchPackingHeuristic = 3;
  bool textureGlobalSeamLeveling = false;
  bool textureLocalSeamLeveling = false;

  std::string runName() const;
};

void applyConfigFile(Config& config, const std::filesystem::path& path);

Config parseArgs(int argc, char** argv);

}  // namespace mvs
