// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_ray_compare.hh"

#include <cstdlib>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace g4occt::benchmarks {
namespace {

using g4occt::tests::geometry::CompareFixtureRays;
using g4occt::tests::geometry::DefaultRepositoryManifestPath;
using g4occt::tests::geometry::FixtureManifest;
using g4occt::tests::geometry::FixtureRayComparisonOptions;
using g4occt::tests::geometry::FixtureRayComparisonSummary;
using g4occt::tests::geometry::FixtureRepositoryManifest;
using g4occt::tests::geometry::FixtureValidationRequest;
using g4occt::tests::geometry::ParseFixtureManifestFile;
using g4occt::tests::geometry::ParseFixtureRepositoryManifest;
using g4occt::tests::geometry::ResolveFamilyManifestPath;
using g4occt::tests::geometry::ValidateFixtureLayout;
using g4occt::tests::geometry::ValidateManifestStructure;
using g4occt::tests::geometry::ValidateRepositoryLayout;
using g4occt::tests::geometry::ValidationReport;

bool HasErrors(const ValidationReport& report) {
  return std::any_of(
      report.Messages().begin(),
      report.Messages().end(),
      [](const g4occt::tests::geometry::ValidationMessage& message) {
        return message.severity == g4occt::tests::geometry::ValidationSeverity::kError;
      });
}

void PrintReportMessages(const ValidationReport& report) {
  for (const auto& message : report.Messages()) {
    std::cout << g4occt::tests::geometry::ToString(message.severity) << " [" << message.code << "] "
              << message.text;
    if (!message.path.empty()) {
      std::cout << " :: " << message.path.string();
    }
    std::cout << '\n';
  }
}

int RunBenchmark(const std::filesystem::path& repository_manifest_path, const std::size_t ray_count) {
  const FixtureRepositoryManifest repository_manifest =
      ParseFixtureRepositoryManifest(repository_manifest_path);

  ValidationReport aggregate_report;
  aggregate_report.Append(ValidateRepositoryLayout(repository_manifest));

  FixtureRayComparisonOptions options;
  options.ray_count = ray_count;

  std::vector<FixtureRayComparisonSummary> summaries;
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
      aggregate_report.AddError(
          "manifest.parse_failed",
          std::string("Failed to parse family manifest: ") + error.what(),
          family_manifest_path);
      continue;
    }
    aggregate_report.Append(ValidateManifestStructure(family_manifest));

    for (const auto& fixture : family_manifest.fixtures) {
      FixtureValidationRequest request;
      request.manifest = family_manifest;
      request.fixture = fixture;
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
        ray_report = g4occt::tests::geometry::ReclassifyExpectedFailures(ray_report, expected_failure.reason);
      }
      aggregate_report.Append(ray_report);
      if (summary.ray_count > 0U) {
        summaries.push_back(summary);
      }
    }
  }

  PrintReportMessages(aggregate_report);
  if (HasErrors(aggregate_report)) {
    return EXIT_FAILURE;
  }

  double total_native_ms = 0.0;
  double total_imported_ms = 0.0;
  std::size_t total_mismatches = 0;

  std::cout << "\n=== Fixture Ray Benchmark Results ===\n";
  std::cout << "Rays per fixture: " << ray_count << "\n";
  for (const auto& summary : summaries) {
    total_native_ms += summary.native_elapsed_ms;
    total_imported_ms += summary.imported_elapsed_ms;
    total_mismatches += summary.mismatch_count;
    std::cout << summary.fixture_id << " (" << summary.geant4_class << "): native="
              << summary.native_elapsed_ms << " ms, imported=" << summary.imported_elapsed_ms
              << " ms, mismatches=" << summary.mismatch_count << "\n";
  }

  std::cout << "Aggregate native   : " << total_native_ms << " ms\n";
  std::cout << "Aggregate imported : " << total_imported_ms << " ms\n";
  if (total_imported_ms > 0.0) {
    std::cout << "Native/imported ratio: " << total_native_ms / total_imported_ms << "\n";
  }
  std::cout << "Total mismatches: " << total_mismatches << "\n";
  if (expected_failure_count > 0U) {
    std::cout << "Expected failures: " << expected_failure_count << "\n";
  }
  return EXIT_SUCCESS;
}

}  // namespace
}  // namespace g4occt::benchmarks

int main(int argc, char** argv) {
  try {
    std::size_t ray_count = 2048;
    if (argc > 1) {
      ray_count = static_cast<std::size_t>(std::stoul(argv[1]));
    }
    const std::filesystem::path manifest_path =
        argc > 2 ? std::filesystem::path(argv[2])
                 : g4occt::tests::geometry::DefaultRepositoryManifestPath();
    return g4occt::benchmarks::RunBenchmark(manifest_path, ray_count);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: benchmark setup threw an exception: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
