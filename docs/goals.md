<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

# G4OCCT — Project Goals and Design Philosophy

## Vision

G4OCCT provides a compatibility layer between
[Geant4](https://github.com/geant4/geant4) geometry descriptions and the
[Open CASCADE Technology (OCCT)](https://github.com/Open-Cascade-SAS/OCCT)
geometry framework.  The ultimate goal is to enable physics simulations to be
driven by CAD geometry imported from STEP (and other CAD exchange) files, while
retaining full compatibility with Geant4's navigation, scoring, and
visualisation subsystems.

---

## Motivation

Geant4 is the de-facto standard toolkit for high-energy physics detector
simulations.  Its geometry is traditionally hand-coded using constructive
solid geometry (CSG) primitives (boxes, spheres, tubes, …) or occasionally
tessellated meshes.  Engineering designs, however, are almost universally
stored in CAD tools and exchanged in formats such as STEP.  Bridging the gap
between CAD geometry and Geant4 geometry enables:

* **Accurate detector simulations** from the engineering design directly,
  eliminating geometry discrepancies between the CAD model and the simulation.
* **Reduced maintenance burden** — geometry changes in the CAD tool propagate
  automatically to the simulation.
* **Richer geometry** — OCCT BRep (boundary representation) shapes can capture
  design intent (fillets, chamfers, swept surfaces) that CSG primitives cannot.

---

## Scope

| In scope | Out of scope (v0.x) |
|---|---|
| OCCT BRep → Geant4 solid wrapper | Full OCCT-native navigator |
| STEP file import via OCCT | GDML export |
| CTest-based validation suite | GPU navigation |
| Navigator performance benchmarks | Magnetic field integration |
| Design documents | Material database mapping (tracked separately) |

---

## Design Philosophy

### 1. Thin wrapper, not a replacement

G4OCCT wraps OCCT shapes *within* Geant4 constructs (`G4VSolid`,
`G4LogicalVolume`, `G4VPhysicalVolume`).  The Geant4 navigator, scoring, and
visualisation infrastructure remain unchanged; only the solid-geometry
queries (`Inside`, `DistanceToIn`, `DistanceToOut`) are delegated to OCCT
algorithms.

### 2. Standard CMake integration

Downstream projects integrate G4OCCT via:

```cmake
find_package(G4OCCT REQUIRED)
target_link_libraries(myApp PRIVATE G4OCCT::G4OCCT)
```

The installed `G4OCCTConfig.cmake` propagates the Geant4 and OCCT
dependencies automatically, so users do not need to manually find either.

### 3. Incremental implementation

The initial codebase ships stub implementations of all required virtual
functions.  This allows the CMake build, CI, and test infrastructure to be
validated before committing to a particular navigation algorithm.  Each
stub is annotated with a `TODO` comment and a pointer to the relevant OCCT
API.

### 4. Validated against standard shapes

The test suite in `src/tests/` exercises all Geant4 primitive shapes
(G4Box, G4Sphere, G4Tubs, G4Cons, G4Trd, G4Trap, …) against their OCCT BRep
equivalents, ensuring that navigation results agree to within numerical
precision before the library is released.

### 5. Benchmark-driven optimisation

The benchmark suite in `src/benchmarks/` provides a reproducible comparison
of navigator throughput (steps per second for geantinos) between native
Geant4 solids and G4OCCTSolid wrappers.  Performance regressions are
detectable early in the development cycle.

---

## Repository Layout

```
G4OCCT/
├── CMakeLists.txt          # Top-level build; installs G4OCCTConfig.cmake
├── cmake/
│   └── G4OCCTConfig.cmake.in
├── include/G4OCCT/
│   ├── G4OCCTSolid.hh      # G4VSolid wrapping TopoDS_Shape
│   ├── G4OCCTLogicalVolume.hh
│   └── G4OCCTPlacement.hh  # G4PVPlacement + TopLoc_Location
├── src/
│   ├── G4OCCTSolid.cc
│   ├── G4OCCTLogicalVolume.cc
│   ├── G4OCCTPlacement.cc
│   ├── tests/              # CTest-integrated unit tests
│   └── benchmarks/         # Geantino navigator benchmarks
└── docs/
    ├── goals.md            # This document
    ├── geometry_mapping.md # Geant4 ↔ OCCT class correspondence
    └── material_bridging.md
```

---

## Roadmap

| Milestone | Description |
|---|---|
| v0.1 | CMake skeleton, stub classes, CI, docs |
| v0.2 | `Inside` via `BRepClass3d_SolidClassifier` |
| v0.3 | `DistanceToIn/Out` via `IntCurvesFace_ShapeIntersector` |
| v0.4 | STEP import end-to-end example |
| v0.5 | Full test suite passing for all G4 primitives |
| v1.0 | Production-quality performance, material bridging |
