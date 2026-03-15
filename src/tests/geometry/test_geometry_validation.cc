// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_ray_compare.hh"
#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_validation.hh"

#include <cstdlib>
#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace g4occt::tests::geometry {

namespace {

bool HasErrors(const ValidationReport& report) {
  return std::any_of(
      report.Messages().begin(),
      report.Messages().end(),
      [](const ValidationMessage& message) { return message.severity == ValidationSeverity::kError; });
}

}  // namespace

void PrintReport(const ValidationReport& report) {
  for (const auto& message : report.Messages()) {
    std::cout << ToString(message.severity) << " [" << message.code << "] " << message.text;
    if (!message.path.empty()) {
      std::cout << " :: " << message.path.string();
    }
    std::cout << '\n';
  }
}

int RunValidation(const std::filesystem::path& repository_manifest_path) {
  const FixtureRepositoryManifest repository_manifest =
      ParseFixtureRepositoryManifest(repository_manifest_path);

  ValidationReport aggregate_report;
  aggregate_report.Append(ValidateRepositoryLayout(repository_manifest));

  std::size_t validated_fixture_count = 0;
  std::size_t geometry_checked_count = 0;
  std::size_t ray_compared_count = 0;
  std::size_t expected_failure_count = 0;
  double total_native_ms = 0.0;
  double total_imported_ms = 0.0;

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
      const FixtureExpectedFailure expected_failure = ExpectedFailureForFixture(request);

      const ValidationReport layout_report = ValidateFixtureLayout(request);
      aggregate_report.Append(layout_report);
      ++validated_fixture_count;

      if (!layout_report.Ok()) {
        continue;
      }

      FixtureGeometryValidationOptions geom_opts;
      geom_opts.volume_unit = repository_manifest.policy.volume_unit;
      FixtureGeometryObservation observation;
      ValidationReport geometry_report = ValidateFixtureGeometry(request, geom_opts, &observation);
      if (expected_failure.enabled) {
        geometry_report = ReclassifyExpectedFailures(geometry_report, expected_failure.reason);
      }
      aggregate_report.Append(geometry_report);
      if (observation.imported) {
        ++geometry_checked_count;
      }

      FixtureRayComparisonSummary ray_summary;
      ValidationReport ray_report = CompareFixtureRays(request, {}, &ray_summary);
      if (expected_failure.enabled) {
        ++expected_failure_count;
        ray_report = ReclassifyExpectedFailures(ray_report, expected_failure.reason);
      }
      aggregate_report.Append(ray_report);
      if (ray_summary.ray_count > 0U) {
        ++ray_compared_count;
        total_native_ms += ray_summary.native_elapsed_ms;
        total_imported_ms += ray_summary.imported_elapsed_ms;
      }
    }
  }

  if (validated_fixture_count == 0U) {
    aggregate_report.AddInfo(
        "repository.fixtures_empty",
        "No fixture entries are registered yet; validated manifest and family layout only",
        repository_manifest.source_path);
  } else if (geometry_checked_count == 0U) {
    aggregate_report.AddInfo(
        "repository.geometry_skipped",
        "Fixture entries were discovered but none reached OCCT geometry validation",
        repository_manifest.source_path);
  }

  PrintReport(aggregate_report);
  std::cout << "Validated " << repository_manifest.families.size() << " fixture families, "
            << validated_fixture_count << " fixture entries, and " << geometry_checked_count
            << " imported STEP geometries.\n";
  if (ray_compared_count > 0U) {
    std::cout << "Ray comparison summary: " << ray_compared_count
              << " fixtures, native=" << total_native_ms
              << " ms, imported=" << total_imported_ms << " ms";
    if (total_imported_ms > 0.0) {
      std::cout << ", native/imported ratio=" << total_native_ms / total_imported_ms;
    }
    std::cout << '\n';
  }
  if (expected_failure_count > 0U) {
    std::cout << "Expected ray/volume failures: " << expected_failure_count << " fixtures\n";
  }
  return HasErrors(aggregate_report) ? EXIT_FAILURE : EXIT_SUCCESS;
}

}  // namespace g4occt::tests::geometry

int main(int argc, char** argv) {
  try {
    const std::filesystem::path manifest_path =
        argc > 1 ? std::filesystem::path(argv[1])
                 : g4occt::tests::geometry::DefaultRepositoryManifestPath();
    return g4occt::tests::geometry::RunValidation(manifest_path);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: geometry fixture validation threw an exception: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
