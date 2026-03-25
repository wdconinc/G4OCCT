<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

# G4OCCT — Performance Considerations

This document discusses the performance characteristics of G4OCCT, with
particular attention to the cost of instantiating OCCT algorithm objects
and the ability to run in a multi-threaded Geant4 environment.  Multiple
optimisations described in §7 (Optimisation Roadmap) — including
per-thread caching via `G4Cache`, a multi-stage `Inside` pipeline with
inscribed-sphere fast path and ray-parity test, per-face intersector loops
with AABB prefilter, BVH safety-distance traversal, and plane-distance
lower bounds for all-planar solids — have been implemented and are live
on `main`.

---

## 1. Background

Geant4 navigation calls `G4VSolid` methods repeatedly and on the
critical path of every particle-tracking step.  In a typical simulation
the navigator calls the following methods in a tight loop for every active
step:

| Call sequence per step | Typical multiplicity |
|---|---|
| `Inside(p)` | 1 per `LocateGlobalPointAndSetup` |
| `DistanceToOut(p, v)` | 1 per step (particle inside a volume) |
| `DistanceToIn(p, v)` | 1 per neighbouring-volume check |
| `BoundingLimits` | Once per volume (voxelisation, cached by Geant4) |

For a geometry with many placed copies of a `G4OCCTSolid`, these calls
can number in the tens of millions per second across all worker threads.
Any per-call overhead that can be eliminated through caching has a
direct impact on simulation throughput.

---

## 2. Cost of Per-Call Algorithm Object Instantiation

Several OCCT algorithm objects require expensive O(N_faces) initialisation
from the shape before they can answer queries.  The sections below describe
how each is handled in the current implementation.

### 2.1 `Inside` — multi-stage pipeline (PRs #197, #214, #221)

`Inside(p)` now follows a four-stage pipeline that avoids the full
`BRepClass3d_SolidClassifier` call for the vast majority of queries:

```cpp
// Stage 1 — AABB early reject
if (fCachedBounds.IsOut(ToPoint(p)))
  return kOutside;

// Stage 2 — adaptive inscribed-sphere fast path (per-thread G4Cache, up to 64 spheres)
SphereCacheData& sc = fSphereCache.Get();
for (const auto& sphere : sc.spheres) {
  if (p.distance(sphere.centre) < sphere.radius)
    return kInside;                 // no OCCT call at all
}

// Stage 3 — ray-parity test (+Z ray, per-face IntCurvesFace_Intersector
//           with Bnd_Box::IsOut pre-filter and RayPlaneFaceHit fast path)
auto [crossings, degenerateRay] = CountRayCrossings(p);  // returns {count, degeneracy flag}
if (crossings > 0 && !degenerateRay)
  return (crossings % 2 == 1) ? kInside : kOutside;

// Stage 4 — BRepClass3d_SolidClassifier fallback (zero crossings or degenerate ray)
BRepClass3d_SolidClassifier& classifier = GetOrCreateClassifier();  // per-thread cache
classifier.Perform(ToPoint(p), IntersectionTolerance());
```

**Stage 1** tests the cached AABB and returns `kOutside` with a single
bounds check.  **Stage 2** checks up to 64 per-thread inscribed spheres
(seeded at construction and grown by `DistanceToOut(p)` results); a hit
returns `kInside` without any OCCT call.  **Stage 3** fires a +Z ray,
prefiltering each face with `Bnd_Box::IsOut` on tolerance-expanded per-face
AABBs and using `RayPlaneFaceHit` for planar faces; crossing parity gives
the result.  **Stage 4** (`BRepClass3d_SolidClassifier`) is reached when the parity count
is unreliable: either when no interior (`TopAbs_IN`) crossings are found — the
ray may have passed entirely through shared edges or vertices — or when a
`TopAbs_ON` hit is encountered, skipping that crossing would alter the effective
parity and could misclassify the point.  The per-thread `ClassifierCache` is
therefore a fallback rather than the primary path.

### 2.2 `DistanceToIn(p, v)` and `DistanceToOut(p, v)` — per-face intersector loop (PR #215)

The previous implementation used `IntCurvesFace_ShapeIntersector::Perform`,
which heap-allocates an `NCollection_Sequence` per call and has no per-face
bounding-box prefilter.  The current implementation replaces this with a
per-face `IntCurvesFace_Intersector` loop stored in `G4Cache<IntersectorCache>`:

