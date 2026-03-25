# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

"""Convert bench_navigator JSON output to a Markdown report."""

import json
import math
import re
import sys
import xml.sax.saxutils
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


# Methods shown in the timing chart.
# Tuple: (short_label, method_key, native_colour, imported_colour)
# "Exit normals" is omitted (no timing); OCCT/Exact rows are omitted (different semantics).
_CHART_METHODS: list[tuple[str, str, str, str]] = [
    ("DTI/DTO(p,v)",    "DistanceToIn/Out(p,v)", "#4c9fde", "#1e6faa"),
    ("Inside(p)",       "Inside(p)",              "#7ecf4c", "#3a8f1a"),
    ("DTI(p) G4↔OCCT", "DistanceToIn(p)",        "#f0a030", "#b06010"),
    ("DTO(p) G4↔OCCT", "DistanceToOut(p)",       "#e05050", "#a02020"),
    ("SN(p)",           "SurfaceNormal(p)",       "#b050e0", "#7010b0"),
    ("Polyhedron",      "CreatePolyhedron()",     "#40d0c0", "#108080"),
]

# Chart layout constants (pixels).
_C_LABEL_W    = 250   # width reserved for fixture-ID labels (left side)
_C_BAR_AREA_W = 760   # width of the bar-drawing area
_C_RIGHT_PAD  = 20    # padding to the right of bars
_C_LEGEND_H   = 52    # height of the legend block at the top
_C_X_AXIS_H   = 26    # height of the x-axis + tick-label row above bars
_C_BOTTOM_PAD = 20    # padding below the bottom axis
_C_BAR_H      = 5     # height of one bar (native or imported)
_C_BAR_GAP    = 1     # vertical gap between native and imported bar within a pair
_C_METH_GAP   = 3     # vertical gap between successive method pairs
_C_FIX_GAP    = 10    # vertical gap between fixture groups

_C_BG         = "#1a1a2e"
_C_AXIS_CLR   = "#555"
_C_GRID_CLR   = "#2a2a4a"
_C_LABEL_CLR  = "#e0e0e0"
_C_MUTED_CLR  = "#888"
_C_FONT       = "-apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"


