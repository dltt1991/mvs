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

    return [
        str(binary),
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
    ]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the C++ MVS reconstruction pipeline.")
    parser.add_argument("--root", default=Path(__file__).resolve().parents[3])
    parser.add_argument("--data-root")
    parser.add_argument("--images")
    parser.add_argument("--cameras")
    parser.add_argument("--output", default="outputs/default")
    parser.add_argument("--binary")
    parser.add_argument("--colmap")
    parser.add_argument("--openmvs-bin")
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
