// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

/// @file G4OCCTSolid.hh
/// @brief Declaration of G4OCCTSolid.

#ifndef G4OCCT_G4OCCTSolid_hh
#define G4OCCT_G4OCCTSolid_hh

#include <G4Cache.hh>
#include <G4Polyhedron.hh>
#include <G4ThreeVector.hh>
#include <G4VSolid.hh>

// OCCT shape representation
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_TriangleSet.hxx>
#include <Bnd_Box.hxx>
#include <IntCurvesFace_Intersector.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_TShape.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt2d.hxx>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
 * `BRepClass3d_SolidClassifier` and `IntCurvesFace_Intersector` hold
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
   * @throws std::invalid_argument if @p shape is null or has no computable bounding box.
   */
  G4OCCTSolid(const G4String& name, const TopoDS_Shape& shape);

  ~G4OCCTSolid() override;

  /**
   * Load a STEP file and construct a G4OCCTSolid from the first shape found.
   *
   * Convenience factory that combines STEP file reading with solid construction.
   * Equivalent to constructing a `G4OCCTSolid` from the shape returned by a
   * `STEPControl_Reader`.
   *
   * @param name Name registered with the Geant4 solid store.
   * @param path Filesystem path to the STEP file.
   * @return Pointer to a newly heap-allocated G4OCCTSolid (owned by the caller).
   * @throws std::runtime_error if the file cannot be read or yields a null shape.
   */
  static G4OCCTSolid* FromSTEP(const G4String& name, const std::string& path);

  // ── G4VSolid pure-virtual interface ───────────────────────────────────────

  /// Return kInside, kSurface, or kOutside for point @p p.
  EInside Inside(const G4ThreeVector& p) const override;

  /// Return the outward unit normal at surface point @p p.
  G4ThreeVector SurfaceNormal(const G4ThreeVector& p) const override;

  /// Distance from external point @p p along direction @p v to solid surface.
  /// @param p  Point outside (or on) the solid surface.
  /// @param v  Non-zero unit direction vector (precondition required by the G4VSolid contract).
  G4double DistanceToIn(const G4ThreeVector& p, const G4ThreeVector& v) const override;

  /// A lower bound on the shortest distance from external point @p p to the solid surface.
  ///
  /// Uses a two-stage acceleration pipeline:
  ///
  /// **Tier 0 (O(1)):** If @p p is outside the cached axis-aligned bounding box by more than
  /// `IntersectionTolerance()`, the AABB distance is returned immediately.  This is always a
  /// valid conservative lower bound and avoids any OCCT call for distant exterior points.
  ///
  /// **Fallback:** Points within `IntersectionTolerance()` of the AABB fall through to
  /// `ExactDistanceToIn(p)`.
  ///
  /// For the exact shortest distance, use ExactDistanceToIn(p).
  G4double DistanceToIn(const G4ThreeVector& p) const override;

  /// Distance from internal point @p p along direction @p v to solid surface.
  /// @param p  Point inside (or on) the solid surface.
  /// @param v  Non-zero unit direction vector (precondition required by the G4VSolid contract).
  G4double DistanceToOut(const G4ThreeVector& p, const G4ThreeVector& v,
                         const G4bool calcNorm = false, G4bool* validNorm = nullptr,
                         G4ThreeVector* n = nullptr) const override;

  /// A lower bound on the shortest distance from internal point @p p to the solid surface.
  /// For the exact shortest distance, use ExactDistanceToOut(p).
  G4double DistanceToOut(const G4ThreeVector& p) const override;

  /// Return a point sampled uniformly at random from the solid's surface.
  ///
  /// The OCCT shape is tessellated with a relative chord deflection of 1 % and
  /// each mesh triangle is selected with probability proportional to its
  /// (planar) area.  A random point within the selected triangle is then
  /// generated using standard barycentric-coordinate sampling and projected
  /// back to the nearest point on the triangle's originating analytical OCCT
  /// face surface via `GeomAPI_ProjectPointOnSurf`.  This projection step
  /// places the returned point on the analytical surface for curved faces
  /// (spheres, cylinders, cones, tori, etc.), rather than inside the solid at
  /// a chord midpoint of the tessellation mesh.  If projection fails (e.g. for
  /// a degenerate or null surface), the raw tessellation point is returned as a
  /// fallback; such a point may lie slightly inside the solid.
  ///
  /// The distribution is area-weighted over the triangulated surface, which
  /// approximates the uniform distribution over the true analytic surface; the
  /// bias is proportional to the curvature and the 1 % chord-height error.
  /// The tessellation is cached per shape generation so repeated calls only
  /// require an O(log N_triangles) area selection plus one surface projection.
  ///
  /// Emits a @c JustWarning G4Exception and returns the origin if the
  /// tessellation produces no valid triangles.
  G4ThreeVector GetPointOnSurface() const override;

  // ── G4OCCTSolid distance functions ────────────────────────────────────────

  /// Exact shortest distance from external point @p p to the solid surface.
  /// Returns 0 if @p p is on or inside the surface, or kInfinity if the
  /// calculation fails.
  G4double ExactDistanceToIn(const G4ThreeVector& p) const;

  /// Exact shortest distance from internal point @p p to the solid surface.
  /// Returns 0 if @p p is within IntersectionTolerance() of the surface, or if
  /// the calculation fails. For points outside the solid,
  /// returns the positive distance to the nearest surface.
  G4double ExactDistanceToOut(const G4ThreeVector& p) const;

  /// Compute and return the cubic volume of the solid.
  /// The result is cached; repeated calls return the cached value.
  G4double GetCubicVolume() override;

  /// Compute and return the surface area of the solid.
  /// The result is cached; repeated calls return the cached value.
  G4double GetSurfaceArea() override;

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
  /// @throws std::invalid_argument if @p shape is null or has no computable bounding box.
  void SetOCCTShape(const TopoDS_Shape& shape) {
    if (shape.IsNull()) {
      throw std::invalid_argument("G4OCCTSolid::SetOCCTShape: shape must not be null");
    }
    fShape = shape;
    ComputeBounds();
    {
      std::unique_lock<std::mutex> lock(fPolyhedronMutex);
      fCachedPolyhedron.reset();
    }
    {
      std::unique_lock<std::mutex> lock(fVolumeAreaMutex);
      fCachedVolume.reset();
      fCachedSurfaceArea.reset();
    }
    {
      std::unique_lock<std::mutex> lock(fSurfaceCacheMutex);
      fSurfaceCache.reset();
    }
    fShapeGeneration.fetch_add(1, std::memory_order_release);
  }

