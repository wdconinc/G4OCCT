<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

# Low-Level Optimization Design Notes

This note collects implementation-level optimization opportunities for
`G4OCCTSolid` and related benchmarking/reporting code.  It is broader than
`Inside()` alone: the same themes apply to ray queries, safety distances,
surface normals, visualisation support, and benchmark methodology.

The goal is not to prematurely micro-optimize every path.  The goal is to
identify optimizations that:

- reduce steady-state cost on hot navigation paths,
- keep behavior identical to Geant4/OCCT semantics,
- preserve explicit error handling and reproducible benchmarking, and
- scale from trivial primitives to large imported STEP solids.

---

## 1. Current Hot Paths

At a high level, imported-solid work is dominated by repeated OCCT calls on
the same immutable `TopoDS_Shape`:

- `Inside(p)` uses `BRepClass3d_SolidClassifier`
- `DistanceToIn(p, v)` / `DistanceToOut(p, v)` use
  `IntCurvesFace_ShapeIntersector`
- `DistanceToIn(p)` / `DistanceToOut(p)` use closest-distance queries
- `SurfaceNormal(p)` uses closest-face lookup plus local surface evaluation
- `CreatePolyhedron()` converts the BRep into a Geant4 display mesh

The existing per-thread caches for the classifier and intersector already
remove one-time `Load(...)` costs from most repeated queries.  Remaining work
is mainly about reducing the number of expensive OCCT calls, improving coarse
rejects, and separating setup cost from steady-state timing.

---

## 2. Principles

Any low-level optimization should follow these rules:

1. **Cheap reject before expensive exact test.**
   Use cached scalar/vector metadata first; use OCCT only when necessary.

2. **Steady-state timing should exclude one-time setup.**
   Benchmarks should not charge the first timed query for lazy cache creation.

3. **Preserve exact behavior at the boundary.**
   Tolerance handling must remain conservative; fast paths should only return
   an exact answer when it is provably safe.

4. **Prefer shape-agnostic accelerations before special-case rewrites.**
   Generic improvements help imported STEP solids and fixture primitives alike.

5. **Only add primitive-specific fast paths when the representation is stable
   and the dispatch is unambiguous.**

---

## 3. Near-Term Opportunities

### 3.1 Warm one-time OCCT setup outside timed loops

> **Addressed in benchmark infrastructure.**

Problem:
The first `Inside()` or ray query on a thread pays for lazy OCCT cache setup.
That is a valid runtime cost, but it obscures steady-state benchmark numbers.

Opportunity:
Warm the relevant path before timing starts in benchmark/test code.

Scope:

- `Inside()` benchmark: one untimed imported `Inside()` call before the timed
  loop
- Ray benchmark: optionally one untimed imported ray query before timing

Expected impact:

- cleaner benchmark numbers,
- easier comparison between native and imported steady-state behavior,
- no behavioral change in production navigation.

### 3.2 Cached-bounds early reject — ✅ Implemented

`Inside(p)` now rejects points outside `fCachedBounds` before any OCCT call.
The same prefilter applies to `DistanceToIn(p,v)` and `DistanceToOut(p,v)` via
`Bnd_Box::IsOut(ray)` on tolerance-expanded AABBs, pruning faces that cannot
possibly intersect the query ray.

~~Problem:~~
~~A point that is clearly outside the cached axis-aligned bounds still reaches~~
~~the OCCT classifier today.~~

~~Opportunity:~~
~~For `Inside(p)`, reject points outside `fCachedBounds` by more than the current~~
~~tolerance before calling `BRepClass3d_SolidClassifier`.~~

~~Scope:~~

- ~~`Inside()`~~
- ~~potentially `DistanceToIn(p)` as a very cheap first-stage decision input~~

~~Expected impact:~~

- ~~large win for benchmark/query sets sampled from a bounding box,~~
- ~~especially useful for sparse or elongated solids where most sampled points~~
  ~~are outside.~~

### 3.3 Direct ray-plane fast path for planar faces — 🔄 In-flight (PR #233)

For planar faces whose boundary consists entirely of straight-line edges, a
`uvPolygon` (outer wire vertices projected into UV space) is precomputed at
construction time.  The hot-path helper `RayPlaneFaceHit()` then performs an
explicit ray-plane intersection followed by a 2D point-in-polygon test,
bypassing `IntCurvesFace_Intersector::Perform` entirely.

Benefits:

