// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include "navigation_test_harness.hh"

#include <gtest/gtest.h>

#include <memory>

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

// ── Ray-parity corner cases ────────────────────────────────────────────────────
// These tests cover the degenerate-ray and onSurface-state edge cases that were
// identified in the ray-parity Inside() implementation review:
//
//   Fix 1 — onSurface state check:
//     IntCurvesFace_Intersector can return intersections with TopAbs_OUT when the
//     +Z ray crosses the underlying (infinite) surface of a face *outside* the
//     trimmed face boundary.  Only TopAbs_IN / TopAbs_ON intersections at |w| ≤ tol
//     should set onSurface.  Without the check a point outside the solid but on
//     the extended face plane would be incorrectly returned as kSurface.
//
//   Fix 2 — degenerate ray fallback on TopAbs_ON at w > tolerance:
//     When the +Z ray hits a shared edge or vertex (TopAbs_ON, w > tol), skipping
//     the crossing alters parity and can misclassify the point.  Any such hit now
//     sets a degenerateRay flag and causes a fallback to BRepClass3d_SolidClassifier.
//     This also covers the pre-existing crossings==0 fallback path (ray passes
//     entirely through edges/vertices).

// Fix 1 regression: a point that lies outside the solid but exactly on the z-plane
// of the +z face must NOT be returned as kSurface.
//
// Setup: cylinder of radius 10 and half-length 30.  The +z face is a circular
// disk at z=30.  Its AABB on that plane is the square [−10,10]×[−10,10], but the
// trimmed face only covers the circle x^2 + y^2 <= 10^2.
//
// Point (10,10,30) lies on the plane z=30 and inside the AABB, but outside the
// circular face (x^2 + y^2 = 200 > 100), so it is outside the solid while
// still exercising the extended-surface / TopAbs_OUT logic in the +Z ray.
// With Fix 1, TopAbs_OUT at |w|<=tol is ignored, so onSurface stays false;
// crossings=0 triggers the classifier fallback which returns kOutside.
TEST(InsideClassification, RayParityOnSurfaceStateCheck) {
  const CylinderFixture cyl("RayParityOnSurfaceCylinder", 10.0 * mm, 30.0 * mm);

  // Outside the solid: inside the AABB of the +z face but outside the circular face.
  ExpectInside("point outside +z circular face but inside AABB is kOutside", cyl.solid,
               G4ThreeVector(10.0 * mm, 10.0 * mm, 30.0 * mm), kOutside);
  // Sanity: face centre on the same plane is on the surface.
  ExpectInside("+z face centre is kSurface", cyl.solid, G4ThreeVector(0.0, 0.0, 30.0 * mm),
               kSurface);
}

