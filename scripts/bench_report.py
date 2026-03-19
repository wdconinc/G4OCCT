#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors
"""
bench_report.py — Re-render the G4OCCT navigator benchmark table from
                  Google Benchmark JSON output.

Usage (piped):
    ./bench_navigator --benchmark_format=json [args] | python3 scripts/bench_report.py

Usage (from file):
    ./bench_navigator --benchmark_format=json --benchmark_out=bench.json [args]
    python3 scripts/bench_report.py bench.json

The JSON must have been produced by bench_navigator with counters that encode
timing (native_ms, imported_ms) and mismatch counts per (fixture × method).

Benchmark name format:  BM_<method>/<family>/<fixture_id>
Methods: rays, inside, safety, polyhedron
"""

import json
import sys
import math


# ─── Formatting helpers (mirror the C++ output exactly) ──────────────────────

def fmt_ms(ms: float) -> str:
    return f"{ms:.2f}"


def fmt_ratio(native_ms: float, imported_ms: float) -> str:
    if native_ms <= 0.0:
        return "---"
    return f"{imported_ms / native_ms:.1f}x"


def print_method_row(label: str, native_ms: float, imported_ms: float,
                     mismatches, has_timing: bool = True) -> None:
    mismatch_str = str(int(mismatches)) if not math.isnan(mismatches) else "---"
    label_col = f"{label:<24}"
    if has_timing:
        print(f"  {label_col}: "
              f"native={fmt_ms(native_ms):>8}ms"
              f"  imported={fmt_ms(imported_ms):>8}ms"
              f"  ratio={fmt_ratio(native_ms, imported_ms):>8}"
              f"  mismatches={mismatch_str}")
    else:
        print(f"  {label_col}: "
              f"native={'---':>8}ms"
              f"  imported={'---':>8}ms"
              f"  ratio={'---':>8}"
              f"  mismatches={mismatch_str}")


def print_polyhedron_row(native_ms: float, imported_ms: float,
                         native_vertices: float, imported_vertices: float,
                         native_facets: float, imported_facets: float) -> None:
    label_col = f"{'CreatePolyhedron()':<24}"
    print(f"  {label_col}: "
          f"native={fmt_ms(native_ms):>8}ms"
          f"  imported={fmt_ms(imported_ms):>8}ms"
          f"  ratio={fmt_ratio(native_ms, imported_ms):>8}"
          f"  vertices={int(native_vertices)}/{int(imported_vertices)}"
          f"  facets={int(native_facets)}/{int(imported_facets)}")


def print_aggregate_row(label: str, native_ms: float, imported_ms: float,
                        mismatches, exp_failures, has_timing: bool = True) -> None:
    mismatch_str = str(int(mismatches)) if not math.isnan(mismatches) else "---"
    exp_str = str(int(exp_failures)) if not math.isnan(exp_failures) else "---"
    label_col = f"{label:<24}"
    if has_timing:
        print(f"  {label_col}{fmt_ms(native_ms):>12}{fmt_ms(imported_ms):>14}"
              f"{fmt_ratio(native_ms, imported_ms):>10}"
              f"{mismatch_str:>13}{exp_str:>14}")
    else:
        print(f"  {label_col}{'---':>12}{'---':>14}{'---':>10}"
              f"{mismatch_str:>13}{exp_str:>14}")


# ─── JSON parsing ─────────────────────────────────────────────────────────────

def get_counter(bm: dict, key: str, default: float = 0.0) -> float:
    """Return a counter value, or *default* when absent."""
    return float(bm.get("counters", {}).get(key, default))


def parse_benchmark_name(name: str):
    """
    Parse a benchmark name of the form ``BM_<method>/<family>/<fixture>``.

    Returns ``(method, fixture_id)`` or ``(None, None)`` if the name does not
    match the expected prefix.
    """
    if not name.startswith("BM_"):
        return None, None
    rest = name[3:]  # strip "BM_"
    slash = rest.find("/")
    if slash < 0:
        return None, None
    method = rest[:slash]
    fixture_id = rest[slash + 1:]
    return method, fixture_id


def load_json(source) -> dict:
    return json.load(source)


# ─── Main ─────────────────────────────────────────────────────────────────────

