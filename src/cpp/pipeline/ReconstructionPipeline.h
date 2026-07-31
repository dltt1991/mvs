#pragma once

#include "pipeline/CameraConfig.h"
#include "pipeline/Config.h"

#include <filesystem>
#include <string>
#include <vector>

namespace mvs {

struct PipelineStage {
  std::string name;
  std::string displayName;
  std::vector<std::string> args;
  std::filesystem::path logFile;

  bool argsContains(const std::string& value) const;
};

struct PipelinePlan {
  std::filesystem::path outputDir;
  std::filesystem::path workspaceDir;
  std::vector<PipelineStage> stages;
};

PipelinePlan buildPipelinePlan(const Config& config, const CameraIntrinsics& camera);
int runPipeline(const Config& config);
std::string formatDuration(double seconds);

}  // namespace mvs
