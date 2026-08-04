# MVS Efficiency Optimization Log

本文档记录稠密 3D 重建系统的效率优化过程、基线数据、已落地改动和后续实验方向。目标是在不降低重建效果的前提下，先减少工程浪费，再用可测方式评估算法参数。

## 约束

- 不修改 `./data` 下的原始输入数据。
- 默认参数优先保持重建质量不变。
- 任何可能影响最终几何或纹理效果的参数，都必须通过 A/B 对比后再进入默认配置。
- 输出、日志、manifest 和中间产物都写入 `outputs/<run-name>/`。

## 配置入口

默认配置文件：

```text
config/reconstruction.json
```

配置加载顺序：

1. C++ 程序先读取配置文件。
2. 命令行参数覆盖配置文件。
3. `scripts/reconstruct.sh` 会用项目/包内路径覆盖数据目录、输出目录和二进制路径。

常用配置项：

- `max_threads`: `0` 表示使用工具默认线程策略；正整数用于限制 COLMAP/OpenMVS 线程数。
- `undistort_copy_policy`: `HARD_LINK` / `SOFT_LINK` / `COPY`。
- `reuse_existing`: 是否复用同一输出目录中签名匹配的已完成阶段。
- `remove_depth_maps`: 是否在 dense fusion 后删除 `.dmap` 中间文件。
- `generate_texture`: 是否运行最终 TextureMesh；默认 `true`，`false` 用于先返回几何结果。

## 初始基线

基线运行：

```bash
RUN_NAME=profile-efficiency-20260803-104910 scripts/reconstruct.sh
```

输入规模：

- 图片文件数：41 个文件。
- 相机 JSON：`data/cameras.json`。
- OpenMVS Densify 阶段日志显示：41 images，1055.48 MPixels，总计约 25.74 MPixels/image。

总耗时：

```text
1294.30s ~= 21m34s
```

阶段耗时和资源：

| Stage | Time | Peak RSS | User CPU | Notes |
| --- | ---: | ---: | ---: | --- |
| `feature_extractor` | 20.93s | 679 MB | 156.51s | COLMAP 特征提取 |
| `exhaustive_matcher` | 26.02s | 172 MB | 142.37s | COLMAP 穷举匹配 |
| `mapper` | 8.65s | 251 MB | 10.72s | COLMAP SfM |
| `image_undistorter` | 49.59s | 2.47 GB | 392.63s | dense workspace 准备 |
| `interface_colmap` | 0.38s | 56 MB | 0.15s | OpenMVS scene 转换 |
| `densify_point_cloud` | 789.51s | 5.24 GB | 6658.85s | 主瓶颈，约 61% 总耗时 |
| `reconstruct_mesh` | 76.60s | 6.07 GB | 238.01s | mesh 重建 |
| `texture_mesh` | 322.61s | 8.92 GB | 1917.79s | 次瓶颈，约 25% 总耗时 |

基线结论：

- 第一瓶颈是 `DensifyPointCloud`，尤其 depth-map 和 geometric-consistency 迭代。
- 第二瓶颈是 `TextureMesh`，主要耗在给数百万面选择最佳视图和纹理生成。
- COLMAP 前半段总耗时约 55s，不是当前主要瓶颈。
- 简单合并进程不是优先级最高的优化；真实收益更可能来自复用、减少 IO、中间文件清理、OpenMVS 参数 A/B 和纹理阶段拆分。

## 已完成优化

### 1. 实时日志与资源统计

改动：

- `ProcessRunner` 从 `popen` 改为 `fork/execvp + pipe + wait4`。
- 子进程 stdout/stderr 边读边写 stage log。
- manifest 每个 stage 增加：
  - `peak_resident_set_size_kb`
  - `user_cpu_seconds`
  - `system_cpu_seconds`

收益：

- 不再把完整日志堆在主进程内存里。
- 每阶段能看到 CPU 时间和峰值内存，便于定位瓶颈。

### 2. `image_undistorter` 减少复制 IO

改动：

- 默认 `undistort_copy_policy` 为 `HARD_LINK`。
- 配置候选：
  - `HARD_LINK`: 同文件系统下最低 IO。
  - `SOFT_LINK`: 符号链接。
  - `COPY`: 兼容性最高，但 IO 和磁盘占用最大。

