from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

from .cameras import load_camera_summary


def build_command(args: argparse.Namespace) -> list[str]:
    root = Path(args.root).resolve()
    binary = Path(args.binary) if args.binary else root / "build" / "mvs_reconstruct"
    colmap = Path(args.colmap) if args.colmap else root / "build" / "third_party" / "colmap" / "src" / "colmap" / "exe" / "colmap"
    openmvs_bin = Path(args.openmvs_bin) if args.openmvs_bin else root / "build" / "third_party" / "openmvs" / "bin"

    return [
        str(binary),
        "--images",
        str(Path(args.images).resolve()),
        "--cameras",
        str(Path(args.cameras).resolve()),
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
    parser.add_argument("--images", default="data/images")
    parser.add_argument("--cameras", default="data/cameras.json")
    parser.add_argument("--output", default="outputs/default")
    parser.add_argument("--binary")
    parser.add_argument("--colmap")
    parser.add_argument("--openmvs-bin")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    summary = load_camera_summary(args.cameras)
    print(
        f"camera: {summary.num_images} images, "
        f"{summary.model}, {summary.width}x{summary.height}"
    )
    command = build_command(args)
    return subprocess.call(command)


if __name__ == "__main__":
    raise SystemExit(main())
