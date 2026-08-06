#include "pipeline/ReconstructionPipeline.h"

#include "pipeline/Manifest.h"
#include "pipeline/ProcessRunner.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
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

void appendOption(std::vector<std::string>& args, const std::string& name, const std::string& value) {
  args.push_back(name);
  args.push_back(value);
}

void appendColmapThreads(std::vector<std::string>& args,
                         const std::string& optionName,
                         int maxThreads) {
  if (maxThreads > 0) {
    appendOption(args, optionName, std::to_string(maxThreads));
  }
}

void appendOpenMvsThreads(std::vector<std::string>& args, int maxThreads) {
  if (maxThreads > 0) {
    appendOption(args, "--max-threads", std::to_string(maxThreads));
  }
}

// 只在非默认值时显式传递，保持默认命令行与 OpenMVS 原生行为一致
// （-1 本身就是 OpenMVS 的默认值：自动选最佳 GPU）。
void appendOpenMvsCudaDevice(std::vector<std::string>& args, int cudaDevice) {
  if (cudaDevice != -1) {
    appendOption(args, "--cuda-device", std::to_string(cudaDevice));
  }
}

PipelineStage stage(const std::string& name,
                    const std::string& displayName,
                    std::vector<std::string> args,
                    std::vector<std::filesystem::path> expectedArtifacts,
                    const std::string& inputFingerprint,
                    const std::string& dependencySignature,
                    const std::filesystem::path& logDir) {
  PipelineStage result;
  result.name = name;
  result.displayName = displayName;
  result.args = std::move(args);
  result.logFile = logDir / (name + ".log");
  result.markerFile = logDir / (name + ".done");
  result.expectedArtifacts = std::move(expectedArtifacts);
  std::ostringstream signature;
  signature << "stage=" << result.name << "\n";
  signature << "input_fingerprint=" << inputFingerprint << "\n";
  signature << "dependency_signature=" << dependencySignature << "\n";
  signature << "args=";
  for (const auto& arg : result.args) {
    signature << shellQuote(arg) << " ";
  }
  result.signature = signature.str();
  return result;
}

double elapsedSeconds(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::string fileStamp(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    return path.string() + ":missing";
  }
  const auto size = std::filesystem::is_regular_file(path, error) ? std::filesystem::file_size(path, error) : 0;
  const auto mtime =
      static_cast<long long>(std::filesystem::last_write_time(path, error).time_since_epoch().count());
  return path.string() + ":" + std::to_string(size) + ":" + std::to_string(mtime);
}

std::string inputFingerprint(const Config& config) {
  std::vector<std::string> stamps;
  stamps.push_back(fileStamp(config.camerasJson));

  std::error_code error;
  if (std::filesystem::exists(config.imagesDir, error)) {
    for (const auto& entry : std::filesystem::directory_iterator(config.imagesDir, error)) {
      if (entry.is_regular_file(error)) {
        stamps.push_back(fileStamp(entry.path()));
      }
    }
  } else {
    stamps.push_back(std::filesystem::path(config.imagesDir).string() + ":missing");
  }
  std::sort(stamps.begin(), stamps.end());

  std::ostringstream out;
  for (const auto& stamp : stamps) {
    out << stamp << "\n";
  }
  return out.str();
}

bool artifactExists(const std::filesystem::path& artifact) {
  std::error_code error;
  if (!std::filesystem::exists(artifact, error)) {
    return false;
  }
  if (std::filesystem::is_regular_file(artifact, error)) {
    return std::filesystem::file_size(artifact, error) > 0;
  }
  if (std::filesystem::is_directory(artifact, error)) {
    return !std::filesystem::is_empty(artifact, error);
  }
  return true;
}

std::string lowerExtension(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  return extension;
}

bool isSupportedImageFile(const std::filesystem::path& path) {
  const auto extension = lowerExtension(path);
  return extension == ".jpg" || extension == ".jpeg" || extension == ".png" ||
         extension == ".tif" || extension == ".tiff" || extension == ".bmp";
}

