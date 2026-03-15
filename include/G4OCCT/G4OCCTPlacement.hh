// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file G4OCCTPlacement.hh
/// @brief Declaration of G4OCCTPlacement.

#ifndef G4OCCT_G4OCCTPlacement_hh
#define G4OCCT_G4OCCTPlacement_hh

#include <G4PVPlacement.hh>

// OCCT
#include <TopLoc_Location.hxx>

/**
 * @brief Extends Geant4's G4PVPlacement with the corresponding OCCT placement.
 *
 * Extends Geant4's G4PVPlacement to carry the corresponding OCCT placement
 * (TopLoc_Location), enabling round-trip translation between the two geometry
 * frameworks.
 *
 * In OCCT, placement information is encoded in TopLoc_Location objects that
 * are attached to TopoDS_Shape instances.  A TopLoc_Location wraps a
 * gp_Trsf (4×3 homogeneous transformation), which may be a pure rotation,
 * translation, or a general rigid-body motion—closely matching the rotation
 * (G4RotationMatrix*) and translation (G4ThreeVector) pair stored in a
 * Geant4 physical volume.
 *
 * The correspondence between the two representations is:
 *   G4PVPlacement / G4VPhysicalVolume  ↔  TopoDS_Shape with TopLoc_Location
 *   G4RotationMatrix + G4ThreeVector   ↔  gp_Trsf (stored in TopLoc_Location)
 *
 * Design strategies for efficiently converting between these representations
 * are discussed in docs/geometry_mapping.md.
 *
 * NOTE: The OCCT location stored here is informational only; conversion
 *       helpers are planned for a future milestone.
 */
class G4OCCTPlacement : public G4PVPlacement {
public:
  /**
   * Construct a placement using a rotation matrix and translation.
   *
   * @param pRot          Rotation relative to the mother volume (may be
   *                      nullptr for identity).
   * @param tlate         Translation relative to the mother volume.
   * @param pCurrentLogical Logical volume being placed.
   * @param pName         Name of this physical volume.
   * @param pMotherLogical Mother logical volume (nullptr for world).
   * @param pMany         Overlapping flag (must be false for now).
   * @param pCopyNo       Copy number.
   * @param location      Corresponding OCCT placement location (optional).
   * @param pSurfChk      Run overlap check during construction if true.
   */
  G4OCCTPlacement(G4RotationMatrix* pRot, const G4ThreeVector& tlate,
                  G4LogicalVolume* pCurrentLogical, const G4String& pName,
                  G4LogicalVolume* pMotherLogical, G4bool pMany, G4int pCopyNo,
                  const TopLoc_Location& location = TopLoc_Location(), G4bool pSurfChk = false);

  ~G4OCCTPlacement() override = default;

  // ── G4OCCTPlacement-specific interface ────────────────────────────────────

  /// Read access to the OCCT placement location.
  const TopLoc_Location& GetOCCTLocation() const { return fLocation; }

  /// Replace the OCCT placement location.
  void SetOCCTLocation(const TopLoc_Location& location) { fLocation = location; }

private:
  TopLoc_Location fLocation;
};

#endif // G4OCCT_G4OCCTPlacement_hh
