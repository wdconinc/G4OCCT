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
#include <stdexcept>
#include <string>
#include <string_view>

namespace g4occt::tests::geometry {

namespace {

  bool HasErrors(const ValidationReport& report) {
    return std::any_of(report.Messages().begin(), report.Messages().end(),
                       [](const ValidationMessage& message) {
                         return message.severity == ValidationSeverity::kError;
                       });
  }

  struct CommandLineOptions {
    std::filesystem::path manifest_path{DefaultRepositoryManifestPath()};
    std::string fixture_filter;
  };

  CommandLineOptions ParseCommandLine(int argc, char** argv) {
    CommandLineOptions options;

    for (int index = 1; index < argc; ++index) {
      const std::string argument = argv[index];
      if (argument == "--fixture") {
        if (index + 1 >= argc) {
          throw std::runtime_error("Missing fixture ID after --fixture");
        }
        options.fixture_filter = argv[++index];
        continue;
      }

      if (argument.rfind("--fixture=", 0) == 0) {
        options.fixture_filter = argument.substr(std::string("--fixture=").size());
        continue;
      }

      if (argument == "--manifest") {
        if (index + 1 >= argc) {
          throw std::runtime_error("Missing manifest path after --manifest");
        }
        options.manifest_path = argv[++index];
        continue;
      }

      if (argument.rfind("--manifest=", 0) == 0) {
        options.manifest_path = argument.substr(std::string("--manifest=").size());
        continue;
      }

      if (!argument.empty() && argument[0] == '-') {
        throw std::runtime_error("Unknown option: " + argument);
      }

      options.manifest_path = argument;
    }

    return options;
  }

} // namespace

void PrintReport(const ValidationReport& report) {
  for (const auto& message : report.Messages()) {
    std::cout << ToString(message.severity) << " [" << message.code << "] " << message.text;
    if (!message.path.empty()) {
      std::cout << " :: " << message.path.string();
    }
    std::cout << '\n';
  }
}

int RunValidation(const std::filesystem::path& repository_manifest_path,
                  const std::string_view fixture_filter = {}) {
  const FixtureRepositoryManifest repository_manifest =
      ParseFixtureRepositoryManifest(repository_manifest_path);

  ValidationReport aggregate_report;
  aggregate_report.Append(ValidateRepositoryLayout(repository_manifest));

  std::size_t validated_fixture_count = 0;
  std::size_t geometry_checked_count  = 0;
  std::size_t ray_compared_count      = 0;
  std::size_t expected_failure_count  = 0;
  double total_native_ms              = 0.0;
  double total_imported_ms            = 0.0;
  bool matched_fixture                = false;

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
      if (!fixture_filter.empty() && fixture.id != fixture_filter) {
        continue;
      }

      matched_fixture = true;
      FixtureValidationRequest request;
      request.manifest                              = family_manifest;
      request.fixture                               = fixture;
      request.require_provenance_file               = true;
      const FixtureExpectedFailure expected_failure = ExpectedFailureForFixture(request);

      const ValidationReport layout_report = ValidateFixtureLayout(request);
      aggregate_report.Append(layout_report);
      ++validated_fixture_count;

      if (!layout_report.Ok()) {
        if (!fixture_filter.empty()) {
          break;
        }
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

      if (!fixture_filter.empty()) {
        break; // Found and processed the requested fixture; skip remaining fixtures.
      }
    }

    if (!fixture_filter.empty() && matched_fixture) {
      break; // Found the fixture in this family; skip remaining families.
    }
  }

  if (!fixture_filter.empty() && !matched_fixture) {
    aggregate_report.AddError("fixture.not_found",
                              "Requested fixture '" + std::string(fixture_filter) +
                                  "' was not found in repository manifests",
                              repository_manifest.source_path);
  } else if (validated_fixture_count == 0U) {
    aggregate_report.AddInfo(
        "repository.fixtures_empty",
        "No fixture entries are registered yet; validated manifest and family layout only",
        repository_manifest.source_path);
  } else if (geometry_checked_count == 0U) {
    const std::string message =
        fixture_filter.empty()
            ? "Fixture entries were discovered but none reached OCCT geometry validation"
            : "Requested fixture was discovered but did not reach OCCT geometry validation";
    aggregate_report.AddInfo("repository.geometry_skipped", message,
                             repository_manifest.source_path);
  }

  PrintReport(aggregate_report);
  if (fixture_filter.empty()) {
    std::cout << "Validated " << repository_manifest.families.size() << " fixture families, "
              << validated_fixture_count << " fixture entries, and " << geometry_checked_count
              << " imported STEP geometries.\n";
  } else {
    std::cout << "Validated requested fixture '" << fixture_filter
              << "': " << validated_fixture_count << " fixture entries, " << geometry_checked_count
              << " imported STEP geometries.\n";
  }
  if (ray_compared_count > 0U) {
    std::cout << "Ray comparison summary: " << ray_compared_count
              << " fixtures, native=" << total_native_ms << " ms, imported=" << total_imported_ms
              << " ms";
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

} // namespace g4occt::tests::geometry

int main(int argc, char** argv) {
  try {
    const auto options = g4occt::tests::geometry::ParseCommandLine(argc, argv);
    return g4occt::tests::geometry::RunValidation(options.manifest_path, options.fixture_filter);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: geometry fixture validation threw an exception: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
