#include "pipeline/Manifest.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace mvs {

void writeManifest(const Manifest& manifest, const std::filesystem::path& path) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("cannot write manifest: " + path.string());
  }

  nlohmann::json json;
  json["status"] = manifest.status;
  json["failed_stage"] = manifest.failedStage;
  json["artifacts"] = manifest.artifacts;
  json["stages"] = nlohmann::json::array();
  for (const auto& stage : manifest.stages) {
    json["stages"].push_back({
        {"name", stage.name},
        {"display_name", stage.displayName},
        {"status", stage.status},
        {"duration_seconds", stage.durationSeconds},
        {"log_file", stage.logFile},
        {"peak_resident_set_size_kb", stage.peakResidentSetSizeKb},
        {"user_cpu_seconds", stage.userCpuSeconds},
        {"system_cpu_seconds", stage.systemCpuSeconds},
    });
  }
  out << json.dump();
}

}  // namespace mvs
