<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

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

**OCCT algorithm:**
`BRepClass3d_SolidClassifier` classifies a 3D point with respect to a solid.

```cpp
#include <BRepClass3d_SolidClassifier.hxx>
#include <gp_Pnt.hxx>

EInside G4OCCTSolid::Inside(const G4ThreeVector& p) const {
  gp_Pnt pnt(p.x(), p.y(), p.z());
  BRepClass3d_SolidClassifier cls(fShape);
  cls.Perform(pnt, kCarTolerance / 2.0);
  switch (cls.State()) {
    case TopAbs_IN:      return kInside;
    case TopAbs_ON:      return kSurface;
    case TopAbs_OUT:     return kOutside;
    default:             return kOutside;
  }
}
```

**Notes:**
* `BRepClass3d_SolidClassifier` requires that `fShape` be a `TopoDS_Solid`
  or a `TopoDS_Shell` that is closed.  Open shells or `TopoDS_Compound`
  shapes will give undefined results; a pre-check should be added.
* The tolerance passed to `Perform` should match Geant4's `kCarTolerance`.

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

**OCCT algorithm:**
`IntCurvesFace_ShapeIntersector` computes intersections between a line (or
ray) and the faces of a shape.

```cpp
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <gp_Lin.hxx>

G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& p,
                                    const G4ThreeVector& v) const {
  gp_Lin ray(gp_Pnt(p.x(), p.y(), p.z()),
             gp_Dir(v.x(), v.y(), v.z()));

  IntCurvesFace_ShapeIntersector intersector;
  intersector.Load(fShape, kCarTolerance);
  // Parametric range: (0, +∞) excludes the current position
  intersector.Perform(ray, kCarTolerance, Precision::Infinite());

  if (!intersector.IsDone() || intersector.NbPnt() == 0)
    return kInfinity;

  // Find the smallest positive parameter (distance along ray)
  G4double minDist = kInfinity;
  for (int i = 1; i <= intersector.NbPnt(); ++i) {
    G4double w = intersector.WParameter(i);
    if (w > kCarTolerance && w < minDist)
      minDist = w;
  }
  return minDist;
}
```

**Notes:**
* The parametric lower bound `kCarTolerance` avoids self-intersection at the
  current surface point.
* `Precision::Infinite()` from OCCT should be replaced with a finite but
  large value consistent with the simulation world size.

---

### 2.4 `G4double DistanceToIn(const G4ThreeVector& p) const`

**Geant4 semantics:**
Returns the *shortest* (perpendicular) distance from external point `p` to
the solid surface.

**OCCT algorithm:**
`BRepExtrema_DistShapeShape` between a vertex at `p` and the shape boundary.

```cpp
#include <BRepExtrema_DistShapeShape.hxx>

G4double G4OCCTSolid::DistanceToIn(const G4ThreeVector& p) const {
  BRep_Builder builder;
  TopoDS_Vertex vertex;
  builder.MakeVertex(vertex, gp_Pnt(p.x(), p.y(), p.z()), kCarTolerance);

  BRepExtrema_DistShapeShape dist(vertex, fShape);
  if (!dist.IsDone()) return kInfinity;
  return dist.Value();
}
```

---

### 2.5 `G4double DistanceToOut(const G4ThreeVector& p, const G4ThreeVector& v, ...) const`

**Geant4 semantics:**
Distance from *internal* point `p` along direction `v` to the first surface
intersection.  If `calcNorm` is true, also fills `*n` with the outward
surface normal at the intersection and sets `*validNorm`.

**OCCT algorithm:**
Same as `DistanceToIn(p, v)` using `IntCurvesFace_ShapeIntersector`, but
interpreting the result for an interior point.

```cpp
G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p,
                                     const G4ThreeVector& v,
                                     const G4bool calcNorm,
                                     G4bool* validNorm,
                                     G4ThreeVector* n) const {
  if (validNorm) *validNorm = false;

  gp_Lin ray(gp_Pnt(p.x(), p.y(), p.z()),
             gp_Dir(v.x(), v.y(), v.z()));

  IntCurvesFace_ShapeIntersector intersector;
  intersector.Load(fShape, kCarTolerance);
  intersector.Perform(ray, kCarTolerance, Precision::Infinite());

  if (!intersector.IsDone() || intersector.NbPnt() == 0)
    return 0.0;  // defensive: interior point should always hit the boundary

  G4double minDist = kInfinity;
  int minIdx = -1;
  for (int i = 1; i <= intersector.NbPnt(); ++i) {
    G4double w = intersector.WParameter(i);
    if (w > kCarTolerance && w < minDist) {
      minDist = w;
      minIdx  = i;
    }
  }

  if (calcNorm && validNorm && n && minIdx > 0) {
    gp_Pnt hitPnt = intersector.Pnt(minIdx);
    G4ThreeVector hp(hitPnt.X(), hitPnt.Y(), hitPnt.Z());
    *n        = SurfaceNormal(hp);
    *validNorm = true;
  }

  return (minDist < kInfinity) ? minDist : 0.0;
}
```

