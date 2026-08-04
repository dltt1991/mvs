#include "pipeline/Manifest.h"
#include "pipeline/ProcessRunner.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
  const auto dir = std::filesystem::temp_directory_path() / "mvs_process_test";
  std::filesystem::create_directories(dir);
  auto result = mvs::runCommand({"/bin/sh", "-c", "printf ok"}, dir, dir / "cmd.log");
  assert(result.exitCode == 0);
  assert(result.stdoutText == "ok");
  assert(result.peakResidentSetSizeKb >= 0);
  assert(result.userCpuSeconds >= 0.0);
  assert(result.systemCpuSeconds >= 0.0);
  assert(std::filesystem::exists(dir / "cmd.log"));

  mvs::Manifest manifest;
  manifest.status = "ok";
  manifest.failedStage = "";
  manifest.artifacts["sparse_model"] = "outputs/test/colmap/sparse/0";
  mvs::StageManifestEntry stage;
  stage.name = "feature_extractor";
  stage.displayName = "COLMAP / pycolmap：特征提取";
  stage.status = "ok";
  stage.durationSeconds = 1.25;
  stage.logFile = "";
  stage.peakResidentSetSizeKb = result.peakResidentSetSizeKb;
  stage.userCpuSeconds = result.userCpuSeconds;
  stage.systemCpuSeconds = result.systemCpuSeconds;
  manifest.stages.push_back(stage);
  mvs::writeManifest(manifest, dir / "manifest.json");

  std::ifstream in(dir / "manifest.json");
  std::string json((std::istreambuf_iterator<char>(in)), {});
  assert(json.find("\"status\":\"ok\"") != std::string::npos);
  assert(json.find("sparse_model") != std::string::npos);
  assert(json.find("\"duration_seconds\":1.25") != std::string::npos);
  assert(json.find("peak_resident_set_size_kb") != std::string::npos);
  assert(json.find("user_cpu_seconds") != std::string::npos);
  assert(json.find("system_cpu_seconds") != std::string::npos);
  assert(json.find("COLMAP / pycolmap") != std::string::npos);
  return 0;
}
