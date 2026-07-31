#pragma once

#include <string>

namespace mvs {

struct Config {
  std::string imagesDir;
  std::string camerasJson;
  std::string outputDir;
  std::string colmapBinary;
  std::string openMvsBinDir;

  std::string runName() const;
};

Config parseArgs(int argc, char** argv);

}  // namespace mvs
