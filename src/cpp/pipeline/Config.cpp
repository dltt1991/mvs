#include "pipeline/Config.h"

#include <filesystem>
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

}  // namespace

std::string Config::runName() const {
  const auto path = std::filesystem::path(outputDir);
  const auto name = path.filename().string();
  return name.empty() ? outputDir : name;
}

Config parseArgs(int argc, char** argv) {
  const auto options = collectOptions(argc, argv);
  Config config;
  config.imagesDir = requireOption(options, "--images");
  config.camerasJson = requireOption(options, "--cameras");
  config.outputDir = requireOption(options, "--output");
  config.colmapBinary = requireOption(options, "--colmap");
  config.openMvsBinDir = requireOption(options, "--openmvs-bin");
  return config;
}

}  // namespace mvs
