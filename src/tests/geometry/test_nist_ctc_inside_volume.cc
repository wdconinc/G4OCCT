// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file test_nist_ctc_inside_volume.cc
/// @brief Validate G4OCCTSolid::Inside() volume estimates for NIST CTC STEP files.
///
/// For each NIST Simplified Test Case (CTC) STEP fixture that is present on
/// disk, this test:
///   1. Imports the STEP file with OCCT and computes the reference volume via
///      BRepGProp::VolumeProperties().
///   2. Constructs a G4OCCTSolid from the imported shape.
///   3. Samples 10 000 random points uniformly inside the bounding box (using a
///      fixed seed for reproducibility).
///   4. Calls G4OCCTSolid::Inside() for each point.
///   5. Estimates the volume as (inside_count / total_count) * bbox_volume.
///   6. Requires that the relative difference between the Monte-Carlo estimate
///      and the OCCT reference volume is within kVolumeTolerance.
///
/// Fixtures whose shape.step file has not been fetched (run fetch.sh in the
/// nist-ctc family directory) are silently skipped; the test exits with
/// success so that CI passes even when the NIST files are absent.
///
/// When invoked with a single positional argument (a fixture ID such as
/// "nist-ctc-01-v1"), only that fixture is tested.  CTest uses this to
/// register one test per fixture so that progress is visible during the run.

#include "geometry/fixture_manifest.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Reader.hxx>
#include <TopoDS_Shape.hxx>

#include <G4SystemOfUnits.hh>
#include <G4ThreeVector.hh>
#include <globals.hh>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>

namespace {

#ifndef G4OCCT_TEST_SOURCE_DIR
#define G4OCCT_TEST_SOURCE_DIR "."
#endif

/// Number of random sample points used for the Monte-Carlo volume estimate.
constexpr int kSampleCount = 10000;

/// Relative tolerance for the Inside()-based volume estimate vs. OCCT reference.
constexpr double kVolumeTolerance = 0.10; // 10 %

/// Fixed seed for reproducible random sampling.
constexpr unsigned int kRandomSeed = 42u;

/// Path to the NIST CTC family manifest.
std::filesystem::path NistManifestPath() {
  return std::filesystem::path(G4OCCT_TEST_SOURCE_DIR) / "fixtures" / "geometry" / "nist-ctc" /
         "manifest.yaml";
}

/// Load a STEP file and return the single top-level shape.
/// Returns a null shape on failure (error printed to stderr).
TopoDS_Shape LoadStepFile(const std::filesystem::path& step_path) {
  // Force unit conversion to millimetres so that G4OCCTSolid coordinates are
  // consistent with the Geant4 mm convention regardless of the native STEP
  // file unit declaration.
  Interface_Static::SetCVal("xstep.cascade.unit", "MM");

  STEPControl_Reader reader;
  const IFSelect_ReturnStatus status = reader.ReadFile(step_path.string().c_str());
  if (status != IFSelect_RetDone) {
    std::cerr << "FAIL: STEPControl_Reader could not read " << step_path.string() << "\n";
    return {};
  }
  if (reader.TransferRoots() <= 0) {
    std::cerr << "FAIL: No STEP roots transferred from " << step_path.string() << "\n";
    return {};
  }
  const TopoDS_Shape shape = reader.OneShape();
  if (shape.IsNull()) {
    std::cerr << "FAIL: Transferred shape is null for " << step_path.string() << "\n";
  }
  return shape;
}

/// Compute the OCCT volume (in mm^3) of @p shape via BRepGProp.
double OcctVolume(const TopoDS_Shape& shape) {
  GProp_GProps props;
  BRepGProp::VolumeProperties(shape, props);
  return props.Mass();
}

/// Estimate the volume of @p solid using Monte-Carlo Inside() sampling.
///
/// Generates @p n_samples uniformly-distributed random points inside the
/// bounding box and counts how many are reported as kInside.  Returns
/// (inside_count / n_samples) * bbox_volume.
double MonteCarloVolume(G4OCCTSolid& solid, int n_samples, unsigned int seed) {
  G4ThreeVector pMin, pMax;
  solid.BoundingLimits(pMin, pMax);

  const G4double dx          = pMax.x() - pMin.x();
  const G4double dy          = pMax.y() - pMin.y();
  const G4double dz          = pMax.z() - pMin.z();
  const G4double bbox_volume = dx * dy * dz;

  std::mt19937 rng(seed);
  std::uniform_real_distribution<G4double> rx(pMin.x(), pMax.x());
  std::uniform_real_distribution<G4double> ry(pMin.y(), pMax.y());
  std::uniform_real_distribution<G4double> rz(pMin.z(), pMax.z());

  long inside_count = 0;
  for (int i = 0; i < n_samples; ++i) {
    const G4ThreeVector pt(rx(rng), ry(rng), rz(rng));
    if (solid.Inside(pt) == kInside) {
      ++inside_count;
    }
  }

  return (static_cast<G4double>(inside_count) / static_cast<G4double>(n_samples)) * bbox_volume;
}

} // namespace

