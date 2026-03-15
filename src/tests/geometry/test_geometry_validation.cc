// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_validation.hh"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace g4occt::tests::geometry {

#ifndef G4OCCT_TEST_SOURCE_DIR
#define G4OCCT_TEST_SOURCE_DIR "."
#endif

std::filesystem::path DefaultRepositoryManifestPath() {
  return std::filesystem::path(G4OCCT_TEST_SOURCE_DIR) / "fixtures" / "geometry" / "manifest.yaml";
}

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

      const ValidationReport layout_report = ValidateFixtureLayout(request);
      aggregate_report.Append(layout_report);
      ++validated_fixture_count;

      if (!layout_report.Ok()) {
        continue;
      }

      FixtureGeometryValidationOptions geom_opts;
      geom_opts.volume_unit = repository_manifest.policy.volume_unit;
      FixtureGeometryObservation observation;
      aggregate_report.Append(ValidateFixtureGeometry(request, geom_opts, &observation));
      if (observation.imported) {
        ++geometry_checked_count;
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
  return aggregate_report.Ok() ? EXIT_SUCCESS : EXIT_FAILURE;
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
