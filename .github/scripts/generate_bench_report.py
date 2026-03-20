# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

"""Convert bench_navigator JSON output to a Markdown report."""

import json
import re
import sys
from pathlib import Path
from urllib.parse import quote

from report_utils import md_escape, timestamp, write_report


def _format_timing(ms: float | None, precision: int = 1) -> str:
    """Format a millisecond value for a Markdown cell; returns '---' for None."""
    return "---" if ms is None else f"{ms:.{precision}f}"


def _fmt_ratio(native_ms: float | None, imported_ms: float | None) -> str:
    """Compute a ratio string, e.g. '1.9x', or '---' when undefined."""
    if native_ms is None or native_ms <= 0.0:
        return "---"
    if imported_ms is None:
        return "---"
    return f"{imported_ms / native_ms:.1f}x"


def _get_ctr(bm: dict, key: str, default: float = 0.0) -> float:
    """Return a Google Benchmark counter value from a benchmark entry.

    Google Benchmark's JSON reporter typically writes user counters as top-level
    fields in each benchmark entry.  Older or differently-configured reporters
    may nest them under a ``"counters"`` object, so we check the top level first
    and fall back to that nested location.
    """
    if key in bm:
        return float(bm[key])
    return float(bm.get("counters", {}).get(key, default))


_BENCH_SUFFIX_RE = re.compile(r"(/iterations:\d+|/manual_time|/real_time)+$")


def _parse_benchmark_name(name: str):
    """Parse ``BM_<method>/<family>/<fixture_id>`` → ``(method, fixture_id)``.

    Google Benchmark appends suffixes such as ``/iterations:1`` and
    ``/manual_time`` (or ``/real_time``) when ``Iterations()`` and
    ``UseManualTime()`` (or ``UseRealTime()``) are configured.  These suffixes
    are stripped so that the returned ``fixture_id`` matches the bare
    ``<family>/<fixture_id>`` used by the point-cloud viewer.
    """
    if not name.startswith("BM_"):
        return None, None
    rest  = name[3:]
    slash = rest.find("/")
    if slash < 0:
        return None, None
    fixture_id = _BENCH_SUFFIX_RE.sub("", rest[slash + 1:])
    return rest[:slash], fixture_id


