// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "geometry/fixture_navigation_summary.hh"
#include "geometry/fixture_polyhedron_compare.hh"
#include "geometry/fixture_ray_compare.hh"
#include "geometry/fixture_inside_compare.hh"
#include "geometry/fixture_safety_compare.hh"

#include <benchmark/benchmark.h>

#include <cstdlib>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace g4occt::benchmarks {
namespace {

  using g4occt::tests::geometry::CompareFixtureInside;
  using g4occt::tests::geometry::CompareFixturePolyhedron;
  using g4occt::tests::geometry::CompareFixtureRays;
  using g4occt::tests::geometry::CompareFixtureSafety;
  using g4occt::tests::geometry::DefaultRepositoryManifestPath;
  using g4occt::tests::geometry::FixtureInsideComparisonOptions;
  using g4occt::tests::geometry::FixtureManifest;
  using g4occt::tests::geometry::FixtureNavigationSummary;
  using g4occt::tests::geometry::FixturePolyhedronComparisonOptions;
  using g4occt::tests::geometry::FixtureRayComparisonOptions;
  using g4occt::tests::geometry::FixtureRepositoryManifest;
  using g4occt::tests::geometry::FixtureSafetyComparisonOptions;
  using g4occt::tests::geometry::FixtureValidationRequest;
  using g4occt::tests::geometry::ParseFixtureManifestFile;
  using g4occt::tests::geometry::ParseFixtureRepositoryManifest;
  using g4occt::tests::geometry::ResolveFamilyManifestPath;
  using g4occt::tests::geometry::ValidateFixtureLayout;
  using g4occt::tests::geometry::ValidateManifestStructure;
  using g4occt::tests::geometry::ValidateRepositoryLayout;
  using g4occt::tests::geometry::ValidationReport;

  // ─── Shared state (set by main, read/written by benchmark callbacks) ─────────

  struct BenchmarkSharedState {
    std::map<std::string, FixtureNavigationSummary> summaries;
    std::vector<std::string> fixture_order;
    ValidationReport aggregate_report;
    std::mutex mu;
  };

  static BenchmarkSharedState* g_state =
      nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

  // ─── Predicates ──────────────────────────────────────────────────────────────

  // Returns true for fixtures where both "native" and "imported" solids are
  // the same G4OCCTSolid loaded from the same STEP file (i.e., NIST CTC
  // fixtures whose geant4_class is G4OCCTSolid).  Mismatches are always 0 for
  // these imported self-comparison fixtures; BM_safety and BM_polyhedron are
  // skipped for them.
  bool IsImportedSelfComparisonFixture(const g4occt::tests::geometry::FixtureReference& fixture) {
    return fixture.geant4_class == "G4OCCTSolid";
  }

  bool HasErrors(const ValidationReport& report) {
    return std::ranges::any_of(
        report.Messages(), [](const g4occt::tests::geometry::ValidationMessage& message) {
          return message.severity == g4occt::tests::geometry::ValidationSeverity::kError;
        });
  }

  void PrintReportMessages(std::ostream& out, const ValidationReport& report) {
    for (const auto& message : report.Messages()) {
      out << g4occt::tests::geometry::ToString(message.severity) << " [" << message.code << "] "
          << message.text;
      if (!message.path.empty()) {
        out << " :: " << message.path.string();
      }
      out << '\n';
    }
  }

  // ─── Formatting helpers ───────────────────────────────────────────────────────

