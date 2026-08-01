#include "pipeline/Config.h"
#include "pipeline/ReconstructionPipeline.h"

#include <cassert>
#include <string>

int main() {
  mvs::Config config;
  config.imagesDir = "data/images";
  config.camerasJson = "data/cameras.json";
  config.outputDir = "outputs/test-run";
  config.colmapBinary = "build/third_party/colmap/bin/colmap";
  config.openMvsBinDir = "build/third_party/openmvs/bin";

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
  assert(plan.stages[0].name == "feature_extractor");
  assert(plan.stages[0].displayName == "COLMAP / pycolmap：特征提取");
  assert(plan.stages[0].args[0] == config.colmapBinary);
  assert(plan.stages[0].args[1] == "feature_extractor");
  assert(plan.stages[0].argsContains("--ImageReader.camera_model"));
  assert(plan.stages[4].name == "interface_colmap");
  assert(plan.stages[4].displayName == "OpenMVS：InterfaceCOLMAP 转换为 scene.mvs");
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
  return 0;
}
