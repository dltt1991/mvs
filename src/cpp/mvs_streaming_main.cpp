#include "pipeline/Config.h"
#include "pipeline/StreamingPipeline.h"

#include <iostream>

void printUsage(const char* prog) {
  std::cout << "用法: " << prog << " [选项]\n\n"
            << "必选参数（完整流程）:\n"
            << "  --config <file>        配置文件 (JSON)\n"
            << "  --output <dir>         输出目录\n\n"
            << "选项 (覆盖配置文件):\n"
            << "  --images <dir>         输入图像目录\n"
            << "  --cameras <file>       相机参数 JSON\n"
            << "  --colmap <bin>         COLMAP 可执行路径\n"
            << "  --openmvs-bin <dir>    OpenMVS 工具目录\n"
            << "  --max-threads <N>      并行线程数 (0=自动)\n"
            << "  --cuda-device <N>      GPU 设备号 (-1=自动)\n"
            << "  --matcher <type>       exhaustive | sequential\n"
            << "  --generate-texture <0|1>\n\n"
            << "仅 buildScene+undistort+densify 模式:\n"
            << "  --sparse <dir>         已有的 COLMAP sparse model 目录\n\n"
            << "示例（完整流程，取代 mvs_reconstruct）:\n"
            << "  " << prog << " --config config/reconstruction-fast.json --output outputs/run\n\n"
            << "示例（仅替换 undistorter + InterfaceCOLMAP 部分）:\n"
            << "  " << prog << " --config config.json --sparse colmap/sparse/0 --output outputs/run\n";
}

int main(int argc, char** argv) {
  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  // 检查 --help
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      printUsage(argv[0]);
      return 0;
    }
  }

  // 解析参数（复用 parseArgs，和 mvs_reconstruct 完全一致）
  mvs::Config config = mvs::parseArgs(argc, argv);

  // 判断模式：有 --sparse 就只跑 buildScene+undistort+densify
  std::string sparse;
  std::string output;
  for (int i = 1; i + 1 < argc; ++i) {
    std::string a = argv[i];
    if (a == "--sparse")  sparse  = argv[i + 1];
    if (a == "--output")  output  = argv[i + 1];
  }
  if (output.empty()) output = config.outputDir;

  try {
    if (!sparse.empty()) {
      // ── 部分模式：从已有 sparse model 开始 ──────────────────────────────
      std::cout << "=== 流式管线（部分模式：buildScene + undistort + densify） ===" << std::endl;
      std::cout << "Sparse model : " << sparse  << std::endl;
      std::cout << "输出目录     : " << output  << std::endl;
      std::cout << std::endl;

      mvs::StreamingPipeline pipeline(config, sparse, output);
      pipeline.run();

    } else {
      // ── 完整模式：特征提取 → ... → 纹理模型 ────────────────────────────
      std::cout << "=== 流式管线（完整模式，取代 mvs_reconstruct） ===" << std::endl;
      std::cout << "图像目录     : " << config.imagesDir    << std::endl;
      std::cout << "相机参数     : " << config.camerasJson  << std::endl;
      std::cout << "输出目录     : " << output              << std::endl;
      std::cout << std::endl;

      // 确保 outputDir 已设置
      if (config.outputDir.empty()) {
        if (output.empty()) {
          std::cerr << "错误: 请通过 --output 或配置文件中的 output 指定输出目录" << std::endl;
          return 1;
        }
        config.outputDir = output;
      }

      mvs::StreamingPipeline pipeline(config, config.outputDir);
      return pipeline.runFull();
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "错误: " << e.what() << std::endl;
    return 1;
  }
}
