// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_ray_compare.hh"
#include "geometry/fixture_inside_compare.hh"
#include "geometry/fixture_safety_compare.hh"

#include <cstdlib>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace g4occt::benchmarks {
namespace {

  using g4occt::tests::geometry::CompareFixtureInside;
  using g4occt::tests::geometry::CompareFixtureRays;
  using g4occt::tests::geometry::CompareFixtureSafety;
  using g4occt::tests::geometry::DefaultRepositoryManifestPath;
  using g4occt::tests::geometry::FixtureInsideComparisonOptions;
  using g4occt::tests::geometry::FixtureInsideComparisonSummary;
  using g4occt::tests::geometry::FixtureManifest;
  using g4occt::tests::geometry::FixtureRayComparisonOptions;
  using g4occt::tests::geometry::FixtureRayComparisonSummary;
  using g4occt::tests::geometry::FixtureRepositoryManifest;
  using g4occt::tests::geometry::FixtureSafetyComparisonOptions;
  using g4occt::tests::geometry::FixtureSafetyComparisonSummary;
  using g4occt::tests::geometry::FixtureValidationRequest;
  using g4occt::tests::geometry::ParseFixtureManifestFile;
  using g4occt::tests::geometry::ParseFixtureRepositoryManifest;
  using g4occt::tests::geometry::ResolveFamilyManifestPath;
  using g4occt::tests::geometry::ValidateFixtureLayout;
  using g4occt::tests::geometry::ValidateManifestStructure;
  using g4occt::tests::geometry::ValidateRepositoryLayout;
  using g4occt::tests::geometry::ValidationReport;

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

    std::vector<FixtureRayComparisonSummary> summaries;
    std::vector<FixtureInsideComparisonSummary> inside_summaries;
    std::vector<FixtureSafetyComparisonSummary> safety_summaries;
    std::size_t expected_failure_count = 0;
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

        FixtureRayComparisonSummary summary;
        ValidationReport ray_report = CompareFixtureRays(request, options, &summary);
        if (expected_failure.enabled) {
          ++expected_failure_count;
          ray_report = g4occt::tests::geometry::ReclassifyExpectedFailures(ray_report,
                                                                           expected_failure.reason);
        }
        aggregate_report.Append(ray_report);
        if (summary.ray_count > 0U) {
          summaries.push_back(summary);
        }

        FixtureInsideComparisonSummary inside_summary;
        ValidationReport inside_report =
            CompareFixtureInside(request, inside_options, &inside_summary);
        if (expected_failure.enabled) {
          inside_report = g4occt::tests::geometry::ReclassifyExpectedFailures(
              inside_report, expected_failure.reason);
        }
        aggregate_report.Append(inside_report);
        if (inside_summary.point_count > 0U) {
          inside_summaries.push_back(inside_summary);
        }

