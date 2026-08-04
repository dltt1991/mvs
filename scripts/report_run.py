#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


DENSE_POINTS_RE = re.compile(r"Point-cloud 'scene_dense\.ply' saved: (\d+) points")
MESH_RE = re.compile(r"Mesh '(scene_mesh|scene_texture)\.ply' saved: (\d+) vertices, (\d+) faces")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text())


def directory_size(path: Path) -> int:
    total = 0
    for child in path.rglob("*"):
        if child.is_file():
            total += child.stat().st_size
    return total


def artifact_sizes(run_dir: Path) -> dict[str, int]:
    names = [
        "manifest.json",
        "colmap/database.db",
        "openmvs/scene.mvs",
        "openmvs/scene_dense.mvs",
        "openmvs/scene_dense.ply",
        "openmvs/scene_mesh.ply",
        "openmvs/scene_texture.ply",
        "openmvs/scene_texture0.png",
        "openmvs/scene_texture1.png",
    ]
    result: dict[str, int] = {}
    for name in names:
        path = run_dir / name
        if path.is_file():
            result[name] = path.stat().st_size
    return result


def parse_openmvs_logs(run_dir: Path) -> dict[str, int]:
    metrics: dict[str, int] = {}
    logs = run_dir / "logs"

    densify_log = logs / "densify_point_cloud.log"
    if densify_log.is_file():
        match = DENSE_POINTS_RE.search(densify_log.read_text(errors="replace"))
        if match:
            metrics["dense_points"] = int(match.group(1))

    for log_name in ["reconstruct_mesh.log", "texture_mesh.log"]:
        path = logs / log_name
        if not path.is_file():
            continue
        text = path.read_text(errors="replace")
        for match in MESH_RE.finditer(text):
            prefix = "mesh" if match.group(1) == "scene_mesh" else "texture"
            metrics[f"{prefix}_vertices"] = int(match.group(2))
            metrics[f"{prefix}_faces"] = int(match.group(3))
    return metrics


def stage_summary(stage: dict[str, Any]) -> dict[str, Any]:
    return {
        "name": stage.get("name", ""),
        "status": stage.get("status", ""),
        "duration_seconds": float(stage.get("duration_seconds", 0.0)),
        "peak_resident_set_size_kb": int(stage.get("peak_resident_set_size_kb", 0)),
        "user_cpu_seconds": float(stage.get("user_cpu_seconds", 0.0)),
        "system_cpu_seconds": float(stage.get("system_cpu_seconds", 0.0)),
        "log_file": stage.get("log_file", ""),
    }


def summarize_run(run_dir: Path) -> dict[str, Any]:
    manifest_path = run_dir / "manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"missing manifest: {manifest_path}")

    manifest = read_json(manifest_path)
    stages = [stage_summary(stage) for stage in manifest.get("stages", [])]
    return {
        "run_dir": str(run_dir),
        "status": manifest.get("status", ""),
        "failed_stage": manifest.get("failed_stage", ""),
        "total_duration_seconds": sum(stage["duration_seconds"] for stage in stages),
        "total_bytes": directory_size(run_dir),
        "artifact_bytes": artifact_sizes(run_dir),
        "openmvs": parse_openmvs_logs(run_dir),
        "stages": stages,
    }


def format_bytes(size: int) -> str:
    value = float(size)
    for unit in ["B", "KB", "MB", "GB", "TB"]:
        if value < 1024.0 or unit == "TB":
            return f"{value:.1f} {unit}"
        value /= 1024.0
    return f"{size} B"


def compare_value(baseline: int | float | None, candidate: int | float | None) -> dict[str, Any]:
    if baseline is None or candidate is None:
        return {
            "baseline": baseline,
            "candidate": candidate,
            "delta": None,
            "percent_delta": None,
        }

    delta = candidate - baseline
    percent_delta = None if baseline == 0 else (delta / baseline) * 100.0
    return {
        "baseline": baseline,
        "candidate": candidate,
        "delta": delta,
        "percent_delta": percent_delta,
    }