- eliminates `TopExp_Explorer`, `BRepTopAdaptor_FClass2d`, and
  `NCollection_Sequence` overhead on every call,
- expected to remove ~50 % of remaining navigation instructions for
  all-box geometries (B4c benchmark),
- falls back to the generic `IntCurvesFace_Intersector` path for non-planar or
  curved-edge faces.

### 3.4 Prepared-query contexts

Problem:
Thread-local cache entries currently hold the OCCT algorithm object only.

Opportunity:
Extend cached state to hold additional precomputed query data:

- expanded bounds with tolerance padding,
- precomputed center / characteristic scale,
- face-count-based heuristics for selecting fallback strategies.

Expected impact:

- fewer repeated scalar computations,
- cleaner place to evolve future fast paths.

---

## 4. Medium-Term Opportunities

### 4.1 Multi-stage `Inside()` classification — ✅ Implemented (PRs #197, #214, #221)

`Inside()` now uses a four-stage pipeline:

1. **AABB coarse reject** — points outside `fCachedBounds` immediately return `kOutside`.
2. **Inscribed-sphere fast path** — adaptive per-thread `G4Cache<SphereCacheData>` holding
   up to 64 inscribed spheres grown from `DistanceToOut(p)` results; accepts interior
   points or rejects clearly exterior points without any OCCT call.
3. **Ray-parity test** — per-face `IntCurvesFace_Intersector` in the +Z direction with
   per-face bbox prefilter; handles the vast majority of surviving points.
4. **`BRepClass3d_SolidClassifier` fallback** — used only for degenerate rays that
   produce ambiguous parity results.

~~Instead of a single exact OCCT classifier call for every surviving point:~~

~~1. coarse AABB reject,~~
~~2. optional tighter coarse volume reject/accept,~~
~~3. exact OCCT classification only for unresolved points.~~

~~Possible middle stages:~~

~~- oriented bounding box or principal-axis box,~~
~~- convex half-space checks for imported convex polyhedra,~~
~~- per-fixture or per-shape signed-distance approximations with exact fallback.~~

~~Risk:~~
~~Any non-exact accept path must be proven safe near boundaries.~~

### 4.2 Bulk-query APIs

Problem:
Benchmarks and validation often classify many points independently.

Opportunity:
Add internal batch helpers for:

- `Inside()` on vectors of points,
- ray intersections on vectors of directions,
- safety distances on vectors of positions.

Potential benefits:

- amortize call overhead,
- reuse temporary storage,
- open the door to SIMD-friendly prefilters.

This does **not** require changing the public Geant4 virtual interface; the
batch API can remain an internal helper used by tests/benchmarks.

### 4.3 Better ray front-ends for `DistanceToIn/Out` — ✅ Implemented (PR #215)

Per-face `IntCurvesFace_Intersector` instances replace the monolithic
`IntCurvesFace_ShapeIntersector`, eliminating `NCollection_Sequence` heap
allocation per call.  Each intersector is stored in a per-thread
`G4Cache<IntersectorCache>::faceIntersectors` and filtered by its per-face
tolerance-expanded AABB (`Bnd_Box::IsOut(ray)`) before `Perform` is called,
so faces that cannot intersect the ray are skipped without any OCCT arithmetic.

~~The intersector is currently the main exact engine.  For complex solids, a~~
~~generic spatial acceleration front-end could reduce face visits before the~~
~~exact OCCT call:~~

~~- OCCT BVH if a stable/public entry point is available,~~
~~- a project-owned AABB tree over faces,~~
~~- cached ray-entry candidate lists for repeated origins.~~

~~This is likely the highest-payoff area for large imported assemblies and~~
~~complex fixtures.~~

### 4.4 BVH-accelerated safety distance bounds — ✅ Implemented (PRs #209, #210, #222)

#### Implementation summary

`PointToMeshDistance` BVH traversal over `fTriangleSet` (`BRepExtrema_TriangleSet`)
is built at construction time and used for every safety query.  The result is
corrected by the linear deflection bound `fBVHDeflection` and clamped:
`max(0, mesh_distance − fBVHDeflection)`.  An AABB lower bound is computed as a
cheap first-stage check before entering BVH traversal.

