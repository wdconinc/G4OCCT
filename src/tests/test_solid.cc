// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

// test_solid.cc
// Tests for G4OCCTSolid: verify that the stub implementations compile and
// return the expected sentinel values.

#include "G4OCCT/G4OCCTSolid.hh"

// OCCT primitives
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <TopoDS_Shape.hxx>

#include <cassert>
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

  // Stub: Inside returns kOutside for any point
  check(solid.Inside(G4ThreeVector(0, 0, 0)) == kOutside,
        "Inside returns kOutside (stub)");

  // Stub: DistanceToIn returns kInfinity
  check(solid.DistanceToIn(G4ThreeVector(100, 0, 0)) == kInfinity,
        "DistanceToIn(p) returns kInfinity (stub)");

  // Stub: DistanceToOut returns 0
  check(solid.DistanceToOut(G4ThreeVector(0, 0, 0)) == 0.0,
        "DistanceToOut(p) returns 0 (stub)");
}

static void test_sphere_solid() {
  TopoDS_Shape sphere = BRepPrimAPI_MakeSphere(50.0).Shape();
  G4OCCTSolid solid("TestSphere", sphere);

  check(!solid.GetOCCTShape().IsNull(), "sphere shape is not null");
  check(solid.GetEntityType() == "G4OCCTSolid",
        "sphere entity type is G4OCCTSolid");
}

static void test_cylinder_solid() {
  TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(25.0, 100.0).Shape();
  G4OCCTSolid solid("TestCylinder", cyl);

  check(!solid.GetOCCTShape().IsNull(), "cylinder shape is not null");

  // Replace shape via setter
  TopoDS_Shape cyl2 = BRepPrimAPI_MakeCylinder(5.0, 10.0).Shape();
  solid.SetOCCTShape(cyl2);
  check(!solid.GetOCCTShape().IsNull(),
        "replaced cylinder shape is not null");
}

static void test_surface_normal_stub() {
  TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
  G4OCCTSolid solid("NormalTest", box);

  G4ThreeVector n = solid.SurfaceNormal(G4ThreeVector(5, 0, 0));
  // Stub returns (0,0,1)
  check(n == G4ThreeVector(0, 0, 1), "SurfaceNormal stub returns (0,0,1)");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
  test_box_solid();
  test_sphere_solid();
  test_cylinder_solid();
  test_surface_normal_stub();

  std::cout << "\nAll test_solid tests passed.\n";
  return 0;
}
