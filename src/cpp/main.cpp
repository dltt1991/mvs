#include "pipeline/Config.h"
#include "pipeline/ReconstructionPipeline.h"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
  try {
    auto config = mvs::parseArgs(argc, argv);
    return mvs::runPipeline(config);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
