# C++ MVS Reconstruction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a C++ reconstruction core that runs COLMAP 4.1.1 and OpenMVS 2.4.0 from source-managed `3rd/` checkouts, with Python limited to I/O and UI orchestration.

**Architecture:** The project builds a standalone C++ executable, `mvs_reconstruct`, that validates inputs, constructs COLMAP/OpenMVS commands, executes each stage, logs results, and writes a manifest. Shell scripts fetch third-party source and build the workspace; Python scripts normalize camera input and launch the binary.

**Tech Stack:** CMake, C++17, POSIX process execution, nlohmann/json via CMake FetchContent, Python 3, COLMAP 4.1.1 source, OpenMVS v2.4.0 source.

## Global Constraints

- COLMAP source must live at `3rd/colmap` and be pinned to tag `4.1.1`.
- OpenMVS source must live at `3rd/openMVS` and be pinned to tag `v2.4.0`.
- Core reconstruction control must be implemented in compiled C++.
- Python must only perform I/O normalization and UI/CLI orchestration.
- Runtime outputs must be written under `outputs/<run-name>/`.
- Current project is not a Git repository, so commit steps are skipped unless Git is initialized later.

---

### Task 1: Project Build Skeleton

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/cpp/main.cpp`
- Create: `src/cpp/pipeline/Config.h`
- Create: `src/cpp/pipeline/Config.cpp`
- Create: `tests/cpp/config_test.cpp`

**Interfaces:**
- Produces: `mvs::Config mvs::parseArgs(int argc, char** argv)`
- Produces: `std::string mvs::Config::runName() const`
- Produces: executable target `mvs_reconstruct`
- Produces: test target `mvs_config_test`

- [ ] **Step 1: Write the failing config test**

```cpp
#include "pipeline/Config.h"

#include <cassert>
#include <string>

int main() {
  const char* argv[] = {
      "mvs_reconstruct",
      "--images", "data/images",
      "--cameras", "data/cameras.json",
      "--output", "outputs/test-run",
      "--colmap", "build/third_party/colmap/bin/colmap",
      "--openmvs-bin", "build/third_party/openmvs/bin"};
  auto config = mvs::parseArgs(11, const_cast<char**>(argv));
  assert(config.imagesDir == "data/images");
  assert(config.camerasJson == "data/cameras.json");
  assert(config.outputDir == "outputs/test-run");
  assert(config.colmapBinary == "build/third_party/colmap/bin/colmap");
  assert(config.openMvsBinDir == "build/third_party/openmvs/bin");
  assert(config.runName() == "test-run");
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target mvs_config_test && ./build/tests/cpp/mvs_config_test`
Expected: FAIL because `CMakeLists.txt` and `Config.h` do not exist.

- [ ] **Step 3: Implement CMake skeleton and config parser**

Create the top-level CMake project with C++17, `mvs_core`, `mvs_reconstruct`, and `mvs_config_test`. Implement a minimal option parser requiring `--images`, `--cameras`, `--output`, `--colmap`, and `--openmvs-bin`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target mvs_config_test && ./build/tests/cpp/mvs_config_test`
Expected: PASS with exit code 0.

### Task 2: Camera JSON Parser

**Files:**
- Create: `src/cpp/pipeline/CameraConfig.h`
- Create: `src/cpp/pipeline/CameraConfig.cpp`
- Create: `tests/cpp/camera_config_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `mvs::CameraDataset mvs::loadCameraDataset(const std::filesystem::path& path)`
- Produces: `mvs::CameraIntrinsics mvs::medianSimpleRadialCamera(const CameraDataset& dataset)`

- [ ] **Step 1: Write the failing camera parser test**

```cpp
#include "pipeline/CameraConfig.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>

int main() {
  const auto path = std::filesystem::temp_directory_path() / "mvs_camera_test.json";
  std::ofstream out(path);
  out << R"({
    "num_cameras": 2,
    "num_images": 2,
    "num_registered": 2,
    "cameras": [
      {"id": 1, "model": "SIMPLE_RADIAL", "width": 100, "height": 80, "params": [50.0, 50.0, 40.0, 0.01]},
      {"id": 2, "model": "SIMPLE_RADIAL", "width": 100, "height": 80, "params": [70.0, 50.0, 40.0, 0.03]}
    ]
  })";
  out.close();

  auto dataset = mvs::loadCameraDataset(path);
  assert(dataset.numImages == 2);
  assert(dataset.cameras.size() == 2);
  auto camera = mvs::medianSimpleRadialCamera(dataset);
  assert(camera.width == 100);
  assert(camera.height == 80);
  assert(std::abs(camera.f - 60.0) < 1e-9);
  assert(std::abs(camera.cx - 50.0) < 1e-9);
  assert(std::abs(camera.cy - 40.0) < 1e-9);
  assert(std::abs(camera.k1 - 0.02) < 1e-9);
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target mvs_camera_config_test && ./build/tests/cpp/mvs_camera_config_test`
Expected: FAIL because `CameraConfig.h` does not exist.

- [ ] **Step 3: Implement JSON parsing**

Use `nlohmann/json` via CMake FetchContent. Accept only `SIMPLE_RADIAL` cameras with four numeric params. Validate `num_images > 0`, non-empty cameras, positive dimensions, and matching required fields.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target mvs_camera_config_test && ./build/tests/cpp/mvs_camera_config_test`
Expected: PASS with exit code 0.

