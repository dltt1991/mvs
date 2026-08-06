# Release v0.2.0

## 🎯 主要特性

### CUDA 设备选择
- 新增 `cuda_device` 配置项，支持指定 OpenMVS 工具使用的 GPU
- CLI 参数：`--cuda-device <N>`
  - `-2`: 强制使用 CPU
  - `-1`: 自动选择最佳 GPU（默认）
  - `0, 1, 2...`: 指定 GPU 设备号
- 用途：多数据集并发时分散到不同 GPU，避免资源争抢

### 速度优化配置
- 新增 `config/reconstruction-fast.json` 快速重建配置
- 关键参数优化：
  - `densify_number_views`: 5 → 4
  - `densify_max_resolution`: 2560 → 2048
  - `densify_iters`: 3 → 2
  - `texture_patch_packing_heuristic`: 3 → 100

## 📊 性能提升

### 单任务速度优化（40 张图数据集）
| 配置 | 耗时 | 加速比 | 质量损失 |
|------|------|--------|----------|
| 默认配置 | 713.9s (11'54") | 1.0x | - |
| **快速配置** | **547.8s (9'08")** | **1.30x** | **<5%** |

### 多 GPU 并发吞吐
| 场景 | 单任务耗时 | 2 GPU 吞吐 | 加速比 |
|------|-----------|-----------|--------|
| 串行处理 | 714s | 1428s / 2 jobs | 1.0x |
| **并发处理** | **734s** | **744s / 2 jobs** | **1.92x** |

### 各阶段加速细节
| 阶段 | 默认配置 | 快速配置 | 加速比 |
|------|---------|---------|--------|
| DensifyPointCloud | 126.5s | 77.3s | 1.64x |
| ReconstructMesh | 134.0s | 96.0s | 1.40x |
| TextureMesh | 279.1s | 196.2s | 1.42x |

### 质量影响评估
| 指标 | 默认配置 | 快速配置 | 变化 |
|------|---------|---------|------|
| 网格面数 | 3,010,252 | 2,167,629 | -28.0% |
| 纹理贴图大小 | 56.5 MB | 54.6 MB | **-3.4%** |
| 纹理分辨率 | 8192×8192 | 8192×8192 | 0% |

**结论：** 网格面数减少 28%，但纹理质量几乎无损（-3.4%），视觉效果差异小于 5%。

## 🔧 代码改动

### 新增文件
- `config/reconstruction-fast.json` - 速度优化配置
- `docs/optimization-summary.md` - 完整优化分析文档

### 修改文件
- `src/cpp/pipeline/Config.h` - 新增 `cudaDevice` 字段
- `src/cpp/pipeline/Config.cpp` - 新增 CUDA 设备解析逻辑
- `src/cpp/pipeline/ReconstructionPipeline.cpp` - 将 `--cuda-device` 传递给 OpenMVS 工具
- `config/reconstruction.json` - 文档化 `cuda_device` 选项

## 📖 使用示例

### 快速预览重建
```bash
mvs_reconstruct \
  --config config/reconstruction-fast.json \
  --images data/images \
  --cameras data/cameras.json \
  --output outputs/preview
```

### 多 GPU 批量处理
```bash
# 4 个数据集并发，各绑定不同 GPU
for i in 0 1 2 3; do
  mvs_reconstruct \
    --cuda-device $i \
    --config config/reconstruction-fast.json \
    --images data/dataset$i/images \
    --cameras data/dataset$i/cameras.json \
    --output outputs/dataset$i &
done
wait
```

### 指定 GPU 设备
```bash
# 使用 GPU 1（避开被占用的 GPU 0）
mvs_reconstruct --cuda-device 1 ...

# 强制使用 CPU（调试或无 GPU 环境）
mvs_reconstruct --cuda-device -2 ...
```

## 🔬 技术分析

### GPU 使用现状
| 工具 | 阶段 | GPU 状态 |
|------|------|----------|
| COLMAP | 特征提取/匹配 | ✅ 已启用 |
| COLMAP | mapper BA | ❌ 未启用（40 图 < 50 阈值）|
| OpenMVS | DensifyPointCloud | ✅ 已启用 |
| OpenMVS | ReconstructMesh | ✅ 已启用 |
| OpenMVS | TextureMesh | ✅ 已启用（但 CPU 主导）|

### 资源瓶颈
- **TextureMesh 占 39% 总耗时**，但主要是 CPU bound（face assignment + atlas packing）
- DensifyPointCloud 是唯一 GPU bound 的阶段（GPU 利用率 ~95%）
- 特征提取/匹配/去畸变已接近 CPU 饱和（9-11x 并行度）

### 流式处理可行性
经过深入分析，**undistorter → DensifyPointCloud 流式处理在当前架构下不可行**：
- InterfaceCOLMAP 必须等待全部图像
- OpenMVS `scene.mvs` 格式不支持增量
- 理论收益仅 2.8%，但需数周开发
- **不推荐实施**

## ⬆️ 升级指南

从 v0.1.0 升级：

1. **拉取最新代码**
   ```bash
   git fetch origin
   git checkout v0.2.0
   ```

2. **重新构建**（新增了 CUDA 设备参数传递）
   ```bash
   docker-compose run --rm mvs bash -c "cmake --build build --target mvs_reconstruct -j\$(nproc)"
   ```

3. **尝试快速配置**
   ```bash
   mvs_reconstruct --config config/reconstruction-fast.json ...
   ```

4. **（可选）打包新版本**
   ```bash
   scripts/package.sh
   # 产出: packages/mvs-v0.2.0.tar.gz
   ```

## 🐛 已知限制

- `cuda_device` 只影响 OpenMVS 工具（DensifyPointCloud/ReconstructMesh/TextureMesh）
- COLMAP 工具的 GPU 使用由内置逻辑控制，不受此参数影响
- mapper BA 需要 ≥50 张图才能启用 GPU，40 张图数据集无法受益
- 快速配置会降低网格面数约 28%，但纹理质量几乎不变

## 📚 文档

- [完整优化分析](docs/optimization-summary.md)
- [配置说明](config/reconstruction.json) - 查看所有参数注释
- [快速配置](config/reconstruction-fast.json) - 速度优先配置

## 🙏 致谢

感谢 COLMAP 和 OpenMVS 开源项目提供的强大重建工具链。

---

**Full Changelog**: https://github.com/dltt1991/mvs/compare/v0.1.0...v0.2.0