收益：

- 减少 dense workspace 图片复制。
- 不改变重建算法和最终效果。

### 3. 线程参数显式配置

改动：

- 增加 `max_threads` 配置。
- 传递到 COLMAP/OpenMVS 对应线程参数。

收益：

- 可以在长任务中给系统、UI 和 IO 留 CPU 余量。
- 也可以为后续 A/B 固定线程数，使耗时对比更稳定。

### 4. 显式阶段复用

改动：

- 增加 `reuse_existing` 配置。
- 每个 stage 成功后写 marker。
- 复用需要同时满足：
  - marker 存在。
  - 命令签名一致。
  - 输入图片/相机文件快照一致。
  - 目标产物存在且非空。

验证：

```bash
RUN_NAME=profile-efficiency-20260803-104910 MVS_REUSE_EXISTING=1 scripts/reconstruct.sh
```

结果：

```text
8 个阶段全部跳过，总耗时约 0.01s
```

收益：

- 失败恢复、后处理调参、重复检查时不用从头跑 20 分钟。

### 5. 删除 OpenMVS `.dmap` 中间文件

改动：

- 默认给 `DensifyPointCloud` 增加 `--remove-dmaps 1`。
- 需要调试深度图时可设置 `remove_depth_maps=false`。

基线观察：

- profile 输出目录中 `.dmap` 文件约 94 MB/个，合计超过 3 GB。

收益：

- 大幅减少输出目录体积。
- 不影响最终 `scene_dense.ply`、mesh 和 texture 产物。

### 6. 进度输出刷新

改动：

- 关键阶段输出增加 flush。

收益：

- 长阶段运行时，终端能及时看到当前跑到哪个 stage。

### 7. 配置文件默认值与注释

改动：

- 新增 `config/reconstruction.json`。
- 配置文件支持 JSONC 风格 `//` 注释。
- 每个配置项写明含义、候选配置和 CLI 覆盖项。

收益：

- 用户不传参数时可直接使用默认配置。
- 常用效率参数集中管理，减少脚本和命令行散落配置。

### 8. 运行报告自动化

改动：

- 新增 `scripts/report_run.py`。
- 支持 Markdown 报告、JSON 输出和写入指定文件。
- 报告内容包括：
  - manifest 状态和阶段耗时。
  - 每阶段峰值内存、user CPU、system CPU。
  - 输出目录总大小。
  - 关键产物大小。
  - OpenMVS dense point 数量、mesh vertices/faces、texture vertices/faces。

示例：

```bash
python3 scripts/report_run.py outputs/default
python3 scripts/report_run.py outputs/default --json
python3 scripts/report_run.py outputs/default --output outputs/default/report.md
```

收益：

- 后续 Densify / Texture / Matcher A/B 实验可以自动留档。
- 减少手抄日志时出错。
- 可以把耗时、内存、体积和几何指标放在同一份报告里比较。

### 9. Texture 阶段可选拆分

改动：

- 新增 `generate_texture` 配置和 `--generate-texture` CLI。
- `scripts/reconstruct.sh` 支持 `MVS_GENERATE_TEXTURE=0`。
- Python 编排入口支持 `--generate-texture 0`。
- 默认仍为 `true`，完整生成 textured mesh。

收益：

- 不改变默认最终效果。
- UI 或调参流程可以先拿到 `scene_mesh.ply`，避免等待波动最大的 TextureMesh 阶段。
- TextureMesh 可作为后台任务或单独队列执行，便于把 IO/算法/UI 渲染流程解耦。

## 当前瓶颈排序

按基线耗时排序：

1. `DensifyPointCloud`: 789.51s。
2. `TextureMesh`: 322.61s。
3. `ReconstructMesh`: 76.60s。
4. `image_undistorter`: 49.59s。
5. COLMAP feature/match/mapper 合计约 55.60s。

## 候选实验结果

### A. 顺序匹配实验

用户补充：图片有明显拍摄顺序。因此先测试 COLMAP `sequential_matcher`，期望减少特征匹配时间。

| Run | Matcher | Overlap | 结果 | 结论 |
| --- | --- | ---: | --- | --- |
| `experiment-seq-default-v2` | `sequential` | 12 | Densify 日志仅显示 12 poses、12 calibrated images、约 1462 sparse points | 质量风险过高，停止 |
| `experiment-seq-overlap40` | `sequential` | 40 | Densify 日志仅显示 16 poses、16 calibrated images、约 2582 sparse points | 质量风险过高，停止 |

