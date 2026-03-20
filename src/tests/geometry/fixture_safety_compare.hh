// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

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
  /// Maximum number of detailed lower-bound violation diagnostics to emit per fixture.
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

  // ── Geant4 vs OCCT timing ────────────────────────────────────────────────

  /// Elapsed time for all native Geant4 `DistanceToIn(p)` calls (milliseconds).
  double native_safety_in_ms{0.0};
  /// Elapsed time for all imported OCCT `DistanceToIn(p)` calls (milliseconds).
  double imported_safety_in_ms{0.0};
  /// Elapsed time for all native Geant4 `DistanceToOut(p)` calls (milliseconds).
  double native_safety_out_ms{0.0};
  /// Elapsed time for all imported OCCT `DistanceToOut(p)` calls (milliseconds).
  double imported_safety_out_ms{0.0};

  // ── Within OCCT: lower-bound vs exact ───────────────────────────────────

  /// Elapsed time for all OCCT `ExactDistanceToIn(p)` calls (milliseconds).
  double exact_safety_in_ms{0.0};
  /// Elapsed time for all OCCT `ExactDistanceToOut(p)` calls (milliseconds).
  double exact_safety_out_ms{0.0};
  /// Number of outside points where `DistanceToIn(p) > ExactDistanceToIn(p)` (hard-fail).
  std::size_t occt_lower_bound_in_violations{0};
  /// Number of inside points where `DistanceToOut(p) > ExactDistanceToOut(p)` (hard-fail).
  std::size_t occt_lower_bound_out_violations{0};
  /// Average of `DistanceToIn(p) / ExactDistanceToIn(p)` over outside points with valid ratios.
  double avg_dti_lb_ratio{0.0};
  /// Average of `DistanceToOut(p) / ExactDistanceToOut(p)` over inside points with valid ratios.
  double avg_dto_lb_ratio{0.0};

  // ── Between Geant4 and OCCT: average safety ratios ───────────────────────

  /// Average of `imported DistanceToIn(p) / native DistanceToIn(p)` (OCCT / Geant4).
  double avg_dti_g4_occt_ratio{0.0};
  /// Average of `imported DistanceToOut(p) / native DistanceToOut(p)` (OCCT / Geant4).
  double avg_dto_g4_occt_ratio{0.0};
};

/**
 * Build both fixture models, evaluate the isotropic safety-distance overloads
 * `DistanceToIn(p)` and `DistanceToOut(p)`, compare values, and collect timings.
 *
 * Test points are generated from the axis-aligned bounding box using the
 * shared `GenerateBoundingBoxPoints` utility (a 3-D Halton low-discrepancy
 * sequence defined in `fixture_solid_builder`).  Each point is then
 * classified with `Inside(p)` on the native solid:
 *
 *  - `kOutside` points → `DistanceToIn(p)` is timed and compared.
 *  - `kInside`  points → `DistanceToOut(p)` is timed and compared.
 *  - `kSurface` points → skipped (safety distance is 0 by definition).
 *
 * Three analyses are performed and reported via `summary`:
 *
 *  1. **Geant4 vs OCCT timing** — elapsed time for native and imported
 *     `DistanceToIn(p)` and `DistanceToOut(p)` calls, plus the average
 *     per-point ratio of imported / native safety distances.
 *
 *  2. **Within OCCT lower-bound validity** — each `DistanceToIn(p)` and
 *     `DistanceToOut(p)` value is compared against the corresponding
 *     `ExactDistanceToIn(p)` / `ExactDistanceToOut(p)`.  The accelerated
 *     overloads must never exceed the exact value.  A violation produces a
 *     hard-fail error and is counted in `occt_lower_bound_in_violations` /
 *     `occt_lower_bound_out_violations`.  The average ratio of lower-bound to
 *     exact distance is accumulated in `avg_dti_lb_ratio` / `avg_dto_lb_ratio`.
 *
 *  3. **Timing of OCCT lower-bound vs exact** — elapsed time for the exact
 *     methods is reported alongside the accelerated timing so callers can
 *     compute the speedup factor.
 *
 * Validation codes:
 *  - `fixture.occt_lower_bound_in_violation`  — `DistanceToIn(p)` exceeds `ExactDistanceToIn(p)`.
 *  - `fixture.occt_lower_bound_out_violation` — `DistanceToOut(p)` exceeds `ExactDistanceToOut(p)`.
 *
 * @param request  Fixture and manifest to process.
 * @param options  Tuning parameters (point count, violation report limit).
 * @param summary  Optional output struct for timings, ratios, and violation counts.
 * @return         Validation report with info, warning, and error messages.
 */
ValidationReport CompareFixtureSafety(const FixtureValidationRequest& request,
                                      const FixtureSafetyComparisonOptions& options = {},
                                      FixtureSafetyComparisonSummary* summary       = nullptr);

} // namespace g4occt::tests::geometry

#endif
