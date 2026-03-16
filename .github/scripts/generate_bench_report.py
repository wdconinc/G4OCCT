# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Convert bench_navigator text output to a Markdown report."""

import re
import sys
from datetime import datetime, timezone
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
from pathlib import Path

try:
    _TZ = ZoneInfo("America/New_York")
except ZoneInfoNotFoundError:
    _TZ = timezone.utc


def _parse_bench_output(text: str) -> dict:
    """Parse bench_navigator stdout into a structured dict."""
    lines = text.splitlines()

    in_results = False
    ray_count  = None
    fixtures   = []
    aggregate  = {}

    for line in lines:
        line = line.rstrip()

        if line == "=== Fixture Ray Benchmark Results ===":
            in_results = True
            continue

        if not in_results:
            continue

        m = re.match(r"Rays per fixture: (\d+)", line)
        if m:
            ray_count = int(m.group(1))
            continue

        # Per-fixture: "id (Class): native=X ms, imported=Y ms, mismatches=M"
        m = re.match(
            r"(.+?) \((.+?)\): native=([\d.]+) ms, imported=([\d.]+) ms, mismatches=(\d+)",
            line,
        )
        if m:
            fixtures.append({
                "id":          m.group(1).strip(),
                "class":       m.group(2).strip(),
                "native_ms":   float(m.group(3)),
                "imported_ms": float(m.group(4)),
                "mismatches":  int(m.group(5)),
            })
            continue

        m = re.match(r"Aggregate native\s+: ([\d.]+) ms", line)
        if m:
            aggregate["native_ms"] = float(m.group(1))
            continue

        m = re.match(r"Aggregate imported\s+: ([\d.]+) ms", line)
        if m:
            aggregate["imported_ms"] = float(m.group(1))
            continue

        m = re.match(r"Native/imported ratio: ([\d.]+)", line)
        if m:
            aggregate["ratio"] = float(m.group(1))
            continue

        m = re.match(r"Total mismatches: (\d+)", line)
        if m:
            aggregate["mismatches"] = int(m.group(1))
            continue

        m = re.match(r"Expected failures: (\d+)", line)
        if m:
            aggregate["expected_failures"] = int(m.group(1))
            continue

    return {"ray_count": ray_count, "fixtures": fixtures, "aggregate": aggregate}


def _md_escape(text: str) -> str:
    """Escape characters that break Markdown table cells."""
    return text.replace("|", "\\|").replace("\n", " ").replace("\r", "")


def _render_report(data: dict) -> str:
    """Render the Markdown string for benchmark data."""
    timestamp = datetime.now(_TZ).strftime("%Y-%m-%d %H:%M %Z")
    agg       = data.get("aggregate", {})
    ray_count = data.get("ray_count", "?")
    fixtures  = data.get("fixtures", [])

    native_ms   = agg.get("native_ms",  0.0)
    imported_ms = agg.get("imported_ms", 0.0)
    ratio       = agg.get("ratio",      0.0)
    total_mm    = agg.get("mismatches", 0)
    exp_fail    = agg.get("expected_failures", 0)

    ratio_str = f"{ratio:.3f}" if ratio else "N/A"

    meta_line = f"Generated: {timestamp}"
    if ray_count:
        meta_line += f" · Rays per fixture: {ray_count}"

    lines = [
        "# G4OCCT Benchmark Results",
        "",
        meta_line,
        "",
        "## Aggregate Results",
        "",
        "| Native (ms) | Imported (ms) | Native/Imported Ratio | Total Mismatches |",
        "|------------:|--------------:|----------------------:|-----------------:|",
        f"| {native_ms:.1f} | {imported_ms:.1f} | {ratio_str} | {total_mm} |",
    ]

    if not fixtures:
        lines += [
            "",
            "> ⚠️ No fixture results found in output.",
        ]
    else:
        lines += [
            "",
            "## Per-Fixture Results",
            "",
            "| Fixture | Geant4 Class | Native (ms) | Imported (ms) | Ratio | Mismatches |",
            "|---------|--------------|------------:|--------------:|------:|-----------:|",
        ]
        for f in fixtures:
            fix_ratio = (f["native_ms"] / f["imported_ms"]) if f["imported_ms"] > 0 else 0.0
            lines.append(
                f"| {_md_escape(f['id'])} | {_md_escape(f['class'])} "
                f"| {f['native_ms']:.2f} | {f['imported_ms']:.2f} "
                f"| {fix_ratio:.3f} | {f['mismatches']} |"
            )

    if exp_fail:
        lines += [
            "",
            f"> **Note:** {exp_fail} expected failure(s) are included in these results. "
            "Mismatches from expected-failure fixtures are reclassified and do not cause errors.",
        ]

    return "\n".join(lines) + "\n"


def _render_error(message: str) -> str:
    """Render a minimal Markdown error report."""
    timestamp = datetime.now(_TZ).strftime("%Y-%m-%d %H:%M %Z")
    return (
        "# G4OCCT Benchmark Results\n\n"
        f"Generated: {timestamp}\n\n"
        f"❌ Could not generate report: {message}\n"
    )


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <bench-results.txt> <output.md>", file=sys.stderr)
        sys.exit(1)

    txt_path, md_path = sys.argv[1], sys.argv[2]
    Path(md_path).parent.mkdir(parents=True, exist_ok=True)

    try:
        text = Path(txt_path).read_text(encoding="utf-8")
    except (FileNotFoundError, OSError) as exc:
        print(f"Warning: {exc}", file=sys.stderr)
        Path(md_path).write_text(_render_error(str(exc)), encoding="utf-8")
        print(f"Benchmark report written to: {md_path}")
        return

    data = _parse_bench_output(text)
    md   = _render_report(data)
    Path(md_path).write_text(md, encoding="utf-8")
    print(f"Benchmark report written to: {md_path}")


if __name__ == "__main__":
    main()
