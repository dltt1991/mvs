#pragma once

#include <string>

namespace mvs {

struct Config {
  std::string imagesDir;
  std::string camerasJson;
  std::string outputDir;
  std::string colmapBinary;
  std::string openMvsBinDir;
  int maxThreads = 0;
  std::string undistortCopyPolicy = "HARD_LINK";
  bool reuseExisting = false;
  bool removeDepthMaps = true;
  std::string matcher = "exhaustive";
  int sequentialOverlap = 10;
  bool sequentialQuadraticOverlap = true;
  int densifyNumberViews = 5;
  int densifyNumberViewsFuse = 2;
  int densifyGeometricIters = 2;
  int densifyResolutionLevel = 1;
  int densifyMaxResolution = 2560;
  int densifyIters = 3;
  bool generateTexture = true;
  int texturePatchPackingHeuristic = 3;

  std::string runName() const;
};

Config parseArgs(int argc, char** argv);

}  // namespace mvs
