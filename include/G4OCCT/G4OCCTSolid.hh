// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

#ifndef G4OCCT_G4OCCTSolid_hh
#define G4OCCT_G4OCCTSolid_hh

#include <G4VSolid.hh>
#include <G4ThreeVector.hh>

// OCCT shape representation
#include <TopoDS_Shape.hxx>

/**
 * G4OCCTSolid
 *
 * Wraps an Open CASCADE Technology (OCCT) TopoDS_Shape as a Geant4 solid
 * (G4VSolid).  The OCCT shape is stored by value; once set it is used to
 * answer the Geant4 navigation queries (Inside, DistanceToIn, …).
 *
 * In OCCT the closest analogue to G4VSolid is TopoDS_Shape, which is the
 * root of the Boundary-Representation (BRep) topology hierarchy and can
 * describe any shape from a simple box to a complex multi-face shell.
 * The mapping is discussed in detail in docs/geometry_mapping.md.
 *
 * NOTE: All navigation methods currently contain stub implementations.
 *       Full OCCT-based implementations are planned for a future milestone.
 */
class G4OCCTSolid : public G4VSolid {
 public:
  /// Construct with a Geant4 solid name and an OCCT shape.
  G4OCCTSolid(const G4String& name, const TopoDS_Shape& shape);

  ~G4OCCTSolid() override = default;

  // ── G4VSolid pure-virtual interface ───────────────────────────────────────

  /// Return kInside, kSurface, or kOutside for point @p p.
  EInside Inside(const G4ThreeVector& p) const override;

  /// Return the outward unit normal at surface point @p p.
  G4ThreeVector SurfaceNormal(const G4ThreeVector& p) const override;

  /// Distance from external point @p p along direction @p v to solid surface.
  G4double DistanceToIn(const G4ThreeVector& p,
                        const G4ThreeVector& v) const override;

  /// Shortest distance from external point @p p to the solid surface.
  G4double DistanceToIn(const G4ThreeVector& p) const override;

  /// Distance from internal point @p p along direction @p v to solid surface.
  G4double DistanceToOut(const G4ThreeVector& p,
                         const G4ThreeVector& v,
                         const G4bool calcNorm = false,
                         G4bool* validNorm = nullptr,
                         G4ThreeVector* n = nullptr) const override;

  /// Shortest distance from internal point @p p to the solid surface.
  G4double DistanceToOut(const G4ThreeVector& p) const override;

  /// Return a string identifying the entity type.
  G4GeometryType GetEntityType() const override;

  /// Return the axis-aligned bounding box extent.
  G4VisExtent GetExtent() const override;

  /// Create a polyhedron representation for visualisation.
  G4Polyhedron* CreatePolyhedron() const override;

  /// Stream a human-readable description.
  std::ostream& StreamInfo(std::ostream& os) const override;

  // ── G4OCCTSolid-specific interface ────────────────────────────────────────

  /// Read access to the underlying OCCT shape.
  const TopoDS_Shape& GetOCCTShape() const { return fShape; }

  /// Replace the underlying OCCT shape.
  void SetOCCTShape(const TopoDS_Shape& shape) { fShape = shape; }

 private:
  TopoDS_Shape fShape;
};

#endif  // G4OCCT_G4OCCTSolid_hh
