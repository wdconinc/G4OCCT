// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

/// @file G4OCCTSolid.hh
/// @brief Declaration of G4OCCTSolid.

#ifndef G4OCCT_G4OCCTSolid_hh
#define G4OCCT_G4OCCTSolid_hh

#include <G4Cache.hh>
#include <G4Polyhedron.hh>
#include <G4ThreeVector.hh>
#include <G4VSolid.hh>

// OCCT shape representation
#include <BRepClass3d_SolidClassifier.hxx>
#include <Bnd_Box.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

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
 * `BRepClass3d_SolidClassifier` and `IntCurvesFace_ShapeIntersector` hold
 * mutable internal state and must not be shared between threads.  Per-thread
 * caches of both algorithm objects are maintained via `G4Cache` so that the
 * one-time O(N_faces) initialisation cost is paid only once per thread rather
 * than on every navigation call.
 *
 * A monotonically increasing `fShapeGeneration` counter is incremented by
 * `SetOCCTShape()`.  Each per-thread cache entry records the generation at
 * which it was built; `GetOrCreateClassifier()` and `GetOrCreateIntersector()`
 * rebuild the entry whenever the stored generation is stale.  This ensures that
 * all worker threads automatically pick up a new shape on their next navigation
 * call, even if `SetOCCTShape()` is invoked after some threads have already
 * initialised their caches.
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

  /// A lower bound on the shortest distance from external point @p p to the solid surface.
  /// For the exact shortest distance, use ExactDistanceToIn(p).
  G4double DistanceToIn(const G4ThreeVector& p) const override;

  /// Distance from internal point @p p along direction @p v to solid surface.
  G4double DistanceToOut(const G4ThreeVector& p, const G4ThreeVector& v,
                         const G4bool calcNorm = false, G4bool* validNorm = nullptr,
                         G4ThreeVector* n = nullptr) const override;

  /// A lower bound on the shortest distance from internal point @p p to the solid surface.
  /// For the exact shortest distance, use ExactDistanceToOut(p).
  G4double DistanceToOut(const G4ThreeVector& p) const override;

  // ── G4OCCTSolid distance functions ────────────────────────────────────────

  /// Exact shortest distance from external point @p p to the solid surface.
  /// Returns 0 if @p p is on or inside the surface, or kInfinity if the
  /// shape is null or the calculation fails.
  G4double ExactDistanceToIn(const G4ThreeVector& p) const;

  /// Exact shortest distance from internal point @p p to the solid surface.
  /// Returns 0 if @p p is within IntersectionTolerance() of the surface, or if
  /// the shape is null or the calculation fails. For points outside the solid,
  /// returns the positive distance to the nearest surface.
  G4double ExactDistanceToOut(const G4ThreeVector& p) const;

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
  /// @note Increments an internal generation counter so that every worker
  ///       thread automatically reloads its per-thread classifier and
  ///       intersector on its next navigation call.  The shape update itself
  ///       is not atomic with respect to ongoing navigation; avoid calling
  ///       this while a simulation run is in progress.
  void SetOCCTShape(const TopoDS_Shape& shape) {
    fShape = shape;
    ComputeBounds();
    {
      const std::lock_guard<std::mutex> lock(fPolyhedronMutex);
      fCachedPolyhedron.reset();
    }
    fShapeGeneration.fetch_add(1, std::memory_order_release);
  }

