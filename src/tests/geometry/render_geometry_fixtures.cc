// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/**
 * @file render_geometry_fixtures.cc
 *
 * Renders each geometry fixture solid to a JPEG image using Geant4's built-in
 * RayTracer visualisation driver (@c /vis/open @c RayTracer).  The RayTracer traces
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

// Vendored private Geant4 RayTracer headers (not installed by Geant4).
// These allow direct instantiation of the single-threaded G4TheRayTracer base
// class, bypassing G4TheMTRayTracer which crashes in MT Geant4 builds when
// BeamOn is called from a visualisation context (G4AllocatorPool corruption in
// worker threads).
#include "G4RayTracerViewer.hh"
#include "G4TheRayTracer.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include <G4Box.hh>
#include <G4Colour.hh>
#include <G4LogicalVolume.hh>
#include <G4Material.hh>
#include <G4ChargedGeantino.hh>
#include <G4Geantino.hh>
#include <G4ParticleDefinition.hh>
#include <G4ParticleTable.hh>
#include <G4NistManager.hh>
#include <G4PVPlacement.hh>
#include <G4PrimaryParticle.hh>
#include <G4PrimaryVertex.hh>
#include <G4RunManager.hh>
#include <G4RunManagerFactory.hh>
#include <G4SystemOfUnits.hh>
#include <G4ThreeVector.hh>
#include <G4UImanager.hh>
#include <G4VUserActionInitialization.hh>
#include <G4VUserDetectorConstruction.hh>
#include <G4VUserPhysicsList.hh>
#include <G4VUserPrimaryGeneratorAction.hh>
#include <G4Scene.hh>
#include <G4VisAttributes.hh>
#include <G4VisExecutive.hh>
#include <G4VSolid.hh>

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace g4occt::tests::geometry {

namespace {

  // ── Minimal physics list ────────────────────────────────────────────────────

  /// Physics list for geometry-only ray-tracing with G4RayTracer.
  /// G4RayTracer traces rays by simulating geantino particles; G4Transportation
  /// must be registered so those particles can navigate the geometry step by
  /// step.  Without it G4SteppingManager enters an infinite zero-step loop.
  class MinimalPhysicsList : public G4VUserPhysicsList {
  public:
    MinimalPhysicsList() : G4VUserPhysicsList() { SetVerboseLevel(0); }
    void ConstructParticle() override {
      G4Geantino::GeantinoDefinition();
      G4ChargedGeantino::ChargedGeantinoDefinition();
    }
    void ConstructProcess() override { AddTransportation(); }
  };

  class DummyPrimaryGeneratorAction : public G4VUserPrimaryGeneratorAction {
  public:
    void GeneratePrimaries(G4Event* event) override {
      G4ParticleDefinition* geantino =
          G4ParticleTable::GetParticleTable()->FindParticle("geantino");
      if (geantino == nullptr) {
        return;
      }

      auto* vertex   = new G4PrimaryVertex(G4ThreeVector(), 0.0);
      auto* particle = new G4PrimaryParticle(geantino);
      particle->SetKineticEnergy(1.0 * GeV);
      particle->SetMomentumDirection(G4ThreeVector(0.0, 0.0, 1.0));
      vertex->SetPrimary(particle);
      event->AddPrimaryVertex(vertex);
    }
  };

  class MinimalActionInitialization : public G4VUserActionInitialization {
  public:
    void Build() const override { SetUserAction(new DummyPrimaryGeneratorAction()); }
  };

  // ── Named constants ───────────────────────────────────────────────────────

  /// Minimum bounding-box span (mm) to prevent degenerate world volumes for
  /// point-like or extremely small solids.
  constexpr G4double kMinimumSpan = 1.0 * mm;
  /// Edge half-length of the world box expressed as a multiple of the solid span.
  /// RayTracer camera placement can sit several radii away from the target; keep
  /// the synthetic world comfortably larger so the eye remains inside it.
  constexpr G4double kWorldHalfSpanFactor = 10.0;
  /// Image resolution (pixels) passed to the RayTracer driver.
  /// RayTracer launches one Geant4 event per pixel; 240 px is the largest
  /// resolution that keeps this harness within the 8 GB CI memory budget.
  constexpr int kRenderResolution = 240;

  bool IsRayTracerNickname(const G4String& nickname) {
    return nickname == "RayTracer" || nickname == "RT";
  }

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
        val_req.manifest         = fRequest.manifest;
        val_req.fixture          = fRequest.fixture;
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
      const G4double span =
          std::max({bmax.x() - bmin.x(), bmax.y() - bmin.y(), bmax.z() - bmin.z(), kMinimumSpan});
      const G4double half = span * kWorldHalfSpanFactor;

      auto* vacuum = G4NistManager::Instance()->FindOrBuildMaterial("G4_Galactic");
      auto* air    = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");

      auto* worldBox = new G4Box("World", half, half, half);
      auto* worldLV  = new G4LogicalVolume(worldBox, vacuum, "World");
      worldLV->SetVisAttributes(G4VisAttributes::GetInvisible());

      auto* solidLV = new G4LogicalVolume(solid, air, fRequest.name + "_lv");
      // Steel-blue shade chosen for good contrast against the configured RayTracer background.
      solidLV->SetVisAttributes(new G4VisAttributes(G4Colour(0.45, 0.73, 0.95)));

      // Translate so the bounding-box centre lands at the world origin.
      new G4PVPlacement(nullptr, -center, solidLV, fRequest.name + "_pv", worldLV, false, 0);
      return new G4PVPlacement(nullptr, G4ThreeVector(), worldLV, "World_pv", nullptr, false, 0);
    }

  private:
    Request fRequest;
  };

  // ── Filename sanitiser ────────────────────────────────────────────────────

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
   * @p vis_manager, @p ui, and @p st_tracer are set on the first call and
   * remain valid for the lifetime of the loop.  @p visualization_ready is
   * set only after the viewer has been validated as a RayTracer instance and
   * the scene has been fully configured.
   *
   * Camera parameters are read from the MT tracer (set by the viewer's
   * @c SetView()) and forwarded to the single-threaded @p st_tracer which
   * performs the actual ray trace on the master thread without spawning
   * worker threads.  This avoids a G4AllocatorPool corruption that occurs
   * in G4TheMTRayTracer::CreateBitMap() when it calls BeamOn() from inside
   * a visualisation context.
   *
   * G4RayTracer writes the JPEG at the exact path passed to
   * @c G4TheRayTracer::Trace() (no extension is appended by the Geant4 code),
   * so @p output_path should already end in @c .jpeg.
   *
   * @return @c true when the JPEG was successfully written.
   */
  bool RenderFixture(G4RunManager* runManager, FixtureDetectorConstruction* detector,
                     G4VisExecutive*& vis_manager, G4UImanager* ui, G4TheRayTracer*& st_tracer,
                     const FixtureDetectorConstruction::Request& req,
                     const std::filesystem::path& output_path, bool& initialized,
                     bool& visualization_ready) {
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

      // Mark geometry as initialized here so any early return below does
      // not cause subsequent calls to re-invoke runManager->Initialize().
      initialized = true;

      // Open the RayTracer driver once; it persists for all subsequent renders.
      // Use the full registered name "RayTracer"; Geant4 reports the opened
      // graphics-system nickname as "RT" in current builds.
      const int vis_open_rc =
          ui->ApplyCommand("/vis/open RayTracer " + std::to_string(kRenderResolution));
      if (vis_open_rc != 0) {
        G4cerr << "render_geometry_fixtures: /vis/open RayTracer failed (rc=" << vis_open_rc
               << "). Skipping render." << G4endl;
        return false;
      }
      // G4UImanager::ApplyCommand returns 0 when the command is dispatched,
      // not when the vis system's SetNewValue succeeds.  A viewer may silently
      // fail to be created even with rc==0.  Verify explicitly.
      if (vis_manager->GetCurrentViewer() == nullptr) {
        G4cerr << "render_geometry_fixtures: /vis/open RayTracer did not create a viewer "
               << "(possibly headless environment). Skipping renders." << G4endl;
        return false;
      }
      // G4RTMessenger::SetNewValue performs dynamic_cast<G4RayTracerViewer*> on
      // the current viewer.  If /vis/open produced a different viewer type the
      // cast returns null, the messenger falls back to a default G4RayTracer,
      // and that default tracer crashes because no camera has been configured.
      // Guard against this by verifying the graphics-system nickname.
      const auto* sh = vis_manager->GetCurrentViewer()->GetSceneHandler();
      if (sh == nullptr) {
        G4cerr << "render_geometry_fixtures: no scene handler on current viewer. Skipping."
               << G4endl;
        return false;
      }
      const auto* gs = sh->GetGraphicsSystem();
      if (gs == nullptr || !IsRayTracerNickname(gs->GetNickname())) {
        const G4String gsName = gs ? gs->GetNickname() : "<none>";
        G4cerr << "render_geometry_fixtures: /vis/open RayTracer opened an unexpected "
               << "viewer type '" << gsName << "'. Skipping renders." << G4endl;
        return false;
      }
      // Disable auto-refresh so that subsequent /vis/viewer/set commands do not
      // trigger an implicit DrawView() → BeamOn() before we explicitly issue
      // our trace.  An unsolicited BeamOn at this point would fire the ray-tracer
      // with an incompletely configured scene and crash.
      ui->ApplyCommand("/vis/viewer/set/autoRefresh false");
      // Add the fixture solid (not the world) to the scene so the scene extent
      // is computed from the solid's bounding box rather than the world box.
      // This keeps the camera inside the world volume: the camera distance
      // computed from the solid extent is ~3.7 × solid_radius, while the
      // world half-span is kWorldHalfSpanFactor(10) × max_solid_dimension,
      // so the camera at ~3.7 × solid_radius is always well inside the world.
      // (Using the world extent instead results in the camera being placed
      // ~3.7 × world_radius = ~36 × max_solid_dimension outside the world.)
      ui->ApplyCommand("/vis/scene/add/volume " + G4String(req.name) + "_pv");

      // Create the single-threaded tracer once.  Its nColumn/nRow are copied
      // from the MT tracer that was configured by the viewer's Initialise().
      // G4TheRayTracer (the base class, not G4TheMTRayTracer) processes events
      // on the master thread via G4EventManager::ProcessOneEvent(), avoiding
      // the worker-thread G4AllocatorPool corruption that plagues G4TheMTRayTracer.
      auto* rt_viewer = static_cast<G4RayTracerViewer*>(vis_manager->GetCurrentViewer());
      auto* mt_tracer = rt_viewer->GetTracer();
      st_tracer       = new G4TheRayTracer();
      st_tracer->SetNColumn(mt_tracer->GetNColumn());
      st_tracer->SetNRow(mt_tracer->GetNRow());

      visualization_ready = true;
    } else {
      // If the vis setup failed on the first call, skip subsequent renders too.
      if (!visualization_ready || vis_manager == nullptr ||
          vis_manager->GetCurrentViewer() == nullptr) {
        return false;
      }
      // Swap geometry between fixtures without destroying the geometry stores.
      // Using destroyFirst=false avoids a dangling-pointer crash in
      // G4VisManager::GeometryHasChanged(): when destroyFirst=true,
      // G4PhysicalVolumeStore::Clean() deletes the world PV, but the navigation
      // manager still holds that pointer; GeometryHasChanged() then dereferences
      // it via GetWorldVolume() and crashes.  With destroyFirst=false the old
      // geometry objects accumulate in stores (bounded per fixture, acceptable
      // for a test binary) and the world PV stays valid through the call.
      // ReinitializeGeometry sets geometryInitialized=false so the subsequent
      // Initialize() call re-runs Construct() with the new fixture request.
      runManager->ReinitializeGeometry(/*destroyFirst=*/false, /*prop=*/false);
      runManager->Initialize();

      // Clear the scene's run-duration model list (which points to the previous
      // fixture's PV) so the tracer only renders the new solid.  Then re-add
      // the new fixture volume to recompute the scene extent and mark the viewer
      // for a kernel re-visit.
      if (auto* scene = vis_manager->GetCurrentScene()) {
        scene->SetRunDurationModelList().clear();
      }
      ui->ApplyCommand("/vis/scene/add/volume " + G4String(req.name) + "_pv");
    }

    std::filesystem::create_directories(output_path.parent_path());

    auto* viewer = vis_manager->GetCurrentViewer();
    if (viewer == nullptr) {
      return false;
    }

    // SetView() reads the current G4ViewParameters (fVP) — which include the
    // viewpoint direction configured by /vis/viewer/set/viewpointThetaPhi —
    // and computes eyePosition, targetPosition, lightDirection, viewSpan, etc.,
    // then pushes them onto the MT tracer.  We then copy those parameters to
    // the single-threaded tracer and call Trace() directly.
    ui->ApplyCommand("/vis/viewer/set/viewpointThetaPhi 45 45");
    viewer->SetView();

    // Populate the G4RayTracerSceneHandler's scene-vis-attrs map by processing
    // the scene geometry.  G4RTSteppingAction looks up colours in this map during
    // the event loop inside CreateBitMap(); without a prior ProcessView() the map
    // is empty and every ray would produce only the background colour.
    // ProcessView() calls G4VSceneHandler::ProcessScene() only when
    // fNeedKernelVisit is true (which /vis/scene/add/volume ensures).
    // Crucially, ProcessView() does NOT call DrawView() / Trace() — it is the
    // scene-geometry traversal step only.
    viewer->ProcessView();

    // Copy camera parameters from the MT tracer (populated by SetView()) to the
    // single-threaded tracer, then render.  Using G4TheRayTracer (base class)
    // rather than G4TheMTRayTracer avoids G4AllocatorPool corruption: the MT
    // override calls BeamOn() which spawns worker threads that corrupt the
    // G4TouchableHistory allocator (free-list head overwritten with the vptr).
    // The base-class Trace() processes events on the calling thread via
    // G4EventManager::ProcessOneEvent(), with no worker threads involved.
    auto* mt_tracer = static_cast<G4RayTracerViewer*>(viewer)->GetTracer();
    st_tracer->SetEyePosition(mt_tracer->GetEyePosition());
    st_tracer->SetTargetPosition(mt_tracer->GetTargetPosition());
    st_tracer->SetLightDirection(mt_tracer->GetLightDirection());
    st_tracer->SetViewSpan(mt_tracer->GetViewSpan());
    st_tracer->SetHeadAngle(mt_tracer->GetHeadAngle());
    st_tracer->SetUpVector(mt_tracer->GetUpVector());
    // Dark background: high contrast against the fixture geometry and more
    // visually appealing than the default white.
    st_tracer->SetBackgroundColour(G4Colour(0.12, 0.12, 0.16));

    // G4TheRayTracer::Trace() processes events on the master thread and writes
    // the JPEG at the exact path given (no extension is appended).
    st_tracer->Trace(output_path.string());

    return std::filesystem::exists(output_path);
  }

  // ── Command-line options ─────────────────────────────────────────────────

  struct CommandLineOptions {
    std::filesystem::path output_dir{"geometry_renders"};
    std::filesystem::path manifest_path{DefaultRepositoryManifestPath()};
    /// Optional filter: only render this qualified fixture ID ("family/id").
    /// Empty string means render all fixtures.
    std::string fixture_filter;
  };

  CommandLineOptions ParseCommandLine(int argc, char** argv) {
    CommandLineOptions opts;
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--fixture" || arg == "--fixture-id") {
        if (i + 1 >= argc) {
          throw std::runtime_error("Missing value after " + arg);
        }
        opts.fixture_filter = argv[++i];
      } else if (arg.rfind("--fixture=", 0) == 0) {
        opts.fixture_filter = arg.substr(std::string("--fixture=").size());
      } else if (arg == "--output-dir") {
        if (i + 1 >= argc) {
          throw std::runtime_error("Missing value after --output-dir");
        }
        opts.output_dir = argv[++i];
      } else if (arg.rfind("--output-dir=", 0) == 0) {
        opts.output_dir = arg.substr(std::string("--output-dir=").size());
      } else if (arg == "--manifest") {
        if (i + 1 >= argc) {
          throw std::runtime_error("Missing value after --manifest");
        }
        opts.manifest_path = argv[++i];
      } else if (arg.rfind("--manifest=", 0) == 0) {
        opts.manifest_path = arg.substr(std::string("--manifest=").size());
      } else if (!arg.empty() && arg[0] != '-') {
        // Legacy positional arguments: <output_dir> [<manifest_path>]
        if (opts.output_dir == std::filesystem::path{"geometry_renders"} &&
            opts.manifest_path == DefaultRepositoryManifestPath()) {
          opts.output_dir = arg;
        } else {
          opts.manifest_path = arg;
        }
      } else {
        throw std::runtime_error("Unknown argument: " + arg);
      }
    }
    return opts;
  }

  // ── Main render loop ──────────────────────────────────────────────────────

  int RunRender(const std::filesystem::path& output_dir,
                const std::filesystem::path& repository_manifest_path,
                const std::string& fixture_filter = {}) {
    std::filesystem::create_directories(output_dir);

    // Use SerialOnly to avoid the G4AllocatorPool corruption in
    // G4TouchableHistory that occurs when G4MTRunManager::Initialize() calls
    // BeamOn(0) to spawn worker threads.  G4TheRayTracer (base class, single-
    // threaded) processes events on the calling thread via
    // G4EventManager::ProcessOneEvent(), so it does not need an MT run manager.
    // Note: even in MT-built Geant4, G4RunManagerFactory accepts SerialOnly.
    auto* runManager = G4RunManagerFactory::CreateRunManager(G4RunManagerType::SerialOnly);
    runManager->SetVerboseLevel(0);

    auto* detector = new FixtureDetectorConstruction();
    runManager->SetUserInitialization(detector);
    runManager->SetUserInitialization(new MinimalPhysicsList());
    runManager->SetUserInitialization(new MinimalActionInitialization());

    G4UImanager* ui             = G4UImanager::GetUIpointer();
    G4VisExecutive* vis_manager = nullptr;
    G4TheRayTracer* st_tracer   = nullptr;
    bool initialized            = false;
    bool visualization_ready    = false;

    const FixtureRepositoryManifest repository_manifest =
        ParseFixtureRepositoryManifest(repository_manifest_path);

    std::size_t rendered_count       = 0;
    std::size_t skipped_count        = 0;
    std::size_t native_skipped_count = 0;
    std::size_t failed_count         = 0;
    bool done                        = false;

    for (const auto& family : repository_manifest.families) {
      if (done)
        break;
      const auto family_manifest_path = ResolveFamilyManifestPath(repository_manifest, family);
      if (!std::filesystem::exists(family_manifest_path)) {
        continue;
      }

      // In aggregate (no filter) mode skip families whose fixtures are large
      // multi-body assemblies.  Per-fixture tests registered in CMakeLists
      // simply never include nist-ctc, so this guard is only reached during
      // manual all-fixture runs.
      static const std::set<std::string> kSkipFamiliesAggregate = {"nist-ctc"};
      if (fixture_filter.empty() && kSkipFamiliesAggregate.count(family) != 0) {
        std::cout << "Skipping family '" << family
                  << "' (excluded from aggregate smoke-test renders)\n";
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
        const std::string qualified_id = family + "/" + fixture.id;

        // When a fixture filter is active, skip everything that doesn't match.
        if (!fixture_filter.empty() && qualified_id != fixture_filter) {
          continue;
        }

        const auto step_path       = ResolveFixtureStepPath(family_manifest, fixture);
        const auto provenance_path = ResolveFixtureProvenancePath(family_manifest, fixture);
        if (!std::filesystem::exists(step_path) || !std::filesystem::exists(provenance_path)) {
          ++skipped_count;
          continue;
        }

        const std::string safe_id = SafeFilename(qualified_id);

        try {
          // ── Native solid ─────────────────────────────────────────────────
          // G4OCCTSolid fixtures (e.g. NIST CTC) have no analytic native solid;
          // BuildNativeSolid() would throw for them.  Skip the native render and
          // emit only the imported image.
          const bool has_native_solid = (fixture.geant4_class != "G4OCCTSolid");
          if (has_native_solid) {
            FixtureDetectorConstruction::Request native_req;
            native_req.provenance_path = provenance_path;
            native_req.manifest        = family_manifest;
            native_req.fixture         = fixture;
            native_req.use_imported    = false;
            native_req.name            = G4String(fixture.id + "_native");

            const auto native_path = output_dir / (safe_id + "_native.jpeg");
            if (!RenderFixture(runManager, detector, vis_manager, ui, st_tracer, native_req,
                               native_path, initialized, visualization_ready)) {
              std::cerr << "WARNING: " << qualified_id << ": native render produced no output\n";
              ++native_skipped_count;
            }
          }

          // ── Imported solid (STEP → G4OCCTSolid) ─────────────────────────
          FixtureDetectorConstruction::Request imported_req;
          imported_req.provenance_path = provenance_path;
          imported_req.manifest        = family_manifest;
          imported_req.fixture         = fixture;
          imported_req.use_imported    = true;
          imported_req.name            = G4String(fixture.id + "_imported");

          const auto imported_path = output_dir / (safe_id + "_imported.jpeg");
          if (!RenderFixture(runManager, detector, vis_manager, ui, st_tracer, imported_req,
                             imported_path, initialized, visualization_ready)) {
            std::cerr << "WARNING: " << qualified_id << ": imported render produced no output\n";
            ++skipped_count;
            done = !fixture_filter.empty();
            continue;
          }

          ++rendered_count;
          std::cout << "Rendered " << qualified_id << '\n';
        } catch (const std::exception& error) {
          std::cerr << "ERROR: " << qualified_id << ": " << error.what() << '\n';
          ++failed_count;
        }

        // In single-fixture mode stop after the first (and only) match.
        if (!fixture_filter.empty()) {
          done = true;
          break;
        }
      }
    }

    delete st_tracer;
    delete vis_manager;
    delete runManager;

    if (!fixture_filter.empty() && rendered_count == 0 && failed_count == 0) {
      std::cerr << "ERROR: fixture '" << fixture_filter << "' not found in manifest.\n";
      return EXIT_FAILURE;
    }
    std::cout << "Render summary: " << rendered_count << " rendered, " << skipped_count
              << " skipped, " << native_skipped_count << " native skipped, " << failed_count
              << " failed.\n";
    return (failed_count > 0U) ? EXIT_FAILURE : EXIT_SUCCESS;
  }

} // namespace

} // namespace g4occt::tests::geometry

int main(int argc, char** argv) {
  try {
    const auto opts = g4occt::tests::geometry::ParseCommandLine(argc, argv);
    return g4occt::tests::geometry::RunRender(opts.output_dir, opts.manifest_path,
                                              opts.fixture_filter);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: render_geometry_fixtures threw an exception: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
