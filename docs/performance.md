<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

# G4OCCT — Performance Considerations

This document discusses the performance characteristics of G4OCCT, with
particular attention to the cost of instantiating OCCT algorithm objects,
the ability to run in a multi-threaded Geant4 environment, and planned
optimisations.

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

The current implementation constructs and initialises OCCT algorithm
objects on every navigation call.  Several of these initialisations are
expensive because they pre-process the `TopoDS_Shape` internally.

### 2.1 `Inside` — `BRepClass3d_SolidClassifier`

```cpp
// Current implementation (G4OCCTSolid.cc)
BRepClass3d_SolidClassifier classifier(fShape);  // ← built every call
classifier.Perform(ToPoint(p), IntersectionTolerance());
```

`BRepClass3d_SolidClassifier` constructed with a `TopoDS_Shape` argument
performs a one-time shape-level preparation (face triangulation look-up,
winding analysis) during construction.  This preparation cost is O(N_faces)
and is wasted when the classifier is discarded at the end of the call.

### 2.2 `DistanceToIn(p, v)` and `DistanceToOut(p, v)` — `IntCurvesFace_ShapeIntersector`

```cpp
IntCurvesFace_ShapeIntersector intersector;
intersector.Load(fShape, tolerance);   // ← rebuilds internal index every call
intersector.Perform(ray, …);
```

`Load` builds an internal face-interference data structure over all faces
of the shape.  For a shape with N faces this is O(N) work that is
discarded after a single ray query.

### 2.3 `DistanceToIn(p)` — Double Classification

```cpp
BRepClass3d_SolidClassifier classifier(fShape);  // ← first classifier
classifier.Perform(ToPoint(p), IntersectionTolerance());
if (/* outside */) {
  BRepExtrema_DistShapeShape distance(vertex, fShape); // ← second object
}
```

`DistanceToIn(p)` currently constructs a `BRepClass3d_SolidClassifier` to
check whether the point is already inside, then constructs a separate
`BRepExtrema_DistShapeShape` for the distance computation.  Both objects
process the full shape at construction time.

### 2.4 `DistanceToOut(p)` — Per-Face Distance Queries

```cpp
for (TopExp_Explorer explorer(fShape, TopAbs_FACE); …) {
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

The current implementation creates a new algorithm object on every call
and discards it afterwards, so there is no shared mutable state and the
implementation is already thread-safe.  However, this safety comes at the
full cost of per-call construction described in Section 2.

### 3.3 Thread-Local Caching with `G4Cache`

Geant4 provides `G4Cache<T>` (in `G4Cache.hh`) as the idiomatic mechanism
for storing per-thread auxiliary data alongside a geometry object that is
itself shared across threads.  This is the preferred path for caching
algorithm objects:

```cpp
// In G4OCCTSolid.hh
#include <G4Cache.hh>
#include <BRepClass3d_SolidClassifier.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>

class G4OCCTSolid : public G4VSolid {
  // ...
 private:
  TopoDS_Shape fShape;

  // Per-thread cached algorithm objects (not yet implemented)
  mutable G4Cache<BRepClass3d_SolidClassifier> fClassifierCache;
  mutable G4Cache<IntCurvesFace_ShapeIntersector> fIntersectorCache;
};
```

Each worker thread would initialise its cache entry on first use:

```cpp
EInside G4OCCTSolid::Inside(const G4ThreeVector& p) const {
  BRepClass3d_SolidClassifier& cls = fClassifierCache.Get();
  if (cls.IsNull()) {       // first call on this thread
    cls.Load(fShape);       // prepares shape data once per thread
  }
  cls.Perform(ToPoint(p), IntersectionTolerance());
  return ToG4Inside(cls.State());
}
```

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

| Function | Native Geant4 | G4OCCTSolid (current) | G4OCCTSolid (with caching) |
|---|---|---|---|
| `Inside` | O(1) analytic | O(N_faces) classifier construction | O(1) classify after O(N_faces) one-time load |
| `DistanceToIn(p, v)` | O(1) analytic | O(N_faces) intersector load + O(N_faces) ray test | O(N_faces) ray test (load amortised) |
| `DistanceToOut(p, v)` | O(1) analytic | O(N_faces) intersector load + O(N_faces) ray test | O(N_faces) ray test (load amortised) |
| `DistanceToIn(p)` | O(1) analytic | O(N_faces) classifier + O(N_faces) distance | O(N_faces) classify + O(N_faces) distance |
| `DistanceToOut(p)` | O(1) analytic | O(N_faces²) per-face distance objects | O(N_faces) single distance object |
| `BoundingLimits` | O(1) stored | O(N_faces) BBox recomputed | O(1) cached BBox |

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

## 7. Planned Optimisations

The optimisations below are listed in approximate priority order.  Each
item references the relevant section of this document.

| Priority | Optimisation | Expected gain | Section |
|---|---|---|---|
| **1** | Cache `BRepClass3d_SolidClassifier` per thread via `G4Cache` | Eliminates O(N_faces) construction in `Inside` and `DistanceToIn(p)` | §3.3 |
| **2** | Cache `IntCurvesFace_ShapeIntersector` per thread via `G4Cache` | Eliminates `Load` cost in `DistanceToIn(p,v)` and `DistanceToOut(p,v)` | §3.3 |
| **3** | Cache axis-aligned bounding box at construction | Eliminates `BRepBndLib::Add` in every `BoundingLimits`/`GetExtent` call | §4 |
| **4** | Rewrite `DistanceToOut(p)` to use a single `BRepExtrema_DistShapeShape` against the full shape | Reduces O(N_faces) object constructions to one | §2.4 |
| **5** | Cache `G4Polyhedron` in `CreatePolyhedron` | Avoids repeated full tessellation on visualisation refreshes | §2.7 |
| **6** | Evaluate BVH / AABB-tree front-end for ray–shape intersection | May reduce O(N_faces) ray test to O(log N_faces) for complex solids | §5 |