```cpp
// IntersectorCache stores one intersector and one expanded Bnd_Box per face
struct IntersectorCache {
  std::uint64_t generation{std::numeric_limits<std::uint64_t>::max()};
  std::vector<std::unique_ptr<IntCurvesFace_Intersector>> faceIntersectors;
  std::vector<Bnd_Box> expandedBoxes;  // tolerance-expanded per-face AABBs
};
```

The per-ray loop is:

```cpp
IntersectorCache& ic = GetOrCreateIntersectorCache();  // per-thread cache
for (std::size_t i = 0; i < fFaceBoundsCache.size(); ++i) {
  if (ic.expandedBoxes[i].IsOut(ray))
    continue;                          // AABB prefilter — no OCCT call
  auto& fi = *ic.faceIntersectors[i];
  fi.Perform(ray, tol, Precision::Infinite());
  // collect hits …
}
```

`faceIntersectors[i]` is pre-loaded for each face once per thread (O(N_faces)
setup amortised across all calls).  `Bnd_Box::IsOut` eliminates faces that
cannot be hit before any intersector call, and no `NCollection_Sequence` is
heap-allocated per ray.

### 2.3 `DistanceToIn(p)` — AABB lower bound with exact fallback (PRs #209, #210)

```cpp
// Tier-0: AABB lower bound — O(1), no OCCT call
const G4double aabbDist = AABBLowerBound(p);
if (aabbDist > IntersectionTolerance())
  return aabbDist;  // conservative lower bound: point is clearly outside

// Fallback: exact distance (points near or inside the AABB)
return ExactDistanceToIn(p);  // classifier check + TryFindClosestFace per thread
```

`AABBLowerBound(p)` computes the Euclidean distance from `p` to the cached
axis-aligned bounding box in O(1) arithmetic with no OCCT call.  If the point
is clearly outside the AABB (distance greater than the intersection tolerance)
this bound is returned directly.  For points near or inside the AABB,
`ExactDistanceToIn(p)` uses the per-thread `BRepClass3d_SolidClassifier` to
detect inside/surface points (returning 0.0) and then `TryFindClosestFace` to
find the exact surface distance via per-face `BRepExtrema_DistShapeShape`.

### 2.4 `DistanceToOut(p)` — BVH and plane-distance lower bounds (PRs #209, #210, #222)

For general solids (with curved faces) the BVH triangle-set traversal
`BVHLowerBoundDistance(p)` provides a conservative lower bound on
`DistanceToOut(p)` in O(log T), clamped non-negative by `std::max(0.0, meshDist - fBVHDeflection)`.
For solids where all faces are planar (`fAllFacesPlanar == true`), the exact
plane-distance `gp_Pln::Distance(pt)` over all faces gives a correct lower
bound in O(N_faces) with no BRep heap allocation at all.  When `fAllFacesPlanar`
is true, every `FaceBounds::plane` optional is guaranteed to be engaged (set
at construction time), so `value()` access is safe.  Both branches fall back
to `ExactDistanceToOut(p)` when the lower bound cannot be computed:

```cpp
if (fAllFacesPlanar) {
  // Plane-distance path — all-planar solids only (PR #222)
  G4double minDist = kInfinity;
  for (const auto& fb : fFaceBoundsCache) {
    minDist = std::min(minDist, fb.plane.value().Distance(ToPoint(p)));
  }
  return (minDist < kInfinity) ? minDist : ExactDistanceToOut(p);
}
// General path — BVH triangle-set lower bound (non-negative, clamped)
const G4double bvhDist = BVHLowerBoundDistance(p);
return (bvhDist < kInfinity) ? bvhDist : ExactDistanceToOut(p);
```

`PlanarFaceLowerBoundDistance()` encapsulates the all-planar branch.  Each
`FaceBounds` entry caches a `std::optional<gp_Pln>` precomputed at
construction for planar faces:

```cpp
struct FaceBounds {
  TopoDS_Face face;
  Bnd_Box box;
  BRepAdaptor_Surface adaptor;
  std::optional<gp_Pln> plane;  // precomputed plane for planar faces
};
```

### 2.5 `SurfaceNormal` — Distance Plus Projection

```cpp
BRepExtrema_DistShapeShape distance(vertex, fShape);  // ← closest-face search
GeomAPI_ProjectPointOnSurf projection(pLocal, surface);  // ← UV projection
```

