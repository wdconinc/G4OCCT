// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/**
 * @file bench_callgrind.cc
 * @brief Callgrind-instrumented single-fixture navigator benchmark.
 *
 * Runs the solid-navigation hot loop for a single geometry fixture under
 * callgrind.  Setup costs (STEP import, solid construction, test-data
 * generation, warm-up calls) are excluded from the profile: instrumentation
 * starts only when the steady-state query loop begins.
 *
 * Usage:
 * @code
 *   # Normal (no profiling):
 *   ./bench_callgrind [fixture_id] [manifest_path] [ray_count]
 *
 *   # Under callgrind (post-warm-up profiling only):
 *   valgrind --tool=callgrind --instr-atstart=no --collect-atstart=no \
 *     ./bench_callgrind [fixture_id] [manifest_path] [ray_count]
 *   callgrind_annotate --auto=yes callgrind.out.<PID> > callgrind_annotate.txt
 * @endcode
 *
 * The binary uses CALLGRIND_START_INSTRUMENTATION + CALLGRIND_TOGGLE_COLLECT
 * to gate both instrumentation and collection to the steady-state hot loop.
 * When launched without valgrind, or built without valgrind headers, all
 * CALLGRIND_* macros become no-ops.
 *
 * Defaults:
 *   - fixture_id  : g4box-box-20x30x40-v1 (direct-primitives family)
 *   - manifest     : \<source-dir\>/fixtures/geometry/manifest.yaml
 *   - ray_count    : 2048 (must be > 0)
 */

// Callgrind instrumentation macros.  When the binary runs outside valgrind, or
// when the valgrind headers are absent at build time, these expand to no-ops.
#ifdef HAVE_VALGRIND_CALLGRIND_H
#include <valgrind/callgrind.h>
#else
#define CALLGRIND_START_INSTRUMENTATION                                                            \
  do {                                                                                             \
  } while (0)
#define CALLGRIND_STOP_INSTRUMENTATION                                                             \
  do {                                                                                             \
  } while (0)
#define CALLGRIND_TOGGLE_COLLECT                                                                   \
  do {                                                                                             \
  } while (0)
#endif

#include "G4OCCT/G4OCCTSolid.hh"

#include "geometry/fixture_manifest.hh"
#include "geometry/fixture_ray_compare.hh"
#include "geometry/fixture_solid_builder.hh"
#include "geometry/fixture_validation.hh"

#include <G4ThreeVector.hh>
#include <G4VSolid.hh>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace g4occt::benchmarks {
namespace {

  using g4occt::tests::geometry::BuildNativeSolidForRequest;
  using g4occt::tests::geometry::FixtureComparisonOrigin;
  using g4occt::tests::geometry::FixtureManifest;
  using g4occt::tests::geometry::FixtureReference;
  using g4occt::tests::geometry::FixtureRepositoryManifest;
  using g4occt::tests::geometry::FixtureValidationRequest;
  using g4occt::tests::geometry::GenerateBoundingBoxPoints;
  using g4occt::tests::geometry::GenerateDirections;
  using g4occt::tests::geometry::LoadImportedShape;
  using g4occt::tests::geometry::ParseFixtureManifestFile;
  using g4occt::tests::geometry::ParseFixtureProvenance;
  using g4occt::tests::geometry::ParseFixtureRepositoryManifest;
  using g4occt::tests::geometry::ResolveFamilyManifestPath;

  /// Locate a fixture by ID in the repository manifest.  Searches all family
  /// manifests and returns a pair of (family-manifest, fixture-reference) on
  /// success.  Throws std::runtime_error if the fixture is not found.
  std::pair<FixtureManifest, FixtureReference>
  FindFixtureById(const FixtureRepositoryManifest& repository_manifest,
                  const std::string& fixture_id) {
    for (const auto& family : repository_manifest.families) {
      const auto family_manifest_path = ResolveFamilyManifestPath(repository_manifest, family);
      if (!std::filesystem::exists(family_manifest_path)) {
        continue;
      }
      FixtureManifest family_manifest;
      try {
        family_manifest = ParseFixtureManifestFile(family_manifest_path);
      } catch (const std::exception& error) {
        throw std::runtime_error("Failed to parse family manifest '" +
                                 family_manifest_path.string() + "': " + error.what());
      }
      for (const auto& fixture : family_manifest.fixtures) {
        if (fixture.id == fixture_id) {
          return {family_manifest, fixture};
        }
      }
    }
    throw std::runtime_error("Fixture not found in any family manifest: " + fixture_id);
  }

