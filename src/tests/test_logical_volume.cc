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

#include <gtest/gtest.h>

TEST(LogicalVolume, WithOCCTSolid) {
  // Create an OCCT box shape
  TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();

  // Build the G4VSolid using G4OCCTSolid
  auto* solid = new G4OCCTSolid("BoxSolid", box);

  // Use Geant4 NIST manager for a material
  G4Material* mat = G4NistManager::Instance()->FindOrBuildMaterial("G4_Fe");

  G4OCCTLogicalVolume lv(solid, mat, "BoxLV", box);

  EXPECT_EQ(lv.GetName(), "BoxLV");
  EXPECT_EQ(lv.GetSolid(), solid);
  EXPECT_EQ(lv.GetMaterial(), mat);
  EXPECT_FALSE(lv.GetOCCTShape().IsNull());
}

TEST(LogicalVolume, DefaultShape) {
  G4Box* g4box    = new G4Box("G4Box", 5.0, 5.0, 5.0);
  G4Material* mat = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");

  // No OCCT shape provided – defaults to null TopoDS_Shape
  G4OCCTLogicalVolume lv(g4box, mat, "DefaultShapeLV");

  EXPECT_TRUE(lv.GetOCCTShape().IsNull());
  EXPECT_EQ(lv.GetName(), "DefaultShapeLV");
}

TEST(LogicalVolume, SetShape) {
  G4Box* g4box    = new G4Box("G4Box2", 5.0, 5.0, 5.0);
  G4Material* mat = G4NistManager::Instance()->FindOrBuildMaterial("G4_AIR");
  G4OCCTLogicalVolume lv(g4box, mat, "SetShapeLV");

  EXPECT_TRUE(lv.GetOCCTShape().IsNull());

  TopoDS_Shape smallBox = BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape();
  lv.SetOCCTShape(smallBox);

  EXPECT_FALSE(lv.GetOCCTShape().IsNull());
}