Two algorithm objects are constructed per call.  `BRepExtrema_DistShapeShape`
against the full shape is O(N_faces); `GeomAPI_ProjectPointOnSurf` is
O(degree) in the surface polynomial degree.

### 2.6 `BoundingLimits` / `GetExtent` — Repeated BBox Computation

```cpp
Bnd_Box boundingBox;
BRepBndLib::Add(shape, boundingBox);  // ← called every time
```

The axis-aligned bounding box is a fixed property of the shape.  It is
currently recomputed on every call rather than cached at construction time.

### 2.7 `CreatePolyhedron` — Incremental Mesh

```cpp
BRepMesh_IncrementalMesh mesher(fShape, kRelativeDeflection, Standard_True);
```

`CreatePolyhedron` is called infrequently (visualisation only), but it
is the most expensive single operation: the mesher tessellates the entire
shape.  The resulting `G4Polyhedron` is not retained, so repeated calls
(e.g. from a visualisation refresh) repeat the full tessellation.

---

## 3. Thread Safety and Concurrent Execution

### 3.1 Geant4 Multi-Threading Model

Geant4 runs simulation with one master thread and N worker threads
(configured via `G4MTRunManager`).  The geometry is constructed once on
the master thread and then shared as read-only data across all worker
threads.  Every `G4VSolid` method is called concurrently from multiple
worker threads with different query points.

`G4OCCTSolid` stores only the `TopoDS_Shape` as member data.
`TopoDS_Shape` objects are reference-counted handles to an underlying
`TopoDS_TShape`; the handle itself is safe to copy across threads, and
the `TopoDS_TShape` is not mutated during navigation queries.

### 3.2 OCCT Algorithm Objects Are Not Thread-Safe

The OCCT algorithm objects used for navigation
(`BRepClass3d_SolidClassifier`, `IntCurvesFace_Intersector`,
`BRepExtrema_DistShapeShape`) maintain **mutable internal state** during
`Perform` / evaluation calls.  They must **not** be shared between threads.

The current implementation maintains a per-thread cache of each algorithm
object via `G4Cache` (see §3.3).  Each thread owns its own
`BRepClass3d_SolidClassifier` and a set of `IntCurvesFace_Intersector`
objects (one per face), so there is no shared mutable state and thread
safety is maintained.  The O(N_faces) initialisation cost is paid only
once per thread per shape rather than on every navigation call, eliminating
the per-call construction overhead described in Section 2 for `Inside`,
`DistanceToIn(p)`, `DistanceToIn(p, v)`, and `DistanceToOut(p, v)`.

### 3.3 Thread-Local Caching with `G4Cache`

Geant4 provides `G4Cache<T>` (in `G4Cache.hh`) as the idiomatic mechanism
for storing per-thread auxiliary data alongside a geometry object that is
itself shared across threads.  Both `fClassifierCache` and `fIntersectorCache`
are implemented using this mechanism.

The actual implementation uses a generation-counter pattern so that
`SetOCCTShape()` can mark all per-thread caches as stale: it atomically
increments `fShapeGeneration`, and each thread lazily rebuilds its cached
object on its next navigation call.  Note that `SetOCCTShape()` also mutates
`fShape` directly, so it must not be called concurrently with any ongoing
navigation (see the note in the header).

```cpp
// In G4OCCTSolid.hh
#include <G4Cache.hh>
#include <BRepClass3d_SolidClassifier.hxx>
#include <IntCurvesFace_Intersector.hxx>

class G4OCCTSolid : public G4VSolid {
  // ...
 private:
  TopoDS_Shape fShape;

  struct ClassifierCache {
    std::uint64_t generation{std::numeric_limits<std::uint64_t>::max()};
    std::optional<BRepClass3d_SolidClassifier> classifier;
  };

  struct IntersectorCache {
    std::uint64_t generation{std::numeric_limits<std::uint64_t>::max()};
    std::vector<std::unique_ptr<IntCurvesFace_Intersector>> faceIntersectors;
    std::vector<Bnd_Box> expandedBoxes;  // tolerance-expanded per-face AABBs
  };

  std::atomic<std::uint64_t> fShapeGeneration{0};
  mutable G4Cache<ClassifierCache> fClassifierCache;
  mutable G4Cache<IntersectorCache> fIntersectorCache;
};
```

Each worker thread initialises its cache entry on first use and reloads it
whenever the shape generation has changed:

