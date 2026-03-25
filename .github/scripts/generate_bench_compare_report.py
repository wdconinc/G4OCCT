# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

"""Compare OCCT benchmark timings between two bench_raw.json files.

Generates a comparative SVG chart and a Markdown report showing how OCCT
(imported) timings in the current run compare to a baseline run (e.g. the
target branch of a pull request).

Usage::

    generate_bench_compare_report.py <current.json> <baseline.json> \\
        <output.md> [output.svg] [baseline-label]
"""

import json
import math
import re
import sys
import xml.sax.saxutils
from pathlib import Path

from report_utils import md_escape, timestamp, write_report

# ── JSON parsing (mirrors generate_bench_report.py) ─────────────────────────

_BENCH_SUFFIX_RE = re.compile(r"(/iterations:\d+|/manual_time|/real_time)+$")


def _get_ctr(bm: dict, key: str, default: float = 0.0) -> float:
    """Return a Google Benchmark counter value, checking top-level then 'counters'."""
    if key in bm:
        return float(bm[key])
    return float(bm.get("counters", {}).get(key, default))


def _parse_benchmark_name(name: str) -> tuple[str | None, str | None]:
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


def _parse_occt_timings(data: dict) -> dict:
    """Parse a bench_raw.json dict and return per-fixture OCCT timings.

    Only the *imported* (OCCT) millisecond values are extracted from each
    benchmark entry; native (Geant4) timings are ignored because the
    comparison is purely between two OCCT runs.

    Returns a dict with a ``"fixtures"`` list; each entry is::

        {
            "id":                   str,   # fixture identifier
            "class":                str,   # Geant4 class name
            "has_expected_failure": bool,
            "methods": {method_key: float},  # imported_ms per method
        }
    """
    fixture_order: list[str] = []
    fixture_groups: dict[str, dict] = {}

    for bm in data.get("benchmarks", []):
        if bm.get("run_type") == "aggregate":
            continue
        if "name" not in bm:
            continue
        method, fixture_id = _parse_benchmark_name(bm["name"])
        if method is None:
            continue
        if fixture_id not in fixture_groups:
            fixture_order.append(fixture_id)
            fixture_groups[fixture_id] = {}
        fixture_groups[fixture_id][method] = bm

    fixtures: list[dict] = []
    for fixture_id in fixture_order:
        group     = fixture_groups[fixture_id]
        rays_bm   = group.get("rays",       {})
        inside_bm = group.get("inside",     {})
        safety_bm = group.get("safety",     {})
        poly_bm   = group.get("polyhedron", {})

        geant4_class         = rays_bm.get("label", "") if rays_bm else ""
        has_expected_failure = (_get_ctr(rays_bm, "has_expected_failure") != 0.0
                                if rays_bm else False)

        methods: dict[str, float] = {}
        if rays_bm:
            methods["DistanceToIn/Out(p,v)"] = _get_ctr(rays_bm, "imported_ms")
            methods["SurfaceNormal(p)"]      = _get_ctr(rays_bm, "sn_imported_ms")
        if inside_bm:
            methods["Inside(p)"] = _get_ctr(inside_bm, "imported_ms")
        if safety_bm:
            methods["DistanceToIn(p)"]  = _get_ctr(safety_bm, "safety_in_imported_ms")
            methods["DistanceToOut(p)"] = _get_ctr(safety_bm, "safety_out_imported_ms")
        if poly_bm:
            methods["CreatePolyhedron()"] = _get_ctr(poly_bm, "imported_ms")

        fixtures.append({
            "id":                   fixture_id,
            "class":                geant4_class,
            "has_expected_failure": has_expected_failure,
            "methods":              methods,
        })

    return {"fixtures": fixtures}


# ── Chart constants ──────────────────────────────────────────────────────────

# Methods shown in the comparison chart.  Tuple: (short label, method_key).
_COMPARE_METHODS: list[tuple[str, str]] = [
    ("DTI/DTO(p,v)", "DistanceToIn/Out(p,v)"),
    ("Inside(p)",    "Inside(p)"),
    ("DTI(p)",       "DistanceToIn(p)"),
    ("DTO(p)",       "DistanceToOut(p)"),
    ("SN(p)",        "SurfaceNormal(p)"),
    ("Polyhedron",   "CreatePolyhedron()"),
]

# Bar colors (mirrors the style in generate_bench_report.py).
_BASELINE_CLR  = "#4c9fde"   # baseline (target branch) – always blue
_IMPROVED_CLR  = "#3a8f1a"   # current PR faster by > _THRESHOLD
_REGRESSED_CLR = "#e05050"   # current PR slower by > _THRESHOLD
_NEUTRAL_CLR   = "#888888"   # within ±_THRESHOLD

