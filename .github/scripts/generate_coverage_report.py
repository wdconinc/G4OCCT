# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors
"""
Convert a gcovr JSON summary (--json-summary) to a Markdown report.

Usage:
    python3 generate_coverage_report.py <summary.json> <output.md>
"""

import json
import os
import sys
from pathlib import Path

from report_utils import write_report


def pages_base_url() -> str:
    """Return the GitHub Pages base URL for this repository.

    Resolution order:
    1. ``PAGES_BASE_URL`` environment variable (explicit override, useful for
       forks or local previews — set to e.g. ``https://myfork.github.io/G4OCCT``).
    2. Derived from ``GITHUB_REPOSITORY`` (``owner/repo`` provided by GitHub
       Actions), producing ``https://{owner}.github.io/{repo}``.
    3. Hard-coded upstream default ``https://eic.github.io/G4OCCT``.
    """
    override = os.environ.get("PAGES_BASE_URL", "").rstrip("/")
    if override:
        return override

    github_repo = os.environ.get("GITHUB_REPOSITORY", "")
    if github_repo and "/" in github_repo:
        owner, repo = github_repo.split("/", 1)
        return f"https://{owner}.github.io/{repo}"

    return "https://eic.github.io/G4OCCT"


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

    Exits with code 1 if the required arguments are missing.  If the
    JSON file cannot be read or parsed, a placeholder report is written
    to ``sys.argv[2]`` instead of raising an exception.
    """
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <summary.json> <output.md>", file=sys.stderr)
        sys.exit(1)

    summary_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])

    try:
        with summary_path.open(encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        write_report(
            output_path,
            "## 📊 Coverage Report\n\nCoverage summary file not found.\n",
            label="Coverage report",
        )
        return
    except json.JSONDecodeError as exc:
        write_report(
            output_path,
            f"## 📊 Coverage Report\n\nFailed to parse coverage summary: {exc}\n",
            label="Coverage report",
        )
        return

    lines_pct = data.get("line_percent") or 0.0
    lines_covered = data.get("line_covered") or 0
    lines_total = data.get("line_total") or 0

    funcs_pct = data.get("function_percent") or 0.0
    funcs_covered = data.get("function_covered") or 0
    funcs_total = data.get("function_total") or 0

    branches_pct = data.get("branch_percent") or 0.0
    branches_covered = data.get("branch_covered") or 0
    branches_total = data.get("branch_total") or 0

    files = sorted(
        data.get("files", []),
        key=lambda f: f.get("line_percent") or 0.0,
    )

    content_lines = [
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
        f"[View annotated HTML coverage report →]({pages_base_url()}/coverage/index.html)",
        "",
    ]

    # Strip the build-machine root prefix from filenames so the table shows
    # paths relative to the project root (e.g. "src/G4OCCTSolid.cc") instead
    # of the full CI absolute path.
    root_prefix = data.get("root", "")
    if root_prefix and not root_prefix.endswith("/"):
        root_prefix += "/"

    if files:
        content_lines += [
            "<details>",
            "<summary>Per-file breakdown</summary>",
            "",
            "| File | Lines | Functions | Branches |",
            "| ---- | ----- | --------- | -------- |",
        ]
        for file_entry in files:
            fname = file_entry.get("filename", "?")
            if root_prefix and fname.startswith(root_prefix):
                fname = fname[len(root_prefix):]
            fl = file_entry.get("line_percent") or 0.0
            ff = file_entry.get("function_percent") or 0.0
            fb = file_entry.get("branch_percent") or 0.0
            content_lines.append(
                f"| `{fname}` | {coverage_badge(fl)} {fmt(fl)} "
                f"| {coverage_badge(ff)} {fmt(ff)} "
                f"| {coverage_badge(fb)} {fmt(fb)} |"
            )
        content_lines += ["", "</details>", ""]

    write_report(output_path, "\n".join(content_lines), label="Coverage report")


if __name__ == "__main__":
    main()