```cpp
BRepClass3d_SolidClassifier& G4OCCTSolid::GetOrCreateClassifier() const {
  ClassifierCache& cache = fClassifierCache.Get();
  const std::uint64_t currentGen = fShapeGeneration.load(std::memory_order_acquire);
  if (cache.generation != currentGen) {
    cache.classifier.emplace();
    cache.classifier->Load(fShape);   // O(N_faces) — paid once per thread per shape
    cache.generation = currentGen;
  }
  return *cache.classifier;
}
```

`SetOCCTShape()` uses `fShapeGeneration.fetch_add(1, std::memory_order_release)`
to increment the counter, causing all threads to lazily reload their caches on
their next navigation call.  The initial `generation` value of
`std::numeric_limits<std::uint64_t>::max()` can never match generation 0, so
the first call from each thread always builds the object.

An equivalent approach using C++17 `thread_local` variables works when a
single `G4OCCTSolid` instance is involved, but does not scale correctly
when multiple solids are in use simultaneously (each needs its own
pre-loaded state per thread).  `G4Cache<T>` is therefore preferred.

### 3.4 Copy Constructor Requirement

`G4Cache<T>` serialises the construction of the cached object.  The
`G4OCCTSolid` copy constructor (used by Geant4 during geometry
construction) must correctly duplicate the `fShape` handle.  Copying a
`TopoDS_Shape` is O(1) (reference increment); the cached algorithm
objects must be left uninitialised in the copy so that each thread
initialises them from the new solid's shape.

---

## 4. Bounding-Box Caching

The axis-aligned bounding box of a `TopoDS_Shape` is immutable after
construction.  Computing it once and storing the result eliminates the
repeated `BRepBndLib::Add` calls from `BoundingLimits`, `GetExtent`, and
`CalculateExtent`:

```cpp
// Current implementation — fCachedBounds is set inside ComputeBounds()
G4OCCTSolid::G4OCCTSolid(const G4String& name, const TopoDS_Shape& shape)
    : G4VSolid(name), fShape(shape) {
  ComputeBounds();  // calls BRepBndLib::AddOptimal → fCachedBounds, fFaceBoundsCache, ...
}
```

`CalculateExtent` is called by the voxel smart navigator during geometry
initialisation (before any worker threads are launched), so this is safe
to compute on the master thread.

---

## 5. Comparison with Native Geant4 Analytic Solids

Native Geant4 solids (`G4Box`, `G4Tubs`, `G4Sphere`, etc.) implement
navigation using closed-form algebraic expressions evaluated in O(1)
arithmetic operations with no heap allocation.

OCCT BRep algorithms are general-purpose and operate on arbitrary surface
geometry.  They carry a fixed overhead proportional to the number of faces
and the degree of the surface parameterisation.  The table below summarises
the qualitative difference:

| Function | Native Geant4 | G4OCCTSolid (current) | Notes |
|---|---|---|---|
| `Inside` | O(1) analytic | O(1) sphere hit / O(N_faces) ray-parity / O(N_faces) classifier fallback | Sphere and AABB fast paths avoid OCCT calls for most queries |
| `DistanceToIn(p, v)` | O(1) analytic | O(N_faces) per-face ray test with AABB prefilter (amortised per thread) | AABB prefilter prunes most faces; no per-call heap allocation |
| `DistanceToOut(p, v)` | O(1) analytic | O(N_faces) per-face ray test with AABB prefilter (amortised per thread) | Same per-face intersector loop as DTI |
| `DistanceToIn(p)` | O(1) analytic | O(1) AABB lower bound; O(N_faces) exact fallback | AABB check avoids OCCT for points clearly outside; exact path uses per-thread classifier + closest-face search |
| `DistanceToOut(p)` | O(1) analytic | O(N_faces) plane-distance (all-planar) or O(log T) BVH lower bound | Plane path: no BRep allocation; BVH path: conservative lower bound |
| `BoundingLimits` | O(1) stored | O(1) cached AABB | `fCachedBounds` computed once at construction |

> **Note:** O(N_faces) per-face ray tests in `DistanceToIn/Out(p,v)` have
> their setup cost (loading per-face intersectors) amortised via per-thread
> `G4Cache<IntersectorCache>` (§3.3).  The AABB prefilter (`Bnd_Box::IsOut`)
> eliminates faces that cannot be hit before any intersector call, giving
> practical sub-linear behaviour for convex solids.