### Task 3: Process Runner and Manifest

**Files:**
- Create: `src/cpp/pipeline/ProcessRunner.h`
- Create: `src/cpp/pipeline/ProcessRunner.cpp`
- Create: `src/cpp/pipeline/Manifest.h`
- Create: `src/cpp/pipeline/Manifest.cpp`
- Create: `tests/cpp/process_manifest_test.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `mvs::CommandResult mvs::runCommand(const std::vector<std::string>& args, const std::filesystem::path& cwd, const std::filesystem::path& logFile)`
- Produces: `void mvs::writeManifest(const Manifest& manifest, const std::filesystem::path& path)`

- [ ] **Step 1: Write the failing process and manifest test**

```cpp
#include "pipeline/Manifest.h"
#include "pipeline/ProcessRunner.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
  const auto dir = std::filesystem::temp_directory_path() / "mvs_process_test";
  std::filesystem::create_directories(dir);
  auto result = mvs::runCommand({"/bin/sh", "-c", "printf ok"}, dir, dir / "cmd.log");
  assert(result.exitCode == 0);
  assert(result.stdoutText == "ok");
  assert(std::filesystem::exists(dir / "cmd.log"));

  mvs::Manifest manifest;
  manifest.status = "ok";
  manifest.failedStage = "";
  manifest.artifacts["sparse_model"] = "outputs/test/colmap/sparse/0";
  mvs::writeManifest(manifest, dir / "manifest.json");

  std::ifstream in(dir / "manifest.json");
  std::string json((std::istreambuf_iterator<char>(in)), {});
  assert(json.find("\"status\":\"ok\"") != std::string::npos);
  assert(json.find("sparse_model") != std::string::npos);
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target mvs_process_manifest_test && ./build/tests/cpp/mvs_process_manifest_test`
Expected: FAIL because process and manifest interfaces do not exist.

- [ ] **Step 3: Implement process execution and manifest writing**

Implement POSIX `popen`-based execution with shell-safe single-quote escaping. Write command logs with command line, working directory, exit code, stdout, and stderr placeholder text. Serialize manifest JSON with `nlohmann/json`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target mvs_process_manifest_test && ./build/tests/cpp/mvs_process_manifest_test`
Expected: PASS with exit code 0.

### Task 4: C++ Reconstruction Pipeline Commands