  /// Format a millisecond value as a right-aligned field (2 decimal places, no padding).
  std::string FormatMs(const double ms) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << ms;
    return oss.str();
  }

  /// Format a performance ratio as "NNN.Nx" — imported divided by native (how many times slower
  /// the imported solid is relative to the native solid). Returns "---" when native time is zero.
  std::string FormatRatio(const double native_ms, const double imported_ms) {
    if (native_ms <= 0.0) {
      return "---";
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << (imported_ms / native_ms) << "x";
    return oss.str();
  }

  /// Print one row of the per-fixture method table.
  /// @param count_label  Label used before the count; use "mismatches" for native-vs-imported
  ///                     comparison rows and "violations" for OCCT lower-bound rows.
  ///                     Pass an empty string to suppress the count field entirely.
  void PrintMethodRow(std::ostream& out, const std::string& label, const double native_ms,
                      const double imported_ms, const std::size_t count,
                      const bool has_timing = true, const std::string& count_label = "mismatches") {
    out << "  " << std::left << std::setw(24) << label << ": ";
    if (has_timing) {
      out << "native=" << std::right << std::setw(8) << FormatMs(native_ms) << "ms"
          << "  imported=" << std::setw(8) << FormatMs(imported_ms) << "ms"
          << "  ratio=" << std::setw(8) << FormatRatio(native_ms, imported_ms);
    } else {
      // Exit normals are computed as part of DistanceToOut — no separate timing.
      out << "native=" << std::right << std::setw(8) << "---" << "ms"
          << "  imported=" << std::setw(8) << "---" << "ms"
          << "  ratio=" << std::setw(8) << "---";
    }
    if (!count_label.empty()) {
      out << "  " << count_label << "=" << count;
    }
    out << "\n";
  }

  /// Print one row of the aggregate summary table.
  void PrintAggregateRow(std::ostream& out, const std::string& label, const double native_ms,
                         const double imported_ms, const std::size_t mismatches,
                         const std::size_t exp_failures, const bool has_timing = true) {
    out << "  " << std::left << std::setw(24) << label;
    if (has_timing) {
      out << std::right << std::setw(12) << FormatMs(native_ms) << std::setw(14)
          << FormatMs(imported_ms) << std::setw(10) << FormatRatio(native_ms, imported_ms);
    } else {
      out << std::right << std::setw(12) << "---" << std::setw(14) << "---" << std::setw(10)
          << "---";
    }
    out << std::setw(13) << mismatches << std::setw(14) << exp_failures << "\n";
  }

  /// Return the number of ray distance/intersection mismatches, excluding exit-normal mismatches
  /// (which are counted separately in normal_mismatch_count but also added to mismatch_count).
  std::size_t RayOnlyMismatches(const g4occt::tests::geometry::FixtureRayComparisonSummary& ray) {
    return ray.mismatch_count >= ray.normal_mismatch_count
               ? ray.mismatch_count - ray.normal_mismatch_count
               : 0U;
  }

  /// Print the CreatePolyhedron() row of the per-fixture method table.
  /// Instead of a mismatch count the row shows vertex and facet counts for both
  /// meshes; these are expected to differ between the analytical native solid and
  /// the OCCT BRepMesh tessellation, so no mismatch is recorded.
  void PrintPolyhedronRow(std::ostream& out,
                          const g4occt::tests::geometry::FixturePolyhedronComparisonSummary& poly) {
    out << "  " << std::left << std::setw(24) << "CreatePolyhedron()" << ": ";
    out << "native=" << std::right << std::setw(8) << FormatMs(poly.native_elapsed_ms) << "ms"
        << "  imported=" << std::setw(8) << FormatMs(poly.imported_elapsed_ms) << "ms"
        << "  ratio=" << std::setw(8)
        << FormatRatio(poly.native_elapsed_ms, poly.imported_elapsed_ms);
    out << "  vertices=" << poly.native_vertices << "/" << poly.imported_vertices
        << "  facets=" << poly.native_facets << "/" << poly.imported_facets << "\n";
  }

  void PrintFixtureSummary(std::ostream& out, const FixtureNavigationSummary& s) {
    out << s.fixture_id << " (" << s.geant4_class << ")";
    if (s.has_expected_failure) {
      out << "  [expected failure]";
    }
    out << ":\n";

    // Row 1: DistanceToIn/Out(p,v) — ray intersection+distance timing.
    PrintMethodRow(out, "DistanceToIn/Out(p,v)", s.ray.native_elapsed_ms, s.ray.imported_elapsed_ms,
                   RayOnlyMismatches(s.ray));

    // Row 2: Exit normals — computed inside DistanceToOut; no separate timing available.
    PrintMethodRow(out, "Exit normals", 0.0, 0.0, s.ray.normal_mismatch_count,
                   /*has_timing=*/false);

    // Row 3: Inside(p) classification.
    PrintMethodRow(out, "Inside(p)", s.inside.native_elapsed_ms, s.inside.imported_elapsed_ms,
                   s.inside.mismatch_count);

    // Row 4: DistanceToIn(p) safety distance — Geant4 vs OCCT timing only (no mismatch count).
    PrintMethodRow(out, "DTI(p) G4 vs OCCT", s.safety.native_safety_in_ms,
                   s.safety.imported_safety_in_ms, 0, /*has_timing=*/true, /*count_label=*/"");

    // Row 5: DistanceToOut(p) safety distance — Geant4 vs OCCT timing only (no mismatch count).
    PrintMethodRow(out, "DTO(p) G4 vs OCCT", s.safety.native_safety_out_ms,
                   s.safety.imported_safety_out_ms, 0, /*has_timing=*/true, /*count_label=*/"");

    // Row 6: DistanceToIn(p) within OCCT — lower bound vs exact.
    PrintMethodRow(out, "DTI(p) OCCT vs Exact", s.safety.imported_safety_in_ms,
                   s.safety.exact_safety_in_ms, s.safety.occt_lower_bound_in_violations,
                   /*has_timing=*/true, /*count_label=*/"violations");

    // Row 7: DistanceToOut(p) within OCCT — lower bound vs exact.
    PrintMethodRow(out, "DTO(p) OCCT vs Exact", s.safety.imported_safety_out_ms,
                   s.safety.exact_safety_out_ms, s.safety.occt_lower_bound_out_violations,
                   /*has_timing=*/true, /*count_label=*/"violations");

    // Row 8: SurfaceNormal(p) post-hoc benchmark at agreed hit points.
    PrintMethodRow(out, "SurfaceNormal(p)", s.ray.native_surface_normal_ms,
                   s.ray.imported_surface_normal_ms, s.ray.surface_normal_mismatch_count);

    // Row 9: CreatePolyhedron() — mesh tessellation timing and mesh-density metrics.
    PrintPolyhedronRow(out, s.polyhedron);

    out << "\n";
  }

  // ─── Benchmark registration ───────────────────────────────────────────────────

  void RegisterBenchmarksForFixtures(const FixtureRepositoryManifest& repo_manifest,
                                     const FixtureRayComparisonOptions& ray_opts,
                                     const FixtureInsideComparisonOptions& inside_opts,
                                     const FixtureSafetyComparisonOptions& safety_opts,
                                     const FixturePolyhedronComparisonOptions& poly_opts) {
    for (const auto& family : repo_manifest.families) {
      const auto family_manifest_path = ResolveFamilyManifestPath(repo_manifest, family);
      if (!std::filesystem::exists(family_manifest_path)) {
        continue;
      }

      FixtureManifest family_manifest;
      try {
        family_manifest = ParseFixtureManifestFile(family_manifest_path);
      } catch (const std::exception& error) {
        std::lock_guard<std::mutex> lk(g_state->mu);
        g_state->aggregate_report.AddError(
            "manifest.parse_failed",
            std::string("Failed to parse family manifest: ") + error.what(), family_manifest_path);
        continue;
      }

      {
        std::lock_guard<std::mutex> lk(g_state->mu);
        g_state->aggregate_report.Append(ValidateManifestStructure(family_manifest));
      }

      for (const auto& fixture : family_manifest.fixtures) {
        FixtureValidationRequest request;
        request.manifest                = family_manifest;
        request.fixture                 = fixture;
        request.require_provenance_file = true;

        const g4occt::tests::geometry::FixtureExpectedFailure expected_failure =
            g4occt::tests::geometry::ExpectedFailureForFixture(request);

        const ValidationReport layout_report = ValidateFixtureLayout(request);
        {
          std::lock_guard<std::mutex> lk(g_state->mu);
          g_state->aggregate_report.Append(layout_report);
        }
        if (!layout_report.Ok()) {
          continue;
        }

        // For imported-self-comparison fixtures (G4OCCTSolid / NIST CTC), register
        // BM_rays and BM_inside benchmarks instead of skipping.
        // Native == imported for these fixtures so mismatches are always 0.
        // BM_safety and BM_polyhedron are skipped for them.
        if (IsImportedSelfComparisonFixture(fixture)) {
          const std::string fixture_id = family_manifest.family + "/" + fixture.id;

          {
            std::lock_guard<std::mutex> lk(g_state->mu);
            if (!g_state->summaries.contains(fixture_id)) {
              g_state->fixture_order.push_back(fixture_id);
              g_state->summaries[fixture_id].fixture_id = fixture_id;
              g_state->summaries[fixture_id].has_expected_failure =
                  expected_failure.enabled || expected_failure.safety_enabled;
            }
          }

          benchmark::RegisterBenchmark(
              ("BM_rays/" + fixture_id).c_str(),
              [fixture_id, request, ray_opts, expected_failure](benchmark::State& state) {
                for (auto _ : state) {
                  g4occt::tests::geometry::FixtureRayComparisonSummary ray;
                  try {
                    ValidationReport report = CompareFixtureRays(request, ray_opts, &ray);
                    report = g4occt::tests::geometry::ReclassifyExpectedFailures(report,
                                                                                 expected_failure);
                    {
                      std::lock_guard<std::mutex> lk(g_state->mu);
                      g_state->summaries[fixture_id].ray = ray;
                      if (g_state->summaries[fixture_id].geant4_class.empty()) {
                        g_state->summaries[fixture_id].geant4_class = ray.geant4_class;
                      }
                      g_state->aggregate_report.Append(report);
                    }
                    state.SetIterationTime(ray.imported_elapsed_ms / 1000.0);
                    state.SetLabel(ray.geant4_class);
                    state.counters["native_ms"]   = ray.native_elapsed_ms;
                    state.counters["imported_ms"] = ray.imported_elapsed_ms;
                    state.counters["mismatches"]  = static_cast<double>(RayOnlyMismatches(ray));
                    state.counters["exit_normal_mismatches"] =
                        static_cast<double>(ray.normal_mismatch_count);
                    state.counters["sn_native_ms"]   = ray.native_surface_normal_ms;
                    state.counters["sn_imported_ms"] = ray.imported_surface_normal_ms;
                    state.counters["sn_mismatches"] =
                        static_cast<double>(ray.surface_normal_mismatch_count);
                    state.counters["has_expected_failure"] = static_cast<double>(
                        expected_failure.enabled || expected_failure.safety_enabled);
                  } catch (const std::exception& ex) {
                    const std::string msg =
                        "[BM_rays/" + fixture_id + "] exception: " + ex.what();
                    std::cerr << msg << "\n";
                    state.SkipWithError(msg.c_str());
                    return;
                  }
                }
              })
              ->UseManualTime()
              ->Iterations(1)
              ->Unit(benchmark::kMillisecond);

          benchmark::RegisterBenchmark(
              ("BM_inside/" + fixture_id).c_str(),
              [fixture_id, request, inside_opts,
               expected_failure](benchmark::State& state) {
                for (auto _ : state) {
                  g4occt::tests::geometry::FixtureInsideComparisonSummary inside;
                  try {
                    ValidationReport report =
                        CompareFixtureInside(request, inside_opts, &inside);
                    report = g4occt::tests::geometry::ReclassifyExpectedFailures(report,
                                                                                 expected_failure);
                    {
                      std::lock_guard<std::mutex> lk(g_state->mu);
                      g_state->summaries[fixture_id].inside = inside;
                      if (g_state->summaries[fixture_id].geant4_class.empty()) {
                        g_state->summaries[fixture_id].geant4_class = inside.geant4_class;
                      }
                      g_state->aggregate_report.Append(report);
                    }
                    state.SetIterationTime(inside.imported_elapsed_ms / 1000.0);
                    state.counters["native_ms"]   = inside.native_elapsed_ms;
                    state.counters["imported_ms"] = inside.imported_elapsed_ms;
                    state.counters["mismatches"]  = static_cast<double>(inside.mismatch_count);
                  } catch (const std::exception& ex) {
                    const std::string msg =
                        "[BM_inside/" + fixture_id + "] exception: " + ex.what();
                    std::cerr << msg << "\n";
                    state.SkipWithError(msg.c_str());
                    return;
                  }
                }
              })
              ->UseManualTime()
              ->Iterations(1)
              ->Unit(benchmark::kMillisecond);

          continue;
        }

        const std::string fixture_id = family_manifest.family + "/" + fixture.id;

        // Pre-create the summary entry to preserve fixture ordering.
        {
          std::lock_guard<std::mutex> lk(g_state->mu);
          if (!g_state->summaries.contains(fixture_id)) {
            g_state->fixture_order.push_back(fixture_id);
            g_state->summaries[fixture_id].fixture_id = fixture_id;
            g_state->summaries[fixture_id].has_expected_failure =
                expected_failure.enabled || expected_failure.safety_enabled;
          }
        }

        // Register ray + SurfaceNormal benchmark.
        // SetLabel encodes geant4_class so the JSON report script can reproduce the
        // fixture header line without a separate lookup.
        benchmark::RegisterBenchmark(
            ("BM_rays/" + fixture_id).c_str(),
            [fixture_id, request, ray_opts, expected_failure](benchmark::State& state) {
              for (auto _ : state) {
                g4occt::tests::geometry::FixtureRayComparisonSummary ray;
                ValidationReport report = CompareFixtureRays(request, ray_opts, &ray);
                report =
                    g4occt::tests::geometry::ReclassifyExpectedFailures(report, expected_failure);
                state.SetIterationTime(ray.imported_elapsed_ms / 1000.0);
                state.SetLabel(ray.geant4_class);
                state.counters["native_ms"]   = ray.native_elapsed_ms;
                state.counters["imported_ms"] = ray.imported_elapsed_ms;
                state.counters["mismatches"]  = static_cast<double>(RayOnlyMismatches(ray));
                state.counters["exit_normal_mismatches"] =
                    static_cast<double>(ray.normal_mismatch_count);
                state.counters["sn_native_ms"]   = ray.native_surface_normal_ms;
                state.counters["sn_imported_ms"] = ray.imported_surface_normal_ms;
                state.counters["sn_mismatches"] =
                    static_cast<double>(ray.surface_normal_mismatch_count);
                state.counters["has_expected_failure"] = static_cast<double>(
                    expected_failure.enabled || expected_failure.safety_enabled);
                std::lock_guard<std::mutex> lk(g_state->mu);
                g_state->summaries[fixture_id].ray = ray;
                if (g_state->summaries[fixture_id].geant4_class.empty()) {
                  g_state->summaries[fixture_id].geant4_class = ray.geant4_class;
                }
                g_state->aggregate_report.Append(report);
              }
            })
            ->UseManualTime()
            ->Iterations(1)
            ->Unit(benchmark::kMillisecond);

        // Register Inside(p) benchmark.
        benchmark::RegisterBenchmark(
            ("BM_inside/" + fixture_id).c_str(),
            [fixture_id, request, inside_opts, expected_failure](benchmark::State& state) {
              for (auto _ : state) {
                g4occt::tests::geometry::FixtureInsideComparisonSummary inside;
                ValidationReport report = CompareFixtureInside(request, inside_opts, &inside);
                report =
                    g4occt::tests::geometry::ReclassifyExpectedFailures(report, expected_failure);
                state.SetIterationTime(inside.imported_elapsed_ms / 1000.0);
                state.counters["native_ms"]   = inside.native_elapsed_ms;
                state.counters["imported_ms"] = inside.imported_elapsed_ms;
                state.counters["mismatches"]  = static_cast<double>(inside.mismatch_count);
                std::lock_guard<std::mutex> lk(g_state->mu);
                g_state->summaries[fixture_id].inside = inside;
                if (g_state->summaries[fixture_id].geant4_class.empty()) {
                  g_state->summaries[fixture_id].geant4_class = inside.geant4_class;
                }
                g_state->aggregate_report.Append(report);
              }
            })
            ->UseManualTime()
            ->Iterations(1)
            ->Unit(benchmark::kMillisecond);

        // Register safety distance benchmark (DistanceToIn(p) + DistanceToOut(p)).
        benchmark::RegisterBenchmark(
            ("BM_safety/" + fixture_id).c_str(),
            [fixture_id, request, safety_opts, expected_failure](benchmark::State& state) {
              for (auto _ : state) {
                g4occt::tests::geometry::FixtureSafetyComparisonSummary safety;
                ValidationReport report = CompareFixtureSafety(request, safety_opts, &safety);
                report =
                    g4occt::tests::geometry::ReclassifyExpectedFailures(report, expected_failure);
                state.SetIterationTime(
                    (safety.imported_safety_in_ms + safety.imported_safety_out_ms) / 1000.0);
                state.counters["safety_in_native_ms"]   = safety.native_safety_in_ms;
                state.counters["safety_in_imported_ms"] = safety.imported_safety_in_ms;
                state.counters["safety_in_exact_ms"]    = safety.exact_safety_in_ms;
                state.counters["safety_in_lb_violations"] =
                    static_cast<double>(safety.occt_lower_bound_in_violations);
                state.counters["safety_in_avg_lb_ratio"]      = safety.avg_dti_lb_ratio;
                state.counters["safety_in_avg_g4_occt_ratio"] = safety.avg_dti_g4_occt_ratio;
                state.counters["safety_out_native_ms"]        = safety.native_safety_out_ms;
                state.counters["safety_out_imported_ms"]      = safety.imported_safety_out_ms;
                state.counters["safety_out_exact_ms"]         = safety.exact_safety_out_ms;
                state.counters["safety_out_lb_violations"] =
                    static_cast<double>(safety.occt_lower_bound_out_violations);
                state.counters["safety_out_avg_lb_ratio"]      = safety.avg_dto_lb_ratio;
                state.counters["safety_out_avg_g4_occt_ratio"] = safety.avg_dto_g4_occt_ratio;
                std::lock_guard<std::mutex> lk(g_state->mu);
                g_state->summaries[fixture_id].safety = safety;
                if (g_state->summaries[fixture_id].geant4_class.empty()) {
                  g_state->summaries[fixture_id].geant4_class = safety.geant4_class;
                }
                g_state->aggregate_report.Append(report);
              }
            })
            ->UseManualTime()
            ->Iterations(1)
            ->Unit(benchmark::kMillisecond);

        // Register CreatePolyhedron() benchmark.
        benchmark::RegisterBenchmark(
            ("BM_polyhedron/" + fixture_id).c_str(),
            [fixture_id, request, poly_opts](benchmark::State& state) {
              for (auto _ : state) {
                g4occt::tests::geometry::FixturePolyhedronComparisonSummary poly;
                const ValidationReport report = CompareFixturePolyhedron(request, poly_opts, &poly);
                state.SetIterationTime(poly.imported_elapsed_ms / 1000.0);
                state.counters["native_ms"]         = poly.native_elapsed_ms;
                state.counters["imported_ms"]       = poly.imported_elapsed_ms;
                state.counters["native_vertices"]   = static_cast<double>(poly.native_vertices);
                state.counters["imported_vertices"] = static_cast<double>(poly.imported_vertices);
                state.counters["native_facets"]     = static_cast<double>(poly.native_facets);
                state.counters["imported_facets"]   = static_cast<double>(poly.imported_facets);
                std::lock_guard<std::mutex> lk(g_state->mu);
                g_state->summaries[fixture_id].polyhedron = poly;
                if (g_state->summaries[fixture_id].geant4_class.empty()) {
                  g_state->summaries[fixture_id].geant4_class = poly.geant4_class;
                }
                g_state->aggregate_report.Append(report);
              }
            })
            ->UseManualTime()
            ->Iterations(1)
            ->Unit(benchmark::kMillisecond);
      }
    }
  }

  // ─── Custom report printing (preserves original tabular format) ───────────────

  void PrintNavigationReport(std::ostream& out, const BenchmarkSharedState& state,
                             const std::size_t ray_count,
                             const FixtureInsideComparisonOptions& inside_opts,
                             const FixtureSafetyComparisonOptions& safety_opts) {
    // Collect nav_summaries in manifest order, filtering out fixtures with no data.
    std::vector<FixtureNavigationSummary> nav_summaries;
    for (const auto& id : state.fixture_order) {
      const auto it = state.summaries.find(id);
      if (it == state.summaries.end()) {
        continue;
      }
      const auto& s       = it->second;
      const bool any_data = s.ray.ray_count > 0U || s.inside.point_count > 0U ||
                            s.safety.point_count > 0U || s.polyhedron.native_valid ||
                            s.polyhedron.imported_valid;
      if (any_data) {
        nav_summaries.push_back(s);
      }
    }

    // ── Per-fixture unified table ─────────────────────────────────────────
    out << "\n=== Fixture Navigation Benchmark Results ===\n";
    out << "Rays: " << ray_count << "  |  Inside points: " << inside_opts.point_count
        << "  |  Safety points: " << safety_opts.point_count << "\n\n";

    for (const auto& s : nav_summaries) {
      PrintFixtureSummary(out, s);
    }

    // ── Aggregate totals per method ───────────────────────────────────────
    double agg_ray_native_ms         = 0.0;
    double agg_ray_imported_ms       = 0.0;
    std::size_t agg_ray_mismatches   = 0;
    std::size_t agg_ray_exp_failures = 0;

    // Exit normals are computed as part of DistanceToOut; no separate timing.
    std::size_t agg_exit_mismatches   = 0;
    std::size_t agg_exit_exp_failures = 0;

    double agg_inside_native_ms         = 0.0;
    double agg_inside_imported_ms       = 0.0;
    std::size_t agg_inside_mismatches   = 0;
    std::size_t agg_inside_exp_failures = 0;

    double agg_dti_native_ms          = 0.0;
    double agg_dti_imported_ms        = 0.0;
    double agg_dti_exact_ms           = 0.0;
    std::size_t agg_dti_lb_violations = 0;
    std::size_t agg_dti_exp_failures  = 0;

    double agg_dto_native_ms          = 0.0;
    double agg_dto_imported_ms        = 0.0;
    double agg_dto_exact_ms           = 0.0;
    std::size_t agg_dto_lb_violations = 0;
    std::size_t agg_dto_exp_failures  = 0;

    double agg_sn_native_ms         = 0.0;
    double agg_sn_imported_ms       = 0.0;
    std::size_t agg_sn_mismatches   = 0;
    std::size_t agg_sn_exp_failures = 0;

    double agg_poly_native_ms   = 0.0;
    double agg_poly_imported_ms = 0.0;

    for (const auto& s : nav_summaries) {
      if (s.ray.ray_count > 0U) {
        agg_ray_native_ms += s.ray.native_elapsed_ms;
        agg_ray_imported_ms += s.ray.imported_elapsed_ms;
        agg_ray_mismatches += RayOnlyMismatches(s.ray);
        if (s.has_expected_failure) {
          ++agg_ray_exp_failures;
        }

        // Exit normals are extracted from the ray data.
        agg_exit_mismatches += s.ray.normal_mismatch_count;
        if (s.has_expected_failure) {
          ++agg_exit_exp_failures;
        }

        // SurfaceNormal(p) is benchmarked at agreed ray hit points.
        agg_sn_native_ms += s.ray.native_surface_normal_ms;
        agg_sn_imported_ms += s.ray.imported_surface_normal_ms;
        agg_sn_mismatches += s.ray.surface_normal_mismatch_count;
        if (s.has_expected_failure) {
          ++agg_sn_exp_failures;
        }
      }

      if (s.inside.point_count > 0U) {
        agg_inside_native_ms += s.inside.native_elapsed_ms;
        agg_inside_imported_ms += s.inside.imported_elapsed_ms;
        agg_inside_mismatches += s.inside.mismatch_count;
        if (s.has_expected_failure) {
          ++agg_inside_exp_failures;
        }
      }

      if (s.safety.point_count > 0U) {
        agg_dti_native_ms += s.safety.native_safety_in_ms;
        agg_dti_imported_ms += s.safety.imported_safety_in_ms;
        agg_dti_exact_ms += s.safety.exact_safety_in_ms;
        agg_dti_lb_violations += s.safety.occt_lower_bound_in_violations;
        agg_dto_native_ms += s.safety.native_safety_out_ms;
        agg_dto_imported_ms += s.safety.imported_safety_out_ms;
        agg_dto_exact_ms += s.safety.exact_safety_out_ms;
        agg_dto_lb_violations += s.safety.occt_lower_bound_out_violations;
        if (s.has_expected_failure) {
          ++agg_dti_exp_failures;
          ++agg_dto_exp_failures;
        }
      }

      // Accumulate CreatePolyhedron() timing whenever either mesh was produced.
      if (s.polyhedron.native_valid || s.polyhedron.imported_valid) {
        agg_poly_native_ms += s.polyhedron.native_elapsed_ms;
        agg_poly_imported_ms += s.polyhedron.imported_elapsed_ms;
      }
    }

    out << "Aggregate:\n";
    out << "  " << std::left << std::setw(24) << "Method" << std::right << std::setw(12)
        << "Native(ms)" << std::setw(14) << "Imported(ms)" << std::setw(10) << "Ratio"
        << std::setw(13) << "Mismatches" << std::setw(14) << "Exp. Failures" << "\n";
    out << "  " << std::string(85, '-') << "\n";

    PrintAggregateRow(out, "DistanceToIn/Out(p,v)", agg_ray_native_ms, agg_ray_imported_ms,
                      agg_ray_mismatches, agg_ray_exp_failures);
    PrintAggregateRow(out, "Exit normals", 0.0, 0.0, agg_exit_mismatches, agg_exit_exp_failures,
                      /*has_timing=*/false);
    PrintAggregateRow(out, "Inside(p)", agg_inside_native_ms, agg_inside_imported_ms,
                      agg_inside_mismatches, agg_inside_exp_failures);
    PrintAggregateRow(out, "DTI(p) G4 vs OCCT", agg_dti_native_ms, agg_dti_imported_ms, 0,
                      agg_dti_exp_failures);
    PrintAggregateRow(out, "DTO(p) G4 vs OCCT", agg_dto_native_ms, agg_dto_imported_ms, 0,
                      agg_dto_exp_failures);
    PrintAggregateRow(out, "DTI(p) OCCT vs Exact", agg_dti_imported_ms, agg_dti_exact_ms,
                      agg_dti_lb_violations, agg_dti_exp_failures);
    PrintAggregateRow(out, "DTO(p) OCCT vs Exact", agg_dto_imported_ms, agg_dto_exact_ms,
                      agg_dto_lb_violations, agg_dto_exp_failures);
    PrintAggregateRow(out, "SurfaceNormal(p)", agg_sn_native_ms, agg_sn_imported_ms,
                      agg_sn_mismatches, agg_sn_exp_failures);
    // CreatePolyhedron() — timing totals only; no mismatch count because native
    // and imported meshes are expected to differ in vertex/facet layout.
    out << "  " << std::left << std::setw(24) << "CreatePolyhedron()" << std::right << std::setw(12)
        << FormatMs(agg_poly_native_ms) << std::setw(14) << FormatMs(agg_poly_imported_ms)
        << std::setw(10) << FormatRatio(agg_poly_native_ms, agg_poly_imported_ms) << std::setw(13)
        << "---" << std::setw(14) << "---" << "\n";
  }

} // namespace

