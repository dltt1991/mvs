# MVS Efficiency Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add low-risk efficiency infrastructure to the MVS pipeline without changing reconstruction quality or modifying `./data`.

**Architecture:** Keep the current C++ pipeline as the orchestration owner and continue running COLMAP/OpenMVS tools as external processes. Improve observability, reduce avoidable undistorted-image IO through hard links, and expose thread controls through explicit configuration.

**Tech Stack:** C++17, CMake, POSIX process execution, nlohmann/json, COLMAP 4.1.1, OpenMVS 2.4.0, shell/Python launchers.

## Global Constraints

- Do not write, move, delete, or rewrite anything under `./data`.
- Do not change reconstruction quality defaults except replacing undistorted image copying with link-based workspace materialization.
- Keep Python limited to I/O validation and launch orchestration.
- Preserve existing artifact paths under `outputs/<run-name>/`.

---

### Task 1: Runtime Metrics in Process Runner and Manifest

**Files:**
- Modify: `src/cpp/pipeline/ProcessRunner.h`
- Modify: `src/cpp/pipeline/ProcessRunner.cpp`
- Modify: `src/cpp/pipeline/Manifest.h`
- Modify: `src/cpp/pipeline/Manifest.cpp`
- Modify: `tests/cpp/process_manifest_test.cpp`

**Interfaces:**
- Produces: `CommandResult::peakResidentSetSizeKb`
- Produces: `CommandResult::userCpuSeconds`
- Produces: `CommandResult::systemCpuSeconds`
- Produces: `StageManifestEntry::peakResidentSetSizeKb`
- Produces: `StageManifestEntry::userCpuSeconds`
- Produces: `StageManifestEntry::systemCpuSeconds`

- [x] **Step 1: Write the failing test**

Add assertions that a command result reports non-negative resource metrics and that manifest JSON serializes those metric names.

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target mvs_process_manifest_test && ./build/tests/cpp/mvs_process_manifest_test`

- [x] **Step 3: Implement minimal metrics**

Use `wait4`/`getrusage` in the process runner so each child command records CPU time and peak RSS. Write command output directly to the log file while still preserving a small tail in `stdoutText`.

- [x] **Step 4: Run test to verify it passes**

Run: `cmake -S . -B build && cmake --build build --target mvs_process_manifest_test && ./build/tests/cpp/mvs_process_manifest_test`

### Task 2: Low-IO Undistorter and Thread Controls

**Files:**
- Modify: `src/cpp/pipeline/Config.h`
- Modify: `src/cpp/pipeline/Config.cpp`
- Modify: `src/cpp/pipeline/ReconstructionPipeline.cpp`
- Modify: `tests/cpp/config_test.cpp`
- Modify: `tests/cpp/pipeline_command_test.cpp`
- Modify: `scripts/reconstruct.sh`
- Modify: `src/python/mvs_io/run.py`

**Interfaces:**
- Produces: `Config::maxThreads`
- Produces: `Config::undistortCopyPolicy`
- Consumes CLI options: `--max-threads`, `--undistort-copy-policy`

- [x] **Step 1: Write failing config and command tests**

Assert optional CLI values are parsed, default values are safe, `image_undistorter` includes `--copy_policy HARD_LINK`, and thread arguments are added when `maxThreads > 0`.

- [x] **Step 2: Run tests to verify they fail**

Run: `cmake -S . -B build && cmake --build build --target mvs_config_test mvs_pipeline_command_test && ./build/tests/cpp/mvs_config_test && ./build/tests/cpp/mvs_pipeline_command_test`

- [x] **Step 3: Implement minimal configuration and command wiring**

Append COLMAP/OpenMVS thread arguments only when requested. Default `undistortCopyPolicy` to `HARD_LINK` and pass it only to `image_undistorter`.

- [x] **Step 4: Run tests to verify they pass**

Run: `cmake -S . -B build && cmake --build build --target mvs_config_test mvs_pipeline_command_test && ./build/tests/cpp/mvs_config_test && ./build/tests/cpp/mvs_pipeline_command_test`

### Task 3: Full Verification

**Files:**
- No production file changes.

**Interfaces:**
- Consumes all previous tasks.

- [x] **Step 1: Run full test suite**

Run: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`

- [x] **Step 2: Inspect diff for data safety**

Run: `git status --short && git diff -- data`

- [x] **Step 3: Report exact verification result**

Report changed files, test command result, and confirm whether `./data` was untouched.

### Task 4: Explicit Stage Reuse

**Files:**
- Modify: `src/cpp/pipeline/Config.h`
- Modify: `src/cpp/pipeline/Config.cpp`
- Modify: `src/cpp/pipeline/ReconstructionPipeline.h`
- Modify: `src/cpp/pipeline/ReconstructionPipeline.cpp`
- Modify: `tests/cpp/config_test.cpp`
- Modify: `tests/cpp/pipeline_command_test.cpp`
- Modify: `scripts/reconstruct.sh`
- Modify: `src/python/mvs_io/run.py`
- Modify: `README.md`

**Interfaces:**
- Produces: `Config::reuseExisting`
- Produces: `PipelineStage::markerFile`
- Produces: `PipelineStage::expectedArtifacts`
- Produces: `PipelineStage::signature`
- Produces: `bool stageArtifactsReady(const PipelineStage& stage)`
- Consumes CLI option: `--reuse-existing`

