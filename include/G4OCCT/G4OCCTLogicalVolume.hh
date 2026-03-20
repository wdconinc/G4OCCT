// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTLogicalVolume.hh
/// @brief Declaration of G4OCCTLogicalVolume.

#ifndef G4OCCT_G4OCCTLogicalVolume_hh
#define G4OCCT_G4OCCTLogicalVolume_hh

#include <G4LogicalVolume.hh>

// OCCT
#include <TopoDS_Shape.hxx>

/**
 * @brief Extends Geant4's G4LogicalVolume with an associated OCCT shape.
 *
 * Extends Geant4's G4LogicalVolume to carry an optional OCCT shape reference
 * alongside the standard Geant4 solid, material, and field pointers.
 *
 * In the OCCT framework, a "logical volume" has no direct equivalent.  The
 * closest concept is a named compound or sub-assembly in a TopoDS_Compound
 * tree: a node that groups child shapes under a common identity (name,
 * material attributes, visual style) without a concrete spatial location.
 * XDE (Extended Data Exchange via TDocStd_Document / XCAFDoc) provides the
 * XDE label mechanism that can store shape, name, colour, material, and
 * placement information in a structured document tree, which is the OCCT
 * counterpart to the Geant4 volume hierarchy.
 *
 * Design strategies for connecting these representations are discussed in
 * docs/geometry_mapping.md.
 *
 * NOTE: The OCCT shape stored here is informational only; it is not used
 *       during navigation in this initial implementation.
 */
class G4OCCTLogicalVolume : public G4LogicalVolume {
public:
  /**
   * Construct a logical volume.
   *
   * @param pSolid    Pointer to the associated Geant4 solid (may be a
   *                  G4OCCTSolid wrapping an OCCT shape).
   * @param pMaterial Pointer to the material filling this volume.
   * @param name      Name of this logical volume.
   * @param shape     OCCT shape associated with this volume (optional).
   */
  G4OCCTLogicalVolume(G4VSolid* pSolid, G4Material* pMaterial, const G4String& name,
                      const TopoDS_Shape& shape = TopoDS_Shape());

  ~G4OCCTLogicalVolume() override = default;

  // ── G4OCCTLogicalVolume-specific interface ────────────────────────────────

  /// Read access to the associated OCCT shape.
  const TopoDS_Shape& GetOCCTShape() const { return fShape; }

  /// Replace the associated OCCT shape.
  void SetOCCTShape(const TopoDS_Shape& shape) { fShape = shape; }

private:
  TopoDS_Shape fShape;
};

#endif // G4OCCT_G4OCCTLogicalVolume_hh
