// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#ifndef G4OCCT_TESTS_GEOMETRY_FIXTURE_NAVIGATION_SUMMARY_HH
#define G4OCCT_TESTS_GEOMETRY_FIXTURE_NAVIGATION_SUMMARY_HH

#include "geometry/fixture_inside_compare.hh"
#include "geometry/fixture_polyhedron_compare.hh"
#include "geometry/fixture_ray_compare.hh"
#include "geometry/fixture_safety_compare.hh"

#include <string>

namespace g4occt::tests::geometry {

/**
 * Composite summary of one fixture's native-vs-imported benchmark across all
 * navigator-critical methods.
 *
 * Aggregates the per-phase summary structs from the ray, inside-classification,
 * and safety-distance comparisons into a single object so that reporting code
 * can produce a unified table that gives a complete picture of performance for
 * each fixture.
 */
struct FixtureNavigationSummary {
  /// Qualified fixture identifier (`family/id`).
  std::string fixture_id;
  /// Geant4 class name read from the fixture provenance.
  std::string geant4_class;
  /// Results from the ray-casting comparison (DistanceToIn/Out(p,v), exit normals,
  /// and the post-hoc SurfaceNormal(p) benchmark).
  FixtureRayComparisonSummary ray;
  /// Results from the Inside(p) classification comparison.
  FixtureInsideComparisonSummary inside;
  /// Results from the DistanceToIn(p) / DistanceToOut(p) safety-distance comparison.
  FixtureSafetyComparisonSummary safety;
  /// Results from the CreatePolyhedron() timing benchmark.
  FixturePolyhedronComparisonSummary polyhedron;
  /// True when this fixture is marked as an expected failure.
  bool has_expected_failure{false};
};

} // namespace g4occt::tests::geometry

#endif
