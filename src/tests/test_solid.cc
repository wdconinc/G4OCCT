// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

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
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

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
  using std::numbers::pi;
  const G4double r        = 50.0;
  const G4double expected = (4.0 / 3.0) * pi * r * r * r;
  TopoDS_Shape sphere     = BRepPrimAPI_MakeSphere(r).Shape();
  G4OCCTSolid solid("VolumeSphere", sphere);

  EXPECT_NEAR(solid.GetCubicVolume(), expected, expected * 1.0e-10);
}

TEST(SolidBasicAPI, GetSurfaceAreaSphere) {
  // Sphere of radius 50 mm: surface area = 4*pi*50^2
  using std::numbers::pi;
  const G4double r        = 50.0;
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
  // With analytical surface projection the returned point lies directly on the
  // sphere surface.  A tolerance of 1e-6 mm is a tight bound on the residual
  // numerical error from GeomAPI_ProjectPointOnSurf for a simple sphere.
  constexpr G4double kTol = 1.0e-6;

  for (int i = 0; i < kSamples; ++i) {
    const G4ThreeVector pt = solid.GetPointOnSurface();
    const G4double r       = pt.mag();
    EXPECT_NEAR(r, kRadius, kTol) << "Point radius deviates from sphere radius at sample " << i;

    // Regression: prior to the projection fix, tessellation chord midpoints
    // were returned which classified as kInside.  Verify the projected point
    // is classified as kSurface (not kInside) by the solid classifier.
    const EInside inside = solid.Inside(pt);
    EXPECT_EQ(inside, kSurface) << "GetPointOnSurface returned non-surface point at sample " << i;
  }
}

// ── G4OCCTSolid::FromSTEP error paths ─────────────────────────────────────────

TEST(SolidBasicAPI, FromSTEPInvalidPathThrows) {
  // ReadFile() returns != IFSelect_RetDone for a non-existent path.
  EXPECT_THROW(G4OCCTSolid::FromSTEP("Test", "/nonexistent/path/file.step"), std::runtime_error);
}

TEST(SolidBasicAPI, FromSTEPEmptyFileThrows) {
  // A STEP file that contains a valid ISO-10303-21 header but an empty DATA
  // section has no geometry roots.  Depending on the OCCT version, ReadFile()
  // may fail (IFSelect_RetDone not returned) or TransferRoots() returns 0.
  // Either way G4OCCTSolid::FromSTEP must throw std::runtime_error.
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "g4occt_test_empty.step";
  {
    std::ofstream f(path);
    f << "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\nENDSEC;\nEND-ISO-10303-21;\n";
  }
  EXPECT_THROW(G4OCCTSolid::FromSTEP("EmptyTest", path.string()), std::runtime_error);
  std::filesystem::remove(path);
}

TEST(SolidInvariant, ConstructorRejectsNullShape) {
  // The G4OCCTSolid invariant: fShape is never null.  Passing a default-
  // constructed (null) TopoDS_Shape must throw std::invalid_argument.
  EXPECT_THROW(G4OCCTSolid("NullShapeSolid", TopoDS_Shape{}), std::invalid_argument);
}

TEST(SolidInvariant, SetOCCTShapeRejectsNullShape) {
  // SetOCCTShape() must also enforce the non-null invariant.
  TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  G4OCCTSolid solid("BoxForSetShapeTest", box);
  EXPECT_THROW(solid.SetOCCTShape(TopoDS_Shape{}), std::invalid_argument);
  // The original shape must be unchanged after a rejected update.
  EXPECT_FALSE(solid.GetOCCTShape().IsNull());
  EXPECT_TRUE(solid.GetOCCTShape().IsEqual(box));
}

// ── Inscribed-sphere fast-path boundary tests ─────────────────────────────────
// The Inside() method uses an inscribed-sphere cache for a fast-path: if a
// query point lies strictly inside a cached sphere (shrunk by IntersectionTolerance),
// kInside is returned immediately without invoking the OCCT classifier.
//
// Regression guard: the old implementation used a closed-ball check (<=), which
// could classify points exactly on a cached sphere boundary as kInside even when
// those points are on the solid surface.  The corrected open-ball check uses
// (radius - tolerance) so boundary-adjacent points fall through to the OCCT
// classifier and receive the correct kSurface classification.
//
// For a 10×10×10 axis-aligned cube [0,10]³:
//   - AABB centre is (5,5,5).
//   - The largest construction-time inscribed sphere is centred at (5,5,5)
//     with radius 5 (distance from centre to each face).
//   - The face-centre points (0,5,5), (10,5,5), (5,0,5), (5,10,5), (5,5,0),
//     (5,5,10) lie exactly on this sphere's boundary.
//   - With the old closed-ball check those points were returned as kInside
//     (incorrect); with the corrected open-ball check they fall through to
//     the OCCT classifier which returns kSurface (correct).

TEST(SolidInscribedSphereFastPath, FaceCentrePointsAreKSurface) {
  // Build a 10×10×10 cube with one corner at the origin.
  TopoDS_Shape cube = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
  G4OCCTSolid solid("CubeFaceTest", cube);

  // The six face-centre points must be classified as kSurface.
  // These lie exactly on the boundary of the initial inscribed sphere
  // (centre (5,5,5), radius 5), so they expose the closed-vs-open-ball bug.
  EXPECT_EQ(solid.Inside(G4ThreeVector(0.0, 5.0, 5.0)), kSurface) << "-x face centre";
  EXPECT_EQ(solid.Inside(G4ThreeVector(10.0, 5.0, 5.0)), kSurface) << "+x face centre";
  EXPECT_EQ(solid.Inside(G4ThreeVector(5.0, 0.0, 5.0)), kSurface) << "-y face centre";
  EXPECT_EQ(solid.Inside(G4ThreeVector(5.0, 10.0, 5.0)), kSurface) << "+y face centre";
  EXPECT_EQ(solid.Inside(G4ThreeVector(5.0, 5.0, 0.0)), kSurface) << "-z face centre";
  EXPECT_EQ(solid.Inside(G4ThreeVector(5.0, 5.0, 10.0)), kSurface) << "+z face centre";
}

TEST(SolidInscribedSphereFastPath, InteriorPointIsKInside) {
  // Verify that the fast-path still works for a point well inside the solid
  // (the sphere fast-path must return kInside without falling through to OCCT).
  TopoDS_Shape cube = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
  G4OCCTSolid solid("CubeInteriorTest", cube);

  // The centroid (5,5,5) is inside the sphere and well inside the solid.
  EXPECT_EQ(solid.Inside(G4ThreeVector(5.0, 5.0, 5.0)), kInside) << "box centroid";
  // A point well inside a face is also interior.
  EXPECT_EQ(solid.Inside(G4ThreeVector(1.0, 5.0, 5.0)), kInside) << "point near face, inside";
}

TEST(SolidInscribedSphereFastPath, ExteriorPointIsKOutside) {
  TopoDS_Shape cube = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
  G4OCCTSolid solid("CubeExteriorTest", cube);

  EXPECT_EQ(solid.Inside(G4ThreeVector(-1.0, 5.0, 5.0)), kOutside) << "outside -x face";
  EXPECT_EQ(solid.Inside(G4ThreeVector(5.0, 15.0, 5.0)), kOutside) << "outside +y face";
}