- [x] **Step 1: Write failing tests**

Assert `--reuse-existing 1` parses to `true`, defaults to `false`, pipeline stages carry marker/signature/artifact metadata, and `stageArtifactsReady` requires both matching marker content and expected artifacts.

- [x] **Step 2: Run tests to verify they fail**

Run: `cmake -S . -B build && cmake --build build --target mvs_config_test mvs_pipeline_command_test`

- [x] **Step 3: Implement minimal stage reuse**

Add explicit reuse configuration, per-stage marker files, command/input signatures, expected artifact checks, skipped manifest entries, and launcher/documentation wiring.

- [x] **Step 4: Run tests to verify they pass**

Run: `cmake -S . -B build && cmake --build build --target mvs_config_test mvs_pipeline_command_test && ./build/tests/cpp/mvs_config_test && ./build/tests/cpp/mvs_pipeline_command_test`

### Task 5: Densify Cleanup and Interactive Progress

**Files:**
- Modify: `src/cpp/pipeline/Config.h`
- Modify: `src/cpp/pipeline/Config.cpp`
- Modify: `src/cpp/pipeline/ReconstructionPipeline.cpp`
- Modify: `tests/cpp/config_test.cpp`
- Modify: `tests/cpp/pipeline_command_test.cpp`
- Modify: `scripts/reconstruct.sh`
- Modify: `src/python/mvs_io/run.py`
- Modify: `README.md`

**Interfaces:**
- Produces: `Config::removeDepthMaps`
- Consumes CLI option: `--remove-depth-maps`

- [x] **Step 1: Write failing tests**

Assert `--remove-depth-maps 0` parses to `false`, defaults to `true`, DensifyPointCloud includes `--remove-dmaps 1`, and densify stage reuse requires both `scene_dense.mvs` and `scene_dense.ply`.

- [x] **Step 2: Run tests to verify they fail**

Run: `cmake -S . -B build && cmake --build build --target mvs_config_test mvs_pipeline_command_test`

- [x] **Step 3: Implement minimal cleanup and progress flushing**

Wire `--remove-depth-maps` to OpenMVS `--remove-dmaps`, add `scene_dense.ply` to densify expected artifacts, flush stage progress output, and document shell/Python entry points.

- [x] **Step 4: Run tests and reuse smoke check**

Run: `cmake -S . -B build && cmake --build build --target mvs_config_test mvs_pipeline_command_test && ./build/tests/cpp/mvs_config_test && ./build/tests/cpp/mvs_pipeline_command_test`

Run: `RUN_NAME=profile-efficiency-20260803-104910 MVS_REUSE_EXISTING=1 scripts/reconstruct.sh`

### Task 6: Reconstruction Config File Defaults

**Files:**
- Create: `config/reconstruction.json`
- Modify: `src/cpp/pipeline/Config.cpp`
- Modify: `tests/cpp/config_test.cpp`
- Modify: `scripts/reconstruct.sh`
- Modify: `src/python/mvs_io/run.py`
- Modify: `scripts/build.sh`
- Modify: `README.md`

**Interfaces:**
- Consumes CLI option: `--config`
- Produces default config file: `config/reconstruction.json`

- [x] **Step 1: Write failing tests**

Assert `--config` loads required paths and efficiency options, command-line arguments override config file values, and no-argument parsing reads `config/reconstruction.json`.

- [x] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build && cmake --build build --target mvs_config_test && ./build/tests/cpp/mvs_config_test`

- [x] **Step 3: Implement config loading**

Load a JSON config before applying CLI options, validate required fields after merging, add `config/reconstruction.json`, and wire shell/Python/package entry points to use it.

- [x] **Step 4: Run full verification**

Run: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`

Run: `bash -n scripts/reconstruct.sh && bash -n scripts/build.sh`

Run: `python3 -m py_compile src/python/mvs_io/run.py`

### Task 7: Run Report Automation

**Files:**
- Create: `scripts/report_run.py`
- Create: `tests/python/report_run_test.py`
- Modify: `CMakeLists.txt`
- Modify: `scripts/build.sh`
- Modify: `README.md`
- Modify: `EFFICIENCY_OPTIMIZATION.md`

**Interfaces:**
- Produces CLI: `python3 scripts/report_run.py <run-dir>`
- Produces CLI option: `--json`
- Produces CLI option: `--output <path>`

- [x] **Step 1: Write failing test**

Create a fake run directory with manifest, OpenMVS logs, and fake artifacts. Assert the report JSON includes stage timing, memory, output size, dense point count, mesh vertices/faces, and texture vertices/faces.

- [x] **Step 2: Run test to verify it fails**

Run: `python3 tests/python/report_run_test.py`

- [x] **Step 3: Implement report script**

Read manifest, parse OpenMVS logs, collect key artifact sizes, print Markdown by default, support machine-readable `--json`, and support writing to `--output`.

- [x] **Step 4: Add to verification and package layout**

Add the Python test to CTest and copy `scripts/report_run.py` into packaged builds.

- [x] **Step 5: Run full verification**

Run: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`

Run: `bash -n scripts/reconstruct.sh && bash -n scripts/build.sh && python3 -m py_compile src/python/mvs_io/run.py scripts/report_run.py tests/python/report_run_test.py`
