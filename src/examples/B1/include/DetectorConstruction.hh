// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file DetectorConstruction.hh
/// @brief Detector construction for the G4OCCT B1 example.

#ifndef B1_DetectorConstruction_hh
#define B1_DetectorConstruction_hh

#include <G4LogicalVolume.hh>
#include <G4VUserDetectorConstruction.hh>

/**
 * @brief B1 detector construction loading shapes from STEP files.
 *
 * Builds an envelope box of water containing two shapes loaded from STEP files
 * via G4OCCTSolid. The scoring volume (shape2) pointer is exposed for use in
 * SteppingAction.
 *
 * Geometry layout:
 * - World: 24 cm × 24 cm × 48 cm box of G4_AIR
 * - Envelope: 20 cm × 20 cm × 30 cm box of G4_WATER
 * - Shape1: sphere (r = 15 mm) loaded from step/shape1.step, G4_A-150_TISSUE
 * - Shape2: box (20 × 30 × 40 mm) loaded from step/shape2.step, G4_BONE_COMPACT_ICRU
 *
 * The @p G4OCCT_B1_STEP_DIR compile definition must be set to the directory
 * containing the STEP files.
 */
class DetectorConstruction : public G4VUserDetectorConstruction {
 public:
  DetectorConstruction() = default;
  ~DetectorConstruction() override = default;

  G4VPhysicalVolume* Construct() override;

  /// Return a pointer to the scoring logical volume (shape2).
  G4LogicalVolume* GetScoringVolume() const { return fScoringVolume; }

 private:
  G4LogicalVolume* fScoringVolume = nullptr;
};

#endif  // B1_DetectorConstruction_hh
