# MVS 重建管线优化总结

## 一、GPU 使用现状

### 当前 GPU 加速情况

| 工具 | 阶段 | GPU 状态 | 说明 |
|------|------|----------|------|
| COLMAP | 特征提取 | ✅ 已启用 | `--FeatureExtraction.use_gpu=1` 默认 |
| COLMAP | 特征匹配 | ✅ 已启用 | `--FeatureMatching.use_gpu=1` 默认 |
| COLMAP | mapper BA | ❌ 未启用 | 40 张图 < 50 张阈值，自动回退 CPU |
| COLMAP | 图像去畸变 | ⚪ CPU | 可切换 OpenCV 后端加速，见方案 B |
| OpenMVS | DensifyPointCloud | ✅ 已启用 | CUDA device 自动初始化 |
| OpenMVS | ReconstructMesh | ✅ 已启用 | CUDA device 自动初始化 |
| OpenMVS | TextureMesh | ✅ 已启用 | CUDA device 自动初始化，但 CPU 占主导 |

### 资源瓶颈分析（12 线程，41 张图，`use_opencv_undistort=true`）

```
阶段                  墙上时间    CPU 并行度    瓶颈类型        占比
─────────────────────────────────────────────────────────────────
feature_extractor      ~47s         9.1x      CPU 饱和        7.1%
exhaustive_matcher     ~15s        11.0x      CPU 饱和        2.3%
mapper                 ~42s         1.7x      CPU 单线程      6.4%
image_undistorter       5.1s        未统计     CPU 并行(OpenCV) 0.8%
densify_point_cloud   ~130s         1.3x      GPU 为主       19.7%
reconstruct_mesh      ~128s         2.7x      GPU+CPU 混合   19.4%
texture_mesh          ~275s         7.4x      CPU 为主       41.7% ⚠️
─────────────────────────────────────────────────────────────────
总计                   660s                                  100%
```

> **注：** `feature_extractor` 单次运行有 ±15s 噪声（调度抖动），以上为今日实测值。

**关键发现：**
- **TextureMesh 占 42% 耗时，但主要是 CPU bound**（face assignment + atlas packing）
- GPU 只在 DensifyPointCloud 阶段真正成为瓶颈（1.3x = 在等 GPU）
- `image_undistorter` 开 OpenCV 后端后从 73.6s 降到 ~5s
- 其他阶段要么 CPU 饱和（9-11x），要么是全局算法无法并行

---

## 二、多 GPU 测试结果

### 单任务 vs 多任务

**单任务无法用多 GPU：**
- OpenMVS 每个工具进程只初始化一块 GPU
- 管线各阶段严格串行（前一阶段产物是后一阶段输入）
- 验证：`CUDA_VISIBLE_DEVICES=0,1` 暴露两块卡，DensifyPointCloud 仍只用 device 0

**多任务可以吞吐翻倍：**
| 场景 | 单任务耗时 | 吞吐量（2 GPU） | 加速比 |
|------|-----------|----------------|--------|
| 串行 | ~672s | ~1344s / 2 jobs | 1.0x |
| 并发（GPU 0/1） | ~690s / ~700s | ~700s / 2 jobs | **≈1.92x** |

> 该结论来自 2026-08-06 的 2-GPU 测试（当时单任务 713.9s / 并发 734.5s、743.8s，1.92x），
> 上表按当前单任务耗时等比换算，未重测。

**使用方法：**
```bash
# 4 块 RTX 6000 同时处理 4 个数据集
for i in 0 1 2 3; do
  mvs_reconstruct --cuda-device $i --output outputs/dataset$i \
    --images data/dataset$i/images \
    --cameras data/dataset$i/cameras.json &
done
wait
```

**并发开销（参考 2026-08-06 测试）：**
- 单任务耗时增加约 2-3%（CPU/内存带宽争抢）
- DensifyPointCloud 阶段干扰最大，因为内存随机访问密集
- 总体仍接近线性加速（≈1.94x vs 理论 2.0x）

---

## 三、速度优化方案

### 方案 A：参数调优（已实施）

**配置文件：** [config/reconstruction-fast.json](config/reconstruction-fast.json)

