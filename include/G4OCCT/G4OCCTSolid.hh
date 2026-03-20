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
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
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

  /// Return a point sampled uniformly at random from the solid's tessellated
  /// surface approximation.
  ///
  /// The OCCT shape is tessellated with a relative chord deflection of 1 % and
  /// each mesh triangle is selected with probability proportional to its
  /// (planar) area.  A random point within the selected triangle is then
  /// generated using standard barycentric-coordinate sampling.
  ///
  /// The returned point is exactly uniformly distributed over the triangulated
  /// surface.  For curved faces the tessellation approximates the true analytic
  /// surface, so the distribution may deviate slightly from uniform on the
  /// exact surface; the magnitude of the bias is proportional to the chord-
  /// height approximation error.  The tessellation is cached per shape
  /// generation so repeated calls are O(log N_triangles).
  ///
  /// Emits a @c JustWarning G4Exception and returns the origin if the shape is
  /// null or the tessellation produces no valid triangles.
  G4ThreeVector GetPointOnSurface() const override;

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
  void SetOCCTShape(const TopoDS_Shape& shape) {
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

  /// Single mesh triangle used by the surface-sampling cache.
  struct SurfaceTriangle {
    G4ThreeVector p1, p2, p3;
  };

  /// Cached data for area-weighted surface-point sampling via `GetPointOnSurface()`.
  ///
  /// Stores the flat list of triangles extracted from the OCCT triangulation
  /// together with a cumulative-area array that enables O(log N) area-weighted
  /// selection.  Built lazily on the first `GetPointOnSurface()` call after
  /// each shape update; keyed by `fShapeGeneration`.
  struct SurfaceSamplingCache {
    std::vector<SurfaceTriangle> triangles;
    std::vector<G4double> cumulativeAreas;
    G4double totalArea{0.0};
  };

  TopoDS_Shape fShape;

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

  /// Build and cache (or return the already-cached) surface-sampling data for
  /// the current shape generation.  The OCCT shape is tessellated first
  /// (idempotent if a triangulation already exists), then all mesh triangles
  /// are collected with their areas.  The result is protected by
  /// @c fSurfaceCacheMutex and keyed by @c fShapeGeneration.
  const SurfaceSamplingCache& GetOrBuildSurfaceCache() const;
};

#endif // G4OCCT_G4OCCTSolid_hh
