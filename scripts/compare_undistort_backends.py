#!/usr/bin/env python3
"""对比两个去畸变后端的产物：内参几何、图像尺寸、黑边、耗时。

用法：
    scripts/compare_undistort_backends.py <colmap-run-dir> <opencv-run-dir>

核心断言是"几何一致"：OpenCV 后端复用 colmap::UndistortCamera() 算几何，
所以 dense/sparse/cameras.bin 的 width/height/fx/fy/cx/cy 应与 COLMAP 后端逐位相同。
不一致说明 UndistortCameraOptions 没有在图像和 sparse model 之间保持同一份。

只依赖标准库 + PIL（容器内无 numpy/cv2，宿主机 PIL 可用）。
"""

import json
import struct
import sys
from pathlib import Path

# COLMAP camera model id → (名称, 参数个数)
CAMERA_MODELS = {
    0: ("SIMPLE_PINHOLE", 3),
    1: ("PINHOLE", 4),
    2: ("SIMPLE_RADIAL", 4),
    3: ("RADIAL", 5),
    4: ("OPENCV", 8),
    5: ("OPENCV_FISHEYE", 8),
    6: ("FULL_OPENCV", 12),
    7: ("FOV", 5),
    8: ("SIMPLE_RADIAL_FISHEYE", 4),
    9: ("RADIAL_FISHEYE", 5),
    10: ("THIN_PRISM_FISHEYE", 12),
}


def read_cameras_bin(path):
    """解析 COLMAP cameras.bin，返回 {camera_id: dict}。"""
    cameras = {}
    with open(path, "rb") as f:
        (num,) = struct.unpack("<Q", f.read(8))
        for _ in range(num):
            camera_id, model_id, width, height = struct.unpack("<iiQQ", f.read(24))
            name, num_params = CAMERA_MODELS[model_id]
            params = struct.unpack("<" + "d" * num_params, f.read(8 * num_params))
            cameras[camera_id] = {
                "model": name,
                "width": width,
                "height": height,
                "params": params,
            }
    return cameras


def cameras_equal(a, b, tol=1e-9):
    """两组相机是否在容差内完全相同。"""
    if set(a) != set(b):
        return False
    for cid in a:
        ca, cb = a[cid], b[cid]
        if ca["model"] != cb["model"]:
            return False
        if (ca["width"], ca["height"]) != (cb["width"], cb["height"]):
            return False
        if len(ca["params"]) != len(cb["params"]):
            return False
        if any(abs(x - y) > tol for x, y in zip(ca["params"], cb["params"])):
            return False
    return True


def stage_durations(run_dir):
    manifest = json.loads((run_dir / "manifest.json").read_text())
    return {s["name"]: (s["duration_seconds"], s["status"]) for s in manifest["stages"]}


