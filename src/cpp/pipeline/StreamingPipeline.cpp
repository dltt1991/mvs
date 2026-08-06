#include "pipeline/StreamingPipeline.h"
#include "pipeline/ProcessRunner.h"

#include <colmap/scene/reconstruction.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace mvs {
namespace {

// COLMAP 相机模型 -> OpenCV 畸变系数 (k1,k2,p1,p2)
// COLMAP 去畸变后的图像使用 PINHOLE 模型，所以这里必须精确还原畸变模型
std::vector<double> distCoeffsFor(const colmap::Camera& camera) {
  const std::string model = camera.ModelName();
  if (model == "SIMPLE_PINHOLE" || model == "PINHOLE") {
    return {0.0, 0.0, 0.0, 0.0};
  }
  if (model == "SIMPLE_RADIAL") {
    // params: f, cx, cy, k
    return {camera.params[3], 0.0, 0.0, 0.0};
  }
  if (model == "RADIAL") {
    // params: f, cx, cy, k1, k2
    return {camera.params[3], camera.params[4], 0.0, 0.0};
  }
  if (model == "OPENCV") {
    // params: fx, fy, cx, cy, k1, k2, p1, p2
    return {camera.params[4], camera.params[5], camera.params[6], camera.params[7]};
  }
  if (model == "FULL_OPENCV") {
    // params: fx, fy, cx, cy, k1, k2, p1, p2, k3, k4, k5, k6
    return {camera.params[4], camera.params[5], camera.params[6], camera.params[7],
            camera.params[8], camera.params[9], camera.params[10], camera.params[11]};
  }
  throw std::runtime_error("unsupported camera model for streaming undistort: " + model);
}

} // namespace

StreamingPipeline::StreamingPipeline(const Config& config,
                                     const std::filesystem::path& sparseModel,
                                     const std::filesystem::path& outputDir)
    : config_(config),
      sparseModel_(sparseModel),
      outputDir_(outputDir) {}

void StreamingPipeline::run() {
  std::cout << "=== 流式融合管线 ===" << std::endl;
  std::cout << "策略: 直接写 OpenMVS 场景 + 多线程去畸变（绕过 undistorter/InterfaceCOLMAP）" << std::endl;
  std::cout << "Sparse model: " << sparseModel_ << std::endl;
  std::cout << "输出目录: " << outputDir_ << std::endl;
  std::cout << std::endl;

  std::filesystem::create_directories(outputDir_ / "images");
  std::filesystem::create_directories(outputDir_ / "openmvs");

  const auto t0 = std::chrono::steady_clock::now();

  // 阶段 1：构建场景（毫秒级，取代 InterfaceCOLMAP）
  const std::vector<UndistortJob> jobs = buildScene();
  const auto t1 = std::chrono::steady_clock::now();
  const double sceneTime = std::chrono::duration<double>(t1 - t0).count();
  std::cout << "[Scene] scene.mvs 写入完成，耗时: " << sceneTime << "s" << std::endl;
  std::cout << std::endl;

  // 阶段 2：多线程去畸变（取代 COLMAP image_undistorter）
  undistortParallel(jobs);
  const auto t2 = std::chrono::steady_clock::now();
  const double undistortTime = std::chrono::duration<double>(t2 - t1).count();
  std::cout << "[Undistort] 全部完成，耗时: " << undistortTime << "s" << std::endl;
  std::cout << std::endl;

  // 阶段 3：稠密化
  const bool ok = densify();
  const auto t3 = std::chrono::steady_clock::now();
  const double densifyTime = std::chrono::duration<double>(t3 - t2).count();
  const double total = std::chrono::duration<double>(t3 - t0).count();

  std::cout << std::endl;
  std::cout << "=== 流式管线完成 ===" << std::endl;
  std::cout << "  scene.mvs 构建 : " << sceneTime << "s" << std::endl;
  std::cout << "  去畸变         : " << undistortTime << "s" << std::endl;
  std::cout << "  DensifyPointCloud: " << densifyTime << "s" << std::endl;
  std::cout << "  总耗时         : " << total << "s" << std::endl;

  if (!ok) {
    throw std::runtime_error("DensifyPointCloud failed; see densify_streaming.log");
  }
}

