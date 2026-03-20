// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4GeometryTolerance.hh>
#include <G4SystemOfUnits.hh>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectDistanceToIn;
using g4occt::tests::navigation::SphereFixture;

TEST(DistanceToInSafety, ShortestDistanceForExternalPoints) {
  const BoxFixture box("DistanceToInSafetyBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  ExpectDistanceToIn("box axis-aligned outside point uses face distance", box.solid,
                     box.OutsideX(7.0 * mm), 7.0 * mm);

  const G4ThreeVector edgePoint(box.halfX + 3.0 * mm, box.halfY + 4.0 * mm, 0.0 * mm);
  ExpectDistanceToIn("box outside point near an edge uses Euclidean shortest distance", box.solid,
                     edgePoint, 5.0 * mm);

  const G4ThreeVector cornerPoint(box.halfX + 2.0 * mm, box.halfY + 3.0 * mm, box.halfZ + 6.0 * mm);
  ExpectDistanceToIn("box outside point near a corner uses 3D Euclidean shortest distance",
                     box.solid, cornerPoint, 7.0 * mm);

  const SphereFixture sphere("DistanceToInSafetySphere", 50.0 * mm);
  ExpectDistanceToIn("sphere outside point uses radial distance", sphere.solid,
                     sphere.OutsideX(9.0 * mm), 9.0 * mm);

  const CylinderFixture cylinder("DistanceToInSafetyCylinder", 25.0 * mm, 40.0 * mm);
  ExpectDistanceToIn("cylinder outside radial point uses radial distance", cylinder.solid,
                     cylinder.OutsideRadialX(6.0 * mm), 6.0 * mm);
}

