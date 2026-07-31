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
  assert(std::filesystem::exists(dir / "cmd.log"));

  mvs::Manifest manifest;
  manifest.status = "ok";
  manifest.failedStage = "";
  manifest.artifacts["sparse_model"] = "outputs/test/colmap/sparse/0";
  manifest.stages.push_back({"feature_extractor", "COLMAP / pycolmap：特征提取", "ok", 1.25, ""});
  mvs::writeManifest(manifest, dir / "manifest.json");

  std::ifstream in(dir / "manifest.json");
  std::string json((std::istreambuf_iterator<char>(in)), {});
  assert(json.find("\"status\":\"ok\"") != std::string::npos);
  assert(json.find("sparse_model") != std::string::npos);
  assert(json.find("\"duration_seconds\":1.25") != std::string::npos);
  assert(json.find("COLMAP / pycolmap") != std::string::npos);
  return 0;
}