**参数变化（相对默认 reconstruction.json）：**
```json
{
  "densify_number_views": 4,              // 5 → 4
  "densify_max_resolution": 2048,         // 2560 → 2048
  "densify_iters": 2,                     // 3 → 2
  "texture_patch_packing_heuristic": 100  // 3 → 100（保持默认）
}
```

**实测效果（2026-08-07，12 线程，device 0）：**

> 这组数据在旧 streaming 去畸变路径下测的（`image_undistorter` 4.7s）。参数调优本身的
> 结论不受影响——差异全在 densify/mesh/texture 三个阶段。换成 COLMAP 后端时两列的
> `image_undistorter` 都会变成 ~74s，总计各加约 69s。

| 阶段 | 默认配置 | 快速配置 | 加速比 |
|------|----------|----------|--------|
| feature_extractor | 59.0s | 45.8s | 1.29x* |
| exhaustive_matcher | 14.6s | 14.7s | 1.0x |
| mapper | 42.5s | 41.7s | 1.0x |
| image_undistorter | 4.7s | 4.8s | 1.0x |
| DensifyPointCloud | 136.2s | 77.8s | 1.75x |
| ReconstructMesh | 127.9s | 93.7s | 1.37x |
| TextureMesh | 275.4s | 205.0s | 1.34x |
| **总计** | **660.4s** | **483.4s** | **1.37x** |

> \* feature_extractor 的差异是单次运行噪声（CPU 时间 428s vs 429s 基本一致），不是配置差异。

**质量对比：**
| 指标 | 默认配置 | 快速配置 | 变化 |
|------|----------|----------|------|
| 网格面数 | 3,023,796 | 2,144,611 | -29.1% |
| 纹理贴图数 | 2 张 8192×8192 | 2 张 8192×8192 | 0% |
| 纹理文件总量 | 84 MB | 81 MB | -3.6% |

**结论：** 速度提升 37%，纹理分辨率/贴图数量不变，网格密度减少 29%（对车载远近景影响小），适合快速迭代。

### 方案 B：OpenCV 去畸变后端（已实施，默认关闭）

`use_opencv_undistort: true` 把 `image_undistorter` 阶段的实现从 COLMAP 子进程换成进程内 OpenCV，**管线结构不变**：仍写 `colmap/dense/{images,sparse}`，之后 `InterfaceCOLMAP` 照常执行。

> 历史说明：早期版本用 `use_streaming_undistort` 同时替换 undistort + InterfaceCOLMAP 两个阶段，
> 粒度太粗（无法单独对比去畸变实现），且输出保留原始尺寸和四角黑边。该开关已移除。

**效果：**
| 路径 | undistort + interface 耗时 | 总耗时 |
|------|---------------------------|--------|
| COLMAP 后端（默认） | 74.0s | 736.8s |
| OpenCV 后端 | **5.1s** | **672.1s** |
| **节省** | **-68.9s (-93%)** | **-64.7s (-8.8%)** |

同一份 41 张图数据集、12 线程、默认配置的完整对照（2026-08-07 实测）：

| 阶段 | COLMAP 后端 | OpenCV 后端 |
|------|------------:|------------:|
| feature_extractor | 45.4s | 45.8s |
| exhaustive_matcher | 14.3s | 14.6s |
| mapper | 43.2s | 45.1s |
| **image_undistorter** | **74.0s** | **5.1s** |
| interface_colmap | 0.1s | 0.1s |
| densify_point_cloud | 132.8s | 133.7s |
| reconstruct_mesh | 127.7s | 132.8s |
| texture_mesh | 299.3s | 294.9s |
| **总计** | **736.8s** | **672.1s** |

除 `image_undistorter` 外各阶段差异都在噪声内。重建产物规模相当：`scene_dense.ply` 178 vs 181 MB，`scene_mesh.ply` 55 vs 54 MB，`scene_texture.ply` 140 vs 138 MB。

> `interface_colmap`（0.1s）在两个后端下都照常执行，不再像早期 streaming 实现那样被跳过。
> 去畸变图像总量 455 MB（COLMAP，近无损）vs 202 MB（OpenCV，Q95）——这是质量差异，不影响速度（见下）。

**实现原理（[OpenCvUndistorter.cpp](../src/cpp/pipeline/OpenCvUndistorter.cpp)）：**

