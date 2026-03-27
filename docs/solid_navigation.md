<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

# G4OCCT — Solid Navigation Design: G4VSolid Functions ↔ OCCT Algorithms

This document provides a detailed analysis of every pure-virtual and virtual
function in `G4VSolid`, maps each to the most appropriate OCCT algorithm,
and discusses implementation strategies for functions that have no direct
OCCT counterpart.

---

## 1. Background

`G4VSolid` defines the geometry query interface that the Geant4 navigator
calls during tracking.  Every solid must answer six fundamental queries:

| Query | Description |
|---|---|
| `Inside(p)` | Is point `p` inside, outside, or on the surface? |
| `SurfaceNormal(p)` | Outward unit normal at surface point `p` |
| `DistanceToIn(p, v)` | Distance along ray (p, v) from outside to surface |
| `DistanceToIn(p)` | Closest distance from external point `p` to surface |
| `DistanceToOut(p, v)` | Distance along ray (p, v) from inside to surface |
| `DistanceToOut(p)` | Closest distance from internal point `p` to surface |

Additional virtual functions govern visualisation and bounding-box queries.
All are described below.

OCCT provides algorithms that operate on BRep shapes (`TopoDS_Shape`) to
answer these queries.  The key challenge is that OCCT uses a different
sign convention and tolerance model, and returns results in OCCT units
(typically millimetres for STEP imports), which must be reconciled with
Geant4's unit system.

---

## 2. Function-by-Function Mapping

### 2.1 `EInside Inside(const G4ThreeVector& p) const`

**Geant4 semantics:**
Returns `kInside`, `kSurface`, or `kOutside` for point `p`.  The surface
shell has a half-width of `kCarTolerance/2` (≈ 1 nm default).

