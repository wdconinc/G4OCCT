<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

# G4OCCT — Performance Considerations

This document discusses the performance characteristics of G4OCCT, with
particular attention to the cost of instantiating OCCT algorithm objects
and the ability to run in a multi-threaded Geant4 environment.  The first
two caching optimisations outlined later in this document (Section 7 —
Optimisation Roadmap) —
per-thread caching of `BRepClass3d_SolidClassifier` (PR #45) and
`IntCurvesFace_ShapeIntersector` (PR #47) via `G4Cache` — have been
implemented and are live on `main`.

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

### 2.1 `Inside` — `BRepClass3d_SolidClassifier`

```cpp
// Current implementation (G4OCCTSolid.cc) — cached via G4Cache
BRepClass3d_SolidClassifier& classifier = GetOrCreateClassifier();  // per-thread cache
classifier.Perform(ToPoint(p), IntersectionTolerance());
```

`BRepClass3d_SolidClassifier` requires a one-time shape-level preparation
(face triangulation look-up, winding analysis) before it can answer queries.
This O(N_faces) cost is now paid only once per thread; subsequent calls on the
same thread go directly to `Perform`, making the classifier step O(1) per call
after the first.  If `SetOCCTShape()` is called, the generation counter is
incremented and each thread lazily rebuilds its classifier on its next
navigation call (threads that never call these methods again pay no cost).

### 2.2 `DistanceToIn(p, v)` and `DistanceToOut(p, v)` — `IntCurvesFace_ShapeIntersector`

```cpp
// Current implementation (G4OCCTSolid.cc) — cached via G4Cache
IntCurvesFace_ShapeIntersector& intersector = GetOrCreateIntersector();  // per-thread cache
intersector.Perform(ray, IntersectionTolerance(), Precision::Infinite());
```

`Load` builds an internal face-interference data structure over all faces of
the shape.  This O(N_faces) cost is now paid only once per thread; each
subsequent ray query on the same thread goes directly to `Perform`.  If
`SetOCCTShape()` is called, the generation counter is incremented and each
thread lazily rebuilds its intersector on its next navigation call.

### 2.3 `DistanceToIn(p)` — Classification Then Distance Query

```cpp
BRepClass3d_SolidClassifier& classifier = GetOrCreateClassifier();  // per-thread cache
classifier.Perform(ToPoint(p), IntersectionTolerance());
if (/* outside */) {
  BRepExtrema_DistShapeShape distance(vertex, fShape); // ← second object, still uncached
}
```

`DistanceToIn(p)` uses the per-thread cached `BRepClass3d_SolidClassifier`
(via `GetOrCreateClassifier()`) to check whether the point is already inside.
The classifier step is O(1) per call after the first on each thread.  The
subsequent `BRepExtrema_DistShapeShape` for the distance computation remains
uncached and still processes the full shape at construction time.

### 2.4 `DistanceToOut(p)` — Per-Face Distance Queries

```cpp
for (TopExp_Explorer explorer(fShape, TopAbs_FACE); explorer.More(); explorer.Next()) {
  BRepExtrema_DistShapeShape distance(vertex, explorer.Current());
}
```

A separate `BRepExtrema_DistShapeShape` object is constructed for every
face of the shape.  This is O(N_faces) object constructions per call,
each with its own internal setup cost.

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
(`BRepClass3d_SolidClassifier`, `IntCurvesFace_ShapeIntersector`,
`BRepExtrema_DistShapeShape`) maintain **mutable internal state** during
`Perform` / evaluation calls.  They must **not** be shared between threads.

The current implementation maintains a per-thread cache of each algorithm
object via `G4Cache` (see §3.3).  Each thread owns its own
`BRepClass3d_SolidClassifier` and `IntCurvesFace_ShapeIntersector`, so there
is no shared mutable state and thread safety is maintained.  The O(N_faces)
initialisation cost is paid only once per thread per shape rather than on
every navigation call, eliminating the per-call construction overhead described
in Section 2 for `Inside`, `DistanceToIn(p)`, `DistanceToIn(p, v)`, and
`DistanceToOut(p, v)`.

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
#include <IntCurvesFace_ShapeIntersector.hxx>

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
    std::optional<IntCurvesFace_ShapeIntersector> intersector;
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
// Proposed addition to G4OCCTSolid constructor
G4OCCTSolid::G4OCCTSolid(const G4String& name, const TopoDS_Shape& shape)
    : G4VSolid(name), fShape(shape),
      fBounds(ComputeAxisAlignedBounds(shape)) {}
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

| Function | Native Geant4 | G4OCCTSolid (current) | G4OCCTSolid (remaining overhead) |
|---|---|---|---|
| `Inside` | O(1) analytic | O(1) classify (O(N_faces) load amortised per thread) | — |
| `DistanceToIn(p, v)` | O(1) analytic | O(N_faces) ray test (load amortised per thread) | O(N_faces) ray test |
| `DistanceToOut(p, v)` | O(1) analytic | O(N_faces) ray test (load amortised per thread) | O(N_faces) ray test |
| `DistanceToIn(p)` | O(1) analytic | O(1) classify + O(N_faces) distance | O(N_faces) `BRepExtrema_DistShapeShape` uncached |
| `DistanceToOut(p)` | O(1) analytic | O(N_faces) per-face distance objects | O(N_faces) single distance object (planned) |
| `BoundingLimits` | O(1) stored | O(N_faces) BBox recomputed | O(1) cached BBox (planned) |

> **Note:** The classifier and intersector construction costs are amortised via per-thread `G4Cache`
> caches (§3.3).  Remaining items in the last column are unimplemented optimisations.

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
measures geantino tracking throughput for both a native Geant4 geometry
and an equivalent `G4OCCTSolid`-wrapped geometry:

```
./bench_navigator [N_geantinos]   # default: 10000
```

It reports wall-clock time and the native-to-OCCT speed ratio.  This
benchmark should be run:

* Before and after changes to navigation methods that are expected to
  impact performance (new caching, algorithm substitutions, etc.).
* On representative shapes from the target use case (not only the
  primitive box/sphere/cylinder in the current benchmark).

> **Note:** The benchmark is not registered as a CTest test.  Run it
> manually after a release build (`-DCMAKE_BUILD_TYPE=Release`).

---

## 7. Optimisation Roadmap

The optimisations below are listed in approximate priority order.  Each
item references the relevant section of this document.

| Priority | Optimisation | Status | Gain | Section |
|---|---|---|---|---|
| **1** | Cache `BRepClass3d_SolidClassifier` per thread via `G4Cache` | ✅ Implemented (PR #45) | Eliminates O(N_faces) construction in `Inside` and `DistanceToIn(p)`; one-time O(N_faces) load amortised across all calls on the same thread | §3.3 |
| **2** | Cache `IntCurvesFace_ShapeIntersector` per thread via `G4Cache` | ✅ Implemented (PR #47) | Eliminates `Load` cost in `DistanceToIn(p,v)` and `DistanceToOut(p,v)`; one-time O(N_faces) load amortised across all calls on the same thread | §3.3 |
| **3** | Cache axis-aligned bounding box at construction | Planned | Eliminates `BRepBndLib::Add` in every `BoundingLimits`/`GetExtent` call | §4 |
| **4** | Rewrite `DistanceToOut(p)` to use a single `BRepExtrema_DistShapeShape` against the full shape | Planned | Reduces O(N_faces) object constructions to one | §2.4 |
| **5** | Cache `G4Polyhedron` in `CreatePolyhedron` | Planned | Avoids repeated full tessellation on visualisation refreshes | §2.7 |
| **6** | Evaluate BVH / AABB-tree front-end for ray–shape intersection | Planned | May reduce O(N_faces) ray test to O(log N_faces) for complex solids | §5 |