void writeStageMarker(const PipelineStage& stage) {
  std::ofstream marker(stage.markerFile);
  if (!marker) {
    throw std::runtime_error("cannot write stage marker: " + stage.markerFile.string());
  }
  marker << stage.signature;
}

void pushStage(PipelinePlan& plan,
               PipelineStage next,
               std::string& dependencySignature) {
  dependencySignature = next.signature;
  plan.stages.push_back(std::move(next));
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

bool stageArtifactsReady(const PipelineStage& stage) {
  if (stage.expectedArtifacts.empty() || !artifactExists(stage.markerFile)) {
    return false;
  }

  std::ifstream marker(stage.markerFile);
  std::string markerText((std::istreambuf_iterator<char>(marker)), {});
  if (markerText != stage.signature) {
    return false;
  }

  for (const auto& artifact : stage.expectedArtifacts) {
    if (!artifactExists(artifact)) {
      return false;
    }
  }
  return true;
}

void writeSortedImageList(const std::filesystem::path& imagesDir,
                          const std::filesystem::path& imageListFile) {
  std::error_code error;
  std::vector<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(imagesDir, error)) {
    if (entry.is_regular_file(error) && isSupportedImageFile(entry.path())) {
      names.push_back(entry.path().filename().string());
    }
  }
  if (error) {
    throw std::runtime_error("cannot list image directory: " + imagesDir.string());
  }
  if (names.empty()) {
    throw std::runtime_error("no supported images found in: " + imagesDir.string());
  }
  std::sort(names.begin(), names.end());

  std::filesystem::create_directories(imageListFile.parent_path());
  std::ofstream out(imageListFile);
  if (!out) {
    throw std::runtime_error("cannot write image list: " + imageListFile.string());
  }
  for (const auto& name : names) {
    out << name << '\n';
  }
}

