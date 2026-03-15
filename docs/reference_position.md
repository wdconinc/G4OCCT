<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

# G4OCCT — Reference Position: Geant4 vs OCCT

This document describes the fundamental difference in how Geant4 and OCCT
define the reference position (local origin) of a solid, explains why this
matters when wrapping OCCT shapes as `G4OCCTSolid` objects, and provides
concrete strategies for handling the offset consistently.

---

## 1. The Difference in Conventions

### 1.1 Geant4 Convention

In Geant4, every `G4VSolid` is defined so that its **geometric center lies at
the local origin** `(0, 0, 0)`.  For example:

* `G4Box("box", 10, 15, 20)` occupies `[-10,+10] × [-15,+15] × [-20,+20]` mm.
* `G4Tubs("tube", 0, 5, 12, 0, 2π)` is centered on the Z axis with
  `z ∈ [-12, +12]` mm.

When a logical volume is placed via `G4PVPlacement`, the **translation vector
points to where the center of the solid will be** in the mother frame:

```cpp
// The box's center is placed at (50, 0, 100) mm in the mother frame.
new G4PVPlacement(nullptr, G4ThreeVector(50, 0, 100), logVol, "phys", mother, false, 0);
```

Navigation queries (`Inside`, `DistanceToIn`, `DistanceToOut`) are expressed
in the **local frame of the solid**, i.e. with respect to `(0, 0, 0)`.

### 1.2 OCCT Convention

An OCCT `TopoDS_Shape` carries **no requirement** on where its vertices sit
relative to the OCCT origin.  The shape is wherever it was modelled:

* A box built with `BRepPrimAPI_MakeBox(20, 30, 40)` places one corner at the
  OCCT origin — vertices range over `[0,20] × [0,30] × [0,40]` mm.
* A STEP-imported part has whatever coordinate system the CAD operator used.
  Common conventions include: corner at origin, flange face at Z = 0,
  centerline on the Z axis, or an arbitrary engineering datum.

The shape's `TopLoc_Location` carries an additional rigid-body transformation
(rotation + translation) that is composed on top of the raw vertex coordinates,
but by default it is the identity.

---

## 2. Why This Matters for G4OCCTSolid

`G4OCCTSolid` passes navigation query points **directly** to OCCT algorithms
without any automatic recentering.  A query point expressed in Geant4's local
frame is forwarded unchanged to `BRepClass3d_SolidClassifier`,
`IntCurvesFace_ShapeIntersector`, etc.

**If the OCCT shape is not centered at the OCCT origin, all navigation results
will be wrong.**

Consider a 20 × 30 × 40 mm box imported from STEP with one corner at
`(0, 0, 0)` — its center is at `(10, 15, 20)` in OCCT coordinates.

| Scenario | OCCT box center | G4PVPlacement translation | Actual solid center in world | Correct? |
|---|---|---|---|---|
| Box not recentered | (10, 15, 20) | (0, 0, 0) | (10, 15, 20) | ✗ (off by half-dims) |
| Box not recentered, offset compensated | (10, 15, 20) | (-10, -15, -20) | (0, 0, 0) | ✓ (but fragile) |
| Box recentered to origin | (0, 0, 0) | (0, 0, 0) | (0, 0, 0) | ✓ |
| Box recentered to origin | (0, 0, 0) | (50, 0, 100) | (50, 0, 100) | ✓ |

The third and fourth rows use the recommended strategy (center the shape before
constructing `G4OCCTSolid`) and behave identically to a native Geant4 solid.

---

## 3. Strategies

### 3.1 Strategy A — Center the Shape at Import Time (Recommended)

Compute the axis-aligned bounding box of the OCCT shape, find its center, and
apply an inverse translation so that the center maps to the OCCT origin.  Then
construct the `G4OCCTSolid` from the recentered shape.

