#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Convert bench_navigator text output to a self-contained HTML report."""

import re
import sys
from datetime import datetime, timezone
from html import escape
from pathlib import Path

_CSS = """
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: #f0f2f5; color: #333; padding: 24px;
    }
    .container { max-width: 1100px; margin: 0 auto; }
    h1 { font-size: 1.6rem; color: #1a2a3a; margin-bottom: 4px; }
    .meta { font-size: 0.85rem; color: #666; margin-bottom: 20px; }
    .aggregate { display: flex; flex-wrap: wrap; gap: 12px; margin-bottom: 28px; }
    .agg-card {
      background: #fff; border-radius: 8px; padding: 16px 24px;
      box-shadow: 0 1px 4px rgba(0,0,0,0.1); min-width: 140px;
    }
    .agg-label {
      font-size: 0.75rem; text-transform: uppercase; color: #666;
      letter-spacing: 0.05em;
    }
    .agg-value { font-size: 1.6rem; font-weight: 700; margin-top: 4px; color: #1a2a3a; }
    .agg-unit  { font-size: 0.8rem; font-weight: 400; color: #888; }
    .ratio-good { color: #065f46; }
    .ratio-bad  { color: #991b1b; }
    .mm-none { color: #065f46; }
    .mm-some { color: #991b1b; font-weight: 600; }
    h2 { font-size: 1.1rem; color: #1a2a3a; margin-bottom: 12px; }
    table {
      width: 100%; border-collapse: collapse; background: #fff;
      border-radius: 8px; overflow: hidden;
      box-shadow: 0 1px 4px rgba(0,0,0,0.1);
    }
    th {
      background: #1a2a3a; color: #fff; padding: 12px 16px;
      text-align: left; font-size: 0.85rem; letter-spacing: 0.03em;
    }
    th.right, td.right { text-align: right; }
    td { padding: 10px 16px; border-bottom: 1px solid #f0f2f5; font-size: 0.9rem; }
    tr:last-child td { border-bottom: none; }
    tr:hover td { background: #f8f9fa; }
    .ratio-col { font-weight: 600; }
    .notes {
      margin-top: 16px; background: #fff8e1;
      border-left: 4px solid #f59e0b; padding: 12px 16px;
      border-radius: 4px; font-size: 0.9rem;
    }
    .error-box {
      background: #fee2e2; border-left: 4px solid #dc2626;
      padding: 16px; border-radius: 4px; color: #991b1b;
    }
    .empty-box {
      background: #fff8e1; border-left: 4px solid #f59e0b;
      padding: 16px; border-radius: 4px; color: #92400e;
    }
"""


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