_THRESHOLD = 0.05            # 5 % threshold for colouring

# Fixture-label truncation limits (characters).
_FIXTURE_LABEL_MAX_LEN    = 35
_FIXTURE_LABEL_SUFFIX_LEN = 32

# Chart layout constants (pixels) – kept in sync with generate_bench_report.py.
_C_LABEL_W    = 250
_C_BAR_AREA_W = 760
_C_RIGHT_PAD  = 20
_C_LEGEND_H   = 52           # legend block height (colour guide + note line)
_C_X_AXIS_H   = 26
_C_BOTTOM_PAD = 20
_C_BAR_H      = 5
_C_BAR_GAP    = 1
_C_METH_GAP   = 3
_C_FIX_GAP    = 10

_C_BG        = "#1a1a2e"
_C_AXIS_CLR  = "#555"
_C_GRID_CLR  = "#2a2a4a"
_C_LABEL_CLR = "#e0e0e0"
_C_MUTED_CLR = "#888"
_C_FONT      = "-apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"


def _current_bar_color(baseline_ms: float, current_ms: float) -> str:
    """Return the colour for the *current* (bottom) bar."""
    if baseline_ms <= 0:
        return _NEUTRAL_CLR
    ratio = current_ms / baseline_ms
    if ratio < 1.0 - _THRESHOLD:
        return _IMPROVED_CLR
    if ratio > 1.0 + _THRESHOLD:
        return _REGRESSED_CLR
    return _NEUTRAL_CLR


