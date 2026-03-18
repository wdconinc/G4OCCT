// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_navigation_summary.hh"
#include "geometry/fixture_ray_compare.hh"
#include "geometry/fixture_inside_compare.hh"
#include "geometry/fixture_safety_compare.hh"

#include <cstdlib>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace g4occt::benchmarks {
namespace {

  using g4occt::tests::geometry::CompareFixtureInside;
  using g4occt::tests::geometry::CompareFixtureRays;
  using g4occt::tests::geometry::CompareFixtureSafety;
  using g4occt::tests::geometry::DefaultRepositoryManifestPath;
  using g4occt::tests::geometry::FixtureInsideComparisonOptions;
  using g4occt::tests::geometry::FixtureManifest;
  using g4occt::tests::geometry::FixtureNavigationSummary;
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

  // Returns true for fixtures where both "native" and "imported" solids are
  // the same G4OCCTSolid loaded from the same STEP file (i.e., NIST CTC
  // fixtures whose geant4_class is G4OCCTSolid).  Navigation comparison on
  // these very large compound assemblies is extremely slow and produces
  // G4Exceptions; geometry import and volume checks are covered by
  // test_nist_ctc_inside_volume (test_geometry_validation/nist-ctc-* is
  // temporarily disabled).
  bool IsImportedSelfComparisonFixture(const g4occt::tests::geometry::FixtureReference& fixture) {
    return fixture.geant4_class == "G4OCCTSolid";
  }

  bool HasErrors(const ValidationReport& report) {
    return std::any_of(report.Messages().begin(), report.Messages().end(),
                       [](const g4occt::tests::geometry::ValidationMessage& message) {
                         return message.severity ==
                                g4occt::tests::geometry::ValidationSeverity::kError;
                       });
  }

  void PrintReportMessages(const ValidationReport& report) {
    for (const auto& message : report.Messages()) {
      std::cout << g4occt::tests::geometry::ToString(message.severity) << " [" << message.code
                << "] " << message.text;
      if (!message.path.empty()) {
        std::cout << " :: " << message.path.string();
      }
      std::cout << '\n';
    }
  }

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
  void PrintMethodRow(const std::string& label, const double native_ms, const double imported_ms,
                      const std::size_t mismatches, const bool has_timing = true) {
    std::cout << "  " << std::left << std::setw(24) << label << ": ";
    if (has_timing) {
      std::cout << "native=" << std::right << std::setw(8) << FormatMs(native_ms) << "ms"
                << "  imported=" << std::setw(8) << FormatMs(imported_ms) << "ms"
                << "  ratio=" << std::setw(8) << FormatRatio(native_ms, imported_ms);
    } else {
      // Exit normals are computed as part of DistanceToOut — no separate timing.
      std::cout << "native=" << std::right << std::setw(8) << "---" << "ms"
                << "  imported=" << std::setw(8) << "---" << "ms"
                << "  ratio=" << std::setw(8) << "---";
    }
    std::cout << "  mismatches=" << mismatches << "\n";
  }

  /// Print one row of the aggregate summary table.
  void PrintAggregateRow(const std::string& label, const double native_ms, const double imported_ms,
                         const std::size_t mismatches, const std::size_t exp_failures,
                         const bool has_timing = true) {
    std::cout << "  " << std::left << std::setw(24) << label;
    if (has_timing) {
      std::cout << std::right << std::setw(12) << FormatMs(native_ms) << std::setw(14)
                << FormatMs(imported_ms) << std::setw(10) << FormatRatio(native_ms, imported_ms);
    } else {
      std::cout << std::right << std::setw(12) << "---" << std::setw(14) << "---" << std::setw(10)
                << "---";
    }
    std::cout << std::setw(13) << mismatches << std::setw(14) << exp_failures << "\n";
  }

  /// Return the number of ray distance/intersection mismatches, excluding exit-normal mismatches
  /// (which are counted separately in normal_mismatch_count but also added to mismatch_count).
  std::size_t RayOnlyMismatches(const g4occt::tests::geometry::FixtureRayComparisonSummary& ray) {
    return ray.mismatch_count >= ray.normal_mismatch_count
               ? ray.mismatch_count - ray.normal_mismatch_count
               : 0U;
  }