PipelinePlan buildPipelinePlan(const Config& config, const CameraIntrinsics& camera) {
  PipelinePlan plan;
  plan.outputDir = config.outputDir;
  plan.workspaceDir = std::filesystem::path(config.outputDir);

  const auto colmapDir = plan.workspaceDir / "colmap";
  const auto openMvsDir = plan.workspaceDir / "openmvs";
  const auto logsDir = plan.workspaceDir / "logs";
  const auto database = colmapDir / "database.db";
  plan.imageListFile = colmapDir / "image_list.txt";
  const auto sparseDir = colmapDir / "sparse";
  const auto denseDir = colmapDir / "dense";
  const auto sparseModel = sparseDir / "0";
  const auto scene = openMvsDir / "scene.mvs";
  const auto denseScene = openMvsDir / "scene_dense.mvs";
  const auto densePly = openMvsDir / "scene_dense.ply";
  // OpenMVS 的 interface 模式（--archive-type 默认 -1）下，.mvs 只承载相机与位姿，
  // 几何数据落在同名 .ply 里，由下游工具通过 --pointcloud-file / --mesh-file 显式加载。
  // 因此 ReconstructMesh / TextureMesh 都不会重写 .mvs，产物是 .ply。
  const auto meshPly = openMvsDir / "scene_mesh.ply";
  const auto texturePly = openMvsDir / "scene_texture.ply";
  const auto inputStamp = inputFingerprint(config);
  std::string dependencySignature;

  const auto cameraParams =
      number(camera.f) + "," + number(camera.cx) + "," + number(camera.cy) + "," + number(camera.k1);

  std::vector<std::string> featureArgs{config.colmapBinary,
                                       "feature_extractor",
                                       "--database_path",
                                       database.string(),
                                       "--image_path",
                                       config.imagesDir,
                                       "--image_list_path",
                                       plan.imageListFile.string(),
                                       "--ImageReader.single_camera",
                                       "1",
                                       "--ImageReader.camera_model",
                                       camera.model,
                                       "--ImageReader.camera_params",
                                       cameraParams};
  appendColmapThreads(featureArgs, "--FeatureExtraction.num_threads", config.maxThreads);
  pushStage(plan,
            stage("feature_extractor",
                  "COLMAP / pycolmap：特征提取",
                  std::move(featureArgs),
                  {database},
                  inputStamp,
                  dependencySignature,
                  logsDir),
            dependencySignature);

  const auto matcherName = config.matcher == "sequential" ? "sequential_matcher" : "exhaustive_matcher";
  std::vector<std::string> matcherArgs{config.colmapBinary, matcherName, "--database_path", database.string()};
  appendColmapThreads(matcherArgs, "--FeatureMatching.num_threads", config.maxThreads);
  if (config.matcher == "sequential") {
    appendOption(matcherArgs, "--SequentialMatching.overlap", std::to_string(config.sequentialOverlap));
    appendOption(matcherArgs,
                 "--SequentialMatching.quadratic_overlap",
                 config.sequentialQuadraticOverlap ? "1" : "0");
  }
  pushStage(plan,
            stage(matcherName,
                  config.matcher == "sequential" ? "COLMAP / pycolmap：顺序特征匹配"
                                                 : "COLMAP / pycolmap：特征匹配",
                  std::move(matcherArgs),
                  {database},
                  inputStamp,
                  dependencySignature,
                  logsDir),
            dependencySignature);

  std::vector<std::string> mapperArgs{config.colmapBinary,
                                      "mapper",
                                      "--database_path",
                                      database.string(),
                                      "--image_path",
                                      config.imagesDir,
                                      "--Mapper.image_list_path",
                                      plan.imageListFile.string(),
                                      "--output_path",
                                      sparseDir.string()};
  appendColmapThreads(mapperArgs, "--Mapper.num_threads", config.maxThreads);
  if (config.mapperBundleAdjustmentGpu) {
    appendOption(mapperArgs, "--Mapper.ba_global_use_gpu", "1");
    appendOption(mapperArgs, "--BundleAdjustmentCeres.use_gpu", "1");
  }
  pushStage(plan,
            stage("mapper",
                  "COLMAP / pycolmap：增量 SfM、相机位姿估计、稀疏点云重建",
                  std::move(mapperArgs),
                  {sparseModel},
                  inputStamp,
                  dependencySignature,
                  logsDir),
            dependencySignature);

  std::vector<std::string> undistorterArgs{config.colmapBinary,
                                           "image_undistorter",
                                           "--image_path",
                                           config.imagesDir,
                                           "--input_path",
                                           sparseModel.string(),
                                           "--output_path",
                                           denseDir.string(),
                                           "--output_type",
                                           "COLMAP",
                                           "--copy_policy",
                                           config.undistortCopyPolicy};
  appendColmapThreads(undistorterArgs, "--num_threads", config.maxThreads);
  pushStage(plan,
            stage("image_undistorter",
                  "COLMAP：图像去畸变和 dense workspace 准备",
                  std::move(undistorterArgs),
                  {denseDir / "images", denseDir / "sparse"},
                  inputStamp,
                  dependencySignature,
                  logsDir),
            dependencySignature);

  auto interfaceArgs =
      withBinary(openMvsTool(config, "InterfaceCOLMAP"), {"--input-file", denseDir.string(), "--output-file", scene.string()});
  appendOpenMvsThreads(interfaceArgs, config.maxThreads);
  pushStage(plan,
            stage("interface_colmap",
                  "OpenMVS：InterfaceCOLMAP 转换为 scene.mvs",
                  std::move(interfaceArgs),
                  {scene},
                  inputStamp,
                  dependencySignature,
                  logsDir),
            dependencySignature);

  auto densifyArgs = withBinary(openMvsTool(config, "DensifyPointCloud"),
                                {scene.string(),
                                 "--working-folder",
                                 openMvsDir.string(),
                                 "--output-file",
                                 denseScene.filename().string(),
                                 "--remove-dmaps",
                                 config.removeDepthMaps ? "1" : "0",
                                 "--number-views",
                                 std::to_string(config.densifyNumberViews),
                                 "--number-views-fuse",
                                 std::to_string(config.densifyNumberViewsFuse),
                                 "--geometric-iters",
                                 std::to_string(config.densifyGeometricIters),
                                 "--resolution-level",
                                 std::to_string(config.densifyResolutionLevel),
                                 "--max-resolution",
                                 std::to_string(config.densifyMaxResolution),
                                 "--iters",
                                 std::to_string(config.densifyIters)});
  appendOpenMvsThreads(densifyArgs, config.maxThreads);
  appendOpenMvsCudaDevice(densifyArgs, config.cudaDevice);
  pushStage(plan,
            stage("densify_point_cloud",
                  "OpenMVS：DensifyPointCloud 生成稠密点云",
                  std::move(densifyArgs),
                  {denseScene, densePly},
                  inputStamp,
                  dependencySignature,
                  logsDir),
            dependencySignature);

  auto meshArgs = withBinary(openMvsTool(config, "ReconstructMesh"),
                             {denseScene.string(),
                              "--working-folder",
                              openMvsDir.string(),
                              "--output-file",
                              meshPly.filename().string()});
  appendOpenMvsThreads(meshArgs, config.maxThreads);
  appendOpenMvsCudaDevice(meshArgs, config.cudaDevice);
  pushStage(plan,
            stage("reconstruct_mesh",
                  "OpenMVS：ReconstructMesh 生成三角网格",
                  std::move(meshArgs),
                  {meshPly},
                  inputStamp,
                  dependencySignature,
                  logsDir),
            dependencySignature);

  if (config.generateTexture) {
    auto textureArgs = withBinary(openMvsTool(config, "TextureMesh"),
                                  {denseScene.string(),
                                   "--working-folder",
                                   openMvsDir.string(),
                                   "--mesh-file",
                                   meshPly.filename().string(),
                                   "--output-file",
                                   texturePly.filename().string(),
                                   "--patch-packing-heuristic",
                                   std::to_string(config.texturePatchPackingHeuristic)});
    appendOpenMvsThreads(textureArgs, config.maxThreads);
    appendOpenMvsCudaDevice(textureArgs, config.cudaDevice);
    pushStage(plan,
              stage("texture_mesh",
                    "OpenMVS：TextureMesh 生成纹理模型",
                    std::move(textureArgs),
                    {texturePly},
                    inputStamp,
                    dependencySignature,
                    logsDir),
              dependencySignature);
  }

  return plan;
}