def _render_report(data: dict) -> str:
    """Render the HTML string for benchmark data."""
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    agg       = data.get("aggregate", {})
    ray_count = data.get("ray_count", "?")
    fixtures  = data.get("fixtures", [])

    native_ms  = agg.get("native_ms",  0.0)
    imported_ms = agg.get("imported_ms", 0.0)
    ratio      = agg.get("ratio",      0.0)
    total_mm   = agg.get("mismatches", 0)
    exp_fail   = agg.get("expected_failures", 0)

    ratio_css = "ratio-good" if ratio >= 1.0 else "ratio-bad"
    ratio_str = f"{ratio:.3f}" if ratio else "N/A"
    mm_css    = "mm-none" if total_mm == 0 else "mm-some"

    agg_html = (
        "    <div class=\"aggregate\">\n"
        f"      <div class=\"agg-card\">\n"
        f"        <div class=\"agg-label\">Native (aggregate)</div>\n"
        f"        <div class=\"agg-value\">{native_ms:.1f}"
        f"<span class=\"agg-unit\"> ms</span></div>\n"
        f"      </div>\n"
        f"      <div class=\"agg-card\">\n"
        f"        <div class=\"agg-label\">Imported (aggregate)</div>\n"
        f"        <div class=\"agg-value\">{imported_ms:.1f}"
        f"<span class=\"agg-unit\"> ms</span></div>\n"
        f"      </div>\n"
        f"      <div class=\"agg-card\">\n"
        f"        <div class=\"agg-label\">Native / Imported Ratio</div>\n"
        f"        <div class=\"agg-value {ratio_css}\">{ratio_str}</div>\n"
        f"      </div>\n"
        f"      <div class=\"agg-card\">\n"
        f"        <div class=\"agg-label\">Total Mismatches</div>\n"
        f"        <div class=\"agg-value {mm_css}\">{total_mm}</div>\n"
        f"      </div>\n"
        "    </div>\n"
    )

    if not fixtures:
        table_html = "    <div class=\"empty-box\">No fixture results found in output.</div>\n"
    else:
        rows = []
        for f in fixtures:
            fix_ratio     = (f["native_ms"] / f["imported_ms"]) if f["imported_ms"] > 0 else 0.0
            fix_ratio_css = "ratio-good" if fix_ratio >= 1.0 else "ratio-bad"
            fix_mm_css    = "mm-none" if f["mismatches"] == 0 else "mm-some"
            rows.append(
                "      <tr>\n"
                f"        <td>{escape(f['id'])}</td>\n"
                f"        <td>{escape(f['class'])}</td>\n"
                f"        <td class=\"right\">{f['native_ms']:.2f}</td>\n"
                f"        <td class=\"right\">{f['imported_ms']:.2f}</td>\n"
                f"        <td class=\"right ratio-col {fix_ratio_css}\">{fix_ratio:.3f}</td>\n"
                f"        <td class=\"right {fix_mm_css}\">{f['mismatches']}</td>\n"
                "      </tr>"
            )
        table_html = (
            "    <h2>Per-Fixture Results</h2>\n"
            "    <table>\n"
            "      <thead>\n"
            "        <tr>\n"
            "          <th>Fixture</th>\n"
            "          <th>Geant4 Class</th>\n"
            "          <th class=\"right\">Native (ms)</th>\n"
            "          <th class=\"right\">Imported (ms)</th>\n"
            "          <th class=\"right\">Ratio</th>\n"
            "          <th class=\"right\">Mismatches</th>\n"
            "        </tr>\n"
            "      </thead>\n"
            "      <tbody>\n"
            + "\n".join(rows) + "\n"
            "      </tbody>\n"
            "    </table>\n"
        )

    note_html = ""
    if exp_fail:
        note_html = (
            "    <div class=\"notes\">\n"
            f"      Note: {exp_fail} expected failure(s) are included in these results.\n"
            "      Mismatches from expected-failure fixtures are reclassified and do not cause errors.\n"
            "    </div>\n"
        )

    return (
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>G4OCCT \u2014 Benchmark Results</title>\n"
        f"  <style>\n{_CSS}\n  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div class=\"container\">\n"
        "    <h1>G4OCCT Benchmark Results</h1>\n"
        f"    <p class=\"meta\">Generated: {timestamp}"
        + (f" \u00b7 Rays per fixture: {ray_count}" if ray_count else "")
        + "</p>\n"
        + agg_html
        + table_html
        + note_html
        + "  </div>\n"
        "</body>\n"
        "</html>\n"
    )


def _render_error(message: str) -> str:
    """Render a minimal HTML error page."""
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    return (
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <title>G4OCCT \u2014 Benchmark Results</title>\n"
        f"  <style>\n{_CSS}\n  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div class=\"container\">\n"
        "    <h1>G4OCCT Benchmark Results</h1>\n"
        f"    <p class=\"meta\">Generated: {timestamp}</p>\n"
        f"    <div class=\"error-box\">Could not generate report: {escape(message)}</div>\n"
        "  </div>\n"
        "</body>\n"
        "</html>\n"
    )


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <bench-results.txt> <output.html>", file=sys.stderr)
        sys.exit(1)

    txt_path, html_path = sys.argv[1], sys.argv[2]
    Path(html_path).parent.mkdir(parents=True, exist_ok=True)

    try:
        text = Path(txt_path).read_text(encoding="utf-8")
    except (FileNotFoundError, OSError) as exc:
        print(f"Warning: {exc}", file=sys.stderr)
        Path(html_path).write_text(_render_error(str(exc)), encoding="utf-8")
        print(f"Benchmark report written to: {html_path}")
        return

    data = _parse_bench_output(text)
    html = _render_report(data)
    Path(html_path).write_text(html, encoding="utf-8")
    print(f"Benchmark report written to: {html_path}")


if __name__ == "__main__":
    main()
