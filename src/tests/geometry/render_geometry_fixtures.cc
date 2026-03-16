// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/**
 * @file render_geometry_fixtures.cc
 *
 * Renders each geometry fixture solid to a JPEG image using Geant4's built-in
 * RayTracer visualisation driver (@c /vis/open @c RT).  The RayTracer traces
 * rays through Geant4's own navigation infrastructure (G4Navigator), so the
 * resulting images reflect how each solid is interpreted by Geant4 — not a
 * separate mesh approximation.
 *
 * Usage:
 *   render_geometry_fixtures <output_dir> [<manifest_path>]
 *
 * For each validated fixture two JPEG images are written:
 *   <output_dir>/<safe_id>_native.jpeg   — solid built from analytic parameters
 *   <output_dir>/<safe_id>_imported.jpeg — solid loaded from STEP via G4OCCTSolid
 *
 * where @c safe_id is the qualified fixture ID with every non-alphanumeric,
 * non-hyphen/period character (including the family/id slash) replaced with '_'.
 */

#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_ray_compare.hh"
#include "geometry/fixture_solid_builder.hh"
#include "geometry/fixture_validation.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include <G4Box.hh>
#include <G4Colour.hh>
#include <G4LogicalVolume.hh>
#include <G4Material.hh>
#include <G4NistManager.hh>
#include <G4PVPlacement.hh>
#include <G4RunManager.hh>
#include <G4RunManagerFactory.hh>
#include <G4SystemOfUnits.hh>
#include <G4ThreeVector.hh>
#include <G4UImanager.hh>
#include <G4VUserDetectorConstruction.hh>
#include <G4VUserPhysicsList.hh>
#include <G4VisAttributes.hh>
#include <G4VisExecutive.hh>
#include <G4VSolid.hh>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace g4occt::tests::geometry {

namespace {

  // ── Minimal physics list (ray-tracing needs no particles) ─────────────────

  /// Empty physics list: no particles or processes are needed for geometry-only
  /// ray-tracing via G4RayTracer.
  class MinimalPhysicsList : public G4VUserPhysicsList {
  public:
    MinimalPhysicsList() : G4VUserPhysicsList() { SetVerboseLevel(0); }
    void ConstructParticle() override {}
    void ConstructProcess() override {}
  };

  // ── Detector construction that builds a fixture solid on demand ───────────

  /**
   * Places a user-configured fixture solid inside an automatically-sized
   * vacuum world box.
   *
   * The solid is built fresh inside each @c Construct() call so that it is
   * created *after* the @c G4SolidStore::Clean() call that
   * @c G4RunManager::ReinitializeGeometry(destroyFirst=true) issues.  This
   * avoids any risk of a prematurely-freed solid pointer.
   */
  class FixtureDetectorConstruction : public G4VUserDetectorConstruction {
  public:
    struct Request {
      /// Path to the fixture @c provenance.yaml.
      std::filesystem::path provenance_path;
      /// Family manifest that owns this fixture (needed for STEP resolution).
      FixtureManifest manifest;
      /// Fixture entry (needed for STEP resolution).
      FixtureReference fixture;
      /// When @c false, build the native Geant4 solid from provenance
      /// parameters.  When @c true, load the STEP file and wrap it in
      /// G4OCCTSolid.
      bool use_imported{false};
      /// Stem used as the solid and logical-volume name.
      G4String name;
    };

    void SetRequest(const Request& req) { fRequest = req; }