        FixtureSafetyComparisonSummary safety_summary;
        ValidationReport safety_report =
            CompareFixtureSafety(request, safety_options, &safety_summary);
        if (expected_failure.enabled) {
          safety_report = g4occt::tests::geometry::ReclassifyExpectedFailures(
              safety_report, expected_failure.reason);
        }
        aggregate_report.Append(safety_report);
        if (safety_summary.point_count > 0U) {
          safety_summaries.push_back(safety_summary);
        }
      }
    }

    double total_native_ms              = 0.0;
    double total_imported_ms            = 0.0;
    std::size_t total_mismatches        = 0;
    std::size_t total_normal_mismatches = 0;

    std::cout << "\n=== Fixture Ray Benchmark Results ===\n";
    std::cout << "Rays per fixture: " << ray_count << "\n";
    for (const auto& summary : summaries) {
      total_native_ms += summary.native_elapsed_ms;
      total_imported_ms += summary.imported_elapsed_ms;
      total_mismatches += summary.mismatch_count;
      total_normal_mismatches += summary.normal_mismatch_count;
      std::cout << summary.fixture_id << " (" << summary.geant4_class
                << "): native=" << summary.native_elapsed_ms
                << " ms, imported=" << summary.imported_elapsed_ms
                << " ms, mismatches=" << summary.mismatch_count
                << ", normal_mismatches=" << summary.normal_mismatch_count << "\n";
    }

    std::cout << "Aggregate native   : " << total_native_ms << " ms\n";
    std::cout << "Aggregate imported : " << total_imported_ms << " ms\n";
    if (total_imported_ms > 0.0) {
      std::cout << "Native/imported ratio: " << total_native_ms / total_imported_ms << "\n";
    }
    std::cout << "Total mismatches: " << total_mismatches << "\n";
    std::cout << "Total normal mismatches: " << total_normal_mismatches << "\n";
    if (expected_failure_count > 0U) {
      std::cout << "Expected failures: " << expected_failure_count << "\n";
    }

    PrintReportMessages(aggregate_report);
    if (HasErrors(aggregate_report)) {
      return EXIT_FAILURE;
    }

    double total_inside_native_ms        = 0.0;
    double total_inside_imported_ms      = 0.0;
    std::size_t total_inside_mismatches  = 0;
    std::size_t total_inside_ambiguities = 0;

    std::cout << "\n=== Fixture Inside Benchmark Results ===\n";
    for (const auto& s : inside_summaries) {
      total_inside_native_ms += s.native_elapsed_ms;
      total_inside_imported_ms += s.imported_elapsed_ms;
      total_inside_mismatches += s.mismatch_count;
      total_inside_ambiguities += s.surface_ambiguity_count;
      std::cout << s.fixture_id << " (" << s.geant4_class << "): native=" << s.native_elapsed_ms
                << " ms, imported=" << s.imported_elapsed_ms << " ms, points=" << s.point_count
                << ", hard_mismatches=" << s.mismatch_count
                << ", surface_ambiguities=" << s.surface_ambiguity_count << "\n";
    }
    std::cout << "Aggregate native   : " << total_inside_native_ms << " ms\n";
    std::cout << "Aggregate imported : " << total_inside_imported_ms << " ms\n";
    if (total_inside_imported_ms > 0.0) {
      std::cout << "Native/imported ratio: " << total_inside_native_ms / total_inside_imported_ms
                << "\n";
    }
    std::cout << "Total hard mismatches: " << total_inside_mismatches << "\n";
    std::cout << "Total surface ambiguities: " << total_inside_ambiguities << "\n";

    double total_safety_in_native_ms   = 0.0;
    double total_safety_in_imported_ms = 0.0;
    double total_safety_out_native_ms  = 0.0;
    double total_safety_out_imported_ms = 0.0;
    std::size_t total_safety_in_mismatches  = 0;
    std::size_t total_safety_out_mismatches = 0;

    std::cout << "\n=== Fixture Safety Distance Benchmark Results ===\n";
    for (const auto& s : safety_summaries) {
      total_safety_in_native_ms += s.native_safety_in_ms;
      total_safety_in_imported_ms += s.imported_safety_in_ms;
      total_safety_out_native_ms += s.native_safety_out_ms;
      total_safety_out_imported_ms += s.imported_safety_out_ms;
      total_safety_in_mismatches += s.safety_in_mismatch_count;
      total_safety_out_mismatches += s.safety_out_mismatch_count;
      std::cout << s.fixture_id << " (" << s.geant4_class << "): points=" << s.point_count
                << "; DistanceToIn: native=" << s.native_safety_in_ms
                << " ms, imported=" << s.imported_safety_in_ms
                << " ms, mismatches=" << s.safety_in_mismatch_count
                << "; DistanceToOut: native=" << s.native_safety_out_ms
                << " ms, imported=" << s.imported_safety_out_ms
                << " ms, mismatches=" << s.safety_out_mismatch_count << "\n";
    }
    std::cout << "Aggregate DistanceToIn  native   : " << total_safety_in_native_ms << " ms\n";
    std::cout << "Aggregate DistanceToIn  imported : " << total_safety_in_imported_ms << " ms\n";
    if (total_safety_in_imported_ms > 0.0) {
      std::cout << "DistanceToIn  native/imported ratio: "
                << total_safety_in_native_ms / total_safety_in_imported_ms << "\n";
    }
    std::cout << "Aggregate DistanceToOut native   : " << total_safety_out_native_ms << " ms\n";
    std::cout << "Aggregate DistanceToOut imported : " << total_safety_out_imported_ms << " ms\n";
    if (total_safety_out_imported_ms > 0.0) {
      std::cout << "DistanceToOut native/imported ratio: "
                << total_safety_out_native_ms / total_safety_out_imported_ms << "\n";
    }
    std::cout << "Total DistanceToIn  mismatches: " << total_safety_in_mismatches << "\n";
    std::cout << "Total DistanceToOut mismatches: " << total_safety_out_mismatches << "\n";
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