def main() -> int:
    if len(sys.argv) > 1:
        with open(sys.argv[1], encoding="utf-8") as fh:
            data = load_json(fh)
    else:
        data = load_json(sys.stdin)

    benchmarks = data.get("benchmarks", [])

    # Group benchmark results by fixture_id, preserving first-seen order.
    fixture_order: list[str] = []
    fixtures: dict[str, dict] = {}

    for bm in benchmarks:
        # Skip aggregate rows (e.g. mean/median/stddev produced by --benchmark_repetitions).
        if bm.get("run_type") in ("aggregate",):
            continue

        method, fixture_id = parse_benchmark_name(bm["name"])
        if method is None:
            continue

        if fixture_id not in fixtures:
            fixture_order.append(fixture_id)
            fixtures[fixture_id] = {}

        fixtures[fixture_id][method] = bm

    if not fixture_order:
        print("No benchmark data found in JSON.", file=sys.stderr)
        return 1

    # Infer ray_count / point_count from the context section if available.
    context = data.get("context", {})
    ray_count = context.get("ray_count", "?")
    inside_count = context.get("inside_count", ray_count)
    safety_count = context.get("safety_count", ray_count)

    print()
    print("=== Fixture Navigation Benchmark Results ===")
    print(f"Rays: {ray_count}  |  Inside points: {inside_count}"
          f"  |  Safety points: {safety_count}")
    print()

    # ── Per-fixture table ─────────────────────────────────────────────────────
    for fixture_id in fixture_order:
        methods = fixtures[fixture_id]

        rays_bm = methods.get("rays", {})
        inside_bm = methods.get("inside", {})
        safety_bm = methods.get("safety", {})
        poly_bm = methods.get("polyhedron", {})

        print(f"{fixture_id}:")

        # Row 1: DistanceToIn/Out(p,v)
        if rays_bm:
            print_method_row(
                "DistanceToIn/Out(p,v)",
                get_counter(rays_bm, "native_ms"),
                get_counter(rays_bm, "imported_ms"),
                get_counter(rays_bm, "mismatches"),
            )
        else:
            print_method_row("DistanceToIn/Out(p,v)", 0.0, 0.0, float("nan"))

        # Row 2: Exit normals (no timing)
        exit_normal_mismatches = (get_counter(rays_bm, "exit_normal_mismatches")
                                  if rays_bm else float("nan"))
        print_method_row("Exit normals", 0.0, 0.0, exit_normal_mismatches, has_timing=False)

        # Row 3: Inside(p)
        if inside_bm:
            print_method_row(
                "Inside(p)",
                get_counter(inside_bm, "native_ms"),
                get_counter(inside_bm, "imported_ms"),
                get_counter(inside_bm, "mismatches"),
            )
        else:
            print_method_row("Inside(p)", 0.0, 0.0, float("nan"))

        # Row 4: DistanceToIn(p)
        if safety_bm:
            print_method_row(
                "DistanceToIn(p)",
                get_counter(safety_bm, "safety_in_native_ms"),
                get_counter(safety_bm, "safety_in_imported_ms"),
                get_counter(safety_bm, "safety_in_mismatches"),
            )
        else:
            print_method_row("DistanceToIn(p)", 0.0, 0.0, float("nan"))

        # Row 5: DistanceToOut(p)
        if safety_bm:
            print_method_row(
                "DistanceToOut(p)",
                get_counter(safety_bm, "safety_out_native_ms"),
                get_counter(safety_bm, "safety_out_imported_ms"),
                get_counter(safety_bm, "safety_out_mismatches"),
            )
        else:
            print_method_row("DistanceToOut(p)", 0.0, 0.0, float("nan"))

        # Row 6: SurfaceNormal(p)
        if rays_bm:
            print_method_row(
                "SurfaceNormal(p)",
                get_counter(rays_bm, "sn_native_ms"),
                get_counter(rays_bm, "sn_imported_ms"),
                get_counter(rays_bm, "sn_mismatches"),
            )
        else:
            print_method_row("SurfaceNormal(p)", 0.0, 0.0, float("nan"))

        # Row 7: CreatePolyhedron()
        if poly_bm:
            print_polyhedron_row(
                get_counter(poly_bm, "native_ms"),
                get_counter(poly_bm, "imported_ms"),
                get_counter(poly_bm, "native_vertices"),
                get_counter(poly_bm, "imported_vertices"),
                get_counter(poly_bm, "native_facets"),
                get_counter(poly_bm, "imported_facets"),
            )
        else:
            print_polyhedron_row(0.0, 0.0, 0, 0, 0, 0)

        print()

    # ── Aggregate table ───────────────────────────────────────────────────────
    agg_ray_native_ms     = 0.0
    agg_ray_imported_ms   = 0.0
    agg_ray_mismatches    = 0.0
    agg_exit_mismatches   = 0.0
    agg_inside_native_ms  = 0.0
    agg_inside_imported_ms = 0.0
    agg_inside_mismatches = 0.0
    agg_dti_native_ms     = 0.0
    agg_dti_imported_ms   = 0.0
    agg_dti_mismatches    = 0.0
    agg_dto_native_ms     = 0.0
    agg_dto_imported_ms   = 0.0
    agg_dto_mismatches    = 0.0
    agg_sn_native_ms      = 0.0
    agg_sn_imported_ms    = 0.0
    agg_sn_mismatches     = 0.0
    agg_poly_native_ms    = 0.0
    agg_poly_imported_ms  = 0.0

    for fixture_id in fixture_order:
        methods = fixtures[fixture_id]
        rays_bm   = methods.get("rays", {})
        inside_bm = methods.get("inside", {})
        safety_bm = methods.get("safety", {})
        poly_bm   = methods.get("polyhedron", {})

        if rays_bm:
            agg_ray_native_ms   += get_counter(rays_bm, "native_ms")
            agg_ray_imported_ms += get_counter(rays_bm, "imported_ms")
            agg_ray_mismatches  += get_counter(rays_bm, "mismatches")
            agg_exit_mismatches += get_counter(rays_bm, "exit_normal_mismatches")
            agg_sn_native_ms    += get_counter(rays_bm, "sn_native_ms")
            agg_sn_imported_ms  += get_counter(rays_bm, "sn_imported_ms")
            agg_sn_mismatches   += get_counter(rays_bm, "sn_mismatches")

        if inside_bm:
            agg_inside_native_ms  += get_counter(inside_bm, "native_ms")
            agg_inside_imported_ms += get_counter(inside_bm, "imported_ms")
            agg_inside_mismatches += get_counter(inside_bm, "mismatches")

        if safety_bm:
            agg_dti_native_ms   += get_counter(safety_bm, "safety_in_native_ms")
            agg_dti_imported_ms += get_counter(safety_bm, "safety_in_imported_ms")
            agg_dti_mismatches  += get_counter(safety_bm, "safety_in_mismatches")
            agg_dto_native_ms   += get_counter(safety_bm, "safety_out_native_ms")
            agg_dto_imported_ms += get_counter(safety_bm, "safety_out_imported_ms")
            agg_dto_mismatches  += get_counter(safety_bm, "safety_out_mismatches")

        if poly_bm:
            agg_poly_native_ms  += get_counter(poly_bm, "native_ms")
            agg_poly_imported_ms += get_counter(poly_bm, "imported_ms")

    print("Aggregate:")
    print(f"  {'Method':<24}{'Native(ms)':>12}{'Imported(ms)':>14}"
          f"{'Ratio':>10}{'Mismatches':>13}{'Exp. Failures':>14}")
    print(f"  {'-' * 85}")

    # Aggregate table does not have per-fixture expected-failure counts in the JSON.
    print_aggregate_row("DistanceToIn/Out(p,v)",
                        agg_ray_native_ms, agg_ray_imported_ms,
                        agg_ray_mismatches, float("nan"))
    print_aggregate_row("Exit normals", 0.0, 0.0,
                        agg_exit_mismatches, float("nan"), has_timing=False)
    print_aggregate_row("Inside(p)",
                        agg_inside_native_ms, agg_inside_imported_ms,
                        agg_inside_mismatches, float("nan"))
    print_aggregate_row("DistanceToIn(p)",
                        agg_dti_native_ms, agg_dti_imported_ms,
                        agg_dti_mismatches, float("nan"))
    print_aggregate_row("DistanceToOut(p)",
                        agg_dto_native_ms, agg_dto_imported_ms,
                        agg_dto_mismatches, float("nan"))
    print_aggregate_row("SurfaceNormal(p)",
                        agg_sn_native_ms, agg_sn_imported_ms,
                        agg_sn_mismatches, float("nan"))
    # CreatePolyhedron() — no mismatch count; meshes differ by design.
    label_col = f"{'CreatePolyhedron()':<24}"
    print(f"  {label_col}"
          f"{fmt_ms(agg_poly_native_ms):>12}"
          f"{fmt_ms(agg_poly_imported_ms):>14}"
          f"{fmt_ratio(agg_poly_native_ms, agg_poly_imported_ms):>10}"
          f"{'---':>13}"
          f"{'---':>14}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