private:
  /// Per-face bounding box entry used to prefilter the closest-face search in
  /// `SurfaceNormal`.
  struct FaceBounds {
    TopoDS_Face face;
    Bnd_Box box;
    BRepAdaptor_Surface adaptor;
    std::optional<gp_Pln> plane; ///< precomputed plane equation for planar faces
    /// Outer wire boundary vertices in the plane's (u,v) parameter space.
    /// Non-empty only when the face is planar and every edge of the outer wire
    /// is a straight line segment, which allows exact point-in-polygon testing
    /// without invoking the full OCCT topology machinery at navigation time.
    std::vector<gp_Pnt2d> uvPolygon;
    /// Precomputed constant outward normal for planar faces.  Non-empty whenever
    /// @c uvPolygon is non-empty.  Used by DistanceToOut(p,v) to skip the
    /// BRepLProp_SLProps evaluation on the hot normal-computation path.
    std::optional<G4ThreeVector> outwardNormal;
  };

  /// Result of the closest-face search.
  struct ClosestFaceMatch {
    TopoDS_Face face;
    G4double distance{kInfinity};
    /// Index into the @c fFaceBoundsCache vector used to retrieve the cached
    /// @c FaceBounds::adaptor without reconstructing it on the fly.
    std::size_t faceIndex{0};
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

  /// Per-thread intersector cache entry: generation stamp + per-face intersectors.
  ///
  /// The `generation` field is compared against `fShapeGeneration` on every
  /// call to `GetOrCreateIntersector()`; a mismatch triggers a reload.
  /// Initialised to the maximum uint64 value, which can never match generation 0,
  /// so the first call from each thread always builds the intersector.
  ///
  /// `faceIntersectors` holds one `IntCurvesFace_Intersector` per entry in
  /// `fFaceBoundsCache`, in the same order.  Each entry is heap-allocated
  /// (via `std::unique_ptr`) to avoid requiring movability of
  /// `IntCurvesFace_Intersector` during vector reallocation.  The per-face
  /// intersectors are used by `DistanceToIn(p,v)` and `DistanceToOut(p,v,...)`
  /// in a bbox-prefiltered loop that avoids `NCollection_Sequence` heap
  /// allocation.
  ///
  /// `expandedBoxes` holds a copy of each entry's `FaceBounds::box`, enlarged
  /// by `IntersectionTolerance()`.  Planar faces have zero-thickness bounding
  /// boxes in their normal direction; `Bnd_Box::IsOut(gp_Lin)` can return a
  /// floating-point false positive for such degenerate boxes when the line
  /// grazes the bounding plane.  Enlarging by the tolerance ensures every box
  /// has finite extent in all directions and makes the `IsOut` test robust.
  struct IntersectorCache {
    std::uint64_t generation{std::numeric_limits<std::uint64_t>::max()};
    std::vector<std::unique_ptr<IntCurvesFace_Intersector>> faceIntersectors;
    std::vector<Bnd_Box> expandedBoxes; ///< per-face boxes enlarged by `IntersectionTolerance()`
  };

  /// A proven inscribed sphere: every point within @c radius of @c centre is
  /// inside the solid.  The guarantee comes from @c DistanceToOut(centre) ≥ radius.
  struct InscribedSphere {
    G4ThreeVector centre;
    G4double radius;
  };

  /// Per-thread inscribed-sphere cache.
  ///
  /// Populated from `fInitialSpheres` on first access per thread and grown by
  /// `TryInsertSphere()` on each `DistanceToOut(p)` call.  Kept sorted in
  /// descending order of radius so `Inside()` checks the largest (most useful)
  /// spheres first and benefits from early exit.
  ///
  /// The `generation` field mirrors the classifier-cache pattern: a mismatch
  /// with `fShapeGeneration` causes the cache to be re-seeded from
  /// `fInitialSpheres`, discarding any runtime-accumulated spheres for the old
  /// shape.
  struct SphereCacheData {
    std::vector<InscribedSphere> spheres;
    std::uint64_t generation{std::numeric_limits<std::uint64_t>::max()};
  };

  /// Maximum number of inscribed spheres kept in the per-thread cache.
  static constexpr std::size_t kMaxInscribedSpheres = 64;

  /// Single mesh triangle used by the surface-sampling cache.
  ///
  /// Each triangle carries only the three vertex positions and a compact index
  /// into `SurfaceSamplingCache::faces`.  Using an index instead of a full
  /// `TopoDS_Face` value avoids duplicating the face handle across every
  /// triangle on that face, which can be thousands of entries for a
  /// finely-tessellated shape.
  struct SurfaceTriangle {
    G4ThreeVector p1, p2, p3;
    std::uint32_t
        faceIndex; ///< Index into SurfaceSamplingCache::faces (valid range: [0, faces.size())).
  };

  /// Cached data for area-weighted surface-point sampling via `GetPointOnSurface()`.
  ///
  /// Stores the flat list of triangles extracted from the OCCT triangulation
  /// together with a cumulative-area array that enables O(log N) area-weighted
  /// selection, and a deduplicated list of the OCCT faces from which the
  /// triangles were collected (used for surface projection in
  /// `GetPointOnSurface()`).  Built lazily on the first `GetPointOnSurface()`
  /// call after each shape update; keyed by `fShapeGeneration`.
  struct SurfaceSamplingCache {
    std::vector<TopoDS_Face> faces; ///< Unique OCCT faces, one entry per shape face.
    std::vector<SurfaceTriangle> triangles;
    std::vector<G4double> cumulativeAreas;
    G4double totalArea{0.0};
  };

  /// The underlying OCCT shape.  Guaranteed non-null: the constructor and
  /// SetOCCTShape() both throw std::invalid_argument when given a null shape.
  TopoDS_Shape fShape;

  /// Cached axis-aligned bounding box; computed eagerly in the constructor and
  /// recomputed by `ComputeBounds()` whenever `SetOCCTShape()` is called.
  /// Always valid after construction: the constructor and `SetOCCTShape()` throw
  /// `std::invalid_argument` if the shape has no computable bounding box.
  AxisAlignedBounds fCachedBounds;

  /// Per-face bounding boxes, populated alongside `fCachedBounds` in `ComputeBounds()`.
  /// Used by `SurfaceNormal` as a lower-bound prefilter to avoid calling
  /// `BRepExtrema_DistShapeShape` on faces that are provably farther than the
  /// current best candidate.  Written once at construction / `SetOCCTShape()`;
  /// read-only during navigation, so no additional synchronisation is required.
  std::vector<FaceBounds> fFaceBoundsCache;

  /// Index from a face's underlying `TShape` pointer to the list of
  /// `fFaceBoundsCache` entry indices that share that `TShape`.  Populated
  /// alongside `fFaceBoundsCache` in `ComputeBounds()` and used by
  /// `DistanceToOut(p,v,...)` to avoid an O(N) linear scan when looking up
  /// the cached `BRepAdaptor_Surface` for the hit face returned by the
  /// intersector.
  ///
  /// Hash lookup narrows candidates to the (almost always singleton) set of
  /// faces sharing the same `TShape`; `IsPartner()` (TShape + Location) is
  /// then used within that set to select the correct located entry, handling
  /// instanced sub-shapes where the same `TShape` appears at several locations.
  ///
  /// Written once at construction / `SetOCCTShape()`; read-only during
  /// navigation.
  std::unordered_map<const TopoDS_TShape*, std::vector<std::size_t>> fFaceAdaptorIndex;

  /// Inscribed spheres seeded at construction time by `ComputeInitialSpheres()`.
  ///
  /// Evaluated once per shape at up to 15 interior candidate points (AABB centre,
  /// 6 axis-offset interior points each placed at half the distance from the AABB
  /// centre to the nearest face along that axis, and 8 octant centres) using
  /// `BVHLowerBoundDistance`.
  /// Written only in `ComputeBounds()` → `ComputeInitialSpheres()`; thereafter
  /// read-only, so no additional synchronisation is needed.
  /// Copied into each thread's `SphereCacheData` on first cache access.
  std::vector<InscribedSphere> fInitialSpheres;

  /// BVH-accelerated triangle set over the tessellated surface of @c fShape.
  ///
  /// Built once in `ComputeBounds()` after `BRepMesh_IncrementalMesh` tessellates
  /// the shape.  Used by `BVHLowerBoundDistance()` to compute O(log T) lower bounds
  /// on the point-to-surface distance.  Null when the shape has no faces.
  /// Written only in `ComputeBounds()`; read-only (const BVH traversal) during
  /// navigation — no additional synchronisation is required beyond the construction
  /// ordering already guaranteed by the geometry-build phase.
  Handle(BRepExtrema_TriangleSet) fTriangleSet;

  /// Conservative upper bound on the Hausdorff distance between the analytical
  /// surface of @c fShape and its tessellation stored in @c fTriangleSet.
  ///
  /// Set in `ComputeBounds()` to `kRelativeDeflection × max_face_bbox_diagonal`,
  /// which bounds the chord-height error for a relative linear deflection of
  /// `kRelativeDeflection`.  Used in `BVHLowerBoundDistance()` as the correction
  /// term `δ` in `s = max(0, mesh_dist − δ)` to guarantee a valid lower bound.
  G4double fBVHDeflection{0.0};

  /// True if every face in @c fFaceBoundsCache has a precomputed plane (i.e. all
  /// faces are planar).  Set by `ComputeBounds()`.  When true, `DistanceToOut(p)`
  /// can bypass the BVH triangle-mesh traversal entirely.
  bool fAllFacesPlanar{false};

  /// Monotonically increasing counter; incremented by each `SetOCCTShape()` call.
  /// Read (acquire) in `GetOrCreateClassifier()` and `GetOrCreateIntersector()` const;
  /// written (release) in `SetOCCTShape()`.
  std::atomic<std::uint64_t> fShapeGeneration{0};

  /// Per-thread cache of the BRepClass3d_SolidClassifier for this solid.
  ///
  /// Initialised lazily on the first `Inside` or `DistanceToIn(p)` call on
  /// each thread.  Stale entries (generation mismatch) are rebuilt automatically.
  mutable G4Cache<ClassifierCache> fClassifierCache;

  /// Per-thread cache of per-face `IntCurvesFace_Intersector` objects for this solid.
  ///
  /// Initialised lazily on the first `DistanceToIn(p, v)` or `DistanceToOut(p, v)`
  /// call on each thread.  Stale entries (generation mismatch) are rebuilt automatically.
  mutable G4Cache<IntersectorCache> fIntersectorCache;

  /// Per-thread inscribed-sphere cache, grown online from `DistanceToOut(p)` calls.
  ///
  /// Seeded from `fInitialSpheres` on first access per thread; grown by
  /// `TryInsertSphere()`.  Stale entries (generation mismatch) are re-seeded from
  /// `fInitialSpheres`, discarding runtime-accumulated spheres for the old shape.
  mutable G4Cache<SphereCacheData> fSphereCache;

  /// Cached polyhedron built by the first `CreatePolyhedron()` call after each
  /// shape update.  Subsequent calls return a copy of this object, avoiding a
  /// repeated (and expensive) `BRepMesh_IncrementalMesh` tessellation.
  ///
  /// Access to this field, `fPolyhedronGeneration`, and `fPolyhedronBuilding` is
  /// serialised by `fPolyhedronMutex`.  `nullptr` indicates that no valid cache
  /// entry exists for the current shape generation.
  mutable std::unique_ptr<G4Polyhedron> fCachedPolyhedron;

  /// Shape-generation stamp at which `fCachedPolyhedron` was last built.
  /// Initialised to `std::numeric_limits<std::uint64_t>::max()` so that it
  /// can never match the initial `fShapeGeneration` value of 0, forcing a
  /// build on the first call to `CreatePolyhedron()`.
  /// Protected by `fPolyhedronMutex`.
  mutable std::uint64_t fPolyhedronGeneration{std::numeric_limits<std::uint64_t>::max()};

  /// True while one thread is performing the expensive tessellation.
  /// Other threads that also miss the cache wait on `fPolyhedronCV` instead
  /// of running duplicate tessellations.
  /// Protected by `fPolyhedronMutex`.
  mutable bool fPolyhedronBuilding{false};

  /// Mutex serialising concurrent access to `fCachedPolyhedron`,
  /// `fPolyhedronGeneration`, and `fPolyhedronBuilding` in `CreatePolyhedron()`.
  mutable std::mutex fPolyhedronMutex;

  /// Notified when `fPolyhedronBuilding` transitions to false so that threads
  /// waiting for a concurrent build can re-evaluate the cache.
  mutable std::condition_variable fPolyhedronCV;

  /// Cached cubic volume in mm³; `std::nullopt` until first `GetCubicVolume()` call
  /// or after each `SetOCCTShape()` call.  Protected by `fVolumeAreaMutex`.
  mutable std::optional<G4double> fCachedVolume;

  /// Cached surface area in mm²; `std::nullopt` until first `GetSurfaceArea()` call
  /// or after each `SetOCCTShape()` call.  Protected by `fVolumeAreaMutex`.
  mutable std::optional<G4double> fCachedSurfaceArea;

  /// Mutex serialising concurrent access to `fCachedVolume` and `fCachedSurfaceArea`.
  mutable std::mutex fVolumeAreaMutex;

  /// Cached surface-sampling data built by the first `GetPointOnSurface()` call
  /// after each shape update.  `std::nullopt` indicates no valid entry for the
  /// current shape generation.  Protected by `fSurfaceCacheMutex`.
  mutable std::optional<SurfaceSamplingCache> fSurfaceCache;

  /// Shape-generation stamp at which `fSurfaceCache` was built.
  /// Initialised to `std::numeric_limits<std::uint64_t>::max()` so that it can
  /// never match the initial `fShapeGeneration` value of 0, forcing a build on
  /// the first `GetPointOnSurface()` call.  Protected by `fSurfaceCacheMutex`.
  mutable std::uint64_t fSurfaceCacheGeneration{std::numeric_limits<std::uint64_t>::max()};

  /// Mutex serialising concurrent access to `fSurfaceCache` and
  /// `fSurfaceCacheGeneration` in `GetOrBuildSurfaceCache()`.
  mutable std::mutex fSurfaceCacheMutex;

  /// Return a reference to the per-thread classifier, (re-)initialising it from
  /// @c fShape whenever the cached generation does not match @c fShapeGeneration.
  BRepClass3d_SolidClassifier& GetOrCreateClassifier() const;

  /// Return a reference to the per-thread intersector cache, (re-)initialising
  /// it from @c fShape whenever the cached generation does not match
  /// @c fShapeGeneration.  The cache holds one `IntCurvesFace_Intersector` per
  /// face in `fFaceBoundsCache`, in the same order.
  IntersectorCache& GetOrCreateIntersector() const;

  /// Return a reference to the per-thread inscribed-sphere cache, seeding it from
  /// @c fInitialSpheres if the cached generation does not match @c fShapeGeneration.
  SphereCacheData& GetOrInitSphereCache() const;

  /// Attempt to insert an inscribed sphere with centre @p centre and radius @p d into
  /// the per-thread sphere cache.  The sphere is accepted only if it is not already
  /// dominated by an existing cache entry and passes the minimum-radius filter.
  /// Maintains the cache sorted descending by radius and bounded to @c kMaxInscribedSpheres.
  void TryInsertSphere(const G4ThreeVector& centre, G4double d) const;

  /// Compute the axis-aligned bounding box of @c fShape and store it in
  /// @c fCachedBounds, and populate @c fFaceBoundsCache with per-face bounding
  /// boxes.  Throws `std::invalid_argument` when the shape has no computable
  /// bounding box (e.g. an empty compound).  Called from the constructor and
  /// @c SetOCCTShape().
  void ComputeBounds();

  /// Seed @c fInitialSpheres with inscribed spheres at up to 15 interior candidate
  /// points (AABB centre, 6 face midpoints, 8 octant centres).  Called at the end
  /// of @c ComputeBounds() after the BVH mesh is available.  Each candidate that
  /// classifies as @c TopAbs_IN and has a positive BVH lower-bound distance is
  /// stored as a seed sphere.  Thread-safe: written only during geometry construction,
  /// read-only during navigation.
  void ComputeInitialSpheres();

  /// Compute the distance from @p p to the cached axis-aligned bounding box.
  /// Returns 0 when @p p is on or inside the AABB.
  G4double AABBLowerBound(const G4ThreeVector& p) const;

  /// Compute a conservative lower bound on the distance from @p p to the solid surface
  /// using the BVH-accelerated triangle set @c fTriangleSet.
  ///
  /// The returned value satisfies @c 0 ≤ s ≤ true_distance, making it a valid
  /// Geant4 safety distance.  Returns @c kInfinity if @c fTriangleSet is not
  /// available (null or empty).
  G4double BVHLowerBoundDistance(const G4ThreeVector& p) const;

  /// Returns the minimum perpendicular distance from @p p to any planar face's
  /// infinite supporting plane.
  ///
  /// When the solid boundary is composed entirely of planar faces, the returned
  /// value is a conservative safety distance satisfying
  /// @c 0 ≤ s ≤ true_distance; for interior points of convex all-planar solids
  /// it is exact. For solids that include any curved faces (mixed-topology
  /// solids), this quantity is only a heuristic accelerator and is not
  /// guaranteed to be a strict lower bound on the true distance to the surface.
  ///
  /// Returns @c kInfinity if no planar faces exist.
  G4double PlanarFaceLowerBoundDistance(const G4ThreeVector& p) const;

  /// Find the closest trimmed face to @p point using pre-computed per-face bounding
  /// boxes as a lower-bound prefilter.  A face whose AABB distance from @p point
  /// exceeds @p maxDistance (default: @c kInfinity) or the current best candidate
  /// distance is skipped before the more expensive BRepExtrema_DistShapeShape call
  /// is made.  Passing a BVH-derived upper bound as @p maxDistance lets callers
  /// skip faces that are provably farther than the nearest tessellated surface point
  /// plus twice the BVH deflection, reducing the number of BRepExtrema calls.
  static std::optional<ClosestFaceMatch>
  TryFindClosestFace(const std::vector<FaceBounds>& faceBoundsCache, const G4ThreeVector& point,
                     G4double maxDistance = kInfinity);

  /// Build and cache (or return the already-cached) surface-sampling data for
  /// the current shape generation.  The OCCT shape is tessellated first
  /// (idempotent if a triangulation already exists), then all mesh triangles
  /// are collected with their areas.  The result is protected by
  /// @c fSurfaceCacheMutex and keyed by @c fShapeGeneration.
  const SurfaceSamplingCache& GetOrBuildSurfaceCache() const;
};

#endif // G4OCCT_G4OCCTSolid_hh
