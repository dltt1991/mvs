#include "pipeline/Config.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace mvs {
namespace {

std::unordered_map<std::string, std::string> collectOptions(int argc, char** argv) {
  std::unordered_map<std::string, std::string> options;
  for (int i = 1; i < argc; i += 2) {
    const std::string key = argv[i];
    if (key.rfind("--", 0) != 0) {
      throw std::invalid_argument("unexpected argument: " + key);
    }
    if (i + 1 >= argc) {
      throw std::invalid_argument("missing value for " + key);
    }
    options[key] = argv[i + 1];
  }
  return options;
}

std::string requireOption(const std::unordered_map<std::string, std::string>& options,
                          const std::string& key) {
  const auto found = options.find(key);
  if (found == options.end() || found->second.empty()) {
    throw std::invalid_argument("missing required option " + key);
  }
  return found->second;
}

bool hasOption(const std::unordered_map<std::string, std::string>& options,
               const std::string& key) {
  return options.find(key) != options.end();
}

std::string optionalOption(const std::unordered_map<std::string, std::string>& options,
                           const std::string& key,
                           const std::string& defaultValue) {
  const auto found = options.find(key);
  if (found == options.end()) {
    return defaultValue;
  }
  return found->second;
}

int parseNonNegativeInt(const std::string& value, const std::string& optionName) {
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || parsed < 0) {
    throw std::invalid_argument("invalid non-negative integer for " + optionName + ": " + value);
  }
  return static_cast<int>(parsed);
}

// OpenMVS 的 --cuda-device 语义：-1 = 自动选最佳 GPU，-2 = 强制 CPU，>=0 = 设备号。
int parseCudaDevice(const std::string& value, const std::string& optionName) {
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || parsed < -2) {
    throw std::invalid_argument("invalid CUDA device for " + optionName + " (expected >= -2): " + value);
  }
  return static_cast<int>(parsed);
}

// COLMAP image_undistorter --jpeg_quality：-1 = 沿用 COLMAP 默认，否则 1..100。
int parseJpegQuality(const std::string& value, const std::string& optionName) {
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' ||
      (parsed != -1 && (parsed < 1 || parsed > 100))) {
    throw std::invalid_argument(
        "invalid JPEG quality for " + optionName + " (expected -1 or 1-100): " + value);
  }
  return static_cast<int>(parsed);
}

std::string parseCopyPolicy(const std::string& value) {
  if (value == "COPY" || value == "SOFT_LINK" || value == "HARD_LINK") {
    return value;
  }
  throw std::invalid_argument(
      "invalid --undistort-copy-policy: " + value + " (expected COPY, SOFT_LINK, or HARD_LINK)");
}

std::string parseMatcher(const std::string& value) {
  if (value == "exhaustive" || value == "sequential") {
    return value;
  }
  throw std::invalid_argument("invalid --matcher: " + value + " (expected exhaustive or sequential)");
}

bool parseBool(const std::string& value, const std::string& optionName) {
  if (value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES") {
    return true;
  }
  if (value == "0" || value == "false" || value == "FALSE" || value == "no" || value == "NO") {
    return false;
  }
  throw std::invalid_argument("invalid boolean for " + optionName + ": " + value);
}

void applyJsonString(const nlohmann::json& json,
                     const std::string& key,
                     std::string& target) {
  if (!json.contains(key)) {
    return;
  }
  if (!json[key].is_string()) {
    throw std::invalid_argument("config field must be a string: " + key);
  }
  target = json[key].get<std::string>();
}

void applyJsonInt(const nlohmann::json& json,
                  const std::string& key,
                  int& target) {
  if (!json.contains(key)) {
    return;
  }
  if (!json[key].is_number_integer() || json[key].get<int>() < 0) {
    throw std::invalid_argument("config field must be a non-negative integer: " + key);
  }
  target = json[key].get<int>();
}

