// test_placement.cc
// Tests for G4OCCTPlacement: verify construction and that the OCCT location
// getter/setter work correctly.

#include "G4OCCT/G4OCCTSolid.hh"
#include "G4OCCT/G4OCCTLogicalVolume.hh"
#include "G4OCCT/G4OCCTPlacement.hh"

#include "G4NistManager.hh"

// OCCT
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>

#include <cassert>
#include <iostream>

static void check(bool condition, const char* msg) {
  if (!condition) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
  }
  std::cout << "PASS: " << msg << "\n";
}

static void test_placement_default_location() {
  // Materials
  G4Material* mat = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");

  // World volume
  TopoDS_Shape worldShape = BRepPrimAPI_MakeBox(2000.0, 2000.0, 2000.0).Shape();
  auto* worldSolid = new G4OCCTSolid("WorldSolid", worldShape);
  auto* worldLV    = new G4OCCTLogicalVolume(worldSolid, mat, "WorldLV");

  // Daughter volume
  TopoDS_Shape boxShape = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
  auto* boxSolid = new G4OCCTSolid("BoxSolid", boxShape);
  auto* boxLV    = new G4OCCTLogicalVolume(boxSolid, mat, "BoxLV");

  // Place with identity location (default)
  G4OCCTPlacement placement(
      nullptr, G4ThreeVector(0, 0, 0),
      boxLV, "BoxPV",
      worldLV, false, 0);

  check(placement.GetName() == "BoxPV", "placement name is 'BoxPV'");
  check(placement.GetCopyNo() == 0,     "copy number is 0");
  check(placement.GetOCCTLocation().IsIdentity(),
        "default OCCT location is identity");
}

static void test_placement_with_translation() {
  G4Material* mat = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");

  TopoDS_Shape worldShape = BRepPrimAPI_MakeBox(2000.0, 2000.0, 2000.0).Shape();
  auto* worldSolid = new G4OCCTSolid("WorldSolid2", worldShape);
  auto* worldLV    = new G4OCCTLogicalVolume(worldSolid, mat, "WorldLV2");

  TopoDS_Shape boxShape = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
  auto* boxSolid = new G4OCCTSolid("BoxSolid2", boxShape);
  auto* boxLV    = new G4OCCTLogicalVolume(boxSolid, mat, "BoxLV2");

  // Build an OCCT location with a 100 mm translation along X
  gp_Trsf trsf;
  trsf.SetTranslation(gp_Vec(100.0, 0.0, 0.0));
  TopLoc_Location loc(trsf);

  G4OCCTPlacement placement(
      nullptr, G4ThreeVector(100, 0, 0),
      boxLV, "BoxPV2",
      worldLV, false, 1,
      loc);

  check(!placement.GetOCCTLocation().IsIdentity(),
        "OCCT location is not identity after translation");
  check(placement.GetCopyNo() == 1, "copy number is 1");
}

static void test_placement_set_location() {
  G4Material* mat = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");

  TopoDS_Shape worldShape = BRepPrimAPI_MakeBox(2000.0, 2000.0, 2000.0).Shape();
  auto* worldSolid = new G4OCCTSolid("WorldSolid3", worldShape);
  auto* worldLV    = new G4OCCTLogicalVolume(worldSolid, mat, "WorldLV3");

  TopoDS_Shape boxShape = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
  auto* boxSolid = new G4OCCTSolid("BoxSolid3", boxShape);
  auto* boxLV    = new G4OCCTLogicalVolume(boxSolid, mat, "BoxLV3");

  G4OCCTPlacement placement(
      nullptr, G4ThreeVector(0, 0, 0),
      boxLV, "BoxPV3",
      worldLV, false, 0);

  check(placement.GetOCCTLocation().IsIdentity(),
        "location starts as identity");

  gp_Trsf trsf;
  trsf.SetTranslation(gp_Vec(0.0, 50.0, 0.0));
  placement.SetOCCTLocation(TopLoc_Location(trsf));

  check(!placement.GetOCCTLocation().IsIdentity(),
        "location is non-identity after SetOCCTLocation");
}

int main() {
  test_placement_default_location();
  test_placement_with_translation();
  test_placement_set_location();

  std::cout << "\nAll test_placement tests passed.\n";
  return 0;
}
