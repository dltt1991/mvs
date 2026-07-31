#include "pipeline/CameraConfig.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

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

}  // namespace mvs