For shapes with small face counts (≤ ~20 faces) the amortised per-thread
cost of OCCT algorithms is expected to be within a small constant factor
of native solids.  For shapes derived from complex STEP solids with
hundreds or thousands of faces, the O(N_faces) cost per ray intersection
will dominate and performance will be significantly lower than native
equivalents.  This is a fundamental characteristic of general BRep-based
navigation, not a fixable deficiency.

---

## 6. Benchmarking

The `bench_navigator` executable (built with `-DBUILD_BENCHMARKS=ON`)
benchmarks and validates all navigator-critical `G4VSolid` methods across
every fixture registered in the repository manifest.  For each fixture it
builds both a native Geant4 solid and an equivalent `G4OCCTSolid`-wrapped
shape, runs each method against a set of deterministically generated test
points, reports per-method wall-clock time and the imported-to-native speed
ratio (how many times slower the imported solid is), and flags any correctness
mismatches between the two solids.

```
./bench_navigator [ray_count [manifest_path [point_cloud_dir]]]
#                  default: 2048
```

The following methods are benchmarked in a single pass:

| Method | Description |
|---|---|
| `DistanceToIn/Out(p, v)` | Directional ray intersection and distance |
| Exit normals | `DistanceToOut(p, v, calcNorm=true)` exit-normal validation |
| `Inside(p)` | Point-in-solid classification |
| `DistanceToIn(p)` | Isotropic safety distance from outside points |
| `DistanceToOut(p)` | Isotropic safety distance from inside points |
| `SurfaceNormal(p)` | Surface normal at agreed ray hit points |

Most rows in the per-fixture output show native time, imported time, the
imported-to-native speed ratio, and the number of correctness mismatches.
The "Exit normals" row is an exception: exit normals are validated as part of
`DistanceToOut(p, v)` and carry no separate timing entry (those columns show
`---`).  An aggregate summary table across all fixtures is printed at the end.

This benchmark should be run:

* Before and after changes to navigation methods that are expected to
  impact performance (new caching, algorithm substitutions, etc.).
* On representative shapes from the target use case (complex STEP-derived
  solids with many faces, not only simple primitives).

> **Note:** The benchmark is not registered as a CTest test.  Run it
> manually after a release build (`-DCMAKE_BUILD_TYPE=Release`).

---

## 7. Optimisation Roadmap

The optimisations below are listed in approximate priority order.  Each
item references the relevant section of this document.

