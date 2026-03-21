# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Convert bench_assembly_navigator JSON output to a Markdown report.

Parses the Google Benchmark JSON file(s) produced by bench_assembly_navigator
and renders a Markdown table comparing GDML-reference vs STEP-imported
assembly ray-casting performance and mismatch counts.

Usage:
    python3 generate_assembly_report.py <bench_json_glob> <output_md> [viewer_path]

<bench_json_glob> is either a single JSON path or a glob pattern such as
``bench_assembly_raw_*.json``.  When CI runs bench_assembly_navigator once per
fixture (for process isolation), each invocation writes its own JSON file and
the glob pattern collects them all for a merged report.
"""

import glob
import json
import re
import sys
from pathlib import Path
from urllib.parse import quote

from report_utils import md_escape, timestamp, write_report


_BENCH_SUFFIX_RE = re.compile(r"(/iterations:\d+|/manual_time|/real_time)+$")


def _get_ctr(bm: dict, key: str, default: float = 0.0) -> float:
    """Return a counter value from a benchmark entry (top-level or nested)."""
    if key in bm:
        return float(bm[key])
    return float(bm.get("counters", {}).get(key, default))


def _parse_benchmark_name(name: str):
    """Parse ``BM_assembly_rays/assembly-comparison/<fixture_id>`` → fixture_id.

    Returns ``None`` when the name does not match the expected pattern.
    """
    if not name.startswith("BM_assembly_rays/"):
        return None
    rest       = name[len("BM_assembly_rays/"):]
    fixture_id = _BENCH_SUFFIX_RE.sub("", rest)
    return fixture_id


def _parse_bench_json(data: dict) -> dict:
    """Parse Google Benchmark JSON output from bench_assembly_navigator.

    The JSON is produced with ``--benchmark_out_format=json``.
    Custom context key ``assembly_ray_count`` carries the ray count.
    """
    context    = data.get("context", {})
    ray_count  = context.get("assembly_ray_count")
    try:
        ray_count = int(ray_count) if ray_count is not None else None
    except (ValueError, TypeError):
        ray_count = None

    fixture_order: list[str]      = []
    fixture_groups: dict[str, dict] = {}

    for bm in data.get("benchmarks", []):
        if bm.get("run_type") == "aggregate":
            continue
        name = bm.get("name", "")
        fixture_id = _parse_benchmark_name(name)
        if fixture_id is None:
            continue
        if fixture_id not in fixture_groups:
            fixture_order.append(fixture_id)
            fixture_groups[fixture_id] = bm  # one entry per fixture

    fixtures: list[dict] = []
    for fixture_id in fixture_order:
        bm = fixture_groups[fixture_id]
        gdml_ms            = _get_ctr(bm, "gdml_ms")
        step_ms            = _get_ctr(bm, "step_ms")
        rays               = int(_get_ctr(bm, "ray_count"))
        mismatches         = int(_get_ctr(bm, "mismatches"))
        material_mismatches = int(_get_ctr(bm, "material_mismatches"))
        gdml_cross         = int(_get_ctr(bm, "gdml_crossings"))
        step_cross         = int(_get_ctr(bm, "step_crossings"))
        ratio              = f"{step_ms / gdml_ms:.1f}x" if gdml_ms > 0.0 else "---"
        fixtures.append({
            "id":                 fixture_id,
            "gdml_ms":            gdml_ms,
            "step_ms":            step_ms,
            "ratio":              ratio,
            "rays":               rays,
            "mismatches":         mismatches,
            "material_mismatches": material_mismatches,
            "gdml_crossings":     gdml_cross,
            "step_crossings":     step_cross,
        })

    return {
        "ray_count": ray_count,
        "fixtures":  fixtures,
    }


def _fixture_viewer_link(fixture_id: str, viewer_path: str) -> str:
    """Return a point-cloud viewer deep link for one assembly fixture."""
    # fixture_id already contains the full path (e.g. "assembly-comparison/triple-box-v1")
    # as returned by _parse_benchmark_name(), so use it directly as the pc_id.
    return f"{viewer_path}?fixture={quote(fixture_id, safe='')}"


def _render_report(data: dict, viewer_path: str) -> str:
    """Render the Markdown string for assembly comparison benchmark data."""
    ts        = timestamp()
    fixtures  = data.get("fixtures", [])
    ray_count = data.get("ray_count")

    lines: list[str] = []
    lines.append("## Assembly GDML vs STEP benchmark\n")
    lines.append(f"*Generated: {ts}*\n")

    if ray_count is not None:
        lines.append(f"Rays per object per fixture: **{ray_count:,}**\n")

    if not fixtures:
        lines.append(
            "> ⚠️ No assembly benchmark results found in the JSON file.\n"
        )
        return "\n".join(lines)

    # Build Markdown table.
    header_cols = [
        "Fixture",
        "Rays",
        "GDML ms",
        "STEP ms",
        "Ratio",
        "Pos. mismatches",
        "Mat. mismatches",
        "GDML crossings",
        "STEP crossings",
    ]
    lines.append("| " + " | ".join(header_cols) + " |")
    lines.append("| " + " | ".join(["---"] * len(header_cols)) + " |")

    for fix in fixtures:
        if viewer_path:
            url = _fixture_viewer_link(fix["id"], viewer_path)
            fixture_cell = f"[{md_escape(fix['id'])}]({md_escape(url)})"
        else:
            fixture_cell = md_escape(fix["id"])

        row = [
            fixture_cell,
            str(fix["rays"]),
            f"{fix['gdml_ms']:.1f}",
            f"{fix['step_ms']:.1f}",
            fix["ratio"],
            str(fix["mismatches"]),
            str(fix["material_mismatches"]),
            str(fix["gdml_crossings"]),
            str(fix["step_crossings"]),
        ]
        lines.append("| " + " | ".join(row) + " |")

    lines.append("")
    return "\n".join(lines)


def _render_error(message: str) -> str:
    return f"## Assembly GDML vs STEP benchmark\n\n> ⚠️ {message}\n"


def main() -> None:
    if len(sys.argv) < 3:
        print(
            "Usage: generate_assembly_report.py <bench_json_glob> <output_md> [viewer_path]",
            file=sys.stderr,
        )
        sys.exit(1)

    json_pattern     = sys.argv[1]
    output_md_path   = Path(sys.argv[2])
    viewer_path      = sys.argv[3] if len(sys.argv) > 3 else ""

    # Expand the glob pattern to obtain a sorted list of JSON paths.  A literal
    # file path that matches no glob characters is handled identically (glob
    # returns [path] when the file exists or [] when it does not).
    json_paths = sorted(glob.glob(json_pattern))
    if not json_paths:
        write_report(output_md_path,
                     _render_error(f"No benchmark JSON files found matching: {json_pattern}"))
        return

    # Merge benchmark entries from all matching JSON files.  When CI runs
    # bench_assembly_navigator once per fixture (for process isolation), each
    # invocation writes its own JSON; this merge step reassembles them into a
    # single logical report.  The context section (ray count, fixture root) is
    # taken from the first file — all per-fixture invocations use the same
    # ray count argument, so context values are consistent across files.
    merged_benchmarks: list[dict] = []
    merged_context: dict          = {}
    for json_path in json_paths:
        try:
            file_data = json.loads(Path(json_path).read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError, ValueError) as exc:
            write_report(output_md_path,
                         _render_error(f"Failed to parse JSON {json_path}: {exc}"))
            return
        if not merged_context:
            merged_context = file_data.get("context", {})
        merged_benchmarks.extend(file_data.get("benchmarks", []))

    merged_data = {"context": merged_context, "benchmarks": merged_benchmarks}
    parsed  = _parse_bench_json(merged_data)
    report  = _render_report(parsed, viewer_path)
    write_report(output_md_path, report)


if __name__ == "__main__":
    main()