def _parse_bench_json(data: dict) -> dict:
    """Parse Google Benchmark JSON output into a structured dict for rendering.

    The JSON is produced by ``bench_navigator`` with ``--benchmark_out_format=json``.
    Custom context keys ``ray_count``, ``inside_count``, and ``safety_count`` are
    added by ``bench_navigator`` via ``benchmark::AddCustomContext()``.

    Benchmark name format: ``BM_<method>/<family>/<fixture_id>``
    Methods: ``rays``, ``inside``, ``safety``, ``polyhedron``
    """
    context      = data.get("context", {})

    def _parse_count(key: str) -> int | None:
        val = context.get(key)
        if val is None:
            return None
        try:
            return int(val)
        except (ValueError, TypeError):
            return None

    ray_count    = _parse_count("ray_count")
    inside_count = _parse_count("inside_count")
    safety_count = _parse_count("safety_count")

    # Group benchmark entries by fixture_id, preserving first-seen order.
    fixture_order: list[str] = []
    fixture_groups: dict[str, dict] = {}

    for bm in data.get("benchmarks", []):
        if bm.get("run_type") == "aggregate":
            continue
        if "name" not in bm:
            print(f"Warning: benchmark entry missing 'name' field: {bm}", file=sys.stderr)
            continue
        method, fixture_id = _parse_benchmark_name(bm["name"])
        if method is None:
            continue
        if fixture_id not in fixture_groups:
            fixture_order.append(fixture_id)
            fixture_groups[fixture_id] = {}
        fixture_groups[fixture_id][method] = bm

    # Per-fixture structured list + running aggregate totals.
    fixtures: list[dict] = []
    agg_ray_native_ms      = 0.0
    agg_ray_imported_ms    = 0.0
    agg_ray_mismatches     = 0.0
    agg_exit_mismatches    = 0.0
    agg_sn_native_ms       = 0.0
    agg_sn_imported_ms     = 0.0
    agg_sn_mismatches      = 0.0
    agg_inside_native_ms   = 0.0
    agg_inside_imported_ms = 0.0
    agg_inside_mismatches  = 0.0
    agg_dti_native_ms      = 0.0
    agg_dti_imported_ms    = 0.0
    agg_dti_exact_ms       = 0.0
    agg_dti_lb_violations  = 0.0
    agg_dto_native_ms      = 0.0
    agg_dto_imported_ms    = 0.0
    agg_dto_exact_ms       = 0.0
    agg_dto_lb_violations  = 0.0
    agg_poly_native_ms     = 0.0
    agg_poly_imported_ms   = 0.0

    for fixture_id in fixture_order:
        group     = fixture_groups[fixture_id]
        rays_bm   = group.get("rays",       {})
        inside_bm = group.get("inside",     {})
        safety_bm = group.get("safety",     {})
        poly_bm   = group.get("polyhedron", {})

        geant4_class         = rays_bm.get("label", "") if rays_bm else ""
        has_expected_failure = (_get_ctr(rays_bm, "has_expected_failure") != 0.0
                                if rays_bm else False)

        methods: dict[str, dict] = {}

        if rays_bm:
            n = _get_ctr(rays_bm, "native_ms")
            i = _get_ctr(rays_bm, "imported_ms")
            methods["DistanceToIn/Out(p,v)"] = {
                "native_ms":   n,
                "imported_ms": i,
                "ratio":       _fmt_ratio(n, i),
                "mismatches":  int(_get_ctr(rays_bm, "mismatches")),
            }
            methods["Exit normals"] = {
                "native_ms":   None,
                "imported_ms": None,
                "ratio":       "---",
                "mismatches":  int(_get_ctr(rays_bm, "exit_normal_mismatches")),
            }
            sn_n = _get_ctr(rays_bm, "sn_native_ms")
            sn_i = _get_ctr(rays_bm, "sn_imported_ms")
            methods["SurfaceNormal(p)"] = {
                "native_ms":   sn_n,
                "imported_ms": sn_i,
                "ratio":       _fmt_ratio(sn_n, sn_i),
                "mismatches":  int(_get_ctr(rays_bm, "sn_mismatches")),
            }
            agg_ray_native_ms   += n
            agg_ray_imported_ms += i
            agg_ray_mismatches  += _get_ctr(rays_bm, "mismatches")
            agg_exit_mismatches += _get_ctr(rays_bm, "exit_normal_mismatches")
            agg_sn_native_ms    += sn_n
            agg_sn_imported_ms  += sn_i
            agg_sn_mismatches   += _get_ctr(rays_bm, "sn_mismatches")

        if inside_bm:
            n = _get_ctr(inside_bm, "native_ms")
            i = _get_ctr(inside_bm, "imported_ms")
            methods["Inside(p)"] = {
                "native_ms":   n,
                "imported_ms": i,
                "ratio":       _fmt_ratio(n, i),
                "mismatches":  int(_get_ctr(inside_bm, "mismatches")),
            }
            agg_inside_native_ms   += n
            agg_inside_imported_ms += i
            agg_inside_mismatches  += _get_ctr(inside_bm, "mismatches")

        if safety_bm:
            dti_n   = _get_ctr(safety_bm, "safety_in_native_ms")
            dti_i   = _get_ctr(safety_bm, "safety_in_imported_ms")
            dti_e   = _get_ctr(safety_bm, "safety_in_exact_ms")
            dto_n   = _get_ctr(safety_bm, "safety_out_native_ms")
            dto_i   = _get_ctr(safety_bm, "safety_out_imported_ms")
            dto_e   = _get_ctr(safety_bm, "safety_out_exact_ms")
            dti_lbv = _get_ctr(safety_bm, "safety_in_lb_violations")
            dto_lbv = _get_ctr(safety_bm, "safety_out_lb_violations")
            dti_lb_r   = _get_ctr(safety_bm, "safety_in_avg_lb_ratio")
            dto_lb_r   = _get_ctr(safety_bm, "safety_out_avg_lb_ratio")
            dti_g4r    = _get_ctr(safety_bm, "safety_in_avg_g4_occt_ratio")
            dto_g4r    = _get_ctr(safety_bm, "safety_out_avg_g4_occt_ratio")
            methods["DistanceToIn(p)"] = {
                "native_ms":         dti_n,
                "imported_ms":       dti_i,
                "ratio":             _fmt_ratio(dti_n, dti_i),
                "avg_g4_occt_ratio": dti_g4r,
            }
            methods["DistanceToOut(p)"] = {
                "native_ms":         dto_n,
                "imported_ms":       dto_i,
                "ratio":             _fmt_ratio(dto_n, dto_i),
                "avg_g4_occt_ratio": dto_g4r,
            }
            methods["DistanceToIn(p) OCCT/Exact"] = {
                "native_ms":   dti_i,
                "imported_ms": dti_e,
                "ratio":       _fmt_ratio(dti_i, dti_e),
                "avg_lb_ratio":  dti_lb_r,
                "lb_violations": int(dti_lbv),
            }
            methods["DistanceToOut(p) OCCT/Exact"] = {
                "native_ms":   dto_i,
                "imported_ms": dto_e,
                "ratio":       _fmt_ratio(dto_i, dto_e),
                "avg_lb_ratio":  dto_lb_r,
                "lb_violations": int(dto_lbv),
            }
            agg_dti_native_ms     += dti_n
            agg_dti_imported_ms   += dti_i
            agg_dti_exact_ms      += dti_e
            agg_dti_lb_violations += dti_lbv
            agg_dto_native_ms     += dto_n
            agg_dto_imported_ms   += dto_i
            agg_dto_exact_ms      += dto_e
            agg_dto_lb_violations += dto_lbv

        if poly_bm:
            p_n = _get_ctr(poly_bm, "native_ms")
            p_i = _get_ctr(poly_bm, "imported_ms")
            methods["CreatePolyhedron()"] = {
                "native_ms":          p_n,
                "imported_ms":        p_i,
                "ratio":              _fmt_ratio(p_n, p_i),
                "native_vertices":    int(_get_ctr(poly_bm, "native_vertices")),
                "imported_vertices":  int(_get_ctr(poly_bm, "imported_vertices")),
                "native_facets":      int(_get_ctr(poly_bm, "native_facets")),
                "imported_facets":    int(_get_ctr(poly_bm, "imported_facets")),
            }
            agg_poly_native_ms  += p_n
            agg_poly_imported_ms += p_i

        fixtures.append({
            "id":                   fixture_id,
            "class":                geant4_class,
            "has_expected_failure": has_expected_failure,
            "methods":              methods,
        })

    # Return empty aggregate when no fixture data was found so _render_report()
    # shows the "No aggregate/fixture results" warning instead of a zero-filled table.
    if not fixture_order:
        return {
            "ray_count":    ray_count,
            "inside_count": inside_count,
            "safety_count": safety_count,
            "fixtures":     [],
            "aggregate":    [],
        }

    # Build aggregate rows in the canonical display order.
    aggregate = [
        {
            "method":       "DistanceToIn/Out(p,v)",
            "native_ms":    agg_ray_native_ms,
            "imported_ms":  agg_ray_imported_ms,
            "ratio":        _fmt_ratio(agg_ray_native_ms, agg_ray_imported_ms),
            "mismatches":   int(agg_ray_mismatches),
            "exp_failures": None,
        },
        {
            "method":       "Exit normals",
            "native_ms":    None,
            "imported_ms":  None,
            "ratio":        "---",
            "mismatches":   int(agg_exit_mismatches),
            "exp_failures": None,
        },
        {
            "method":       "Inside(p)",
            "native_ms":    agg_inside_native_ms,
            "imported_ms":  agg_inside_imported_ms,
            "ratio":        _fmt_ratio(agg_inside_native_ms, agg_inside_imported_ms),
            "mismatches":   int(agg_inside_mismatches),
            "exp_failures": None,
        },
        {
            "method":       "DistanceToIn(p)",
            "native_ms":    agg_dti_native_ms,
            "imported_ms":  agg_dti_imported_ms,
            "ratio":        _fmt_ratio(agg_dti_native_ms, agg_dti_imported_ms),
            "mismatches":   None,
            "exp_failures": None,
            "extra":        "G4 vs OCCT timing",
        },
        {
            "method":       "DistanceToOut(p)",
            "native_ms":    agg_dto_native_ms,
            "imported_ms":  agg_dto_imported_ms,
            "ratio":        _fmt_ratio(agg_dto_native_ms, agg_dto_imported_ms),
            "mismatches":   None,
            "exp_failures": None,
            "extra":        "G4 vs OCCT timing",
        },
        {
            "method":       "DistanceToIn(p) OCCT/Exact",
            "native_ms":    agg_dti_imported_ms,
            "imported_ms":  agg_dti_exact_ms,
            "ratio":        _fmt_ratio(agg_dti_imported_ms, agg_dti_exact_ms),
            "mismatches":   int(agg_dti_lb_violations),
            "exp_failures": None,
            "extra":        "OCCT lower bound vs exact",
        },
        {
            "method":       "DistanceToOut(p) OCCT/Exact",
            "native_ms":    agg_dto_imported_ms,
            "imported_ms":  agg_dto_exact_ms,
            "ratio":        _fmt_ratio(agg_dto_imported_ms, agg_dto_exact_ms),
            "mismatches":   int(agg_dto_lb_violations),
            "exp_failures": None,
            "extra":        "OCCT lower bound vs exact",
        },
        {
            "method":       "SurfaceNormal(p)",
            "native_ms":    agg_sn_native_ms,
            "imported_ms":  agg_sn_imported_ms,
            "ratio":        _fmt_ratio(agg_sn_native_ms, agg_sn_imported_ms),
            "mismatches":   int(agg_sn_mismatches),
            "exp_failures": None,
        },
        {
            "method":       "CreatePolyhedron()",
            "native_ms":    agg_poly_native_ms,
            "imported_ms":  agg_poly_imported_ms,
            "ratio":        _fmt_ratio(agg_poly_native_ms, agg_poly_imported_ms),
            "mismatches":   None,
            "exp_failures": None,
        },
    ]

    return {
        "ray_count":    ray_count,
        "inside_count": inside_count,
        "safety_count": safety_count,
        "fixtures":     fixtures,
        "aggregate":    aggregate,
    }


