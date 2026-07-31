#include "pipeline/ReconstructionPipeline.h"

#include "pipeline/Manifest.h"
#include "pipeline/ProcessRunner.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mvs {
namespace {

std::string number(double value) {
  std::ostringstream out;
  out.precision(12);
  out << value;
  return out.str();
}

std::filesystem::path openMvsTool(const Config& config, const std::string& name) {
  return std::filesystem::path(config.openMvsBinDir) / name;
}

std::vector<std::string> withBinary(const std::filesystem::path& binary,
                                    std::vector<std::string> args) {
  args.insert(args.begin(), binary.string());
  return args;
}

PipelineStage stage(const std::string& name,
                    const std::string& displayName,
                    std::vector<std::string> args,
                    const std::filesystem::path& logDir) {
  PipelineStage result;
  result.name = name;
  result.displayName = displayName;
  result.args = std::move(args);
  result.logFile = logDir / (name + ".log");
  return result;
}

double elapsedSeconds(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

std::string formatDuration(double seconds) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(2) << seconds << "s";
  return out.str();
}

bool PipelineStage::argsContains(const std::string& value) const {
  for (const auto& arg : args) {
    if (arg == value) {
      return true;
    }
  }
  return false;
}

PipelinePlan buildPipelinePlan(const Config& config, const CameraIntrinsics& camera) {
  PipelinePlan plan;
  plan.outputDir = config.outputDir;
  plan.workspaceDir = std::filesystem::path(config.outputDir);

  const auto colmapDir = plan.workspaceDir / "colmap";
  const auto openMvsDir = plan.workspaceDir / "openmvs";
  const auto logsDir = plan.workspaceDir / "logs";
  const auto database = colmapDir / "database.db";
  const auto sparseDir = colmapDir / "sparse";
  const auto denseDir = colmapDir / "dense";
  const auto sparseModel = sparseDir / "0";
  const auto scene = openMvsDir / "scene.mvs";
  const auto denseScene = openMvsDir / "scene_dense.mvs";
  const auto meshScene = openMvsDir / "scene_mesh.mvs";

  const auto cameraParams =
      number(camera.f) + "," + number(camera.cx) + "," + number(camera.cy) + "," + number(camera.k1);

  plan.stages.push_back(stage(
      "feature_extractor",
      "COLMAP / pycolmap：特征提取",
      {config.colmapBinary,
       "feature_extractor",
       "--database_path",
       database.string(),
       "--image_path",
       config.imagesDir,
       "--ImageReader.single_camera",
       "1",
       "--ImageReader.camera_model",
       camera.model,
       "--ImageReader.camera_params",
       cameraParams},
      logsDir));

  plan.stages.push_back(stage(
      "exhaustive_matcher",
      "COLMAP / pycolmap：特征匹配",
      {config.colmapBinary, "exhaustive_matcher", "--database_path", database.string()},
      logsDir));

  plan.stages.push_back(stage(
      "mapper",
      "COLMAP / pycolmap：增量 SfM、相机位姿估计、稀疏点云重建",
      {config.colmapBinary,
       "mapper",
       "--database_path",
       database.string(),
       "--image_path",
       config.imagesDir,
       "--output_path",
       sparseDir.string()},
      logsDir));

  plan.stages.push_back(stage(
      "image_undistorter",
      "COLMAP：图像去畸变和 dense workspace 准备",
      {config.colmapBinary,
       "image_undistorter",
       "--image_path",
       config.imagesDir,
       "--input_path",
       sparseModel.string(),
       "--output_path",
       denseDir.string(),
       "--output_type",
       "COLMAP"},
      logsDir));

  plan.stages.push_back(stage(
      "interface_colmap",
      "OpenMVS：InterfaceCOLMAP 转换为 scene.mvs",
      withBinary(openMvsTool(config, "InterfaceCOLMAP"),
                 {"--input-file", denseDir.string(), "--output-file", scene.string()}),
      logsDir));

  plan.stages.push_back(stage(
      "densify_point_cloud",
      "OpenMVS：DensifyPointCloud 生成稠密点云",
      withBinary(openMvsTool(config, "DensifyPointCloud"),
                 {scene.string(), "--working-folder", openMvsDir.string(), "--output-file", denseScene.filename().string()}),
      logsDir));

  plan.stages.push_back(stage(
      "reconstruct_mesh",
      "OpenMVS：ReconstructMesh 生成三角网格",
      withBinary(openMvsTool(config, "ReconstructMesh"),
                 {denseScene.string(), "--working-folder", openMvsDir.string(), "--output-file", meshScene.filename().string()}),
      logsDir));

  plan.stages.push_back(stage(
      "texture_mesh",
      "OpenMVS：TextureMesh 生成纹理模型",
      withBinary(openMvsTool(config, "TextureMesh"),
                 {meshScene.string(), "--working-folder", openMvsDir.string(), "--output-file", "scene_texture.mvs"}),
      logsDir));

  return plan;
}

int runPipeline(const Config& config) {
  const auto runStart = std::chrono::steady_clock::now();
  std::cout << "== 多视角三维重建开始 ==\n";
  std::cout << "[1/11] 多张输入图片: " << config.imagesDir << "\n";
  std::cout << "[2/11] 读取相机参数: " << config.camerasJson << std::flush;
  const auto cameraStart = std::chrono::steady_clock::now();
  const auto dataset = loadCameraDataset(config.camerasJson);
  const auto camera = medianSimpleRadialCamera(dataset);
  std::cout << " 完成 (" << formatDuration(elapsedSeconds(cameraStart)) << ")"
            << "，图片数: " << dataset.numImages
            << "，已注册: " << dataset.numRegistered << "\n";

  const auto plan = buildPipelinePlan(config, camera);

  std::filesystem::create_directories(plan.workspaceDir / "colmap" / "sparse");
  std::filesystem::create_directories(plan.workspaceDir / "colmap" / "dense");
  std::filesystem::create_directories(plan.workspaceDir / "openmvs");
  std::filesystem::create_directories(plan.workspaceDir / "logs");

  Manifest manifest;
  manifest.status = "running";
  manifest.failedStage = "";
  manifest.artifacts["colmap_database"] = (plan.workspaceDir / "colmap" / "database.db").string();
  manifest.artifacts["colmap_sparse"] = (plan.workspaceDir / "colmap" / "sparse" / "0").string();
  manifest.artifacts["colmap_dense"] = (plan.workspaceDir / "colmap" / "dense").string();
  manifest.artifacts["openmvs_scene"] = (plan.workspaceDir / "openmvs" / "scene.mvs").string();
  manifest.artifacts["openmvs_dense"] = (plan.workspaceDir / "openmvs" / "scene_dense.mvs").string();
  manifest.artifacts["openmvs_mesh"] = (plan.workspaceDir / "openmvs" / "scene_mesh.mvs").string();
  manifest.artifacts["openmvs_texture"] = (plan.workspaceDir / "openmvs" / "scene_texture.mvs").string();

  const auto manifestPath = plan.workspaceDir / "manifest.json";
  writeManifest(manifest, manifestPath);

  int step = 3;
  for (const auto& pipelineStage : plan.stages) {
    std::cout << "[" << step << "/11] " << pipelineStage.displayName << "\n";
    std::cout << "      日志: " << pipelineStage.logFile.string() << "\n";
    const auto stageStart = std::chrono::steady_clock::now();
    const auto result = runCommand(pipelineStage.args, plan.workspaceDir, pipelineStage.logFile);
    const auto duration = elapsedSeconds(stageStart);
    StageManifestEntry stageEntry;
    stageEntry.name = pipelineStage.name;
    stageEntry.displayName = pipelineStage.displayName;
    stageEntry.durationSeconds = duration;
    stageEntry.logFile = pipelineStage.logFile.string();
    if (result.exitCode != 0) {
      stageEntry.status = "failed";
      manifest.stages.push_back(stageEntry);
      manifest.status = "failed";
      manifest.failedStage = pipelineStage.name;
      writeManifest(manifest, manifestPath);
      std::cout << "      失败 (" << formatDuration(duration) << ")，exit code: "
                << result.exitCode << "\n";
      std::cout << "      请查看日志: " << pipelineStage.logFile.string() << "\n";
      return result.exitCode;
    }
    stageEntry.status = "ok";
    manifest.stages.push_back(stageEntry);
    writeManifest(manifest, manifestPath);
    std::cout << "      完成 (" << formatDuration(duration) << ")\n";
    ++step;
  }

  std::cout << "[11/11] 输出 manifest.json、日志、稠密点云、网格和纹理模型\n";
  manifest.status = "ok";
  manifest.failedStage = "";
  writeManifest(manifest, manifestPath);
  std::cout << "== 多视角三维重建完成，总耗时 "
            << formatDuration(elapsedSeconds(runStart)) << " ==\n";
  std::cout << "manifest: " << manifestPath.string() << "\n";
  return 0;
}

}  // namespace mvs
