# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Convert bench_navigator text output to a Markdown report."""

import re
import sys
from pathlib import Path
from urllib.parse import quote

from report_utils import md_escape, timestamp, write_report


def _parse_timing_value(value: str) -> float | None:
    """Convert a timing string ('---' or a decimal) to float or None."""
    return None if value == "---" else float(value)


def _format_timing(ms: float | None, precision: int = 1) -> str:
    """Format a millisecond value for a Markdown cell; returns '---' for None."""
    return "---" if ms is None else f"{ms:.{precision}f}"


def _parse_bench_output(text: str) -> dict:
    """Parse bench_navigator stdout into a structured dict.

    Expected output format (bench_navigator.cc):

        === Fixture Navigation Benchmark Results ===
        Rays: 2048  |  Inside points: 2048  |  Safety points: 2048

        family/fixture_id (G4ClassName):
          DistanceToIn/Out(p,v)   : native=    1.23ms  imported=    4.56ms  ratio=    3.7x  mismatches=0
          Exit normals            : native=     ---ms  imported=     ---ms  ratio=     ---  mismatches=0
          Inside(p)               : native=    1.23ms  imported=    4.56ms  ratio=    3.7x  mismatches=0
          DistanceToIn(p)         : native=    1.23ms  imported=    4.56ms  ratio=    3.7x  mismatches=0
          DistanceToOut(p)        : native=    1.23ms  imported=    4.56ms  ratio=    3.7x  mismatches=0
          SurfaceNormal(p)        : native=    1.23ms  imported=    4.56ms  ratio=    3.7x  mismatches=0
          CreatePolyhedron()      : native=    1.23ms  imported=    4.56ms  ratio=    3.7x  vertices=8/12  facets=12/24

        Aggregate:
          Method                    Native(ms)  Imported(ms)     Ratio   Mismatches Exp. Failures
          ---------------------...
          DistanceToIn/Out(p,v)        1234.56       5678.90       4.6x           12             0
          ...
          CreatePolyhedron()           1234.56       5678.90       4.6x          ---           ---
    """
    lines = text.splitlines()

    in_results   = False
    in_aggregate = False
    ray_count    = None
    current      = None   # fixture dict being built
    fixtures     = []
    aggregate    = []     # list of {"method", "native_ms", "imported_ms", "ratio", "mismatches", "exp_failures"}

    for line in lines:
        line = line.rstrip()

        # ── Section start ────────────────────────────────────────────────
        if line == "=== Fixture Navigation Benchmark Results ===":
            in_results   = True
            in_aggregate = False
            current      = None
            continue

        if not in_results:
            continue

        # ── Ray / point counts ───────────────────────────────────────────
        m = re.match(r"Rays:\s*(\d+)\s*\|", line)
        if m:
            ray_count = int(m.group(1))
            continue

        # ── Aggregate section ────────────────────────────────────────────
        if line.rstrip() == "Aggregate:":
            in_aggregate = True
            current      = None
            continue

        if in_aggregate:
            # Skip the header row and the separator line.
            if "Method" in line or re.match(r"\s+-{10,}", line):
                continue
            # Each data row: 2-space indent + 24-char left-justified label + numbers.
            if line.startswith("  ") and len(line) > 26:
                label = line[2:26].strip()
                rest  = line[26:].split()
                if label and len(rest) >= 4:
                    try:
                        native_ms    = _parse_timing_value(rest[0])
                        imported_ms  = _parse_timing_value(rest[1])
                        ratio        = rest[2]          # "3.7x" or "---"
                        # mismatches and exp_failures may be "---" for CreatePolyhedron()
                        mismatches   = None if rest[3] == "---" else int(rest[3])
                        exp_failures = None if (len(rest) <= 4 or rest[4] == "---") else int(rest[4])
                        aggregate.append({
                            "method":       label,
                            "native_ms":    native_ms,
                            "imported_ms":  imported_ms,
                            "ratio":        ratio,
                            "mismatches":   mismatches,
                            "exp_failures": exp_failures,
                        })
                    except (ValueError, IndexError):
                        print(f"Warning: could not parse aggregate row: {line!r}", file=sys.stderr)
            continue

        # ── Per-fixture header ───────────────────────────────────────────
        # Format: "family/id (G4Class):" or "family/id (G4Class)  [expected failure]:"
        m = re.match(r"^(.+?)\s+\((.+?)\)(\s+\[expected failure\])?:\s*$", line)
        if m:
            current = {
                "id":                   m.group(1).strip(),
                "class":                m.group(2).strip(),
                "has_expected_failure": bool(m.group(3)),
                "methods":              {},
            }
            fixtures.append(current)
            continue

        # ── Per-fixture method row ───────────────────────────────────────
        if current is not None:
            # CreatePolyhedron() row has a different format: vertices=N/M  facets=N/M
            # instead of mismatches=M.
            m = re.match(
                r"  (.+?)\s*: native=\s*([\d.]+|---)\s*ms\s+"
                r"imported=\s*([\d.]+|---)\s*ms\s+"
                r"ratio=\s*([\d.]+x?|---)\s+"
                r"vertices=(\d+)/(\d+)\s+"
                r"facets=(\d+)/(\d+)",
                line,
            )
            if m:
                method = m.group(1).strip()
                current["methods"][method] = {
                    "native_ms":        _parse_timing_value(m.group(2)),
                    "imported_ms":      _parse_timing_value(m.group(3)),
                    "ratio":            m.group(4),
                    "native_vertices":  int(m.group(5)),
                    "imported_vertices": int(m.group(6)),
                    "native_facets":    int(m.group(7)),
                    "imported_facets":  int(m.group(8)),
                }
                continue

            # Standard row format: "  MethodName : native=X.XXms  imported=Y.YYms  ratio=Z.Zx  mismatches=M"
            m = re.match(
                r"  (.+?)\s*: native=\s*([\d.]+|---)\s*ms\s+"
                r"imported=\s*([\d.]+|---)\s*ms\s+"
                r"ratio=\s*([\d.]+x?|---)\s+"
                r"mismatches=(\d+)",
                line,
            )
            if m:
                method      = m.group(1).strip()
                native_ms   = _parse_timing_value(m.group(2))
                imported_ms = _parse_timing_value(m.group(3))
                ratio       = m.group(4)   # "3.7x" or "---"
                mismatches  = int(m.group(5))
                current["methods"][method] = {
                    "native_ms":   native_ms,
                    "imported_ms": imported_ms,
                    "ratio":       ratio,
                    "mismatches":  mismatches,
                }
                continue

        # ── Blank line ends the current fixture block ────────────────────
        if line == "":
            current = None

    return {"ray_count": ray_count, "fixtures": fixtures, "aggregate": aggregate}