def _render_compare_chart_svg(
    baseline_fixtures: list[dict],
    current_fixtures:  list[dict],
    baseline_label: str = "baseline",
    current_label:  str = "current",
) -> str:
    """Render a comparative horizontal grouped bar chart as an SVG string.

    For each fixture present in *both* runs, for every method in
    ``_COMPARE_METHODS``, two bars are drawn:

    * Top bar (blue) – baseline OCCT timing.
    * Bottom bar (green / grey / red) – current OCCT timing, coloured by
      whether it improved (>5% faster), regressed (>5% slower), or stayed
      within ±5% of the baseline.

    The x-axis uses a log₁₀ scale identical to the G4-vs-OCCT chart.
    Returns an empty string when there are no common fixtures or timing data.
    """
    current_by_id  = {f["id"]: f for f in current_fixtures}
    baseline_by_id = {f["id"]: f for f in baseline_fixtures}

    # Preserve baseline fixture order; skip fixtures missing from either run.
    common_ids = [f["id"] for f in baseline_fixtures if f["id"] in current_by_id]
    if not common_ids:
        return ""

    # Collect all timing values to determine the x-axis range.
    all_vals: list[float] = []
    for fid in common_ids:
        bf = baseline_by_id[fid]
        cf = current_by_id[fid]
        for _, mk in _COMPARE_METHODS:
            bv = bf["methods"].get(mk, 0.0)
            cv = cf["methods"].get(mk, 0.0)
            if bv > 0:
                all_vals.append(bv)
            if cv > 0:
                all_vals.append(cv)
    if not all_vals:
        return ""

    # ── derived dimensions ────────────────────────────────────────────────────
    n_m     = len(_COMPARE_METHODS)
    pair_h  = _C_BAR_H + _C_BAR_GAP + _C_BAR_H
    group_h = pair_h * n_m + _C_METH_GAP * (n_m - 1)
    n_f     = len(common_ids)
    chart_h = group_h * n_f + _C_FIX_GAP * max(n_f - 1, 0)
    top_off = _C_LEGEND_H + _C_X_AXIS_H
    total_h = top_off + chart_h + _C_BOTTOM_PAD
    total_w = _C_LABEL_W + _C_BAR_AREA_W + _C_RIGHT_PAD

    # ── log₁₀ x-axis scale ───────────────────────────────────────────────────
    max_ms    = max(all_vals)
    min_ms    = min(all_vals)
    x_min_log = math.floor(math.log10(min_ms)) - 0.3
    x_max_log = math.log10(max_ms) + 0.3
    if x_max_log <= x_min_log:
        x_max_log = x_min_log + 1.0

    def _log_x(ms: float) -> float:
        if ms <= 0:
            return 0.0
        clamped = max(ms, 10 ** x_min_log)
        return (math.log10(clamped) - x_min_log) / (x_max_log - x_min_log) * _C_BAR_AREA_W

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
    # Single row of four colour swatches explaining the colour coding.
    leg_y    = 10
    lx0      = _C_LABEL_W
    sw_gap   = 6   # gap between swatch rect and label text
    swatches = [
        (_BASELINE_CLR,  baseline_label + " (top bar)"),
        (_IMPROVED_CLR,  current_label  + " faster (>5%)"),
        (_REGRESSED_CLR, current_label  + " slower (>5%)"),
        (_NEUTRAL_CLR,   "within \u00b15%"),
    ]
    col_w = _C_BAR_AREA_W // len(swatches)
    for idx, (clr, lbl) in enumerate(swatches):
        lx = lx0 + idx * col_w
        # Two overlapping bars (top = baseline swatch, bottom = current swatch)
        e(f'  <rect x="{lx}" y="{leg_y}" width="14" height="{_C_BAR_H}" '
          f'fill="{_BASELINE_CLR}" rx="1"/>')
        e(f'  <rect x="{lx}" y="{leg_y + _C_BAR_H + 1}" width="14" height="{_C_BAR_H}" '
          f'fill="{clr}" rx="1"/>')
        e(f'  <text x="{lx + 14 + sw_gap}" y="{leg_y + _C_BAR_H + 2}" '
          f'fill="{_C_LABEL_CLR}" font-size="10">'
          f'{xml.sax.saxutils.escape(lbl)}</text>')

    # Method abbreviation guide
    abbr_parts = [f"{lbl}={mk}" for lbl, mk in _COMPARE_METHODS]
    abbr_text  = " · ".join(abbr_parts)
    e(f'  <text x="{lx0}" y="{leg_y + 34}" fill="{_C_MUTED_CLR}" font-size="9">'
      f'Top bar = {xml.sax.saxutils.escape(baseline_label)} OCCT · '
      f'Bottom bar = {xml.sax.saxutils.escape(current_label)} OCCT · '
      f'Logarithmic x-axis</text>')
    e(f'  <text x="{lx0}" y="{leg_y + 46}" fill="{_C_MUTED_CLR}" font-size="8">'
      f'{xml.sax.saxutils.escape(abbr_text)}</text>')

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
        e(f'  <line x1="{tx:.1f}" y1="{top_off}" '
          f'x2="{tx:.1f}" y2="{top_off + chart_h}" '
          f'stroke="{_C_GRID_CLR}" stroke-width="1" stroke-dasharray="3,2"/>')

    # ── fixture rows ──────────────────────────────────────────────────────────
    for fi, fid in enumerate(common_ids):
        bf  = baseline_by_id[fid]
        cf  = current_by_id[fid]
        gy  = top_off + fi * (group_h + _C_FIX_GAP)

        # Alternating subtle row background.
        if fi % 2 == 0:
            e(f'  <rect x="{_C_LABEL_W}" y="{gy - 1}" '
              f'width="{_C_BAR_AREA_W}" height="{group_h + 2}" '
              f'fill="#ffffff" fill-opacity="0.025"/>')

        # Fixture label.
        short   = (fid if len(fid) <= _FIXTURE_LABEL_MAX_LEN
                   else "\u2026" + fid[-_FIXTURE_LABEL_SUFFIX_LEN:])
        has_expected_failure = bf["has_expected_failure"] or cf["has_expected_failure"]
        ef      = " \u26a0" if has_expected_failure else ""
        label_y = gy + group_h / 2 + 4
        e(f'  <text x="{_C_LABEL_W - 6}" y="{label_y:.0f}" fill="{_C_LABEL_CLR}" '
          f'font-size="10" text-anchor="end">{xml.sax.saxutils.escape(short)}{ef}</text>')

        # One baseline+current bar pair per method.
        for mi, (_, mk) in enumerate(_COMPARE_METHODS):
            by      = gy + mi * (pair_h + _C_METH_GAP)
            b_ms    = bf["methods"].get(mk, 0.0)
            c_ms    = cf["methods"].get(mk, 0.0)
            bw      = _log_x(b_ms) if b_ms > 0 else 0.0
            cw      = _log_x(c_ms) if c_ms > 0 else 0.0
            c_color = _current_bar_color(b_ms, c_ms)

            ratio_str = (f"{c_ms / b_ms:.2f}x vs {xml.sax.saxutils.escape(baseline_label)}"
                         if b_ms > 0 and c_ms > 0 else "N/A")

            if bw > 0:
                e(f'  <rect x="{_C_LABEL_W}" y="{by}" width="{bw:.1f}" '
                  f'height="{_C_BAR_H}" fill="{_BASELINE_CLR}" rx="1">'
                  f'<title>{xml.sax.saxutils.escape(mk)} {xml.sax.saxutils.escape(baseline_label)}: {b_ms:.2f} ms</title></rect>')
            if cw > 0:
                e(f'  <rect x="{_C_LABEL_W}" y="{by + _C_BAR_H + _C_BAR_GAP}" '
                  f'width="{cw:.1f}" height="{_C_BAR_H}" fill="{c_color}" rx="1">'
                  f'<title>{xml.sax.saxutils.escape(mk)} {xml.sax.saxutils.escape(current_label)}: {c_ms:.2f} ms '
                  f'({ratio_str})</title></rect>')

    # Bottom axis line.
    bot_y = top_off + chart_h
    e(f'  <line x1="{_C_LABEL_W}" y1="{bot_y}" '
      f'x2="{_C_LABEL_W + _C_BAR_AREA_W}" y2="{bot_y}" '
      f'stroke="{_C_AXIS_CLR}" stroke-width="1"/>')

    e('</svg>')
    return '\n'.join(E)