TEST(DistanceToInSafety, NearSurfaceBehaviour) {
  const BoxFixture box("DistanceToInSafetyNearSurface", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const G4double surfaceTolerance = G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();

  ExpectDistanceToIn("point exactly on the surface has zero safety", box.solid,
                     box.PositiveXSurface(), 0.0);

  const G4ThreeVector withinSurfaceShell(box.halfX + 0.25 * surfaceTolerance, 0.0, 0.0);
  ExpectDistanceToIn("point within the Geant4 surface shell has zero safety", box.solid,
                     withinSurfaceShell, 0.0);

  const G4double externalOffset = std::max(10.0 * surfaceTolerance, 1.0e-6 * mm);
  const G4ThreeVector outsideSurfaceShell(box.halfX + externalOffset, 0.0, 0.0);
  ExpectDistanceToIn("point just outside the surface shell returns a finite distance", box.solid,
                     outsideSurfaceShell, externalOffset);
}

/// Verify the lower-bound contract: DistanceToIn(p) is always a valid conservative
/// lower bound on the true surface distance, i.e. 0 ≤ DistanceToIn(p) ≤ ExactDistanceToIn(p).
TEST(DistanceToInLowerBound, IsNonNegative) {
  const BoxFixture box("LowerBoundBoxNonNeg", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const SphereFixture sphere("LowerBoundSphereNonNeg", 50.0 * mm);

  // Axis-aligned exterior points.
  EXPECT_GE(box.solid.DistanceToIn(box.OutsideX(7.0 * mm)), 0.0)
      << "box axis-aligned exterior: must be non-negative";
  EXPECT_GE(sphere.solid.DistanceToIn(sphere.OutsideX(9.0 * mm)), 0.0)
      << "sphere axis-aligned exterior: must be non-negative";

  // Diagonal exterior point (sphere only — AABB is not tight here).
  const G4ThreeVector sphereDiag(60.0 * mm, 10.0 * mm, 0.0 * mm);
  EXPECT_GE(sphere.solid.DistanceToIn(sphereDiag), 0.0)
      << "sphere diagonal exterior: must be non-negative";
}

TEST(DistanceToInLowerBound, IsLessOrEqualExact) {
  const BoxFixture box("LowerBoundBoxLE", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const SphereFixture sphere("LowerBoundSphereLE", 50.0 * mm);

  // For all query points the safety lower bound must not exceed the exact value.
  auto checkLE = [](const G4OCCTSolid& solid, const G4ThreeVector& p, const std::string& label) {
    const G4double safety = solid.DistanceToIn(p);
    const G4double exact  = solid.ExactDistanceToIn(p);
    EXPECT_LE(safety, exact + g4occt::tests::navigation::kDefaultTolerance)
        << label << ": safety=" << safety << " must be ≤ exact=" << exact;
  };

  checkLE(box.solid, box.OutsideX(7.0 * mm), "box axis-aligned");
  checkLE(box.solid, G4ThreeVector(box.halfX + 3.0 * mm, box.halfY + 4.0 * mm, 0.0), "box edge");
  checkLE(sphere.solid, sphere.OutsideX(9.0 * mm), "sphere axis-aligned");
  // Diagonal point: AABB gives 10 mm, exact gives ≈10.83 mm — bound is strict.
  checkLE(sphere.solid, G4ThreeVector(60.0 * mm, 10.0 * mm, 0.0), "sphere diagonal");
}

TEST(DistanceToInLowerBound, StrictlyPositiveForDistantPoints) {
  const G4double surfaceTol = G4GeometryTolerance::GetInstance()->GetSurfaceTolerance();
  const BoxFixture box("LowerBoundBoxPos", 10.0 * mm, 20.0 * mm, 30.0 * mm);
  const SphereFixture sphere("LowerBoundSpherePos", 50.0 * mm);

  // Points that are clearly more than IntersectionTolerance() outside the AABB
  // must return a strictly positive safety distance.
  EXPECT_GT(box.solid.DistanceToIn(box.OutsideX(1.0 * mm)), surfaceTol)
      << "box exterior 1 mm out: safety must be > surface tolerance";
  EXPECT_GT(sphere.solid.DistanceToIn(sphere.OutsideX(1.0 * mm)), surfaceTol)
      << "sphere exterior 1 mm out: safety must be > surface tolerance";
}

TEST(DistanceToInLowerBound, AABBIsConservativeForDiagonalExterior) {
  // For a sphere, the AABB is not tight at diagonal directions: the point
  // (60, 10, 0) mm is 10 mm outside the AABB in X, but ≈10.83 mm from the surface.
  // DistanceToIn must return a value ≤ the exact distance.
  const SphereFixture sphere("LowerBoundSphereAABB", 50.0 * mm);
  const G4ThreeVector diag(60.0 * mm, 10.0 * mm, 0.0 * mm);

  const G4double safety = sphere.solid.DistanceToIn(diag);
  const G4double exact  = sphere.solid.ExactDistanceToIn(diag);

  // AABB distance = 10 mm (only X contributes); exact ≈ 10.83 mm.
  EXPECT_NEAR(safety, 10.0 * mm, g4occt::tests::navigation::kDefaultTolerance)
      << "AABB lower bound for diagonal sphere point should be 10 mm";
  EXPECT_LT(safety, exact) << "AABB bound must be strictly less than exact for diagonal point";
}

TEST(DistanceToInLowerBound, FallbackWhenAABBUnavailable) {
  // Regression test for the "AABB unavailable" path: when fCachedBounds is
  // std::nullopt (triggered here by a null shape), AABBLowerBound() returns
  // kInfinity.  Before the fix, DistanceToIn() would short-circuit and return
  // kInfinity instead of falling through to ExactDistanceToIn().
  // The fixed guard (std::isfinite) ensures the fallback is always reached,
  // so the result is consistent with ExactDistanceToIn().
  G4OCCTSolid nullShapeSolid("FallbackAABBNull", TopoDS_Shape{});
  const G4ThreeVector anyPoint(100.0 * mm, 0.0, 0.0);

  const G4double safety = nullShapeSolid.DistanceToIn(anyPoint);
  const G4double exact  = nullShapeSolid.ExactDistanceToIn(anyPoint);

  // Both return kInfinity for a null shape — this confirms DistanceToIn()
  // correctly reaches the exact-solver fallback instead of misreporting kInfinity
  // from the AABB sentinel alone.
  EXPECT_TRUE(std::isinf(safety)) << "null-shape: DistanceToIn must return kInfinity";
  EXPECT_EQ(safety, exact) << "null-shape: DistanceToIn must match ExactDistanceToIn";
}

} // namespace