def _render_chart_svg(fixtures: list[dict]) -> str:
    """Render a horizontal grouped bar chart of per-fixture timings as an SVG string.

    Each fixture is a row group.  Within each group there is one pair of bars
    (native on top, imported below) for every method in ``_CHART_METHODS``.
    The x-axis uses a log₁₀ scale so that fixtures with very different absolute
    timings are still distinguishable.

    Returns an empty string when *fixtures* is empty or contains no timing data.
    """
    if not fixtures:
        return ""

    # ── derived dimensions ────────────────────────────────────────────────────
    n_m      = len(_CHART_METHODS)
    pair_h   = _C_BAR_H + _C_BAR_GAP + _C_BAR_H
    group_h  = pair_h * n_m + _C_METH_GAP * (n_m - 1)
    n_f      = len(fixtures)
    chart_h  = group_h * n_f + _C_FIX_GAP * max(n_f - 1, 0)
    top_off  = _C_LEGEND_H + _C_X_AXIS_H   # y-coordinate where bars start
    total_h  = top_off + chart_h + _C_BOTTOM_PAD
    total_w  = _C_LABEL_W + _C_BAR_AREA_W + _C_RIGHT_PAD

    # ── log₁₀ x-axis scale ───────────────────────────────────────────────────
    all_vals: list[float] = []
    for fix in fixtures:
        for _, mk, _, _ in _CHART_METHODS:
            d = fix["methods"].get(mk)
            if d:
                for key in ("native_ms", "imported_ms"):
                    v = d.get(key) or 0.0
                    if v > 0:
                        all_vals.append(v)

    if not all_vals:
        return ""

    max_ms    = max(all_vals)
    min_ms    = min(all_vals)
    # Extend the range slightly so bars don't touch the axis edges.
    x_min_log = math.floor(math.log10(min_ms)) - 0.3
    x_max_log = math.log10(max_ms) + 0.3
    # Ensure a minimum visible span so the denominator in _log_x is never zero
    # or negative (which would invert the scale or produce invalid SVG).
    if x_max_log <= x_min_log:
        x_max_log = x_min_log + 1.0

    def _log_x(ms: float) -> float:
        """Map an ms value → pixel offset from the left edge of the bar area."""
        if ms <= 0:
            return 0.0
        clamped = max(ms, 10 ** x_min_log)
        return (math.log10(clamped) - x_min_log) / (x_max_log - x_min_log) * _C_BAR_AREA_W

    # Tick positions: every integer power-of-10 that falls within the visible range.
    tick_logs = [
        i for i in range(int(math.floor(x_min_log)), int(math.ceil(x_max_log)) + 1)
        if x_min_log <= i <= x_max_log
    ]

    def _tick_label(log_val: int) -> str:
        v = 10 ** log_val
        if v >= 1000:
            return f"{v // 1000:g} s"
        if v >= 1:
            return f"{v:g} ms"
        return f"{v * 1000:g} µs"

    # ── SVG assembly ──────────────────────────────────────────────────────────
    E: list[str] = []

    def e(s: str) -> None:
        E.append(s)

    e(f'<svg xmlns="http://www.w3.org/2000/svg" '
      f'width="{total_w}" height="{total_h}" '
      f'font-family="{_C_FONT}" font-size="11">')
    e(f'  <rect width="{total_w}" height="{total_h}" fill="{_C_BG}"/>')

    # ── legend ────────────────────────────────────────────────────────────────
    leg_y     = 10
    col_w     = _C_BAR_AREA_W // 3
    for idx, (lbl, _, nc, ic) in enumerate(_CHART_METHODS):
        col = idx % 3
        row = idx // 3
        lx  = _C_LABEL_W + col * col_w
        ly  = leg_y + row * 18
        e(f'  <rect x="{lx}" y="{ly}" width="14" height="{_C_BAR_H}" fill="{nc}" rx="1"/>')
        e(f'  <rect x="{lx}" y="{ly + _C_BAR_H + 1}" width="14" height="{_C_BAR_H}" '
          f'fill="{ic}" rx="1"/>')
        e(f'  <text x="{lx + 18}" y="{ly + _C_BAR_H + 2}" '
          f'fill="{_C_LABEL_CLR}" font-size="10">{lbl}</text>')
    e(f'  <text x="{_C_LABEL_W}" y="{leg_y + 44}" fill="{_C_MUTED_CLR}" font-size="9">'
      f'Top bar = native (G4) · Bottom bar = imported (G4OCCTSolid) · '
      f'Logarithmic x-axis</text>')

    # ── x-axis line and ticks ─────────────────────────────────────────────────
    axis_y = top_off - 4
    e(f'  <line x1="{_C_LABEL_W}" y1="{axis_y}" '
      f'x2="{_C_LABEL_W + _C_BAR_AREA_W}" y2="{axis_y}" '
      f'stroke="{_C_AXIS_CLR}" stroke-width="1"/>')
    for tl in tick_logs:
        tx = _C_LABEL_W + _log_x(10 ** tl)
        e(f'  <line x1="{tx:.1f}" y1="{axis_y}" x2="{tx:.1f}" y2="{axis_y + 4}" '
          f'stroke="{_C_MUTED_CLR}" stroke-width="1"/>')
        e(f'  <text x="{tx:.1f}" y="{axis_y - 3}" fill="{_C_MUTED_CLR}" '
          f'font-size="9" text-anchor="middle">{_tick_label(tl)}</text>')
        # vertical grid line spanning the full chart
        e(f'  <line x1="{tx:.1f}" y1="{top_off}" '
          f'x2="{tx:.1f}" y2="{top_off + chart_h}" '
          f'stroke="{_C_GRID_CLR}" stroke-width="1" stroke-dasharray="3,2"/>')

    # ── fixture rows ──────────────────────────────────────────────────────────
    for fi, fix in enumerate(fixtures):
        gy = top_off + fi * (group_h + _C_FIX_GAP)

        # Alternating subtle row background.
        if fi % 2 == 0:
            e(f'  <rect x="{_C_LABEL_W}" y="{gy - 1}" '
              f'width="{_C_BAR_AREA_W}" height="{group_h + 2}" '
              f'fill="#ffffff" fill-opacity="0.025"/>')

        # Fixture label (right-aligned, vertically centred in the group).
        fid     = fix["id"]
        short   = fid if len(fid) <= 35 else "\u2026" + fid[-32:]
        ef      = " \u26a0" if fix["has_expected_failure"] else ""
        label_y = gy + group_h / 2 + 4
        e(f'  <text x="{_C_LABEL_W - 6}" y="{label_y:.0f}" fill="{_C_LABEL_CLR}" '
          f'font-size="10" text-anchor="end">{xml.sax.saxutils.escape(short)}{ef}</text>')

        # One native+imported bar pair per method.
        for mi, (_, mk, nc, ic) in enumerate(_CHART_METHODS):
            by   = gy + mi * (pair_h + _C_METH_GAP)
            d    = fix["methods"].get(mk)
            n_ms = (d.get("native_ms") or 0.0) if d else 0.0
            i_ms = (d.get("imported_ms") or 0.0) if d else 0.0
            nw   = _log_x(n_ms) if n_ms > 0 else 0.0
            iw   = _log_x(i_ms) if i_ms > 0 else 0.0

            if nw > 0:
                e(f'  <rect x="{_C_LABEL_W}" y="{by}" width="{nw:.1f}" '
                  f'height="{_C_BAR_H}" fill="{nc}" rx="1">'
                  f'<title>{mk} native: {n_ms:.2f} ms</title></rect>')
            if iw > 0:
                e(f'  <rect x="{_C_LABEL_W}" y="{by + _C_BAR_H + _C_BAR_GAP}" '
                  f'width="{iw:.1f}" height="{_C_BAR_H}" fill="{ic}" rx="1">'
                  f'<title>{mk} imported: {i_ms:.2f} ms</title></rect>')

    # Bottom axis line.
    bot_y = top_off + chart_h
    e(f'  <line x1="{_C_LABEL_W}" y1="{bot_y}" '
      f'x2="{_C_LABEL_W + _C_BAR_AREA_W}" y2="{bot_y}" '
      f'stroke="{_C_AXIS_CLR}" stroke-width="1"/>')

    e('</svg>')
    return '\n'.join(E)