    G4VPhysicalVolume* Construct() override {
      // Build the solid here so it is created after G4SolidStore::Clean().
      G4VSolid* solid = nullptr;
      if (fRequest.use_imported) {
        FixtureValidationRequest val_req;
        val_req.manifest = fRequest.manifest;
        val_req.fixture  = fRequest.fixture;
        const TopoDS_Shape shape = LoadImportedShape(val_req);
        // Ownership is transferred to G4SolidStore via G4VSolid's constructor.
        solid = new G4OCCTSolid(fRequest.name, shape);
      } else {
        const FixtureProvenance prov = ParseFixtureProvenance(fRequest.provenance_path);
        // release() hands ownership to G4SolidStore (registered during construction).
        solid = BuildNativeSolid(prov).release();
      }

      G4ThreeVector bmin;
      G4ThreeVector bmax;
      solid->BoundingLimits(bmin, bmax);
      const G4ThreeVector center = 0.5 * (bmin + bmax);
      const G4double span        = std::max({bmax.x() - bmin.x(), bmax.y() - bmin.y(),
                                             bmax.z() - bmin.z(), kMinimumSpan});
      const G4double half        = span * kWorldHalfSpanFactor;

      auto* vacuum = G4NistManager::Instance()->FindOrBuildMaterial("G4_Galactic");
      auto* air    = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");

      auto* worldBox = new G4Box("World", half, half, half);
      auto* worldLV  = new G4LogicalVolume(worldBox, vacuum, "World");
      worldLV->SetVisAttributes(G4VisAttributes::GetInvisible());

      auto* solidLV = new G4LogicalVolume(solid, air, fRequest.name + "_lv");
      // Steel-blue: clearly visible against a white RT background.
      solidLV->SetVisAttributes(new G4VisAttributes(G4Colour(0.25, 0.60, 0.85)));

      // Translate so the bounding-box centre lands at the world origin.
      new G4PVPlacement(nullptr, -center, solidLV, fRequest.name + "_pv", worldLV, false, 0);
      return new G4PVPlacement(nullptr, G4ThreeVector(), worldLV, "World_pv", nullptr, false, 0);
    }

  private:
    Request fRequest;
  };

  // ── Filename sanitiser ────────────────────────────────────────────────────

  // Named constants for render geometry setup.
  /// Minimum bounding-box span (mm) to prevent degenerate world volumes for
  /// point-like or extremely small solids.
  constexpr G4double kMinimumSpan = 1.0 * mm;
  /// Edge half-length of the world box expressed as a multiple of the solid span.
  constexpr G4double kWorldHalfSpanFactor = 2.0;
  /// Image resolution (pixels) passed to the RayTracer driver.
  constexpr int kRenderResolution = 512;

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

  // ── Per-fixture render ────────────────────────────────────────────────────

  /**
   * Set a fixture request on @p detector, (re)initialise the Geant4 geometry,
   * and render to JPEG via the RT visualisation driver.
   *
   * On the first call @p initialized must be @c false; a full
   * @c G4RunManager::Initialize() is performed and the G4VisExecutive +
   * RT viewer are opened.  On subsequent calls @c ReinitializeGeometry is
   * used to cleanly swap the geometry.
   *
   * @p vis_manager and @p ui are set on the first call and remain valid for
   * the lifetime of the loop.
   *
   * G4RayTracer appends @c .jpeg to the stem passed to @c /vis/rayTracer/trace,
   * so @p output_path should include the @c .jpeg extension — the function
   * strips it before issuing the command and then verifies the output file.
   *
   * @return @c true when the JPEG was successfully written.
   */
  bool RenderFixture(G4RunManager* runManager, FixtureDetectorConstruction* detector,
                     G4VisExecutive*& vis_manager, G4UImanager* ui,
                     const FixtureDetectorConstruction::Request& req,
                     const std::filesystem::path& output_path, bool& initialized) {
    detector->SetRequest(req);

    if (!initialized) {
      runManager->Initialize();

      // Set up visualization after the first geometry initialization.
      vis_manager = new G4VisExecutive("quiet");
      vis_manager->Initialize();

      ui->ApplyCommand("/run/verbose 0");
      ui->ApplyCommand("/event/verbose 0");
      ui->ApplyCommand("/tracking/verbose 0");
      ui->ApplyCommand("/vis/verbose quiet");

      // Open the RayTracer driver once; it persists for all subsequent renders.
      ui->ApplyCommand("/vis/open RT " + std::to_string(kRenderResolution));

      initialized = true;
    } else {
      // destroyFirst=true → G4SolidStore::Clean() + Construct() rebuild.
      runManager->ReinitializeGeometry(/*destroyFirst=*/true, /*prop=*/false);
    }

    std::filesystem::create_directories(output_path.parent_path());

    // G4RayTracer appends ".jpeg"; strip it from the output path stem.
    std::string stem = output_path.string();
    if (stem.size() > 5 && stem.substr(stem.size() - 5) == ".jpeg") {
      stem.resize(stem.size() - 5);
    }

    // Create a fresh scene with the new geometry and render.
    ui->ApplyCommand("/vis/scene/create");
    ui->ApplyCommand("/vis/scene/add/volume");
    ui->ApplyCommand("/vis/viewer/set/viewpointThetaPhi 45 45");
    ui->ApplyCommand(G4String("/vis/rayTracer/setFileName ") + G4String(stem));
    ui->ApplyCommand("/vis/rayTracer/trace");

    return std::filesystem::exists(std::filesystem::path(stem + ".jpeg"));
  }

