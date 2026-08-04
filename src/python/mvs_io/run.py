from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

from .cameras import load_camera_summary


def build_command(args: argparse.Namespace) -> list[str]:
    root = Path(args.root).resolve()
    is_package = (root / "bin" / "mvs_reconstruct").is_file()
    data_root = Path(args.data_root).resolve() if args.data_root else (
        root.parent.parent / "data" if is_package else root / "data"
    )
    binary = Path(args.binary) if args.binary else (
        root / "bin" / "mvs_reconstruct" if is_package else root / "build" / "mvs_reconstruct"
    )
    config = Path(args.config).resolve() if args.config else root / "config" / "reconstruction.json"
    colmap = Path(args.colmap) if args.colmap else (
        root / "bin" / "colmap"
        if is_package
        else root / "build" / "third_party" / "colmap" / "src" / "colmap" / "exe" / "colmap"
    )
    openmvs_bin = Path(args.openmvs_bin) if args.openmvs_bin else (
        root / "bin" if is_package else root / "build" / "third_party" / "openmvs" / "bin"
    )
    images = Path(args.images).resolve() if args.images else data_root / "images"
    cameras = Path(args.cameras).resolve() if args.cameras else data_root / "cameras.json"

    command = [str(binary)]
    if config.is_file() or args.config:
        command.extend(["--config", str(config)])
    command.extend([
        "--images",
        str(images),
        "--cameras",
        str(cameras),
        "--output",
        str(Path(args.output).resolve()),
        "--colmap",
        str(colmap),
        "--openmvs-bin",
        str(openmvs_bin),
    ])
    if args.max_threads is not None:
        command.extend(["--max-threads", str(args.max_threads)])
    if args.undistort_copy_policy:
        command.extend(["--undistort-copy-policy", args.undistort_copy_policy])
    if args.reuse_existing is not None:
        command.extend(["--reuse-existing", str(args.reuse_existing)])
    if args.remove_depth_maps is not None:
        command.extend(["--remove-depth-maps", str(args.remove_depth_maps)])
    if args.matcher:
        command.extend(["--matcher", args.matcher])
    if args.sequential_overlap is not None:
        command.extend(["--sequential-overlap", str(args.sequential_overlap)])
    if args.sequential_quadratic_overlap is not None:
        command.extend([
            "--sequential-quadratic-overlap",
            str(args.sequential_quadratic_overlap),
        ])
    if args.densify_number_views is not None:
        command.extend(["--densify-number-views", str(args.densify_number_views)])
    if args.densify_number_views_fuse is not None:
        command.extend(["--densify-number-views-fuse", str(args.densify_number_views_fuse)])
    if args.densify_geometric_iters is not None:
        command.extend(["--densify-geometric-iters", str(args.densify_geometric_iters)])
    if args.densify_resolution_level is not None:
        command.extend(["--densify-resolution-level", str(args.densify_resolution_level)])
    if args.densify_max_resolution is not None:
        command.extend(["--densify-max-resolution", str(args.densify_max_resolution)])
    if args.densify_iters is not None:
        command.extend(["--densify-iters", str(args.densify_iters)])
    if args.generate_texture is not None:
        command.extend(["--generate-texture", str(args.generate_texture)])
    if args.texture_patch_packing_heuristic is not None:
        command.extend([
            "--texture-patch-packing-heuristic",
            str(args.texture_patch_packing_heuristic),
        ])
    return command


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the C++ MVS reconstruction pipeline.")
    parser.add_argument("--root", default=Path(__file__).resolve().parents[3])
    parser.add_argument("--data-root")
    parser.add_argument("--images")
    parser.add_argument("--cameras")
    parser.add_argument("--output", default="outputs/default")
    parser.add_argument("--config")
    parser.add_argument("--binary")
    parser.add_argument("--colmap")
    parser.add_argument("--openmvs-bin")
    parser.add_argument("--max-threads", type=int)
    parser.add_argument(
        "--undistort-copy-policy",
        choices=["COPY", "SOFT_LINK", "HARD_LINK"],
    )
    parser.add_argument("--reuse-existing", choices=["0", "1", "true", "false"])
    parser.add_argument("--remove-depth-maps", choices=["0", "1", "true", "false"])
    parser.add_argument("--matcher", choices=["exhaustive", "sequential"])
    parser.add_argument("--sequential-overlap", type=int)
    parser.add_argument("--sequential-quadratic-overlap", choices=["0", "1", "true", "false"])
    parser.add_argument("--densify-number-views", type=int)
    parser.add_argument("--densify-number-views-fuse", type=int)
    parser.add_argument("--densify-geometric-iters", type=int)
    parser.add_argument("--densify-resolution-level", type=int)
    parser.add_argument("--densify-max-resolution", type=int)
    parser.add_argument("--densify-iters", type=int)
    parser.add_argument("--generate-texture", choices=["0", "1", "true", "false"])
    parser.add_argument("--texture-patch-packing-heuristic", type=int)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(args.root).resolve()
    data_root = Path(args.data_root).resolve() if args.data_root else (
        root.parent.parent / "data" if (root / "bin" / "mvs_reconstruct").is_file() else root / "data"
    )
    cameras = Path(args.cameras).resolve() if args.cameras else data_root / "cameras.json"
    summary = load_camera_summary(cameras)
    print(
        f"camera: {summary.num_images} images, "
        f"{summary.model}, {summary.width}x{summary.height}"
    )
    command = build_command(args)
    return subprocess.call(command)


if __name__ == "__main__":
    raise SystemExit(main())
