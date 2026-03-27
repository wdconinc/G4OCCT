<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

# G4OCCT — Geometry Class Mapping: Geant4 ↔ OCCT

This document discusses the parallels between the Geant4 geometry class
hierarchy and corresponding entities in the OCCT (Open CASCADE Technology)
framework, and analyses how they can best be connected.

---

## 1. Overview of the Two Frameworks

### 1.1 Geant4 Geometry Hierarchy

Geant4 organises geometry through three distinct levels:

| Class | Role |
|---|---|
| `G4VSolid` | Pure geometric shape; answers navigation queries (Inside, DistanceToIn/Out) |
| `G4LogicalVolume` | Associates a solid with a material, sensitive detector, and field |
| `G4VPhysicalVolume` (`G4PVPlacement`) | Places a logical volume at a position/rotation inside a mother volume |

Navigation is performed by `G4Navigator`, which walks a tree of nested
physical volumes and delegates geometry queries to the `G4VSolid` at each
level.

### 1.2 OCCT Topology Hierarchy (BRep)

OCCT represents geometry as Boundary Representation (BRep) shapes organised
in the `TopoDS` topology hierarchy:

| Class | Role |
|---|---|
| `TopoDS_Shape` | Root topology type; may be any of the sub-types below |
| `TopoDS_Solid` | A closed shell — a volume enclosed by faces |
| `TopoDS_Shell` | A connected set of faces (open or closed) |
| `TopoDS_Face` | A surface patch bounded by edges |
| `TopoDS_Compound` | An unordered collection of shapes (assembly) |
| `TopoDS_CompSolid` | An ordered collection of solids sharing faces |

Geometry (curves, surfaces) is separated from topology:

| Class | Role |
|---|---|
| `Geom_Surface` | Parametric surface (plane, cylinder, B-spline, …) |
| `Geom_Curve` | Parametric curve |
| `gp_Trsf` | Rigid-body transformation (rotation + translation) |
| `TopLoc_Location` | Placement (wraps a `gp_Trsf`) attached to a `TopoDS_Shape` |

### 1.3 OCCT XDE (Extended Data Exchange)

The OCCT XDE layer (`TDocStd_Document`, `XCAFDoc_*`) adds semantic
information on top of BRep:

| XDE concept | Geant4 counterpart |
|---|---|
| Shape label | `G4VPhysicalVolume` instance |
| Shape reference | Copy / replica |
| Colour attribute | Visualisation attributes |
| Material attribute | `G4Material` (partial mapping) |
| Name attribute | Volume name |
| Assembly | `G4AssemblyVolume` |

---

## 2. Correspondence Table

| Geant4 | OCCT | Notes |
|---|---|---|
| `G4VSolid` | `TopoDS_Shape` (specifically `TopoDS_Solid`) | The shape is the geometry; navigation queries map to BRep algorithms |
| `G4LogicalVolume` | XDE shape label with material + name attributes | Logical volumes have no direct BRep equivalent; XDE provides the closest mapping |
| `G4VPhysicalVolume` / `G4PVPlacement` | `TopoDS_Shape` + `TopLoc_Location` | A placed shape = a shape with a transformation |
| `G4AssemblyVolume` | `TopoDS_Compound` | Both group sub-volumes without imposing a mother solid |
| `G4RotationMatrix` + `G4ThreeVector` | `gp_Trsf` (rotation + translation) | One-to-one mapping; conversion is straightforward |
| `G4Navigator` | `BRepClass3d_SolidClassifier`, `IntCurvesFace_ShapeIntersector` | Navigation queries implemented via OCCT algorithms |

---

## 3. Connection Strategies

Three strategies are available for connecting the two frameworks.  They are
not mutually exclusive; the chosen strategy may vary by use case.

### 3.1 Inheritance / Subclassing

**Description:** Create G4OCCT subclasses that inherit from the Geant4 base
classes and embed an OCCT object.

| G4OCCT class | Inherits from | Embeds |
|---|---|---|
| `G4OCCTSolid` | `G4VSolid` | `TopoDS_Shape` |
| `G4OCCTLogicalVolume` | `G4LogicalVolume` | `TopoDS_Shape` (optional) |
| `G4OCCTPlacement` | `G4PVPlacement` | `TopLoc_Location` |

**Advantages:**

* Drop-in replacement — a `G4OCCTSolid*` can be passed anywhere a `G4VSolid*`
  is expected without changes to the Geant4 kernel.
* The Geant4 navigator calls virtual methods on `G4OCCTSolid`, which delegate
  to OCCT algorithms.  No navigator modification is needed.

**Disadvantages:**

* `G4LogicalVolume` is a concrete, non-virtual-destructor class in some
  Geant4 versions, so subclassing requires care.
* The OCCT shape is carried redundantly alongside the `G4VSolid` pointer in
  `G4LogicalVolume`.

