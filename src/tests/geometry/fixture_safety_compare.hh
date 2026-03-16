// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#ifndef G4OCCT_TESTS_GEOMETRY_FIXTURE_SAFETY_COMPARE_HH
#define G4OCCT_TESTS_GEOMETRY_FIXTURE_SAFETY_COMPARE_HH

#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_validation.hh"

#include <cstddef>
#include <string>

namespace g4occt::tests::geometry {

/** Options controlling the safety-distance benchmark and comparison. */
struct FixtureSafetyComparisonOptions {
  /// Total number of test points to evaluate (split between inside and outside).
  std::size_t point_count{2048};
  /// Maximum number of detailed mismatch diagnostics to emit per fixture.
  std::size_t max_reported_mismatches{8};
};

/** Summary of one fixture's native-vs-imported safety-distance comparison run. */
struct FixtureSafetyComparisonSummary {
  /// Qualified fixture identifier (`family/id`).
  std::string fixture_id;
  /// Geant4 class name read from the fixture provenance.
  std::string geant4_class;
  /// Total number of test points evaluated.
  std::size_t point_count{0};
  /// Number of `DistanceToIn(p)` mismatches (outside points).
  std::size_t safety_in_mismatch_count{0};
  /// Number of `DistanceToOut(p)` mismatches (inside points).
  std::size_t safety_out_mismatch_count{0};
  /// Elapsed time for all native `DistanceToIn(p)` calls (milliseconds).
  double native_safety_in_ms{0.0};
  /// Elapsed time for all imported `DistanceToIn(p)` calls (milliseconds).
  double imported_safety_in_ms{0.0};
  /// Elapsed time for all native `DistanceToOut(p)` calls (milliseconds).
  double native_safety_out_ms{0.0};
  /// Elapsed time for all imported `DistanceToOut(p)` calls (milliseconds).
  double imported_safety_out_ms{0.0};
};

/**
 * Build both fixture models, evaluate the isotropic safety-distance overloads
 * `DistanceToIn(p)` and `DistanceToOut(p)`, compare values, and collect timings.
 *
 * Test points are generated from the axis-aligned bounding box using a
 * deterministic Halton low-discrepancy sequence (reusing the point-generation
 * utilities from Phase 2).  Each point is then classified with `Inside(p)` on
 * the native solid:
 *
 *  - `kOutside` points → `DistanceToIn(p)` is timed and compared.
 *  - `kInside`  points → `DistanceToOut(p)` is timed and compared.
 *  - `kSurface` points → skipped (safety distance is 0 by definition).
 *
 * Validation tolerance uses a relative formula:
 *   `max(kSurfaceTolerance, 0.01 * expected_value)`
 *
 * Validation codes:
 *  - `fixture.safety_in_distance_mismatch`  — `DistanceToIn(p)` disagreement.
 *  - `fixture.safety_out_distance_mismatch` — `DistanceToOut(p)` disagreement.
 *
 * @param request  Fixture and manifest to process.
 * @param options  Tuning parameters (point count, mismatch report limit).
 * @param summary  Optional output struct for timing and mismatch counts.
 * @return         Validation report with info, warning, and error messages.
 */
ValidationReport CompareFixtureSafety(const FixtureValidationRequest& request,
                                      const FixtureSafetyComparisonOptions& options = {},
                                      FixtureSafetyComparisonSummary* summary       = nullptr);

} // namespace g4occt::tests::geometry

#endif
