# 方案3实施结果：库级集成尝试

## 实施过程

尝试使用 COLMAP 库 API 直接在 C++ 中实现去畸变，以实现真正的流水线并行。

### 遇到的问题

1. **COLMAP 库依赖复杂**
   - 需要链接：scene, scene_types, image, sensor, util, geometry, math
   - 需要系统库：Eigen3, Boost, curl, crypto, glog, gflags, OpenImageIO
   - 最终二进制从 233KB 膨胀到 1.6MB

2. **COLMAP 4.x API 变更**
   - `Qvec()`/`Tvec()` → `CamFromWorld().rotation()`/`translation()`
   - `Params(i)` → `params[i]`（公开成员）
   - Image 与 Camera 有内部指针依赖，无法简单复制

3. **相机模型转换问题**
   - OpenMVS 只接受 PINHOLE 模型
   - 原始 sparse model 包含带畸变的模型（SIMPLE_RADIAL 等）
   - 需要创建新 Reconstruction 并转换，但 Image 对象无法重新关联到新 Camera
   - 错误：`Check failed: image.CameraPtr() == &camera`

4. **根本限制：COLMAP undistorter 的行为**
   - Undistorter 在**完成所有图像去畸变后**才写入 `dense/sparse/` 的 PINHOLE 模型
   - 提前复制原始 sparse model 没有用——OpenMVS 需要 PINHOLE 模型
   - 即使用库实现去畸变，也需要等所有图像完成才能写入正确的 sparse model

## 方案1测试结果

退回到简单方案：提前复制 sparse model，使用 COLMAP 命令行。

```
基线（串行分离进程）:
  undistorter:        74.2s
  InterfaceCOLMAP:     0.2s
  DensifyPointCloud:  77.3s
  ────────────────────────
  总计:              151.7s

方案1（提前复制 + 等待图像）:
  undistorter:        73.6s（后台）
  densify 等待:       50s（等到 36 张图像）
  InterfaceCOLMAP:    失败（相机模型不是 PINHOLE）
  ────────────────────────
  无法完成
```

## 结论

### 为什么融合方案无法工作？

1. **OpenMVS 约束**：InterfaceCOLMAP 只接受 PINHOLE 相机模型
2. **COLMAP 行为**：undistorter 在完成所有去畸变后才创建 PINHOLE sparse model
3. **依赖链**：InterfaceCOLMAP 需要 PINHOLE sparse model → 必须等 undistorter 完全完成

### 理论上可行但不值得的方案

**完全重写 undistorter + InterfaceCOLMAP**：
- 自己实现去畸变（OpenCV）
- 逐张流式处理
- 边去畸变边转换为 OpenMVS 格式
- 30 张图像完成后立即启动 DensifyPointCloud

**预期收益**：~20s (13%)
**投入**：7-10 天开发 + 调试
**风险**：高（格式兼容性、精度验证）

### 推荐方案

**停止进一步优化**，原因：
1. 已通过参数调优获得 -23% (714s → 548s)
2. 进一步融合理论最大收益 ~13%，但实施成本高、风险大
3. **投入产出比太低**

## 累计优化效果

```
基线（默认参数）:             714s
参数调优:                     548s (-23%)
理论融合极限（重写一切）:      ~480s (-33% 累计)
```

已经获得了大部分可得收益（23% / 33% = 70%），剩余 30% 需要 10 倍的工作量。

## 保留的代码

- `mvs_fused_densify`：可编译可运行，但无法达到预期加速
- 架构：多线程设计正确，问题在于工具链的执行模型
- 可用于未来改进（如果有人修改 COLMAP/OpenMVS）

---

**建议：接受当前 548s 的结果，停止此方向的优化。**
