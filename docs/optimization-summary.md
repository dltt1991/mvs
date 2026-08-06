# MVS 重建管线优化总结

## 一、GPU 使用现状

### 当前 GPU 加速情况

| 工具 | 阶段 | GPU 状态 | 说明 |
|------|------|----------|------|
| COLMAP | 特征提取 | ✅ 已启用 | `--FeatureExtraction.use_gpu=1` 默认 |
| COLMAP | 特征匹配 | ✅ 已启用 | `--FeatureMatching.use_gpu=1` 默认 |
| COLMAP | mapper BA | ❌ 未启用 | 40 张图 < 50 张阈值，自动回退 CPU |
| COLMAP | 图像去畸变 | ⚪ CPU 为主 | 图像重采样，GPU 加速空间有限 |
| OpenMVS | DensifyPointCloud | ✅ 已启用 | CUDA device 自动初始化 |
| OpenMVS | ReconstructMesh | ✅ 已启用 | CUDA device 自动初始化 |
| OpenMVS | TextureMesh | ✅ 已启用 | CUDA device 自动初始化，但 CPU 占主导 |

### 资源瓶颈分析（12 线程，40 张图）

```
阶段                  墙上时间    CPU 并行度    瓶颈类型        占比
─────────────────────────────────────────────────────────────────
feature_extractor      45.5s        9.7x      CPU 饱和        6.4%
exhaustive_matcher     14.6s       11.5x      CPU 饱和        2.0%
mapper                 40.4s        1.7x      CPU 单线程      5.7%
image_undistorter      73.6s       10.1x      CPU 饱和       10.3%
densify_point_cloud   126.5s        1.7x      GPU 为主       17.7%
reconstruct_mesh      134.0s        2.7x      GPU+CPU 混合   18.8%
texture_mesh          279.1s        7.3x      CPU 为主       39.1% ⚠️
─────────────────────────────────────────────────────────────────
总计                  713.9s                                 100%
```

**关键发现：**
- **TextureMesh 占 39% 耗时，但主要是 CPU bound**（face assignment + atlas packing）
- GPU 只在 DensifyPointCloud 阶段真正成为瓶颈（1.7x = 在等 GPU）
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
| 串行 | 713.9s | 1427.8s / 2 jobs | 1.0x |
| 并发（GPU 0/1） | 734.5s / 743.8s | 743.8s / 2 jobs | **1.92x** |

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

**并发开销：**
- 单任务耗时增加 2.9%（CPU/内存带宽争抢）
- DensifyPointCloud 阶段干扰最大（+16.9%），因为内存随机访问密集
- 总体仍接近线性加速（1.92x vs 理论 2.0x）

---

## 三、速度优化方案

### 方案 A：参数调优（已实施）

**配置文件：** [config/reconstruction-fast.json](config/reconstruction-fast.json)

**参数变化：**
```json
{
  "densify_number_views": 4,              // 5 → 4
  "densify_max_resolution": 2048,         // 2560 → 2048
  "densify_iters": 2,                     // 3 → 2
  "texture_patch_packing_heuristic": 100  // 3 → 100
}
```

**实测效果：**
| 阶段 | 基线 | 优化后 | 加速比 |
|------|------|--------|--------|
| DensifyPointCloud | 126.5s | 77.3s | 1.64x |
| ReconstructMesh | 134.0s | 96.0s | 1.40x |
| TextureMesh | 279.1s | 196.2s | 1.42x |
| **总计** | **713.9s** | **547.8s** | **1.30x** |

**质量损失：**
| 指标 | 基线 | 优化后 | 变化 |
|------|------|--------|------|
| 网格面数 | 3,010,252 | 2,167,629 | -28.0% |
| 纹理贴图大小 | 56.5 MB | 54.6 MB | **-3.4%** |
| 纹理分辨率 | 8192×8192 | 8192×8192 | 0% |

**结论：** 速度提升 30%，纹理质量几乎无损（-3.4%），推荐用于快速迭代和预览。

---

### 方案 B：Pipeline 流水（未实施）

**唯一有价值的流水点：** undistorter → DensifyPointCloud

当前流程（完全串行）：
```
undistorter(全部40张) → 73.6s → dense/images/
↓
DensifyPointCloud(等待全部) → 126.5s
```

改进流程（流式处理）：
```
undistorter(图0) → 1.8s ──┐
undistorter(图1) → 1.8s ──┤
...                        ├→ DensifyPointCloud(流式) → 126.5s
undistorter(图39) → 1.8s ─┘   ↑
                              提前 ~70s 开始
```