def _fixture_viewer_link(fixture_id: str, viewer_path: str) -> str:
    """Return a point-cloud viewer deep link for one fixture."""
    return f"{viewer_path}?fixture={quote(fixture_id, safe='')}"


def _render_report(data: dict, viewer_path: str) -> str:
    """Render the Markdown string for benchmark data."""
    ts        = timestamp()
    aggregate = data.get("aggregate", [])
    ray_count = data.get("ray_count", "?")
    fixtures  = data.get("fixtures", [])

    meta_line = f"Generated: {ts}"
    if ray_count:
        meta_line += f" · Rays/points per fixture: {ray_count}"

    lines = [
        "# G4OCCT Benchmark Results",
        "",
        meta_line,
        "",
        f"[Open point-cloud viewer]({viewer_path})",
        "",
        "## Aggregate Results",
    ]

    if not aggregate:
        lines += [
            "",
            "> ⚠️ No aggregate results found in output.",
        ]
    else:
        lines += [
            "",
            "| Method | Native (ms) | Imported (ms) | Ratio | Mismatches | Exp. Failures |",
            "|--------|------------:|--------------:|------:|-----------:|--------------:|",
        ]
        total_exp_failures = 0
        for row in aggregate:
            native_str   = _format_timing(row["native_ms"])
            imported_str = _format_timing(row["imported_ms"])
            mm_str  = "---" if row["mismatches"] is None else str(row["mismatches"])
            ef_str  = "---" if row["exp_failures"] is None else str(row["exp_failures"])
            lines.append(
                f"| {md_escape(row['method'])} | {native_str} | {imported_str} "
                f"| {row['ratio']} | {mm_str} | {ef_str} |"
            )
            # All methods report the same exp_failures count (one per expected-failure
            # fixture); take the maximum to get the fixture-level count.
            if row["exp_failures"] is not None and row["exp_failures"] > total_exp_failures:
                total_exp_failures = row["exp_failures"]

        if total_exp_failures:
            lines += [
                "",
                f"> **Note:** Up to {total_exp_failures} expected failure(s) are included in these "
                "results. Mismatches from expected-failure fixtures are reclassified and do not "
                "cause errors.",
            ]

    if not fixtures:
        lines += [
            "",
            "> ⚠️ No fixture results found in output.",
        ]
    else:
        # Ordered list of (column_header, method_key, has_timing, is_polyhedron)
        method_columns = [
            ("DTI/DTO(p,v)",   "DistanceToIn/Out(p,v)", True,  False),
            ("Exit normals",   "Exit normals",           False, False),
            ("Inside(p)",      "Inside(p)",              True,  False),
            ("DTI(p)",         "DistanceToIn(p)",        True,  False),
            ("DTO(p)",         "DistanceToOut(p)",       True,  False),
            ("SN(p)",          "SurfaceNormal(p)",       True,  False),
            ("Polyhedron",     "CreatePolyhedron()",     True,  True),
        ]

        col_headers = " | ".join(h for h, _, _, _ in method_columns)
        col_sep     = "|".join(":---:" for _ in method_columns)

        lines += [
            "",
            "## Per-Fixture Results",
            "",
            "Each cell shows `native ms → imported ms (ratio) [mismatches]`."
            " ✅ = no mismatches; N ⚠️ = N result mismatches between native and imported."
            " Exit normals has no separate timing (normals are computed as part of DistanceToOut)."
            " Column abbreviations: **DTI/DTO(p,v)** = DistanceToIn/Out(p,v),"
            " **DTI(p)** = DistanceToIn safety, **DTO(p)** = DistanceToOut safety,"
            " **SN(p)** = SurfaceNormal,"
            " **Polyhedron** = CreatePolyhedron() timing with native/imported vertex and facet counts."
            " Fixtures marked ⚠️ are expected failures and do not block CI.",
            "",
            f"| Fixture | Geant4 Class | {col_headers} |",
            f"|---------|:-------------|{col_sep}|",
        ]

        for f in fixtures:
            cells = []
            for _, method_key, has_timing, is_polyhedron in method_columns:
                d = f["methods"].get(method_key)
                if d is None:
                    cells.append("---")
                    continue
                if is_polyhedron:
                    # CreatePolyhedron() has no mismatch; show timing + mesh counts.
                    n = d.get("native_ms")
                    i = d.get("imported_ms")
                    r = d.get("ratio", "---")
                    nv             = d.get("native_vertices", 0)
                    iv             = d.get("imported_vertices", 0)
                    nf             = d.get("native_facets", 0)
                    imported_facets = d.get("imported_facets", 0)
                    n_str = _format_timing(n, precision=2)
                    i_str = _format_timing(i, precision=2)
                    cells.append(f"{n_str} → {i_str} ({r})<br/>{nv}/{iv} verts, {nf}/{imported_facets} facets")
                elif has_timing:
                    mm     = d.get("mismatches", 0)
                    mm_str = "✅" if mm == 0 else f"{mm} ⚠️"
                    n = d.get("native_ms")
                    i = d.get("imported_ms")
                    r = d.get("ratio", "---")
                    n_str = _format_timing(n, precision=2)
                    i_str = _format_timing(i, precision=2)
                    cells.append(f"{n_str} → {i_str} ({r}) [{mm_str}]")
                else:
                    mm     = d.get("mismatches", 0)
                    mm_str = "✅" if mm == 0 else f"{mm} ⚠️"
                    cells.append(f"[{mm_str}]")

            fixture_link = _fixture_viewer_link(f["id"], viewer_path)
            ef_marker    = " ⚠️" if f["has_expected_failure"] else ""
            cell_str = " | ".join(cells)
            lines.append(
                f"| [{md_escape(f['id'])}]({fixture_link}){ef_marker} "
                f"| {md_escape(f['class'])} | {cell_str} |"
            )

    return "\n".join(lines) + "\n"


def _render_error(message: str) -> str:
    """Render a minimal Markdown error report."""
    return (
        "# G4OCCT Benchmark Results\n\n"
        f"Generated: {timestamp()}\n\n"
        f"❌ Could not generate report: {message}\n"
    )


def main() -> None:
    if len(sys.argv) not in (3, 4):
        print(f"Usage: {sys.argv[0]} <bench-results.txt> <output.md> [viewer-path]",
              file=sys.stderr)
        sys.exit(1)

    txt_path, md_path = sys.argv[1], sys.argv[2]
    viewer_path       = sys.argv[3] if len(sys.argv) == 4 else "point_cloud_viewer.html"

    try:
        text = Path(txt_path).read_text(encoding="utf-8")
    except (FileNotFoundError, OSError) as exc:
        print(f"Warning: {exc}", file=sys.stderr)
        write_report(Path(md_path), _render_error(str(exc)), label="Benchmark report")
        return

    data = _parse_bench_output(text)
    md   = _render_report(data, viewer_path)
    write_report(Path(md_path), md, label="Benchmark report")


if __name__ == "__main__":
    main()
