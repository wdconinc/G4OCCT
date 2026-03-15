// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file DetectorConstruction.cc
/// @brief Implementation of the B1 detector construction with STEP-based geometry.

#include "DetectorConstruction.hh"

#include "G4OCCT/G4OCCTSolid.hh"

#include <G4Box.hh>
#include <G4LogicalVolume.hh>
#include <G4NistManager.hh>
#include <G4PVPlacement.hh>
#include <G4SystemOfUnits.hh>

#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <TopoDS_Shape.hxx>

#include <stdexcept>
#include <string>

namespace {

/// Load the first shape from a STEP file at @p path.
/// Throws std::runtime_error on failure.
TopoDS_Shape LoadStepFile(const std::string& path) {
  STEPControl_Reader reader;
  if (reader.ReadFile(path.c_str()) != IFSelect_RetDone) {
    throw std::runtime_error("Failed to read STEP file: " + path);
  }
  if (reader.TransferRoots() <= 0) {
    throw std::runtime_error("No STEP roots transferred from: " + path);
  }
  TopoDS_Shape shape = reader.OneShape();
  if (shape.IsNull()) {
    throw std::runtime_error("Null shape loaded from STEP file: " + path);
  }
  return shape;
}

} // namespace

namespace B1 {

G4VPhysicalVolume* DetectorConstruction::Construct() {
  G4NistManager* nist = G4NistManager::Instance();

  // ── Materials ─────────────────────────────────────────────────────────────

  G4Material* matAir    = nist->FindOrBuildMaterial("G4_AIR");
  G4Material* matWater  = nist->FindOrBuildMaterial("G4_WATER");
  G4Material* matShape1 = nist->FindOrBuildMaterial("G4_A-150_TISSUE");
  G4Material* matShape2 = nist->FindOrBuildMaterial("G4_BONE_COMPACT_ICRU");

  // ── Envelope and world dimensions ─────────────────────────────────────────

  const G4double envSizeXY = 20.0 * cm;
  const G4double envSizeZ  = 30.0 * cm;

  // ── World ─────────────────────────────────────────────────────────────────

  auto* worldSolid = new G4Box("World", 0.6 * envSizeXY, 0.6 * envSizeXY, 0.8 * envSizeZ);
  auto* worldLV    = new G4LogicalVolume(worldSolid, matAir, "World");
  auto* worldPV =
      new G4PVPlacement(nullptr, G4ThreeVector(), worldLV, "World", nullptr, false, 0, true);

  // ── Envelope ─────────────────────────────────────────────────────────────

  auto* envSolid = new G4Box("Envelope", 0.5 * envSizeXY, 0.5 * envSizeXY, 0.5 * envSizeZ);
  auto* envLV    = new G4LogicalVolume(envSolid, matWater, "Envelope");
  new G4PVPlacement(nullptr, G4ThreeVector(), envLV, "Envelope", worldLV, false, 0, true);

  // ── Shape 1 — sphere (r = 15 mm) from STEP ───────────────────────────────
  // Material: G4_A-150_TISSUE, placed in the upstream half of the envelope.

  const std::string stepDir = G4OCCT_B1_STEP_DIR;
  TopoDS_Shape occtShape1   = LoadStepFile(stepDir + "/shape1.step");
  auto* solid1              = new G4OCCTSolid("Shape1", occtShape1);
  auto* lv1                 = new G4LogicalVolume(solid1, matShape1, "Shape1");
  new G4PVPlacement(nullptr, G4ThreeVector(0, 2.0 * cm, -7.0 * cm), lv1, "Shape1", envLV, false, 0,
                    true);

  // ── Shape 2 — box (20 × 30 × 40 mm) from STEP ────────────────────────────
  // Material: G4_BONE_COMPACT_ICRU, placed in the downstream half.
  // The OCCT box has its corner at the STEP origin (0,0,0); the placement
  // point below is therefore the corner of the box, not its centre.

  TopoDS_Shape occtShape2 = LoadStepFile(stepDir + "/shape2.step");
  auto* solid2            = new G4OCCTSolid("Shape2", occtShape2);
  auto* lv2               = new G4LogicalVolume(solid2, matShape2, "Shape2");
  new G4PVPlacement(nullptr, G4ThreeVector(-1.0 * cm, -1.5 * cm, 5.0 * cm), lv2, "Shape2", envLV,
                    false, 0, true);

  fScoringVolume = lv2;
  return worldPV;
}

} // namespace B1
