// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/**
 * @file render_geometry_fixtures.cc
 *
 * Export G4Polyhedron mesh data for every geometry fixture as JSON so that
 * the companion Python script can render PNG images using the Geant4
 * visualisation mesh representation.
 *
 * Usage:
 *   render_geometry_fixtures <output_dir> [<manifest_path>]
 *
 * For each validated fixture the executable writes two JSON files:
 *   <output_dir>/<safe_fixture_id>_native.json
 *   <output_dir>/<safe_fixture_id>_imported.json
 *
 * where <safe_fixture_id> has every non-alphanumeric, non-hyphen/period
 * character (in particular the family/id slash separator) replaced with '_'.
 *
 * JSON schema per file:
 * {
 *   "fixture_id":   "<family>/<id>",
 *   "geant4_class": "<G4ClassName>",
 *   "label":        "native" | "imported",
 *   "faces": [
 *     [[x1,y1,z1],[x2,y2,z2],[x3,y3,z3]],          // triangle
 *     [[x1,y1,z1],[x2,y2,z2],[x3,y3,z3],[x4,y4,z4]] // quad
 *   ]
 * }
 *
 * The mesh is obtained from G4Polyhedron::GetNextFacet(), which is the core
 * of Geant4's visualisation infrastructure and is used by all Geant4
 * visualisation drivers (OGL, RayTracer, etc.) to represent solids.
 */

#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_ray_compare.hh"
#include "geometry/fixture_solid_builder.hh"
#include "geometry/fixture_validation.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include <G4Polyhedron.hh>
#include <G4VSolid.hh>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace g4occt::tests::geometry {

namespace {