// ─── Entry point (called from main) ──────────────────────────────────────────
// Must be outside the anonymous namespace so it is accessible as
// g4occt::benchmarks::RunBenchmark from main().

int RunBenchmark(const std::filesystem::path& repository_manifest_path, const std::size_t ray_count,
                 const std::filesystem::path& point_cloud_dir, const bool json_to_stdout) {
  const FixtureRepositoryManifest repository_manifest =
      ParseFixtureRepositoryManifest(repository_manifest_path);

  FixtureRayComparisonOptions ray_opts;
  ray_opts.ray_count       = ray_count;
  ray_opts.point_cloud_dir = point_cloud_dir;

  FixtureInsideComparisonOptions inside_opts;
  inside_opts.point_count = ray_count;
  // Keep the benchmark on bulk-classification points: the ray benchmark
  // already probes boundary behaviour, while boundary-adjacent Inside()
  // queries can wedge OCCT's imported-solid classifier for some fixtures.
  inside_opts.include_near_surface_points = false;

  FixtureSafetyComparisonOptions safety_opts;
  safety_opts.point_count = ray_count;

  const FixturePolyhedronComparisonOptions poly_opts;

  BenchmarkSharedState state;
  g_state = &state;

  state.aggregate_report.Append(ValidateRepositoryLayout(repository_manifest));

  RegisterBenchmarksForFixtures(repository_manifest, ray_opts, inside_opts, safety_opts, poly_opts);

  // Expose ray/inside/safety counts in the JSON context so bench_report.py
  // can reproduce the "Rays: N | Inside points: N | Safety points: N" header.
  benchmark::AddCustomContext("ray_count", std::to_string(ray_count));
  benchmark::AddCustomContext("inside_count", std::to_string(inside_opts.point_count));
  benchmark::AddCustomContext("safety_count", std::to_string(safety_opts.point_count));

  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();

  // Redirect the custom table to stderr only when JSON is going to stdout
  // (i.e., --benchmark_format=json without --benchmark_out).  When a file
  // is specified for JSON output, stdout is free and the table goes there.
  std::ostream& report_out = json_to_stdout ? std::cerr : std::cout;

  // ── Per-fixture unified table + aggregate ─────────────────────────────
  PrintReportMessages(report_out, state.aggregate_report);
  if (HasErrors(state.aggregate_report)) {
    g_state = nullptr;
    return EXIT_FAILURE;
  }

  PrintNavigationReport(report_out, state, ray_count, inside_opts, safety_opts);

  // Fail explicitly when any correctness mismatch is detected across non-expected-failure
  // fixtures.  Each of these raw counts can exceed the ValidationReport error cap
  // (max_reported_mismatches per fixture) without the aggregate HasErrors() check
  // catching the full extent of failures:
  //
  //  - ray_mismatches / exit_mismatches share a combined mismatch_count cap, so
  //    exit-normal errors may be silently dropped if intersection/distance errors
  //    exhaust the cap first.
  //  - surface_normal_mismatch_count is tracked separately from the ray report
  //    error count; the benchmark JSON counter carries the total while the report
  //    only carries ≤ max_reported_mismatches error messages per fixture.
  //  - inside_mismatches: same cap issue; belt-and-suspenders guard.
  //
  // Expected-failure fixtures (has_expected_failure == true) are excluded because
  // their errors are reclassified to warnings by ReclassifyExpectedFailures.

  std::size_t total_ray_mismatches    = 0;
  std::size_t total_exit_mismatches   = 0;
  std::size_t total_sn_mismatches     = 0;
  std::size_t total_inside_mismatches = 0;
  for (const auto& [id, s] : state.summaries) {
    if (s.has_expected_failure) {
      continue;
    }
    const std::size_t ray_only = s.ray.mismatch_count >= s.ray.normal_mismatch_count
                                     ? s.ray.mismatch_count - s.ray.normal_mismatch_count
                                     : 0U;
    total_ray_mismatches += ray_only;
    total_exit_mismatches += s.ray.normal_mismatch_count;
    total_sn_mismatches += s.ray.surface_normal_mismatch_count;
    total_inside_mismatches += s.inside.mismatch_count;
  }

  bool has_mismatch_failures = false;
  const auto report_if_nonzero = [&](std::size_t count, std::string_view label) {
    if (count > 0) {
      report_out << "ERROR: " << count << " " << label
                 << " mismatch(es) detected across all non-xfail fixtures.\n";
      has_mismatch_failures = true;
    }
  };
  report_if_nonzero(total_ray_mismatches, "DistanceToIn/Out(p,v)");
  report_if_nonzero(total_exit_mismatches, "exit-normal");
  report_if_nonzero(total_sn_mismatches, "SurfaceNormal(p)");
  report_if_nonzero(total_inside_mismatches, "Inside(p)");

  if (has_mismatch_failures) {
    g_state = nullptr;
    return EXIT_FAILURE;
  }

  g_state = nullptr;
  return EXIT_SUCCESS;
}

} // namespace g4occt::benchmarks