int runPipeline(const Config& config) {
  const auto runStart = std::chrono::steady_clock::now();
  const int totalSteps = config.generateTexture ? 11 : 10;
  std::cout << "== 多视角三维重建开始 ==\n";
  std::cout << "[1/" << totalSteps << "] 多张输入图片: " << config.imagesDir << "\n" << std::flush;
  std::cout << "[2/" << totalSteps << "] 读取相机参数: " << config.camerasJson << std::flush;
  const auto cameraStart = std::chrono::steady_clock::now();
  const auto dataset = loadCameraDataset(config.camerasJson);
  const auto camera = medianSimpleRadialCamera(dataset);
  std::cout << " 完成 (" << formatDuration(elapsedSeconds(cameraStart)) << ")"
            << "，图片数: " << dataset.numImages
            << "，已注册: " << dataset.numRegistered << "\n" << std::flush;

  const auto plan = buildPipelinePlan(config, camera);

  std::filesystem::create_directories(plan.workspaceDir / "colmap" / "sparse");
  std::filesystem::create_directories(plan.workspaceDir / "colmap" / "dense");
  std::filesystem::create_directories(plan.workspaceDir / "openmvs");
  std::filesystem::create_directories(plan.workspaceDir / "logs");
  writeSortedImageList(config.imagesDir, plan.imageListFile);

  Manifest manifest;
  manifest.status = "running";
  manifest.failedStage = "";
  manifest.artifacts["colmap_database"] = (plan.workspaceDir / "colmap" / "database.db").string();
  manifest.artifacts["colmap_image_list"] = plan.imageListFile.string();
  manifest.artifacts["colmap_sparse"] = (plan.workspaceDir / "colmap" / "sparse" / "0").string();
  manifest.artifacts["colmap_dense"] = (plan.workspaceDir / "colmap" / "dense").string();
  manifest.artifacts["openmvs_scene"] = (plan.workspaceDir / "openmvs" / "scene.mvs").string();
  manifest.artifacts["openmvs_dense"] = (plan.workspaceDir / "openmvs" / "scene_dense.mvs").string();
  manifest.artifacts["openmvs_mesh"] = (plan.workspaceDir / "openmvs" / "scene_mesh.ply").string();
  if (config.generateTexture) {
    manifest.artifacts["openmvs_texture"] = (plan.workspaceDir / "openmvs" / "scene_texture.ply").string();
  }

  const auto manifestPath = plan.workspaceDir / "manifest.json";
  writeManifest(manifest, manifestPath);

  int step = 3;
  for (const auto& pipelineStage : plan.stages) {
    std::cout << "[" << step << "/" << totalSteps << "] " << pipelineStage.displayName << "\n";
    std::cout << "      日志: " << pipelineStage.logFile.string() << "\n" << std::flush;
    if (config.reuseExisting && stageArtifactsReady(pipelineStage)) {
      StageManifestEntry stageEntry;
      stageEntry.name = pipelineStage.name;
      stageEntry.displayName = pipelineStage.displayName;
      stageEntry.status = "skipped";
      stageEntry.durationSeconds = 0.0;
      stageEntry.logFile = pipelineStage.logFile.string();
      manifest.stages.push_back(stageEntry);
      writeManifest(manifest, manifestPath);
      std::cout << "      复用已有产物，跳过\n" << std::flush;
      ++step;
      continue;
    }

    const auto stageStart = std::chrono::steady_clock::now();
    const auto result = runCommand(pipelineStage.args, plan.workspaceDir, pipelineStage.logFile);
    const auto duration = elapsedSeconds(stageStart);
    StageManifestEntry stageEntry;
    stageEntry.name = pipelineStage.name;
    stageEntry.displayName = pipelineStage.displayName;
    stageEntry.durationSeconds = duration;
    stageEntry.logFile = pipelineStage.logFile.string();
    stageEntry.peakResidentSetSizeKb = result.peakResidentSetSizeKb;
    stageEntry.userCpuSeconds = result.userCpuSeconds;
    stageEntry.systemCpuSeconds = result.systemCpuSeconds;
    if (result.exitCode != 0) {
      stageEntry.status = "failed";
      manifest.stages.push_back(stageEntry);
      manifest.status = "failed";
      manifest.failedStage = pipelineStage.name;
      writeManifest(manifest, manifestPath);
      std::cout << "      失败 (" << formatDuration(duration) << ")，exit code: "
                << result.exitCode << "\n";
      std::cout << "      请查看日志: " << pipelineStage.logFile.string() << "\n" << std::flush;
      return result.exitCode;
    }
    for (const auto& artifact : pipelineStage.expectedArtifacts) {
      if (!artifactExists(artifact)) {
        stageEntry.status = "failed";
        manifest.stages.push_back(stageEntry);
        manifest.status = "failed";
        manifest.failedStage = pipelineStage.name;
        writeManifest(manifest, manifestPath);
        std::cout << "      失败 (" << formatDuration(duration) << ")，缺少产物: "
                  << artifact.string() << "\n" << std::flush;
        return 2;
      }
    }
    writeStageMarker(pipelineStage);
    stageEntry.status = "ok";
    manifest.stages.push_back(stageEntry);
    writeManifest(manifest, manifestPath);
    std::cout << "      完成 (" << formatDuration(duration) << ")\n" << std::flush;
    ++step;
  }

  std::cout << "[" << totalSteps << "/" << totalSteps << "] 输出 manifest.json、日志、稠密点云、网格"
            << (config.generateTexture ? "和纹理模型" : "模型") << "\n" << std::flush;
  manifest.status = "ok";
  manifest.failedStage = "";
  writeManifest(manifest, manifestPath);
  std::cout << "== 多视角三维重建完成，总耗时 "
            << formatDuration(elapsedSeconds(runStart)) << " ==\n";
  std::cout << "manifest: " << manifestPath.string() << "\n";
  return 0;
}

}  // namespace mvs