def compare_maps(baseline: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    keys = sorted(set(baseline) | set(candidate))
    return {key: compare_value(baseline.get(key), candidate.get(key)) for key in keys}


def stage_map(summary: dict[str, Any], metric: str) -> dict[str, float | int]:
    return {stage["name"]: stage[metric] for stage in summary["stages"]}


def compare_runs(baseline: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    stage_names = sorted(
        {stage["name"] for stage in baseline["stages"]} | {stage["name"] for stage in candidate["stages"]}
    )
    stages: dict[str, Any] = {}
    for name in stage_names:
        stages[name] = {
            "duration_seconds": compare_value(
                stage_map(baseline, "duration_seconds").get(name),
                stage_map(candidate, "duration_seconds").get(name),
            ),
            "peak_resident_set_size_kb": compare_value(
                stage_map(baseline, "peak_resident_set_size_kb").get(name),
                stage_map(candidate, "peak_resident_set_size_kb").get(name),
            ),
            "user_cpu_seconds": compare_value(
                stage_map(baseline, "user_cpu_seconds").get(name),
                stage_map(candidate, "user_cpu_seconds").get(name),
            ),
            "system_cpu_seconds": compare_value(
                stage_map(baseline, "system_cpu_seconds").get(name),
                stage_map(candidate, "system_cpu_seconds").get(name),
            ),
        }

    return {
        "total_duration_seconds": compare_value(
            baseline["total_duration_seconds"],
            candidate["total_duration_seconds"],
        ),
        "total_bytes": compare_value(baseline["total_bytes"], candidate["total_bytes"]),
        "openmvs": compare_maps(baseline["openmvs"], candidate["openmvs"]),
        "artifact_bytes": compare_maps(baseline["artifact_bytes"], candidate["artifact_bytes"]),
        "stages": stages,
    }


def format_percent(value: float | None) -> str:
    return "n/a" if value is None else f"{value:+.2f}%"


def format_number(value: Any) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, float):
        return f"{value:.2f}"
    return str(value)


def format_delta(metric: dict[str, Any], is_bytes: bool = False) -> tuple[str, str, str, str]:
    baseline = metric["baseline"]
    candidate = metric["candidate"]
    delta = metric["delta"]
    if is_bytes:
        baseline_text = "n/a" if baseline is None else format_bytes(int(baseline))
        candidate_text = "n/a" if candidate is None else format_bytes(int(candidate))
        delta_text = "n/a" if delta is None else format_bytes(abs(int(delta)))
        if isinstance(delta, (int, float)) and delta < 0:
            delta_text = "-" + delta_text
        elif isinstance(delta, (int, float)) and delta > 0:
            delta_text = "+" + delta_text
    else:
        baseline_text = format_number(baseline)
        candidate_text = format_number(candidate)
        delta_text = format_number(delta)
        if isinstance(delta, (int, float)) and delta > 0:
            delta_text = "+" + delta_text
    return baseline_text, candidate_text, delta_text, format_percent(metric["percent_delta"])


def format_markdown(summary: dict[str, Any]) -> str:
    lines = [
        f"# Run Report: {summary['run_dir']}",
        "",
        f"- Status: `{summary['status']}`",
        f"- Total stage time: `{summary['total_duration_seconds']:.2f}s`",
        f"- Total directory size: `{format_bytes(summary['total_bytes'])}`",
        "",
        "## Stages",
        "",
        "| Stage | Status | Time | Peak RSS | User CPU | System CPU |",
        "| --- | --- | ---: | ---: | ---: | ---: |",
    ]
    for stage in summary["stages"]:
        lines.append(
            "| {name} | {status} | {duration_seconds:.2f}s | {rss} | {user_cpu_seconds:.2f}s | {system_cpu_seconds:.2f}s |".format(
                name=stage["name"],
                status=stage["status"],
                duration_seconds=stage["duration_seconds"],
                rss=format_bytes(stage["peak_resident_set_size_kb"] * 1024),
                user_cpu_seconds=stage["user_cpu_seconds"],
                system_cpu_seconds=stage["system_cpu_seconds"],
            )
        )

    if summary["openmvs"]:
        lines.extend(["", "## OpenMVS Metrics", ""])
        for key, value in sorted(summary["openmvs"].items()):
            lines.append(f"- `{key}`: {value}")

    if summary["artifact_bytes"]:
        lines.extend(["", "## Artifacts", ""])
        for name, size in sorted(summary["artifact_bytes"].items()):
            lines.append(f"- `{name}`: {format_bytes(size)}")

    return "\n".join(lines) + "\n"


def format_compare_markdown(report: dict[str, Any]) -> str:
    comparison = report["comparison"]
    lines = [
        f"# Run Compare: {report['baseline']['run_dir']} -> {report['candidate']['run_dir']}",
        "",
        "## Totals",
        "",
        "| Metric | Baseline | Candidate | Delta | Change |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    for key, is_bytes in [
        ("total_duration_seconds", False),
        ("total_bytes", True),
    ]:
        baseline, candidate, delta, percent = format_delta(comparison[key], is_bytes=is_bytes)
        lines.append(f"| `{key}` | {baseline} | {candidate} | {delta} | {percent} |")

    if comparison["stages"]:
        lines.extend(
            [
                "",
                "## Stage Times",
                "",
                "| Stage | Baseline | Candidate | Delta | Change |",
                "| --- | ---: | ---: | ---: | ---: |",
            ]
        )
        for name, metrics in comparison["stages"].items():
            baseline, candidate, delta, percent = format_delta(metrics["duration_seconds"])
            lines.append(f"| `{name}` | {baseline}s | {candidate}s | {delta}s | {percent} |")

    if comparison["openmvs"]:
        lines.extend(
            [
                "",
                "## OpenMVS Metrics",
                "",
                "| Metric | Baseline | Candidate | Delta | Change |",
                "| --- | ---: | ---: | ---: | ---: |",
            ]
        )
        for key, metric in comparison["openmvs"].items():
            baseline, candidate, delta, percent = format_delta(metric)
            lines.append(f"| `{key}` | {baseline} | {candidate} | {delta} | {percent} |")

    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Summarize an MVS reconstruction run.")
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--compare", type=Path, help="Compare the positional baseline run to this candidate run.")
    parser.add_argument("--json", action="store_true", help="Print machine-readable JSON.")
    parser.add_argument("--output", type=Path, help="Write the report to this file.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    summary = summarize_run(args.run_dir.resolve())
    if args.compare:
      candidate = summarize_run(args.compare.resolve())
      report = {
          "baseline": summary,
          "candidate": candidate,
          "comparison": compare_runs(summary, candidate),
      }
      text = (
          json.dumps(report, indent=2, sort_keys=True) + "\n"
          if args.json
          else format_compare_markdown(report)
      )
    else:
      text = json.dumps(summary, indent=2, sort_keys=True) + "\n" if args.json else format_markdown(summary)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
