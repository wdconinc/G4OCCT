// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file G4OCCTSolid.hh
/// @brief Declaration of G4OCCTSolid.

#ifndef G4OCCT_G4OCCTSolid_hh
#define G4OCCT_G4OCCTSolid_hh

#include <G4Cache.hh>
#include <G4ThreeVector.hh>
#include <G4VSolid.hh>

// OCCT shape representation
#include <BRepClass3d_SolidClassifier.hxx>
#include <TopoDS_Shape.hxx>

#include <optional>

/**
 * @brief Geant4 solid wrapping an Open CASCADE Technology (OCCT) TopoDS_Shape.
 *
 * Wraps an Open CASCADE Technology (OCCT) TopoDS_Shape as a Geant4 solid
 * (G4VSolid). The OCCT shape is stored by value and is queried directly for
 * Geant4 navigation, extent, and visualisation requests.
 *
 * In OCCT the closest analogue to G4VSolid is TopoDS_Shape, which is the root
 * of the Boundary-Representation topology hierarchy and can describe any shape
 * from a simple box to a complex multi-face shell. The mapping is discussed in
 * detail in docs/geometry_mapping.md.
 *
 * ## Thread safety and caching
 *
 * `G4OCCTSolid` is shared read-only across all Geant4 worker threads once
 * the geometry is constructed.  OCCT algorithm objects such as
 * `BRepClass3d_SolidClassifier` hold mutable internal state and must not be
 * shared between threads.  A per-thread cache of the classifier is maintained
 * via `G4Cache` so that the one-time O(N_faces) initialisation cost is paid
 * only once per thread rather than on every `Inside` or `DistanceToIn(p)` call.
 */
class G4OCCTSolid : public G4VSolid {
public:
  /**
   * Construct with a Geant4 solid name and an OCCT shape.
   *
   * @param name  Name registered with the Geant4 solid store.
   * @param shape OCCT boundary-representation shape to wrap.
   */
  G4OCCTSolid(const G4String& name, const TopoDS_Shape& shape);

  ~G4OCCTSolid() override = default;

  // ── G4VSolid pure-virtual interface ───────────────────────────────────────

  /// Return kInside, kSurface, or kOutside for point @p p.
  EInside Inside(const G4ThreeVector& p) const override;

  /// Return the outward unit normal at surface point @p p.
  G4ThreeVector SurfaceNormal(const G4ThreeVector& p) const override;

  /// Distance from external point @p p along direction @p v to solid surface.
  G4double DistanceToIn(const G4ThreeVector& p, const G4ThreeVector& v) const override;

  /// Shortest distance from external point @p p to the solid surface.
  G4double DistanceToIn(const G4ThreeVector& p) const override;

  /// Distance from internal point @p p along direction @p v to solid surface.
  G4double DistanceToOut(const G4ThreeVector& p, const G4ThreeVector& v,
                         const G4bool calcNorm = false, G4bool* validNorm = nullptr,
                         G4ThreeVector* n = nullptr) const override;

  /// Shortest distance from internal point @p p to the solid surface.
  G4double DistanceToOut(const G4ThreeVector& p) const override;

  /// Return a string identifying the entity type.
  G4GeometryType GetEntityType() const override;

  /// Return the axis-aligned bounding box extent.
  G4VisExtent GetExtent() const override;

  /// Return the axis-aligned bounding box limits.
  void BoundingLimits(G4ThreeVector& pMin, G4ThreeVector& pMax) const override;

  /// Calculate the extent of the solid in the given axis.
  G4bool CalculateExtent(const EAxis pAxis, const G4VoxelLimits& pVoxelLimit,
                         const G4AffineTransform& pTransform, G4double& pMin,
                         G4double& pMax) const override;

  /// Describe the solid to the graphics scene.
  void DescribeYourselfTo(G4VGraphicsScene& scene) const override;

  /// Create a polyhedron representation for visualisation.
  G4Polyhedron* CreatePolyhedron() const override;

  /// Stream a human-readable description.
  std::ostream& StreamInfo(std::ostream& os) const override;

  // ── G4OCCTSolid-specific interface ────────────────────────────────────────

  /// Read access to the underlying OCCT shape.
  const TopoDS_Shape& GetOCCTShape() const { return fShape; }

  /// Replace the underlying OCCT shape.
  /// @note Must only be called during geometry construction, before any worker
  ///       threads are launched.  Calling this after worker threads have been
  ///       started leaves per-thread classifier caches stale.
  void SetOCCTShape(const TopoDS_Shape& shape) {
    fShape = shape;
    fClassifierCache.Get().reset();
  }

private:
  TopoDS_Shape fShape;

  /// Per-thread cache of the BRepClass3d_SolidClassifier for this solid.
  ///
  /// Initialised lazily on the first `Inside` or `DistanceToIn(p)` call on
  /// each thread; thereafter the already-loaded classifier is reused, avoiding
  /// the O(N_faces) construction overhead on every call.
  mutable G4Cache<std::optional<BRepClass3d_SolidClassifier>> fClassifierCache;

  /// Return a reference to the per-thread classifier, initialising it from
  /// @c fShape on the first call from each thread.
  BRepClass3d_SolidClassifier& GetOrCreateClassifier() const;
};

#endif // G4OCCT_G4OCCTSolid_hh
