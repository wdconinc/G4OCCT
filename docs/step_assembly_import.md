<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

# G4OCCT — Multi-Shape STEP Assembly Import

This document describes the design goal of importing a STEP file that contains
a **multi-part assembly** — multiple named solid shapes with individual
placements and material assignments — into a corresponding hierarchy of
`G4OCCT*` objects suitable for use in a Geant4 simulation.

The single-shape workflow demonstrated in
[Example B1](example_b1.md) (`STEPControl_Reader::OneShape()` → one
`G4OCCTSolid`) is the starting point.  This document generalises that workflow
to the realistic engineering case where a STEP file contains dozens or hundreds
of distinct parts arranged in an assembly tree.

---

## 1. Motivation

STEP files from CAD tools rarely contain a single isolated solid.  A real
detector component such as a calorimeter module, a support structure, or a
sensor array is exported as an **assembly** — a tree of named parts, each
with:

* Its own geometry (a `TopoDS_Solid` in OCCT BRep).
* A **placement** (rotation + translation) relative to its parent.
* An optional **material name** (e.g., `"Al 6061-T6"`, `"FR4"`, `"G10"`).
* A human-readable **part name** that should become the Geant4 volume name.

Importing such a file into Geant4 requires:

1. Traversing the OCCT XDE label tree to discover every part.
2. Creating one `G4OCCTSolid` + `G4OCCTLogicalVolume` per unique solid shape.
3. Placing each part in the correct location inside its parent volume via
   `G4OCCTPlacement`.
4. Resolving every material name to a `G4Material*` without heuristics or
   silent fallbacks (see [Material Bridging](material_bridging.md)).

---

## 2. STEP Assembly Concepts

### 2.1 STEP Product Structure

In the STEP AP214 / AP242 exchange format, a product hierarchy is encoded
as a tree of `PRODUCT_DEFINITION` entities linked by
`NEXT_ASSEMBLY_USAGE_OCCURRENCE` (NAUO) relationships.  Each occurrence carries
a `PRODUCT_DEFINITION_TRANSFORMATION` (a `gp_Trsf` in OCCT terms) that
positions the child part in the parent's coordinate frame.

```
Assembly (PRODUCT_DEFINITION)
├── Part A  (PRODUCT_DEFINITION)  ← transformation T_A
│   └── geometry (ADVANCED_BREP_SHAPE_REPRESENTATION)
├── Part B  (PRODUCT_DEFINITION)  ← transformation T_B
│   ├── Sub-part B1  ← transformation T_B1
│   └── Sub-part B2  ← transformation T_B2
└── Part C  (two instances)       ← transformations T_C1, T_C2
```

The key concepts that must be preserved during import are:

| STEP concept | OCCT equivalent | Geant4 equivalent |
|---|---|---|
| `PRODUCT_DEFINITION` (assembly) | `TopoDS_Compound` | `G4AssemblyVolume` |
| `PRODUCT_DEFINITION` (leaf part) | `TopoDS_Solid` | `G4OCCTSolid` + `G4OCCTLogicalVolume` |
| `NEXT_ASSEMBLY_USAGE_OCCURRENCE` | XDE shape reference | `G4OCCTPlacement` |
| `PRODUCT_DEFINITION_TRANSFORMATION` | `TopLoc_Location` / `gp_Trsf` | `G4RotationMatrix` + `G4ThreeVector` |
| `PRODUCT_DEFINITION.name` | XDE name attribute | `G4LogicalVolume` name |
| `MATERIAL_DESIGNATION` (if present) | XDE material attribute | `G4Material*` |

### 2.2 Instance Sharing

A common CAD pattern is to define a part *once* and **instantiate** it at
multiple locations.  In Geant4 terms this maps to a single
`G4LogicalVolume` placed multiple times via distinct `G4PVPlacement`
objects (or `G4AssemblyVolume::AddPlacedVolume`).  The import algorithm must
detect shape references and share the corresponding logical volume rather than
creating duplicate solids.