  void PrintFixtureSummary(const FixtureNavigationSummary& s) {
    std::cout << s.fixture_id << " (" << s.geant4_class << ")";
    if (s.has_expected_failure) {
      std::cout << "  [expected failure]";
    }
    std::cout << ":\n";

    // Row 1: DistanceToIn/Out(p,v) — ray intersection+distance timing.
    PrintMethodRow("DistanceToIn/Out(p,v)", s.ray.native_elapsed_ms, s.ray.imported_elapsed_ms,
                   RayOnlyMismatches(s.ray));

    // Row 2: Exit normals — computed inside DistanceToOut; no separate timing available.
    PrintMethodRow("Exit normals", 0.0, 0.0, s.ray.normal_mismatch_count, /*has_timing=*/false);

    // Row 3: Inside(p) classification.
    PrintMethodRow("Inside(p)", s.inside.native_elapsed_ms, s.inside.imported_elapsed_ms,
                   s.inside.mismatch_count);

    // Row 4: DistanceToIn(p) safety distance (outside points).
    PrintMethodRow("DistanceToIn(p)", s.safety.native_safety_in_ms, s.safety.imported_safety_in_ms,
                   s.safety.safety_in_mismatch_count);

    // Row 5: DistanceToOut(p) safety distance (inside points).
    PrintMethodRow("DistanceToOut(p)", s.safety.native_safety_out_ms,
                   s.safety.imported_safety_out_ms, s.safety.safety_out_mismatch_count);

    // Row 6: SurfaceNormal(p) post-hoc benchmark at agreed hit points.
    PrintMethodRow("SurfaceNormal(p)", s.ray.native_surface_normal_ms,
                   s.ray.imported_surface_normal_ms, s.ray.surface_normal_mismatch_count);

    std::cout << "\n";
  }

  int RunBenchmark(const std::filesystem::path& repository_manifest_path,
                   const std::size_t ray_count, const std::filesystem::path& point_cloud_dir) {
    const FixtureRepositoryManifest repository_manifest =
        ParseFixtureRepositoryManifest(repository_manifest_path);

    ValidationReport aggregate_report;
    aggregate_report.Append(ValidateRepositoryLayout(repository_manifest));

    FixtureRayComparisonOptions options;
    options.ray_count       = ray_count;
    options.point_cloud_dir = point_cloud_dir;
    FixtureInsideComparisonOptions inside_options;
    inside_options.point_count = ray_count;
    // Keep the benchmark on bulk-classification points: the ray benchmark
    // already probes boundary behaviour, while boundary-adjacent `Inside()`
    // queries can wedge OCCT's imported-solid classifier for some fixtures.
    inside_options.include_near_surface_points = false;
    FixtureSafetyComparisonOptions safety_options;
    safety_options.point_count = ray_count;

    std::vector<FixtureNavigationSummary> nav_summaries;

    for (const auto& family : repository_manifest.families) {
      const auto family_manifest_path = ResolveFamilyManifestPath(repository_manifest, family);
      if (!std::filesystem::exists(family_manifest_path)) {
        continue;
      }

      FixtureManifest family_manifest;
      try {
        family_manifest = ParseFixtureManifestFile(family_manifest_path);
      } catch (const std::exception& error) {
        aggregate_report.AddError("manifest.parse_failed",
                                  std::string("Failed to parse family manifest: ") + error.what(),
                                  family_manifest_path);
        continue;
      }
      aggregate_report.Append(ValidateManifestStructure(family_manifest));

      for (const auto& fixture : family_manifest.fixtures) {
        FixtureValidationRequest request;
        request.manifest                = family_manifest;
        request.fixture                 = fixture;
        request.require_provenance_file = true;
        const g4occt::tests::geometry::FixtureExpectedFailure expected_failure =
            g4occt::tests::geometry::ExpectedFailureForFixture(request);

        const ValidationReport layout_report = ValidateFixtureLayout(request);
        aggregate_report.Append(layout_report);
        if (!layout_report.Ok()) {
          continue;
        }

        FixtureNavigationSummary nav;
        nav.fixture_id           = family_manifest.family + "/" + fixture.id;
        nav.has_expected_failure = expected_failure.enabled;

        // Skip navigation comparisons for imported-only fixtures (G4OCCTSolid /
        // NIST CTC).  These are large, complex AP203 assemblies: navigation is
        // very slow and triggers G4Exceptions.  Geometry import and volume
        // checks are covered by test_nist_ctc_inside_volume
        // (test_geometry_validation/nist-ctc-* is temporarily disabled);
        // navigation benchmarking will be re-enabled once a per-fixture timeout
        // mechanism is in place.
        if (!IsImportedSelfComparisonFixture(fixture)) {
          ValidationReport ray_report = CompareFixtureRays(request, options, &nav.ray);
          if (expected_failure.enabled) {
            ray_report = g4occt::tests::geometry::ReclassifyExpectedFailures(
                ray_report, expected_failure);
          }
          aggregate_report.Append(ray_report);

          ValidationReport inside_report =
              CompareFixtureInside(request, inside_options, &nav.inside);
          if (expected_failure.enabled) {
            inside_report = g4occt::tests::geometry::ReclassifyExpectedFailures(
                inside_report, expected_failure);
          }
          aggregate_report.Append(inside_report);

          ValidationReport safety_report =
              CompareFixtureSafety(request, safety_options, &nav.safety);
          if (expected_failure.enabled) {
            safety_report = g4occt::tests::geometry::ReclassifyExpectedFailures(
                safety_report, expected_failure);
          }
          aggregate_report.Append(safety_report);
        }

        // Use the geant4_class from whichever sub-summary is populated.
        if (!nav.ray.geant4_class.empty()) {
          nav.geant4_class = nav.ray.geant4_class;
        } else if (!nav.inside.geant4_class.empty()) {
          nav.geant4_class = nav.inside.geant4_class;
        } else if (!nav.safety.geant4_class.empty()) {
          nav.geant4_class = nav.safety.geant4_class;
        }

        const bool any_data =
            nav.ray.ray_count > 0U || nav.inside.point_count > 0U || nav.safety.point_count > 0U;
        if (any_data) {
          nav_summaries.push_back(nav);
        }
      }
    }

    // ── Per-fixture unified table ─────────────────────────────────────────
    std::cout << "\n=== Fixture Navigation Benchmark Results ===\n";
    std::cout << "Rays: " << ray_count << "  |  Inside points: " << inside_options.point_count
              << "  |  Safety points: " << safety_options.point_count << "\n\n";

    for (const auto& s : nav_summaries) {
      PrintFixtureSummary(s);
    }

    PrintReportMessages(aggregate_report);
    if (HasErrors(aggregate_report)) {
      return EXIT_FAILURE;
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

    double agg_dti_native_ms         = 0.0;
    double agg_dti_imported_ms       = 0.0;
    std::size_t agg_dti_mismatches   = 0;
    std::size_t agg_dti_exp_failures = 0;

    double agg_dto_native_ms         = 0.0;
    double agg_dto_imported_ms       = 0.0;
    std::size_t agg_dto_mismatches   = 0;
    std::size_t agg_dto_exp_failures = 0;

    double agg_sn_native_ms         = 0.0;
    double agg_sn_imported_ms       = 0.0;
    std::size_t agg_sn_mismatches   = 0;
    std::size_t agg_sn_exp_failures = 0;

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
        agg_dti_mismatches += s.safety.safety_in_mismatch_count;
        agg_dto_native_ms += s.safety.native_safety_out_ms;
        agg_dto_imported_ms += s.safety.imported_safety_out_ms;
        agg_dto_mismatches += s.safety.safety_out_mismatch_count;
        if (s.has_expected_failure) {
          ++agg_dti_exp_failures;
          ++agg_dto_exp_failures;
        }
      }
    }

