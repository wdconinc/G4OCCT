<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

# G4OCCT

[![CI](https://github.com/wdconinc/G4OCCT/actions/workflows/ci.yml/badge.svg)](https://github.com/wdconinc/G4OCCT/actions/workflows/ci.yml)
[![Documentation](https://github.com/wdconinc/G4OCCT/actions/workflows/docs.yml/badge.svg)](https://wdconinc.github.io/G4OCCT/)
[![License: LGPL v2.1](https://img.shields.io/badge/License-LGPL_v2.1-blue.svg)](LICENSE)

Geant4 interface to Open CASCADE Technology (OCCT) geometry definitions.

G4OCCT provides a compatibility layer between
[Geant4](https://github.com/geant4/geant4) geometry descriptions and the
[Open CASCADE Technology (OCCT)](https://github.com/Open-Cascade-SAS/OCCT)
geometry framework.  The goal is to enable physics simulations to be driven
by CAD geometry imported from STEP (and other CAD exchange) files, while
retaining full compatibility with Geant4's navigation, scoring, and
visualisation subsystems.

---

## Motivation

Geant4 is the de-facto standard toolkit for high-energy physics detector
simulations.  Its geometry is traditionally hand-coded using constructive
solid geometry (CSG) primitives or tessellated meshes.  Engineering designs,
however, are almost universally stored in CAD tools and exchanged in formats
such as STEP.  G4OCCT bridges this gap by:

- Providing **accurate detector simulations** from the engineering design
  directly, eliminating geometry discrepancies between the CAD model and the
  simulation.
- **Reducing maintenance burden** — geometry changes in the CAD tool propagate
  automatically to the simulation.
- Enabling **richer geometry** — OCCT BRep (boundary representation) shapes
  can capture design intent (fillets, chamfers, swept surfaces) that CSG
  primitives cannot.

For more detail see the [Project Goals](https://wdconinc.github.io/G4OCCT/#/goals)
documentation page.

---

## Architecture

G4OCCT uses a thin-wrapper approach: OCCT shapes are embedded inside Geant4
constructs so that the Geant4 navigator, scoring, and visualisation
infrastructure remain unchanged.

| G4OCCT class | Inherits from | Embeds |
|---|---|---|
| `G4OCCTSolid` | `G4VSolid` | `TopoDS_Shape` |
| `G4OCCTLogicalVolume` | `G4LogicalVolume` | `TopoDS_Shape` (optional) |
| `G4OCCTPlacement` | `G4PVPlacement` | `TopLoc_Location` |

Navigation queries (`Inside`, `DistanceToIn/Out`, `SurfaceNormal`, …) are
delegated to OCCT BRep algorithms.  See the
[Geometry Mapping](https://wdconinc.github.io/G4OCCT/#/geometry_mapping) and
[Solid Navigation Design](https://wdconinc.github.io/G4OCCT/#/solid_navigation)
documentation pages for details.

---

## Requirements

| Dependency | Minimum version |
|---|---|
| CMake | 3.16 |
| C++ | 17 |
| [Geant4](https://github.com/geant4/geant4) | 11.3 |
| [OpenCASCADE (OCCT)](https://github.com/Open-Cascade-SAS/OCCT) | 7.8 |

---

## Building

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DBUILD_BENCHMARKS=ON \
  -DCMAKE_INSTALL_PREFIX=/path/to/install

cmake --build build -- -j$(nproc)
ctest --test-dir build --output-on-failure -j$(nproc)
cmake --install build
```

Both `BUILD_TESTING` and `BUILD_BENCHMARKS` default to `OFF`; enable them
during development to run the CTest suite and the geantino navigator
benchmarks.

---

## Downstream Usage

After installation, integrate G4OCCT into a CMake project with:

```cmake
find_package(G4OCCT REQUIRED)
target_link_libraries(myApp PRIVATE G4OCCT::G4OCCT)
```

The installed `G4OCCTConfig.cmake` propagates the Geant4 and OCCT
dependencies automatically.

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
    ├── goals.md            # Project goals and design philosophy
    ├── geometry_mapping.md # Geant4 ↔ OCCT class correspondence
    ├── solid_navigation.md # G4VSolid ↔ OCCT algorithm mapping
    └── material_bridging.md
```

---

## Documentation

Full documentation is available at **<https://wdconinc.github.io/G4OCCT/>**,
including:

- [**Project Overview Slides**](https://wdconinc.github.io/G4OCCT/slides.html) —
  20-slide deck covering motivation, architecture, performance, and roadmap.
- [Project Goals](https://wdconinc.github.io/G4OCCT/#/goals) — Vision,
  motivation, design philosophy, and roadmap.
- [Geometry Mapping](https://wdconinc.github.io/G4OCCT/#/geometry_mapping) —
  Correspondence between Geant4 and OCCT class hierarchies.
- [Solid Navigation Design](https://wdconinc.github.io/G4OCCT/#/solid_navigation) —
  Per-function mapping of `G4VSolid` queries to OCCT algorithms.
- [Material Bridging](https://wdconinc.github.io/G4OCCT/#/material_bridging) —
  Strategies for mapping STEP/OCCT material names to `G4Material`.
- [API Reference](https://wdconinc.github.io/G4OCCT/api/) — Doxygen-generated
  API documentation.

---

## Contributing

Contributor conventions (coding style, CI setup, documentation requirements,
and design principles) are described in [AGENTS.md](AGENTS.md).

---

## License

This project is licensed under the
[GNU Lesser General Public License v2.1 or later](LICENSE)
(SPDX identifier: `LGPL-2.1-or-later`).