---

## 3. OCCT XDE Model

STEP assembly data is accessed in OCCT through the **Extended Data Exchange
(XDE)** layer, which layers semantic attributes (names, materials, colours,
placements) on top of the raw BRep topology.

### 3.1 Key XDE Classes

| Class | Role |
|---|---|
| `STEPCAFControl_Reader` | Reads a STEP file into an XDE document; preserves assembly structure, names, materials |
| `TDocStd_Document` | Root document object; holds the label forest |
| `TDF_Label` | Node in the label tree; carries typed attributes |
| `XCAFDoc_DocumentTool` | Entry point for retrieving shape/material/color/layer tools |
| `XCAFDoc_ShapeTool` | Shape tree manipulation; identifies assemblies, simple shapes, and references |
| `XCAFDoc_MaterialTool` | Reads material names and densities attached to labels |
| `XCAFDoc_ColorTool` | Reads colour attributes (for visualisation) |
| `XCAFDoc_Location` | Retrieves the `TopLoc_Location` placement stored on a reference label |

### 3.2 Shape Label Categories

`XCAFDoc_ShapeTool` classifies every label into one of four categories:

| Category | Predicate | Meaning |
|---|---|---|
| Free shape | `IsTopLevel` | A root entry in the shape tree |
| Simple shape | `IsSimpleShape` | A leaf solid (no sub-components) |
| Assembly | `IsAssembly` | A compound with child component labels |
| Reference | `IsReference` | An instance of another label (shares geometry) |

The traversal algorithm must handle all four cases correctly.

### 3.3 Reading a STEP Assembly with `STEPCAFControl_Reader`

Unlike `STEPControl_Reader`, which collapses all shapes into a single
`TopoDS_Shape`, `STEPCAFControl_Reader` populates an XDE document:

```cpp
Handle(TDocStd_Document) doc;
app->NewDocument("MDTV-CAF", doc);

STEPCAFControl_Reader cafReader;
cafReader.SetNameMode(true);     // preserve part names
cafReader.SetMatMode(true);      // preserve material names
cafReader.SetColorMode(true);    // preserve colours (optional)
cafReader.ReadFile("assembly.step");
cafReader.Transfer(doc);

Handle(XCAFDoc_ShapeTool) shapeTool =
    XCAFDoc_DocumentTool::ShapeTool(doc->Main());
Handle(XCAFDoc_MaterialTool) matTool =
    XCAFDoc_DocumentTool::MaterialTool(doc->Main());
```

---

## 4. Mapping to the Geant4 Hierarchy

### 4.1 Structural Correspondence

The mapping from XDE concepts to G4OCCT classes follows naturally from
the correspondence established in [Geometry Mapping](geometry_mapping.md):

| XDE entity | G4OCCT class | Notes |
|---|---|---|
| Free assembly label | — | Root of the import; mapped to the world or envelope |
| Assembly label (non-root) | `G4AssemblyVolume` | Groups sub-volumes; no mother solid geometry |
| Simple-shape label | `G4OCCTSolid` + `G4OCCTLogicalVolume` | Leaf solid + material |
| Reference label + `TopLoc_Location` | `G4OCCTPlacement` | Placed instance |
| Part name (XDE name attribute) | `G4LogicalVolume` name | Used for material lookup and diagnostics |
| Material name (XDE material attribute) | `G4Material*` | Resolved via user-supplied material map |

### 4.2 Assembly Volume Strategy

Geant4 `G4AssemblyVolume` groups physical volumes without requiring a mother
solid.  This maps cleanly to a STEP sub-assembly that has no geometry of its
own and acts purely as a coordinate frame for its children.

For STEP assemblies that **do** have geometry (i.e., both an enclosing solid
and child parts), a `G4OCCTLogicalVolume` using the enclosing solid becomes the
mother volume into which the children are placed.