诊断：

- `experiment-seq-overlap40` 只生成 183 个 matched pairs，其中 117 个有 verified inliers。
- `experiment-sorted-e2e-20260803-141308` 的 exhaustive matcher 生成 820 个 matched pairs，其中 353 个有 verified inliers。
- COLMAP `sequential_matcher` 默认 `SequentialMatching.quadratic_overlap=1`，会对远邻做稀疏采样。当前图片前后位姿变化较大时，这种采样使约束图断裂，mapper 输出多个 sparse model；后续 pipeline 只使用 `sparse/0`，因此 dense 阶段只看到部分图片。

增强实验：

```bash
RUN_NAME=experiment-seq-overlap40-linear-20260804-094239 \
MVS_MATCHER=sequential \
MVS_SEQUENTIAL_OVERLAP=40 \
MVS_SEQUENTIAL_QUADRATIC_OVERLAP=0 \
MVS_GENERATE_TEXTURE=0 \
scripts/reconstruct.sh
```

结果：

| Metric | `sorted-e2e` exhaustive | `seq overlap40 linear` | Change |
| --- | ---: | ---: | ---: |
| Matched pairs | 820 | 820 | 0.00% |
| Verified pairs with inliers | 353 | 358 | +1.42% |
| Sparse points seen by OpenMVS | 7,914 | 7,807 | -1.35% |
| Calibrated images | 41 | 41 | 0.00% |
| Matcher time | 28.36s | 31.22s | +10.10% |
| Densify time | 683.60s | 617.40s | -9.68% |
| Dense points | 2,875,744 | 2,756,742 | -4.14% |
| ReconstructMesh time | 58.74s | 72.53s | +23.48% |
| Mesh faces | 3,305,464 | 3,220,794 | -2.56% |
| Texture patches, view assignment | 34,462 | 33,936 | -1.53% |
| TextureMesh manual wall time | 223.07s | 279.73s | +25.40% |
| Full textured estimate | 1080.01s | 1078.15s | -0.17% |

结论：

- 虽然图片有拍摄顺序，但当前数据集直接用 `sequential_matcher` 默认 quadratic overlap 会显著减少有效重建约束。
- `MVS_SEQUENTIAL_QUADRATIC_OVERLAP=0` 可以修复这个问题：matched pairs 回到 820，OpenMVS 重新看到 41 calibrated images。
- 该组合的完整 textured 估算耗时和 exhaustive 基本持平，不是明显提速配置。
- 默认继续使用 `exhaustive_matcher`。
- 如果图片数量明显增加，`sequential + overlap + quadratic_overlap=0` 可以作为 ordered dataset 的安全候选；若要明显提速，还需要更聪明的 pair 选择，例如分段 sequence + 少量跨段 retrieval/vocab-tree/spatial pairs，而不能直接使用默认 sequential。

### B. Densify `geometric_iters=1`

实验运行：

```bash
RUN_NAME=experiment-exhaustive-geom1 MVS_DENSIFY_GEOMETRIC_ITERS=1 scripts/reconstruct.sh
```

配置：

- `matcher=exhaustive`
- `densify_number_views=5`
- `densify_geometric_iters=1`
- `remove_depth_maps=true`
- `undistort_copy_policy=HARD_LINK`

结果对比：

| Metric | Baseline | `geom1` | Change |
| --- | ---: | ---: | ---: |
| Total time | 1294.30s | 996.85s | -22.98% |
| Densify time | 789.51s | 550.39s | -30.29% |
| ReconstructMesh time | 76.60s | 57.91s | -24.41% |
| TextureMesh time | 322.61s | 235.43s | -27.02% |
| Dense points | 2,866,094 | 2,775,825 | -3.15% |
| Mesh vertices | 1,659,137 | 1,570,887 | -5.32% |
| Mesh faces | 3,316,828 | 3,140,575 | -5.31% |
| Texture vertices | 1,659,137 | 1,570,887 | -5.32% |
| Texture faces | 3,316,828 | 3,140,575 | -5.31% |

结论：