| Priority | Optimisation | Status | Gain | Section |
|---|---|---|---|---|
| **1** | Cache `BRepClass3d_SolidClassifier` per thread via `G4Cache` | ✅ Done (PR #45) | Eliminates O(N_faces) construction in `Inside` and `DistanceToIn(p)`; one-time O(N_faces) load amortised across all calls on the same thread | §3.3 |
| **2** | Cache `IntCurvesFace_ShapeIntersector` per thread via `G4Cache` | ✅ Done (PR #47) | Eliminates `Load` cost in `DistanceToIn(p,v)` and `DistanceToOut(p,v)`; superseded by per-face intersector loop (PR #215) | §3.3 |
| **3** | Cache axis-aligned bounding box at construction | ✅ Done | Eliminates `BRepBndLib::Add` in every `BoundingLimits`/`GetExtent` call; `fCachedBounds` used as AABB early-reject in `Inside` | §4 |
| **4** | Adaptive inscribed-sphere fast path for `Inside` | ✅ Done (PRs #197, #214) | Per-thread cache of up to 64 inscribed spheres; sphere hit returns `kInside` with no OCCT call | §2.1 |
| **5** | Ray-parity test for `Inside` with per-face AABB prefilter | ✅ Done (PR #221) | Replaces `BRepClass3d_SolidClassifier` as primary path; classifier fallback only for zero crossings or degenerate rays | §2.1 |
| **6** | Per-face `IntCurvesFace_Intersector` loop with AABB prefilter for DTI/DTO(p,v) | ✅ Done (PR #215) | Eliminates `NCollection_Sequence` heap allocation; AABB prefilter prunes non-hit faces before intersector call | §2.2 |
| **7** | BVH triangle-set lower bound for `DistanceToOut(p)` (mixed-face solids) | ✅ Done (PRs #209, #210) | O(log T) BVH traversal replaces O(N_faces) `BRepExtrema_DistShapeShape` for non-all-planar solids; `DistanceToIn(p)` uses O(1) AABB lower bound instead | §2.3, §2.4 |
| **8** | Plane-distance lower bound for all-planar solids (`DistanceToOut(p)`) | ✅ Done (PR #222) | Exact O(N_faces) plane-distance with no BRep heap allocation for `fAllFacesPlanar` solids | §2.4 |
| **9** | Cache `G4Polyhedron` in `CreatePolyhedron` | Planned | Avoids repeated full tessellation on visualisation refreshes | §2.7 |
| **10** | Direct ray-plane fast path for planar faces in DTI/DTO(p,v) | 🚧 In-flight (PR #233) | Replaces `IntCurvesFace_Intersector::Perform` for all-straight-edge planar faces; algebraic ray–plane intersection with no OCCT call | §2.2 |
| **11** | SIMD-accelerated AABB prefilter via `FaceBoundsSOA` | ✅ Done (this PR) | AVX2 4-wide double batch AABB test replaces scalar `Bnd_Box::IsOut` per face in `Inside`, `DistanceToIn/Out(p,v)`; AVX2 FMA `MinPlaneDistance` replaces scalar loop in `SurfaceNormal` and `PlanarFaceLowerBoundDistance`; 2–37× speedup on polyhedral and planar solids (geomean: inside 2.0×, rays 2.7×, safety 2.5×) | §8 |

---

## 8. SIMD AABB Prefilter (`FaceBoundsSOA`)

### Motivation

The scalar per-face AABB prefilter (`Bnd_Box::IsOut`) is the dominant cost
in `Inside(p)` and `DistanceToIn/Out(p,v)` for solids with many faces.
Each call tests one AABB against a ray or point, loading six scalars from
heap-allocated `Bnd_Box` objects scattered in memory.

### Implementation

`FaceBoundsSOA` (see `include/G4OCCT/FaceBoundsSOA.hh`) stores the same
bounding-box data in Struct-of-Arrays layout:

```
fXmin[0..N], fXmax[0..N]   — 32-byte aligned
fYmin[0..N], fYmax[0..N]
fZmin[0..N], fZmax[0..N]
fPlaneA[0..N], fPlaneB[0..N], fPlaneC[0..N], fPlaneD[0..N]
```

Arrays are padded to a multiple of 4 (AVX2 double lane width) so SIMD
loops need no tail handling.

Three high-level methods hide all SIMD from call sites:

| Method | Used in | SIMD kernel |
|---|---|---|
| `RayZPassFilter(px, py, out[])` | `Inside(p)` parity stage | AVX2 4-wide `(px∈[xmin,xmax]) & (py∈[ymin,ymax])` |
| `RayPassFilter(ray, out[])` | `DistanceToIn/Out(p,v)` | AVX2 4-wide slab test; scalar fallback for axis-aligned rays |
| `MinPlaneDistance(px,py,pz)` | `SurfaceNormal`, `PlanarFaceLowerBoundDistance` | AVX2 4-wide FMA `‖A·p+D‖`; horizontal min reduction |

The ISA level is selected at compile time via `USE_SIMD` (CMake option, ON
by default): `FaceBoundsSOA.cc` is compiled with `-mavx2 -mfma` when
available, and uses `#ifdef G4OCCT_HAVE_AVX2` guards internally.  The build
falls back to an auto-vectorisable scalar implementation when AVX2 is
absent or `USE_SIMD=OFF`.

### Benchmark results (Intel Core i7-10510U, Release build, 2048 points/rays)

The gains are largest for solids whose AABB prefilter dominates (polyhedral
and all-planar solids with few faces); complex curved solids remain bottlenecked
by `IntCurvesFace_Intersector::Perform`.

| Solid category | Inside speedup | Rays speedup | Safety speedup |
|---|---|---|---|
| All-planar compound (box union/subtraction) | 11–14× | 9–12× | 20–37× |
| Direct polyhedral primitives (box, TRD, trap, para) | 10–13× | 7–11× | 3–18× |
| Tessellated solids | 1.7× | 10× | 2.4× |
| Curved primitives (tubs, sphere, cons, torus) | 1.2–1.4× | 1.3–1.6× | 2–3× |
| Complex curved/swept solids | ≈1× | ≈1× | ≈1× |
| **Geometric mean over all fixtures** | **2.0×** | **2.7×** | **2.5×** |