| STEP assembly kind | Geant4 mother strategy |
|---|---|
| Geometry-free sub-assembly | `G4AssemblyVolume` |
| Solid with nested parts | `G4OCCTLogicalVolume` (enclosing solid) |
| Top-level compound (no solid) | `G4AssemblyVolume` imprinted into world |

### 4.3 Transformation Handling

Each XDE reference label carries a `TopLoc_Location` that positions the
referenced shape in the parent frame.  The conversion to Geant4 placement
parameters follows the scheme described in
[Geometry Mapping §5](geometry_mapping.md#5-transformation-conversion) and
[Reference Position Handling](reference_position.md):

```cpp
// XCAFDoc_ShapeTool::GetShape returns the leaf shape pre-moved to its
// absolute position in the assembly frame.  Compose the TopLoc_Location
// chain into a single gp_Trsf by iterating the datum-power pairs:
TopoDS_Shape locShape = shapeTool->GetShape(referenceLabel);
TopLoc_Location loc   = locShape.Location();

gp_Trsf trsf;  // starts as identity
for (TopLoc_Location cursor = loc; !cursor.IsIdentity();
     cursor = cursor.NextLocation()) {
  gp_Trsf step = cursor.FirstDatum()->Trsf();
  if (cursor.FirstPower() < 0) { step.Invert(); }
  trsf.Multiply(step);
}

// Convert to Geant4 rotation + translation (both use mm; see Geometry Mapping §5)
G4RotationMatrix* rot = new G4RotationMatrix(
    trsf.Value(1,1), trsf.Value(1,2), trsf.Value(1,3),
    trsf.Value(2,1), trsf.Value(2,2), trsf.Value(2,3),
    trsf.Value(3,1), trsf.Value(3,2), trsf.Value(3,3));
G4ThreeVector trans(trsf.Value(1,4), trsf.Value(2,4), trsf.Value(3,4));
```

Units: STEP files exported from most CAD tools use **mm**, which matches
Geant4's default unit system.  The import layer must verify the unit scale
factor stored in the STEP header and apply a correction if necessary.

---

## 5. Traversal Algorithm

The XDE label tree is traversed recursively.  The algorithm below uses
depth-first traversal and builds the Geant4 hierarchy as it unwinds.

### 5.1 Pseudocode

```
function ImportLabel(label, parentLV, placement, shapeTool, matTool, materialMap):

  if shapeTool.IsAssembly(label):
    # Non-leaf: recurse into components
    assembly = new G4AssemblyVolume()
    for each component in shapeTool.GetComponents(label):
      childLabel    = shapeTool.GetReferredShape(component)
      childLocation = shapeTool.GetLocation(component)
      ImportLabel(childLabel, assembly_or_parentLV, childLocation, ...)
    if parentLV is not null:
      assembly.MakeImprint(parentLV, placement.rotation, placement.translation)

  else if shapeTool.IsSimpleShape(label):
    # Leaf solid
    shape    = shapeTool.GetShape(label)
    name     = GetName(label)          # from TDataStd_Name attribute
    matName  = GetMaterialName(label, matTool)
    material = materialMap.Resolve(matName)   # fatal error if not found

    Recenter(shape)  # translate bounding-box centroid to OCCT origin
                     # see Reference Position Handling

    solid = new G4OCCTSolid(name, shape)
    lv    = new G4OCCTLogicalVolume(solid, material, name, shape)
    pv    = new G4OCCTPlacement(placement.rotation, placement.translation,
                                lv, name, parentLV, false, copyNo++,
                                placement.location)

  else if shapeTool.IsReference(label):
    # Instance: resolve to referred label, apply compounded location
    referredLabel    = shapeTool.GetReferredShape(label)
    compoundLocation = placement ∘ shapeTool.GetLocation(label)
    ImportLabel(referredLabel, parentLV, compoundLocation, ...)
```

### 5.2 Instance Sharing (Prototype Pattern)

To avoid creating duplicate `G4OCCTSolid` / `G4OCCTLogicalVolume` pairs for
the same part placed multiple times, the algorithm maintains a map from XDE
label entry to `G4OCCTLogicalVolume*`:

```cpp
NCollection_DataMap<TDF_Label, G4OCCTLogicalVolume*> prototypeMap;
```

When a simple-shape label is first encountered the logical volume is created
and cached.  On subsequent encounters the cached pointer is reused and only
a new `G4OCCTPlacement` (with an incremented copy number) is added.

This mirrors Geant4's own convention where a logical volume defines the
geometry and material once, and multiple physical volumes express distinct
placements.

---

## 6. Proposed API

The import logic will be encapsulated in a new builder class
`G4OCCTAssemblyBuilder`.  Its interface is deliberately kept narrow to
encourage composition with user-defined material maps and post-processing hooks.

```cpp
/// Imports a STEP assembly and constructs the corresponding G4OCCT hierarchy.
class G4OCCTAssemblyBuilder {
public:
  /// Construct with the path to the STEP file.
  explicit G4OCCTAssemblyBuilder(const G4String& stepFilePath);

  /// Register a material map.  Every material name encountered in the STEP
  /// file must have a corresponding entry; unresolved names are fatal errors.
  void SetMaterialMap(const G4OCCTMaterialMap& materialMap);

  /// Build and return the top-level logical volume (or nullptr if the root
  /// is a geometry-free assembly, in which case use GetAssemblyVolume()).
  G4LogicalVolume* Build();

  /// Return the top-level G4AssemblyVolume when the STEP root has no solid.
  G4AssemblyVolume* GetAssemblyVolume() const;

  /// Return a flat list of all created logical volumes, keyed by part name.
  const std::map<G4String, G4OCCTLogicalVolume*>& GetLogicalVolumes() const;
};
```

### 6.1 Material Map Interface

```cpp
/// Maps STEP material names to G4Material objects.
/// Every name used in the STEP file must be present; absent entries are fatal.
class G4OCCTMaterialMap {
public:
  /// Add a mapping from a STEP material name to an existing G4Material.
  void Add(const G4String& stepName, G4Material* material);

  /// Look up a material.  Throws G4Exception if stepName is not registered.
  G4Material* Resolve(const G4String& stepName) const;
};
```

### 6.2 Usage Example

```cpp
// 1. Build the material map
G4OCCTMaterialMap matMap;
matMap.Add("Al 6061-T6",      nist->FindOrBuildMaterial("G4_Al"));
matMap.Add("AISI 316L Steel", nist->FindOrBuildMaterial("G4_STAINLESS-STEEL"));
matMap.Add("FR4",             myFR4Material);

// 2. Import the STEP assembly
G4OCCTAssemblyBuilder builder("detector_module.step");
builder.SetMaterialMap(matMap);
G4LogicalVolume* moduleLV = builder.Build();

// 3. Place the module in the world
new G4PVPlacement(nullptr, G4ThreeVector(), moduleLV, "DetectorModule",
                  worldLV, false, 0);
```

---

## 7. Open Design Questions

### 7.1 Root Volume Strategy

When the STEP root is a geometry-free compound (a typical assembly), there is
no natural mother solid.  Two strategies exist:

| Strategy | Description | Pros | Cons |
|---|---|---|---|
| `G4AssemblyVolume` | Use Geant4's imprint mechanism | No artificial mother geometry | Imprinting into world requires a logical volume argument |
| Bounding-box envelope | Compute tight bounding box; create a `G4Box` mother | Straightforward placement | Adds an artificial volume; may confuse scoring |

The recommended default is `G4AssemblyVolume`; a bounding-box envelope should
be available as an opt-in for users who need a concrete mother solid.

### 7.2 Reference Position

Each imported leaf solid must be recentered so that its bounding-box centroid
coincides with the local OCCT origin (see
[Reference Position Handling](reference_position.md)).  The corresponding
translation must be *absorbed into the placement transformation* so that the
shape appears at its intended position in the assembly:

```
T_effective = T_placement ∘ T_recenter
```

### 7.3 Part-Name Uniqueness

Geant4 does not require logical volume names to be unique, but diagnostics
and material lookup are easier when names are distinct.  The builder should:

1. Warn (but not abort) when two different shapes have the same XDE name.
2. Append a numeric suffix to disambiguate duplicate names.

### 7.4 Deep Instance Hierarchies

STEP files from parametric CAD tools can have assemblies nested more than ten
levels deep.  The recursive traversal algorithm must not impose a depth limit
and must handle circular reference detection (which STEP forbids but which can
appear in malformed files).

### 7.5 Colour Attributes

XDE colour attributes can be forwarded to Geant4 visualisation attributes via
`G4VisAttributes`.  This is optional but improves the out-of-the-box
visualisation quality of imported assemblies.

### 7.6 Selective Import

For large assemblies it may be desirable to import only a subset of parts —
for example, only the active detector elements, skipping support structures
that are outside the tracking acceptance.  The builder should support a
predicate function (or name filter) that selects which labels to import.

---

## 8. Validation Strategy

### 8.1 Test Fixtures

New test fixtures will be added to `src/tests/fixtures/geometry/` to cover
multi-part assemblies:

| Fixture | Description | Expected outcome |
|---|---|---|
| `two-solids-assembly` | Two boxes side by side | Both volumes navigated correctly |
| `nested-assembly` | Three levels of sub-assemblies | Correct compound placement |
| `repeated-instance` | Same solid placed 4× | Single LV, 4 PVs, correct copy numbers |
| `overlapping-check` | Intentionally overlapping parts | G4 overlap check catches it |

### 8.2 Acceptance Criteria

* Navigation (`Inside`, `DistanceToIn`, `DistanceToOut`) produces correct
  results for every leaf solid at every level of the hierarchy.
* Placements are correct: a point inside a leaf solid is reported as
  `kInside` relative to that solid's logical volume.
* Material assignments match the user-supplied map exactly.
* Duplicate part names generate a `G4Exception` warning, not a crash.
* Unresolved material names generate a fatal `G4Exception` before any
  navigation starts.

---

## 9. Implementation Roadmap

| Phase | Target | Description |
|---|---|---|
| Phase 1 | `G4OCCTMaterialMap` | Implement and unit-test the material map; integrate with existing B1 example |
| Phase 2 | XDE flat import | `G4OCCTAssemblyBuilder` for STEP files with a flat list of solids (no sub-assemblies); validate with `two-solids-assembly` fixture |
| Phase 3 | Full assembly traversal | Recursive XDE traversal; prototype-map for instance sharing; validate with `nested-assembly` and `repeated-instance` fixtures |
| Phase 4 | Colour + visualisation | Forward XDE colour attributes to `G4VisAttributes` |
| Phase 5 | Selective import | Predicate/name-filter API; test with large NIST CTC assemblies |

---

## 10. Relationship to Existing Documents

| Document | Relationship |
|---|---|
| [Geometry Mapping](geometry_mapping.md) | Defines the Geant4 ↔ OCCT class correspondence used here |
| [Reference Position Handling](reference_position.md) | Recentering strategy applied to every imported leaf solid |
| [Material Bridging](material_bridging.md) | Material map design; `G4OCCTMaterialMap` implements Strategy 2.1 |
| [Solid Navigation Design](solid_navigation.md) | Navigation algorithms used by each `G4OCCTSolid` in the hierarchy |
| [Performance Considerations](performance.md) | Per-solid algorithm choice affects whole-assembly throughput |
| [Example B1](example_b1.md) | Single-shape baseline; assembly import is the generalisation |
