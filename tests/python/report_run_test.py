from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def remove_tree(path: Path) -> None:
    if not path.exists():
        return
    for child in sorted(path.rglob("*"), reverse=True):
        if child.is_file():
            child.unlink()
        elif child.is_dir():
            child.rmdir()
    path.rmdir()


def create_run(run_dir: Path, densify_time: float, mesh_time: float, dense_points: int) -> None:
    remove_tree(run_dir)
    (run_dir / "logs").mkdir(parents=True)
    (run_dir / "openmvs").mkdir(parents=True)

    write(
        run_dir / "manifest.json",
        json.dumps(
            {
                "status": "ok",
                "failed_stage": "",
                "stages": [
                    {
                        "name": "densify_point_cloud",
                        "display_name": "OpenMVS densify",
                        "status": "ok",
                        "duration_seconds": densify_time,
                        "log_file": str(run_dir / "logs" / "densify_point_cloud.log"),
                        "peak_resident_set_size_kb": 2048,
                        "user_cpu_seconds": 40.0,
                        "system_cpu_seconds": 2.0,
                    },
                    {
                        "name": "reconstruct_mesh",
                        "display_name": "OpenMVS mesh",
                        "status": "ok",
                        "duration_seconds": mesh_time,
                        "log_file": str(run_dir / "logs" / "reconstruct_mesh.log"),
                        "peak_resident_set_size_kb": 4096,
                        "user_cpu_seconds": 8.0,
                        "system_cpu_seconds": 0.5,
                    },
                ],
            }
        ),
    )
    write(
        run_dir / "logs" / "densify_point_cloud.log",
        f"Point-cloud 'scene_dense.ply' saved: {dense_points} points (438ms)\n",
    )
    write(
        run_dir / "logs" / "reconstruct_mesh.log",
        "Mesh 'scene_mesh.ply' saved: 111 vertices, 222 faces (135ms)\n",
    )
    write(
        run_dir / "logs" / "texture_mesh.log",
        "Mesh 'scene_texture.ply' saved: 333 vertices, 444 faces (200ms)\n",
    )
    write(run_dir / "openmvs" / "scene_dense.ply", "ply\n")
    write(run_dir / "openmvs" / "scene_mesh.ply", "ply\n")
    write(run_dir / "openmvs" / "scene_texture.ply", "ply\n")


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    run_dir = Path("/tmp/mvs_report_run_test")
    baseline_dir = Path("/tmp/mvs_report_run_baseline_test")
    create_run(run_dir, 12.5, 4.0, 123456)
    create_run(baseline_dir, 20.0, 5.0, 130000)

    result = subprocess.run(
        [sys.executable, str(repo / "scripts" / "report_run.py"), str(run_dir), "--json"],
        check=False,
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    data = json.loads(result.stdout)

    assert data["run_dir"] == str(run_dir.resolve())
    assert data["status"] == "ok"
    assert data["total_duration_seconds"] == 16.5
    assert data["stages"][0]["name"] == "densify_point_cloud"
    assert data["stages"][0]["duration_seconds"] == 12.5
    assert data["openmvs"]["dense_points"] == 123456
    assert data["openmvs"]["mesh_vertices"] == 111
    assert data["openmvs"]["mesh_faces"] == 222
    assert data["openmvs"]["texture_vertices"] == 333
    assert data["openmvs"]["texture_faces"] == 444
    assert data["artifact_bytes"]["openmvs/scene_dense.ply"] > 0
    assert data["total_bytes"] > 0

    compare = subprocess.run(
        [
            sys.executable,
            str(repo / "scripts" / "report_run.py"),
            str(baseline_dir),
            "--compare",
            str(run_dir),
            "--json",
        ],
        check=False,
        text=True,
        capture_output=True,
    )
    assert compare.returncode == 0, compare.stderr
    comparison = json.loads(compare.stdout)["comparison"]
    assert comparison["total_duration_seconds"]["baseline"] == 25.0
    assert comparison["total_duration_seconds"]["candidate"] == 16.5
    assert comparison["total_duration_seconds"]["delta"] == -8.5
    assert comparison["total_duration_seconds"]["percent_delta"] == -34.0
    assert comparison["stages"]["densify_point_cloud"]["duration_seconds"]["delta"] == -7.5
    assert comparison["openmvs"]["dense_points"]["delta"] == -6544
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
