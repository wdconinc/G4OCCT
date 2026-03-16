<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

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

### 3.2 Cached-bounds early reject

Problem:
A point that is clearly outside the cached axis-aligned bounds still reaches
the OCCT classifier today.

Opportunity:
For `Inside(p)`, reject points outside `fCachedBounds` by more than the current
tolerance before calling `BRepClass3d_SolidClassifier`.

Scope:

- `Inside()`
- potentially `DistanceToIn(p)` as a very cheap first-stage decision input

Expected impact:

- large win for benchmark/query sets sampled from a bounding box,
- especially useful for sparse or elongated solids where most sampled points
  are outside.

### 3.3 Prepared-query contexts

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

### 4.1 Multi-stage `Inside()` classification

Instead of a single exact OCCT classifier call for every surviving point:

1. coarse AABB reject,
2. optional tighter coarse volume reject/accept,
3. exact OCCT classification only for unresolved points.

Possible middle stages:

- oriented bounding box or principal-axis box,
- convex half-space checks for imported convex polyhedra,
- per-fixture or per-shape signed-distance approximations with exact fallback.

Risk:
Any non-exact accept path must be proven safe near boundaries.

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

### 4.3 Better ray front-ends for `DistanceToIn/Out`

The intersector is currently the main exact engine.  For complex solids, a
generic spatial acceleration front-end could reduce face visits before the
exact OCCT call:

- OCCT BVH if a stable/public entry point is available,
- a project-owned AABB tree over faces,
- cached ray-entry candidate lists for repeated origins.

This is likely the highest-payoff area for large imported assemblies and
complex fixtures.

---

## 5. Longer-Term Opportunities

### 5.1 Primitive and canonical-shape detection

For STEP fixtures that are known imports of simple primitives, we can consider
dispatching to equivalent analytic checks when the imported representation is
recognized unambiguously.

Examples:

- boxes,
- spheres/orbs,
- full cylinders/tubes,
- cones/frusta,
- tori.

Potential strategy:

- record canonical shape metadata at import time, or
- detect exact/topology-preserving primitive signatures once and cache them.

Trade-off:
High upside for very common shapes, but only worthwhile if detection is robust
and maintenance cost stays low.

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
2. Add and validate cheap exact rejects (`Inside()` AABB reject).
3. Evaluate prepared-query contexts for small repeated savings.
4. Explore batch helpers for benchmark/validation internals.
5. Prototype a shared spatial acceleration front-end for ray queries.
6. Revisit primitive detection only after generic accelerations are exhausted.

---

## 8. Candidate PR Breakdown

This note suggests a natural decomposition into reviewable PRs:

- benchmark-only warm-up changes,
- exact coarse rejects using cached bounds,
- benchmark/reporting improvements for first-call vs steady-state visibility,
- internal batch helpers,
- ray-acceleration experiments behind isolated helper APIs,
- visualisation/mesh caching changes.

That breakdown keeps correctness review manageable while still allowing a
larger long-term optimization strategy.
