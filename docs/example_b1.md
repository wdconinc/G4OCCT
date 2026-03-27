<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

# Example: B1 — Water Phantom with STEP Geometry

The `src/examples/B1` directory contains a G4OCCT adaptation of the standard
[Geant4 basic/B1 example](https://github.com/Geant4/geant4/tree/master/examples/basic/B1).
It demonstrates how to replace hand-coded CSG shapes with geometry loaded
directly from STEP files via `G4OCCTSolid`.

---

## What the Example Does

A 6 MeV gamma beam is fired through a water-filled envelope that contains two
solids imported from STEP files:

| Volume | STEP file | Material (G4NistManager) | Role |
|--------|-----------|--------------------------|------|
| Shape1 | `step/shape1.step` | `G4_A-150_TISSUE` | First interaction target |
| Shape2 | `step/shape2.step` | `G4_BONE_COMPACT_ICRU` | **Scoring volume** |

The example accumulates the total energy deposited in Shape2 and prints a
summary at the end of the run.

---

## Geometry Layout

```text
┌──────────────────────────────┐  World (G4_AIR)
│  ┌────────────────────────┐  │
│  │       Envelope         │  │  20 × 20 × 30 cm  G4_WATER
│  │   ○ Shape1 (sphere)    │  │  sphere r = 15 mm at (0, +2 cm, −7 cm)
│  │                        │  │
│  │   □ Shape2 (box)       │  │  box 20 × 30 × 40 mm at (−1, −1.5, +5) cm
│  └────────────────────────┘  │
└──────────────────────────────┘

  γ beam fires along +z from random (x,y) within the envelope footprint.
```

---

## Building the Example

Configure the project with `-DBUILD_EXAMPLES=ON`:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=ON
cmake --build build -- -j$(nproc)
```

The executable is placed at `build/src/examples/B1/exampleB1`.

---

## Running the Example

**Batch mode** (supply a Geant4 macro file):

```bash
./build/src/examples/B1/exampleB1 src/examples/B1/run.mac
```

The included `run.mac` fires 100 primary gammas:

```geant4mac
/run/initialize
/gun/particle gamma
/gun/energy 6 MeV
/run/beamOn 100
```

**Headless / initialise only** (no macro):

```bash
./build/src/examples/B1/exampleB1
```

---

## Key Source Files

| File | Purpose |
|------|---------|
| `exampleB1.cc` | `main()` — creates the run manager, physics list, and actions |
| `src/DetectorConstruction.cc` | Loads STEP files, wraps shapes in `G4OCCTSolid` |
| `src/PrimaryGeneratorAction.cc` | 6 MeV gamma gun with random (x, y) start position |
| `src/SteppingAction.cc` | Accumulates edep inside Shape2 (scoring volume) |
| `src/RunAction.cc` | Prints energy deposit summary at end of run |
| `step/shape1.step` | OCCT sphere (r = 15 mm) |
| `step/shape2.step` | OCCT box (20 × 30 × 40 mm) |

---

## How STEP Geometry is Loaded

`DetectorConstruction::Construct()` uses the OpenCASCADE `STEPControl_Reader`
to read each STEP file and then wraps the resulting `TopoDS_Shape` in a
`G4OCCTSolid`:

```cpp
STEPControl_Reader reader;
reader.ReadFile(path.c_str());
reader.TransferRoots();
TopoDS_Shape shape = reader.OneShape();

auto* solid = new G4OCCTSolid("Shape1", shape);
auto* lv    = new G4LogicalVolume(solid, material, "Shape1");
```

The path to the `step/` directory is supplied at compile time via the
`G4OCCT_B1_STEP_DIR` preprocessor definition in `CMakeLists.txt`.

---

## Materials

Materials are defined entirely in code using `G4NistManager` — no material
data is read from the STEP files themselves:

```cpp
G4Material* matShape1 = nist->FindOrBuildMaterial("G4_A-150_TISSUE");
G4Material* matShape2 = nist->FindOrBuildMaterial("G4_BONE_COMPACT_ICRU");
```

See [Material Bridging](material_bridging.md) for a discussion of strategies
to associate materials with STEP geometry in more complex scenarios.

---

## Replacing the STEP Files

Drop any STEP file into the `step/` directory and update
`DetectorConstruction.cc` to reference the new file name and choose an
appropriate material from `G4NistManager`.  The STEP file may contain a single
solid or a compound; `STEPControl_Reader::OneShape()` returns the top-level
shape in either case.