- `geometric_iters=1` 是当前最佳默认配置：总耗时降低约 4m57s，主要瓶颈 Densify 降低约 30%。
- dense point 数量仅下降约 3.1%，mesh/texture 面数下降约 5.3%，属于可接受的轻微下降。
- 需要人工可视检查最终纹理和关键区域完整度；若对极细节几何要求更高，可把 `densify_geometric_iters` 改回 2。

已固化默认配置：

```json
"matcher": "exhaustive",
"densify_number_views": 5,
"densify_geometric_iters": 1
```

### C. TextureMesh patch packing A/B

实验目的：

- `TextureMesh` 是当前第二瓶颈。
- `--patch-packing-heuristic` 主要影响纹理 patch 放入 atlas 的启发式策略。
- 该参数不改变 mesh 顶点/面数；风险主要是纹理 atlas 紧凑度和 patch 排布，而不是几何。

实验方式：

- 使用 `outputs/experiment-exhaustive-geom1/openmvs/scene_dense.mvs` 和同一份 `scene_mesh.ply`。
- 单独运行 TextureMesh，避免重新跑 Densify/Reconstruct。
- 输出到候选文件名，避免覆盖当前最优 `scene_texture.ply`。

结果：

| Variant | `patch-packing-heuristic` | Wall Time | Texturing Time | Vertices | Faces | Textures | Texture Sizes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| control | 3 | 239.90s | 3m49.80s | 1,570,887 | 3,140,575 | 2 | 58 MB + 19 MB |
| pack100 | 100 | 187.11s | 2m57.06s | 1,570,887 | 3,140,575 | 2 | 58 MB + 19 MB |

结论：

- `patch-packing-heuristic=100` 在同条件控制组下让 TextureMesh wall time 降低约 22%。
- 几何指标和纹理文件数量/体积保持一致。
- 默认配置采用 `texture_patch_packing_heuristic=100`。

已固化默认配置补充：

```json
"texture_patch_packing_heuristic": 100
```

### D. 当前最优默认配置端到端复测

实验运行：

```bash
RUN_NAME=experiment-best-e2e-20260803-133517 scripts/reconstruct.sh
```

配置确认：

- Densify 日志包含 `--remove-dmaps 1 --number-views 5 --geometric-iters 1`。
- Texture 日志包含 `--patch-packing-heuristic 100`。
- OpenMVS `.dmap` 文件数量为 0。

结果：

| Stage | Time | Peak RSS | User CPU |
| --- | ---: | ---: | ---: |
| `feature_extractor` | 20.67s | 707 MB | 154.01s |
| `exhaustive_matcher` | 25.06s | 171 MB | 129.30s |
| `mapper` | 8.20s | 254 MB | 10.63s |
| `image_undistorter` | 47.80s | 2.47 GB | 382.37s |
| `interface_colmap` | 0.28s | 56 MB | 0.14s |
| `densify_point_cloud` | 654.92s | 5.36 GB | 5301.34s |
| `reconstruct_mesh` | 67.50s | 5.94 GB | 208.95s |
| `texture_mesh` | 315.63s | 9.07 GB | 1910.49s |

总耗时：

```text
1140.07s ~= 19m00s
```

OpenMVS 指标：

- dense points: 2,815,346。
- mesh vertices/faces: 1,630,815 / 3,260,210。
- texture vertices/faces: 1,630,815 / 3,260,210。
- 输出目录体积：约 993 MB。

结论：

- 当前默认配置在这轮完整复测中相对初始基线 `21m34s` 仍降低约 12%。
- 这轮 Densify 和 Texture 明显慢于 `experiment-exhaustive-geom1`，但配置已确认生效；主要差异来自运行环境波动和本轮生成的 mesh/texture 面数更高。
- `patch-packing-heuristic=100` 的单阶段 A/B 仍有效，但端到端收益会被 Densify 和 mesh 规模波动稀释。
- 后续如果要继续压端到端时间，优先做固定线程数 A/B，例如 `max_threads=8` 或 `max_threads=6`，观察是否能降低系统调度波动和 Texture/Densify 的长尾耗时。

### E. Release 重新构建与输入顺序稳定性

Release 构建确认：