  /// Sanitise a qualified fixture ID (e.g. "family/id") to a filename-safe
  /// string by replacing any character that is not alphanumeric, a hyphen, or
  /// a period with an underscore.
  std::string SafeFilename(const std::string& fixture_id) {
    std::string safe = fixture_id;
    for (char& c : safe) {
      if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c < '0' || c > '9') && c != '-' &&
          c != '.') {
        c = '_';
      }
    }
    return safe;
  }

  /// Escape a string for safe embedding as a JSON string value.
  std::string JsonString(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 2U);
    result += '"';
    for (const char ch : s) {
      const auto uc = static_cast<unsigned char>(ch);
      if (ch == '"') {
        result += "\\\"";
      } else if (ch == '\\') {
        result += "\\\\";
      } else if (uc < 0x20U) {
        char buf[7];
        std::snprintf(buf, sizeof(buf), "\\u%04X", uc);
        result += buf;
      } else {
        result += ch;
      }
    }
    result += '"';
    return result;
  }

  /**
   * Write the G4Polyhedron mesh of @p solid as a JSON file.
   *
   * Returns true on success, false if CreatePolyhedron() returns null (some
   * complex composite solids cannot provide a polyhedron representation).
   */
  bool WriteMeshJson(const std::filesystem::path& output_path, const G4VSolid& solid,
                     const std::string& fixture_id, const std::string& geant4_class,
                     const std::string& label) {
    // 72 steps → 5° angular resolution, giving smooth tessellation for
    // curved solids (spheres, tori, cylinders) visible in rendered images.
    constexpr G4int kRotationSteps = 72;
    G4Polyhedron::SetNumberOfRotationSteps(kRotationSteps);
    const std::unique_ptr<G4Polyhedron> poly(solid.CreatePolyhedron());
    if (!poly) {
      return false;
    }

    std::filesystem::create_directories(output_path.parent_path());
    std::ofstream out(output_path);
    if (!out) {
      throw std::runtime_error("Cannot open output file: " + output_path.string());
    }

    out << std::setprecision(15);
    out << "{\n";
    out << "  \"fixture_id\": " << JsonString(fixture_id) << ",\n";
    out << "  \"geant4_class\": " << JsonString(geant4_class) << ",\n";
    out << "  \"label\": " << JsonString(label) << ",\n";
    out << "  \"faces\": [\n";

    G4int n_vertices = 0;
    G4Point3D nodes[4];
    G4int edge_flags[4];
    G4Normal3D normals[4];
    bool first_face = true;

    while (poly->GetNextFacet(n_vertices, nodes, edge_flags, normals)) {
      if (!first_face) {
        out << ",\n";
      }
      first_face = false;

      out << "    [";
      for (G4int i = 0; i < n_vertices; ++i) {
        if (i > 0) {
          out << ",";
        }
        out << "[" << nodes[i].x() << "," << nodes[i].y() << "," << nodes[i].z() << "]";
      }
      out << "]";
    }

    out << "\n  ]\n";
    out << "}\n";
    return true;
  }

  int RunRender(const std::filesystem::path& output_dir,
                const std::filesystem::path& repository_manifest_path) {
    const FixtureRepositoryManifest repository_manifest =
        ParseFixtureRepositoryManifest(repository_manifest_path);

    std::filesystem::create_directories(output_dir);

    std::size_t rendered_count  = 0;
    std::size_t skipped_count   = 0;
    std::size_t failed_count    = 0;

    for (const auto& family : repository_manifest.families) {
      const auto family_manifest_path = ResolveFamilyManifestPath(repository_manifest, family);
      if (!std::filesystem::exists(family_manifest_path)) {
        continue;
      }

      FixtureManifest family_manifest;
      try {
        family_manifest = ParseFixtureManifestFile(family_manifest_path);
      } catch (const std::exception& error) {
        std::cerr << "WARNING: failed to parse " << family_manifest_path << ": " << error.what()
                  << '\n';
        continue;
      }

      for (const auto& fixture : family_manifest.fixtures) {
        FixtureValidationRequest request;
        request.manifest              = family_manifest;
        request.fixture               = fixture;
        request.require_step_file     = true;
        request.require_provenance_file = false;

        const auto step_path = ResolveFixtureStepPath(family_manifest, fixture);
        if (!std::filesystem::exists(step_path)) {
          ++skipped_count;
          continue;
        }

        const auto provenance_path = ResolveFixtureProvenancePath(family_manifest, fixture);
        if (!std::filesystem::exists(provenance_path)) {
          ++skipped_count;
          continue;
        }

        const std::string qualified_id = family + "/" + fixture.id;
        const std::string safe_id      = SafeFilename(qualified_id);

        try {
          const FixtureProvenance provenance = ParseFixtureProvenance(provenance_path);
          const std::string geant4_class     = FixtureGeant4Class(provenance);

          // ── Native solid ───────────────────────────────────────────────────
          std::unique_ptr<G4VSolid> native_solid = BuildNativeSolid(provenance);
          const bool native_ok = WriteMeshJson(output_dir / (safe_id + "_native.json"),
                                               *native_solid, qualified_id, geant4_class, "native");
          if (!native_ok) {
            std::cerr << "WARNING: " << qualified_id
                      << ": native solid returned null polyhedron, skipping\n";
            ++skipped_count;
            continue;
          }

          // ── Imported solid (via STEP + G4OCCTSolid) ────────────────────────
          const TopoDS_Shape imported_shape = LoadImportedShape(request);
          G4OCCTSolid imported_solid(fixture.id + "_imported", imported_shape);
          const bool imported_ok =
              WriteMeshJson(output_dir / (safe_id + "_imported.json"), imported_solid, qualified_id,
                            geant4_class, "imported");
          if (!imported_ok) {
            std::cerr << "WARNING: " << qualified_id
                      << ": imported solid returned null polyhedron, skipping\n";
            ++skipped_count;
            continue;
          }

          ++rendered_count;
          std::cout << "Rendered " << qualified_id << '\n';
        } catch (const std::exception& error) {
          std::cerr << "ERROR: " << qualified_id << ": " << error.what() << '\n';
          ++failed_count;
        }
      }
    }

    std::cout << "Render summary: " << rendered_count << " rendered, " << skipped_count
              << " skipped, " << failed_count << " failed.\n";
    return (failed_count > 0U) ? EXIT_FAILURE : EXIT_SUCCESS;
  }

} // namespace

} // namespace g4occt::tests::geometry

int main(int argc, char** argv) {
  try {
    const std::filesystem::path output_dir =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("geometry_meshes");
    const std::filesystem::path manifest_path =
        argc > 2 ? std::filesystem::path(argv[2])
                 : g4occt::tests::geometry::DefaultRepositoryManifestPath();
    return g4occt::tests::geometry::RunRender(output_dir, manifest_path);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: render_geometry_fixtures threw an exception: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
