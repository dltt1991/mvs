// 流式管线：直接写 OpenMVS 场景 + 多线程去畸变
// 完全绕过 COLMAP image_undistorter 和 OpenMVS InterfaceCOLMAP

#pragma once

#include "pipeline/Config.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

// Interface.h 在 _USE_OPENCV 下直接使用 cv::Point3_/cv::Matx，必须先引入 OpenCV
#include <opencv2/core.hpp>

#define _USE_OPENCV
#include <MVS/Interface.h>

namespace mvs {

// 一张待去畸变图像的任务描述
struct UndistortJob {
  std::filesystem::path inputPath;   // 原始图像路径
  std::filesystem::path outputPath;  // 去畸变后输出路径
  cv::Matx33d cameraMatrix;          // COLMAP 内参
  std::vector<double> distCoeffs;    // OpenCV 畸变系数
};

class StreamingPipeline {
public:
  StreamingPipeline(const Config& config,
                    const std::filesystem::path& sparseModel,
                    const std::filesystem::path& outputDir);

  void run();

private:
  // 阶段 1：从 sparse model 直接构建 scene.mvs，并生成去畸变任务列表
  std::vector<UndistortJob> buildScene();

  // 阶段 2：多线程去畸变
  void undistortParallel(const std::vector<UndistortJob>& jobs);

  // 阶段 3：DensifyPointCloud
  bool densify();

  Config config_;
  std::filesystem::path sparseModel_;
  std::filesystem::path outputDir_;

  std::atomic<int> imagesCompleted_{0};
};

} // namespace mvs