```text
build/CMakeCache.txt: CMAKE_BUILD_TYPE=Release
build/third_party/colmap/CMakeCache.txt: CMAKE_BUILD_TYPE=Release
build/third_party/openmvs/CMakeCache.txt: CMAKE_BUILD_TYPE=Release
```

`scripts/build.sh` 已显式用 `-DCMAKE_BUILD_TYPE=Release` 构建主项目。COLMAP/OpenMVS 之前已经是 Release，主项目主要负责 orchestration，因此对端到端耗时影响很小，但可避免正式实验误用无优化构建。

稳定性分析：

- `experiment-exhaustive-geom1` 和 `experiment-best-e2e-20260803-133517` 中 COLMAP database 的 `image_id -> filename` 顺序不同。
- 两次 mapper 初始图像对不同：
  - `geom1`: image pair `#34` and `#36`。
  - `best-e2e`: image pair `#24` and `#35`。
- 两次最终稀疏点接近，但 ROI、dense points、mesh faces 和 texture patches 出现波动。
- Texture patches 从约 33.8k 增加到 53.6k，是最新端到端 Texture 阶段变慢的重要原因。

已完成工程修复：

- pipeline 在每个输出目录生成 `colmap/image_list.txt`。
- 文件列表按文件名排序，且只包含常见图片扩展名。
- `feature_extractor` 使用 `--image_list_path`。
- `mapper` 使用 `--Mapper.image_list_path`。

预期收益：

- 不改变原始 `./data`。
- 不降低重建质量。
- 固定 COLMAP 导入顺序，让后续 mapper 初始化、注册路径和性能实验更稳定。
- 因为图片文件名含拍摄时间，排序后的 image list 也更贴合实际拍摄顺序。

端到端复测：

```bash
RUN_NAME=experiment-sorted-e2e-20260803-141308 scripts/reconstruct.sh
```

结果：

| Metric | `best-e2e` | `sorted-e2e` | Change |
| --- | ---: | ---: | ---: |
| Total time | 1140.06s | 1080.01s | -5.27% |
| Densify time | 654.92s | 683.60s | +4.38% |
| ReconstructMesh time | 67.50s | 58.74s | -12.98% |
| TextureMesh time | 315.63s | 223.07s | -29.33% |
| Dense points | 2,815,346 | 2,875,744 | +2.15% |
| Mesh faces | 3,260,210 | 3,305,464 | +1.39% |
| Texture patches, view assignment | 53,601 | 34,462 | -35.71% |

补充观察：

- `image_list.txt` 已按文件名排序，但 COLMAP database 的 `image_id` 仍有局部乱序，推测是 feature extraction 并行写入 database 导致。
- 虽然 image_id 未完全顺序化，mapper 初始图像对已经回到和 fast run 同一组相邻文件附近：`#34` and `#35`，对应第二组拍摄序列的相邻图片。
- 排序 image list 对 Texture 复杂度帮助明显，patch 数回落后 TextureMesh 降低约 92.6s。
- Densify 仍然是最大瓶颈，本轮占总耗时约 63.3%。

后续稳定性候选：

- 增加可选“确定性导入模式”：让 `feature_extractor` 单线程导入/提特征，观察 image_id 是否完全按列表稳定；风险是 COLMAP 前段会变慢，需要独立 A/B，不宜直接默认。
- 增加可选 mapper 初始化图像对配置：按 database 中的文件名解析 image ID 后再运行 mapper。工程复杂度更高，但能更强地锁定 SfM 初始化路径。

## 后续候选优化与实验

### A. Texture 阶段拆分

状态：已落地为可选 mesh-only 模式，默认仍生成纹理。

方案：

- 新增 `generate_texture` 配置和 `--generate-texture` CLI。
- 默认 `true`，完整输出 `scene_texture.ply`。
- 设置为 `false` 时 pipeline 在 `scene_mesh.ply` 后停止，可由 UI 先展示几何结果，TextureMesh 后续作为后台任务单独排队。

收益：

- 不改变最终效果。
- 缩短用户首次看到可用几何结果的等待时间。
- 当前 `sorted-e2e` 中 TextureMesh 为 223.07s，因此 mesh-only 首次结果理论上约可提前 3m43s；在 `iters=2` 实验中 TextureMesh 为 382.67s，可提前约 6m23s。

### B. 报告对比模式

当前 `scripts/report_run.py` 已能生成单个 run 的报告，也支持 baseline/candidate 对比：

