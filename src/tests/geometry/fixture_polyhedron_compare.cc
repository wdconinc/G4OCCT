// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#include "geometry/fixture_polyhedron_compare.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_solid_builder.hh"
#include "geometry/fixture_validation.hh"

#include <G4Polyhedron.hh>

#include <chrono>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace g4occt::tests::geometry {

ValidationReport
CompareFixturePolyhedron(const FixtureValidationRequest&           request,
                         const FixturePolyhedronComparisonOptions& /*options*/,
                         FixturePolyhedronComparisonSummary*       summary) {
  ValidationReport report;

  FixturePolyhedronComparisonSummary local_summary;
  local_summary.fixture_id = request.manifest.family + "/" + request.fixture.id;

  try {
    const auto       provenance_path = ResolveFixtureProvenancePath(request.manifest, request.fixture);
    const FixtureProvenance provenance = ParseFixtureProvenance(provenance_path);
    local_summary.geant4_class         = Geant4Class(provenance);

    std::unique_ptr<G4VSolid> native_solid = BuildNativeSolidForRequest(request, provenance);
    auto imported_solid =
        std::make_unique<G4OCCTSolid>(request.fixture.id + "_imported", LoadImportedShape(request));

    // ── Time native CreatePolyhedron() ───────────────────────────────────
    const auto native_begin = std::chrono::steady_clock::now();
    std::unique_ptr<G4Polyhedron> native_poly(native_solid->CreatePolyhedron());
    const auto native_end = std::chrono::steady_clock::now();
    local_summary.native_elapsed_ms =
        std::chrono::duration<double, std::milli>(native_end - native_begin).count();

    if (native_poly != nullptr) {
      local_summary.native_valid    = true;
      local_summary.native_vertices = native_poly->GetNoVertices();
      local_summary.native_facets   = native_poly->GetNoFacets();
    } else {
      report.AddError("fixture.polyhedron_native_null",
                      "Native CreatePolyhedron() returned null for fixture '" +
                          request.fixture.id + "'",
                      provenance_path);
    }

    // ── Time imported CreatePolyhedron() ─────────────────────────────────
    const auto imported_begin = std::chrono::steady_clock::now();
    std::unique_ptr<G4Polyhedron> imported_poly(imported_solid->CreatePolyhedron());
    const auto imported_end = std::chrono::steady_clock::now();
    local_summary.imported_elapsed_ms =
        std::chrono::duration<double, std::milli>(imported_end - imported_begin).count();

    if (imported_poly != nullptr) {
      local_summary.imported_valid    = true;
      local_summary.imported_vertices = imported_poly->GetNoVertices();
      local_summary.imported_facets   = imported_poly->GetNoFacets();
    } else {
      report.AddError("fixture.polyhedron_imported_null",
                      "Imported CreatePolyhedron() returned null for fixture '" +
                          request.fixture.id + "'",
                      provenance_path);
    }

    // ── Summary info message ──────────────────────────────────────────────
    std::ostringstream msg;
    msg << "CreatePolyhedron for fixture '" << request.fixture.id << "' ("
        << local_summary.geant4_class << ")"
        << ": native=" << local_summary.native_elapsed_ms << " ms"
        << " (vertices=" << local_summary.native_vertices
        << ", facets=" << local_summary.native_facets << ")"
        << "; imported=" << local_summary.imported_elapsed_ms << " ms"
        << " (vertices=" << local_summary.imported_vertices
        << ", facets=" << local_summary.imported_facets << ")";
    report.AddInfo("fixture.polyhedron_compare_summary", msg.str(), provenance_path);

  } catch (const std::exception& error) {
    report.AddError("fixture.polyhedron_compare_failed",
                    std::string("Fixture polyhedron comparison failed: ") + error.what(),
                    ResolveFixtureProvenancePath(request.manifest, request.fixture));
  }

  if (summary != nullptr) {
    *summary = local_summary;
  }
  return report;
}

} // namespace g4occt::tests::geometry
