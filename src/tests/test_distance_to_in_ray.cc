// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4SystemOfUnits.hh>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::ExpectDistanceToIn;

TEST(DistanceToInRay, HitReturnsEntryDistance) {
  const BoxFixture box("DistanceToInRayHitBox");

  ExpectDistanceToIn("ray hit returns distance to +x face", box.solid, box.OutsideX(),
                     G4ThreeVector(-1.0, 0.0, 0.0), 5.0 * mm);
}

TEST(DistanceToInRay, MissReturnsInfinity) {
  const BoxFixture box("DistanceToInRayMissBox");

  ExpectDistanceToIn("ray miss returns kInfinity", box.solid, box.OutsideX(),
                     G4ThreeVector(1.0, 0.0, 0.0), kInfinity);
}

TEST(DistanceToInRay, ChoosesFirstPositiveIntersection) {
  const BoxFixture box("DistanceToInRayFirstHitBox");
  const G4ThreeVector start(-box.halfX - 15.0 * mm, 0.0, 0.0);

  ExpectDistanceToIn("ray chooses first positive hit", box.solid, start,
                     G4ThreeVector(1.0, 0.0, 0.0), 15.0 * mm);
}

// ── Curved-surface STEP fixture test ──────────────────────────────────────────

TEST(DistanceToInRay, TorusSurface) {
  // G4Torus: swept radius 20 mm, tube radius 5 mm.
  // Outermost surface along +X is at x = 25 mm.
  // A ray from (100, 0, 0) heading in −X must travel 75 mm before entry.
  const std::string step_path =
      (std::filesystem::path(G4OCCT_TEST_SOURCE_DIR) / "fixtures" / "geometry" /
       "direct-primitives" / "G4Torus" / "torus-rtor20-rmax5-v1" / "shape.step")
          .string();
  std::unique_ptr<G4OCCTSolid> solid(G4OCCTSolid::FromSTEP("TorusDistSTEP", step_path));

  const G4ThreeVector start(100.0 * mm, 0.0, 0.0);
  const G4ThreeVector dir(-1.0, 0.0, 0.0);
  ExpectDistanceToIn("torus ray from +x hits outer surface", *solid, start, dir, 75.0 * mm,
                     1e-3 * mm);
}

} // namespace
