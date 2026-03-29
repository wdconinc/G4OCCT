// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Pnt.hxx>

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

// Solids with curved faces set fAllFacesPlanar = false, routing DistanceToOut(p)
// through BVHLowerBoundDistance instead of PlanarFaceLowerBoundDistance.
TEST(DistanceToOutBranchCoverage, NonPlanarSolidUsesBVHPath) {
  const SphereFixture sphere("BVHPathSphere", 50.0 * mm);
  const CylinderFixture cylinder("BVHPathCylinder", 25.0 * mm, 40.0 * mm);

  // Both solids have curved faces (fAllFacesPlanar = false) -> BVH path.
  EXPECT_GT(sphere.solid.DistanceToOut(sphere.Center()), 0.0)
      << "sphere center: BVH path must return positive distance";
  EXPECT_GT(cylinder.solid.DistanceToOut(cylinder.Center()), 0.0)
      << "cylinder center: BVH path must return positive distance";
  EXPECT_GT(cylinder.solid.DistanceToOut(G4ThreeVector(20.0 * mm, 0.0, 0.0)), 0.0)
      << "cylinder off-centre: BVH path must return positive distance";
}

// For a box with halfExtents (H,H,H), the inscribed sphere at the centre has
// radius H.  A point at (delta, 0, 0) has radius H - delta.  Dominance: gap =
// H - (H-delta) = delta, |c_new - c_centre|^2 = delta^2 <= delta^2 = gap^2.
TEST(DistanceToOutBranchCoverage, SphereCacheDominanceCheck) {
  // 10 mm half-extent box: centre sphere has exact radius 10 mm (planar path).
  const BoxFixture box("DominanceCheckBox", 10.0 * mm, 10.0 * mm, 10.0 * mm);

  // DistanceToOut at the centre -> TryInsertSphere((0,0,0), 10mm).
  // The centre sphere is already in fInitialSpheres; gap = 0, dist^2 = 0 <= 0
  // -> dominated (the identity check fires).
  const G4double dCenter = box.solid.DistanceToOut(G4ThreeVector(0.0, 0.0, 0.0));
  EXPECT_GE(dCenter, 0.0) << "box centre DistanceToOut must be non-negative";

  // DistanceToOut at (1mm, 0, 0) -> d = 9 mm.
  // Centre sphere (r=10mm): gap = 1mm, |c_new|^2 = 1mm^2 <= 1mm^2 = gap^2 -> dominated.
  const G4double d1mm = box.solid.DistanceToOut(G4ThreeVector(1.0 * mm, 0.0, 0.0));
  EXPECT_GE(d1mm, 0.0) << "box near-centre DistanceToOut must be non-negative";
  EXPECT_LE(d1mm, dCenter + kDefaultTolerance)
      << "near-centre point must have smaller or equal safety to centre";
}

// Filling the inscribed sphere cache to kMaxInscribedSpheres (64) and then
// adding a better sphere triggers eviction (pop_back).  Adding a sphere worse
// than the current minimum when the cache is full triggers the capacity early
// exit (return without insert).
//
// A flat box (+-100 x +-100 x +-10 mm) provides many interior points with
// r ~ halfZ = 10 mm.  These spheres are non-dominated by each other: equal
// radii -> gap = 0, so dominance fires only when |Dc| = 0 (same centre).
// After construction the box has 15 initial spheres.  Fifty grid calls fill the
// cache to 65 entries and trigger eviction; a subsequent near-corner call with
// r ~ 1 mm (< back().radius ~ 2.5 mm) triggers the capacity early exit.
TEST(DistanceToOutBranchCoverage, SphereCacheEvictionAndCapacityEarlyExit) {
  const G4double halfX = 100.0 * mm;
  const G4double halfY = 100.0 * mm;
  const G4double halfZ = 10.0 * mm;
  const TopoDS_Shape shape =
      BRepPrimAPI_MakeBox(gp_Pnt(-halfX, -halfY, -halfZ), gp_Pnt(halfX, halfY, halfZ)).Shape();
  G4OCCTSolid solid("SphereCacheEvictionTest", shape);

  // Fill the cache with 50 distinct interior points along a grid.  Points at
  // (x, y, 0) with |x| <= 75 mm have r = halfZ = 10 mm and are non-dominated.
  for (int i = 0; i < 50; ++i) {
    const G4double x = static_cast<G4double>(i - 25) * 3.0 * mm; // -75 mm to +72 mm
    const G4double y = static_cast<G4double>(i % 7) * 10.0 * mm; // 0 mm to 60 mm
    const G4double d = solid.DistanceToOut(G4ThreeVector(x, y, 0.0));
    EXPECT_GE(d, 0.0) << "cache fill point " << i << ": DistanceToOut must be non-negative";
  }

  // Near-corner point has r ~ 1 mm < 2.5 mm ~ back().radius when cache is full,
  // triggering the capacity early exit (size >= 64 && d <= back().radius -> return).
  const G4double dCorner =
      solid.DistanceToOut(G4ThreeVector(99.0 * mm, 99.0 * mm, 9.0 * mm));
  EXPECT_GE(dCorner, 0.0) << "near-corner DistanceToOut must be non-negative";
}

} // namespace