**Algorithm — multi-stage pipeline (PRs #197, #214, #221):**

Classification proceeds through up to four stages, returning as soon as a
definitive answer is available:

1. **AABB early reject** — if `p` lies sufficiently outside `fCachedBounds`,
   return `kOutside` immediately.  `fCachedBounds` stores the raw OCCT
   bounding box from `BRepBndLib::AddOptimal`; the `Inside(p)` implementation
   applies an `IntersectionTolerance()` margin (≈ `kCarTolerance/2`) in its
   component-wise comparisons against this box.
2. **Inscribed-sphere fast path** — per-thread `G4Cache<SphereCacheData>`
   holds up to 64 inscribed spheres.  If `p` is strictly inside any cached
   sphere, return `kInside` without touching OCCT.
3. **Ray-parity test** — cast a +Z ray from `p` and count face crossings
   using a per-thread `G4Cache<IntersectorCache>`.  Each entry holds a
   pre-constructed `IntCurvesFace_Intersector` for one face; a
   `Bnd_Box::IsOut` prefilter skips faces whose bounding box the ray cannot
   reach.  Only `TopAbs_IN` state forward hits count; `TopAbs_ON` hits (ray
   lands on a shared edge or vertex) set the `degenerateRay` flag.  If `p`
   itself is on a face, `onSurface` is set.  Fall through to stage 4 when the
   parity count is ambiguous (zero `TopAbs_IN` crossings, or at least one
   edge/vertex hit).
4. **`BRepClass3d_SolidClassifier` fallback** — used only when the ray is
   ambiguous (degenerate hit or no interior crossings found).

```cpp
EInside G4OCCTSolid::Inside(const G4ThreeVector& p) const {
  const G4double tolerance = IntersectionTolerance();
  // Stage 1: AABB early reject (raw bounding box + tolerance margin)
  if (p.x() < fCachedBounds.min.x() - tolerance || p.x() > fCachedBounds.max.x() + tolerance ||
      p.y() < fCachedBounds.min.y() - tolerance || p.y() > fCachedBounds.max.y() + tolerance ||
      p.z() < fCachedBounds.min.z() - tolerance || p.z() > fCachedBounds.max.z() + tolerance)
    return kOutside;

  // Stage 2: inscribed-sphere fast path (per-thread cache)
  const SphereCacheData& sphereCache = GetOrInitSphereCache();
  for (const InscribedSphere& s : sphereCache.spheres) {
    const G4double interiorRadius = s.radius - tolerance;
    if (interiorRadius > 0.0 && (p - s.centre).mag2() < interiorRadius * interiorRadius)
      return kInside;
  }

  // Stage 3: ray-parity test
  const gp_Lin ray(ToPoint(p), gp_Dir(0.0, 0.0, 1.0));
  IntersectorCache& cache = GetOrCreateIntersector();
  int crossings      = 0;
  bool onSurface     = false;
  bool degenerateRay = false;
  for (std::size_t i = 0; i < fFaceBoundsCache.size(); ++i) {
    if (cache.expandedBoxes[i].IsOut(ray)) continue;  // O(1) AABB reject
    IntCurvesFace_Intersector& fi = *cache.faceIntersectors[i];
    fi.Perform(ray, -tolerance, Precision::Infinite());
    if (!fi.IsDone()) continue;
    for (Standard_Integer j = 1; j <= fi.NbPnt(); ++j) {
      const G4double w         = fi.WParameter(j);
      const TopAbs_State state = fi.State(j);
      if (std::abs(w) <= tolerance && (state == TopAbs_IN || state == TopAbs_ON))
        onSurface = true;  // p lies on the face
      else if (w > tolerance && state == TopAbs_IN)
        ++crossings;
      else if (w > tolerance && state == TopAbs_ON)
        degenerateRay = true;  // ray hits a shared edge/vertex
    }
  }
  if (onSurface) return kSurface;
  if (crossings == 0 || degenerateRay) {
    // Stage 4: BRepClass3d fallback (degenerate or ambiguous ray only)
    BRepClass3d_SolidClassifier& classifier = GetOrCreateClassifier();
    classifier.Perform(ToPoint(p), tolerance);
    return ToG4Inside(classifier.State());
  }
  return (crossings % 2 == 1) ? kInside : kOutside;
}
```

**Notes:**

* The per-thread caches (`fSphereCache`, `fIntersectorCache`) are
  `G4Cache<T>` objects so that each worker thread owns independent mutable
  state — the shape itself (`fShape`) is read-only and shared safely.
* `BRepClass3d_SolidClassifier` is only reached when the parity count is
  ambiguous (no `TopAbs_IN` crossings found, or at least one edge/vertex hit),
  keeping the common-case cost to pure C++ arithmetic plus per-face line/box
  tests.

---

### 2.2 `G4ThreeVector SurfaceNormal(const G4ThreeVector& p) const`

**Geant4 semantics:**
Returns the outward unit normal at surface point `p`.  Behaviour is undefined
for points not on the surface.

**OCCT algorithm:**
Use `BRepAdaptor_Surface` or `BRepGProp_Face` on the closest face.  The
sequence is:

1. Find the face closest to `p` using `BRepExtrema_DistShapeShape` between
   `p` (as a `BRep_Builder` vertex) and the shape.
2. Project `p` onto that face to get UV parameters using
   `GeomAPI_ProjectPointOnSurf`.
3. Evaluate the surface normal at (U, V) using `BRepAdaptor_Surface::DN`.

```cpp
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <gp_Vec.hxx>

G4ThreeVector G4OCCTSolid::SurfaceNormal(const G4ThreeVector& p) const {
  // Step 1: build a point vertex for distance computation
  BRep_Builder builder;
  TopoDS_Vertex vertex;
  builder.MakeVertex(vertex, gp_Pnt(p.x(), p.y(), p.z()), kCarTolerance);

  // Step 2: find the closest face
  BRepExtrema_DistShapeShape dist(vertex, fShape);
  if (!dist.IsDone() || dist.NbSolution() == 0)
    return G4ThreeVector(0, 0, 1);  // fallback

  TopoDS_Shape closest = dist.SupportOnShape2(1);
  if (closest.ShapeType() != TopAbs_FACE)
    return G4ThreeVector(0, 0, 1);

  // Step 3: project point onto face and evaluate normal
  BRepAdaptor_Surface surf(TopoDS::Face(closest));
  gp_Pnt projPnt;
  Standard_Real u, v;
  GeomAPI_ProjectPointOnSurf proj(gp_Pnt(p.x(), p.y(), p.z()),
                                  surf.Surface().Surface());
  proj.LowerDistanceParameters(u, v);
  gp_Vec n1, n2;
  surf.D1(u, v, projPnt, n1, n2);
  gp_Vec normal = n1.Crossed(n2);
  normal.Normalize();
  // Ensure outward orientation
  if (closest.Orientation() == TopAbs_REVERSED)
    normal.Reverse();
  return G4ThreeVector(normal.X(), normal.Y(), normal.Z());
}
```

---

### 2.3 `G4double DistanceToIn(const G4ThreeVector& p, const G4ThreeVector& v) const`

**Geant4 semantics:**
Returns the distance along ray `(p, v)` from external point `p` to the first
surface intersection.  Returns `kInfinity` if the ray misses.

**Algorithm — per-face intersector loop (PR #215):**
A per-thread `IntersectorCache` holds one `IntCurvesFace_Intersector` per
face together with an expanded bounding box for that face.  This avoids
re-constructing intersector objects on every call and allows an O(1)
per-face AABB prefilter to skip irrelevant faces.

```cpp
G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& p,
                                    const G4ThreeVector& v) const {
  gp_Lin ray(gp_Pnt(p.x(), p.y(), p.z()),
             gp_Dir(v.x(), v.y(), v.z()));

  IntersectorCache& cache = fIntersectorCache.Get();  // per-thread
  G4double minDistance = kInfinity;
  for (std::size_t i = 0; i < fFaceBoundsCache.size(); ++i) {
    if (cache.expandedBoxes[i].IsOut(ray)) continue;  // O(1) AABB reject
    IntCurvesFace_Intersector& fi = *cache.faceIntersectors[i];
    fi.Perform(ray, kCarTolerance, Precision::Infinite());
    for (Standard_Integer j = 1; j <= fi.NbPnt(); ++j) {
      const G4double w = fi.WParameter(j);
      if (w > kCarTolerance && w < minDistance) minDistance = w;
    }
  }
  return minDistance;
}
```

**Notes:**

* Each `IntCurvesFace_Intersector` in the cache is associated with a single
  `TopoDS_Face`; this replaces the previous `IntCurvesFace_ShapeIntersector`
  which operated on the whole shape and could not be prefiltered per-face.
* The parametric lower bound `kCarTolerance` avoids self-intersection at the
  current surface point.
* `Precision::Infinite()` from OCCT acts as the upper parametric bound;
  intersections beyond the simulation world are naturally excluded by the
  navigator's step-length limit.

---

### 2.4 `G4double DistanceToIn(const G4ThreeVector& p) const`

**Geant4 semantics:**
Returns the shortest distance from external point `p` to the solid surface,
which may be zero if `p` is already inside or on the surface.

**Algorithm — AABB lower bound + exact fallback (PR #209):**
A two-tier approach is used:

1. **AABB lower bound (O(1))** — compute the distance from `p` to the
   axis-aligned bounding box `fCachedBounds`.  If this distance exceeds
   `IntersectionTolerance()`, it is a guaranteed conservative lower bound on
   the true surface distance and is returned immediately.
2. **Exact fallback** — for points near or inside the AABB (where the AABB
   bound is no longer useful), `ExactDistanceToIn(p)` is called, which uses
   `BRepClass3d_SolidClassifier` to check interiority and
   `TryFindClosestFace` (backed by `BRepExtrema_DistShapeShape` per face) to
   obtain the true closest-point distance.

```cpp
G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& p) const {
  // Tier-0: AABB lower bound (O(1)).
  const G4double aabbDist = AABBLowerBound(p);
  if (aabbDist > IntersectionTolerance()) {
    return aabbDist;
  }

  // Fallback: exact distance (handles points near or inside the AABB).
  return ExactDistanceToIn(p);
}
```

---

### 2.5 `G4double DistanceToOut(const G4ThreeVector& p, const G4ThreeVector& v, ...) const`

**Geant4 semantics:**
Distance from *internal* point `p` along direction `v` to the first surface
intersection.  If `calcNorm` is true, also fills `*n` with the outward
surface normal at the intersection and sets `*validNorm`.

**Algorithm — per-face intersector loop (PR #215):**
Identical loop structure to `DistanceToIn(p, v)`, reusing the same per-thread
`IntersectorCache`.  The semantic difference is that `p` is interior, so the
smallest positive crossing distance is the exit distance.

```cpp
G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p,
                                     const G4ThreeVector& v,
                                     const G4bool calcNorm,
                                     G4bool* validNorm,
                                     G4ThreeVector* n) const {
  if (validNorm != nullptr) *validNorm = false;

  const G4double tolerance = IntersectionTolerance();
  const gp_Lin ray(ToPoint(p), gp_Dir(v.x(), v.y(), v.z()));

  IntersectorCache& cache = GetOrCreateIntersector();  // per-thread
  G4double minDistance   = kInfinity;
  std::size_t minFaceIdx = std::numeric_limits<std::size_t>::max();
  G4double minU = 0.0, minV = 0.0;
  bool minIsIn = false;

  for (std::size_t i = 0; i < fFaceBoundsCache.size(); ++i) {
    if (cache.expandedBoxes[i].IsOut(ray)) continue;  // O(1) AABB reject
    IntCurvesFace_Intersector& fi = *cache.faceIntersectors[i];
    fi.Perform(ray, tolerance, Precision::Infinite());
    if (!fi.IsDone()) continue;
    for (Standard_Integer j = 1; j <= fi.NbPnt(); ++j) {
      const G4double w = fi.WParameter(j);
      if (w > tolerance && w < minDistance) {
        minDistance = w;
        minFaceIdx  = i;
        minU        = fi.UParameter(j);
        minV        = fi.VParameter(j);
        minIsIn     = (fi.State(j) == TopAbs_IN || fi.State(j) == TopAbs_ON);
      }
    }
  }

  if (minFaceIdx == std::numeric_limits<std::size_t>::max() || minDistance == kInfinity)
    return 0.0;

  // Normal: read pre-stored (u,v) parameters to avoid re-intersecting.
  if (calcNorm && validNorm != nullptr && n != nullptr && minIsIn) {
    const FaceBounds& fb = fFaceBoundsCache[minFaceIdx];
    const auto outNorm   = TryGetOutwardNormal(fb.adaptor, fb.face, minU, minV);
    if (outNorm) {
      *n         = *outNorm;
      *validNorm = true;
    }
  }

  return minDistance;
}
```

---

### 2.6 `G4double DistanceToOut(const G4ThreeVector& p) const`

**Geant4 semantics:**
Shortest distance from *internal* point `p` to the surface.  The returned
value is a *conservative lower bound* on the true distance (never larger),
which is what the navigator's safety calculation requires.

**Algorithm — planar shortcut or BVH lower bound + exact fallback (PR #210):**

1. **All-planar solids** (`fAllFacesPlanar == true`) — the minimum perpendicular
   distance to each face's infinite supporting plane is computed.  This is a
   conservative lower bound: for a convex solid it is exact; for a non-convex
   solid the closest point may lie on an edge or vertex and the true distance
   could be smaller than the plane distance, but never larger.  If
   `PlanarFaceLowerBoundDistance` returns `kInfinity` (degenerate case), the
   exact fallback is used.
2. **Mixed geometry** (at least one curved face) — a BVH-accelerated triangle
   set (`fTriangleSet`) provides a fast conservative lower bound
   (`BVHLowerBoundDistance`).  If the BVH bound is unavailable (returns
   `kInfinity`), `ExactDistanceToOut` is called.
3. **Exact fallback** (`ExactDistanceToOut`) — uses `TryFindClosestFace`
   (backed by `BRepExtrema_DistShapeShape` per face) for a precise result.

As a side effect, the returned distance `d` seeds the inscribed-sphere cache
(`TryInsertSphere`): every positive return value proves that the ball `B(p, d)`
is fully inside the solid and can accelerate future `Inside(p)` calls.

```cpp
G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p) const {
  G4double d;
  if (fAllFacesPlanar) {
    // All faces planar: plane distance is a conservative lower bound.
    d = PlanarFaceLowerBoundDistance(p);
    if (d == kInfinity) {
      d = ExactDistanceToOut(p);
    }
  } else {
    // Mixed geometry: use BVH lower bound; exact fallback if unavailable.
    const G4double bvhDist = BVHLowerBoundDistance(p);
    d = (bvhDist < kInfinity) ? bvhDist : ExactDistanceToOut(p);
  }
  // Seed the inscribed-sphere cache with the returned distance.
  TryInsertSphere(p, d);
  return d;
}
```

---

### 2.7 `G4GeometryType GetEntityType() const`

**OCCT counterpart:** None needed.  Returns a fixed string `"G4OCCTSolid"`.

---

### 2.8 `G4VisExtent GetExtent() const`

**Geant4 semantics:**
Returns an axis-aligned bounding box sufficient for visualisation.

**Algorithm — cached at construction:**
`fCachedBounds` is a `Bnd_Box` computed once in the `G4OCCTSolid` constructor
via `BRepBndLib::Add` and stored as a member.  `GetExtent` simply unpacks it.

```cpp
G4VisExtent G4OCCTSolid::GetExtent() const {
  if (fCachedBounds.IsVoid()) return G4VisExtent(-1, 1, -1, 1, -1, 1);
  Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
  fCachedBounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  return G4VisExtent(xmin, xmax, ymin, ymax, zmin, zmax);
}
```

---

### 2.9 `G4Polyhedron* CreatePolyhedron() const`

**Geant4 semantics:**
Returns a tessellated polyhedron for OpenGL/ROOT visualisation.

**OCCT algorithm:**
`BRepMesh_IncrementalMesh` tessellates the shape.  The resulting
`Poly_Triangulation` on each face is then converted to a `G4Polyhedron`.

```cpp
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <Poly_Triangulation.hxx>
#include <TColgp_Array1OfPnt.hxx>

G4Polyhedron* G4OCCTSolid::CreatePolyhedron() const {
  // Tessellate with a deflection of 0.1 mm
  BRepMesh_IncrementalMesh mesher(fShape, 0.1);
  // TODO: iterate over all faces, collect vertices and triangle indices,
  //       build and return a G4Polyhedron (G4PolyhedronArbitrary or
  //       construct via G4TessellatedSolid and call GetPolyhedron()).
  return nullptr;  // placeholder
}
```

**Implementation note:** `G4Polyhedron` does not have a public constructor
that directly accepts a vertex/face list.  The recommended path is to build
a `G4TessellatedSolid` from the OCCT triangulation and call
`G4TessellatedSolid::GetPolyhedron()`.

---

### 2.10 `std::ostream& StreamInfo(std::ostream& os) const`

**OCCT counterpart:** None.  Dumps a human-readable summary of the solid
and the OCCT shape type (obtained from `fShape.ShapeType()`).

---

### 2.11 `void BoundingLimits(G4ThreeVector& pMin, G4ThreeVector& pMax) const`

**Geant4 semantics:**
Optional virtual that returns tight axis-aligned bounds.  The default
implementation delegates to `GetExtent()`.

**Algorithm — cached at construction:**
Same `fCachedBounds` member as `GetExtent()`.  No OCCT call at query time.

```cpp
void G4OCCTSolid::BoundingLimits(G4ThreeVector& pMin,
                                  G4ThreeVector& pMax) const {
  Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
  fCachedBounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
  pMin = G4ThreeVector(xmin, ymin, zmin);
  pMax = G4ThreeVector(xmax, ymax, zmax);
}
```

---

### 2.12 `G4bool CalculateExtent(const EAxis pAxis, const G4VoxelLimits& pVoxelLimits, const G4AffineTransform& pTransform, G4double& pMin, G4double& pMax) const`

**Geant4 semantics:**
Computes the extent of the (possibly transformed) solid along one axis within
the given voxelisation limits.  Used by the voxel-based smart voxel
optimisation of the navigator.

**OCCT counterpart:**
There is no direct OCCT equivalent.  The function must be implemented by:

1. Transforming `fShape` by `pTransform` using `BRepBuilderAPI_Transform`.
2. Computing the axis-aligned bounding box with `BRepBndLib::Add`.
3. Clipping the result to `pVoxelLimits`.

```cpp
G4bool G4OCCTSolid::CalculateExtent(const EAxis pAxis,
                                     const G4VoxelLimits& pVoxelLimits,
                                     const G4AffineTransform& pTransform,
                                     G4double& pMin, G4double& pMax) const {
  // TODO:
  // 1. Convert pTransform to a gp_Trsf
  // 2. Apply with BRepBuilderAPI_Transform
  // 3. Compute bounding box
  // 4. Extract axis coordinate range and clip to pVoxelLimits
  // Fallback to base-class implementation in the interim:
  return G4VSolid::CalculateExtent(pAxis, pVoxelLimits, pTransform,
                                   pMin, pMax);
}
```

**Implementation note:** `BRepBuilderAPI_Transform` creates a copy of the
shape — avoid calling it during hot navigation loops.  A better strategy is
to pre-transform shape vertices into the navigation frame once and cache the
result.

---

## 3. Summary Table

| G4VSolid function | Primary algorithm | Difficulty |
|---|---|---|
| `Inside` | AABB → inscribed-sphere cache → ray-parity (per-face `IntCurvesFace_Intersector`) → `BRepClass3d_SolidClassifier` fallback | ⬜ High |
| `SurfaceNormal` | `BRepExtrema_DistShapeShape` + `BRepAdaptor_Surface::D1` | ⬜ Medium |
| `DistanceToIn(p, v)` | Per-face `IntCurvesFace_Intersector` loop with `Bnd_Box` prefilter | ⬜ Medium |
| `DistanceToIn(p)` | AABB lower bound (`AABBLowerBound`); exact fallback (`BRepExtrema_DistShapeShape` per face) | ⬜ Medium |
| `DistanceToOut(p, v)` | Per-face `IntCurvesFace_Intersector` loop with `Bnd_Box` prefilter; `TryGetOutwardNormal` for exit normal | ⬜ Medium |
| `DistanceToOut(p)` | Planar shortcut (`PlanarFaceLowerBoundDistance`) or BVH lower bound (`BVHLowerBoundDistance`); exact fallback | ⬜ Medium |
| `GetExtent` / `BoundingLimits` | `fCachedBounds` (computed once at construction) | ⬛ Easy |
| `CreatePolyhedron` | `BRepMesh_IncrementalMesh` + `G4TessellatedSolid` | ⬜ Hard |
| `CalculateExtent` | `BRepBuilderAPI_Transform` + bounding box | ⬜ Hard |
| `StreamInfo` | `TopoDS_Shape::ShapeType()` | ⬛ Easy |
| `GetEntityType` | (constant string) | ⬛ Easy |

---

## 4. Implementation Notes

### 4.1 Tolerance Reconciliation

Geant4 uses `kCarTolerance` (default 1 nm) as the surface half-width.  OCCT
has its own tolerance model (`Precision::Confusion()` ≈ 1 × 10⁻⁷ mm for
STEP files).  The OCCT tolerance for intersection algorithms should be set to
match `kCarTolerance / 2.0` when constructing `IntCurvesFace_ShapeIntersector`
and `BRepClass3d_SolidClassifier`.

### 4.2 Unit Convention

Geant4 uses the CLHEP unit system (mm = 1).  STEP files import with OCCT
typically in mm.  Distances and coordinates passed to OCCT algorithms must be
in the same units as the shape was imported in.  If the shape was scaled
during import, all query points and returned distances must be scaled
consistently.

In addition to unit conventions, the **origin** of the OCCT shape must match
Geant4's expectation that every solid is centered at `(0, 0, 0)` in its local
frame.  See [Reference Position Handling](reference_position.md) for how to
recenter STEP-imported shapes before constructing `G4OCCTSolid`.

### 4.3 Thread Safety

`BRepClass3d_SolidClassifier` and `IntCurvesFace_Intersector` (from OCCT)
are **not thread-safe** — they maintain mutable internal state.  Each Geant4
worker thread must own its own instances.  G4OCCT uses `G4Cache<T>` wrappers
(`fSphereCache`, `fIntersectorCache`) to provide per-thread storage without
locks.

In the common `Inside` path, `BRepClass3d_SolidClassifier` is only reached
as a fallback for degenerate rays (point exactly on an edge or vertex).  The
primary path — inscribed-sphere check followed by the per-face ray-parity
loop — requires only the cached `IntersectorCache`, so the expensive
classifier is rarely constructed.

### 4.4 Performance

The navigation hot path has been progressively optimised across multiple PRs:

* **`Inside`** (PRs #197, #214, #221): A four-stage funnel — AABB reject,
  inscribed-sphere cache, ray-parity with per-face `Bnd_Box` prefilter,
  and a `BRepClass3d_SolidClassifier` fallback only for degenerate rays —
  reduces the common case to pure arithmetic and avoids heavy OCCT calls
  in the vast majority of queries.
* **`DistanceToIn/Out(p, v)`** (PR #215): Replaced
  `IntCurvesFace_ShapeIntersector` (whole-shape, no prefilter) with a
  per-face `IntCurvesFace_Intersector` loop using cached, pre-expanded
  `Bnd_Box` objects for O(1) per-face rejection.
* **`DistanceToIn/Out(p)`** (PRs #209, #210): Replaced
  `BRepExtrema_DistShapeShape` (BRep topology search per call) with a
  BVH-accelerated `PointToMeshDistance` on a pre-built triangle mesh
  (`fTriangleSet`), with a planar-face shortcut for all-planar solids.
* **Bounding boxes** are computed once at construction and cached in
  `fCachedBounds`, eliminating per-call `BRepBndLib::Add` overhead.

Profiling with the navigator benchmark (`bench_navigator`) and Callgrind
reports (see `callgrind-reports-*/`) continues to guide further work.

### 4.5 Shape Validity

Not all `TopoDS_Shape` objects are valid for navigation:

* `TopoDS_Compound` — a group of shapes; does not define an interior/exterior.
* Open shells — `Inside` is undefined.
* Shapes with gaps or overlaps in the boundary.

A shape validation step using `BRepCheck_Analyzer` should be run during
`G4OCCTSolid` construction, with a descriptive error if the shape is not
solid and closed.
