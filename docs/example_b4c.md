<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

# Example: B4c — Sampling Calorimeter with STEP Geometry

The `src/examples/B4c` directory contains a G4OCCT adaptation of the standard
[Geant4 basic/B4c example](https://github.com/Geant4/geant4/tree/master/examples/basic/B4c).
It demonstrates how to replace hand-coded CSG shapes with geometry loaded
directly from STEP files via `G4OCCTSolid::FromSTEP`, and how to score energy
deposits and track lengths with sensitive detectors.

---

## What the Example Does

A positron (or other particle) beam is fired into a layered lead / liquid-argon
sampling calorimeter.  The absorber and gap shapes within each layer are
imported from STEP files.  Per-event energy deposits and track lengths in both
the absorber and gap are recorded using the Geant4 analysis manager and written
to a ROOT file.

---

## Geometry Layout

The calorimeter consists of 10 identical layers stacked along the z-axis, each
containing one absorber slab followed by one gap slab:

```text
┌──────────────────────────────────────┐  World (Galactic vacuum)
│  ┌──────────────────────────────┐    │
│  │         Calorimeter          │    │  10 × 15 mm = 150 mm total depth
│  │  ┌────┬────┬ … ┬────┬────┐  │    │  10 cm × 10 cm cross-section
│  │  │Abs │Gap │   │Abs │Gap │  │    │
│  │  └────┴────┴ … ┴────┴────┘  │    │  Absorber: 10 mm Pb (STEP)
│  └──────────────────────────────┘    │  Gap:      5 mm lAr (STEP)
└──────────────────────────────────────┘

  beam starts at the world boundary (z = -worldZHalfLength) and fires along +z into the calorimeter.
```

| Volume | STEP file | Material | Role |
|--------|-----------|----------|------|
| Absorber (`Abso` / `AbsoLV`) | `step/absorber.step` | `G4_Pb` (lead) | Energy loss |
| Gap (`Gap` / `GapLV`) | `step/gap.step` | `liquidArgon` | Active scoring medium |

---

## Building the Example

Configure the project with `-DBUILD_EXAMPLES=ON`:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=ON
cmake --build build -- -j$(nproc)
```

The executable is placed at `build/src/examples/B4c/exampleB4c`.

---

## Running the Example

**Batch mode** (supply a macro file):

```bash
./build/src/examples/B4c/exampleB4c -m src/examples/B4c/exampleB4.in
```

The included `exampleB4.in` fires a 300 MeV e⁺ beam, toggles multiple
scattering on and off, then fires 500 MeV gammas:

```geant4mac
/run/initialize

/gun/particle e+
/gun/energy 300 MeV
/run/beamOn 1

/process/inactivate msc
/run/beamOn 1

/process/activate msc

/gun/particle gamma
/gun/energy 500 MeV
/run/beamOn 1
```

**Multi-threaded mode**:

```bash
./build/src/examples/B4c/exampleB4c -m src/examples/B4c/exampleB4.in -t 4
```

**Interactive mode** (no macro):

```bash
./build/src/examples/B4c/exampleB4c
```

---

## Key Source Files

| File | Purpose |
|------|---------|
| `exampleB4c.cc` | `main()` — run manager, physics list, UI/vis setup |
| `src/DetectorConstruction.cc` | Loads STEP files, builds layered calorimeter geometry |
| `src/CalorimeterSD.cc` | Sensitive detector accumulating edep and track length per layer |
| `src/CalorHit.cc` | Hit class storing per-layer energy and track-length sums |
| `src/EventAction.cc` | Fills histograms and ntuple from hits collections at end of event |
| `src/RunAction.cc` | Creates analysis objects; opens/writes/closes `B4.root` |
| `src/PrimaryGeneratorAction.cc` | Particle gun aimed along +z |
| `step/absorber.step` | OCCT box — 10 cm × 10 cm × 10 mm (lead absorber layer) |
| `step/gap.step` | OCCT box — 10 cm × 10 cm × 5 mm (liquid-argon gap layer) |

---

## How STEP Geometry is Loaded

`DetectorConstruction::DefineVolumes()` uses the `G4OCCTSolid::FromSTEP`
convenience factory, which reads a STEP file and returns a ready-to-use
`G4OCCTSolid` in a single call:

```cpp
const std::string stepDir = G4OCCT_B4C_STEP_DIR;
auto* absorberS = G4OCCTSolid::FromSTEP("Abso", stepDir + "/absorber.step");
auto* gapS      = G4OCCTSolid::FromSTEP("Gap",  stepDir + "/gap.step");
```

The path to the `step/` directory is supplied at compile time via the
`G4OCCT_B4C_STEP_DIR` preprocessor definition in `CMakeLists.txt`.

> **Compare with B1:** Example B1 uses `STEPControl_Reader` directly.  B4c
> uses the higher-level `G4OCCTSolid::FromSTEP` factory, which is the
> recommended approach for new code.

---

## Scoring and Output

Each layer has two sensitive detectors registered via `ConstructSDandField()`:

| SD name | Hits collection | Scored volume |
|---------|----------------|---------------|
| `AbsorberSD` | `AbsorberHitsCollection` | `AbsoLV` |
| `GapSD` | `GapHitsCollection` | `GapLV` |

`EventAction` retrieves the total hit from each collection and fills four
histograms and an ntuple, all written to `B4.root` at end-of-run:

| Object | Description |
|--------|-------------|
| H1 `Eabs` | Energy deposit in absorber (0–330 MeV, 110 bins) |
| H1 `Egap` | Energy deposit in gap (0–30 MeV, 100 bins) |
| H1 `Labs` | Track length in absorber (0–50 cm, 100 bins) |
| H1 `Lgap` | Track length in gap (0–50 cm, 100 bins) |
| Ntuple `B4` | Per-event `Eabs`, `Egap`, `Labs`, `Lgap` columns |

---

## Materials

Materials are defined in `DefineMaterials()` using `G4NistManager` and a
manual definition for liquid argon:

```cpp
// Lead from NIST database
nistManager->FindOrBuildMaterial("G4_Pb");

// Liquid argon (NIST value is a gas; define density manually)
new G4Material("liquidArgon", 18., 39.95*g/mole, 1.390*g/cm3);
```

See [Material Bridging](material_bridging.md) for strategies to associate
materials with STEP geometry in more complex scenarios.

---

## Replacing the STEP Files

Drop any STEP file into the `step/` directory and update
`DetectorConstruction.cc` to reference the new file name and choose an
appropriate material.  `G4OCCTSolid::FromSTEP` accepts any STEP file
containing a single solid or a compound; it returns the top-level shape.
