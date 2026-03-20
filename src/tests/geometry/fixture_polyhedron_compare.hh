// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#ifndef G4OCCT_TESTS_GEOMETRY_FIXTURE_POLYHEDRON_COMPARE_HH
#define G4OCCT_TESTS_GEOMETRY_FIXTURE_POLYHEDRON_COMPARE_HH

#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_validation.hh"

#include <string>

namespace g4occt::tests::geometry {

/** Options controlling the `CreatePolyhedron()` benchmark. */
struct FixturePolyhedronComparisonOptions {
  // Currently no tuning knobs are needed; the struct is provided for
  // forward-compatibility with future options (e.g. deflection override).
};

/**
 * Summary of one fixture's native-vs-imported `CreatePolyhedron()` benchmark.
 *
 * The native and imported polyhedra will never be structurally identical
 * (the native Geant4 solid uses an analytical parametrisation while the
 * imported `G4OCCTSolid` uses OCCT `BRepMesh` tessellation), so no mismatch
 * count is recorded.  Instead, vertex and facet counts are reported as
 * informational mesh-density metrics alongside wall-clock timing.
 */
struct FixturePolyhedronComparisonSummary {
  /// Qualified fixture identifier (`family/id`).
  std::string fixture_id;
  /// Geant4 class name read from the fixture provenance.
  std::string geant4_class;
  /// True when the native `CreatePolyhedron()` returned a non-null pointer.
  bool native_valid{false};
  /// True when the imported `CreatePolyhedron()` returned a non-null pointer.
  bool imported_valid{false};
  /// Number of vertices in the native polyhedron mesh (`G4Polyhedron::GetNoVertices()`).
  int native_vertices{0};
  /// Number of vertices in the imported polyhedron mesh.
  int imported_vertices{0};
  /// Number of facets in the native polyhedron mesh (`G4Polyhedron::GetNoFacets()`).
  int native_facets{0};
  /// Number of facets in the imported polyhedron mesh.
  int imported_facets{0};
  /// Wall-clock time for the native `CreatePolyhedron()` call (milliseconds).
  double native_elapsed_ms{0.0};
  /// Wall-clock time for the imported `CreatePolyhedron()` call (milliseconds).
  double imported_elapsed_ms{0.0};
};

/**
 * Build both fixture models, time `CreatePolyhedron()` on each, and collect
 * mesh-density metrics.
 *
 * Because the native Geant4 solid uses an analytical parametrisation while
 * `G4OCCTSolid` uses OCCT `BRepMesh` incremental tessellation, the vertex
 * and facet counts are expected to differ and are reported for information
 * only — no correctness error is raised when they disagree.
 *
 * Validation codes produced by this function:
 *  - `fixture.polyhedron_native_null`   — native `CreatePolyhedron()` returned null.
 *  - `fixture.polyhedron_imported_null` — imported `CreatePolyhedron()` returned null.
 *
 * @param request  Fixture and manifest to process.
 * @param options  Tuning parameters (currently unused; provided for extensibility).
 * @param summary  Optional output struct for timing and mesh-density counts.
 * @return         Validation report with info, warning, and error messages.
 */
ValidationReport CompareFixturePolyhedron(const FixtureValidationRequest& request,
                                          const FixturePolyhedronComparisonOptions& options = {},
                                          FixturePolyhedronComparisonSummary* summary = nullptr);

} // namespace g4occt::tests::geometry

#endif