int main(int argc, char** argv) {
  try {
    // Detect whether JSON output is going to stdout (pipe-to-bench_report.py
    // use case) before Initialize() removes the benchmark flags from argv.
    // This is true when --benchmark_format=json is present but --benchmark_out
    // is absent (JSON to stdout corrupts the pipe if we also print our table).
    bool has_json_format = false;
    bool has_json_out    = false;
    for (int i = 1; i < argc; ++i) {
      const std::string arg(argv[i]);
      if (arg == "--benchmark_format=json") {
        has_json_format = true;
      }
      if (arg.starts_with("--benchmark_out=") || arg == "--benchmark_out") {
        has_json_out = true;
      }
    }
    const bool json_to_stdout = has_json_format && !has_json_out;

    // Let Google Benchmark consume its own --benchmark_* flags.
    benchmark::Initialize(&argc, argv);

    // Parse remaining args: [N_rays] [manifest_path] [point_cloud_dir]
    std::size_t ray_count = 2048;
    if (argc > 1) {
      ray_count = static_cast<std::size_t>(std::stoul(argv[1]));
    }
    const std::filesystem::path manifest_path =
        argc > 2 ? std::filesystem::path(argv[2])
                 : g4occt::tests::geometry::DefaultRepositoryManifestPath();
    const std::filesystem::path point_cloud_dir = argc > 3 ? std::filesystem::path(argv[3]) : "";

    return g4occt::benchmarks::RunBenchmark(manifest_path, ray_count, point_cloud_dir,
                                            json_to_stdout);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: benchmark setup threw an exception: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
