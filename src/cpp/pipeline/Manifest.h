#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mvs {

struct StageManifestEntry {
  std::string name;
  std::string displayName;
  std::string status;
  double durationSeconds = 0.0;
  std::string logFile;
  long peakResidentSetSizeKb = 0;
  double userCpuSeconds = 0.0;
  double systemCpuSeconds = 0.0;
};

struct Manifest {
  std::string status;
  std::string failedStage;
  std::map<std::string, std::string> artifacts;
  std::vector<StageManifestEntry> stages;
};

void writeManifest(const Manifest& manifest, const std::filesystem::path& path);

}  // namespace mvs