// Fix 2 regression: points on box edges and vertices trigger TopAbs_ON hits in
// the +Z ray and must fall back to the classifier, which returns kSurface.
//
// For a box centred at the origin the twelve edges sit at the intersections of
// face planes.  A +Z ray cast from an edge midpoint hits the adjacent top-face
// edge at w > tolerance with fi.State() == TopAbs_ON (shared edge), raising the
// degenerateRay flag and causing the classifier fallback.
TEST(InsideClassification, RayParityDegenerateRayEdgeAndVertex) {
  const BoxFixture box("RayParityEdgeVertexBox", 10.0 * mm, 20.0 * mm, 30.0 * mm);

  // Midpoints on the four vertical edges (+x / +y, +x / -y, -x / +y, -x / -y)
  // at z=0 (mid-height).
  ExpectInside("+x/+y vertical edge midpoint is kSurface", box.solid,
               G4ThreeVector(10.0 * mm, 20.0 * mm, 0.0), kSurface);
  ExpectInside("+x/-y vertical edge midpoint is kSurface", box.solid,
               G4ThreeVector(10.0 * mm, -20.0 * mm, 0.0), kSurface);
  ExpectInside("-x/+y vertical edge midpoint is kSurface", box.solid,
               G4ThreeVector(-10.0 * mm, 20.0 * mm, 0.0), kSurface);
  ExpectInside("-x/-y vertical edge midpoint is kSurface", box.solid,
               G4ThreeVector(-10.0 * mm, -20.0 * mm, 0.0), kSurface);

  // Midpoints on the four top horizontal edges.
  ExpectInside("+z/+x top edge midpoint is kSurface", box.solid,
               G4ThreeVector(10.0 * mm, 0.0, 30.0 * mm), kSurface);
  ExpectInside("+z/-x top edge midpoint is kSurface", box.solid,
               G4ThreeVector(-10.0 * mm, 0.0, 30.0 * mm), kSurface);
  ExpectInside("+z/+y top edge midpoint is kSurface", box.solid,
               G4ThreeVector(0.0, 20.0 * mm, 30.0 * mm), kSurface);
  ExpectInside("+z/-y top edge midpoint is kSurface", box.solid,
               G4ThreeVector(0.0, -20.0 * mm, 30.0 * mm), kSurface);

  // Midpoints on the four bottom horizontal edges.
  ExpectInside("-z/+x bottom edge midpoint is kSurface", box.solid,
               G4ThreeVector(10.0 * mm, 0.0, -30.0 * mm), kSurface);
  ExpectInside("-z/-x bottom edge midpoint is kSurface", box.solid,
               G4ThreeVector(-10.0 * mm, 0.0, -30.0 * mm), kSurface);
  ExpectInside("-z/+y bottom edge midpoint is kSurface", box.solid,
               G4ThreeVector(0.0, 20.0 * mm, -30.0 * mm), kSurface);
  ExpectInside("-z/-y bottom edge midpoint is kSurface", box.solid,
               G4ThreeVector(0.0, -20.0 * mm, -30.0 * mm), kSurface);

  // All eight corners of the box must be kSurface.
  ExpectInside("corner (+x,+y,+z) is kSurface", box.solid,
               G4ThreeVector(10.0 * mm, 20.0 * mm, 30.0 * mm), kSurface);
  ExpectInside("corner (+x,-y,+z) is kSurface", box.solid,
               G4ThreeVector(10.0 * mm, -20.0 * mm, 30.0 * mm), kSurface);
  ExpectInside("corner (-x,+y,+z) is kSurface", box.solid,
               G4ThreeVector(-10.0 * mm, 20.0 * mm, 30.0 * mm), kSurface);
  ExpectInside("corner (-x,-y,+z) is kSurface", box.solid,
               G4ThreeVector(-10.0 * mm, -20.0 * mm, 30.0 * mm), kSurface);
  ExpectInside("corner (+x,+y,-z) is kSurface", box.solid,
               G4ThreeVector(10.0 * mm, 20.0 * mm, -30.0 * mm), kSurface);
  ExpectInside("corner (+x,-y,-z) is kSurface", box.solid,
               G4ThreeVector(10.0 * mm, -20.0 * mm, -30.0 * mm), kSurface);
  ExpectInside("corner (-x,+y,-z) is kSurface", box.solid,
               G4ThreeVector(-10.0 * mm, 20.0 * mm, -30.0 * mm), kSurface);
  ExpectInside("corner (-x,-y,-z) is kSurface", box.solid,
               G4ThreeVector(-10.0 * mm, -20.0 * mm, -30.0 * mm), kSurface);

  // Interior and exterior points must not be affected by the fallback.
  ExpectInside("box centre is kInside", box.solid, G4ThreeVector(0.0, 0.0, 0.0), kInside);
  ExpectInside("point beyond +x face is kOutside", box.solid, G4ThreeVector(15.0 * mm, 0.0, 0.0),
               kOutside);
}