    std::cout << "Aggregate:\n";
    std::cout << "  " << std::left << std::setw(24) << "Method" << std::right << std::setw(12)
              << "Native(ms)" << std::setw(14) << "Imported(ms)" << std::setw(10) << "Ratio"
              << std::setw(13) << "Mismatches" << std::setw(14) << "Exp. Failures" << "\n";
    std::cout << "  " << std::string(85, '-') << "\n";

    PrintAggregateRow("DistanceToIn/Out(p,v)", agg_ray_native_ms, agg_ray_imported_ms,
                      agg_ray_mismatches, agg_ray_exp_failures);
    PrintAggregateRow("Exit normals", 0.0, 0.0, agg_exit_mismatches, agg_exit_exp_failures,
                      /*has_timing=*/false);
    PrintAggregateRow("Inside(p)", agg_inside_native_ms, agg_inside_imported_ms,
                      agg_inside_mismatches, agg_inside_exp_failures);
    PrintAggregateRow("DistanceToIn(p)", agg_dti_native_ms, agg_dti_imported_ms, agg_dti_mismatches,
                      agg_dti_exp_failures);
    PrintAggregateRow("DistanceToOut(p)", agg_dto_native_ms, agg_dto_imported_ms,
                      agg_dto_mismatches, agg_dto_exp_failures);
    PrintAggregateRow("SurfaceNormal(p)", agg_sn_native_ms, agg_sn_imported_ms, agg_sn_mismatches,
                      agg_sn_exp_failures);

    return EXIT_SUCCESS;
  }

} // namespace
} // namespace g4occt::benchmarks

int main(int argc, char** argv) {
  try {
    std::size_t ray_count = 2048;
    if (argc > 1) {
      ray_count = static_cast<std::size_t>(std::stoul(argv[1]));
    }
    const std::filesystem::path manifest_path =
        argc > 2 ? std::filesystem::path(argv[2])
                 : g4occt::tests::geometry::DefaultRepositoryManifestPath();
    const std::filesystem::path point_cloud_dir = argc > 3 ? std::filesystem::path(argv[3]) : "";
    return g4occt::benchmarks::RunBenchmark(manifest_path, ray_count, point_cloud_dir);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: benchmark setup threw an exception: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
