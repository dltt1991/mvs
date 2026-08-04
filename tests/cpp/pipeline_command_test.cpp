#include "pipeline/Config.h"
#include "pipeline/ReconstructionPipeline.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
  mvs::Config config;
  config.imagesDir = "data/images";
  config.camerasJson = "data/cameras.json";
  config.outputDir = "outputs/test-run";
  config.colmapBinary = "build/third_party/colmap/bin/colmap";
  config.openMvsBinDir = "build/third_party/openmvs/bin";
  config.maxThreads = 4;
  config.undistortCopyPolicy = "HARD_LINK";
  config.matcher = "sequential";
  config.sequentialOverlap = 12;
  config.sequentialQuadraticOverlap = false;
  config.densifyNumberViews = 4;
  config.densifyNumberViewsFuse = 3;
  config.densifyGeometricIters = 1;
  config.densifyResolutionLevel = 2;
  config.densifyMaxResolution = 2048;
  config.densifyIters = 2;
  config.texturePatchPackingHeuristic = 100;

  mvs::CameraIntrinsics camera;
  camera.model = "SIMPLE_RADIAL";
  camera.width = 6016;
  camera.height = 4512;
  camera.f = 4404.0;
  camera.cx = 3008.0;
  camera.cy = 2256.0;
  camera.k1 = 0.01;

  auto plan = mvs::buildPipelinePlan(config, camera);
  assert(plan.stages.size() == 8);
  assert(plan.imageListFile == std::filesystem::path("outputs/test-run/colmap/image_list.txt"));
  assert(plan.stages[0].name == "feature_extractor");
  assert(!plan.stages[0].signature.empty());
  assert(plan.stages[0].markerFile.filename() == "feature_extractor.done");
  assert(plan.stages[0].expectedArtifacts.size() == 1);
  assert(plan.stages[0].expectedArtifacts[0] == std::filesystem::path("outputs/test-run/colmap/database.db"));
  assert(plan.stages[0].displayName == "COLMAP / pycolmap：特征提取");
  assert(plan.stages[0].args[0] == config.colmapBinary);
  assert(plan.stages[0].args[1] == "feature_extractor");
  assert(plan.stages[0].argsContains("--ImageReader.camera_model"));
  assert(plan.stages[0].argsContains("--image_list_path"));
  assert(plan.stages[0].argsContains("outputs/test-run/colmap/image_list.txt"));
  assert(plan.stages[0].argsContains("--FeatureExtraction.num_threads"));
  assert(plan.stages[0].argsContains("4"));
  assert(plan.stages[1].name == "sequential_matcher");
  assert(plan.stages[1].args[1] == "sequential_matcher");
  assert(plan.stages[1].argsContains("--FeatureMatching.num_threads"));
  assert(plan.stages[1].argsContains("--SequentialMatching.overlap"));
  assert(plan.stages[1].argsContains("12"));
  assert(plan.stages[1].argsContains("--SequentialMatching.quadratic_overlap"));
  assert(plan.stages[1].argsContains("0"));
  assert(plan.stages[2].signature.find(plan.stages[1].signature) != std::string::npos);
  assert(plan.stages[2].argsContains("--Mapper.num_threads"));
  assert(plan.stages[2].argsContains("--Mapper.image_list_path"));
  assert(plan.stages[2].argsContains("outputs/test-run/colmap/image_list.txt"));
  assert(plan.stages[3].argsContains("--num_threads"));
  assert(plan.stages[3].argsContains("--copy_policy"));
  assert(plan.stages[3].argsContains("HARD_LINK"));
  assert(plan.stages[4].name == "interface_colmap");
  assert(plan.stages[4].displayName == "OpenMVS：InterfaceCOLMAP 转换为 scene.mvs");
  assert(plan.stages[4].argsContains("--max-threads"));
  assert(plan.stages[5].argsContains("--max-threads"));
  assert(plan.stages[5].argsContains("--remove-dmaps"));
  assert(plan.stages[5].argsContains("1"));
  assert(plan.stages[5].argsContains("--number-views"));
  assert(plan.stages[5].argsContains("4"));
  assert(plan.stages[5].argsContains("--number-views-fuse"));
  assert(plan.stages[5].argsContains("3"));
  assert(plan.stages[5].argsContains("--geometric-iters"));
  assert(plan.stages[5].argsContains("--resolution-level"));
  assert(plan.stages[5].argsContains("2"));
  assert(plan.stages[5].argsContains("--max-resolution"));
  assert(plan.stages[5].argsContains("2048"));
  assert(plan.stages[5].argsContains("--iters"));
  assert(plan.stages[5].expectedArtifacts.size() == 2);
  assert(plan.stages[5].expectedArtifacts[0] == std::filesystem::path("outputs/test-run/openmvs/scene_dense.mvs"));
  assert(plan.stages[5].expectedArtifacts[1] == std::filesystem::path("outputs/test-run/openmvs/scene_dense.ply"));
  assert(plan.stages[6].argsContains("--max-threads"));
  assert(plan.stages[7].argsContains("--max-threads"));
  assert(plan.stages[7].name == "texture_mesh");
  assert(plan.stages[7].displayName == "OpenMVS：TextureMesh 生成纹理模型");

  // interface 模式（--archive-type 默认 -1）下 ReconstructMesh 不写 .mvs，只写 .ply，
  // 所以 TextureMesh 必须吃 scene_dense.mvs 并用 --mesh-file 显式指定网格。
  assert(plan.stages[6].name == "reconstruct_mesh");
  assert(plan.stages[6].argsContains("scene_mesh.ply"));
  assert(plan.stages[7].args[1] == "outputs/test-run/openmvs/scene_dense.mvs");
  assert(plan.stages[7].argsContains("--mesh-file"));
  assert(plan.stages[7].argsContains("scene_mesh.ply"));
  assert(plan.stages[7].argsContains("scene_texture.ply"));
  assert(plan.stages[7].argsContains("--patch-packing-heuristic"));
  assert(plan.stages[7].argsContains("100"));

  config.generateTexture = false;
  auto meshOnlyPlan = mvs::buildPipelinePlan(config, camera);
  assert(meshOnlyPlan.stages.size() == 7);
  assert(meshOnlyPlan.stages[6].name == "reconstruct_mesh");

  const auto dir = std::filesystem::temp_directory_path() / "mvs_stage_ready_test";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  mvs::PipelineStage readyStage;
  readyStage.signature = "expected signature";
  readyStage.markerFile = dir / "stage.done";
  readyStage.expectedArtifacts.push_back(dir / "artifact.bin");
  assert(!mvs::stageArtifactsReady(readyStage));

  {
    std::ofstream artifact(dir / "artifact.bin");
    artifact << "artifact";
  }
  {
    std::ofstream marker(dir / "stage.done");
    marker << "different signature\n";
  }
  assert(!mvs::stageArtifactsReady(readyStage));

  {
    std::ofstream marker(dir / "stage.done");
    marker << "expected signature";
  }
  assert(mvs::stageArtifactsReady(readyStage));

  const auto imagesDir = std::filesystem::temp_directory_path() / "mvs_sorted_image_list_images";
  const auto imageListFile = std::filesystem::temp_directory_path() / "mvs_sorted_image_list.txt";
  std::filesystem::remove_all(imagesDir);
  std::filesystem::create_directories(imagesDir);
  {
    std::ofstream image(imagesDir / "IMG_0003.jpg");
    image << "fake";
  }
  {
    std::ofstream image(imagesDir / "IMG_0001.jpg");
    image << "fake";
  }
  {
    std::ofstream ignored(imagesDir / ".DS_Store");
    ignored << "ignore";
  }
  {
    std::ofstream image(imagesDir / "IMG_0002.JPG");
    image << "fake";
  }
  mvs::writeSortedImageList(imagesDir, imageListFile);
  std::ifstream imageList(imageListFile);
  std::string imageListText((std::istreambuf_iterator<char>(imageList)), {});
  assert(imageListText == "IMG_0001.jpg\nIMG_0002.JPG\nIMG_0003.jpg\n");

  return 0;
}
