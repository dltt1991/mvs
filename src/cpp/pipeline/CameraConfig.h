#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mvs {

struct CameraIntrinsics {
  int id = 0;
  std::string model;
  int width = 0;
  int height = 0;
  double f = 0.0;
  double cx = 0.0;
  double cy = 0.0;
  double k1 = 0.0;
};

struct CameraDataset {
  int numCameras = 0;
  int numImages = 0;
  int numRegistered = 0;
  std::vector<CameraIntrinsics> cameras;
};

CameraDataset loadCameraDataset(const std::filesystem::path& path);
CameraIntrinsics medianSimpleRadialCamera(const CameraDataset& dataset);

}  // namespace mvs