# ── Markdown report ──────────────────────────────────────────────────────────

def _fmt_ms(ms: float) -> str:
    return "---" if ms <= 0 else f"{ms:.1f}"


def _fmt_ratio(baseline_ms: float, current_ms: float) -> str:
    if baseline_ms <= 0 or current_ms <= 0:
        return "---"
    return f"{current_ms / baseline_ms:.2f}x"


def _ratio_status(baseline_ms: float, current_ms: float) -> str:
    if baseline_ms <= 0 or current_ms <= 0:
        return "---"
    ratio = current_ms / baseline_ms
    if ratio < 1.0 - _THRESHOLD:
        return "\U0001f7e2"   # 🟢 improved
    if ratio > 1.0 + _THRESHOLD:
        return "\U0001f534"   # 🔴 regressed
    return "\u26aa"           # ⚪ neutral


def _render_compare_report(
    baseline_data:  dict,
    current_data:   dict,
    baseline_label: str,
    current_label:  str,
    chart_svg:      str | None = None,
    chart_src:      str | None = None,
) -> str:
    """Render the Markdown comparison report."""
    baseline_fixtures = baseline_data.get("fixtures", [])
    current_fixtures  = current_data.get("fixtures", [])

    baseline_by_id = {f["id"]: f for f in baseline_fixtures}
    current_by_id  = {f["id"]: f for f in current_fixtures}

    common_ids  = [f["id"] for f in baseline_fixtures if f["id"] in current_by_id]
    added_ids   = [f["id"] for f in current_fixtures  if f["id"] not in baseline_by_id]
    removed_ids = [f["id"] for f in baseline_fixtures if f["id"] not in current_by_id]

    ts = timestamp()
    lines: list[str] = [
        "# G4OCCT OCCT Benchmark Comparison",
        "",
        f"Generated: {ts}",
        "",
        f"Baseline: **{md_escape(baseline_label)}** · Current: **{md_escape(current_label)}**",
        "",
        "Compares OCCT (*imported*) timings only — native (Geant4) timings are excluded. "
        "\U0001f7e2 = current faster >5%, \U0001f534 = current slower >5%,"
        " \u26aa = within \u00b15%.",
        "",
    ]

    if chart_svg:
        src = xml.sax.saxutils.escape(chart_src) if chart_src else ""
        if src:
            lines += [
                "## Comparison Chart",
                "",
                "<details open><summary>Show chart</summary>",
                "",
                f'<img src="{src}"'
                ' alt="OCCT benchmark comparison chart — horizontal grouped bar chart per fixture"/>',
                "",
                "</details>",
                "",
            ]

    # ── Aggregate table ───────────────────────────────────────────────────────
    lines += [
        "## Aggregate (common fixtures only)",
        "",
        "| Method | Baseline (ms) | Current (ms) | Ratio | Status |",
        "|--------|-------------:|-------------:|------:|:------:|",
    ]

    agg: dict[str, dict] = {mk: {"baseline": 0.0, "current": 0.0}
                             for _, mk in _COMPARE_METHODS}
    for fid in common_ids:
        bf = baseline_by_id[fid]
        cf = current_by_id[fid]
        for _, mk in _COMPARE_METHODS:
            agg[mk]["baseline"] += bf["methods"].get(mk, 0.0)
            agg[mk]["current"]  += cf["methods"].get(mk, 0.0)

    for lbl, mk in _COMPARE_METHODS:
        b = agg[mk]["baseline"]
        c = agg[mk]["current"]
        lines.append(
            f"| {md_escape(lbl)} | {_fmt_ms(b)} | {_fmt_ms(c)}"
            f" | {_fmt_ratio(b, c)} | {_ratio_status(b, c)} |"
        )

    lines.append("")

    if added_ids:
        fid_list = ", ".join(f"`{md_escape(fid)}`" for fid in added_ids)
        lines += [f"> **New fixtures** (in current only): {fid_list}", ""]

    if removed_ids:
        fid_list = ", ".join(f"`{md_escape(fid)}`" for fid in removed_ids)
        lines += [f"> **Removed fixtures** (in baseline only): {fid_list}", ""]

    if not common_ids:
        lines += ["> \u26a0\ufe0f No common fixtures found between baseline and current.", ""]
        return "\n".join(lines) + "\n"

    # ── Per-fixture table ─────────────────────────────────────────────────────
    col_headers = " | ".join(lbl for lbl, _ in _COMPARE_METHODS)
    col_sep     = "|".join(":---:" for _ in _COMPARE_METHODS)
    lines += [
        "## Per-Fixture Comparison",
        "",
        "Each cell: `baseline ms \u2192 current ms (ratio) status`."
        " Fixtures marked \u26a0 are expected failures.",
        "",
        f"| Fixture | Geant4 Class | {col_headers} |",
        f"|---------|:-------------|{col_sep}|",
    ]

    for fid in common_ids:
        bf    = baseline_by_id[fid]
        cf    = current_by_id[fid]
        cells = []
        for _, mk in _COMPARE_METHODS:
            b_ms = bf["methods"].get(mk, 0.0)
            c_ms = cf["methods"].get(mk, 0.0)
            if b_ms <= 0 and c_ms <= 0:
                cells.append("---")
            else:
                b_str = _fmt_ms(b_ms)
                c_str = _fmt_ms(c_ms)
                r_str = _fmt_ratio(b_ms, c_ms)
                st    = _ratio_status(b_ms, c_ms)
                cells.append(f"{b_str}\u2192{c_str} ({r_str}) {st}")

        ef        = " \u26a0\ufe0f" if (bf["has_expected_failure"] or cf["has_expected_failure"]) else ""
        cells_str = " | ".join(cells)
        lines.append(
            f"| {md_escape(fid)}{ef} | {md_escape(bf['class'])} | {cells_str} |"
        )

    lines.append("")
    return "\n".join(lines) + "\n"


