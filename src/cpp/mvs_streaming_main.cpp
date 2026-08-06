#include "pipeline/StreamingPipeline.h"
#include "pipeline/Config.h"

#include <iostream>
#include <string>

void printUsage() {
  std::cout << "用法: ./build/mvs_streaming [选项]" << std::endl;
  std::cout << std::endl;
  std::cout << "必选参数:" << std::endl;
  std::cout << "  --config <file>        配置文件 (JSON)" << std::endl;
  std::cout << "  --sparse <dir>         COLMAP sparse model 目录" << std::endl;
  std::cout << "  --output <dir>         输出目录" << std::endl;
  std::cout << std::endl;
  std::cout << "示例:" << std::endl;
  std::cout << "  ./build/mvs_streaming --config config.json --sparse colmap/sparse/0 --output outputs/streaming" << std::endl;
}

int main(int argc, char** argv) {
  std::string config_file;
  std::string sparse_dir;
  std::string output_dir;

  // 解析参数
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_file = argv[++i];
    } else if (arg == "--sparse" && i + 1 < argc) {
      sparse_dir = argv[++i];
    } else if (arg == "--output" && i + 1 < argc) {
      output_dir = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      printUsage();
      return 0;
    }
  }

  if (config_file.empty() || sparse_dir.empty() || output_dir.empty()) {
    std::cerr << "错误: 缺少必需参数" << std::endl;
    std::cerr << std::endl;
    printUsage();
    return 1;
  }

  std::cout << "=== MVS 流式重建管线 ===" << std::endl;
  std::cout << "配置文件: " << config_file << std::endl;
  std::cout << "Sparse model: " << sparse_dir << std::endl;
  std::cout << "输出目录: " << output_dir << std::endl;
  std::cout << std::endl;
  std::cout << "原理: 直接写 OpenMVS 场景 + 多线程去畸变" << std::endl;
  std::cout << "取代: COLMAP image_undistorter + OpenMVS InterfaceCOLMAP" << std::endl;
  std::cout << std::endl;

  try {
    // 加载配置
    mvs::Config config;
    mvs::applyConfigFile(config, config_file);

    // 运行流式管线
    mvs::StreamingPipeline pipeline(config, sparse_dir, output_dir);
    pipeline.run();

    std::cout << std::endl;
    std::cout << "=== 完成 ===" << std::endl;
    std::cout << "产物: " << output_dir << "/openmvs/scene_dense.mvs" << std::endl;

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "错误: " << e.what() << std::endl;
    return 1;
  }
}
