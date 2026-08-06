#include "pipeline/StreamingPipeline.h"
#include "pipeline/CameraConfig.h"
#include "pipeline/ProcessRunner.h"
#include "pipeline/ReconstructionPipeline.h"

#include <colmap/scene/reconstruction.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace mvs {
namespace {

// ── 去畸变参数提取 ──────────────────────────────────────────────────────────

std::vector<double> distCoeffsFor(const colmap::Camera& camera) {
  const std::string m = camera.ModelName();
  if (m == "SIMPLE_PINHOLE" || m == "PINHOLE") return {0, 0, 0, 0};
  if (m == "SIMPLE_RADIAL")  return {camera.params[3], 0, 0, 0};
  if (m == "RADIAL")         return {camera.params[3], camera.params[4], 0, 0};
  if (m == "OPENCV")
    return {camera.params[4], camera.params[5], camera.params[6], camera.params[7]};
  if (m == "FULL_OPENCV")
    return {camera.params[4], camera.params[5], camera.params[6], camera.params[7],
            camera.params[8], camera.params[9], camera.params[10], camera.params[11]};
  throw std::runtime_error("unsupported camera model: " + m);
}

// ── 计时辅助 ────────────────────────────────────────────────────────────────

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

double elapsed(TimePoint t0) {
  return std::chrono::duration<double>(Clock::now() - t0).count();
}

std::string fmtDur(double s) {
  std::ostringstream ss;
  if (s < 60.0) {
    ss.precision(2);
    ss << std::fixed << s << "s";
  } else {
    int m = static_cast<int>(s) / 60;
    ss << m << "m" << static_cast<int>(s) % 60 << "s";
  }
  return ss.str();
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════════
// 构造函数
// ══════════════════════════════════════════════════════════════════════════════

StreamingPipeline::StreamingPipeline(const Config& config,
                                     const std::filesystem::path& outputDir)
    : config_(config), outputDir_(outputDir) {
  namespace fs = std::filesystem;
  colmapDir_     = outputDir_ / "colmap";
  openMvsDir_    = outputDir_ / "openmvs";
  logsDir_       = outputDir_ / "logs";
  imageListFile_ = colmapDir_ / "image_list.txt";
  database_      = colmapDir_ / "database.db";
  sparseDir_     = colmapDir_ / "sparse";
  sparseModel_   = sparseDir_ / "0";
  sceneMvs_      = openMvsDir_ / "scene.mvs";
  denseMvs_      = openMvsDir_ / "scene_dense.mvs";
  meshPly_       = openMvsDir_ / "scene_mesh.ply";
  texturePly_    = openMvsDir_ / "scene_texture.ply";
}

StreamingPipeline::StreamingPipeline(const Config& config,
                                     const std::filesystem::path& sparseModel,
                                     const std::filesystem::path& outputDir)
    : StreamingPipeline(config, outputDir) {
  sparseModel_ = sparseModel;
}

// ══════════════════════════════════════════════════════════════════════════════
// 公开入口
// ══════════════════════════════════════════════════════════════════════════════

int StreamingPipeline::runFull() {
  namespace fs = std::filesystem;

  // 把配置中的相对路径解析为绝对路径（相对于进程的当前工作目录，即 /workspace）
  // 否则子进程 chdir 到 outputDir_ 后会找不到 build/third_party/... 等路径
  const auto cwd = fs::current_path();
  auto toAbs = [&](const std::string& p) -> std::string {
    if (p.empty()) return p;
    const fs::path fp(p);
    return fp.is_absolute() ? p : (cwd / fp).lexically_normal().string();
  };
  config_.colmapBinary  = toAbs(config_.colmapBinary);
  config_.openMvsBinDir = toAbs(config_.openMvsBinDir);
  config_.imagesDir     = toAbs(config_.imagesDir);
  config_.camerasJson   = toAbs(config_.camerasJson);
  if (!config_.outputDir.empty())
    config_.outputDir   = toAbs(config_.outputDir);
  outputDir_ = toAbs(outputDir_.string());

  fs::create_directories(colmapDir_ / "sparse");
  fs::create_directories(openMvsDir_);
  fs::create_directories(logsDir_);
  fs::create_directories(outputDir_ / "images");

  writeSortedImageList(config_.imagesDir, imageListFile_);

  std::cout << "== 多视角三维重建（流式管线）开始 ==" << std::endl;

  // 读取相机参数（用于特征提取的初始内参）
  const auto t0 = Clock::now();
  std::cout << "[cameras] 读取相机参数: " << config_.camerasJson << std::flush;
  const auto dataset = loadCameraDataset(config_.camerasJson);
  const auto camera  = medianSimpleRadialCamera(dataset);
  std::cout << " 完成 (" << fmtDur(elapsed(t0)) << ")，"
            << dataset.numImages << " 张，注册: " << dataset.numRegistered << std::endl;

  // ── COLMAP 阶段 ──────────────────────────────────────────────────────────
  stageFeatureExtractor(camera);
  stageMatcher();
  stageMapper();

  // ── 流式阶段（取代 undistorter + InterfaceCOLMAP）─────────────────────────
  const auto tScene = Clock::now();
  std::cout << "[streaming] buildScene..." << std::flush;
  const auto jobs = buildScene();
  std::cout << " 完成 (" << fmtDur(elapsed(tScene)) << ")" << std::endl;

  const auto tUndist = Clock::now();
  std::cout << "[streaming] undistortParallel (" << jobs.size() << " 张, "
            << (config_.maxThreads > 0 ? config_.maxThreads : (int)std::thread::hardware_concurrency())
            << " 线程)..." << std::flush;
  undistortParallel(jobs);
  std::cout << " 完成 (" << fmtDur(elapsed(tUndist)) << ")" << std::endl;

  // ── OpenMVS 阶段 ─────────────────────────────────────────────────────────
  stageDensify();
  stageReconstructMesh();
  if (config_.generateTexture) {
    stageTextureMesh();
  }

  std::cout << std::endl;
  std::cout << "== 完成，总耗时: " << fmtDur(elapsed(t0)) << " ==" << std::endl;
  return 0;
}

void StreamingPipeline::run() {
  namespace fs = std::filesystem;

  fs::create_directories(outputDir_ / "images");
  fs::create_directories(openMvsDir_);

  const auto t0 = Clock::now();
  const auto jobs = buildScene();
  std::cout << "[Scene] 完成 (" << fmtDur(elapsed(t0)) << ")" << std::endl;

  const auto t1 = Clock::now();
  undistortParallel(jobs);
  std::cout << "[Undistort] 完成 (" << fmtDur(elapsed(t1)) << ")" << std::endl;

  const auto t2 = Clock::now();
  stageDensify();
  std::cout << "[Densify] 完成 (" << fmtDur(elapsed(t2)) << ")" << std::endl;

  std::cout << "总耗时: " << fmtDur(elapsed(t0)) << std::endl;
}

// ══════════════════════════════════════════════════════════════════════════════
// 私有阶段实现
// ══════════════════════════════════════════════════════════════════════════════

void StreamingPipeline::stageFeatureExtractor(const CameraIntrinsics& camera) {
  const std::string cameraParams =
      std::to_string(camera.f) + "," +
      std::to_string(camera.cx) + "," +
      std::to_string(camera.cy) + "," +
      std::to_string(camera.k1);

  std::vector<std::string> args{
    config_.colmapBinary, "feature_extractor",
    "--database_path", database_.string(),
    "--image_path", config_.imagesDir,
    "--image_list_path", imageListFile_.string(),
    "--ImageReader.single_camera", "1",
    "--ImageReader.camera_model", camera.model,
    "--ImageReader.camera_params", cameraParams
  };
  if (config_.maxThreads > 0)
    args.insert(args.end(), {"--FeatureExtraction.num_threads",
                              std::to_string(config_.maxThreads)});

  runStage("COLMAP 特征提取", std::move(args), logsDir_ / "feature_extractor.log");
}

void StreamingPipeline::stageMatcher() {
  const std::string matcherName =
      config_.matcher == "sequential" ? "sequential_matcher" : "exhaustive_matcher";

  std::vector<std::string> args{
    config_.colmapBinary, matcherName,
    "--database_path", database_.string()
  };
  if (config_.maxThreads > 0)
    args.insert(args.end(), {"--FeatureMatching.num_threads",
                              std::to_string(config_.maxThreads)});
  if (config_.matcher == "sequential") {
    args.insert(args.end(), {
      "--SequentialMatching.overlap", std::to_string(config_.sequentialOverlap),
      "--SequentialMatching.quadratic_overlap",
      config_.sequentialQuadraticOverlap ? "1" : "0"
    });
  }

  const std::string display = config_.matcher == "sequential"
                                  ? "COLMAP 顺序特征匹配"
                                  : "COLMAP 特征匹配";
  runStage(display, std::move(args), logsDir_ / (matcherName + ".log"));
}

void StreamingPipeline::stageMapper() {
  std::vector<std::string> args{
    config_.colmapBinary, "mapper",
    "--database_path", database_.string(),
    "--image_path", config_.imagesDir,
    "--Mapper.image_list_path", imageListFile_.string(),
    "--output_path", sparseDir_.string()
  };
  if (config_.maxThreads > 0)
    args.insert(args.end(), {"--Mapper.num_threads",
                              std::to_string(config_.maxThreads)});
  if (config_.mapperBundleAdjustmentGpu) {
    args.insert(args.end(), {
      "--Mapper.ba_global_use_gpu", "1",
      "--BundleAdjustmentCeres.use_gpu", "1"
    });
  }

  runStage("COLMAP 增量 SfM", std::move(args), logsDir_ / "mapper.log");
}

std::vector<UndistortJob> StreamingPipeline::buildScene() {
  colmap::Reconstruction reconstruction;
  reconstruction.Read(sparseModel_);

  MVS::Interface scene;

  // 每个 COLMAP 相机 → 一个 platform，cameraID 恒为 0
  std::unordered_map<colmap::camera_t, uint32_t> camToPlatform;
  for (const auto& [camId, cam] : reconstruction.Cameras()) {
    MVS::Interface::Platform platform;
    platform.name = "platform" + std::to_string(camId);

    MVS::Interface::Platform::Camera mvsCamera;
    mvsCamera.name   = cam.ModelName();
    mvsCamera.width  = static_cast<uint32_t>(cam.width);
    mvsCamera.height = static_cast<uint32_t>(cam.height);
    mvsCamera.K = MVS::Interface::Mat33d::eye();
    mvsCamera.K(0,0) = cam.FocalLengthX();
    mvsCamera.K(1,1) = cam.FocalLengthY();
    mvsCamera.K(0,2) = cam.PrincipalPointX() - 0.5;  // COLMAP(0.5,0.5) → OpenMVS 整数
    mvsCamera.K(1,2) = cam.PrincipalPointY() - 0.5;
    mvsCamera.R = MVS::Interface::Mat33d::eye();
    mvsCamera.C = MVS::Interface::Pos3d(0, 0, 0);
    platform.cameras.push_back(mvsCamera);

    camToPlatform[camId] = static_cast<uint32_t>(scene.platforms.size());
    scene.platforms.push_back(std::move(platform));
  }

  // 图像：全局 image_id → 局部下标
  std::unordered_map<colmap::image_t, uint32_t> imgToLocal;
  std::vector<UndistortJob> jobs;

  std::vector<colmap::image_t> ids = reconstruction.RegImageIds();
  std::sort(ids.begin(), ids.end());

  for (const colmap::image_t imgId : ids) {
    const colmap::Image&  image  = reconstruction.Image(imgId);
    const colmap::Camera& camera = reconstruction.Camera(image.CameraId());

    const uint32_t platId = camToPlatform.at(image.CameraId());
    auto& platform = scene.platforms[platId];

    // 位姿：world→camera 的 R；相机中心 C = -R^T * t
    const Eigen::Matrix3d R = image.CamFromWorld().rotation().toRotationMatrix();
    const Eigen::Vector3d t = image.CamFromWorld().translation();
    const Eigen::Vector3d C = -(R.transpose() * t);

    MVS::Interface::Platform::Pose pose;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        pose.R(r, c) = R(r, c);
    pose.C = MVS::Interface::Pos3d(C.x(), C.y(), C.z());

    MVS::Interface::Image mvsImg;
    mvsImg.name       = (outputDir_ / "images" / image.Name()).string();
    mvsImg.platformID = platId;
    mvsImg.cameraID   = 0;
    mvsImg.ID         = imgId;
    mvsImg.poseID     = static_cast<uint32_t>(platform.poses.size());
    platform.poses.push_back(pose);

    imgToLocal[imgId] = static_cast<uint32_t>(scene.images.size());
    scene.images.push_back(std::move(mvsImg));

    // 去畸变任务
    UndistortJob job;
    job.inputPath  = std::filesystem::path(config_.imagesDir) / image.Name();
    job.outputPath = outputDir_ / "images" / image.Name();
    job.cameraMatrix = cv::Matx33d(
        camera.FocalLengthX(), 0, camera.PrincipalPointX(),
        0, camera.FocalLengthY(), camera.PrincipalPointY(),
        0, 0, 1);
    job.distCoeffs = distCoeffsFor(camera);
    jobs.push_back(std::move(job));
  }

  // 3D 点（局部下标 + 按下标升序）
  for (const auto& [ptId, pt] : reconstruction.Points3D()) {
    MVS::Interface::Vertex vertex;
    vertex.X = MVS::Interface::Pos3f(
        static_cast<float>(pt.xyz.x()),
        static_cast<float>(pt.xyz.y()),
        static_cast<float>(pt.xyz.z()));

    for (const auto& el : pt.track.Elements()) {
      const auto it = imgToLocal.find(el.image_id);
      if (it == imgToLocal.end()) continue;
      MVS::Interface::Vertex::View view;
      view.imageID   = it->second;
      view.confidence = 0.0f;
      vertex.views.push_back(view);
    }
    if (vertex.views.size() < 2) continue;
    std::sort(vertex.views.begin(), vertex.views.end(),
              [](const MVS::Interface::Vertex::View& a,
                 const MVS::Interface::Vertex::View& b) {
                return a.imageID < b.imageID;
              });

    scene.vertices.push_back(std::move(vertex));
    scene.verticesColor.push_back(MVS::Interface::Color{
        MVS::Interface::Col3(pt.color(2), pt.color(1), pt.color(0))});
  }

  if (!MVS::ARCHIVE::SerializeSave(scene, sceneMvs_.string()))
    throw std::runtime_error("failed to write " + sceneMvs_.string());

  return jobs;
}

void StreamingPipeline::undistortParallel(const std::vector<UndistortJob>& jobs) {
  unsigned threads = config_.maxThreads > 0
                         ? static_cast<unsigned>(config_.maxThreads)
                         : std::thread::hardware_concurrency();
  if (threads == 0) threads = 4;
  threads = std::min<unsigned>(threads, static_cast<unsigned>(jobs.size()));

  std::atomic<size_t> next{0};
  std::mutex errMu;
  std::vector<std::string> errors;

  const auto worker = [&]() {
    for (;;) {
      const size_t idx = next.fetch_add(1);
      if (idx >= jobs.size()) return;
      const UndistortJob& job = jobs[idx];

      cv::Mat orig = cv::imread(job.inputPath.string(), cv::IMREAD_COLOR);
      if (orig.empty()) {
        std::lock_guard<std::mutex> lk(errMu);
        errors.push_back("cannot read " + job.inputPath.string());
        continue;
      }
      cv::Mat undist;
      cv::undistort(orig, undist, job.cameraMatrix, job.distCoeffs, job.cameraMatrix);
      if (!cv::imwrite(job.outputPath.string(), undist, {cv::IMWRITE_JPEG_QUALITY, 95})) {
        std::lock_guard<std::mutex> lk(errMu);
        errors.push_back("cannot write " + job.outputPath.string());
        continue;
      }
      const int done = ++imagesCompleted_;
      if (done % 10 == 0 || done == static_cast<int>(jobs.size()))
        std::cout << "  " << done << "/" << jobs.size() << std::flush;
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(threads);
  for (unsigned i = 0; i < threads; ++i) pool.emplace_back(worker);
  for (std::thread& t : pool) t.join();
  std::cout << std::endl;

  if (!errors.empty())
    throw std::runtime_error("undistort failed for " + std::to_string(errors.size()) +
                             " images, first: " + errors.front());
}

void StreamingPipeline::stageDensify() {
  const std::filesystem::path bin(config_.openMvsBinDir);
  std::vector<std::string> args{
    (bin / "DensifyPointCloud").string(),
    sceneMvs_.string(),
    "--working-folder", openMvsDir_.string(),
    "--output-file",    denseMvs_.filename().string(),
    "--number-views",   std::to_string(config_.densifyNumberViews),
    "--number-views-fuse", std::to_string(config_.densifyNumberViewsFuse),
    "--geometric-iters", std::to_string(config_.densifyGeometricIters),
    "--resolution-level", std::to_string(config_.densifyResolutionLevel),
    "--max-resolution", std::to_string(config_.densifyMaxResolution),
    "--iters",          std::to_string(config_.densifyIters),
    "--remove-dmaps",   config_.removeDepthMaps ? "1" : "0"
  };
  if (config_.maxThreads > 0)
    args.insert(args.end(), {"--max-threads", std::to_string(config_.maxThreads)});
  if (config_.cudaDevice != -1)
    args.insert(args.end(), {"--cuda-device", std::to_string(config_.cudaDevice)});

  runStage("OpenMVS DensifyPointCloud", std::move(args), logsDir_ / "densify_point_cloud.log");
}

void StreamingPipeline::stageReconstructMesh() {
  const std::filesystem::path bin(config_.openMvsBinDir);
  std::vector<std::string> args{
    (bin / "ReconstructMesh").string(),
    denseMvs_.string(),
    "--working-folder", openMvsDir_.string(),
    "--output-file",    meshPly_.filename().string()
  };
  if (config_.maxThreads > 0)
    args.insert(args.end(), {"--max-threads", std::to_string(config_.maxThreads)});
  if (config_.cudaDevice != -1)
    args.insert(args.end(), {"--cuda-device", std::to_string(config_.cudaDevice)});

  runStage("OpenMVS ReconstructMesh", std::move(args), logsDir_ / "reconstruct_mesh.log");
}

void StreamingPipeline::stageTextureMesh() {
  const std::filesystem::path bin(config_.openMvsBinDir);
  std::vector<std::string> args{
    (bin / "TextureMesh").string(),
    denseMvs_.string(),
    "--working-folder", openMvsDir_.string(),
    "--mesh-file",      meshPly_.filename().string(),
    "--output-file",    texturePly_.filename().string(),
    "--patch-packing-heuristic", std::to_string(config_.texturePatchPackingHeuristic)
  };
  if (config_.maxThreads > 0)
    args.insert(args.end(), {"--max-threads", std::to_string(config_.maxThreads)});
  if (config_.cudaDevice != -1)
    args.insert(args.end(), {"--cuda-device", std::to_string(config_.cudaDevice)});

  runStage("OpenMVS TextureMesh", std::move(args), logsDir_ / "texture_mesh.log");
}

// ── 子进程辅助 ───────────────────────────────────────────────────────────────

void StreamingPipeline::runStage(const std::string& displayName,
                                  std::vector<std::string> args,
                                  const std::filesystem::path& logFile) {
  const auto t0 = Clock::now();
  std::cout << "[" << displayName << "]..." << std::flush;

  std::filesystem::create_directories(logFile.parent_path());
  const CommandResult result = runCommand(args, outputDir_, logFile);

  if (result.exitCode != 0) {
    std::cout << " 失败 (exit " << result.exitCode << ")" << std::endl;
    throw std::runtime_error(displayName + " failed; log: " + logFile.string());
  }
  std::cout << " 完成 (" << fmtDur(elapsed(t0)) << ")" << std::endl;
}

} // namespace mvs
