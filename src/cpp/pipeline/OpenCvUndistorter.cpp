#include "pipeline/OpenCvUndistorter.h"

#include <colmap/image/undistortion.h>
#include <colmap/scene/reconstruction.h>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mvs {
namespace {

// COLMAP 相机模型 → OpenCV 畸变系数顺序 [k1,k2,p1,p2,(k3,k4,k5,k6)]。
// 顺序与 cv::undistort 的 distCoeffs 约定一致。
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

cv::Matx33d intrinsicsOf(const colmap::Camera& camera) {
  return cv::Matx33d(camera.FocalLengthX(), 0, camera.PrincipalPointX(),
                     0, camera.FocalLengthY(), camera.PrincipalPointY(),
                     0, 0, 1);
}

// 一张图的去畸变任务。newK 与 outputSize 成对使用：cv::undistort 的
// newCameraMatrix 语义就是"输出图像的内参"，其 cx/cy 已含 COLMAP 的裁剪偏移。
struct Job {
  std::filesystem::path inputPath;
  std::filesystem::path outputPath;
  cv::Matx33d cameraMatrix;
  cv::Matx33d newCameraMatrix;
  cv::Size outputSize;
  std::vector<double> distCoeffs;
};

}  // namespace

std::size_t runOpenCvUndistort(const std::filesystem::path& sparseModel,
                               const std::filesystem::path& imagesDir,
                               const std::filesystem::path& denseDir,
                               const OpenCvUndistortOptions& options) {
  namespace fs = std::filesystem;

  // 图像几何与 sparse model 必须用同一份 options，否则位姿与图像不匹配。
  // 默认值即 blank_pixels=0，与 colmap image_undistorter 的 CLI 默认一致。
  const colmap::UndistortCameraOptions camOptions{};

  colmap::Reconstruction reconstruction;
  reconstruction.Read(sparseModel.string());

  fs::create_directories(denseDir / "images");
  fs::create_directories(denseDir / "sparse");

  // 每个 camera_id 只算一次去畸变几何：UndistortCamera 内部要逐边界像素追踪，
  // 每张图重算是纯浪费（本数据集 41 张图共用 1 个相机）。
  std::unordered_map<colmap::camera_t, colmap::Camera> undistortedCameras;
  for (const auto& [cameraId, camera] : reconstruction.Cameras()) {
    undistortedCameras.emplace(cameraId, colmap::UndistortCamera(camOptions, camera));
  }

  std::vector<colmap::image_t> imageIds = reconstruction.RegImageIds();
  std::sort(imageIds.begin(), imageIds.end());

  std::vector<Job> jobs;
  jobs.reserve(imageIds.size());
  for (const colmap::image_t imageId : imageIds) {
    const colmap::Image& image = reconstruction.Image(imageId);
    const colmap::Camera& camera = reconstruction.Camera(image.CameraId());
    const colmap::Camera& undistorted = undistortedCameras.at(image.CameraId());

    Job job;
    job.inputPath = imagesDir / image.Name();
    job.outputPath = denseDir / "images" / image.Name();
    job.cameraMatrix = intrinsicsOf(camera);
    job.newCameraMatrix = intrinsicsOf(undistorted);
    job.outputSize = cv::Size(static_cast<int>(undistorted.width),
                              static_cast<int>(undistorted.height));
    job.distCoeffs = distCoeffsFor(camera);
    jobs.push_back(std::move(job));
  }

  // 子目录（COLMAP 允许 image.Name() 带相对路径）
  for (const Job& job : jobs) {
    if (job.outputPath.has_parent_path()) {
      fs::create_directories(job.outputPath.parent_path());
    }
  }

  unsigned threads = options.maxThreads > 0
                         ? static_cast<unsigned>(options.maxThreads)
                         : std::thread::hardware_concurrency();
  if (threads == 0) threads = 4;
  threads = std::min<unsigned>(threads, static_cast<unsigned>(std::max<std::size_t>(jobs.size(), 1)));

  std::atomic<std::size_t> next{0};
  std::atomic<int> completed{0};
  std::mutex errMu;
  std::vector<std::string> errors;

  std::vector<int> writeParams;
  if (options.jpegQuality != -1) {
    writeParams = {cv::IMWRITE_JPEG_QUALITY, options.jpegQuality};
  }

  const auto worker = [&]() {
    for (;;) {
      const std::size_t idx = next.fetch_add(1);
      if (idx >= jobs.size()) return;
      const Job& job = jobs[idx];

      cv::Mat orig = cv::imread(job.inputPath.string(), cv::IMREAD_COLOR);
      if (orig.empty()) {
        std::lock_guard<std::mutex> lk(errMu);
        errors.push_back("cannot read " + job.inputPath.string());
        continue;
      }

      cv::Mat undist;
      cv::undistort(orig, undist, job.cameraMatrix, job.distCoeffs, job.newCameraMatrix);

      // UndistortCamera 的裁剪尺寸小于原图时，cv::undistort 仍按输入尺寸输出，
      // 需按 newK 对应的输出尺寸裁掉多余边缘（cx/cy 已含偏移，从原点裁即可）。
      if (undist.cols != job.outputSize.width || undist.rows != job.outputSize.height) {
        const int w = std::min(job.outputSize.width, undist.cols);
        const int h = std::min(job.outputSize.height, undist.rows);
        undist = undist(cv::Rect(0, 0, w, h)).clone();
      }

      if (!cv::imwrite(job.outputPath.string(), undist, writeParams)) {
        std::lock_guard<std::mutex> lk(errMu);
        errors.push_back("cannot write " + job.outputPath.string());
        continue;
      }

      const int done = ++completed;
      if (done % 10 == 0 || done == static_cast<int>(jobs.size())) {
        std::cout << "  " << done << "/" << jobs.size() << std::flush;
      }
    }
  };

  std::vector<std::thread> pool;
  pool.reserve(threads);
  for (unsigned i = 0; i < threads; ++i) pool.emplace_back(worker);
  for (std::thread& t : pool) t.join();
  std::cout << std::endl;

  if (!errors.empty()) {
    throw std::runtime_error("opencv undistort failed for " + std::to_string(errors.size()) +
                             " images, first: " + errors.front());
  }

  // 去畸变后的 sparse model：相机换成 PINHOLE，观测坐标同步重写。
  colmap::Reconstruction undistortedReconstruction = reconstruction;
  colmap::UndistortReconstruction(camOptions, &undistortedReconstruction);
  undistortedReconstruction.Write((denseDir / "sparse").string());

  return jobs.size();
}

}  // namespace mvs