```cpp
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>
#include <gp_Trsf.hxx>
#include "G4OCCT/G4OCCTSolid.hh"

/// Return a copy of @p shape translated so that its bounding-box center
/// coincides with the OCCT origin (0, 0, 0).
TopoDS_Shape CenterShape(const TopoDS_Shape& shape) {
  Bnd_Box bbox;
  BRepBndLib::Add(shape, bbox);
  if (bbox.IsVoid()) return shape;

  Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
  bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);

  gp_Trsf trsf;
  trsf.SetTranslation(gp_Vec(
      -0.5 * (xmin + xmax),
      -0.5 * (ymin + ymax),
      -0.5 * (zmin + zmax)));

  return BRepBuilderAPI_Transform(shape, trsf, /*Copy=*/true).Shape();
}

// Usage — STEP-imported box with corner at origin:
//   BRep_Builder / STEPControl_Reader fills rawShape with [0,20]×[0,30]×[0,40]
TopoDS_Shape rawShape = /* ... import from STEP ... */;
TopoDS_Shape centered  = CenterShape(rawShape);  // now [-10,10]×[-15,15]×[-20,20]

auto* solid = new G4OCCTSolid("myBox", centered);
// Placement translation now refers to the geometric center, as Geant4 expects:
new G4PVPlacement(nullptr, G4ThreeVector(50, 0, 100), logVol, "phys", mother, false, 0);
```

**Advantages:**
* The solid behaves exactly like a native Geant4 solid.
* Placement transforms have the same semantics as for `G4Box`, `G4Tubs`, etc.
* Downstream code (detector construction, GDML, scoring) requires no special
  treatment.

**Disadvantages:**
* The recentering copies the shape (`BRepBuilderAPI_Transform` with
  `Copy = true`), which has a small memory and CPU cost at construction time.
  For shapes used in many placements, share a single `G4OCCTSolid` (registered
  in a logical volume) and let `G4PVPlacement` handle the per-instance offsets.

**When to use this strategy:**
* Always, unless there is a strong reason to keep the shape in its original
  coordinate frame (e.g., when sharing geometry with other OCCT workflows that
  depend on the original position).

---

### 3.2 Strategy B — Encode the Offset in the Placement

Keep the OCCT shape in its original coordinate frame and compensate by
adjusting the `G4PVPlacement` translation.  The translation must point to the
**OCCT shape's origin** in the mother frame (not the geometric center).

```cpp
// Box imported with corner at (0,0,0), center is at (10,15,20) in OCCT frame.
// To place the box center at world position (50, 0, 100), compute:
//   placement_translation = world_center - occt_origin_offset
//                         = (50, 0, 100) - (10, 15, 20) = (40, -15, 80)
// But this is equivalent to placing the OCCT origin at (40, -15, 80).
G4ThreeVector occtOriginInWorld(40, -15, 80);
new G4PVPlacement(nullptr, occtOriginInWorld, logVol, "phys", mother, false, 0);
```

**Advantages:**
* No shape copy — uses the raw imported shape.

**Disadvantages:**
* The placement translation no longer refers to the geometric center.  This
  breaks the standard Geant4 convention and makes the geometry harder to
  understand, verify, and document.
* Every placement must carry a non-obvious offset that depends on the OCCT
  origin of the specific STEP file.
* Overlap checks and geometry browsers (e.g. Geant4 visualisation) will report
  positions relative to the OCCT origin, not the shape center.

**When to use this strategy:**
* Avoid it in general.  It may be acceptable as a transitional measure when
  importing legacy STEP files whose coordinate systems cannot be changed.

---

### 3.3 Strategy C — Use `TopLoc_Location` to Carry the Offset

An OCCT `TopoDS_Shape` stores an associated `TopLoc_Location` (a
`gp_Trsf`).  The location is composed with the raw vertex coordinates during
all BRep operations.  You can therefore embed the recentering transform in the
shape's location rather than copying the shape:

```cpp
// Compute the bounding-box centroid as in Strategy A.
gp_Trsf trsf;
trsf.SetTranslation(gp_Vec(-cx, -cy, -cz));

// Move the shape to the centered position by updating its location.
// This modifies the shape in-place (no copy of the underlying geometry).
TopoDS_Shape centered = shape.Moved(TopLoc_Location(trsf));
auto* solid = new G4OCCTSolid("myBox", centered);
```

`G4OCCTSolid` stores the shape including its `TopLoc_Location`.  All OCCT
algorithms (`BRepClass3d_SolidClassifier`, `IntCurvesFace_ShapeIntersector`,
etc.) automatically compose the location into their computations.

**Advantages:**
* No deep copy of BRep geometry — only the location wrapper is created.
* Efficient for shapes that are constructed once and placed many times.

**Disadvantages:**
* Subtle: `shape.Moved(loc)` returns a **new** `TopoDS_Shape` object (same
  underlying geometry, different location).  The original `shape` is unchanged.
  Keep the recentered handle around, not the raw one.

