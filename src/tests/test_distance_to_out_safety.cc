// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4GeometryTolerance.hh>
#include <G4SystemOfUnits.hh>

#include <gtest/gtest.h>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::kDefaultTolerance;
using g4occt::tests::navigation::SphereFixture;

// DistanceToOut(p) is specified to return a conservative lower bound on the
// true surface distance (safety estimate).  The exact values are tested via
// ExactDistanceToOut in test_exact_distance_to_out.cc.

/// Verify the lower-bound contract: 0 ≤ DistanceToOut(p) ≤ ExactDistanceToOut(p).
TEST(DistanceToOutLowerBound, IsNonNegative) {
  const BoxFixture box("LBOutBoxNonNeg", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const SphereFixture sphere("LBOutSphereNonNeg", 50.0 * mm);

  EXPECT_GE(box.solid.DistanceToOut(box.Center()), 0.0)
      << "box center: DistanceToOut must be non-negative";
  EXPECT_GE(box.solid.DistanceToOut(G4ThreeVector(2.0 * mm, 3.0 * mm, 4.0 * mm)), 0.0)
      << "box offset: DistanceToOut must be non-negative";
  EXPECT_GE(sphere.solid.DistanceToOut(sphere.Center()), 0.0)
      << "sphere center: DistanceToOut must be non-negative";
  EXPECT_GE(sphere.solid.DistanceToOut(G4ThreeVector(12.0 * mm, 0.0 * mm, 0.0 * mm)), 0.0)
      << "sphere offset: DistanceToOut must be non-negative";
}

TEST(DistanceToOutLowerBound, IsLessOrEqualExact) {
  const BoxFixture box("LBOutBoxLE", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const SphereFixture sphere("LBOutSphereLE", 50.0 * mm);
  const CylinderFixture cylinder("LBOutCylinderLE", 25.0 * mm, 40.0 * mm);

  auto checkLE = [](const G4OCCTSolid& solid, const G4ThreeVector& p, const std::string& label) {
    const G4double safety = solid.DistanceToOut(p);
    const G4double exact  = solid.ExactDistanceToOut(p);
    EXPECT_LE(safety, exact + kDefaultTolerance)
        << label << ": safety=" << safety << " must be ≤ exact=" << exact;
  };

  checkLE(box.solid, box.Center(), "box center");
  checkLE(box.solid, G4ThreeVector(2.0 * mm, 3.0 * mm, 4.0 * mm), "box offset");
  checkLE(sphere.solid, sphere.Center(), "sphere center");
  checkLE(sphere.solid, G4ThreeVector(12.0 * mm, 0.0 * mm, 0.0 * mm), "sphere offset");
  checkLE(cylinder.solid, cylinder.Center(), "cylinder center");
  checkLE(cylinder.solid, G4ThreeVector(20.0 * mm, 0.0 * mm, 0.0 * mm), "cylinder radial");
  checkLE(cylinder.solid, G4ThreeVector(0.0 * mm, 0.0 * mm, 35.0 * mm), "cylinder axial");
}

TEST(DistanceToOutLowerBound, StrictlyPositiveForInteriorPoints) {
  const G4double surfaceTol = G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();
  const BoxFixture box("LBOutBoxPos", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const SphereFixture sphere("LBOutSpherePos", 50.0 * mm);

  // Points clearly inside (> surface tolerance from any face) must return > 0.
  EXPECT_GT(box.solid.DistanceToOut(box.Center()), surfaceTol)
      << "box center is clearly inside: safety must be positive";
  EXPECT_GT(sphere.solid.DistanceToOut(sphere.Center()), surfaceTol)
      << "sphere center is clearly inside: safety must be positive";
  EXPECT_GT(sphere.solid.DistanceToOut(G4ThreeVector(12.0 * mm, 0.0 * mm, 0.0 * mm)), surfaceTol)
      << "sphere interior offset point: safety must be positive";
}

} // namespace
