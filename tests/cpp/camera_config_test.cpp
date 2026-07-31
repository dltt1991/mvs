#include "pipeline/CameraConfig.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>

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
    ]
  })";
  out.close();

  auto dataset = mvs::loadCameraDataset(path);
  assert(dataset.numImages == 2);
  assert(dataset.cameras.size() == 2);
  auto camera = mvs::medianSimpleRadialCamera(dataset);
  assert(camera.width == 100);
  assert(camera.height == 80);
  assert(std::abs(camera.f - 60.0) < 1e-9);
  assert(std::abs(camera.cx - 50.0) < 1e-9);
  assert(std::abs(camera.cy - 40.0) < 1e-9);
  assert(std::abs(camera.k1 - 0.02) < 1e-9);
  return 0;
}
