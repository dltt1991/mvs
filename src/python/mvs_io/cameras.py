from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class CameraSummary:
    num_cameras: int
    num_images: int
    num_registered: int
    model: str
    width: int
    height: int


def load_camera_summary(path: str | Path) -> CameraSummary:
    camera_path = Path(path)
    with camera_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)

    cameras = data.get("cameras", [])
    if not cameras:
        raise ValueError(f"{camera_path} does not contain any cameras")

    first = cameras[0]
    params = first.get("params", [])
    if first.get("model") != "SIMPLE_RADIAL" or len(params) != 4:
        raise ValueError("only SIMPLE_RADIAL cameras with four params are supported")

    return CameraSummary(
        num_cameras=int(data["num_cameras"]),
        num_images=int(data["num_images"]),
        num_registered=int(data.get("num_registered", 0)),
        model=str(first["model"]),
        width=int(first["width"]),
        height=int(first["height"]),
    )
