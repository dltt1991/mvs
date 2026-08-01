# 数据目录说明

`data/` 用于存放本地重建输入数据。该目录下的图片和相机参数通常较大或与具体拍摄任务相关，默认不会全部提交到 Git；仓库只保留本说明文件。

## 目录结构

推荐结构如下：

```text
data/
  README.md
  cameras.json
  images/
    image_001.jpg
    image_002.jpg
    ...
```

- `images/`：多视角输入图片目录。
- `cameras.json`：相机内参配置文件。

图片可以使用 `.jpg`、`.jpeg`、`.png`、`.tif`、`.tiff` 等 COLMAP/OpenMVS 可读取的格式。建议同一批数据使用相同分辨率、相同相机或相近焦段，并保证相邻视角之间有足够重叠。

## cameras.json 格式

当前 C++ pipeline 支持 COLMAP `SIMPLE_RADIAL` 相机模型。示例：

```json
{
  "num_cameras": 2,
  "num_images": 2,
  "num_registered": 2,
  "cameras": [
    {
      "id": 1,
      "model": "SIMPLE_RADIAL",
      "width": 6016,
      "height": 4512,
      "params": [4404.6373, 3008.0, 2256.0, 0.00595],
      "fx": 4404.6373,
      "fy": 4404.6373,
      "cx": 3008.0,
      "cy": 2256.0,
      "k1": 0.00595
    }
  ]
}
```

字段说明：

- `num_cameras`：相机记录数量，应与 `cameras` 数组长度一致。
- `num_images`：输入图片数量，应与 `data/images/` 中参与重建的图片数量一致。
- `num_registered`：已注册图片数量，可选；缺省时按 `0` 处理。用于记录数据来源状态，不直接控制 COLMAP 注册过程。
- `cameras`：相机内参数组，至少包含一条记录。
- `cameras[].id`：相机记录 ID。
- `cameras[].model`：相机模型，当前必须为 `SIMPLE_RADIAL`。
- `cameras[].width`：图片宽度，单位像素。
- `cameras[].height`：图片高度，单位像素。
- `cameras[].params`：COLMAP `SIMPLE_RADIAL` 参数，必须为 4 个数值，顺序是 `[f, cx, cy, k1]`。
- `cameras[].fx`、`fy`、`cx`、`cy`、`k1`：辅助字段，便于人工阅读或由外部工具生成；当前 C++ 核心只读取 `params`。

## 当前代码如何使用

程序启动时会读取 `cameras.json`，校验所有相机记录：

- `model` 必须是 `SIMPLE_RADIAL`。
- `width`、`height` 必须大于 0。
- `params` 必须包含 4 个数值。
- `f` 必须大于 0。
- 所有相机记录的 `width` 和 `height` 必须一致。

随后程序会对所有相机记录的 `f`、`cx`、`cy`、`k1` 分别取中位数，生成一个共享的 COLMAP 相机参数，并传给 `feature_extractor`：

```text
--ImageReader.camera_model SIMPLE_RADIAL
--ImageReader.camera_params f,cx,cy,k1
--ImageReader.single_camera 1
```

也就是说，当前流程默认一批图片使用同一个相机模型和一组汇总后的内参。

## 运行路径

源码目录中运行：

```bash
scripts/reconstruct.sh
```

默认读取：

```text
data/images
data/cameras.json
```

从 `packages/mvs-<version>/` 包目录中运行时，包内不包含 `data/`，`scripts/reconstruct.sh` 默认回到主项目目录读取：

```text
../../data/images
../../data/cameras.json
```

也可以显式指定数据目录：

```bash
DATA_ROOT=/path/to/data packages/mvs-<version>/scripts/reconstruct.sh
```
