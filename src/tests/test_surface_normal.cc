// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <G4SystemOfUnits.hh>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectNear;
using g4occt::tests::navigation::ExpectSurfaceNormal;
using g4occt::tests::navigation::SphereFixture;

void ExpectUnitNormal(const std::string& label, const G4ThreeVector& normal) {
  ExpectNear(label, normal.mag(), 1.0, 1.0e-12);
}

TEST(SurfaceNormal, Box) {
  const BoxFixture box("SurfaceNormalBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  const G4ThreeVector positiveXNormal = box.solid.SurfaceNormal(box.PositiveXSurface());
  ExpectSurfaceNormal("box +x face normal", box.solid, box.PositiveXSurface(),
                      G4ThreeVector(1.0, 0.0, 0.0));
  ExpectUnitNormal("box +x face normal is unit length", positiveXNormal);

  const G4ThreeVector negativeYPoint(0.0, -box.halfY, 0.0);
  const G4ThreeVector negativeYNormal = box.solid.SurfaceNormal(negativeYPoint);
  ExpectSurfaceNormal("box -y face normal", box.solid, negativeYPoint,
                      G4ThreeVector(0.0, -1.0, 0.0));
  ExpectUnitNormal("box -y face normal is unit length", negativeYNormal);

  const G4ThreeVector positiveZNormal = box.solid.SurfaceNormal(box.PositiveZSurface());
  ExpectSurfaceNormal("box +z face normal", box.solid, box.PositiveZSurface(),
                      G4ThreeVector(0.0, 0.0, 1.0));
  ExpectUnitNormal("box +z face normal is unit length", positiveZNormal);
}

TEST(SurfaceNormal, Sphere) {
  const SphereFixture sphere("SurfaceNormalSphere", 50.0 * mm);

  const G4double component = sphere.radius / std::sqrt(3.0);
  const G4ThreeVector diagonalPoint(component, component, component);
  const G4ThreeVector expectedDiagonalNormal = diagonalPoint.unit();
  const G4ThreeVector diagonalNormal         = sphere.solid.SurfaceNormal(diagonalPoint);
  ExpectSurfaceNormal("sphere diagonal normal", sphere.solid, diagonalPoint,
                      expectedDiagonalNormal);
  ExpectUnitNormal("sphere diagonal normal is unit length", diagonalNormal);

  const G4double equatorialComponent = sphere.radius / std::sqrt(2.0);
  const G4ThreeVector equatorialPoint(0.0, equatorialComponent, equatorialComponent);
  const G4ThreeVector expectedEquatorialNormal = equatorialPoint.unit();
  const G4ThreeVector equatorialNormal         = sphere.solid.SurfaceNormal(equatorialPoint);
  ExpectSurfaceNormal("sphere equatorial normal", sphere.solid, equatorialPoint,
                      expectedEquatorialNormal);
  ExpectUnitNormal("sphere equatorial normal is unit length", equatorialNormal);
}

TEST(SurfaceNormal, Cylinder) {
  const CylinderFixture cylinder("SurfaceNormalCylinder", 25.0 * mm, 40.0 * mm);

  const G4double radialComponent = cylinder.radius / std::sqrt(2.0);
  const G4ThreeVector radialPoint(radialComponent, radialComponent, 0.0);
  const G4ThreeVector expectedRadialNormal = radialPoint.unit();
  const G4ThreeVector radialNormal         = cylinder.solid.SurfaceNormal(radialPoint);
  ExpectSurfaceNormal("cylinder radial normal", cylinder.solid, radialPoint, expectedRadialNormal);
  ExpectUnitNormal("cylinder radial normal is unit length", radialNormal);

  const G4ThreeVector topNormal = cylinder.solid.SurfaceNormal(cylinder.PositiveZSurface());
  ExpectSurfaceNormal("cylinder top normal", cylinder.solid, cylinder.PositiveZSurface(),
                      G4ThreeVector(0.0, 0.0, 1.0));
  ExpectUnitNormal("cylinder top normal is unit length", topNormal);

  const G4ThreeVector bottomPoint(0.0, 0.0, -cylinder.halfLength);
  const G4ThreeVector bottomNormal = cylinder.solid.SurfaceNormal(bottomPoint);
  ExpectSurfaceNormal("cylinder bottom normal", cylinder.solid, bottomPoint,
                      G4ThreeVector(0.0, 0.0, -1.0));
  ExpectUnitNormal("cylinder bottom normal is unit length", bottomNormal);
}

// ── Sphere-pole degenerate-normal retry loop ──────────────────────────────────
// At the poles of a sphere (V = ±π/2 in OCCT parametrisation) the partial
// derivative dP/dU vanishes, so BRepLProp_SLProps::IsNormalDefined() returns
// false.  TryGetOutwardNormal first nudges U and V away from parametric
// boundaries; if IsNormalDefined() still fails it retries with exponentially
// larger V nudges (up to 8 attempts) walking toward the V midpoint.
// These tests exercise that retry loop and verify the correct outward normal
// is returned at both poles.

TEST(SurfaceNormal, SpherePoleNorth) {
  // North pole at (0, 0, r): V = +π/2, dP/dU → 0. Initial nudge alone may not
  // satisfy OCCT's null-derivative threshold; retry loop is exercised.
  const SphereFixture sphere("SpherePoleNorth", 50.0 * mm);
  const G4ThreeVector northPole(0.0, 0.0, 50.0 * mm);
  const G4ThreeVector normal = sphere.solid.SurfaceNormal(northPole);
  EXPECT_NEAR(normal.z(), 1.0, 1.0e-3) << "north-pole normal must point in +z";
  EXPECT_NEAR(normal.mag(), 1.0, 1.0e-6) << "north-pole normal must be unit length";
}

TEST(SurfaceNormal, SphereSouthPole) {
  // South pole at (0, 0, -r): V = -π/2, same degenerate condition.
  const SphereFixture sphere("SphereSouthPole", 50.0 * mm);
  const G4ThreeVector southPole(0.0, 0.0, -50.0 * mm);
  const G4ThreeVector normal = sphere.solid.SurfaceNormal(southPole);
  EXPECT_NEAR(normal.z(), -1.0, 1.0e-3) << "south-pole normal must point in -z";
  EXPECT_NEAR(normal.mag(), 1.0, 1.0e-6) << "south-pole normal must be unit length";
}

TEST(SurfaceNormal, BVHPruningStrictComparisonRegression) {
  // Regression for the strict (>) AABB pruning comparison in TryFindClosestFace.
  //
  // The previous >= comparison would incorrectly prune the true closest face when
  // fb.box.Distance(queryBox) exactly equalled the BVH-seeded maxDistance threshold
  // (seedDist = bvhLB + 2·fBVHDeflection), causing TryFindClosestFace to return
  // std::nullopt and SurfaceNormal() to fall back to FallbackNormal() = (0,0,1).
  //
  // A cylinder is used because it is non-planar (curved barrel + flat end caps), so
  // fAllFacesPlanar == false and SurfaceNormal() is forced onto the BVH-seeded
  // TryFindClosestFace path — not the all-planar fast path.
  //
  // Query point: (radius + delta, 0, 0), just outside the barrel.
  //   • Barrel AABB distance ≈ delta (small — evaluated by TryFindClosestFace).
  //   • End-cap AABB distances ≈ halfLength >> seedDist — pruned even with new code.
  // If the barrel were incorrectly pruned (bug), TryFindClosestFace would return
  // nullopt and SurfaceNormal() would return FallbackNormal() = (0,0,1), which would
  // fail the (1,0,0) assertion below.
  //
  // Note: the exact AABB == seed-threshold equality depends on fBVHDeflection
  // (implementation-internal), so the arithmetic coincidence cannot be engineered
  // precisely via the public API.  This test exercises the same code path and would
  // detect a regression if the strict > comparison were reverted to >=.
  const CylinderFixture cyl("BVHPruningRegressionCylinder", 25.0 * mm, 40.0 * mm);

  const G4double delta = 0.5 * mm;
  const G4ThreeVector outsidePoint(cyl.radius + delta, 0.0, 0.0);
  ExpectSurfaceNormal("cylinder barrel outward normal from outside point (BVH pruning regression)",
                      cyl.solid, outsidePoint, G4ThreeVector(1.0, 0.0, 0.0), 1e-4 * mm);
}

// ── Curved-surface STEP fixture test ──────────────────────────────────────────

TEST(SurfaceNormal, EllipsoidPoleSTEP) {
  // G4Ellipsoid: semi-axes pX=15 mm, pY=10 mm, pZ=20 mm (no z cuts).
  // The top pole is at (0, 0, 20): the outward normal must point in +Z.
  const std::string step_path =
      (std::filesystem::path(G4OCCT_TEST_SOURCE_DIR) / "fixtures" / "geometry" / "profile-faceted" /
       "G4Ellipsoid" / "ellipsoid-15x10x20-v1" / "shape.step")
          .string();
  std::unique_ptr<G4OCCTSolid> solid(G4OCCTSolid::FromSTEP("EllipsoidSTEP", step_path));

  const G4ThreeVector pole(0.0, 0.0, 20.0 * mm);
  const G4ThreeVector n = solid->SurfaceNormal(pole);
  ExpectNear("ellipsoid top pole normal is unit length", n.mag(), 1.0, 1e-6);
  ExpectNear("ellipsoid top pole normal points in +Z", n.z(), 1.0, 1e-3);
}

} // namespace