def _render_report(data: dict, viewer_path: str,
                   onshape_links: dict[str, str] | None = None,
                   callgrind_links: dict[str, str] | None = None,
                   chart_svg: str | None = None) -> str:
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
        "# G4OCCT Solid Benchmark Results",
        "",
        meta_line,
        "",
        "**Interpretation note:** The per-method timings below reflect isolated,"
        " function-level measurements and are **not** representative of the call"
        " distribution in a typical Geant4 simulation, where particles are tracked"
        " along trajectories rather than random points being queried for their"
        " inside/outside status.",
        "",
        "The purpose of this comparison is to measure the cost of using a general"
        " BRep solid (G4OCCTSolid) in place of a hand-tuned Geant4 primitive for the"
        " shapes where both representations exist.  The native Geant4 implementation"
        " is always faster because it is specialised for a single shape class and"
        " does not need to be general.  **The goal is not to replace these faster"
        " primitives in simulations.**  Instead, the ratios here give a sense of the"
        " overhead incurred when a STEP geometry cannot be decomposed into a carefully"
        " chosen set of analytic primitives — that is, the price of generality.",
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
            "| Method | Col A (ms) | Col B (ms) | Ratio | Mismatches/Violations | Exp. Failures | Notes |",
            "|--------|----------:|-----------:|------:|----------------------:|--------------:|-------|",
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
            " **Mismatches/Violations**: for ray/inside/normal rows this counts"
            " native-vs-imported result disagreements; for `OCCT/Exact` rows it counts"
            " points where the lower bound exceeded the exact distance (hard fail);"
            " `---` means not applicable.",
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
        if chart_svg:
            lines += [
                "",
                "## Timing Chart",
                "",
                "<details open><summary>Show chart</summary>",
                "",
                '![Benchmark timing chart — horizontal grouped bar chart per fixture](bench-chart.svg)',
                "",
                "</details>",
            ]

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
        callgrind_note = (" 📊 links open the callgrind profile artifact for that fixture."
                          if callgrind_links else "")
        lines += [
            "",
            "## Per-Fixture Results",
            "",
            "Each cell shows `col-A ms → col-B ms (ratio)`."
            " For most methods: col A = Geant4/native, col B = OCCT/imported."
            " For `OCCT↔Exact` cells: col A = OCCT lower-bound, col B = OCCT exact."
            " **✅/⚠️ semantics by column:**"
            " For ray/inside/normal columns ✅ = no native-vs-imported result mismatches,"
            " N ⚠️ = N mismatches."
            " For `OCCT↔Exact` columns ✅ = no lower-bound violations,"
            " N ⚠️ = N violations (hard fail: lower-bound exceeded exact)."
            " Exit normals has no separate timing (normals are computed as part of DistanceToOut)."
            " Column abbreviations: **DTI/DTO(p,v)** = DistanceToIn/Out(p,v),"
            " **DTI(p) G4↔OCCT** = DistanceToIn(p) Geant4 vs OCCT timing with avg distance ratio,"
            " **DTI(p) OCCT↔Exact** = OCCT lower-bound vs exact timing with avg lb ratio and violations,"
            " **DTO(p)** columns analogous to DTI(p),"
            " **SN(p)** = SurfaceNormal,"
            " **Polyhedron** = CreatePolyhedron() timing with native/imported vertex and facet counts."
            " Fixtures marked ⚠️ are expected failures and do not block CI."
            f"{onshape_note}{callgrind_note}",
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

            fixture_link     = _fixture_viewer_link(f["id"], viewer_path)
            ef_marker        = " ⚠️" if f["has_expected_failure"] else ""
            cell_str         = " | ".join(cells)
            onshape_url      = onshape_links.get(f["id"]) if onshape_links else None
            onshape_badge    = f" [🔗]({onshape_url})" if onshape_url else ""
            callgrind_url    = callgrind_links.get(f["id"]) if callgrind_links else None
            callgrind_badge  = f" [📊]({callgrind_url})" if callgrind_url else ""
            lines.append(
                f"| [{md_escape(f['id'])}]({fixture_link}){onshape_badge}{callgrind_badge}{ef_marker} "
                f"| {md_escape(f['class'])} | {cell_str} |"
            )

    return "\n".join(lines) + "\n"


def _render_error(message: str) -> str:
    """Render a minimal Markdown error report."""
    return (
        "# G4OCCT Solid Benchmark Results\n\n"
        f"Generated: {timestamp()}\n\n"
        f"❌ Could not generate report: {message}\n"
    )


def main() -> None:
    if len(sys.argv) not in (3, 4, 5, 6):
        print(
            f"Usage: {sys.argv[0]} <bench-results.json> <output.md> [viewer-path]"
            " [onshape-links.json] [callgrind-links.json]",
            file=sys.stderr,
        )
        sys.exit(1)

    json_path   = sys.argv[1]
    md_path     = sys.argv[2]
    viewer_path = sys.argv[3] if len(sys.argv) >= 4 else "point_cloud_viewer.html"

    def _load_links_json(path_str: str, label: str) -> dict[str, str] | None:
        """Load a fixture-ID → URL mapping JSON file; warn and return None on error."""
        links_path = Path(path_str)
        try:
            loaded = json.loads(links_path.read_text(encoding="utf-8"))
        except (FileNotFoundError, OSError, json.JSONDecodeError) as exc:
            print(f"Warning: could not load {label} from {links_path}: {exc}",
                  file=sys.stderr)
            return None
        if not isinstance(loaded, dict):
            print(
                f"Warning: ignoring {label} from {links_path}: expected JSON object"
                f" at top level, got {type(loaded).__name__}",
                file=sys.stderr,
            )
            return None
        return loaded

    onshape_links: dict[str, str] | None = None
    if len(sys.argv) >= 5:
        onshape_links = _load_links_json(sys.argv[4], "Onshape links")

    callgrind_links: dict[str, str] | None = None
    if len(sys.argv) >= 6:
        callgrind_links = _load_links_json(sys.argv[5], "callgrind links")

    try:
        raw = json.loads(Path(json_path).read_text(encoding="utf-8"))
    except (FileNotFoundError, OSError, json.JSONDecodeError) as exc:
        print(f"Warning: {exc}", file=sys.stderr)
        write_report(Path(md_path), _render_error(str(exc)), label="Benchmark report")
        return

    data      = _parse_bench_json(raw)
    chart_svg = _render_chart_svg(data.get("fixtures", []))
    md        = _render_report(data, viewer_path, onshape_links, callgrind_links,
                               chart_svg=chart_svg)
    write_report(Path(md_path), md, label="Benchmark report")
    if chart_svg:
        chart_path = Path(md_path).parent / "bench-chart.svg"
        write_report(chart_path, chart_svg, label="Benchmark chart")


if __name__ == "__main__":
    main()