std::vector<UndistortJob> StreamingPipeline::buildScene() {
  std::cout << "[Scene] 加载 sparse reconstruction..." << std::endl;
  colmap::Reconstruction reconstruction;
  reconstruction.Read(sparseModel_);
  std::cout << "[Scene] 已加载: " << reconstruction.NumRegImages() << " 张图像, "
            << reconstruction.NumCameras() << " 个相机, "
            << reconstruction.NumPoints3D() << " 个 3D 点" << std::endl;

  MVS::Interface scene;

  // 每个 COLMAP 相机对应一个 platform，platform 内 cameraID 恒为 0
  // （与 InterfaceCOLMAP 一致：OpenMVS 内部 ASSERT(image.cameraID == 0)）
  std::unordered_map<colmap::camera_t, uint32_t> cameraToPlatform;
  for (const auto& [cameraId, camera] : reconstruction.Cameras()) {
    MVS::Interface::Platform platform;
    platform.name = "platform" + std::to_string(cameraId);

    MVS::Interface::Platform::Camera mvsCamera;
    mvsCamera.name = camera.ModelName();
    mvsCamera.width = static_cast<uint32_t>(camera.width);
    mvsCamera.height = static_cast<uint32_t>(camera.height);
    // 去畸变后为 PINHOLE；COLMAP 像素中心在 (0.5,0.5)，OpenMVS 在整数坐标
    mvsCamera.K = MVS::Interface::Mat33d::eye();
    mvsCamera.K(0, 0) = camera.FocalLengthX();
    mvsCamera.K(1, 1) = camera.FocalLengthY();
    mvsCamera.K(0, 2) = camera.PrincipalPointX() - 0.5;
    mvsCamera.K(1, 2) = camera.PrincipalPointY() - 0.5;
    mvsCamera.R = MVS::Interface::Mat33d::eye();
    mvsCamera.C = MVS::Interface::Pos3d(0, 0, 0);
    platform.cameras.push_back(mvsCamera);

    cameraToPlatform[cameraId] = static_cast<uint32_t>(scene.platforms.size());
    scene.platforms.push_back(std::move(platform));
  }

  // 图像：COLMAP 全局 image_id -> OpenMVS 局部下标
  std::unordered_map<colmap::image_t, uint32_t> imageToLocal;
  std::vector<UndistortJob> jobs;

  std::vector<colmap::image_t> imageIds = reconstruction.RegImageIds();
  std::sort(imageIds.begin(), imageIds.end());

  for (const colmap::image_t imageId : imageIds) {
    const colmap::Image& image = reconstruction.Image(imageId);
    const colmap::Camera& camera = reconstruction.Camera(image.CameraId());

    const uint32_t platformId = cameraToPlatform.at(image.CameraId());
    MVS::Interface::Platform& platform = scene.platforms[platformId];

    MVS::Interface::Image mvsImage;
    // 绝对路径：DensifyPointCloud 的 working-folder 与图像目录不同
    mvsImage.name = (outputDir_ / "images" / image.Name()).string();
    mvsImage.platformID = platformId;
    mvsImage.cameraID = 0;
    mvsImage.ID = imageId;

    // 位姿：OpenMVS 的 R 是 world->camera，C 是相机中心（world 坐标）
    const colmap::Rigid3d& camFromWorld = image.CamFromWorld();
    const Eigen::Matrix3d R = camFromWorld.rotation().toRotationMatrix();
    const Eigen::Vector3d t = camFromWorld.translation();
    const Eigen::Vector3d C = -(R.transpose() * t);

    MVS::Interface::Platform::Pose pose;
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        pose.R(r, c) = R(r, c);
      }
    }
    pose.C = MVS::Interface::Pos3d(C.x(), C.y(), C.z());

    mvsImage.poseID = static_cast<uint32_t>(platform.poses.size());
    platform.poses.push_back(pose);

    imageToLocal[imageId] = static_cast<uint32_t>(scene.images.size());
    scene.images.push_back(std::move(mvsImage));

    // 去畸变任务
    UndistortJob job;
    job.inputPath = std::filesystem::path(config_.imagesDir) / image.Name();
    job.outputPath = outputDir_ / "images" / image.Name();
    job.cameraMatrix = cv::Matx33d(
        camera.FocalLengthX(), 0.0, camera.PrincipalPointX(),
        0.0, camera.FocalLengthY(), camera.PrincipalPointY(),
        0.0, 0.0, 1.0);
    job.distCoeffs = distCoeffsFor(camera);
    jobs.push_back(std::move(job));
  }

  // 3D 点：views 使用局部下标并按下标排序（与 InterfaceCOLMAP 一致）
  for (const auto& [pointId, point] : reconstruction.Points3D()) {
    MVS::Interface::Vertex vertex;
    vertex.X = MVS::Interface::Pos3f(
        static_cast<float>(point.xyz.x()),
        static_cast<float>(point.xyz.y()),
        static_cast<float>(point.xyz.z()));

    for (const auto& element : point.track.Elements()) {
      const auto it = imageToLocal.find(element.image_id);
      if (it == imageToLocal.end()) {
        continue;  // 未注册的图像
      }
      MVS::Interface::Vertex::View view;
      view.imageID = it->second;
      view.confidence = 0.0f;
      vertex.views.push_back(view);
    }
    if (vertex.views.size() < 2) {
      continue;
    }
    std::sort(vertex.views.begin(), vertex.views.end(),
              [](const MVS::Interface::Vertex::View& a,
                 const MVS::Interface::Vertex::View& b) { return a.imageID < b.imageID; });

    scene.vertices.push_back(std::move(vertex));
    scene.verticesColor.push_back(MVS::Interface::Color{
        MVS::Interface::Col3(point.color(2), point.color(1), point.color(0))});  // BGR
  }

  std::cout << "[Scene] 场景: " << scene.platforms.size() << " platform, "
            << scene.images.size() << " 图像, " << scene.vertices.size() << " 点" << std::endl;

  const std::filesystem::path scenePath = outputDir_ / "openmvs" / "scene.mvs";
  if (!MVS::ARCHIVE::SerializeSave(scene, scenePath.string())) {
    throw std::runtime_error("failed to write " + scenePath.string());
  }
  return jobs;
}