  int RunCallgrindBenchmark(const std::string& fixture_id,
                            const std::filesystem::path& repository_manifest_path,
                            const std::size_t ray_count) {
    std::cout << "Callgrind benchmark: fixture=" << fixture_id << "  rays=" << ray_count << "\n";

    // Number of iterations used for warm-up (not profiled).
    constexpr std::size_t kWarmupIterations = 32U;

    // ── Find fixture ──────────────────────────────────────────────────────
    const FixtureRepositoryManifest repository_manifest =
        ParseFixtureRepositoryManifest(repository_manifest_path);
    const auto [family_manifest, fixture] = FindFixtureById(repository_manifest, fixture_id);

    FixtureValidationRequest request;
    request.manifest                = family_manifest;
    request.fixture                 = fixture;
    request.require_provenance_file = true;

    // ── Build solids (setup – excluded from callgrind profile) ────────────
    std::cout << "Loading fixture assets...\n";
    const auto provenance_path =
        g4occt::tests::geometry::ResolveFixtureProvenancePath(family_manifest, fixture);
    const auto provenance = ParseFixtureProvenance(provenance_path);

    std::unique_ptr<G4VSolid> native_solid = BuildNativeSolidForRequest(request, provenance);
    std::unique_ptr<G4VSolid> imported_solid =
        std::make_unique<G4OCCTSolid>(fixture.id + "_imported", LoadImportedShape(request));

    // ── Generate test data ────────────────────────────────────────────────
    const G4ThreeVector native_origin   = FixtureComparisonOrigin(provenance, *native_solid);
    const G4ThreeVector imported_origin = FixtureComparisonOrigin(provenance, *imported_solid);

    const std::vector<G4ThreeVector> directions = GenerateDirections(ray_count);
    const EInside native_origin_state           = native_solid->Inside(native_origin);
    const EInside imported_origin_state         = imported_solid->Inside(imported_origin);

    // Sample points from the bounding box for Inside(p) and safety queries.
    const std::vector<G4ThreeVector> bbox_points =
        GenerateBoundingBoxPoints(*native_solid, ray_count);

    // ── Pre-classify bbox_points into inside/outside sets ─────────────────
    // Classification is done before the profiled loop so that the safety
    // section calls the semantically correct overload for each point,
    // matching what CompareFixtureSafety does.  kSurface points are skipped:
    // their safety distance is 0 by definition.
    std::vector<G4ThreeVector> outside_points;
    std::vector<G4ThreeVector> inside_points;
    outside_points.reserve(bbox_points.size());
    inside_points.reserve(bbox_points.size());
    for (const auto& point : bbox_points) {
      const EInside state = native_solid->Inside(point);
      if (state == kOutside) {
        outside_points.push_back(point);
      } else if (state == kInside) {
        inside_points.push_back(point);
      }
    }

    // ── Warm-up: prime instruction caches and OCCT internal state ─────────
    // These calls mirror every call site in the hot loop so that no
    // lazy-initialised state is captured in the profiled section.
    std::cout << "Warming up...\n";

    // DistanceToIn / DistanceToOut – collect warm-up hit points for SurfaceNormal.
    std::vector<G4ThreeVector> warmup_native_hits;
    std::vector<G4ThreeVector> warmup_imported_hits;
    for (std::size_t i = 0; i < std::min<std::size_t>(directions.size(), kWarmupIterations); ++i) {
      if (native_origin_state == kOutside) {
        const G4double dist = native_solid->DistanceToIn(native_origin, directions[i]);
        if (!std::isinf(dist)) {
          warmup_native_hits.push_back(native_origin + dist * directions[i]);
        }
      } else {
        G4ThreeVector norm;
        G4bool validNorm = false;
        const G4double dist =
            native_solid->DistanceToOut(native_origin, directions[i], true, &validNorm, &norm);
        if (!std::isinf(dist)) {
          warmup_native_hits.push_back(native_origin + dist * directions[i]);
        }
      }
      if (imported_origin_state == kOutside) {
        const G4double dist = imported_solid->DistanceToIn(imported_origin, directions[i]);
        if (!std::isinf(dist)) {
          warmup_imported_hits.push_back(imported_origin + dist * directions[i]);
        }
      } else {
        G4ThreeVector norm;
        G4bool validNorm = false;
        const G4double dist =
            imported_solid->DistanceToOut(imported_origin, directions[i], true, &validNorm, &norm);
        if (!std::isinf(dist)) {
          warmup_imported_hits.push_back(imported_origin + dist * directions[i]);
        }
      }
    }

    // Inside(p).
    for (std::size_t i = 0; i < std::min<std::size_t>(bbox_points.size(), kWarmupIterations); ++i) {
      (void)native_solid->Inside(bbox_points[i]);
      (void)imported_solid->Inside(bbox_points[i]);
    }

    // Safety – DistanceToIn(p) on outside points, DistanceToOut(p) on inside points.
    for (std::size_t i = 0; i < std::min<std::size_t>(outside_points.size(), kWarmupIterations);
         ++i) {
      (void)native_solid->DistanceToIn(outside_points[i]);
      (void)imported_solid->DistanceToIn(outside_points[i]);
    }
    for (std::size_t i = 0; i < std::min<std::size_t>(inside_points.size(), kWarmupIterations);
         ++i) {
      (void)native_solid->DistanceToOut(inside_points[i]);
      (void)imported_solid->DistanceToOut(inside_points[i]);
    }

    // SurfaceNormal(p) on warm-up ray hit points.
    for (const auto& point : warmup_native_hits) {
      (void)native_solid->SurfaceNormal(point);
    }
    for (const auto& point : warmup_imported_hits) {
      (void)imported_solid->SurfaceNormal(point);
    }

    // ── Hot loop (callgrind-instrumented) ─────────────────────────────────
    // CALLGRIND_START_INSTRUMENTATION re-enables JIT recompilation so that
    // subsequent instructions are counted.  CALLGRIND_TOGGLE_COLLECT switches
    // event collection on (it was disabled at launch via --collect-atstart=no)
    // so that only the hot loop contributes to the callgrind profile.
    std::cout << "Starting instrumented hot loop...\n";
    CALLGRIND_START_INSTRUMENTATION;
    CALLGRIND_TOGGLE_COLLECT;

    // ── DistanceToIn / DistanceToOut – collect surface hit points ─────────
    std::vector<G4ThreeVector> native_hit_points;
    std::vector<G4ThreeVector> imported_hit_points;
    native_hit_points.reserve(directions.size());
    imported_hit_points.reserve(directions.size());

    for (const auto& direction : directions) {
      if (native_origin_state == kOutside) {
        const G4double dist = native_solid->DistanceToIn(native_origin, direction);
        if (!std::isinf(dist)) {
          native_hit_points.push_back(native_origin + dist * direction);
        }
      } else {
        G4ThreeVector norm;
        G4bool validNorm = false;
        const G4double dist =
            native_solid->DistanceToOut(native_origin, direction, true, &validNorm, &norm);
        if (!std::isinf(dist)) {
          native_hit_points.push_back(native_origin + dist * direction);
        }
      }
    }
    for (const auto& direction : directions) {
      if (imported_origin_state == kOutside) {
        const G4double dist = imported_solid->DistanceToIn(imported_origin, direction);
        if (!std::isinf(dist)) {
          imported_hit_points.push_back(imported_origin + dist * direction);
        }
      } else {
        G4ThreeVector norm;
        G4bool validNorm = false;
        const G4double dist =
            imported_solid->DistanceToOut(imported_origin, direction, true, &validNorm, &norm);
        if (!std::isinf(dist)) {
          imported_hit_points.push_back(imported_origin + dist * direction);
        }
      }
    }

    // ── Inside(p) ─────────────────────────────────────────────────────────
    for (const auto& point : bbox_points) {
      (void)native_solid->Inside(point);
    }
    for (const auto& point : bbox_points) {
      (void)imported_solid->Inside(point);
    }

    // ── DistanceToIn(p) / DistanceToOut(p) safety ────────────────────────
    // Outside points → DistanceToIn(p); inside points → DistanceToOut(p).
    // kSurface points are excluded (safety distance is 0 by definition).
    for (const auto& point : outside_points) {
      (void)native_solid->DistanceToIn(point);
    }
    for (const auto& point : inside_points) {
      (void)native_solid->DistanceToOut(point);
    }
    for (const auto& point : outside_points) {
      (void)imported_solid->DistanceToIn(point);
    }
    for (const auto& point : inside_points) {
      (void)imported_solid->DistanceToOut(point);
    }

    // ── SurfaceNormal(p) at ray hit points ───────────────────────────────
    // Surface points are obtained from the ray-casting loop above, matching
    // the approach used in CompareFixtureRays.
    for (const auto& point : native_hit_points) {
      (void)native_solid->SurfaceNormal(point);
    }
    for (const auto& point : imported_hit_points) {
      (void)imported_solid->SurfaceNormal(point);
    }

    CALLGRIND_TOGGLE_COLLECT;
    CALLGRIND_STOP_INSTRUMENTATION;
    std::cout << "Hot loop complete.\n";

    return EXIT_SUCCESS;
  }

} // namespace
} // namespace g4occt::benchmarks

int main(int argc, char** argv) {
  try {
    // Default fixture: g4box-box-20x30x40-v1 (direct-primitives family).
    const std::string fixture_id =
        argc > 1 ? std::string(argv[1]) : std::string("g4box-box-20x30x40-v1");
    const std::filesystem::path manifest_path =
        argc > 2 ? std::filesystem::path(argv[2])
                 : g4occt::tests::geometry::DefaultRepositoryManifestPath();
    const std::size_t ray_count = argc > 3 ? static_cast<std::size_t>(std::stoul(argv[3])) : 2048U;

    if (ray_count == 0U) {
      std::cerr << "FAIL: ray_count must be greater than zero\n";
      return EXIT_FAILURE;
    }

    return g4occt::benchmarks::RunCallgrindBenchmark(fixture_id, manifest_path, ray_count);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