**Current approach in G4OCCT v0.1.**

---

### 3.2 Embedding (Composition)

**Description:** The G4OCCT classes hold pointers to *both* a Geant4 object
and an OCCT object without inheriting from either framework.

```
G4OCCTGeometry {
  G4LogicalVolume*  g4Volume;
  TopoDS_Shape      occtShape;
  TopLoc_Location   placement;
}
```

**Advantages:**

* Clean separation of concerns.
* Easier to unit-test OCCT-side logic independently.
* No risk of violating Geant4 class invariants.

**Disadvantages:**

* Requires an adapter layer to feed results back to Geant4 navigation — the
  navigator cannot call virtual methods on `G4OCCTGeometry` directly.
* More boilerplate; user must register the `G4VSolid` with the logical volume
  separately.

---

### 3.3 Pointer-Based Bridge (Observer Pattern)

**Description:** A lightweight bridge object holds a `TopoDS_Shape*` and a
`G4VSolid*`.  A registry maps Geant4 volume IDs to OCCT shapes.

```
G4OCCTRegistry {
  std::map<G4int, TopoDS_Shape>   solidMap;
  std::map<G4int, TopLoc_Location> locationMap;
}
```

**Advantages:**

* Non-intrusive — existing Geant4 geometry code is unchanged.
* Can be used to annotate an existing Geant4 geometry with OCCT shapes
  without rebuilding it.

**Disadvantages:**

* Navigation still uses Geant4 native algorithms unless the `G4VSolid` is
  replaced with a `G4OCCTSolid`.
* Registry synchronisation (keeping map up to date during construction /
  deletion) adds complexity.

---

## 4. Navigation Query Mapping

| Geant4 query | OCCT algorithm |
|---|---|
| `Inside(p)` | `BRepClass3d_SolidClassifier::Perform(shape, pnt, tol)` |
| `SurfaceNormal(p)` | `BRepGProp_Face` on the closest face; `gp_Vec` normal |
| `DistanceToIn(p, v)` | `IntCurvesFace_ShapeIntersector` ray–shape intersection |
| `DistanceToIn(p)` | `BRepExtrema_DistShapeShape` (point to shell) |
| `DistanceToOut(p, v)` | `IntCurvesFace_ShapeIntersector` from interior |
| `DistanceToOut(p)` | `BRepExtrema_DistShapeShape` (interior point to faces) |
| `GetExtent()` | `BRepBndLib::Add` → `Bnd_Box` |
| `CreatePolyhedron()` | `BRepMesh_IncrementalMesh` + tessellation extraction |

---

## 5. Transformation Conversion

Geant4 and OCCT use the same mathematical representation for rigid-body
placements; conversion is straightforward:

```cpp
// Geant4 → OCCT
gp_Trsf ToOCCT(const G4RotationMatrix& rot, const G4ThreeVector& trans) {
  gp_Trsf trsf;
  trsf.SetValues(
      rot.xx(), rot.xy(), rot.xz(), trans.x(),
      rot.yx(), rot.yy(), rot.yz(), trans.y(),
      rot.zx(), rot.zy(), rot.zz(), trans.z());
  return trsf;
}

// OCCT → Geant4
std::pair<G4RotationMatrix, G4ThreeVector>
ToG4(const gp_Trsf& trsf) {
  G4RotationMatrix rot(
      trsf.Value(1,1), trsf.Value(1,2), trsf.Value(1,3),
      trsf.Value(2,1), trsf.Value(2,2), trsf.Value(2,3),
      trsf.Value(3,1), trsf.Value(3,2), trsf.Value(3,3));
  G4ThreeVector trans(trsf.Value(1,4), trsf.Value(2,4), trsf.Value(3,4));
  return {rot, trans};
}
```

Units: Geant4 uses mm; OCCT STEP import typically uses mm as well, but this
should be verified for each import session.

> **Reference position:** Geant4 solids are centered at the local origin,
> whereas OCCT shapes imported from STEP can have an arbitrary reference
> position (e.g., a corner at the origin rather than the center).  Before
> constructing a `G4OCCTSolid`, the shape should be translated so that its
> bounding-box centroid coincides with the OCCT origin.  See
> [Reference Position Handling](reference_position.md) for strategies and
> worked examples.

---

## 6. Recommended Strategy

For the initial G4OCCT implementation, **Strategy 3.1 (Inheritance)** is
adopted because it requires the fewest modifications to downstream Geant4
code and is the most natural extension point.  The classes (`G4OCCTSolid`,
`G4OCCTLogicalVolume`, `G4OCCTPlacement`) serve as the primary API.

As the library matures and performance requirements become clearer, the
registry-based approach (Strategy 3.3) may be layered on top to support
annotating pre-existing Geant4 geometries without rebuilding them.
