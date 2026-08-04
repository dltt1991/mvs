#include "pipeline/Config.h"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
  const char* argv[] = {
      "mvs_reconstruct",
      "--images", "data/images",
      "--cameras", "data/cameras.json",
      "--output", "outputs/test-run",
      "--colmap", "build/third_party/colmap/bin/colmap",
      "--openmvs-bin", "build/third_party/openmvs/bin",
      "--max-threads", "6",
      "--undistort-copy-policy", "SOFT_LINK",
      "--reuse-existing", "1",
      "--remove-depth-maps", "0",
      "--matcher", "sequential",
      "--sequential-overlap", "12",
      "--sequential-quadratic-overlap", "0",
      "--densify-number-views", "4",
      "--densify-number-views-fuse", "3",
      "--densify-geometric-iters", "1",
      "--densify-resolution-level", "2",
      "--densify-max-resolution", "2048",
      "--densify-iters", "2",
      "--generate-texture", "0",
      "--texture-patch-packing-heuristic", "100"};
  auto config = mvs::parseArgs(41, const_cast<char**>(argv));
  assert(config.imagesDir == "data/images");
  assert(config.camerasJson == "data/cameras.json");
  assert(config.outputDir == "outputs/test-run");
  assert(config.colmapBinary == "build/third_party/colmap/bin/colmap");
  assert(config.openMvsBinDir == "build/third_party/openmvs/bin");
  assert(config.maxThreads == 6);
  assert(config.undistortCopyPolicy == "SOFT_LINK");
  assert(config.reuseExisting == true);
  assert(config.removeDepthMaps == false);
  assert(config.matcher == "sequential");
  assert(config.sequentialOverlap == 12);
  assert(config.sequentialQuadraticOverlap == false);
  assert(config.densifyNumberViews == 4);
  assert(config.densifyNumberViewsFuse == 3);
  assert(config.densifyGeometricIters == 1);
  assert(config.densifyResolutionLevel == 2);
  assert(config.densifyMaxResolution == 2048);
  assert(config.densifyIters == 2);
  assert(config.generateTexture == false);
  assert(config.texturePatchPackingHeuristic == 100);
  assert(config.runName() == "test-run");

  const char* defaultArgv[] = {
      "mvs_reconstruct",
      "--images", "data/images",
      "--cameras", "data/cameras.json",
      "--output", "outputs/default-run",
      "--colmap", "colmap",
      "--openmvs-bin", "openmvs/bin"};
  const auto originalPath = std::filesystem::current_path();
  const auto isolatedPath = std::filesystem::temp_directory_path() / "mvs_config_default_fallback_test";
  std::filesystem::create_directories(isolatedPath);
  std::filesystem::current_path(isolatedPath);
  auto defaultConfig = mvs::parseArgs(11, const_cast<char**>(defaultArgv));
  std::filesystem::current_path(originalPath);
  assert(defaultConfig.maxThreads == 0);
  assert(defaultConfig.undistortCopyPolicy == "HARD_LINK");
  assert(defaultConfig.reuseExisting == false);
  assert(defaultConfig.removeDepthMaps == true);
  assert(defaultConfig.matcher == "exhaustive");
  assert(defaultConfig.sequentialOverlap == 10);
  assert(defaultConfig.sequentialQuadraticOverlap == true);
  assert(defaultConfig.densifyNumberViews == 5);
  assert(defaultConfig.densifyNumberViewsFuse == 2);
  assert(defaultConfig.densifyGeometricIters == 2);
  assert(defaultConfig.densifyResolutionLevel == 1);
  assert(defaultConfig.densifyMaxResolution == 2560);
  assert(defaultConfig.densifyIters == 3);
  assert(defaultConfig.generateTexture == true);
  assert(defaultConfig.texturePatchPackingHeuristic == 3);

  const auto configPath = std::filesystem::temp_directory_path() / "mvs_config_file_test.json";
  {
    std::ofstream out(configPath);
    out << R"({
      // Input image directory. Use an absolute path for external datasets.
      "images": "config/images",
      // COLMAP camera metadata generated for this image set.
      "cameras": "config/cameras.json",
      "output": "outputs/from-config",
      "colmap": "config/colmap",
      "openmvs_bin": "config/openmvs",
      "max_threads": 3,
      "undistort_copy_policy": "COPY",
      "reuse_existing": true,
      "remove_depth_maps": false,
      "matcher": "sequential",
      "sequential_overlap": 8,
      "sequential_quadratic_overlap": false,
      "densify_number_views": 3,
      "densify_number_views_fuse": 3,
      "densify_geometric_iters": 1,
      "densify_resolution_level": 2,
      "densify_max_resolution": 2048,
      "densify_iters": 2,
      "generate_texture": false,
      "texture_patch_packing_heuristic": 100
    })";
  }

  const auto configPathText = configPath.string();
  const char* configArgv[] = {
      "mvs_reconstruct",
      "--config", configPathText.c_str(),
      "--output", "outputs/from-cli",
      "--max-threads", "5",
      "--remove-depth-maps", "1",
      "--matcher", "exhaustive",
      "--sequential-quadratic-overlap", "1",
      "--densify-number-views", "4",
      "--densify-max-resolution", "1920",
      "--generate-texture", "1",
      "--texture-patch-packing-heuristic", "50"};
  auto fileConfig = mvs::parseArgs(21, const_cast<char**>(configArgv));
  assert(fileConfig.imagesDir == "config/images");
  assert(fileConfig.camerasJson == "config/cameras.json");
  assert(fileConfig.outputDir == "outputs/from-cli");
  assert(fileConfig.colmapBinary == "config/colmap");
  assert(fileConfig.openMvsBinDir == "config/openmvs");
  assert(fileConfig.maxThreads == 5);
  assert(fileConfig.undistortCopyPolicy == "COPY");
  assert(fileConfig.reuseExisting == true);
  assert(fileConfig.removeDepthMaps == true);
  assert(fileConfig.matcher == "exhaustive");
  assert(fileConfig.sequentialOverlap == 8);
  assert(fileConfig.sequentialQuadraticOverlap == true);
  assert(fileConfig.densifyNumberViews == 4);
  assert(fileConfig.densifyNumberViewsFuse == 3);
  assert(fileConfig.densifyGeometricIters == 1);
  assert(fileConfig.densifyResolutionLevel == 2);
  assert(fileConfig.densifyMaxResolution == 1920);
  assert(fileConfig.densifyIters == 2);
  assert(fileConfig.generateTexture == true);
  assert(fileConfig.texturePatchPackingHeuristic == 50);

  const auto repoRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
  std::filesystem::current_path(repoRoot);
  const char* noArgv[] = {"mvs_reconstruct"};
  auto defaultFileConfig = mvs::parseArgs(1, const_cast<char**>(noArgv));
  std::filesystem::current_path(originalPath);
  assert(defaultFileConfig.imagesDir == "data/images");
  assert(defaultFileConfig.camerasJson == "data/cameras.json");
  assert(defaultFileConfig.outputDir == "outputs/default");
  assert(defaultFileConfig.undistortCopyPolicy == "HARD_LINK");
  assert(defaultFileConfig.removeDepthMaps == true);
  assert(defaultFileConfig.matcher == "exhaustive");
  assert(defaultFileConfig.densifyNumberViews == 5);
  assert(defaultFileConfig.sequentialQuadraticOverlap == true);
  assert(defaultFileConfig.densifyNumberViewsFuse == 2);
  assert(defaultFileConfig.densifyGeometricIters == 1);
  assert(defaultFileConfig.densifyResolutionLevel == 1);
  assert(defaultFileConfig.densifyMaxResolution == 2560);
  assert(defaultFileConfig.densifyIters == 3);
  assert(defaultFileConfig.generateTexture == true);
  assert(defaultFileConfig.texturePatchPackingHeuristic == 100);
  return 0;
}