For all-planar solids, `PlanarFaceLowerBoundDistance()` provides an exact
plane-distance lower bound (PR #222), eliminating the deflection correction on
that class of geometry.

#### Background: what Geant4 requires

The isotropic safety methods `DistanceToIn(p)` and `DistanceToOut(p)` need only
return a **conservative lower bound** (never an overestimate) of the true
distance from `p` to the nearest surface.  Formally, the returned value `s`
must satisfy:

```
0 ≤ s ≤ true minimum distance to nearest surface
```

Geant4 never assumes the returned value is exact; any non-negative value
satisfying the inequality above is valid.  This opens the door to fast
lower-bound algorithms that are much cheaper than exact OCCT distance queries.

#### Current implementation and its cost

`DistanceToIn(p)` currently calls `BRepExtrema_DistShapeShape(vertex, shape)`,
which recurses through all subshapes and invokes exact analytic extrema solvers
(curve/surface projection) for each face.

`DistanceToOut(p)` is even more expensive: it iterates over every face
explicitly and calls `BRepExtrema_DistShapeShape(vertex, face)` per face,
giving O(F) exact projection calls where F is the face count.

Both paths are dominated by analytic projection cost and offer no spatial
pruning.  On a 500-face imported solid, this can mean hundreds of exact
curve/surface solvers firing per safety query.

#### OCCT facilities for fast lower bounds

OCCT provides several layers of distance machinery that are faster and
still give valid lower bounds:

**Tier 0 — Axis-aligned bounding box (O(1) after setup)**

The distance from a point to an AABB is always ≤ the distance to any geometry
it contains.  A point already inside the box gives distance 0 to the box, which
is still a valid underestimate.  OCCT provides this via
`BVH_Tools<double,3>::PointBoxSquareDistance`:

```cpp
#include <BVH_Tools.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

Bnd_Box aabb;
BRepBndLib::Add(shape, aabb);
double xmin, ymin, zmin, xmax, ymax, zmax;
aabb.Get(xmin, ymin, zmin, xmax, ymax, zmax);

// Later, per query (O(1)):
BVH_Vec3d pt(p.x(), p.y(), p.z());
BVH_Vec3d cmin(xmin, ymin, zmin), cmax(xmax, ymax, zmax);
double sqLowerBound = BVH_Tools<double,3>::PointBoxSquareDistance(pt, cmin, cmax);
// sqrt(sqLowerBound) is a valid conservative safety distance
```

`Bnd_Sphere` (from `<Bnd_Sphere.hxx>`) additionally gives a tight
**enclosing sphere** and exposes both a minimum and maximum distance
to a query point in one call.  The minimum is a lower bound; the maximum
enables branch pruning in BVH traversal:

```cpp
double minDist, maxDist;
sphere.Distances(gp_XYZ(p.x(), p.y(), p.z()), minDist, maxDist);
```

**Tier 1 — BVH over tessellation triangles (O(log T) after setup)**

OCCT's `BRepExtrema_TriangleSet` builds a BVH over the triangulated faces of a
shape.  Once built (O(T log T) where T is the triangle count), each query
traverses the BVH using:

- `BVH_Tools::PointBoxSquareDistance` at interior nodes to prune branches
  whose nearest possible triangle is already farther than the current best,
- `BVH_Tools::PointTriangleSquareDistance` at leaf nodes for the exact
  distance to each triangle.

The result is the exact distance to the tessellation.  However, this is **not**
automatically a valid lower bound on the true analytical surface distance.  On
convex surface patches, chordal facets sit inside the true surface, so the
distance from an exterior point to the triangulated mesh can be *larger* than
the distance to the actual surface — an overestimate that would violate Geant4
safety semantics.

To obtain a conservative lower bound, the mesh distance must be reduced by a
proven deflection bound `δ` (the Hausdorff distance between the analytical
surface and its tessellation, bounded by the mesher's linear deflection
parameter) and then clamped to zero:

```
s = max(0, mesh_distance − δ)
```

When `BRepMesh_IncrementalMesh` is invoked with linear deflection `d`, the
OCCT default guarantees `δ ≤ d`.  Setting `d` equal to
`IntersectionTolerance()` (= 0.5 × `G4GeometryTolerance::GetInstance()->GetSurfaceTolerance()`,
as used in `src/G4OCCTSolid.cc`) ensures the correction stays at tolerance
scale and the result remains a valid lower bound.

The framework for this traversal already exists in OCCT's own code:
`BRepExtrema_ProximityDistTool` (`src/ModelingAlgorithms/TKTopAlgo/BRepExtrema/`)
is a concrete subclass of `BVH_Distance<double,3,BVH_Vec3d,BRepExtrema_TriangleSet>`
and its `RejectNode` / `Accept` implementations (`BRepExtrema_ProximityDistTool.cxx`
lines 177–230) serve as a direct template.  A G4OCCT helper can follow the same
pattern:

```cpp
#include <BRepExtrema_TriangleSet.hxx>
#include <BVH_Distance.hxx>
#include <BVH_Tools.hxx>

class PointToMeshDistance
    : public BVH_Distance<double, 3, BVH_Vec3d, BRepExtrema_TriangleSet>
{
public:
  // Prune a BVH branch if its box is already farther than current best
  bool RejectNode(const BVH_Vec3d& theCornerMin,
                  const BVH_Vec3d& theCornerMax,
                  double&          theMetric) const override
  {
    theMetric = std::sqrt(
      BVH_Tools<double,3>::PointBoxSquareDistance(myObject, theCornerMin, theCornerMax));
    return theMetric > myDistance;
  }

  // Compute exact distance from query point to one triangle
  bool Accept(const int theIndex, const double&) override
  {
    BVH_Vec3d v0, v1, v2;
    mySet->GetVertices(theIndex, v0, v1, v2);
    double sq = BVH_Tools<double,3>::PointTriangleSquareDistance(myObject, v0, v1, v2);
    double d  = std::sqrt(sq);
    if (d < myDistance)
      myDistance = d;
    return true;
  }

private:
  Handle(BRepExtrema_TriangleSet) mySet;
};
```

Setup (once per solid, stored in `G4OCCTSolid`):

```cpp
NCollection_Vector<TopoDS_Shape> faces;
for (TopExp_Explorer e(fShape, TopAbs_FACE); e.More(); e.Next())
  faces.Append(e.Current());
fTriangleSet = new BRepExtrema_TriangleSet(faces);  // builds BVH
```

Query (per safety call):

```cpp
PointToMeshDistance solver;
solver.SetObject(BVH_Vec3d(p.x(), p.y(), p.z()));
solver.SetBVHSet(fTriangleSet.get());
double dist = solver.ComputeDistance();
// Apply deflection correction to ensure a strict lower bound:
// subtract the linear deflection used during BRepMesh_IncrementalMesh
// (here: IntersectionTolerance()) and clamp to zero.
double delta = IntersectionTolerance();
double safetyLowerBound = std::max(0.0, dist - delta);
```

**Tier 2 — Precomputed signed distance field (O(1) queries)**

For fixed shapes that are queried millions of times,
`BVH_DistanceField<double,3>` (from `<BVH_DistanceField.hxx>`) precomputes a
regular voxel grid of distances.  When constructed with `theComputeSign = true`
it stores negative values inside and positive values outside, giving a signed
safety distance with O(1) lookup (trilinear interpolation).

The trade-offs are:

- accuracy bounded by voxel resolution,
- memory cost proportional to grid size,
- no incremental update when geometry changes.

This tier is most attractive for large simulation campaigns with many identical
copies of the same solid.

#### Correctness guarantee

A valid Geant4 safety lower bound `s` must satisfy `0 ≤ s ≤ true distance`.
The mesh-BVH approach produces a conservative lower bound when the deflection
correction described above is applied:

```
s = max(0, mesh_distance − δ)    where δ ≤ linear deflection parameter d
```

In G4OCCT, the reference deflection is `IntersectionTolerance()` = 0.5 ×
`G4GeometryTolerance::GetInstance()->GetSurfaceTolerance()` (see
`src/G4OCCTSolid.cc`).  The safety validation fixture (`fixture_safety_compare.cc`)
accepts a mismatch between native and imported solids if it satisfies:

```
|native − imported| ≤ max(G4GeometryTolerance::GetInstance()->GetSurfaceTolerance(),
                          0.01 × max(native, imported))
```

Tier-0 bounds (AABB and enclosing sphere) are always conservative: they derive
purely from bounding geometry and require no deflection correction.  Exact
classification or ray queries should still be used for `Inside(p)` and the
directional variants.

#### Summary of safety-distance approach by tier

| Tier | Method | Setup | Per-query | Lower bound? |
|------|--------|-------|-----------|--------------|
| 0 | AABB (`BVH_Tools::PointBoxSquareDistance`) | O(F) once | O(1) | Yes |
| 0 | Enclosing sphere (`Bnd_Sphere`) | O(F) once | O(1) | Yes (min) |
| 1 | Mesh BVH (`BRepExtrema_TriangleSet`) | O(T log T) once | O(log T) | Yes (with δ correction) |
| 2 | Distance field (`BVH_DistanceField`) | O(T·G³) once | O(1) | Yes (approx) |
| — | Current exact (`BRepExtrema_DistShapeShape`) | none | O(F·exact) | Exact |

F = face count, T = triangle count, G = voxel grid dimension per axis.

The recommended near-term implementation is a two-stage approach: use the AABB
as a fast first-stage lower bound (especially for `DistanceToIn(p)` where
distant exterior points are common), then fall back to the mesh-BVH for points
that survive the AABB test.

---

## 5. Longer-Term Opportunities

### 5.1 Primitive and canonical-shape detection — **won't do**

Users who need simple primitive shapes (boxes, spheres, cylinders, cones,
tori, …) should use native Geant4 primitives directly. Native Geant4 primitives use
closed-form analytic solvers and deliver the best possible navigation
performance for those shapes.

Attempting to detect and dispatch to Geant4 primitives inside `G4OCCTSolid` is
not pursued for several reasons:

- **Correctness and consistency**: STEP representations of "a box" do not carry
  a canonical tag; recognition heuristics based on topology counts or bounding
  geometry can produce false positives on non-primitive shapes.
- **Scalability**: The maintenance burden grows with each new primitive type
  supported, and OCCT's STEP reader may represent the same shape differently
  across versions.
- **Dispatch complexity**: Reliably routing an incoming query to the right
  analytic back-end without ambiguity is harder than it appears and adds code
  that is difficult to test exhaustively.

If high performance on simple shapes is the goal, the recommended approach is
to model those volumes as native Geant4 solids and reserve `G4OCCTSolid` for
complex imported geometry that has no analytic equivalent.

### 5.2 Shared acceleration structures across multiple query types

Today, classification, ray tests, normals, and distances each lean on their
own OCCT machinery.  A shared intermediate representation could help several
paths at once:

- face bounding boxes,
- tessellated/BVH hybrid structures,
- adjacency data for fast local normal lookup after ray hits.

This is a bigger architectural step, but it may be the only path to
substantial gains on large imported solids.

### 5.3 Visualization and mesh reuse

`CreatePolyhedron()` and related visual/report paths are not part of transport
navigation, but they matter for CI and diagnostics.

Opportunities:

- cache generated `G4Polyhedron`,
- reuse triangulations between reporting and visualisation code,
- persist fixture render intermediates where safe.

---

## 6. Benchmarking and Measurement Guidance

Optimization work should be measured in layers:

### 6.1 Separate setup from steady-state

Report at least:

- first-call cost,
- steady-state per-query cost,
- total batch cost.

### 6.2 Use representative fixture families

Do not optimize only against boxes and spheres.  Track at minimum:

- direct primitives,
- profile-faceted shapes,
- twisted/swept shapes,
- boolean compounds,
- large imported multi-face solids when available.

### 6.3 Track correctness alongside speed

Every optimization PR should confirm:

- no new classification mismatches,
- no new ray mismatches,
- no tolerance regressions on boundary-heavy fixtures.

---

## 7. Suggested Roadmap

1. Keep setup cost out of benchmark timing.
2. ✅ Add and validate cheap exact rejects (`Inside()` AABB reject).
3. ✅ Evaluate prepared-query contexts for small repeated savings.
4. ✅ Replace per-face `BRepExtrema_DistShapeShape` loops in `DistanceToOut(p)` and
   `DistanceToIn(p)` with mesh-BVH lower bounds (see §4.4).
5. Explore batch helpers for benchmark/validation internals.
6. Prototype a shared spatial acceleration front-end for ray queries.
7. For simple shapes, prefer native Geant4 primitives; primitive detection inside `G4OCCTSolid` is not pursued (see §5.1).

---

## 8. Candidate PR Breakdown

This note suggests a natural decomposition into reviewable PRs:

- benchmark-only warm-up changes,
- exact coarse rejects using cached bounds,
- benchmark/reporting improvements for first-call vs steady-state visibility,
- mesh-BVH safety distance: add `BRepExtrema_TriangleSet` setup in
  `G4OCCTSolid`, implement `PointToMeshDistance` traversal helper, replace
  `DistanceToOut(p)` face-iteration loop, replace `DistanceToIn(p)` whole-shape
  extrema call — validate against existing safety fixture tests,
- internal batch helpers,
- ray-acceleration experiments behind isolated helper APIs,
- visualisation/mesh caching changes.

That breakdown keeps correctness review manageable while still allowing a
larger long-term optimization strategy.
