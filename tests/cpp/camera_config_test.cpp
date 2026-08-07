#include "pipeline/CameraConfig.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

int main() {
  const auto path = std::filesystem::temp_directory_path() / "mvs_camera_test.json";
  std::ofstream out(path);
  out << R"({
    "num_cameras": 2,
    "num_images": 2,
    "num_registered": 2,
    "cameras": [
      {"id": 1, "model": "SIMPLE_RADIAL", "width": 100, "height": 80, "params": [50.0, 50.0, 40.0, 0.01]},
      {"id": 2, "model": "SIMPLE_RADIAL", "width": 100, "height": 80, "params": [70.0, 50.0, 40.0, 0.03]}
    ],
    "images": [
      {"name": "IMG_0001.jpg", "center": [1.0, 2.0, 3.0]},
      {
        "name": "IMG_0002.jpg",
        "rotation": [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
        "translation": [-4.0, -5.0, -6.0]
      },
      {"name": "IMG_0003.jpg", "registered": false, "center": [7.0, 8.0, 9.0]}
    ]
  })";
  out.close();

  auto dataset = mvs::loadCameraDataset(path);
  assert(dataset.numImages == 2);
  assert(dataset.cameras.size() == 2);
  assert(dataset.poseReferences.size() == 2);
  assert(dataset.poseReferences[0].name == "IMG_0001.jpg");
  assert(std::abs(dataset.poseReferences[0].x - 1.0) < 1e-9);
  assert(std::abs(dataset.poseReferences[0].y - 2.0) < 1e-9);
  assert(std::abs(dataset.poseReferences[0].z - 3.0) < 1e-9);
  assert(dataset.poseReferences[1].name == "IMG_0002.jpg");
  assert(std::abs(dataset.poseReferences[1].x - 4.0) < 1e-9);
  assert(std::abs(dataset.poseReferences[1].y - 5.0) < 1e-9);
  assert(std::abs(dataset.poseReferences[1].z - 6.0) < 1e-9);
  auto camera = mvs::medianSimpleRadialCamera(dataset);
  assert(camera.width == 100);
  assert(camera.height == 80);
  assert(std::abs(camera.f - 60.0) < 1e-9);
  assert(std::abs(camera.cx - 50.0) < 1e-9);
  assert(std::abs(camera.cy - 40.0) < 1e-9);
  assert(std::abs(camera.k1 - 0.02) < 1e-9);

  const auto imagesDir = std::filesystem::temp_directory_path() / "mvs_camera_fallback_images";
  std::filesystem::remove_all(imagesDir);
  std::filesystem::create_directories(imagesDir);
  cv::imwrite((imagesDir / "IMG_0001.jpg").string(), cv::Mat(80, 120, CV_8UC3, cv::Scalar(10, 20, 30)));
  cv::imwrite((imagesDir / "IMG_0002.jpg").string(), cv::Mat(80, 120, CV_8UC3, cv::Scalar(30, 20, 10)));
  const auto fallback = mvs::estimateSimpleRadialCameraFromImages(imagesDir);
  assert(fallback.model == "SIMPLE_RADIAL");
  assert(fallback.width == 120);
  assert(fallback.height == 80);
  assert(std::abs(fallback.f - 144.0) < 1e-9);
  assert(std::abs(fallback.cx - 60.0) < 1e-9);
  assert(std::abs(fallback.cy - 40.0) < 1e-9);
  assert(std::abs(fallback.k1) < 1e-9);
  return 0;
}
