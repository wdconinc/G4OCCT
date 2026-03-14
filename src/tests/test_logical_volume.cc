// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

// test_logical_volume.cc
// Tests for G4OCCTLogicalVolume: verify construction with an OCCT shape and
// that the G4LogicalVolume base is properly initialised.

#include "G4OCCT/G4OCCTSolid.hh"
#include "G4OCCT/G4OCCTLogicalVolume.hh"

#include <G4Box.hh>
#include <G4NistManager.hh>

// OCCT
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopoDS_Shape.hxx>

#include <cassert>
#include <iostream>

static void check(bool condition, const char* msg) {
  if (!condition) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
  }
  std::cout << "PASS: " << msg << "\n";
}

static void test_occt_logical_volume_with_occt_solid() {
  // Create an OCCT box shape
  TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();

  // Build the G4VSolid using G4OCCTSolid
  auto* solid = new G4OCCTSolid("BoxSolid", box);

  // Use Geant4 NIST manager for a material
  G4Material* mat = G4NistManager::Instance()->FindOrBuildMaterial("G4_Fe");

  G4OCCTLogicalVolume lv(solid, mat, "BoxLV", box);

  check(lv.GetName() == "BoxLV", "logical volume name is 'BoxLV'");
  check(lv.GetSolid() == solid, "logical volume solid pointer matches");
  check(lv.GetMaterial() == mat, "logical volume material matches");
  check(!lv.GetOCCTShape().IsNull(), "OCCT shape in logical volume is not null");
}

static void test_occt_logical_volume_default_shape() {
  G4Box* g4box = new G4Box("G4Box", 5.0, 5.0, 5.0);
  G4Material* mat = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");

  // No OCCT shape provided – defaults to null TopoDS_Shape
  G4OCCTLogicalVolume lv(g4box, mat, "DefaultShapeLV");

  check(lv.GetOCCTShape().IsNull(), "default OCCT shape is null");
  check(lv.GetName() == "DefaultShapeLV", "default logical volume name matches");
}

static void test_occt_logical_volume_set_shape() {
  G4Box* g4box = new G4Box("G4Box2", 5.0, 5.0, 5.0);
  G4Material* mat = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
  G4OCCTLogicalVolume lv(g4box, mat, "SetShapeLV");

  check(lv.GetOCCTShape().IsNull(), "shape starts null");

  TopoDS_Shape smallBox = BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape();
  lv.SetOCCTShape(smallBox);

  check(!lv.GetOCCTShape().IsNull(), "shape is non-null after SetOCCTShape");
}

int main() {
  test_occt_logical_volume_with_occt_solid();
  test_occt_logical_volume_default_shape();
  test_occt_logical_volume_set_shape();

  std::cout << "\nAll test_logical_volume tests passed.\n";
  return 0;
}