// cuda_device 允许负值：-1 = 自动选最佳 GPU，-2 = 强制 CPU（OpenMVS 语义）。
void applyJsonCudaDevice(const nlohmann::json& json,
                         const std::string& key,
                         int& target) {
  if (!json.contains(key)) {
    return;
  }
  if (!json[key].is_number_integer() || json[key].get<int>() < -2) {
    throw std::invalid_argument("config field must be an integer >= -2: " + key);
  }
  target = json[key].get<int>();
}

// 去畸变 JPEG 质量：-1 = 沿用后端默认，否则 1..100。
void applyJsonJpegQuality(const nlohmann::json& json,
                          const std::string& key,
                          int& target) {
  if (!json.contains(key)) {
    return;
  }
  if (!json[key].is_number_integer()) {
    throw std::invalid_argument("config field must be an integer: " + key);
  }
  const int value = json[key].get<int>();
  if (value != -1 && (value < 1 || value > 100)) {
    throw std::invalid_argument("config field must be -1 or 1-100: " + key);
  }
  target = value;
}

// SIFT first_octave：-1（上采样）、0（标准）、1+（下采样，少用）。
// COLMAP 接受 -1~4，但 >1 的语义是跳过低频金字塔，车载场景几乎不用。
void applyJsonFirstOctave(const nlohmann::json& json,
                          const std::string& key,
                          int& target) {
  if (!json.contains(key)) {
    return;
  }
  if (!json[key].is_number_integer()) {
    throw std::invalid_argument("config field must be an integer: " + key);
  }
  const int value = json[key].get<int>();
  if (value < -1 || value > 4) {
    throw std::invalid_argument("config field must be -1 to 4: " + key);
  }
  target = value;
}

void applyJsonBool(const nlohmann::json& json,
                   const std::string& key,
                   bool& target) {
  if (!json.contains(key)) {
    return;
  }
  if (!json[key].is_boolean()) {
    throw std::invalid_argument("config field must be a boolean: " + key);
  }
  target = json[key].get<bool>();
}

}  // namespace (anonymous)

void applyConfigFile(Config& config, const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::invalid_argument("cannot read config file: " + path.string());
  }

  auto json = nlohmann::json::parse(in, nullptr, true, true);
  if (!json.is_object()) {
    throw std::invalid_argument("config file must contain a JSON object: " + path.string());
  }

  applyJsonString(json, "images", config.imagesDir);
  applyJsonString(json, "cameras", config.camerasJson);
  applyJsonString(json, "output", config.outputDir);
  applyJsonString(json, "colmap", config.colmapBinary);
  applyJsonString(json, "openmvs_bin", config.openMvsBinDir);
  applyJsonInt(json, "max_threads", config.maxThreads);
  applyJsonString(json, "undistort_copy_policy", config.undistortCopyPolicy);
  applyJsonJpegQuality(json, "undistort_jpeg_quality", config.undistortJpegQuality);
  applyJsonBool(json, "reuse_existing", config.reuseExisting);
  applyJsonBool(json, "remove_depth_maps", config.removeDepthMaps);
  applyJsonString(json, "matcher", config.matcher);
  applyJsonInt(json, "sequential_overlap", config.sequentialOverlap);
  applyJsonBool(json, "sequential_quadratic_overlap", config.sequentialQuadraticOverlap);
  applyJsonBool(json, "mapper_ba_use_gpu", config.mapperBundleAdjustmentGpu);
  applyJsonCudaDevice(json, "cuda_device", config.cudaDevice);
  applyJsonBool(json, "enable_batched_pipeline", config.enableBatchedPipeline);
  applyJsonInt(json, "batch_size", config.batchSize);
  applyJsonInt(json, "batch_overlap", config.batchOverlap);
  applyJsonBool(json, "use_opencv_undistort", config.useOpenCvUndistort);
  applyJsonFirstOctave(json, "feature_first_octave", config.featureFirstOctave);
  applyJsonInt(json, "densify_number_views", config.densifyNumberViews);
  applyJsonInt(json, "densify_number_views_fuse", config.densifyNumberViewsFuse);
  applyJsonInt(json, "densify_geometric_iters", config.densifyGeometricIters);
  applyJsonInt(json, "densify_resolution_level", config.densifyResolutionLevel);
  applyJsonInt(json, "densify_max_resolution", config.densifyMaxResolution);
  applyJsonInt(json, "densify_iters", config.densifyIters);
  applyJsonBool(json, "generate_texture", config.generateTexture);
  applyJsonInt(json, "texture_patch_packing_heuristic", config.texturePatchPackingHeuristic);
  applyJsonBool(json, "texture_global_seam_leveling", config.textureGlobalSeamLeveling);
  applyJsonBool(json, "texture_local_seam_leveling", config.textureLocalSeamLeveling);
}

