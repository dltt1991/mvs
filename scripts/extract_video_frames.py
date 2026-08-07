#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def run(command: list[str]) -> str:
    result = subprocess.run(command, check=True, text=True, capture_output=True)
    return result.stdout.strip()


def video_duration(video: Path) -> float:
    text = run([
        "ffprobe",
        "-v",
        "error",
        "-show_entries",
        "format=duration",
        "-of",
        "default=noprint_wrappers=1:nokey=1",
        str(video),
    ])
    duration = float(text)
    if duration <= 0:
        raise ValueError(f"video duration must be positive: {video}")
    return duration


def extract_frame(video: Path, timestamp: float, output: Path) -> None:
    subprocess.run([
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-ss",
        f"{timestamp:.6f}",
        "-i",
        str(video),
        "-frames:v",
        "1",
        "-q:v",
        "2",
        str(output),
    ], check=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract evenly spaced frames from a video into target_dir/video_name/images/."
    )
    parser.add_argument("video", type=Path)
    parser.add_argument("count", type=int)
    parser.add_argument("target_dir", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    video = args.video.expanduser().resolve()
    if not video.is_file():
        raise FileNotFoundError(f"video not found: {video}")
    if args.count <= 0:
        raise ValueError("count must be positive")

    output_dir = args.target_dir.expanduser().resolve() / video.stem / "images"
    output_dir.mkdir(parents=True, exist_ok=True)

    duration = video_duration(video)
    width = max(6, len(str(args.count)))
    for index in range(args.count):
        # Sample each interval at its midpoint to avoid duplicate first/last frames.
        timestamp = (index + 0.5) * duration / args.count
        output = output_dir / f"{index + 1:0{width}d}.jpg"
        extract_frame(video, timestamp, output)

    print(output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