**When to use this strategy:**
* When performance or memory usage is a concern and many large shapes need
  recentering.

---

## 4. Determining the Correct Reference Point

The bounding-box centroid (`Strategy A / C`) is the most common choice
because it mirrors how Geant4's native solids define their center.  However,
bounding-box centroid is not always the best choice:

| Shape type | Recommended reference point |
|---|---|
| Symmetric solid (sphere, cylinder, cube) | Bounding-box centroid = center of symmetry |
| Asymmetric CAD part | Bounding-box centroid (conservative default) |
| Part with a defined datum or flange | The engineering datum, if known |
| Assembly (`TopoDS_Compound`) | Bounding-box centroid of the compound |

For simple shapes that originate from Geant4 primitives (e.g., a `G4Box`
exported to STEP and re-imported), the bounding-box centroid is exact.

For complex CAD parts, the bounding-box centroid may not coincide with the
intended placement reference (e.g., a bracket that should be placed by its
bolt-hole center).  In such cases, the user must determine the correct
reference and apply the appropriate offset manually.

---

## 5. Worked Example

The following shows a complete detector-construction snippet that imports a
STEP box, recenters it, and places it at two positions:

```cpp
#include <STEPControl_Reader.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <Bnd_Box.hxx>

#include <G4Box.hh>
#include <G4LogicalVolume.hh>
#include <G4NistManager.hh>
#include <G4PVPlacement.hh>
#include <G4SystemOfUnits.hh>
#include <G4ThreeVector.hh>

#include "G4OCCT/G4OCCTSolid.hh"

// ── Load STEP ──────────────────────────────────────────────────────────────
STEPControl_Reader reader;
reader.ReadFile("detector_part.step");
reader.NbRootsForTransfer();
reader.TransferRoots();
TopoDS_Shape rawShape = reader.OneShape();

// ── Recenter ───────────────────────────────────────────────────────────────
Bnd_Box bbox;
BRepBndLib::Add(rawShape, bbox);
Standard_Real xmin, ymin, zmin, xmax, ymax, zmax;
bbox.Get(xmin, ymin, zmin, xmax, ymax, zmax);
gp_Trsf centerTrsf;
centerTrsf.SetTranslation(gp_Vec(
    -0.5 * (xmin + xmax),
    -0.5 * (ymin + ymax),
    -0.5 * (zmin + zmax)));
TopoDS_Shape centeredShape = BRepBuilderAPI_Transform(rawShape, centerTrsf, true).Shape();

// ── Create solid and logical volume ────────────────────────────────────────
auto* solid = new G4OCCTSolid("detPart", centeredShape);
auto* logVol = new G4LogicalVolume(solid,
    G4NistManager::Instance()->FindOrBuildMaterial("G4_Si"), "detPart_lv");

// ── Place twice — translation = center of the solid in the mother frame ───
new G4PVPlacement(nullptr, G4ThreeVector( 50*mm, 0, 100*mm), logVol, "detPart_1", motherLV, false, 0);
new G4PVPlacement(nullptr, G4ThreeVector(-50*mm, 0, 100*mm), logVol, "detPart_2", motherLV, false, 1);
```

Key points:
1. The STEP file may define a box with corners anywhere — the `CenterShape`
   step removes that ambiguity.
2. After recentering, `G4PVPlacement` receives the **geometric center** of the
   solid as the translation vector, matching the Geant4 convention.
3. The same `logVol` (and therefore the same `G4OCCTSolid`) is shared by both
   placements; no extra OCCT geometry is allocated.

---

## 6. Summary

| Concern | Recommendation |
|---|---|
| STEP import origin | Always recenter to bounding-box centroid before constructing `G4OCCTSolid` |
| `G4PVPlacement` translation | Must point to the **geometric center** of the solid in the mother frame |
| Implementation | Use `BRepBuilderAPI_Transform` (Strategy A) for clarity; use `TopoDS_Shape::Moved` (Strategy C) for large/complex shapes |
| Avoid | Strategy B (offset encoded in placement) — breaks Geant4 conventions |

See also:
* [Geometry Mapping](geometry_mapping.md) — class correspondence and
  transformation conversion between Geant4 and OCCT.
* [Solid Navigation Design](solid_navigation.md) — unit convention and
  tolerance reconciliation (Sections 4.1 and 4.2).
