#include "pipeline/CameraConfig.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>

namespace mvs {
namespace {

double median(std::vector<double> values) {
  if (values.empty()) {
    throw std::invalid_argument("cannot compute median of empty values");
  }
  std::sort(values.begin(), values.end());
  const auto middle = values.size() / 2;
  if (values.size() % 2 == 1) {
    return values[middle];
  }
  return (values[middle - 1] + values[middle]) / 2.0;
}

void validateCamera(const CameraIntrinsics& camera) {
  if (camera.model != "SIMPLE_RADIAL") {
    throw std::invalid_argument("unsupported camera model: " + camera.model);
  }
  if (camera.width <= 0 || camera.height <= 0) {
    throw std::invalid_argument("camera dimensions must be positive");
  }
  if (camera.f <= 0.0) {
    throw std::invalid_argument("camera focal length must be positive");
  }
}

bool isSupportedImageFile(const std::filesystem::path& path) {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".tif" || ext == ".tiff";
}

bool readCenter(const nlohmann::json& item, CameraPoseReference* reference) {
  if (item.contains("center")) {
    const auto center = item.at("center").get<std::vector<double>>();
    if (center.size() != 3) return false;
    reference->x = center[0];
    reference->y = center[1];
    reference->z = center[2];
    return true;
  }

  if (!item.contains("rotation") || !item.contains("translation")) {
    return false;
  }
  const auto rotation = item.at("rotation").get<std::vector<std::vector<double>>>();
  const auto translation = item.at("translation").get<std::vector<double>>();
  if (rotation.size() != 3 || translation.size() != 3) return false;
  for (const auto& row : rotation) {
    if (row.size() != 3) return false;
  }

  reference->x = -(rotation[0][0] * translation[0] +
                   rotation[1][0] * translation[1] +
                   rotation[2][0] * translation[2]);
  reference->y = -(rotation[0][1] * translation[0] +
                   rotation[1][1] * translation[1] +
                   rotation[2][1] * translation[2]);
  reference->z = -(rotation[0][2] * translation[0] +
                   rotation[1][2] * translation[1] +
                   rotation[2][2] * translation[2]);
  return true;
}

}  // namespace

CameraDataset loadCameraDataset(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::invalid_argument("cannot open camera json: " + path.string());
  }

  nlohmann::json json;
  input >> json;

  CameraDataset dataset;
  dataset.numCameras = json.at("num_cameras").get<int>();
  dataset.numImages = json.at("num_images").get<int>();
  dataset.numRegistered = json.value("num_registered", 0);
  if (dataset.numImages <= 0) {
    throw std::invalid_argument("num_images must be positive");
  }

  const auto& cameras = json.at("cameras");
  if (!cameras.is_array() || cameras.empty()) {
    throw std::invalid_argument("cameras must be a non-empty array");
  }

  for (const auto& item : cameras) {
    const auto params = item.at("params").get<std::vector<double>>();
    if (params.size() != 4) {
      throw std::invalid_argument("SIMPLE_RADIAL camera params must contain four values");
    }

    CameraIntrinsics camera;
    camera.id = item.at("id").get<int>();
    camera.model = item.at("model").get<std::string>();
    camera.width = item.at("width").get<int>();
    camera.height = item.at("height").get<int>();
    camera.f = params[0];
    camera.cx = params[1];
    camera.cy = params[2];
    camera.k1 = params[3];
    validateCamera(camera);
    dataset.cameras.push_back(camera);
  }

  if (dataset.numCameras != 0 && dataset.numCameras != static_cast<int>(dataset.cameras.size())) {
    throw std::invalid_argument("num_cameras does not match cameras array size");
  }

  if (json.contains("images") && json.at("images").is_array()) {
    for (const auto& item : json.at("images")) {
      if (!item.value("registered", true) || !item.contains("name")) {
        continue;
      }
      CameraPoseReference reference;
      reference.name = item.at("name").get<std::string>();
      if (!reference.name.empty() && readCenter(item, &reference)) {
        dataset.poseReferences.push_back(reference);
      }
    }
  }

  return dataset;
}

CameraIntrinsics medianSimpleRadialCamera(const CameraDataset& dataset) {
  if (dataset.cameras.empty()) {
    throw std::invalid_argument("camera dataset is empty");
  }

  std::vector<double> f;
  std::vector<double> cx;
  std::vector<double> cy;
  std::vector<double> k1;
  f.reserve(dataset.cameras.size());
  cx.reserve(dataset.cameras.size());
  cy.reserve(dataset.cameras.size());
  k1.reserve(dataset.cameras.size());

  const auto width = dataset.cameras.front().width;
  const auto height = dataset.cameras.front().height;
  for (const auto& camera : dataset.cameras) {
    validateCamera(camera);
    if (camera.width != width || camera.height != height) {
      throw std::invalid_argument("all cameras must use the same image dimensions");
    }
    f.push_back(camera.f);
    cx.push_back(camera.cx);
    cy.push_back(camera.cy);
    k1.push_back(camera.k1);
  }

  CameraIntrinsics camera;
  camera.id = 1;
  camera.model = "SIMPLE_RADIAL";
  camera.width = width;
  camera.height = height;
  camera.f = median(f);
  camera.cx = median(cx);
  camera.cy = median(cy);
  camera.k1 = median(k1);
  return camera;
}

CameraIntrinsics estimateSimpleRadialCameraFromImages(const std::filesystem::path& imagesDir) {
  std::error_code error;
  if (!std::filesystem::exists(imagesDir, error) || !std::filesystem::is_directory(imagesDir, error)) {
    throw std::invalid_argument("cannot read image directory for camera fallback: " + imagesDir.string());
  }

  std::vector<std::filesystem::path> imagePaths;
  for (const auto& entry : std::filesystem::directory_iterator(imagesDir, error)) {
    if (entry.is_regular_file(error) && isSupportedImageFile(entry.path())) {
      imagePaths.push_back(entry.path());
    }
  }
  if (error) {
    throw std::invalid_argument("cannot list image directory for camera fallback: " + imagesDir.string());
  }
  if (imagePaths.empty()) {
    throw std::invalid_argument("no supported images found for camera fallback: " + imagesDir.string());
  }
  std::sort(imagePaths.begin(), imagePaths.end());

  int width = 0;
  int height = 0;
  for (const auto& imagePath : imagePaths) {
    const auto image = cv::imread(imagePath.string(), cv::IMREAD_COLOR);
    if (image.empty()) {
      throw std::invalid_argument("cannot read image for camera fallback: " + imagePath.string());
    }
    if (width == 0 && height == 0) {
      width = image.cols;
      height = image.rows;
      continue;
    }
    if (image.cols != width || image.rows != height) {
      throw std::invalid_argument("all images must use the same dimensions for camera fallback");
    }
  }

  CameraIntrinsics camera;
  camera.id = 1;
  camera.model = "SIMPLE_RADIAL";
  camera.width = width;
  camera.height = height;
  camera.f = 1.2 * static_cast<double>(std::max(width, height));
  camera.cx = static_cast<double>(width) / 2.0;
  camera.cy = static_cast<double>(height) / 2.0;
  camera.k1 = 0.0;
  validateCamera(camera);
  return camera;
}

}  // namespace mvs