**Files:**
- Create: `src/cpp/pipeline/ReconstructionPipeline.h`
- Create: `src/cpp/pipeline/ReconstructionPipeline.cpp`
- Create: `tests/cpp/pipeline_command_test.cpp`
- Modify: `src/cpp/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `mvs::PipelinePlan mvs::buildPipelinePlan(const Config& config, const CameraIntrinsics& camera)`
- Produces: `int mvs::runPipeline(const Config& config)`

- [ ] **Step 1: Write the failing command construction test**

```cpp
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
  assert(plan.stages[0].args[0] == config.colmapBinary);
  assert(plan.stages[0].args[1] == "feature_extractor");
  assert(plan.stages[0].argsContains("--ImageReader.camera_model"));
  assert(plan.stages[4].name == "interface_colmap");
  assert(plan.stages[7].name == "texture_mesh");
  return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target mvs_pipeline_command_test && ./build/tests/cpp/mvs_pipeline_command_test`
Expected: FAIL because `ReconstructionPipeline.h` does not exist.

- [ ] **Step 3: Implement command planning and pipeline execution**

Create stages for `feature_extractor`, `exhaustive_matcher`, `mapper`, `image_undistorter`, `InterfaceCOLMAP`, `DensifyPointCloud`, `ReconstructMesh`, and `TextureMesh`. Each stage writes logs under `outputs/<run-name>/logs/<stage>.log`. `runPipeline` creates directories, loads cameras, executes stages in order, and writes `manifest.json`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target mvs_pipeline_command_test && ./build/tests/cpp/mvs_pipeline_command_test`
Expected: PASS with exit code 0.

### Task 5: Third-Party Fetch and Build Scripts

**Files:**
- Create: `scripts/fetch_3rdparty.sh`
- Create: `scripts/build.sh`
- Create: `scripts/reconstruct.sh`

**Interfaces:**
- Produces: `scripts/fetch_3rdparty.sh`
- Produces: `scripts/build.sh`
- Produces: `scripts/reconstruct.sh`

- [ ] **Step 1: Write executable scripts**

`fetch_3rdparty.sh` clones exact tags into `3rd/colmap` and `3rd/openMVS`, or verifies existing checkouts. `build.sh` configures the top-level CMake project and documents third-party build locations. `reconstruct.sh` runs `build/mvs_reconstruct` against `data/`.

- [ ] **Step 2: Verify script syntax**

Run: `bash -n scripts/fetch_3rdparty.sh scripts/build.sh scripts/reconstruct.sh`
Expected: PASS with no output.

- [ ] **Step 3: Verify fetch script can see upstream tags**

Run: `scripts/fetch_3rdparty.sh --check-only`
Expected: PASS and prints that COLMAP `4.1.1` and OpenMVS `v2.4.0` are available.

### Task 6: Python I/O Orchestration

**Files:**
- Create: `src/python/mvs_io/__init__.py`
- Create: `src/python/mvs_io/cameras.py`
- Create: `src/python/mvs_io/run.py`
- Create: `scripts/run_python_ui.py`

**Interfaces:**
- Produces: `python3 scripts/run_python_ui.py --images data/images --cameras data/cameras.json --output outputs/default`

- [ ] **Step 1: Write Python modules**

Implement camera JSON loading and a thin subprocess launcher for `build/mvs_reconstruct`.

- [ ] **Step 2: Verify Python syntax**

Run: `python3 -m py_compile src/python/mvs_io/__init__.py src/python/mvs_io/cameras.py src/python/mvs_io/run.py scripts/run_python_ui.py`
Expected: PASS with no output.

### Task 7: Final Verification

**Files:**
- Modify: `README.md`

**Interfaces:**
- Produces: documented commands for fetch, build, and reconstruction.

- [ ] **Step 1: Write README**

Document source layout, build requirements, fetch/build/reconstruct commands, and artifact paths.

- [ ] **Step 2: Run all fast checks**

Run: `bash -n scripts/fetch_3rdparty.sh scripts/build.sh scripts/reconstruct.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure && python3 -m py_compile src/python/mvs_io/__init__.py src/python/mvs_io/cameras.py src/python/mvs_io/run.py scripts/run_python_ui.py`
Expected: PASS.

- [ ] **Step 3: Optional long reconstruction**

Run: `scripts/reconstruct.sh`
Expected: Runs COLMAP/OpenMVS if third-party binaries and system dependencies are available. This step may take significant time on the full `data/images` set.
