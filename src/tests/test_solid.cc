// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

// test_solid.cc
// Tests for G4OCCTSolid: verify that the basic APIs compile and return
// expected values for implemented navigation methods.

#include "G4OCCT/G4OCCTSolid.hh"

// OCCT primitives
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <gtest/gtest.h>

#include <numbers>

// ── tests ─────────────────────────────────────────────────────────────────────

TEST(SolidBasicAPI, BoxSolid) {
  TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  G4OCCTSolid solid("TestBox", box);

  EXPECT_EQ(solid.GetName(), "TestBox");
  EXPECT_EQ(solid.GetEntityType(), "G4OCCTSolid");
  EXPECT_FALSE(solid.GetOCCTShape().IsNull());

  EXPECT_EQ(solid.Inside(G4ThreeVector(5.0, 10.0, 15.0)), kInside);
  EXPECT_EQ(solid.Inside(G4ThreeVector(0.0, 0.0, 0.0)), kSurface);

  EXPECT_NEAR(solid.DistanceToIn(G4ThreeVector(100.0, 0.0, 0.0)), 90.0, 1.0e-9);
  EXPECT_NEAR(solid.DistanceToOut(G4ThreeVector(5.0, 10.0, 15.0)), 5.0, 1.0e-9);
}

TEST(SolidBasicAPI, SphereSolid) {
  TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(50.0).Shape();
  G4OCCTSolid solid("TestSphere", sphere);

  EXPECT_FALSE(solid.GetOCCTShape().IsNull());
  EXPECT_EQ(solid.GetEntityType(), "G4OCCTSolid");
}

TEST(SolidBasicAPI, CylinderSolid) {
  TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(25.0, 100.0).Shape();
  G4OCCTSolid solid("TestCylinder", cyl);

  EXPECT_FALSE(solid.GetOCCTShape().IsNull());

  // Replace shape via setter
  TopoDS_Shape cyl2 = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();
  solid.SetOCCTShape(cyl2);
  EXPECT_FALSE(solid.GetOCCTShape().IsNull());
}

TEST(SolidBasicAPI, SetOCCTShapeInvalidatesCache) {
  // Build a 10×10×10 box centred at the origin.
  TopoDS_Shape box1 = BRepPrimAPI_MakeBox(gp_Pnt(-5.0, -5.0, -5.0), gp_Pnt(5.0, 5.0, 5.0)).Shape();
  G4OCCTSolid solid("CacheInvalidateTest", box1);

  // Warm the per-thread classifier cache.
  const G4ThreeVector originPt(0.0, 0.0, 0.0);
  EXPECT_EQ(solid.Inside(originPt), kInside);

  // Replace with a different box that does not contain the original test point.
  TopoDS_Shape box2 =
      BRepPrimAPI_MakeBox(gp_Pnt(20.0, 20.0, 20.0), gp_Pnt(30.0, 30.0, 30.0)).Shape();
  solid.SetOCCTShape(box2);

  // After shape replacement the original point must be kOutside.
  EXPECT_EQ(solid.Inside(originPt), kOutside);

  // A point inside the new box must be kInside.
  EXPECT_EQ(solid.Inside(G4ThreeVector(25.0, 25.0, 25.0)), kInside);

  // DistanceToIn(p) must also reflect the new shape.
  // Nearest corner of the new box to origin is (20,20,20): distance = 20*sqrt(3) ~= 34.64
  const G4double expectedDist = 20.0 * std::sqrt(3.0);
  const G4double dist         = solid.DistanceToIn(originPt);
  EXPECT_NEAR(dist, expectedDist, 0.5);
}

TEST(SolidBasicAPI, SurfaceNormalBoxFace) {
  TopoDS_Shape box =
      BRepPrimAPI_MakeBox(gp_Pnt(-10.0, -10.0, -10.0), gp_Pnt(10.0, 10.0, 10.0)).Shape();
  G4OCCTSolid solid("NormalTest", box);

  G4ThreeVector n = solid.SurfaceNormal(G4ThreeVector(10.0, 0.0, 0.0));
  EXPECT_NEAR(n.x(), 1.0, 1.0e-9) << "SurfaceNormal +x component on centered box face";
  EXPECT_NEAR(n.y(), 0.0, 1.0e-9) << "SurfaceNormal +y component on centered box face";
  EXPECT_NEAR(n.z(), 0.0, 1.0e-9) << "SurfaceNormal +z component on centered box face";
}

TEST(SolidBasicAPI, GetCubicVolumeBox) {
  // 10 × 20 × 30 box: volume = 6000 mm³
  TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  G4OCCTSolid solid("VolumeBox", box);

  const G4double volume = solid.GetCubicVolume();
  EXPECT_NEAR(volume, 6000.0, 1.0e-6);

  // Second call must return the cached value.
  EXPECT_NEAR(solid.GetCubicVolume(), 6000.0, 1.0e-6);
}

TEST(SolidBasicAPI, GetSurfaceAreaBox) {
  // 10 × 20 × 30 box: surface area = 2*(10*20 + 10*30 + 20*30) = 2200 mm²
  TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  G4OCCTSolid solid("SurfaceAreaBox", box);

  const G4double area = solid.GetSurfaceArea();
  EXPECT_NEAR(area, 2200.0, 1.0e-6);

  // Second call must return the cached value.
  EXPECT_NEAR(solid.GetSurfaceArea(), 2200.0, 1.0e-6);
}

