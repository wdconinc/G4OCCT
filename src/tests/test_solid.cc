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

#include <cmath>

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