int main(int argc, char* argv[]) {
  using g4occt::tests::geometry::ParseFixtureManifestFile;
  using g4occt::tests::geometry::ResolveFixtureDirectory;
  using g4occt::tests::geometry::ResolveFixtureStepPath;

  // Optional positional argument: restrict the run to a single fixture ID.
  const std::string fixture_filter = (argc > 1) ? argv[1] : "";

  const std::filesystem::path manifest_path = NistManifestPath();
  if (!std::filesystem::exists(manifest_path)) {
    std::cerr << "FAIL: NIST manifest not found: " << manifest_path.string() << "\n";
    return EXIT_FAILURE;
  }

  g4occt::tests::geometry::FixtureManifest manifest;
  try {
    manifest = ParseFixtureManifestFile(manifest_path);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: Could not parse NIST manifest: " << error.what() << "\n";
    return EXIT_FAILURE;
  }

  // When a specific fixture ID was requested, verify it exists in the manifest.
  if (!fixture_filter.empty()) {
    const auto it = std::ranges::find_if(manifest.fixtures, [&](const auto& f) {
      return f.id == fixture_filter;
    });
    if (it == manifest.fixtures.end()) {
      std::cerr << "FAIL: Fixture '" << fixture_filter << "' not found in manifest.\n";
      return EXIT_FAILURE;
    }
  }

  int pass_count = 0;
  int fail_count = 0;
  int skip_count = 0;

  for (const auto& fixture : manifest.fixtures) {
    // Skip fixtures not matching the filter (when one was provided).
    if (!fixture_filter.empty() && fixture.id != fixture_filter) {
      continue;
    }

    const auto step_path = ResolveFixtureStepPath(manifest, fixture);

    if (!std::filesystem::exists(step_path)) {
      std::cout << "SKIP: " << fixture.id << " (shape.step not present — run fetch.sh)\n";
      ++skip_count;
      continue;
    }

    std::cout << "Testing " << fixture.id << " ...\n";

    // ── 1. Import STEP file ───────────────────────────────────────────────
    const TopoDS_Shape shape = LoadStepFile(step_path);
    if (shape.IsNull()) {
      ++fail_count;
      continue;
    }

    // ── 2. OCCT reference volume ──────────────────────────────────────────
    const double occt_volume = OcctVolume(shape);
    if (!(occt_volume > 0.0)) {
      std::cerr << "FAIL: " << fixture.id << ": OCCT volume is non-positive (" << occt_volume
                << " mm^3)\n";
      ++fail_count;
      continue;
    }

    // ── 3. Construct G4OCCTSolid ──────────────────────────────────────────
    G4OCCTSolid solid(fixture.id, shape);

    // ── 4 & 5. Monte-Carlo Inside() volume estimate ───────────────────────
    const double mc_volume = MonteCarloVolume(solid, kSampleCount, kRandomSeed);

    // ── 6. Compare ────────────────────────────────────────────────────────
    const double relative_error = std::fabs(mc_volume - occt_volume) / occt_volume;

    std::cout << "  OCCT volume  = " << occt_volume << " mm^3\n";
    std::cout << "  MC estimate  = " << mc_volume << " mm^3  (n=" << kSampleCount << ")\n";
    std::cout << "  Relative err = " << 100.0 * relative_error << " %"
              << "  (tolerance " << 100.0 * kVolumeTolerance << " %)\n";

    if (relative_error <= kVolumeTolerance) {
      std::cout << "  PASS: " << fixture.id << "\n";
      ++pass_count;
    } else {
      std::cerr << "  FAIL: " << fixture.id << ": relative volume error " << 100.0 * relative_error
                << " % exceeds tolerance " << 100.0 * kVolumeTolerance << " %\n";
      ++fail_count;
    }
  }

  std::cout << "\nResults: " << pass_count << " passed, " << fail_count << " failed, " << skip_count
            << " skipped.\n";

  if (fail_count > 0) {
    return EXIT_FAILURE;
  }
  if (skip_count == static_cast<int>(manifest.fixtures.size())) {
    std::cout << "NOTE: All fixtures were skipped because no STEP files were present.\n"
              << "      Run src/tests/fixtures/geometry/nist-ctc/fetch.sh to download them.\n";
  }
  return EXIT_SUCCESS;
}