---

### 2.6 `G4double DistanceToOut(const G4ThreeVector& p) const`

**Geant4 semantics:**
Shortest distance from *internal* point `p` to the surface.

**OCCT algorithm:**
Same as `DistanceToIn(p)` — `BRepExtrema_DistShapeShape` between a vertex
at `p` and the shape.  Since `p` is inside, this gives the inward distance.

```cpp
G4double G4OCCTSolid::DistanceToOut(const G4ThreeVector& p) const {
  BRep_Builder builder;
  TopoDS_Vertex vertex;
  builder.MakeVertex(vertex, gp_Pnt(p.x(), p.y(), p.z()), kCarTolerance);
  BRepExtrema_DistShapeShape dist(vertex, fShape);
  if (!dist.IsDone()) return 0.0;
  return dist.Value();
}
```

---

### 2.7 `G4GeometryType GetEntityType() const`

**OCCT counterpart:** None needed.  Returns a fixed string `"G4OCCTSolid"`.

---

### 2.8 `G4VisExtent GetExtent() const`

**Geant4 semantics:**
Returns an axis-aligned bounding box sufficient for visualisation.

**OCCT algorithm:**
`BRepBndLib::Add` fills a `Bnd_Box` with the tight axis-aligned bounding box.

```cpp
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

G4VisExtent G4OCCTSolid::GetExtent() const {
  Bnd_Box bbox;
  BRepBndLib::Add(fShape, bbox);
  if (bbox.IsVoid()) return G4VisExtent(-1, 1, -1, 1, -1, 1);

  Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
  bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
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

**OCCT algorithm:** Same as `GetExtent()` — use `BRepBndLib::Add`.

```cpp
void G4OCCTSolid::BoundingLimits(G4ThreeVector& pMin,
                                  G4ThreeVector& pMax) const {
  Bnd_Box bbox;
  BRepBndLib::Add(fShape, bbox);
  Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
  bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
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

| G4VSolid function | OCCT algorithm | Difficulty |
|---|---|---|
| `Inside` | `BRepClass3d_SolidClassifier` | ⬜ Medium |
| `SurfaceNormal` | `BRepExtrema_DistShapeShape` + `BRepAdaptor_Surface::D1` | ⬜ Medium |
| `DistanceToIn(p, v)` | `IntCurvesFace_ShapeIntersector` | ⬜ Medium |
| `DistanceToIn(p)` | `BRepExtrema_DistShapeShape` | ⬛ Easy |
| `DistanceToOut(p, v)` | `IntCurvesFace_ShapeIntersector` | ⬜ Medium |
| `DistanceToOut(p)` | `BRepExtrema_DistShapeShape` | ⬛ Easy |
| `GetExtent` / `BoundingLimits` | `BRepBndLib::Add` → `Bnd_Box` | ⬛ Easy |
| `CreatePolyhedron` | `BRepMesh_IncrementalMesh` + `G4TessellatedSolid` | 🔲 Hard |
| `CalculateExtent` | `BRepBuilderAPI_Transform` + bounding box | 🔲 Hard |
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

`IntCurvesFace_ShapeIntersector` and `BRepClass3d_SolidClassifier` are
**not thread-safe** — they maintain mutable internal state.  Each thread
(Geant4 worker thread) must maintain its own instances.  Use
`G4Cache<IntCurvesFace_ShapeIntersector>` or `thread_local` storage.

### 4.4 Performance

The OCCT BRep algorithms are general-purpose and may be significantly slower
than Geant4's analytic solid implementations for simple shapes (boxes,
spheres, tubes).  Profiling with the navigator benchmark (`bench_navigator`)
is essential before release.  Caching the `BRepClass3d_SolidClassifier` and
`IntCurvesFace_ShapeIntersector` objects (not re-constructing them per call)
is the primary optimisation opportunity.

### 4.5 Shape Validity

Not all `TopoDS_Shape` objects are valid for navigation:
* `TopoDS_Compound` — a group of shapes; does not define an interior/exterior.
* Open shells — `Inside` is undefined.
* Shapes with gaps or overlaps in the boundary.

A shape validation step using `BRepCheck_Analyzer` should be run during
`G4OCCTSolid` construction, with a descriptive error if the shape is not
solid and closed.