```bash
python3 scripts/report_run.py outputs/baseline --compare outputs/variant
```

对比内容：

- 每阶段耗时变化百分比。
- 输出目录体积变化。
- dense point / mesh face / texture face 变化。
- 关键产物是否缺失。

注意：

- 旧基线目录 `outputs/profile-efficiency-20260803-104910` 的 manifest 曾被复用 smoke test 覆盖为 skipped，因此自动对比该目录时阶段耗时为 0。
- 该目录的 OpenMVS 点数/面数仍可从日志解析。

### C. Densify 线程数 A/B

当前最新 run 中 Densify 占总耗时约 63.3%，CPU 并行倍率约 8.2x-9.4x。下一步建议测试：

```bash
RUN_NAME=experiment-sorted-threads8 MVS_MAX_THREADS=8 scripts/reconstruct.sh
RUN_NAME=experiment-sorted-threads6 MVS_MAX_THREADS=6 scripts/reconstruct.sh
```

观察：

- Densify wall time 是否降低或波动变小。
- Texture wall time 是否受益于更少调度争用。
- 峰值内存和系统 CPU 是否下降。
- OpenMVS dense points / mesh faces 是否保持在合理波动范围内。

实验结果：

| Run | `max_threads` | Total | Densify | ReconstructMesh | TextureMesh | Dense Points | Mesh Faces | Texture Patches |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `experiment-sorted-e2e-20260803-141308` | 0/tool default | 1080.01s | 683.60s | 58.74s | 223.07s | 2,875,744 | 3,305,464 | 34,462 |
| `experiment-sorted-threads8-20260803-172116` | 8 | 1172.00s | 685.82s | 66.34s | 303.40s | 2,758,377 | 3,106,420 | 51,718 |
| `experiment-sorted-threads6-20260803-174114` | 6 | 1354.34s | 773.44s | 68.06s | 342.73s | 2,835,738 | 3,320,079 | 52,815 |

结论：

- `max_threads=8` 不改善 Densify wall time，Texture 反而因为 patch 数高而慢约 80s，总耗时增加约 8.5%。
- `max_threads=6` 明显变慢，COLMAP 前段、undistorter、Densify 和 Texture 都有明显损失，总耗时增加约 25.4%。
- 不建议把 `max_threads` 固化为 8 或 6；默认继续使用 `0`，即工具默认线程策略。
- 限制线程可作为“给 UI/系统留资源”的交互模式，但不是当前性能最优模式。

下一步方向：

- 线程数不是当前突破口。
- Densify 仍是第一瓶颈，继续优化应集中在 `resolution-level`、`max-resolution`、`number-views`、`number-views-fuse`、`iters` 等参数的质量受控 A/B。
- Texture 的大波动来自 mapper 初始化和 texture patch 数；如果要继续稳定实验，可增加 mapper 初始化图像对配置，而不是依赖随机初始化。

### D. Densify `max-resolution` A/B

为继续压缩第一瓶颈 Densify，新增配置/CLI：

- `densify_resolution_level`
- `densify_max_resolution`
- `densify_iters`
- `densify_number_views_fuse`

默认值保持 OpenMVS 当前默认或已验证默认：

```json
"densify_resolution_level": 1,
"densify_max_resolution": 2560,
"densify_iters": 3,
"densify_number_views_fuse": 2
```

实验 1：

```bash
RUN_NAME=experiment-sorted-maxres2048-20260803-181743 MVS_DENSIFY_MAX_RESOLUTION=2048 scripts/reconstruct.sh
```

结果：

| Metric | `sorted-e2e` | `maxres2048` | Change |
| --- | ---: | ---: | ---: |
| Total time | 1080.01s | 816.08s | -24.44% |
| Densify time | 683.60s | 444.46s | -34.98% |
| ReconstructMesh time | 58.74s | 47.52s | -19.10% |
| TextureMesh time | 223.07s | 218.97s | -1.84% |
| Dense points | 2,875,744 | 1,964,480 | -31.69% |
| Mesh faces | 3,305,464 | 2,229,972 | -32.54% |
| Texture patches, view assignment | 34,462 | 23,002 | -33.25% |

实验 2：