几何**完全复用 COLMAP 自己的实现**，只把逐像素重采样内核换掉：

1. `colmap::UndistortCamera(camOptions, camera)` 算裁剪后的 PINHOLE 相机（width/height/fx/fy/cx/cy）。这是 COLMAP 逐边界像素追踪的结果，不是近似；每个 camera_id 只算一次并缓存
2. `cv::undistort(src, dst, K, distCoeffs, newK)` 做重采样，`newK` 就是上一步的内参（其 cx/cy 已含裁剪偏移）
3. `colmap::UndistortReconstruction(camOptions, &recon)` + `recon.Write()` 写 `dense/sparse/*.bin`

关键约束：第 1 步和第 3 步必须传**同一份** `UndistortCameraOptions`，否则图像几何与 sparse model 不一致、位姿对不上。代码里用同一个 `const colmap::UndistortCameraOptions camOptions{}` 传给两处。

这样给定**相同的输入 sparse model** 时，两个后端的图像尺寸和 `dense/sparse` 内参完全相同，唯一差异是重采样实现和 JPEG 编码器。

> ⚠️ 对比几何时必须用同一次 mapper 产物（`--reuse-existing true`）。COLMAP mapper 是
> 非确定性的：两次独立运行的 f 会差千分之一量级（实测 4436.726 vs 4435.404），
> `UndistortCamera` 据此算出的裁剪尺寸就会差几个像素（5997×4497 vs 5994×4495）。
> 这与后端无关。[tests/cpp/opencv_undistort_test.cpp](../tests/cpp/opencv_undistort_test.cpp)
> 通过对同一 sparse model 断言"写出的内参 == `UndistortCamera()` 的结果、且图像尺寸与之一致"
> 来钉住这个不变量（容差 1e-9），实测通过。