# ── CLI entry point ──────────────────────────────────────────────────────────

def main() -> None:
    """Entry point: parse args, run comparison, write outputs."""
    if len(sys.argv) < 4 or len(sys.argv) > 6:
        print(
            f"Usage: {sys.argv[0]} <current.json> <baseline.json> <output.md>"
            " [output.svg] [baseline-label]",
            file=sys.stderr,
        )
        sys.exit(1)

    current_json_path  = Path(sys.argv[1])
    baseline_json_path = Path(sys.argv[2])
    md_path            = Path(sys.argv[3])
    svg_path           = (Path(sys.argv[4]) if len(sys.argv) >= 5
                          else md_path.parent / "bench-compare-chart.svg")
    baseline_label     = sys.argv[5] if len(sys.argv) >= 6 else "baseline"
    current_label      = "current"

    def _load_json(path: Path, label: str) -> dict | None:
        try:
            return json.loads(path.read_text(encoding="utf-8"))
        except (FileNotFoundError, OSError, json.JSONDecodeError) as exc:
            print(f"Warning: could not load {label} from {path}: "
                  f"{type(exc).__name__}: {exc}",
                  file=sys.stderr)
            return None

    current_raw  = _load_json(current_json_path,  "current benchmark JSON")
    baseline_raw = _load_json(baseline_json_path, "baseline benchmark JSON")

    if current_raw is None or baseline_raw is None:
        write_report(
            md_path,
            "# G4OCCT OCCT Benchmark Comparison\n\n"
            f"Generated: {timestamp()}\n\n"
            "\u274c Could not generate comparison: missing input JSON file(s).\n",
            label="Comparison report",
        )
        return

    current_data  = _parse_occt_timings(current_raw)
    baseline_data = _parse_occt_timings(baseline_raw)

    chart_svg = _render_compare_chart_svg(
        baseline_data["fixtures"],
        current_data["fixtures"],
        baseline_label=baseline_label,
        current_label=current_label,
    )

    chart_src: str | None = None
    if chart_svg:
        write_report(svg_path, chart_svg, label="Comparison chart")
        try:
            chart_src = svg_path.relative_to(md_path.parent).as_posix()
        except ValueError:
            chart_src = svg_path.name

    md = _render_compare_report(
        baseline_data, current_data,
        baseline_label=baseline_label,
        current_label=current_label,
        chart_svg=chart_svg,
        chart_src=chart_src,
    )
    write_report(md_path, md, label="Comparison report")


if __name__ == "__main__":
    main()