```bash
RUN_NAME=experiment-sorted-maxres2304-20260803-183153 MVS_DENSIFY_MAX_RESOLUTION=2304 scripts/reconstruct.sh
```

结果：

| Metric | `sorted-e2e` | `maxres2304` | Change |
| --- | ---: | ---: | ---: |
| Total time | 1080.01s | 1160.83s | +7.48% |
| Densify time | 683.60s | 621.15s | -9.14% |
| ReconstructMesh time | 58.74s | 64.32s | +9.50% |
| TextureMesh time | 223.07s | 286.90s | +28.62% |
| Dense points | 2,875,744 | 2,350,115 | -18.28% |
| Mesh faces | 3,305,464 | 2,689,663 | -18.63% |
| Texture patches, view assignment | 34,462 | 26,925 | -21.87% |

结论：

- `max-resolution=2048` 是有效速度档，总耗时约 13m36s，但 dense/mesh 指标下降约 32%，不符合“不损失效果”的默认目标。
- `max-resolution=2304` 只让 Densify 快约 9%，但本轮总耗时受前段/Texture 波动反而变慢；几何指标仍下降约 18%，也不宜默认采纳。
- 默认继续保持 `densify_max_resolution=2560`。
- 如果用户愿意牺牲几何密度换速度，可以把 `MVS_DENSIFY_MAX_RESOLUTION=2048` 作为显式 speed preset 使用。

### E. Densify `iters=2` A/B

实验目的：

- 在不降低 Densify 输入分辨率的前提下，减少 PatchMatch 迭代次数。
- 相比 `max-resolution=2048/2304`，这个参数预期质量代价更温和。

实验运行：

```bash
RUN_NAME=experiment-sorted-iters2-20260803-185352 MVS_DENSIFY_ITERS=2 scripts/reconstruct.sh
```

结果：

| Metric | `sorted-e2e` | `iters2` | Change |
| --- | ---: | ---: | ---: |
| Total time | 1080.01s | 1150.10s | +6.49% |
| Densify time | 683.60s | 558.80s | -18.26% |
| Densify peak RSS | 5.59 GB | 4.89 GB | -12.53% |
| ReconstructMesh time | 58.74s | 74.16s | +26.26% |
| TextureMesh time | 223.07s | 382.67s | +71.55% |
| Dense points | 2,875,744 | 2,806,110 | -2.42% |
| Mesh faces | 3,305,464 | 3,246,896 | -1.77% |
| Texture patches, view assignment | 34,462 | 52,655 | +52.79% |

结论：

- `densify_iters=2` 对 Densify 本身有效：wall time 降低约 125s，峰值内存降低约 700 MB。
- 几何指标下降很小，dense points 约 -2.4%，mesh faces 约 -1.8%。
- 但本轮 TextureMesh patch 数从 34,462 增至 52,655，TextureMesh 增加约 160s，总耗时反而比 `sorted-e2e` 慢约 70s。
- 不建议把 `densify_iters=2` 固化为默认。它可以作为“只需要 dense point cloud、mesh，不急需 texture”的候选模式；对完整 textured mesh 端到端不是最优配置。

### F. Hybrid ordered pair matching

实验目的：

- 利用图片拍摄顺序减少 COLMAP 匹配 pair 数。
- 避免默认 `sequential_matcher` 因 quadratic overlap 稀疏采样导致匹配图断裂。
- 保留少量跨段 bridge pair，使 mapper 尽量输出一个主 sparse model。

实验 run：

```text
outputs/experiment-hybrid-local12-bridges-20260804-101100
```

#### F.1 `local12 + bridges`

pair 生成策略：

- 按文件名排序。
- 每张图匹配后续 12 张局部邻居。
- 每隔 8 张加入少量粗桥接 pair。
- 在 `IMG_20260730_110018.jpg` 到 `IMG_20260730_151015.jpg` 的时间跳变处加入左右各 6 张的交叉桥接。

结果：

| Metric | Value |
| --- | ---: |
| Pair list size | 424 |
| Matched pairs in database | 424 |
| Verified pairs with inliers | 248 |
| Total inliers | 46,923 |
| Mapper output | 2 sparse models |

结论：

- pair 数从 820 降到 424，匹配耗时明显下降。
- 但 mapper 仍断成多个 sparse model，不满足 dense 阶段完整重建要求。
- `local12` 对当前“大位姿变化 + 两段拍摄”的数据仍偏窄。

