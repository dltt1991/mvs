# Hybrid Pair Matching Experiment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evaluate whether ordered local image pairs plus a small set of bridge pairs can reduce COLMAP matching work below exhaustive matching without breaking sparse model connectivity.

**Architecture:** This is a non-default experiment that writes all generated files under `outputs/<run-name>/`. It does not modify `data/`. The experiment uses COLMAP `feature_extractor`, `matches_importer`, `mapper`, `image_undistorter`, and OpenMVS stages directly so the custom pair list can replace COLMAP's built-in matcher.

**Tech Stack:** Bash, Python 3 for pair-list generation, COLMAP CLI, OpenMVS CLI, existing `scripts/report_run.py`.

## Global Constraints

- Do not modify `./data`.
- Do not change the default production configuration unless the experiment clearly beats the current default.
- Stop before full TextureMesh unless sparse and mesh metrics are close to the current sorted-e2e baseline.
- Compare against `outputs/experiment-sorted-e2e-20260803-141308`.

---

### Task 1: Generate Local12 + Bridge Pair List

**Files:**
- Create: `outputs/experiment-hybrid-local12-bridges-<timestamp>/colmap/image_list.txt`
- Create: `outputs/experiment-hybrid-local12-bridges-<timestamp>/colmap/match_pairs.txt`

**Interfaces:**
- Consumes: `data/images`
- Produces: COLMAP pair list where each line is `image_a image_b`

- [ ] **Step 1: Create output directories**

Run: `mkdir -p "$RUN_DIR/colmap" "$RUN_DIR/logs" "$RUN_DIR/openmvs"`

- [ ] **Step 2: Generate sorted image list and pair list**

Generate local pairs with radius 12 in filename order, then add bridge pairs across the observed capture discontinuity between `IMG_20260730_110018.jpg` and `IMG_20260730_151015.jpg`, plus coarse stride bridges every 8 images.

- [ ] **Step 3: Count pair list**

Run: `wc -l "$RUN_DIR/colmap/match_pairs.txt"`

Expected: substantially fewer than 820 exhaustive pairs.

### Task 2: Run COLMAP Sparse Reconstruction

**Files:**
- Create: `outputs/experiment-hybrid-local12-bridges-<timestamp>/colmap/database.db`
- Create: `outputs/experiment-hybrid-local12-bridges-<timestamp>/colmap/sparse/`

**Interfaces:**
- Consumes: `match_pairs.txt`
- Produces: COLMAP sparse model

- [ ] **Step 1: Run feature extraction**

Use the same SIMPLE_RADIAL camera parameters as the current pipeline.

- [ ] **Step 2: Run `matches_importer`**

Run with `--match_type pairs` using the generated pair list.

- [ ] **Step 3: Run mapper**

Run mapper with the generated image list.

- [ ] **Step 4: Check connectivity**

Query database pair counts and inspect `colmap/sparse` model count. Continue only if the mapper produces one main model with near-complete registration.

### Task 3: Run Mesh-Only Dense Validation

**Files:**
- Create: `outputs/experiment-hybrid-local12-bridges-<timestamp>/openmvs/scene_dense.ply`
- Create: `outputs/experiment-hybrid-local12-bridges-<timestamp>/openmvs/scene_mesh.ply`

**Interfaces:**
- Consumes: sparse model `colmap/sparse/0`
- Produces: mesh-only OpenMVS output

- [ ] **Step 1: Run `image_undistorter`**

Use `--copy_policy HARD_LINK`.

- [ ] **Step 2: Run `InterfaceCOLMAP`**

Convert COLMAP dense workspace into `scene.mvs`.

- [ ] **Step 3: Run `DensifyPointCloud`**

Use the current default OpenMVS quality parameters.

- [ ] **Step 4: Run `ReconstructMesh`**

Generate `scene_mesh.ply`.

- [ ] **Step 5: Compare metrics**

Use `scripts/report_run.py` and direct log parsing to compare dense points and mesh faces with the sorted-e2e baseline.

### Task 4: Optional Texture Check

**Files:**
- Create: `outputs/experiment-hybrid-local12-bridges-<timestamp>/openmvs/scene_texture.ply`

**Interfaces:**
- Consumes: `scene_dense.mvs` and `scene_mesh.ply`
- Produces: texture patch count and textured mesh

- [ ] **Step 1: Run TextureMesh only if mesh metrics are close**

Use `--patch-packing-heuristic 100`.

- [ ] **Step 2: Compare texture patch count and wall time**

Do not recommend the hybrid strategy unless full textured estimate is better than the current default or pair count is meaningfully lower with acceptable quality.
