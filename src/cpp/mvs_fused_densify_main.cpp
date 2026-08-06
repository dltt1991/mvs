// 融合管线的主入口程序

#include "pipeline/Config.h"
#include "pipeline/FusedPipeline.h"

#include <iostream>
#include <filesystem>

void printUsage(const char* progName) {
  std::cout << "用法: " << progName << " [选项]" << std::endl;
  std::cout << std::endl;
  std::cout << "必选参数:" << std::endl;
  std::cout << "  --config <file>        配置文件 (JSON)" << std::endl;
  std::cout << "  --sparse <dir>         COLMAP sparse model 目录" << std::endl;
  std::cout << "  --output <dir>         输出目录" << std::endl;
  std::cout << std::endl;
  std::cout << "示例:" << std::endl;
  std::cout << "  " << progName << " --config config.json --sparse colmap/sparse/0 --output outputs/fused" << std::endl;
}

int main(int argc, char** argv) {
  if (argc < 7) {
    printUsage(argv[0]);
    return 1;
  }

  std::string config_file;
  std::string sparse_model;
  std::string output_dir;

  // 解析参数
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_file = argv[++i];
    } else if (arg == "--sparse" && i + 1 < argc) {
      sparse_model = argv[++i];
    } else if (arg == "--output" && i + 1 < argc) {
      output_dir = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    }
  }

  if (config_file.empty() || sparse_model.empty() || output_dir.empty()) {
    std::cerr << "错误: 缺少必选参数" << std::endl;
    printUsage(argv[0]);
    return 1;
  }

  try {
    // 1. 加载配置
    mvs::Config config;
    mvs::applyConfigFile(config, config_file);

    std::cout << "=== MVS 融合重建管线 ===" << std::endl;
    std::cout << "配置文件: " << config_file << std::endl;
    std::cout << "Sparse model: " << sparse_model << std::endl;
    std::cout << "输出目录: " << output_dir << std::endl;
    std::cout << std::endl;
    std::cout << "原理: undistorter (CPU) 和 DensifyPointCloud (GPU) 并行执行" << std::endl;
    std::cout << "预期节省: ~50-70s (相比串行执行)" << std::endl;
    std::cout << std::endl;

    // 2. 创建输出目录
    std::filesystem::create_directories(output_dir);

    // 3. 运行融合管线
    mvs::FusedPipeline pipeline(config, sparse_model, output_dir);

    auto start = std::chrono::steady_clock::now();
    pipeline.run();
    auto end = std::chrono::steady_clock::now();

    double elapsed = std::chrono::duration<double>(end - start).count();

    std::cout << std::endl;
    std::cout << "=== 完成 ===" << std::endl;
    std::cout << "总耗时: " << elapsed << "s" << std::endl;
    std::cout << "产物: " << output_dir << "/openmvs/scene_dense.mvs" << std::endl;

    return 0;

  } catch (const std::exception& e) {
    std::cerr << "错误: " << e.what() << std::endl;
    return 1;
  }
}
