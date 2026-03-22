// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <gtest/gtest.h>

namespace {

using g4occt::tests::navigation::BoxFixture;
using g4occt::tests::navigation::CylinderFixture;
using g4occt::tests::navigation::ExpectInside;
using g4occt::tests::navigation::SphereFixture;

TEST(InsideClassification, Box) {
  const BoxFixture box("InsideBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  ExpectInside("box center is inside", box.solid, box.Center(), kInside);
  ExpectInside("box +x face is on the surface", box.solid, box.PositiveXSurface(), kSurface);
  ExpectInside("box +y face is on the surface", box.solid, box.PositiveYSurface(), kSurface);
  ExpectInside("box +z face is on the surface", box.solid, box.PositiveZSurface(), kSurface);
  ExpectInside("box point beyond +x face is outside", box.solid, box.OutsideX(), kOutside);
}

TEST(InsideClassification, Sphere) {
  const SphereFixture sphere("InsideSphere", 50.0 * mm);

  ExpectInside("sphere center is inside", sphere.solid, sphere.Center(), kInside);
  ExpectInside("sphere +x point is on the surface", sphere.solid, sphere.PositiveXSurface(),
               kSurface);
  ExpectInside("sphere point beyond radius is outside", sphere.solid, sphere.OutsideX(), kOutside);
}

TEST(InsideClassification, Cylinder) {
  const CylinderFixture cylinder("InsideCylinder", 25.0 * mm, 40.0 * mm);

  ExpectInside("cylinder center is inside", cylinder.solid, cylinder.Center(), kInside);
  ExpectInside("cylinder radial point is on the surface", cylinder.solid,
               cylinder.PositiveRadialSurface(), kSurface);
  ExpectInside("cylinder top point is on the surface", cylinder.solid, cylinder.PositiveZSurface(),
               kSurface);
  ExpectInside("cylinder point beyond radius is outside", cylinder.solid, cylinder.OutsideRadialX(),
               kOutside);
}

// Tests for the inscribed-sphere fast path in Inside():
//   For a sphere of radius R, the inscribed sphere has radius ≈ 0.99*R.
//   Points within that sphere take the O(1) kInside short-circuit; points
//   outside fall through to BRepClass3d_SolidClassifier.  Both code paths
//   must return consistent classifications that match the analytic expectation
//   for the shapes under test.
TEST(InsideClassification, InscribedSphereFastPath) {
  // Use a large sphere so the inscribed sphere is clearly distinct from the
  // surface and we can place test points in the three distinct zones:
  //   Zone A — inside the inscribed sphere (fast kInside path)
  //   Zone B — between inscribed sphere and the surface (classifier path)
  //   Zone C — outside the solid (kOutside)
  const G4double kRadius = 100.0 * mm;
  const G4double kInscribedRadius =
      0.99 * kRadius; // radius scale factor applied in ComputeInscribedSphere()
  const SphereFixture sphere("InscribedSphereFastPath", kRadius);

  // Zone A: well inside the inscribed sphere (|p| << kInscribedRadius).
  ExpectInside("deep interior point is inside", sphere.solid, G4ThreeVector(0.0, 0.0, 0.0),
               kInside);
  ExpectInside("interior point along x is inside", sphere.solid,
               G4ThreeVector(0.5 * kRadius, 0.0, 0.0), kInside);
  ExpectInside("interior point along diagonal is inside", sphere.solid,
               G4ThreeVector(0.3 * kRadius, 0.3 * kRadius, 0.3 * kRadius), kInside);

  // Zone B: between the inscribed sphere boundary and the solid surface.
  // Points at |p| slightly above kInscribedRadius but below kRadius are outside
  // the inscribed sphere but still inside the solid.
  const G4double kZoneBRadius = kInscribedRadius + 0.5 * mm;
  ExpectInside("near-surface interior point along x is inside", sphere.solid,
               G4ThreeVector(kZoneBRadius, 0.0, 0.0), kInside);
  ExpectInside("near-surface interior point along -z is inside", sphere.solid,
               G4ThreeVector(0.0, 0.0, -kZoneBRadius), kInside);

  // Zone C: outside the solid surface.
  ExpectInside("point beyond +x surface is outside", sphere.solid,
               G4ThreeVector(kRadius + 5.0 * mm, 0.0, 0.0), kOutside);
  ExpectInside("point beyond +y surface is outside", sphere.solid,
               G4ThreeVector(0.0, kRadius + 5.0 * mm, 0.0), kOutside);

  // Box: the inscribed sphere radius ≈ 0.99 * min(halfX) ≈ 9.9 mm for a
  // 10×20×30 mm half-extent box.  Verify both the fast path (Zone A) and
  // the classifier fallback (Zone B) for an asymmetric shape.
  const G4double kHalfX              = 10.0 * mm;
  const G4double kHalfY              = 20.0 * mm;
  const G4double kHalfZ              = 30.0 * mm;
  const G4double kBoxInscribedRadius = 0.99 * kHalfX;
  // Zone B midpoint: halfway between the inscribed sphere boundary and the x-face
  // (must be inside the box: kBoxInscribedRadius < kNearXFaceRadius < kHalfX).
  const G4double kNearXFaceRadius = 0.5 * (kBoxInscribedRadius + kHalfX);
  const BoxFixture box("InscribedSphereBox", kHalfX, kHalfY, kHalfZ);
  ExpectInside("box deep interior (fast path) is inside", box.solid, G4ThreeVector(0.0, 0.0, 0.0),
               kInside);
  ExpectInside("box near x-face interior (classifier path) is inside", box.solid,
               G4ThreeVector(kNearXFaceRadius, 0.0, 0.0), kInside);
  ExpectInside("box near y-face interior (classifier path) is inside", box.solid,
               G4ThreeVector(0.0, kHalfY - 0.5 * mm, 0.0), kInside);
  ExpectInside("box near z-face interior (classifier path) is inside", box.solid,
               G4ThreeVector(0.0, 0.0, kHalfZ - 0.5 * mm), kInside);
  ExpectInside("box beyond x-face is outside", box.solid,
               G4ThreeVector(kHalfX + 5.0 * mm, 0.0, 0.0), kOutside);
}

} // namespace
