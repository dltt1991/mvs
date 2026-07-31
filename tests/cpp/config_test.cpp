#include "pipeline/Config.h"

#include <cassert>

int main() {
  const char* argv[] = {
      "mvs_reconstruct",
      "--images", "data/images",
      "--cameras", "data/cameras.json",
      "--output", "outputs/test-run",
      "--colmap", "build/third_party/colmap/bin/colmap",
      "--openmvs-bin", "build/third_party/openmvs/bin"};
  auto config = mvs::parseArgs(11, const_cast<char**>(argv));
  assert(config.imagesDir == "data/images");
  assert(config.camerasJson == "data/cameras.json");
  assert(config.outputDir == "outputs/test-run");
  assert(config.colmapBinary == "build/third_party/colmap/bin/colmap");
  assert(config.openMvsBinDir == "build/third_party/openmvs/bin");
  assert(config.runName() == "test-run");
  return 0;
}