TEST(SolidBasicAPI, GetCubicVolumeSphere) {
  // Sphere of radius 50 mm: volume = (4/3)*pi*50^3
  const G4double r        = 50.0;
  const G4double pi       = std::numbers::pi;
  const G4double expected = (4.0 / 3.0) * pi * r * r * r;
  TopoDS_Shape sphere     = BRepPrimAPI_MakeSphere(r).Shape();
  G4OCCTSolid solid("VolumeSphere", sphere);

  EXPECT_NEAR(solid.GetCubicVolume(), expected, expected * 1.0e-10);
}

TEST(SolidBasicAPI, GetSurfaceAreaSphere) {
  // Sphere of radius 50 mm: surface area = 4*pi*50^2
  const G4double r        = 50.0;
  const G4double pi       = std::numbers::pi;
  const G4double expected = 4.0 * pi * r * r;
  TopoDS_Shape sphere     = BRepPrimAPI_MakeSphere(r).Shape();
  G4OCCTSolid solid("SurfaceSphere", sphere);

  EXPECT_NEAR(solid.GetSurfaceArea(), expected, expected * 1.0e-10);
}

TEST(SolidBasicAPI, VolumeAreaCacheInvalidatedBySetOCCTShape) {
  // Start with a 10³ box.
  TopoDS_Shape box1 = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
  G4OCCTSolid solid("CacheVol", box1);

  const G4double vol1 = solid.GetCubicVolume();
  EXPECT_NEAR(vol1, 1000.0, 1.0e-6);

  // Replace with a 20³ box and verify the cache is invalidated.
  TopoDS_Shape box2 = BRepPrimAPI_MakeBox(20.0, 20.0, 20.0).Shape();
  solid.SetOCCTShape(box2);

  const G4double vol2 = solid.GetCubicVolume();
  EXPECT_NEAR(vol2, 8000.0, 1.0e-6);
}

TEST(SolidBasicAPI, GetPointOnSurfaceBox) {
  // A 20×20×20 axis-aligned box centred at the origin.
  TopoDS_Shape box =
      BRepPrimAPI_MakeBox(gp_Pnt(-10.0, -10.0, -10.0), gp_Pnt(10.0, 10.0, 10.0)).Shape();
  G4OCCTSolid solid("GetPointOnSurfaceBox", box);

  // Sample a number of points and verify each lies on (or very close to) the
  // surface.  A point is on the surface when exactly one coordinate equals
  // ±10 and the other two are within [-10, 10].
  constexpr int kSamples   = 100;
  constexpr G4double kHalf = 10.0;
  constexpr G4double kTol  = 1.0e-6;

  for (int i = 0; i < kSamples; ++i) {
    const G4ThreeVector pt = solid.GetPointOnSurface();

    // Each coordinate must be within the box bounds (with tolerance).
    EXPECT_GE(pt.x(), -kHalf - kTol) << "x below lower bound at sample " << i;
    EXPECT_LE(pt.x(), kHalf + kTol) << "x above upper bound at sample " << i;
    EXPECT_GE(pt.y(), -kHalf - kTol) << "y below lower bound at sample " << i;
    EXPECT_LE(pt.y(), kHalf + kTol) << "y above upper bound at sample " << i;
    EXPECT_GE(pt.z(), -kHalf - kTol) << "z below lower bound at sample " << i;
    EXPECT_LE(pt.z(), kHalf + kTol) << "z above upper bound at sample " << i;

    // The point must lie on a face: at least one coordinate must be at ±kHalf.
    const bool onFace = std::abs(std::abs(pt.x()) - kHalf) <= kTol ||
                        std::abs(std::abs(pt.y()) - kHalf) <= kTol ||
                        std::abs(std::abs(pt.z()) - kHalf) <= kTol;
    EXPECT_TRUE(onFace) << "Point " << pt << " is not on a box face at sample " << i;

    // Independently confirm with Inside(): the solid must classify the
    // returned point as kSurface or kInside (never strictly kOutside).
    const EInside inside = solid.Inside(pt);
    EXPECT_NE(inside, kOutside) << "GetPointOnSurface returned exterior point at sample " << i;
  }
}

TEST(SolidBasicAPI, GetPointOnSurfaceSphere) {
  // A sphere of radius 50.
  constexpr G4double kRadius = 50.0;
  TopoDS_Shape sphere        = BRepPrimAPI_MakeSphere(kRadius).Shape();
  G4OCCTSolid solid("GetPointOnSurfaceSphere", sphere);

  constexpr int kSamples = 100;
  // With a 1% relative deflection, the chord height (distance from a triangle
  // midpoint to the true curved surface) is bounded by the absolute deflection,
  // which for a sphere of radius 50 is at most ~1% of the face bounding-box
  // diagonal (~173 units), giving roughly 1.7 mm.  A tolerance of 2.0 is a
  // conservative upper bound to account for different tessellation algorithms.
  constexpr G4double kTol = 2.0;

  for (int i = 0; i < kSamples; ++i) {
    const G4ThreeVector pt = solid.GetPointOnSurface();
    const G4double r       = pt.mag();
    EXPECT_NEAR(r, kRadius, kTol) << "Point radius deviates from sphere radius at sample " << i;
  }
}
