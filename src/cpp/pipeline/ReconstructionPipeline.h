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
  std::filesystem::path markerFile;
  std::vector<std::filesystem::path> expectedArtifacts;
  std::string signature;

  bool argsContains(const std::string& value) const;
};

struct PipelinePlan {
  std::filesystem::path outputDir;
  std::filesystem::path workspaceDir;
  std::filesystem::path imageListFile;
  std::vector<PipelineStage> stages;
};

PipelinePlan buildPipelinePlan(const Config& config, const CameraIntrinsics& camera);
bool stageArtifactsReady(const PipelineStage& stage);
void writeSortedImageList(const std::filesystem::path& imagesDir,
                          const std::filesystem::path& imageListFile);
int runPipeline(const Config& config);
std::string formatDuration(double seconds);

}  // namespace mvs