#### F.2 `local16 + bridges`

pair 生成策略：

- 每张图匹配后续 16 张局部邻居。
- 保留粗桥接 pair。
- 时间跳变处左右各 8 张做交叉桥接。

结果：

| Metric | `sorted-e2e` exhaustive | `hybrid local16` | Change |
| --- | ---: | ---: | ---: |
| Pair list size | 820 | 526 | -35.85% |
| Verified pairs with inliers | 353 | 298 | -15.58% |
| Total inliers | 62,223 | 60,637 | -2.55% |
| Main sparse model images | 41 | 40 | -1 image |
| Main sparse points | 7,914 | 7,750 | -2.07% |
| Feature extraction | 23.63s | 23.26s | -1.57% |
| Matching/import total | 28.36s | 17.08s | -39.77% |
| Mapper | 9.26s | 13.73s | +48.27% |
| Image undistorter | 53.02s | 39.11s | -26.23% |
| Densify | 683.60s | 579.16s | -15.28% |
| Dense points | 2,875,744 | 2,832,728 | -1.50% |
| ReconstructMesh | 58.74s | 62.89s | +7.07% |
| Mesh faces | 3,305,464 | 3,480,418 | +5.29% |
| Texture patches, view assignment | 34,462 | 55,555 | +61.21% |
| TextureMesh | 223.07s | 288.10s | +29.15% |
| Full textured estimate | 1080.01s | 1023.64s | -5.22% |

补充实验：

- 针对缺失的 `IMG_20260730_110010.jpg` 增加 40 个全局桥接 pair。
- 数据库 matched pairs 从 526 增至 537，但 verified pairs 仍为 298，total inliers 不变。
- mapper 主模型仍为 40 张图，因此该补桥无效。

结论：

- `hybrid local16` 是目前第一个真正利用拍摄顺序取得端到端小幅收益的方案：完整 textured 估算约快 56s。
- 几何指标表现不错：dense points 仅 -1.5%，mesh faces 反而 +5.3%。
- 但 Texture patch 数增加 61%，TextureMesh 慢约 65s，吃掉了 Densify 的大部分收益。
- 主模型少 1 张图，虽影响不大，但不符合默认配置应有的保守性。
- 暂不建议固化为默认；可以作为后续 `matcher=hybrid` 的候选策略继续优化，重点是减少 Texture patch 反弹并补回缺失图片。

后续候选：

- `local18` 或 `local20`：可能补回 41 张，但 pair 数仍低于 820，需要看 Texture patch 是否回落。
- 用 COLMAP 数据库的 verified inliers 做二阶段补桥：先 local16，再只为断点/低连接图片补充候选 pair。
- 若有图像检索或位姿先验，可用 retrieval/spatial bridge 替代手工 stride bridge。

#### F.3 `local20 + bridges`

实验 run：

```text
outputs/experiment-hybrid-local20-bridges-20260804-103543
```

pair 生成策略：

- 每张图匹配后续 20 张局部邻居。
- 保留 coarse stride bridge。
- 时间跳变处保留左右各 8 张交叉桥接。

结果：

| Metric | `hybrid local16` | `hybrid local20` | Change |
| --- | ---: | ---: | ---: |
| Pair list size | 526 | 616 | +17.11% |
| Matched pairs in database | 526 | 616 | +17.11% |
| Verified pairs with inliers | 298 | 324 | +8.72% |
| Total inliers | 60,637 | 61,142 | +0.83% |
| Feature extraction | 23.26s | 19.97s | -14.14% |
| Matching/import | 17.08s | 18.88s | +10.54% |
| Mapper | 13.73s | 14.90s | +8.52% |
| Best sparse model images | 40 | 37 | -3 images |
| Best sparse model points | 7,750 | 7,267 | -6.23% |

结论：

- `local20` 的 verified pairs 比 `local16` 多，但 mapper 主模型反而从 40 张降到 37 张。
- 原因推断：更多 pair 改变了 COLMAP 增量 mapper 的初始化和多模型竞争路径，不保证窗口越大越好。
- 因为 sparse 注册数明显退化，未继续跑 Densify/mesh/texture。
- 当前 hybrid 最优候选仍是 `local16 + bridges`，而不是 `local20`。
