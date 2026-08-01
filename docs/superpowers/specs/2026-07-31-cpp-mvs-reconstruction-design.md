# C++ MVS Reconstruction Design

## Goal

Build a source-integrated monocular multi-view reconstruction project that uses COLMAP/pycolmap 4.1.1 source and OpenMVS 2.4.0 source under `3rd/`, with the reconstruction core compiled and run in C++. Python is limited to I/O preparation and UI/CLI orchestration.

## Scope

The project reconstructs an object or scene from the existing image set in `data/images` and the existing camera metadata in `data/cameras.json`. It produces COLMAP sparse reconstruction artifacts, OpenMVS dense point clouds, meshes, and textured meshes under a configurable output directory.

The first version targets a local workstation build. It does not implement a web UI, distributed processing, live camera capture, or neural rendering.

## Third-Party Source Layout

`3rd/colmap` is checked out from `https://github.com/colmap/colmap.git` at tag `4.1.1`. This is the source location that contains COLMAP 4.1.1 and pycolmap bindings; the standalone `colmap/pycolmap` repository does not provide a `4.1.1` tag.

`3rd/openMVS` is checked out from `https://github.com/cdcseacave/openMVS.git` at tag `v2.4.0`.

The project provides scripts to fetch or update these exact versions without silently switching to floating branches.

## Architecture

The C++ executable owns the reconstruction pipeline:

1. Validate input image directory and camera JSON.
2. Create a workspace with COLMAP database, sparse model, undistorted images, OpenMVS scene files, and final outputs.
3. Run COLMAP feature extraction, matching, mapping, and model export through C++ wrappers around COLMAP executables or libraries.
4. Convert COLMAP output to OpenMVS format using OpenMVS tools.
5. Run OpenMVS densification, meshing, mesh refinement, and texturing.
6. Write a machine-readable manifest summarizing output paths and command status.

Python scripts do only orchestration and presentation:

1. Prepare config files from `data/cameras.json`.
2. Launch the compiled C++ executable.
3. Offer a thin command-line entry point for common runs.
4. Optionally inspect outputs and print user-facing summaries.

## Components

`src/cpp/` contains the compiled core:

- `main.cpp`: parses command-line options and calls the pipeline.
- `pipeline/Config.*`: stores input paths, output paths, camera model settings, and quality presets.
- `pipeline/ProcessRunner.*`: executes external COLMAP/OpenMVS tools with logged stdout/stderr and checked exit codes.
- `pipeline/CameraConfig.*`: parses and validates `cameras.json`.
- `pipeline/ReconstructionPipeline.*`: coordinates COLMAP and OpenMVS stages.
- `pipeline/Manifest.*`: writes `manifest.json` for downstream Python/UI code.

`src/python/` contains orchestration:

- `mvs_io/cameras.py`: validates and normalizes camera JSON for the C++ executable.
- `mvs_io/run.py`: launches the compiled binary with paths and presets.

`scripts/` contains developer and user scripts:

- `fetch_3rdparty.sh`: clones exact COLMAP and OpenMVS versions into `3rd/`.
- `build.sh`: configures and builds third-party projects and the C++ core.
- `reconstruct.sh`: runs a standard reconstruction using `data/`.

## Data Flow

Input:

- `data/images/*.jpg`
- `data/cameras.json`

Workspace output:

- `build/third_party/`: compiled COLMAP and OpenMVS artifacts.
- `outputs/<run-name>/colmap/database.db`
- `outputs/<run-name>/colmap/sparse/`
- `outputs/<run-name>/colmap/dense/`
- `outputs/<run-name>/openmvs/scene.mvs`
- `outputs/<run-name>/openmvs/scene_dense.mvs`
- `outputs/<run-name>/openmvs/scene_mesh.ply`
- `outputs/<run-name>/openmvs/scene_texture.ply` (plus `scene_texture*.png` texture atlases)
- `outputs/<run-name>/manifest.json`

OpenMVS runs in interface mode (`--archive-type` defaults to `-1`), where `.mvs` carries only
cameras and poses while geometry lives in the sibling `.ply`. `ReconstructMesh` and `TextureMesh`
therefore do not rewrite `.mvs`; downstream stages load geometry via `--pointcloud-file` /
`--mesh-file`.

## Camera Handling

The existing `data/cameras.json` is treated as the authoritative camera metadata. The first implementation validates that the image count, dimensions, and supported camera model fields are present.

Because COLMAP commonly expects image-to-camera associations and database camera rows, the first build will support a conservative path: one shared `SIMPLE_RADIAL` camera initialized from the median focal length and distortion values in the JSON, then COLMAP bundle adjustment may refine intrinsics. If per-image camera IDs are required later, that will be added as an explicit extension after the baseline pipeline works.

## Error Handling

Every external tool invocation records command, working directory, exit code, stdout, and stderr under the run output directory. The C++ pipeline stops at the first failed stage and writes a manifest marking the run as failed with the failing stage name.

Input validation errors are reported before any expensive reconstruction work starts.

## Build Strategy

The top-level project uses CMake for the C++ core. Third-party source is fetched into `3rd/`, then built into `build/third_party`.

The first implementation may call COLMAP/OpenMVS command-line binaries from the C++ executable instead of linking every internal library directly. This keeps the compiled core in C++ while avoiding fragile private library APIs. The C++ process remains the owner of execution, validation, logging, and output contracts.

## Testing

Unit tests cover camera JSON parsing, command construction, process failure propagation, output manifest writing, and path validation.

Integration smoke tests run against a tiny fixture image directory when available. The full `data/images` reconstruction is exposed as a manual or long-running integration command because it is computationally expensive.

## Acceptance Criteria

- `scripts/fetch_3rdparty.sh` fetches COLMAP `4.1.1` and OpenMVS `v2.4.0` into `3rd/`.
- `scripts/build.sh` builds the C++ core and prepares third-party binaries under `build/`.
- `scripts/reconstruct.sh` runs the C++ pipeline on `data/images` and `data/cameras.json`.
- Core reconstruction control is implemented in C++.
- Python code is limited to I/O normalization and UI/CLI orchestration.
- The pipeline writes a manifest with final artifact paths and stage status.
