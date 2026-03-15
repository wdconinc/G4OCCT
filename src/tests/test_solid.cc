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

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

// ── helpers ───────────────────────────────────────────────────────────────────

static void check(bool condition, const char* msg) {
  if (!condition) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
  }
  std::cout << "PASS: " << msg << "\n";
}

// ── tests ─────────────────────────────────────────────────────────────────────

static void test_box_solid() {
  TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  G4OCCTSolid solid("TestBox", box);

  check(solid.GetName() == "TestBox", "solid name is 'TestBox'");
  check(solid.GetEntityType() == "G4OCCTSolid", "entity type is G4OCCTSolid");
  check(!solid.GetOCCTShape().IsNull(), "OCCT shape is not null");

  check(solid.Inside(G4ThreeVector(5.0, 10.0, 15.0)) == kInside,
        "Inside returns kInside for an interior point");
  check(solid.Inside(G4ThreeVector(0.0, 0.0, 0.0)) == kSurface,
        "Inside returns kSurface for a corner point on the box");

  check(std::abs(solid.DistanceToIn(G4ThreeVector(100.0, 0.0, 0.0)) - 90.0) < 1.0e-9,
        "DistanceToIn(p) returns the shortest distance for an outside point");

  check(std::abs(solid.DistanceToOut(G4ThreeVector(5.0, 10.0, 15.0)) - 5.0) < 1.0e-9,
        "DistanceToOut(p) returns the nearest face distance for an interior point");
}

static void test_sphere_solid() {
  TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(50.0).Shape();
  G4OCCTSolid solid("TestSphere", sphere);

  check(!solid.GetOCCTShape().IsNull(), "sphere shape is not null");
  check(solid.GetEntityType() == "G4OCCTSolid", "sphere entity type is G4OCCTSolid");
}

static void test_cylinder_solid() {
  TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(25.0, 100.0).Shape();
  G4OCCTSolid solid("TestCylinder", cyl);

  check(!solid.GetOCCTShape().IsNull(), "cylinder shape is not null");

  // Replace shape via setter
  TopoDS_Shape cyl2 = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();
  solid.SetOCCTShape(cyl2);
  check(!solid.GetOCCTShape().IsNull(), "replaced cylinder shape is not null");
}

static void test_set_occt_shape_invalidates_cache() {
  // Build a 10×10×10 box centred at the origin.
  TopoDS_Shape box1 =
      BRepPrimAPI_MakeBox(gp_Pnt(-5.0, -5.0, -5.0), gp_Pnt(5.0, 5.0, 5.0)).Shape();
  G4OCCTSolid solid("CacheInvalidateTest", box1);

  // Warm the per-thread classifier cache.
  const G4ThreeVector originPt(0.0, 0.0, 0.0);
  check(solid.Inside(originPt) == kInside,
        "cache-invalidate: Inside() kInside for box1 centre before SetOCCTShape");

  // Replace with a different box that does not contain the original test point.
  TopoDS_Shape box2 =
      BRepPrimAPI_MakeBox(gp_Pnt(20.0, 20.0, 20.0), gp_Pnt(30.0, 30.0, 30.0)).Shape();
  solid.SetOCCTShape(box2);

  // After shape replacement the original point must be kOutside.
  check(solid.Inside(originPt) == kOutside,
        "cache-invalidate: Inside() kOutside after SetOCCTShape (cache must reload)");

  // A point inside the new box must be kInside.
  check(solid.Inside(G4ThreeVector(25.0, 25.0, 25.0)) == kInside,
        "cache-invalidate: Inside() kInside for new box centre after SetOCCTShape");

  // DistanceToIn(p) must also reflect the new shape.
  // Nearest corner of the new box to origin is (20,20,20): distance = 20*sqrt(3) ~= 34.64
  const G4double expectedDist = 20.0 * std::sqrt(3.0);
  const G4double dist         = solid.DistanceToIn(originPt);
  check(std::abs(dist - expectedDist) < 0.5,
        "cache-invalidate: DistanceToIn(p) reflects new shape after SetOCCTShape");
}

static void test_surface_normal_box_face() {
  TopoDS_Shape box =
      BRepPrimAPI_MakeBox(gp_Pnt(-10.0, -10.0, -10.0), gp_Pnt(10.0, 10.0, 10.0)).Shape();
  G4OCCTSolid solid("NormalTest", box);

  G4ThreeVector n = solid.SurfaceNormal(G4ThreeVector(10.0, 0.0, 0.0));
  check((n - G4ThreeVector(1.0, 0.0, 0.0)).mag() < 1.0e-9,
        "SurfaceNormal returns +x on centered box face");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
  test_box_solid();
  test_sphere_solid();
  test_cylinder_solid();
  test_set_occt_shape_invalidates_cache();
  test_surface_normal_box_face();

  std::cout << "\nAll test_solid tests passed.\n";
  return 0;
}