void StreamingPipeline::undistortParallel(const std::vector<UndistortJob>& jobs) {
  unsigned threads = config_.maxThreads > 0
                         ? static_cast<unsigned>(config_.maxThreads)
                         : std::thread::hardware_concurrency();
  if (threads == 0) {
    threads = 4;
  }
  threads = std::min<unsigned>(threads, static_cast<unsigned>(jobs.size()));

  std::cout << "[Undistort] " << jobs.size() << " 张图像，" << threads << " 线程" << std::endl;

  std::atomic<size_t> next{0};
  std::mutex errorMutex;
  std::vector<std::string> errors;

  const auto worker = [&]() {
    for (;;) {
      const size_t index = next.fetch_add(1);
      if (index >= jobs.size()) {
        return;
      }
      const UndistortJob& job = jobs[index];

      const cv::Mat original = cv::imread(job.inputPath.string(), cv::IMREAD_COLOR);
      if (original.empty()) {
        std::lock_guard<std::mutex> lock(errorMutex);
        errors.push_back("cannot read " + job.inputPath.string());
        continue;
      }

      cv::Mat undistorted;
      // newCameraMatrix == cameraMatrix，保证与 scene.mvs 中声明的 PINHOLE 内参一致
      cv::undistort(original, undistorted, job.cameraMatrix, job.distCoeffs, job.cameraMatrix);

      if (!cv::imwrite(job.outputPath.string(), undistorted,
                       {cv::IMWRITE_JPEG_QUALITY, 95})) {
        std::lock_guard<std::mutex> lock(errorMutex);
        errors.push_back("cannot write " + job.outputPath.string());
        continue;
      }

      const int done = ++imagesCompleted_;
      if (done % 10 == 0 || done == static_cast<int>(jobs.size())) {
        std::cout << "[Undistort] " << done << "/" << jobs.size() << std::endl;
      }
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(threads);
  for (unsigned i = 0; i < threads; ++i) {
    pool.emplace_back(worker);
  }
  for (std::thread& t : pool) {
    t.join();
  }

  if (!errors.empty()) {
    throw std::runtime_error("undistort failed for " + std::to_string(errors.size()) +
                             " image(s), first: " + errors.front());
  }
}

bool StreamingPipeline::densify() {
  std::cout << "[Densify] 启动 DensifyPointCloud..." << std::endl;

  const std::filesystem::path openmvsBin(config_.openMvsBinDir);
  std::vector<std::string> args{
    (openmvsBin / "DensifyPointCloud").string(),
    (outputDir_ / "openmvs" / "scene.mvs").string(),
    "--working-folder", (outputDir_ / "openmvs").string(),
    "--output-file", "scene_dense.mvs",
    "--number-views", std::to_string(config_.densifyNumberViews),
    "--number-views-fuse", std::to_string(config_.densifyNumberViewsFuse),
    "--geometric-iters", std::to_string(config_.densifyGeometricIters),
    "--resolution-level", std::to_string(config_.densifyResolutionLevel),
    "--max-resolution", std::to_string(config_.densifyMaxResolution),
    "--iters", std::to_string(config_.densifyIters),
    "--max-threads", std::to_string(config_.maxThreads),
    "--remove-dmaps", config_.removeDepthMaps ? "1" : "0"
  };
  if (config_.cudaDevice != -1) {
    args.push_back("--cuda-device");
    args.push_back(std::to_string(config_.cudaDevice));
  }

  const std::filesystem::path log = outputDir_ / "densify_streaming.log";
  const CommandResult result = runCommand(args, outputDir_, log);
  if (result.exitCode != 0) {
    std::cerr << "[Densify] 失败 (exit " << result.exitCode << ")，日志: " << log << std::endl;
    return false;
  }
  std::cout << "[Densify] 完成" << std::endl;
  return true;
}

} // namespace mvs
