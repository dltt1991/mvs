#include "pipeline/Config.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>

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
      "--texture-patch-packing-heuristic", "100",
      "--texture-global-seam-leveling", "1",
      "--texture-local-seam-leveling", "1",
      "--use-opencv-undistort", "1",
      "--undistort-jpeg-quality", "95",
      "--feature-first-octave", "0"};
  auto config = mvs::parseArgs(51, const_cast<char**>(argv));
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
  assert(config.textureGlobalSeamLeveling == true);
  assert(config.textureLocalSeamLeveling == true);
  assert(config.useOpenCvUndistort == true);
  assert(config.undistortJpegQuality == 95);
  assert(config.featureFirstOctave == 0);
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
  assert(defaultConfig.useOpenCvUndistort == true);
  assert(defaultConfig.undistortJpegQuality == -1);
  assert(defaultConfig.featureFirstOctave == -1);
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
  assert(defaultConfig.textureGlobalSeamLeveling == false);
  assert(defaultConfig.textureLocalSeamLeveling == false);

  const char* noCamerasArgv[] = {
      "mvs_reconstruct",
      "--images", "data/images",
      "--output", "outputs/no-cameras",
      "--colmap", "colmap",
      "--openmvs-bin", "openmvs/bin"};
  auto noCamerasConfig = mvs::parseArgs(9, const_cast<char**>(noCamerasArgv));
  assert(noCamerasConfig.camerasJson.empty());

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
      "texture_patch_packing_heuristic": 100,
      "texture_global_seam_leveling": true,
      "texture_local_seam_leveling": true,
      "use_opencv_undistort": false,
      "undistort_jpeg_quality": -1,
      "feature_first_octave": 0
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
      "--texture-patch-packing-heuristic", "50",
      "--texture-global-seam-leveling", "0",
      "--texture-local-seam-leveling", "0"};
  auto fileConfig = mvs::parseArgs(25, const_cast<char**>(configArgv));
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
  assert(fileConfig.textureGlobalSeamLeveling == false);
  assert(fileConfig.textureLocalSeamLeveling == false);
  assert(fileConfig.useOpenCvUndistort == false);
  assert(fileConfig.undistortJpegQuality == -1);
  assert(fileConfig.featureFirstOctave == 0);

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
  assert(defaultFileConfig.textureGlobalSeamLeveling == false);
  assert(defaultFileConfig.textureLocalSeamLeveling == false);
  assert(defaultFileConfig.useOpenCvUndistort == true);
  assert(defaultFileConfig.undistortJpegQuality == -1);

  // JPEG 质量校验：-1 与 1-100 合法，其余必须拒绝。
  // 今天踩过"参数被静默忽略"的坑（parseArgs 里没 handler 的参数会被丢弃且不报错），
  // 这里显式钉住非法值一定抛异常。
  const auto parseQuality = [](const char* value) {
    const char* qArgv[] = {
        "mvs_reconstruct",
        "--images", "data/images",
        "--cameras", "data/cameras.json",
        "--output", "outputs/q",
        "--colmap", "colmap",
        "--openmvs-bin", "openmvs/bin",
        "--undistort-jpeg-quality", value};
    return mvs::parseArgs(13, const_cast<char**>(qArgv));
  };
  for (const char* bad : {"0", "101", "-2", "abc", ""}) {
    bool threw = false;
    try {
      parseQuality(bad);
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    assert(threw);
  }
  assert(parseQuality("1").undistortJpegQuality == 1);
  assert(parseQuality("100").undistortJpegQuality == 100);
  assert(parseQuality("-1").undistortJpegQuality == -1);

  // featureFirstOctave 校验：-1 到 4 合法，其余必须拒绝。
  const auto parseFirstOctave = [](const char* value) {
    const char* fArgv[] = {
        "mvs_reconstruct",
        "--images", "data/images",
        "--cameras", "data/cameras.json",
        "--output", "outputs/f",
        "--colmap", "colmap",
        "--openmvs-bin", "openmvs/bin",
        "--feature-first-octave", value};
    return mvs::parseArgs(13, const_cast<char**>(fArgv));
  };
  for (const char* bad : {"-2", "5", "abc", ""}) {
    bool threw = false;
    try {
      parseFirstOctave(bad);
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    assert(threw);
  }
  assert(parseFirstOctave("-1").featureFirstOctave == -1);
  assert(parseFirstOctave("0").featureFirstOctave == 0);
  assert(parseFirstOctave("1").featureFirstOctave == 1);
  assert(parseFirstOctave("4").featureFirstOctave == 4);

  return 0;
}