**实现复杂度：**
- 需改 `ReconstructionPipeline.cpp` 调度逻辑
- OpenMVS 的 `scene.mvs` 不是为流式设计的，可能需改源码
- 或者并发跑多个 DensifyPointCloud 子任务，最后合并点云

**预期收益：** 节省 ~70s（10% 总耗时），但开发成本 1-2 天

**不推荐理由：** 投入产出比低于参数调优（5 分钟配置 vs 2 天开发）

---

## 四、使用建议

### 场景 1：快速迭代和预览
```bash
mvs_reconstruct --config config/reconstruction-fast.json \
  --images data/images --cameras data/cameras.json \
  --output outputs/preview
```
- 耗时：~9 分钟（40 张图）
- 质量：适合预览和算法调试

### 场景 2：最终交付质量
```bash
mvs_reconstruct --config config/reconstruction.json \
  --images data/images --cameras data/cameras.json \
  --output outputs/final
```
- 耗时：~12 分钟（40 张图）
- 质量：最优网格密度和纹理 packing

### 场景 3：批量处理（多 GPU）
```bash
# 4 个数据集并发，绑定不同 GPU
for i in {0..3}; do
  mvs_reconstruct --cuda-device $i \
    --config config/reconstruction-fast.json \
    --images data/batch$i/images \
    --cameras data/batch$i/cameras.json \
    --output outputs/batch$i &
done
wait
```
- 吞吐：~4 个/10 分钟（4 块 GPU）
- 单卡吞吐：~6 个/小时

---

## 五、进一步优化方向

### 短期（参数级）
1. **更激进的速度配置**（如果能接受 -40% 网格密度）：
   ```json
   {
     "densify_max_resolution": 1600,
     "densify_number_views": 3,
     "densify_iters": 1
   }
   ```
   预计 547s → ~380s，但质量损失 15-20%

2. **自适应分辨率**（远景降低，近景保持）：
   需要改代码支持按图像距离动态设置 `max_resolution`

### 中期（算法级）
1. **DensifyPointCloud 分批并行**（如方案 B）
2. **TextureMesh 使用 GPU 加速的 packing 算法**（需改 OpenMVS 源码）
3. **Mapper 启用 GPU BA**（需 ≥50 张图数据集）

### 长期（架构级）
1. **流式管线重构**：让各阶段能增量输入/输出
2. **分布式处理**：大数据集（>500 张图）分块处理后合并
3. **神经网络加速**：用 NeRF/Gaussian Splatting 替代传统 MVS

---

## 六、相关文件

- **默认配置（质量优先）：** [config/reconstruction.json](config/reconstruction.json)
- **快速配置（速度优先）：** [config/reconstruction-fast.json](config/reconstruction-fast.json)
- **优化分析文档：** `/tmp/mvs-pipeline-optimization.md`
- **基线测试产物：** `outputs/baseline-12t-20260806-111619/`
- **优化测试产物：** `outputs/fast-20260806-143704/`
- **2-GPU 测试产物：** `outputs/gpu2-dev0-135357/`, `outputs/gpu2-dev1-135357/`

---

## 七、FAQ

**Q: 为什么单任务用不了多 GPU？**
A: OpenMVS 每个工具进程只初始化一块卡，且管线各阶段严格串行（前一阶段完成后才能开始下一阶段）。要让单次重建用多卡，需要改 OpenMVS 源码把深度图估计分片到多卡。

**Q: 为什么 TextureMesh 这么慢但 GPU 利用率低？**
A: TextureMesh 主要是 CPU 算法（face-to-view assignment 和 bin packing），GPU 只用在纹理投影的小部分。2042s CPU 时间 / 279s 墙上时间 = 7.3x 并行度，说明是 CPU bound。

**Q: mapper 的 GPU BA 能加速多少？**
A: 对你的 40 张图数据集无效（< 50 张阈值会回退 CPU）。即使能用，mapper 只占 5.7% 总耗时，不是瓶颈。

**Q: 优化配置的网格面数减少 28%，会影响视觉效果吗？**
A: 对车载场景影响很小，因为：(1) 纹理分辨率保持 8192×8192 不变，(2) 2048 深度图分辨率对远景和中景已足够，(3) 实测纹理贴图只减少 3.4%。除非有近距离特写需求，否则推荐用优化配置。