  // ── Main render loop ──────────────────────────────────────────────────────

  int RunRender(const std::filesystem::path& output_dir,
                const std::filesystem::path& repository_manifest_path) {
    std::filesystem::create_directories(output_dir);

    auto* runManager = G4RunManagerFactory::CreateRunManager(G4RunManagerType::Serial);
    runManager->SetVerboseLevel(0);

    auto* detector = new FixtureDetectorConstruction();
    runManager->SetUserInitialization(detector);
    runManager->SetUserInitialization(new MinimalPhysicsList());

    G4UImanager* ui          = G4UImanager::GetUIpointer();
    G4VisExecutive* vis_manager = nullptr;
    bool initialized            = false;

    const FixtureRepositoryManifest repository_manifest =
        ParseFixtureRepositoryManifest(repository_manifest_path);

    std::size_t rendered_count = 0;
    std::size_t skipped_count  = 0;
    std::size_t failed_count   = 0;

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
        const auto step_path       = ResolveFixtureStepPath(family_manifest, fixture);
        const auto provenance_path = ResolveFixtureProvenancePath(family_manifest, fixture);
        if (!std::filesystem::exists(step_path) || !std::filesystem::exists(provenance_path)) {
          ++skipped_count;
          continue;
        }

        const std::string qualified_id = family + "/" + fixture.id;
        const std::string safe_id      = SafeFilename(qualified_id);

        try {
          // ── Native solid ─────────────────────────────────────────────────
          FixtureDetectorConstruction::Request native_req;
          native_req.provenance_path = provenance_path;
          native_req.manifest        = family_manifest;
          native_req.fixture         = fixture;
          native_req.use_imported    = false;
          native_req.name            = G4String(fixture.id + "_native");

          const auto native_path = output_dir / (safe_id + "_native.jpeg");
          if (!RenderFixture(runManager, detector, vis_manager, ui, native_req, native_path,
                             initialized)) {
            std::cerr << "WARNING: " << qualified_id << ": native render produced no output\n";
            ++skipped_count;
            continue;
          }

          // ── Imported solid (STEP → G4OCCTSolid) ─────────────────────────
          FixtureDetectorConstruction::Request imported_req;
          imported_req.provenance_path = provenance_path;
          imported_req.manifest        = family_manifest;
          imported_req.fixture         = fixture;
          imported_req.use_imported    = true;
          imported_req.name            = G4String(fixture.id + "_imported");

          const auto imported_path = output_dir / (safe_id + "_imported.jpeg");
          if (!RenderFixture(runManager, detector, vis_manager, ui, imported_req, imported_path,
                             initialized)) {
            std::cerr << "WARNING: " << qualified_id << ": imported render produced no output\n";
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

    delete vis_manager;
    delete runManager;

    std::cout << "Render summary: " << rendered_count << " rendered, " << skipped_count
              << " skipped, " << failed_count << " failed.\n";
    return (failed_count > 0U) ? EXIT_FAILURE : EXIT_SUCCESS;
  }

} // namespace

} // namespace g4occt::tests::geometry

int main(int argc, char** argv) {
  try {
    const std::filesystem::path output_dir =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("geometry_renders");
    const std::filesystem::path manifest_path =
        argc > 2 ? std::filesystem::path(argv[2])
                 : g4occt::tests::geometry::DefaultRepositoryManifestPath();
    return g4occt::tests::geometry::RunRender(output_dir, manifest_path);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: render_geometry_fixtures threw an exception: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