def _fixture_viewer_link(fixture_id: str, viewer_path: str) -> str:
    """Return a point-cloud viewer deep link for one fixture."""
    return f"{viewer_path}?fixture={quote(fixture_id, safe='')}"


def _render_report(data: dict, viewer_path: str,
                   onshape_links: dict[str, str] | None = None) -> str:
    """Render the Markdown string for benchmark data."""
    ts           = timestamp()
    aggregate    = data.get("aggregate", [])
    ray_count    = data.get("ray_count")
    inside_count = data.get("inside_count")
    safety_count = data.get("safety_count")
    fixtures     = data.get("fixtures", [])

    meta_line = f"Generated: {ts}"
    if ray_count is not None:
        if inside_count == ray_count and safety_count == ray_count:
            meta_line += f" · Rays/points per fixture: {ray_count}"
        else:
            parts = [f"Rays: {ray_count}"]
            if inside_count is not None:
                parts.append(f"inside points: {inside_count}")
            if safety_count is not None:
                parts.append(f"safety points: {safety_count}")
            meta_line += " · " + ", ".join(parts)

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
            "| Method | Col A (ms) | Col B (ms) | Ratio | Violations | Exp. Failures | Notes |",
            "|--------|----------:|-----------:|------:|-----------:|--------------:|-------|",
        ]
        total_exp_failures = 0
        for row in aggregate:
            native_str   = _format_timing(row["native_ms"])
            imported_str = _format_timing(row["imported_ms"])
            mm_str  = "---" if row["mismatches"] is None else str(row["mismatches"])
            ef_str  = "---" if row["exp_failures"] is None else str(row["exp_failures"])
            extra   = row.get("extra", "")
            lines.append(
                f"| {md_escape(row['method'])} | {native_str} | {imported_str} "
                f"| {row['ratio']} | {mm_str} | {ef_str} | {extra} |"
            )
            # All methods report the same exp_failures count (one per expected-failure
            # fixture); take the maximum to get the fixture-level count.
            if row["exp_failures"] is not None and row["exp_failures"] > total_exp_failures:
                total_exp_failures = row["exp_failures"]

        lines += [
            "",
            "> **Column guide:** For most methods, **Col A** = Geant4/native (ms) and"
            " **Col B** = OCCT/imported (ms)."
            " For `OCCT/Exact` rows, **Col A** = OCCT lower-bound (ms) and"
            " **Col B** = OCCT exact (ms)."
            " **Violations** for `OCCT/Exact` rows counts points where the lower bound"
            " exceeded the exact distance (hard fail); `---` means not applicable.",
        ]

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
        # Ordered list of (column_header, method_key, has_timing, is_polyhedron, is_safety_occt_exact)
        method_columns = [
            ("DTI/DTO(p,v)",       "DistanceToIn/Out(p,v)",       True,  False, False),
            ("Exit normals",       "Exit normals",                 False, False, False),
            ("Inside(p)",          "Inside(p)",                    True,  False, False),
            ("DTI(p) G4↔OCCT",    "DistanceToIn(p)",              True,  False, False),
            ("DTI(p) OCCT↔Exact", "DistanceToIn(p) OCCT/Exact",   True,  False, True),
            ("DTO(p) G4↔OCCT",    "DistanceToOut(p)",             True,  False, False),
            ("DTO(p) OCCT↔Exact", "DistanceToOut(p) OCCT/Exact",  True,  False, True),
            ("SN(p)",              "SurfaceNormal(p)",             True,  False, False),
            ("Polyhedron",         "CreatePolyhedron()",           True,  True,  False),
        ]

        col_headers = " | ".join(h for h, _, _, _, _ in method_columns)
        col_sep     = "|".join(":---:" for _ in method_columns)

        onshape_note = (" 🔗 links open the fixture in the Onshape web viewer."
                        if onshape_links else "")
        lines += [
            "",
            "## Per-Fixture Results",
            "",
            "Each cell shows `native ms → imported ms (ratio)`."
            " ✅ = no violations; N ⚠️ = N lower-bound violations (hard fail)."
            " Exit normals has no separate timing (normals are computed as part of DistanceToOut)."
            " Column abbreviations: **DTI/DTO(p,v)** = DistanceToIn/Out(p,v),"
            " **DTI(p) G4↔OCCT** = DistanceToIn(p) Geant4 vs OCCT timing with avg distance ratio,"
            " **DTI(p) OCCT↔Exact** = OCCT lower-bound vs exact timing with avg lb ratio and violations,"
            " **DTO(p)** columns analogous to DTI(p),"
            " **SN(p)** = SurfaceNormal,"
            " **Polyhedron** = CreatePolyhedron() timing with native/imported vertex and facet counts."
            " Fixtures marked ⚠️ are expected failures and do not block CI."
            f"{onshape_note}",
            "",
            f"| Fixture | Geant4 Class | {col_headers} |",
            f"|---------|:-------------|{col_sep}|",
        ]

        for f in fixtures:
            cells = []
            for _, method_key, has_timing, is_polyhedron, is_safety_occt_exact in method_columns:
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
                elif is_safety_occt_exact:
                    # OCCT lower-bound vs exact: show timing, avg lb ratio, and violations.
                    n   = d.get("native_ms")
                    i   = d.get("imported_ms")
                    r   = d.get("ratio", "---")
                    lbr = d.get("avg_lb_ratio", 0.0)
                    lbv = d.get("lb_violations", 0)
                    viol_str = "✅" if lbv == 0 else f"{lbv} ⚠️"
                    n_str = _format_timing(n, precision=2)
                    i_str = _format_timing(i, precision=2)
                    cells.append(
                        f"{n_str} → {i_str} ({r})<br/>avg lb/exact: {lbr:.2f} [{viol_str}]"
                    )
                elif has_timing:
                    n   = d.get("native_ms")
                    i   = d.get("imported_ms")
                    r   = d.get("ratio", "---")
                    # G4↔OCCT safety rows: show timing and avg OCCT/G4 distance ratio.
                    g4r = d.get("avg_g4_occt_ratio")
                    n_str = _format_timing(n, precision=2)
                    i_str = _format_timing(i, precision=2)
                    if g4r is not None:
                        cells.append(f"{n_str} → {i_str} ({r})<br/>avg OCCT/G4: {g4r:.2f}")
                    else:
                        mm     = d.get("mismatches", 0)
                        mm_str = "✅" if mm == 0 else f"{mm} ⚠️"
                        cells.append(f"{n_str} → {i_str} ({r}) [{mm_str}]")
                else:
                    mm     = d.get("mismatches", 0)
                    mm_str = "✅" if mm == 0 else f"{mm} ⚠️"
                    cells.append(f"[{mm_str}]")

            fixture_link = _fixture_viewer_link(f["id"], viewer_path)
            ef_marker    = " ⚠️" if f["has_expected_failure"] else ""
            cell_str = " | ".join(cells)
            onshape_url  = onshape_links.get(f["id"]) if onshape_links else None
            onshape_badge = f" [🔗]({onshape_url})" if onshape_url else ""
            lines.append(
                f"| [{md_escape(f['id'])}]({fixture_link}){onshape_badge}{ef_marker} "
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
    if len(sys.argv) not in (3, 4, 5):
        print(
            f"Usage: {sys.argv[0]} <bench-results.json> <output.md> [viewer-path] [onshape-links.json]",
            file=sys.stderr,
        )
        sys.exit(1)

    json_path   = sys.argv[1]
    md_path     = sys.argv[2]
    viewer_path = sys.argv[3] if len(sys.argv) >= 4 else "point_cloud_viewer.html"

    onshape_links: dict[str, str] | None = None
    if len(sys.argv) == 5:
        links_path = Path(sys.argv[4])
        try:
            loaded_links = json.loads(links_path.read_text(encoding="utf-8"))
        except (FileNotFoundError, OSError, json.JSONDecodeError) as exc:
            print(f"Warning: could not load Onshape links from {links_path}: {exc}",
                  file=sys.stderr)
        else:
            if isinstance(loaded_links, dict):
                onshape_links = loaded_links
            else:
                print(
                    "Warning: ignoring Onshape links from "
                    f"{links_path}: expected JSON object at top level, "
                    f"got {type(loaded_links).__name__}",
                    file=sys.stderr,
                )

    try:
        raw = json.loads(Path(json_path).read_text(encoding="utf-8"))
    except (FileNotFoundError, OSError, json.JSONDecodeError) as exc:
        print(f"Warning: {exc}", file=sys.stderr)
        write_report(Path(md_path), _render_error(str(exc)), label="Benchmark report")
        return

    data = _parse_bench_json(raw)
    md   = _render_report(data, viewer_path, onshape_links)
    write_report(Path(md_path), md, label="Benchmark report")


if __name__ == "__main__":
    main()
