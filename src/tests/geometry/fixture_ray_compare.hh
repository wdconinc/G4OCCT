// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#ifndef G4OCCT_TESTS_GEOMETRY_FIXTURE_RAY_COMPARE_HH
#define G4OCCT_TESTS_GEOMETRY_FIXTURE_RAY_COMPARE_HH

#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_validation.hh"

#include <filesystem>
#include <string>

#include <G4ThreeVector.hh>
#include <G4VSolid.hh>

namespace g4occt::tests::geometry {

/** Options controlling fixture ray-comparison workloads. */
struct FixtureRayComparisonOptions {
  /// Number of deterministic ray directions to compare.
  std::size_t ray_count{2048};
  /// Maximum number of detailed mismatch diagnostics to emit per fixture.
  std::size_t max_reported_mismatches{8};
};

/** Summary of one fixture's native-vs-imported ray-comparison run. */
struct FixtureRayComparisonSummary {
  std::string fixture_id;
  std::string geant4_class;
  std::size_t ray_count{0};
  std::size_t mismatch_count{0};
  G4double distance_tolerance{0.0};
  G4ThreeVector native_origin;
  G4ThreeVector imported_origin;
  EInside native_origin_state{kOutside};
  EInside imported_origin_state{kOutside};
  double native_elapsed_ms{0.0};
  double imported_elapsed_ms{0.0};
};

/** Return the default repository fixture manifest path for tests and benchmarks. */
std::filesystem::path DefaultRepositoryManifestPath();

/** Return the Geant4-derived distance tolerance used for ray comparisons. */
G4double DefaultRayComparisonTolerance();

/** Build both fixture models, compare center-origin ray hits, and collect timings. */
ValidationReport CompareFixtureRays(
    const FixtureValidationRequest& request,
    const FixtureRayComparisonOptions& options = {},
    FixtureRayComparisonSummary* summary = nullptr);

}  // namespace g4occt::tests::geometry

#endif
