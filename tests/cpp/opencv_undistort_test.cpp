// OpenCV 去畸变后端的几何不变量测试。
//
// 核心断言：写出的 dense/sparse 内参、以及实际输出图像的尺寸，都必须与
// colmap::UndistortCamera(默认 options) 的结果严格一致。这是"两个后端几何可比"
// 的基础——COLMAP image_undistorter 用同一个函数和同一份默认 options，所以给定
// 相同输入 sparse model 时两者必然产出相同几何。
//
// 之前踩过的坑正是这里：图像用一套 K、sparse model 用另一套，位姿就与图像不匹配。
//
// 需要真实数据（sparse model + 原图），通过环境变量指定；缺失时跳过：
//   MVS_TEST_SPARSE_MODEL=<run>/colmap/sparse/0
//   MVS_TEST_IMAGES_DIR=<原图目录>

#include "pipeline/OpenCvUndistorter.h"

#include <colmap/image/undistortion.h>
#include <colmap/scene/reconstruction.h>

#include <opencv2/imgcodecs.hpp>

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main() {
  const char* sparseEnv = std::getenv("MVS_TEST_SPARSE_MODEL");
  const char* imagesEnv = std::getenv("MVS_TEST_IMAGES_DIR");
  if (sparseEnv == nullptr || imagesEnv == nullptr) {
    std::cout << "skipped: set MVS_TEST_SPARSE_MODEL and MVS_TEST_IMAGES_DIR\n";
    return 0;
  }

  const std::filesystem::path sparseModel(sparseEnv);
  const std::filesystem::path imagesDir(imagesEnv);
  if (!std::filesystem::exists(sparseModel / "cameras.bin")) {
    std::cout << "skipped: no cameras.bin under " << sparseModel << "\n";
    return 0;
  }

  const auto denseDir = std::filesystem::temp_directory_path() / "mvs_opencv_undistort_test";
  std::filesystem::remove_all(denseDir);

  mvs::OpenCvUndistortOptions options;
  options.maxThreads = 4;
  options.jpegQuality = 95;
  const std::size_t numImages =
      mvs::runOpenCvUndistort(sparseModel, imagesDir, denseDir, options);
  assert(numImages > 0);

  // 期望几何：直接调 COLMAP 的函数，用与后端相同的默认 options
  colmap::Reconstruction input;
  input.Read(sparseModel.string());
  const colmap::UndistortCameraOptions camOptions{};

  // 1. 写出的 dense/sparse 内参必须等于 UndistortCamera 的结果
  colmap::Reconstruction written;
  written.Read((denseDir / "sparse").string());
  for (const auto& [cameraId, inputCamera] : input.Cameras()) {
    const colmap::Camera expected = colmap::UndistortCamera(camOptions, inputCamera);
    const colmap::Camera& actual = written.Camera(cameraId);
    assert(actual.model_id == expected.model_id);
    assert(actual.width == expected.width);
    assert(actual.height == expected.height);
    assert(actual.params.size() == expected.params.size());
    for (std::size_t i = 0; i < expected.params.size(); ++i) {
      assert(std::abs(actual.params[i] - expected.params[i]) < 1e-9);
    }
  }

  // 2. 实际图像尺寸必须与该相机的 undistorted 尺寸一致（图像与 K 同步）
  std::size_t checked = 0;
  for (const colmap::image_t imageId : input.RegImageIds()) {
    const colmap::Image& image = input.Image(imageId);
    const colmap::Camera expected =
        colmap::UndistortCamera(camOptions, input.Camera(image.CameraId()));
    const auto imagePath = denseDir / "images" / image.Name();
    assert(std::filesystem::exists(imagePath));
    const cv::Mat img = cv::imread(imagePath.string(), cv::IMREAD_COLOR);
    assert(!img.empty());
    assert(static_cast<std::size_t>(img.cols) == expected.width);
    assert(static_cast<std::size_t>(img.rows) == expected.height);
    if (++checked >= 3) break;  // 前几张足以钉住尺寸约定
  }
  assert(checked > 0);

  std::filesystem::remove_all(denseDir);
  std::cout << "ok: " << numImages << " images, geometry matches UndistortCamera\n";
  return 0;
}