def black_stats(image_path, corner=40, threshold=8):
    """返回 (宽, 高, 全图近黑占比, 四角近黑数, 四角总数)。"""
    try:
        from PIL import Image
    except ImportError:
        return None
    with Image.open(image_path) as im:
        g = im.convert("L")
        w, h = g.size
        total_black = sum(g.histogram()[:threshold])
        boxes = [
            (0, 0, corner, corner),
            (w - corner, 0, w, corner),
            (0, h - corner, corner, h),
            (w - corner, h - corner, w, h),
        ]
        corner_black = sum(sum(g.crop(b).histogram()[:threshold]) for b in boxes)
    return w, h, 100.0 * total_black / (w * h), corner_black, 4 * corner * corner


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    runs = {"colmap": Path(sys.argv[1]), "opencv": Path(sys.argv[2])}

    failures = []

    # ── 1. 耗时 ──────────────────────────────────────────────────────────────
    print("=== 阶段耗时 ===")
    print(f"{'stage':22s} {'colmap':>12s} {'opencv':>12s}")
    durations = {k: stage_durations(v) for k, v in runs.items()}
    all_stages = list(durations["colmap"].keys())
    for name in all_stages:
        c = durations["colmap"].get(name, (0, "-"))
        o = durations["opencv"].get(name, (0, "-"))
        mark = "  <<<" if name == "image_undistorter" else ""
        print(f"{name:22s} {c[0]:10.1f}s {o[0]:10.1f}s{mark}")
    total_c = sum(d for d, _ in durations["colmap"].values())
    total_o = sum(d for d, _ in durations["opencv"].values())
    print(f"{'TOTAL':22s} {total_c:10.1f}s {total_o:10.1f}s")

    # interface_colmap 必须两边都真正执行过
    for key in runs:
        dur, status = durations[key].get("interface_colmap", (0, "missing"))
        if status != "ok":
            failures.append(f"{key}: interface_colmap 状态为 {status}，应为 ok")

    # ── 2. 输入 sparse model 是否可比 ────────────────────────────────────────
    # COLMAP mapper 是非确定性的：两次独立运行会得到略微不同的内参，进而让
    # UndistortCamera 算出不同的裁剪尺寸。这与后端实现无关，必须先排除，
    # 否则会把 mapper 抖动误报成后端 bug。
    print("\n=== 输入 sparse model（mapper 产物）===")
    input_cams = {}
    for key, run in runs.items():
        path = run / "colmap" / "sparse" / "0" / "cameras.bin"
        if path.exists():
            input_cams[key] = read_cameras_bin(path)
            for cid, cam in input_cams[key].items():
                print(f"  {key} camera {cid}: {cam['model']:14s} "
                      f"{cam['width']}x{cam['height']}  "
                      + " ".join(f"{p:.6f}" for p in cam["params"]))

    same_input = len(input_cams) == 2 and cameras_equal(
        input_cams["colmap"], input_cams["opencv"], tol=1e-9)
    if same_input:
        print("  → 输入相同，dense/sparse 内参必须逐位一致")
    else:
        print("  → 输入不同（mapper 非确定性）：内参差异预期存在，"
              "改用 --reuse-existing 复用同一次 mapper 产物才能严格对比几何")

    # ── 3. 几何一致性 ────────────────────────────────────────────────────────
    print("\n=== dense/sparse 内参对比 ===")
    cams = {}
    for key, run in runs.items():
        path = run / "colmap" / "dense" / "sparse" / "cameras.bin"
        if not path.exists():
            failures.append(f"{key}: 缺少 {path}")
            continue
        cams[key] = read_cameras_bin(path)

    if len(cams) == 2:
        ids = sorted(set(cams["colmap"]) | set(cams["opencv"]))
        for cid in ids:
            c, o = cams["colmap"].get(cid), cams["opencv"].get(cid)
            if c is None or o is None:
                failures.append(f"camera {cid} 只存在于一侧")
                continue
            print(f"camera {cid}:")
            print(f"  colmap: {c['model']:14s} {c['width']}x{c['height']}  "
                  + " ".join(f"{p:.6f}" for p in c["params"]))
            print(f"  opencv: {o['model']:14s} {o['width']}x{o['height']}  "
                  + " ".join(f"{p:.6f}" for p in o["params"]))
            # 模型必须一致（PINHOLE），这与 mapper 抖动无关
            if c["model"] != o["model"]:
                failures.append(f"camera {cid} 模型不同: {c['model']} vs {o['model']}")

            # 尺寸/内参只在输入相同时才要求逐位一致
            geom_same = cameras_equal({cid: c}, {cid: o}, tol=1e-6)
            if same_input and not geom_same:
                failures.append(
                    f"camera {cid} 几何不一致（输入相同，属后端 bug）: "
                    f"{c['width']}x{c['height']} vs {o['width']}x{o['height']}")
            elif not same_input and not geom_same:
                dw = abs(c["width"] - o["width"])
                dh = abs(c["height"] - o["height"])
                print(f"  → 差异 {dw}x{dh} px，由 mapper 非确定性解释（非后端问题）")

    # ── 3. 图像尺寸与黑边 ────────────────────────────────────────────────────
    print("\n=== 去畸变图像（同名首图）===")
    names = {}
    for key, run in runs.items():
        d = run / "colmap" / "dense" / "images"
        names[key] = sorted(p.name for p in d.glob("*.jpg")) if d.is_dir() else []
    common = sorted(set(names["colmap"]) & set(names["opencv"]))
    if not common:
        failures.append("两侧没有同名图像可比对")
    else:
        for key, run in runs.items():
            p = run / "colmap" / "dense" / "images" / common[0]
            st = black_stats(p)
            size_mb = sum(f.stat().st_size for f in p.parent.glob("*.jpg")) / 1024 / 1024
            if st is None:
                print(f"  {key}: (PIL 不可用，跳过黑边统计)  总量 {size_mb:.0f}MB")
                continue
            w, h, pct, cb, ct = st
            # 黑边是后端自身的性质，与 mapper 抖动无关：必须始终接近 0
            if cb > ct * 0.01:
                failures.append(
                    f"{key}: 四角近黑 {cb}/{ct}，去畸变黑边未裁剪")
            print(f"  {key}: {w}x{h}  近黑 {pct:.4f}%  四角 {cb}/{ct}  "
                  f"{len(names[key])} 张 共 {size_mb:.0f}MB")

        cs = black_stats(runs["colmap"] / "colmap" / "dense" / "images" / common[0])
        os_ = black_stats(runs["opencv"] / "colmap" / "dense" / "images" / common[0])
        if cs and os_ and (cs[0], cs[1]) != (os_[0], os_[1]):
            if same_input:
                failures.append(
                    f"图像尺寸不同: colmap {cs[0]}x{cs[1]} vs opencv {os_[0]}x{os_[1]}")
            else:
                print(f"  → 尺寸差异同样由 mapper 非确定性解释")

        # 图像尺寸必须与自己那侧写出的内参一致（K 与图像同步，与对侧无关）
        for key, st in (("colmap", cs), ("opencv", os_)):
            if st and key in cams:
                for cid, cam in cams[key].items():
                    if (st[0], st[1]) != (cam["width"], cam["height"]):
                        failures.append(
                            f"{key}: 图像 {st[0]}x{st[1]} 与 dense/sparse 内参 "
                            f"{cam['width']}x{cam['height']} 不符（K 与图像未同步）")

    # ── 4. 重建产物规模 ─────────────────────────────────────────────────────
    print("\n=== 重建产物 ===")
    for key, run in runs.items():
        parts = []
        for artifact in ("scene_dense.ply", "scene_mesh.ply", "scene_texture.ply"):
            f = run / "openmvs" / artifact
            parts.append(f"{artifact}={f.stat().st_size // 1024 // 1024}MB" if f.exists()
                         else f"{artifact}=缺失")
        print(f"  {key}: " + "  ".join(parts))

    print()
    if failures:
        print("❌ 不一致项：")
        for f in failures:
            print(f"  - {f}")
        return 1
    if same_input:
        print("✅ 几何逐位一致、黑边已裁剪、K 与图像同步、interface_colmap 正常执行")
    else:
        print("✅ 黑边已裁剪、K 与图像同步、interface_colmap 正常执行")
        print("   （两次 mapper 产物不同，几何未做逐位比较——严格对比请用 --reuse-existing）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
