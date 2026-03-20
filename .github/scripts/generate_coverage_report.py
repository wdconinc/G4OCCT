# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors
"""
Convert a gcovr JSON summary (--json-summary) to a Markdown report.

Usage:
    python3 generate_coverage_report.py <summary.json> <output.md>
"""

import json
import sys
from pathlib import Path


def coverage_badge(percent: float) -> str:
    """Return a simple text badge indicating coverage quality."""
    if percent >= 80:
        return "🟢"
    if percent >= 60:
        return "🟡"
    return "🔴"


def fmt(percent: float) -> str:
    return f"{percent:.1f}%"


def main() -> None:
    """Read a gcovr JSON summary and write a Markdown coverage report.

    Reads the gcovr ``--json-summary`` output from ``sys.argv[1]``, which
    contains top-level keys ``line_percent``, ``line_covered``,
    ``line_total``, ``function_percent``, ``function_covered``,
    ``function_total``, ``branch_percent``, ``branch_covered``,
    ``branch_total``, and an optional ``files`` list with per-file
    breakdowns.  Writes a Markdown table (with emoji traffic-light
    indicators) to ``sys.argv[2]``.
    """
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <summary.json> <output.md>", file=sys.stderr)
        sys.exit(1)

    summary_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])

    with summary_path.open() as f:
        data = json.load(f)

    lines_pct = data.get("line_percent", 0.0)
    lines_covered = data.get("line_covered", 0)
    lines_total = data.get("line_total", 0)

    funcs_pct = data.get("function_percent", 0.0)
    funcs_covered = data.get("function_covered", 0)
    funcs_total = data.get("function_total", 0)

    branches_pct = data.get("branch_percent", 0.0)
    branches_covered = data.get("branch_covered", 0)
    branches_total = data.get("branch_total", 0)

    files = sorted(
        data.get("files", []),
        key=lambda f: f.get("line_percent", 0.0),
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)

    lines = [
        "## 📊 Coverage Report",
        "",
        "| Metric | Coverage | Covered / Total |",
        "| ------ | -------- | --------------- |",
        f"| Lines | {coverage_badge(lines_pct)} {fmt(lines_pct)} "
        f"| {lines_covered} / {lines_total} |",
        f"| Functions | {coverage_badge(funcs_pct)} {fmt(funcs_pct)} "
        f"| {funcs_covered} / {funcs_total} |",
        f"| Branches | {coverage_badge(branches_pct)} {fmt(branches_pct)} "
        f"| {branches_covered} / {branches_total} |",
        "",
    ]

    if files:
        lines += [
            "<details>",
            "<summary>Per-file breakdown</summary>",
            "",
            "| File | Lines | Functions | Branches |",
            "| ---- | ----- | --------- | -------- |",
        ]
        for file_entry in files:
            fname = file_entry.get("filename", "?")
            fl = file_entry.get("line_percent", 0.0)
            ff = file_entry.get("function_percent", 0.0)
            fb = file_entry.get("branch_percent", 0.0)
            lines.append(
                f"| `{fname}` | {coverage_badge(fl)} {fmt(fl)} "
                f"| {coverage_badge(ff)} {fmt(ff)} "
                f"| {coverage_badge(fb)} {fmt(fb)} |"
            )
        lines += ["", "</details>", ""]

    output_path.write_text("\n".join(lines))
    print(f"Coverage report written to {output_path}")


if __name__ == "__main__":
    main()