// ── BVH TriangleRayCast investigation ─────────────────────────────────────────
//
// A. RejectNode slab-swap branch (if (t1 > t2) std::swap(t1, t2))
//    This branch fires when a ray direction component is negative (t1 > t2
//    means ck_min/dk > ck_max/dk, which requires dk < 0).  The three ray
//    directions used by Inside() are always +Z (primary), +X, and +Y — all
//    components are non-negative, so dk is never negative.  The swap branch
//    is therefore structurally unreachable with the current fixed positive ray
//    directions and cannot be triggered via the public API.
//
// B. TriangleRayCast::Accept myOnSurface path
//    myOnSurface is set to true when |t| <= myTolerance, i.e. the ray origin
//    lies on a tessellation triangle.  However, the BVH path is only entered
//    when BVHLowerBoundDistance(p) >= tolerance (points provably away from the
//    mesh surface).  Points on a tessellation triangle have BVHLowerBoundDistance
//    ≈ 0, so the guard condition redirects them to the OCCT exact classifier
//    before any BVH traversal occurs.  The myOnSurface path in the BVH cast is
//    therefore not reachable for geometric surface points through the current
//    Inside() implementation; no additional test is added.

// ── Curved-surface STEP fixture tests ─────────────────────────────────────────
// These tests load real STEP files generated from Geant4 primitive shapes and
// exercise Inside() on curved/boolean geometries to cover branches that the
// analytic-primitive fixtures (Box, Sphere, Cylinder) leave untouched.

TEST(InsideClassification, TorusSTEP) {
  // G4Torus: swept radius 20 mm, tube radius 5 mm.
  // Tube centre circle lies in the XY plane at distance 20 mm from the Z axis.
  std::unique_ptr<G4OCCTSolid> solid(G4OCCTSolid::FromSTEP(
      "TorusSTEP", "/home/wdconinc/git/G4OCCT/src/tests/fixtures/geometry/direct-primitives/"
                   "G4Torus/torus-rtor20-rmax5-v1/shape.step"));

  // Centre of the tube cross-section at (20, 0, 0): inside the torus material.
  ExpectInside("torus tube centre is inside", *solid, G4ThreeVector(20.0 * mm, 0.0, 0.0), kInside);
  // Origin — inside the central hole: outside the solid.
  ExpectInside("torus hole origin is outside", *solid, G4ThreeVector(0.0, 0.0, 0.0), kOutside);
  // Far exterior along X.
  ExpectInside("torus far exterior is outside", *solid, G4ThreeVector(100.0 * mm, 0.0, 0.0),
               kOutside);
}

TEST(InsideClassification, ConeSTEP) {
  // G4Cons: solid cone, outer radius 8 mm at z=−12, outer radius 3 mm at z=+12.
  // Centred at the origin (translated during generation).
  std::unique_ptr<G4OCCTSolid> solid(G4OCCTSolid::FromSTEP(
      "ConeSTEP", "/home/wdconinc/git/G4OCCT/src/tests/fixtures/geometry/direct-primitives/"
                  "G4Cons/cons-r8-r3-z24-v1/shape.step"));

  // Axis centre (0, 0, 0): inside the solid cone.
  ExpectInside("cone axis centre is inside", *solid, G4ThreeVector(0.0, 0.0, 0.0), kInside);
  // Far exterior.
  ExpectInside("cone far exterior is outside", *solid, G4ThreeVector(100.0 * mm, 0.0, 0.0),
               kOutside);
}

TEST(InsideClassification, BooleanUnionSTEP) {
  // G4UnionSolid: two overlapping 20×20×20 mm boxes fused into one body.
  // After translation, the union spans X ∈ [−10, 20] mm, Y/Z ∈ [−10, 10] mm.
  std::unique_ptr<G4OCCTSolid> solid(G4OCCTSolid::FromSTEP(
      "BooleanUnionSTEP", "/home/wdconinc/git/G4OCCT/src/tests/fixtures/geometry/boolean-compound/"
                          "G4UnionSolid/box-overlap-x10-v1/shape.step"));

  // Inside the first box region.
  ExpectInside("union first box centre is inside", *solid, G4ThreeVector(0.0, 0.0, 0.0), kInside);
  // Inside the second box region (past the overlap).
  ExpectInside("union second box region is inside", *solid, G4ThreeVector(8.0 * mm, 0.0, 0.0),
               kInside);
  // Well outside the union.
  ExpectInside("union far exterior is outside", *solid, G4ThreeVector(50.0 * mm, 0.0, 0.0),
               kOutside);
}

} // namespace