private:
  /// Per-face bounding box entry used to prefilter the closest-face search in
  /// `SurfaceNormal`.
  struct FaceBounds {
    TopoDS_Face face;
    Bnd_Box box;
  };

  /// Result of the closest-face search.
  struct ClosestFaceMatch {
    TopoDS_Face face;
    G4double distance{kInfinity};
  };

  /// Axis-aligned bounding box: minimum and maximum corners.
  struct AxisAlignedBounds {
    G4ThreeVector min;
    G4ThreeVector max;
  };

  /// Per-thread classifier cache entry: generation stamp + lazily-built classifier.
  ///
  /// The `generation` field is compared against `fShapeGeneration` on every
  /// call to `GetOrCreateClassifier()`; a mismatch triggers a reload.
  /// Initialised to the maximum uint64 value, which can never match generation 0,
  /// so the first call from each thread always builds the classifier.
  struct ClassifierCache {
    std::uint64_t generation{std::numeric_limits<std::uint64_t>::max()};
    std::optional<BRepClass3d_SolidClassifier> classifier;
  };

  /// Per-thread intersector cache entry: generation stamp + lazily-built intersector.
  ///
  /// The `generation` field is compared against `fShapeGeneration` on every
  /// call to `GetOrCreateIntersector()`; a mismatch triggers a reload.
  /// Initialised to the maximum uint64 value, which can never match generation 0,
  /// so the first call from each thread always builds the intersector.
  struct IntersectorCache {
    std::uint64_t generation{std::numeric_limits<std::uint64_t>::max()};
    std::optional<IntCurvesFace_ShapeIntersector> intersector;
  };

  TopoDS_Shape fShape;

  /// Compound of all faces of @c fShape, cached for @c DistanceToOut(p) queries.
  ///
  /// A solid-wide @c BRepExtrema_DistShapeShape(vertex, solid) returns distance
  /// zero for interior points because the solid contains the vertex.  Querying
  /// against this face compound (which has no interior volume) yields the correct
  /// surface distance regardless of whether the query point is inside or outside.
  TopoDS_Compound fFaceCompound;

  /// Cached axis-aligned bounding box; computed eagerly in the constructor and
  /// recomputed by `ComputeBounds()` whenever `SetOCCTShape()` is called.
  /// `std::nullopt` when the shape is null or has no geometry.
  std::optional<AxisAlignedBounds> fCachedBounds;

  /// Per-face bounding boxes, populated alongside `fCachedBounds` in `ComputeBounds()`.
  /// Used by `SurfaceNormal` as a lower-bound prefilter to avoid calling
  /// `BRepExtrema_DistShapeShape` on faces that are provably farther than the
  /// current best candidate.  Written once at construction / `SetOCCTShape()`;
  /// read-only during navigation, so no additional synchronisation is required.
  std::vector<FaceBounds> fFaceBoundsCache;

  /// Monotonically increasing counter; incremented by each `SetOCCTShape()` call.
  /// Read (acquire) in `GetOrCreateClassifier()` and `GetOrCreateIntersector()` const;
  /// written (release) in `SetOCCTShape()`.
  std::atomic<std::uint64_t> fShapeGeneration{0};

  /// Per-thread cache of the BRepClass3d_SolidClassifier for this solid.
  ///
  /// Initialised lazily on the first `Inside` or `DistanceToIn(p)` call on
  /// each thread.  Stale entries (generation mismatch) are rebuilt automatically.
  mutable G4Cache<ClassifierCache> fClassifierCache;

  /// Per-thread cache of the IntCurvesFace_ShapeIntersector for this solid.
  ///
  /// Initialised lazily on the first `DistanceToIn(p, v)` or `DistanceToOut(p, v)`
  /// call on each thread.  Stale entries (generation mismatch) are rebuilt automatically.
  mutable G4Cache<IntersectorCache> fIntersectorCache;

  /// Cached polyhedron built by the first `CreatePolyhedron()` call after each
  /// shape update.  Subsequent calls return a copy of this object, avoiding a
  /// repeated (and expensive) `BRepMesh_IncrementalMesh` tessellation.
  ///
  /// The cache is invalidated whenever `SetOCCTShape()` is called by comparing
  /// `fPolyhedronGeneration` against `fShapeGeneration` at the start of
  /// `CreatePolyhedron()`.  `nullptr` indicates that no valid cache entry exists.
  ///
  /// Access to this field and `fPolyhedronGeneration` is serialised by
  /// `fPolyhedronMutex` to allow safe concurrent calls to `CreatePolyhedron()`.
  mutable std::unique_ptr<G4Polyhedron> fCachedPolyhedron;

  /// Shape-generation stamp at which `fCachedPolyhedron` was last built.
  /// Initialised to `std::numeric_limits<std::uint64_t>::max()` so that it
  /// can never match the initial `fShapeGeneration` value of 0, forcing a
  /// build on the first call to `CreatePolyhedron()`.
  /// Protected by `fPolyhedronMutex`.
  mutable std::uint64_t fPolyhedronGeneration{std::numeric_limits<std::uint64_t>::max()};

  /// Mutex serialising concurrent access to `fCachedPolyhedron` and
  /// `fPolyhedronGeneration` in `CreatePolyhedron()`.
  mutable std::mutex fPolyhedronMutex;

  /// Return a reference to the per-thread classifier, (re-)initialising it from
  /// @c fShape whenever the cached generation does not match @c fShapeGeneration.
  BRepClass3d_SolidClassifier& GetOrCreateClassifier() const;

  /// Return a reference to the per-thread intersector, (re-)initialising it from
  /// @c fShape whenever the cached generation does not match @c fShapeGeneration.
  IntCurvesFace_ShapeIntersector& GetOrCreateIntersector() const;

  /// Compute the axis-aligned bounding box of @c fShape and store it in
  /// @c fCachedBounds, and populate @c fFaceBoundsCache with per-face bounding
  /// boxes.  Sets @c fCachedBounds to @c std::nullopt and clears @c fFaceBoundsCache
  /// when the shape is null or has no geometry.  Called from the constructor and
  /// @c SetOCCTShape().
  void ComputeBounds();

  /// Find the closest trimmed face to @p point using pre-computed per-face bounding
  /// boxes as a lower-bound prefilter.  A face whose AABB distance from @p point
  /// exceeds the current best candidate distance is skipped before the more expensive
  /// BRepExtrema_DistShapeShape call is made.
  static std::optional<ClosestFaceMatch>
  TryFindClosestFace(const std::vector<FaceBounds>& faceBoundsCache, const G4ThreeVector& point);
};

#endif // G4OCCT_G4OCCTSolid_hh