`InterfaceCOLMAP` 只需要 `dense/images/` + `dense/sparse/*.bin`——`stereo/` 相关读取整段被 `File::isFolder()` 守卫（[InterfaceCOLMAP.cpp:878](../3rd/openMVS-2.4.0/apps/InterfaceCOLMAP/InterfaceCOLMAP.cpp#L878)），只用于转换已有的 COLMAP 深度图，DensifyPointCloud 从零跑时不会进。所以本后端不写 `stereo/`、`patch-match.cfg`、`fusion.cfg`。

**加速来源：不是 JPEG 质量，也不是并行化（2026-08-07 实测）**

OpenCV 后端默认写 JPEG quality=95，COLMAP `image_undistorter` 默认 `--jpeg_quality -1`（近无损）。为隔离"降低存储质量"这一变量，给 COLMAP 后端显式加 `--jpeg_quality 95` 重跑：

| 配置 | undistort 墙上 | undistort CPU | 并行度 | 写入图像 | 总耗时 |
|------|---------------:|--------------:|-------:|---------:|-------:|
| 旧路径 默认质量 | 73.6s | 745.4s | 10.1x | 457 MB | 713.9s |
| 旧路径 `--jpeg_quality 95` | **73.9s** | 745.1s | 10.1x | **202 MB** | 721.2s |
| OpenCV 后端 Q95 | **~5s** | 未统计（≤60s） | 12 线程 | 203 MB | — |

**结论：降低 JPEG 质量对速度没有贡献。** 写入量砍掉 55%（457→202 MB，与 OpenCV 后端的 203 MB 基本相同），墙上时间 73.6s→73.9s、CPU 时间 745.4s→745.1s，全部落在噪声内。**这个阶段不是 I/O bound。**

**并行化也不是主因：** COLMAP `image_undistorter` 本身已经跑到 10.1x 并行度（745s CPU / 73.6s 墙上，12 线程），并没有"单线程"的问题。

真正的差异是 **CPU 工作量**：COLMAP 花 745s CPU，OpenCV 后端上限 60s（~5s × 12 线程），少做至少 12 倍的工作。从代码能确认的机制差异：

- `UndistortCamera()` 返回 **PINHOLE** 模型（[undistortion.cc:79](../3rd/colmap-4.1.1/src/colmap/image/undistortion.cc#L79)），所以 [warp.cc:77](../3rd/colmap-4.1.1/src/colmap/image/warp.cc#L77) 的重采样循环里 `CamFromImg` 走闭式解，**那条 100 次 Newton 迭代的 `IterativeUndistortion` 路径不会被触发**——开销不在迭代反解上
- COLMAP 的循环是逐像素标量代码：每个像素构造 `Eigen::Vector2d`、两次返回 `std::optional` 的相机模型调用（经 model_id 分派）、一次 `InterpolateBilinear()` 带边界检查
- COLMAP 还在源分辨率下 warp 完再 `Rescale()` 一遍以抗锯齿（[warp.cc:68-74](../3rd/colmap-4.1.1/src/colmap/image/warp.cc#L68-L74)），多一趟全图重采样
- `cv::undistort()` 走 `initUndistortRectifyMap` + `cv::remap` 的查表路径，定点插值、SIMD 向量化、内部多线程

> 最后一条是根据 OpenCV 实现的推断，未在本机 profile 验证；前三条可直接从上面引用的代码行读到。

**黑边问题（已随本后端修复）：**

早期的 `use_streaming_undistort` 进出用同一个 K、保持原始尺寸，四角留下大量无效黑像素并喂给 DensifyPointCloud：

| 实现 | 输出分辨率 | 近黑像素占比 | 四角 40×40 内近黑 |
|------|-----------|-------------|------------------|
| COLMAP 后端（默认质量） | 5998×4498 | 0.0036% | 0 / 6400 |
| COLMAP 后端（`--jpeg_quality 95`） | 5996×4496 | 0.0030% | 0 / 6400 |
| **OpenCV 后端（当前）** | 5994×4495 | **0.0022%** | **0 / 6400** |
| 旧 streaming 实现（已移除） | 6016×4512（原始尺寸） | 0.4158% | 2438 / 6400 |

现在的 OpenCV 后端复用 `colmap::UndistortCamera()`，裁剪行为与 COLMAP 一致（`blank_pixels=0`）。实测两个后端四角近黑均为 **0 / 6400**，全图近黑 0.0025% vs 0.0022%——黑边问题已消除。

> 上表 COLMAP 后端两行的分辨率差 2px（5998 vs 5996）是 COLMAP mapper 非确定性导致内参微小不同、进而裁剪框不同，与 JPEG 质量无关。对比几何时用 `--reuse-existing true` 复用同一次 mapper 产物可消除。

**Q95 是否影响重建质量：** 未直接验证。今日两次跑的网格面数（302 万 vs 旧 baseline 301 万）差异在噪声内，但那两次的去畸变路径不同（黑边 + 尺寸都不同），不足以单独归因到 JPEG 质量。要干净地测这一项，应固定路径只改质量参数。

---

## 四、使用建议

### 场景 1：快速迭代和预览
```bash
docker-compose run --rm mvs build/mvs_reconstruct \
  --config /workspace/config/reconstruction-fast.json \
  --images /workspace/data/images \
  --cameras /workspace/data/cameras.json \
  --colmap /workspace/build/third_party/colmap/src/colmap/exe/colmap \
  --openmvs-bin /workspace/build/third_party/openmvs/bin \
  --output /workspace/outputs/preview \
  --cuda-device 0 \
  --use-opencv-undistort true
```
- 耗时：**~8 分钟**（41 张图，12 线程，开 OpenCV 后端）；用默认 COLMAP 后端约 9 分钟
- 质量：网格面数 ~214 万，适合预览和算法调试

### 场景 2：最终交付质量
```bash
docker-compose run --rm mvs build/mvs_reconstruct \
  --config /workspace/config/reconstruction.json \
  --images /workspace/data/images \
  --cameras /workspace/data/cameras.json \
  --colmap /workspace/build/third_party/colmap/src/colmap/exe/colmap \
  --openmvs-bin /workspace/build/third_party/openmvs/bin \
  --output /workspace/outputs/final \
  --cuda-device 0 \
  --use-opencv-undistort true
```
- 耗时：**11.2 分钟**（672.1s，实测）；用默认 COLMAP 后端 12.3 分钟（736.8s）
- 质量：网格面数 ~302 万，纹理 2 张 8192×8192

### 场景 3：批量处理（多 GPU）
```bash
# 4 个数据集并发，绑定不同 GPU
for i in {0..3}; do
  docker-compose run --rm mvs build/mvs_reconstruct \
    --config /workspace/config/reconstruction-fast.json \
    --images /workspace/data/batch$i/images \
    --cameras /workspace/data/batch$i/cameras.json \
    --colmap /workspace/build/third_party/colmap/src/colmap/exe/colmap \
    --openmvs-bin /workspace/build/third_party/openmvs/bin \
    --output /workspace/outputs/batch$i \
    --cuda-device $i \
    --use-opencv-undistort true &
done
wait
```
- 吞吐：~4 个/10 分钟（4 块 GPU）
- 单卡吞吐：~7 个/小时（快速配置）

> **注意：** config 中的 `colmap`/`openmvs_bin` 是相对路径，直接运行时会因 chdir 失败报 exit 127。
> 命令行始终用 `/workspace/build/...` 绝对路径覆盖，如上所示。

### 对照测试用的两个开关

2026-08-07 新增，用于隔离变量做对比测试：

| CLI 参数 | config 字段 | 默认 | 说明 |
|----------|------------|------|------|
| `--use-opencv-undistort` | `use_opencv_undistort` | `false` | `true` 用进程内 OpenCV 后端取代 COLMAP `image_undistorter` 子进程。几何一致，`InterfaceCOLMAP` 照常执行 |
| `--undistort-jpeg-quality` | `undistort_jpeg_quality` | `-1` | 去畸变图像的 JPEG 质量，**两个后端都生效**。`-1` = 沿用后端默认（COLMAP 近无损；OpenCV 95）；否则 1-100 |

```bash
# OpenCV 后端
docker-compose run --rm mvs build/mvs_reconstruct \
  --config /workspace/config/reconstruction.json \
  --colmap /workspace/build/third_party/colmap/src/colmap/exe/colmap \
  --openmvs-bin /workspace/build/third_party/openmvs/bin \
  --output /workspace/outputs/be-opencv \
  --use-opencv-undistort true

# COLMAP 后端 + 指定 JPEG 质量（隔离质量变量的对照）
docker-compose run --rm mvs build/mvs_reconstruct \
  ... --output /workspace/outputs/be-colmap-q95 \
  --undistort-jpeg-quality 95
```

对比两次运行的产物：

```bash
scripts/compare_undistort_backends.py \
  /data/taoguo/mvs-workspace/outputs/be-colmap \
  /data/taoguo/mvs-workspace/outputs/be-opencv
```

该脚本核对内参几何是否逐位一致、图像尺寸、四角黑边、各阶段耗时，以及 `interface_colmap` 是否真的执行过（不是被跳过）。只依赖标准库 + PIL。

> **踩坑记录：** COLMAP 4.1.1 的参数名是 `--jpeg_quality`，不是 `--image_quality`（后者会报
> `unrecognised option`）。另外 `parseArgs()` 里没有 handler 的 CLI 参数会被
> [Config.cpp 的 `collectOptions()`](../src/cpp/pipeline/Config.cpp) 收进 map 后**静默忽略**，
> 不报未知参数错误。早期的 `--use-streaming-undistort` 就属于这种情况，
> 传了等于没传——写对照测试时务必先确认参数真的生效（查 `logs/image_undistorter.log`
> 的 `command:` 行，或看产物落在 `colmap/dense/images/` 还是 `images/`）。

---

## 五、进一步优化方向

### 短期
1. **提高 `max_threads`**（当前 12，机器 64 核）：
   `texture_mesh` 占 42% 耗时且是 CPU bound（2026s CPU / 275s 墙上 = 7.4x，说明吃满了 12 线程配额但远没吃满机器）。这是目前最省事的收益点。

2. **OpenCV 后端的 JPEG 质量**（低优先级）：
   默认 95。实测 JPEG 质量对速度无影响（见方案 B），所以这纯粹是质量取舍，代价接近零——想要近无损直接传 `--undistort-jpeg-quality 100`。

2. **更激进的速度配置**（如果能接受 ~-40% 网格密度）：
   ```json
   {
     "densify_max_resolution": 1600,
     "densify_number_views": 3,
     "densify_iters": 1
   }
   ```
   预计快速配置 483s → ~350s，质量损失 15-20%

3. **增加 max_threads**（当前 12，机器有 64 核）：
   TextureMesh 7.4x 并行度 = 12 线程已满配额，提到 32/48 应进一步压缩 TextureMesh 耗时（CPU bound 阶段）

### 中期（算法级）
1. **TextureMesh 使用 GPU 加速的 packing 算法**（需改 OpenMVS 源码）
2. **Mapper 启用 GPU BA**（需 ≥50 张图数据集）

### 长期（架构级）
1. **流式管线重构**：让各阶段能增量输入/输出
2. **分布式处理**：大数据集（>500 张图）分块处理后合并
3. **神经网络加速**：用 NeRF/Gaussian Splatting 替代传统 MVS

---

## 六、相关文件

- **默认配置（质量优先）：** [config/reconstruction.json](../config/reconstruction.json)
- **快速配置（速度优先）：** [config/reconstruction-fast.json](../config/reconstruction-fast.json)
- **OpenCV 去畸变后端：** [src/cpp/pipeline/OpenCvUndistorter.cpp](../src/cpp/pipeline/OpenCvUndistorter.cpp)
- **后端对照脚本：** [scripts/compare_undistort_backends.py](../scripts/compare_undistort_backends.py)
- **旧 baseline 产物（旧路径 + 默认质量, 713.9s）：** `outputs/baseline-12t-20260806-111619/`
- **后端对照 — COLMAP（736.8s）：** `outputs/be-colmap/`
- **后端对照 — OpenCV（672.1s）：** `outputs/be-opencv/`
- **JPEG 质量隔离测试（721.2s）：** `outputs/oldpath-colmap-q95-final/`
- **旧 streaming 实现的产物（已废弃，660.4s / 483.4s）：** `outputs/bl-default-20260807/`、`outputs/bl-fast-20260807/`
- **2-GPU 测试产物：** `outputs/gpu2-dev0-135357/`, `outputs/gpu2-dev1-135357/`

---

## 七、FAQ

**Q: 为什么单任务用不了多 GPU？**
A: OpenMVS 每个工具进程只初始化一块卡，且管线各阶段严格串行。要让单次重建用多卡，需要改 OpenMVS 源码把深度图估计分片到多卡。

**Q: 为什么 TextureMesh 这么慢但 GPU 利用率低？**
A: TextureMesh 主要是 CPU 算法（face-to-view assignment 和 bin packing），GPU 只用在纹理投影的小部分。当前 7.4x 并行度说明 12 线程已吃满配额，但机器有 64 核，提高 `max_threads` 应有收益。

**Q: mapper 的 GPU BA 能加速多少？**
A: 对 40 张图数据集无效（< 50 张阈值会回退 CPU）。即使能用，mapper 只占 ~6% 总耗时，不是瓶颈。

**Q: OpenCV 后端的加速是因为存储的图片质量降低了吗？**
A: 不是。给 COLMAP 后端显式加 `--jpeg_quality 95` 后，写入量从 457 MB 降到 202 MB（与 OpenCV 后端相当），但 undistort 墙上时间 73.6s→73.9s、CPU 时间 745.4s→745.1s，全在噪声内。这个阶段不是 I/O bound。加速来自 CPU 工作量差异（745s vs ≤60s），详见方案 B。

**Q: 两个后端的输出能直接对比吗？**
A: 能，这是设计目标。OpenCV 后端复用 `colmap::UndistortCamera()` 算几何，所以图像尺寸和 `dense/sparse` 内参与 COLMAP 后端逐位相同，唯一差异是重采样内核和 JPEG 编码器。用 [scripts/compare_undistort_backends.py](../scripts/compare_undistort_backends.py) 可以核对。

**Q: JPEG Q95 会影响重建质量吗？**
A: 现在可以干净地测了——固定 `--use-opencv-undistort true`，只改 `--undistort-jpeg-quality`（95 vs 100），其余变量全部相同。之前无法干净归因是因为当时两条路径的图像尺寸和黑边都不同。

**Q: 快速配置的网格面数减少 29%，会影响视觉效果吗？**
A: 对车载场景影响小，因为纹理分辨率保持 8192×8192 不变，2048 深度图分辨率对远景中景已足够。除非有近距离特写需求，否则快速配置适合日常迭代。
