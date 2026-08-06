// 流式融合管线：完整重建流程，用多线程 OpenCV 去畸变取代 COLMAP undistorter
// 节省：undistorter 74s + InterfaceCOLMAP 0.2s → 4.6s (并行) + 0.06s (库内)

#pragma once

#include "pipeline/CameraConfig.h"
#include "pipeline/Config.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

// Interface.h 依赖 cv::Point3_ / cv::Matx，必须先引入 OpenCV
#include <opencv2/core.hpp>

#define _USE_OPENCV
#include <MVS/Interface.h>

namespace mvs {

// 去畸变任务：一张图的输入路径、输出路径、相机参数
struct UndistortJob {
  std::filesystem::path inputPath;
  std::filesystem::path outputPath;
  cv::Matx33d cameraMatrix;         // COLMAP 内参
  std::vector<double> distCoeffs;   // OpenCV 畸变系数 [k1,k2,p1,p2,...]
};

// 完整重建流水线
class StreamingPipeline {
public:
  // 构建完整管线（images + cameras → texturing）
  StreamingPipeline(const Config& config,
                    const std::filesystem::path& outputDir);

  // 仅 buildScene + undistortParallel + densify（传入已有的 sparse model）
  StreamingPipeline(const Config& config,
                    const std::filesystem::path& sparseModel,
                    const std::filesystem::path& outputDir);

  // 完整流程：feature → matcher → mapper → streaming → densify → mesh → texture
  int runFull();

  // 仅替换 undistorter + InterfaceCOLMAP（从已有 sparse model 开始）
  void run();

  // ── 公开供 runPipeline() 直接调用 ───────────────────────────────────────
  // 读取 sparse model，构建 scene.mvs，返回去畸变任务列表
  std::vector<UndistortJob> buildScene();
  // 多线程去畸变，写入 outputDir_/images/
  void undistortParallel(const std::vector<UndistortJob>& jobs);

private:
  // ── COLMAP 阶段（子进程）────────────────────────────────────────────
  void stageFeatureExtractor(const CameraIntrinsics& camera);
  void stageMatcher();
  void stageMapper();

  // ── OpenMVS 阶段（子进程）──────────────────────────────────────────
  void stageDensify();
  void stageReconstructMesh();
  void stageTextureMesh();

  // ── 辅助 ────────────────────────────────────────────────────────────
  void runStage(const std::string& displayName,
                std::vector<std::string> args,
                const std::filesystem::path& logFile);

  Config config_;
  std::filesystem::path outputDir_;
  std::filesystem::path sparseModel_;  // colmap/sparse/0

  // 派生路径（由 outputDir_ 计算）
  std::filesystem::path colmapDir_;
  std::filesystem::path openMvsDir_;
  std::filesystem::path logsDir_;
  std::filesystem::path imageListFile_;
  std::filesystem::path database_;
  std::filesystem::path sparseDir_;
  std::filesystem::path sceneMvs_;
  std::filesystem::path denseMvs_;
  std::filesystem::path meshPly_;
  std::filesystem::path texturePly_;

  std::atomic<int> imagesCompleted_{0};
};

} // namespace mvs