void requireConfigValue(const std::string& value, const std::string& name) {
  if (value.empty()) {
    throw std::invalid_argument("missing required config value " + name);
  }
}

std::string Config::runName() const {
  const auto path = std::filesystem::path(outputDir);
  const auto name = path.filename().string();
  return name.empty() ? outputDir : name;
}

Config parseArgs(int argc, char** argv) {
  const auto options = collectOptions(argc, argv);
  Config config;
  const auto configPath = optionalOption(options, "--config", "config/reconstruction.json");
  const bool explicitConfig = hasOption(options, "--config");
  if (std::filesystem::exists(configPath)) {
    applyConfigFile(config, configPath);
  } else if (explicitConfig) {
    throw std::invalid_argument("cannot read config file: " + configPath);
  }

  if (hasOption(options, "--images")) {
    config.imagesDir = requireOption(options, "--images");
  }
  if (hasOption(options, "--cameras")) {
    config.camerasJson = requireOption(options, "--cameras");
  }
  if (hasOption(options, "--output")) {
    config.outputDir = requireOption(options, "--output");
  }
  if (hasOption(options, "--colmap")) {
    config.colmapBinary = requireOption(options, "--colmap");
  }
  if (hasOption(options, "--openmvs-bin")) {
    config.openMvsBinDir = requireOption(options, "--openmvs-bin");
  }
  if (hasOption(options, "--max-threads")) {
    config.maxThreads = parseNonNegativeInt(optionalOption(options, "--max-threads", "0"), "--max-threads");
  }
  if (hasOption(options, "--undistort-copy-policy")) {
    config.undistortCopyPolicy = parseCopyPolicy(
        optionalOption(options, "--undistort-copy-policy", "HARD_LINK"));
  } else {
    config.undistortCopyPolicy = parseCopyPolicy(config.undistortCopyPolicy);
  }
  if (hasOption(options, "--use-opencv-undistort")) {
    config.useOpenCvUndistort = parseBool(
        optionalOption(options, "--use-opencv-undistort", "0"),
        "--use-opencv-undistort");
  }
  if (hasOption(options, "--undistort-jpeg-quality")) {
    config.undistortJpegQuality = parseJpegQuality(
        optionalOption(options, "--undistort-jpeg-quality", "-1"),
        "--undistort-jpeg-quality");
  }
  if (hasOption(options, "--feature-first-octave")) {
    const std::string value = optionalOption(options, "--feature-first-octave", "-1");
    try {
      const int parsed = std::stoi(value);
      if (parsed < -1 || parsed > 4) {
        throw std::invalid_argument("expected -1 to 4");
      }
      config.featureFirstOctave = parsed;
    } catch (const std::exception& e) {
      throw std::invalid_argument("invalid value for --feature-first-octave: " + value +
                                  " (" + e.what() + ")");
    }
  }
  if (hasOption(options, "--reuse-existing")) {
    config.reuseExisting = parseBool(optionalOption(options, "--reuse-existing", "0"), "--reuse-existing");
  }
  if (hasOption(options, "--remove-depth-maps")) {
    config.removeDepthMaps = parseBool(optionalOption(options, "--remove-depth-maps", "1"), "--remove-depth-maps");
  }
  if (hasOption(options, "--matcher")) {
    config.matcher = parseMatcher(optionalOption(options, "--matcher", "exhaustive"));
  } else {
    config.matcher = parseMatcher(config.matcher);
  }
  if (hasOption(options, "--sequential-overlap")) {
    config.sequentialOverlap =
        parseNonNegativeInt(optionalOption(options, "--sequential-overlap", "10"), "--sequential-overlap");
  }
  if (hasOption(options, "--sequential-quadratic-overlap")) {
    config.sequentialQuadraticOverlap = parseBool(
        optionalOption(options, "--sequential-quadratic-overlap", "1"),
        "--sequential-quadratic-overlap");
  }
  if (hasOption(options, "--mapper-ba-use-gpu")) {
    config.mapperBundleAdjustmentGpu = parseBool(
        optionalOption(options, "--mapper-ba-use-gpu", "0"),
        "--mapper-ba-use-gpu");
  }
  if (hasOption(options, "--cuda-device")) {
    config.cudaDevice = parseCudaDevice(optionalOption(options, "--cuda-device", "-1"), "--cuda-device");
  }
  if (hasOption(options, "--enable-batched-pipeline")) {
    config.enableBatchedPipeline = parseBool(
        optionalOption(options, "--enable-batched-pipeline", "0"),
        "--enable-batched-pipeline");
  }
  if (hasOption(options, "--batch-size")) {
    config.batchSize = parseNonNegativeInt(optionalOption(options, "--batch-size", "10"), "--batch-size");
  }
  if (hasOption(options, "--batch-overlap")) {
    config.batchOverlap = parseNonNegativeInt(optionalOption(options, "--batch-overlap", "5"), "--batch-overlap");
  }
  if (hasOption(options, "--densify-number-views")) {
    config.densifyNumberViews =
        parseNonNegativeInt(optionalOption(options, "--densify-number-views", "5"), "--densify-number-views");
  }
  if (hasOption(options, "--densify-number-views-fuse")) {
    config.densifyNumberViewsFuse = parseNonNegativeInt(
        optionalOption(options, "--densify-number-views-fuse", "2"),
        "--densify-number-views-fuse");
  }
  if (hasOption(options, "--densify-geometric-iters")) {
    config.densifyGeometricIters =
        parseNonNegativeInt(optionalOption(options, "--densify-geometric-iters", "2"), "--densify-geometric-iters");
  }
  if (hasOption(options, "--densify-resolution-level")) {
    config.densifyResolutionLevel = parseNonNegativeInt(
        optionalOption(options, "--densify-resolution-level", "1"),
        "--densify-resolution-level");
  }
  if (hasOption(options, "--densify-max-resolution")) {
    config.densifyMaxResolution = parseNonNegativeInt(
        optionalOption(options, "--densify-max-resolution", "2560"),
        "--densify-max-resolution");
  }
  if (hasOption(options, "--densify-iters")) {
    config.densifyIters =
        parseNonNegativeInt(optionalOption(options, "--densify-iters", "3"), "--densify-iters");
  }
  if (hasOption(options, "--generate-texture")) {
    config.generateTexture = parseBool(optionalOption(options, "--generate-texture", "1"), "--generate-texture");
  }
  if (hasOption(options, "--texture-patch-packing-heuristic")) {
    config.texturePatchPackingHeuristic = parseNonNegativeInt(
        optionalOption(options, "--texture-patch-packing-heuristic", "3"),
        "--texture-patch-packing-heuristic");
  }
  if (hasOption(options, "--texture-global-seam-leveling")) {
    config.textureGlobalSeamLeveling = parseBool(
        optionalOption(options, "--texture-global-seam-leveling", "0"),
        "--texture-global-seam-leveling");
  }
  if (hasOption(options, "--texture-local-seam-leveling")) {
    config.textureLocalSeamLeveling = parseBool(
        optionalOption(options, "--texture-local-seam-leveling", "0"),
        "--texture-local-seam-leveling");
  }

  requireConfigValue(config.imagesDir, "images");
  requireConfigValue(config.outputDir, "output");
  requireConfigValue(config.colmapBinary, "colmap");
  requireConfigValue(config.openMvsBinDir, "openmvs_bin");
  return config;
}

}  // namespace mvs
