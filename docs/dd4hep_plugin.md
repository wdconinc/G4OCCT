<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

# G4OCCT — DD4hep Plugin Design

This document describes the design and development strategy for a **DD4hep
detector element plugin** that makes it easy to declare STEP-based geometry
directly in DD4hep compact XML files, leveraging G4OCCT under the hood.

---

## 1. Motivation and Vision

[DD4hep](https://dd4hep.web.cern.ch/) is the standard detector description
toolkit for the Electron-Ion Collider (EIC) and many other high-energy physics
experiments.  It provides an XML-driven workflow: engineers write compact XML
files that describe detector geometry, materials, and sensitive volumes, and
DD4hep constructs the corresponding Geant4 (and ROOT TGeo) hierarchy at
run-time via *detector element plugins*.

Currently, using STEP-based geometry in a DD4hep simulation requires writing a
bespoke detector constructor in C++ that:

1. Calls `G4OCCTSolid::FromSTEP()` (or `G4OCCTAssemblyVolume::FromSTEP()`)
   to import the shape.
2. Wraps it in `dd4hep::Solid`, `dd4hep::Volume`, and `dd4hep::PlacedVolume`
   objects.
3. Registers the resulting `dd4hep::DetElement` manually.

This is error-prone, repetitive, and bypasses the compact-file workflow that
the rest of the experiment's geometry uses.  A DD4hep plugin would allow
detector engineers to declare STEP geometry *declaratively* in the same compact
XML files as the rest of the detector:

```xml
<detector id="1" name="MyBeamPipe" type="G4OCCT_STEPSolid" vis="BlueVis">
  <step_file path="geometry/beampipe.step"/>
  <position x="0" y="0" z="100*cm"/>
  <rotation x="0" y="0" z="0"/>
  <material name="Aluminium"/>
</detector>
```

The plugin bridges DD4hep's XML-driven geometry description with G4OCCT's STEP
import capabilities.  The resulting geometry participates fully in DD4hep's
visualisation, overlap checking, sensitive detector assignment, and Geant4
navigation — because G4OCCT solids are proper `G4VSolid` subclasses.

---

## 2. Repository Strategy (Phased Approach)

### 2.1 Phase 1 (now → G4OCCT v1.0): Plugin lives inside `eic/G4OCCT`

During the pre-v1.0 phase the DD4hep plugin is built as an **optional
component of G4OCCT itself**, gated by a CMake option and a quiet
`find_package` call:

```cmake
option(BUILD_DD4HEP_PLUGIN "Build DD4hep detector element plugins (requires DD4hep)" OFF)
find_package(DD4hep QUIET)
if(BUILD_DD4HEP_PLUGIN AND DD4hep_FOUND)
  add_subdirectory(src/dd4hep)
endif()
```

This follows the existing pattern used for `BUILD_TESTING`, `BUILD_BENCHMARKS`,
and `BUILD_EXAMPLES`.

**Rationale for keeping the plugin in-repo during Phase 1:**

- G4OCCT is pre-v1.0 with a rapidly evolving API (v0.5 in progress, v0.6
  assembly import planned, active SIMD/performance refactors).  Tight coupling
  means API-breaking changes are caught immediately in a single CI pipeline.
- Shared test fixtures from `src/tests/fixtures/` are available without
  cross-repository dependencies.
- A single `find_package(G4OCCT)` gives downstream users both the core library
  and the DD4hep plugin (if built).
- Less CI infrastructure to maintain: the plugin shares the existing
  eic-shell container and CTest setup.

### 2.2 Phase 2 (post v1.0): Spin out into a separate repository

Once G4OCCT reaches v1.0 (stable API, production-quality performance,
material bridging complete), the plugin moves to its own repository (e.g.
`eic/DD4hep-G4OCCT`).  At that point:

- The G4OCCT API is stable enough that the plugin can declare a minimum
  version requirement (`find_package(G4OCCT 1.0 REQUIRED)`).
- Independent release cadences make sense: DD4hep version pins and G4OCCT
  version pins evolve separately.
- The heavier DD4hep+ROOT dependency no longer burdens every G4OCCT
  contributor.

### 2.3 Comparison Table

| Criterion | In-repo (Phase 1) | Separate repo (Phase 2) |
|---|---|---|
| **API drift risk** | None — same commit catches breakage | Requires pinned version + cross-repo CI |
| **CI overhead for G4OCCT contributors** | Larger CI matrix (DD4hep job added) | Zero overhead for G4OCCT-only work |
| **Fixture sharing** | Direct — `src/tests/fixtures/` | Copy or cross-repo fetch at CI time |
| **Dependency footprint** | DD4hep+ROOT optional even when in-repo | Separate consumers `find_package` independently |
| **Release cadence** | Tied to G4OCCT releases | Independent; pin to stable G4OCCT |
| **Discoverability** | Automatic — part of G4OCCT package | Requires cross-linking in both READMEs |
| **Scope clarity** | Slight scope creep (DD4hep in OCCT repo) | Clean separation of concerns |
| **Licensing flexibility** | Must match LGPL-2.1-or-later | Can adopt different license if needed |

---

## 3. Plugin Architecture

The plugin provides two DD4hep detector element constructors, each registered
via the `DECLARE_DETELEMENT` macro.

### 3.1 `G4OCCT_STEPSolid` — Single solid from a STEP file

**Purpose:** Import a STEP file containing a single solid shape, wrap it as a
`dd4hep::Solid`, and place it as a `dd4hep::DetElement`.

**Compact XML:**

```xml
<detector id="1" name="MyBeamPipe" type="G4OCCT_STEPSolid" vis="BlueVis">
  <step_file path="geometry/beampipe.step"/>
  <position x="0" y="0" z="100*cm"/>
  <rotation x="0" y="0" z="0"/>
  <material name="Aluminium"/>
</detector>
```

**C++ plugin skeleton:**

```cpp
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include <DD4hep/DetFactoryHelper.h>
#include <DD4hep/Printout.h>
#include <G4OCCT/G4OCCTSolid.hh>

using namespace dd4hep;

static Ref_t create_detector(Detector& description, xml_h e,
                              SensitiveDetector /*sens*/)
{
  xml_det_t  x_det  = e;
  xml_comp_t x_step = x_det.child(_Unicode(step_file));
  xml_comp_t x_pos  = x_det.child(_Unicode(position));
  xml_comp_t x_rot  = x_det.child(_Unicode(rotation));
  xml_comp_t x_mat  = x_det.child(_Unicode(material));

  std::string name = x_det.nameStr();
  std::string path = x_step.attr<std::string>(_Unicode(path));

  // Import the STEP solid via G4OCCT
  G4OCCTSolid* g4solid = G4OCCTSolid::FromSTEP(name, path);
  if (!g4solid) {
    throw std::runtime_error("G4OCCT_STEPSolid: failed to import " + path);
  }

  // Wrap in DD4hep constructs
  Material   mat = description.material(x_mat.attr<std::string>(_Unicode(name)));
  Volume     vol(name, Solid(g4solid), mat);
  vol.setVisAttributes(description, x_det.visStr());

  DetElement det(name, x_det.id());
  Position   pos(x_pos.x(), x_pos.y(), x_pos.z());
  RotationZYX rot(x_rot.z(), x_rot.y(), x_rot.x());
  PlacedVolume pv =
      description.pickMotherVolume(det).placeVolume(vol, Transform3D(rot, pos));
  pv.addPhysVolID("system", x_det.id());
  det.setPlacement(pv);
  return det;
}

DECLARE_DETELEMENT(G4OCCT_STEPSolid, create_detector)
```

### 3.2 `G4OCCT_STEPAssembly` — Multi-shape STEP assembly

**Purpose:** Import a STEP file containing a multi-part assembly, apply a
user-supplied material map, and imprint the resulting `G4OCCTAssemblyVolume`
into the DD4hep geometry tree.

**Compact XML:**

```xml
<detector id="2" name="TrackingStation" type="G4OCCT_STEPAssembly" vis="RedVis">
  <step_file path="geometry/tracker_station.step"/>
  <position x="0" y="0" z="200*cm"/>
  <material_map>
    <entry step_name="Al 6061-T6" dd4hep_material="Aluminium"/>
    <entry step_name="G10"        dd4hep_material="G10"/>
  </material_map>
</detector>
```

**C++ plugin skeleton:**

```cpp
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

#include <DD4hep/DetFactoryHelper.h>
#include <DD4hep/Printout.h>
#include <G4OCCT/G4OCCTAssemblyVolume.hh>
#include <G4OCCT/G4OCCTMaterialMap.hh>

using namespace dd4hep;

static Ref_t create_assembly_detector(Detector& description, xml_h e,
                                      SensitiveDetector /*sens*/)
{
  xml_det_t  x_det  = e;
  xml_comp_t x_step = x_det.child(_Unicode(step_file));
  xml_comp_t x_pos  = x_det.child(_Unicode(position));
  xml_comp_t x_map  = x_det.child(_Unicode(material_map));

  std::string name = x_det.nameStr();
  std::string path = x_step.attr<std::string>(_Unicode(path));

  // Build the material map from the compact XML entries
  G4OCCTMaterialMap matMap;
  for (xml_coll_t entry(x_map, _Unicode(entry)); entry; ++entry) {
    xml_comp_t x_entry = entry;
    std::string stepName =
        x_entry.attr<std::string>(_Unicode(step_name));
    std::string dd4hepMatName =
        x_entry.attr<std::string>(_Unicode(dd4hep_material));
    G4Material* g4mat =
        G4Material::GetMaterial(dd4hepMatName, /* warn= */ true);
    if (!g4mat) {
      throw std::runtime_error(
          "G4OCCT_STEPAssembly: DD4hep material not found: " + dd4hepMatName);
    }
    matMap.Add(stepName, g4mat);
  }

  // Import the STEP assembly
  G4OCCTAssemblyVolume* assembly =
      G4OCCTAssemblyVolume::FromSTEP(path, matMap);
  if (!assembly) {
    throw std::runtime_error(
        "G4OCCT_STEPAssembly: failed to import " + path);
  }

  // Place the assembly
  DetElement det(name, x_det.id());
  Position   pos(x_pos.x(), x_pos.y(), x_pos.z());
  G4ThreeVector g4pos(pos.x(), pos.y(), pos.z());
  // Assemblies are placed without an enclosing rotation; individual
  // part placements carry their own orientations from the STEP file.
  G4RotationMatrix* g4rot = nullptr;
  assembly->MakeImprint(
      description.pickMotherVolume(det).solid().ptr(), g4pos, g4rot);

  return det;
}

DECLARE_DETELEMENT(G4OCCT_STEPAssembly, create_assembly_detector)
```

---

## 4. File Layout

The DD4hep plugin source code lives under `src/dd4hep/` inside the G4OCCT
repository:

```
src/dd4hep/
├── CMakeLists.txt
├── G4OCCT_STEPSolid.cc         # DECLARE_DETELEMENT plugin
├── G4OCCT_STEPAssembly.cc      # DECLARE_DETELEMENT plugin
└── tests/
    ├── CMakeLists.txt
    ├── compact_step_solid.xml   # test compact file for STEPSolid
    ├── compact_step_assembly.xml # test compact file for STEPAssembly
    └── test_dd4hep_plugins.cc   # GTest-based integration tests
```

This mirrors the layout of `src/examples/` and `src/benchmarks/`, which are
also opt-in subdirectories activated by CMake options.

---

## 5. CMake Integration

### 5.1 Top-level `CMakeLists.txt` additions

```cmake
# cmake-format: off
option(BUILD_DD4HEP_PLUGIN
  "Build DD4hep detector element plugins (requires DD4hep)" OFF)
# cmake-format: on

if(BUILD_DD4HEP_PLUGIN)
  find_package(DD4hep QUIET)
  if(NOT DD4hep_FOUND)
    message(WARNING
      "BUILD_DD4HEP_PLUGIN=ON but DD4hep was not found. "
      "The DD4hep plugin will not be built.")
  else()
    add_subdirectory(src/dd4hep)
  endif()
endif()
```

### 5.2 `src/dd4hep/CMakeLists.txt`

```cmake
# cmake-format: off
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors
# cmake-format: on

dd4hep_add_plugin(G4OCCTDD4hep
  SOURCES
    G4OCCT_STEPSolid.cc
    G4OCCT_STEPAssembly.cc
)

target_link_libraries(G4OCCTDD4hep
  PRIVATE
    G4OCCT::G4OCCT
    DD4hep::DDCore
)

install(TARGETS G4OCCTDD4hep
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
```

The `dd4hep_add_plugin` helper (provided by `DD4hepConfig.cmake`) generates the
shared library and registers it with DD4hep's plugin system automatically.

### 5.3 Downstream usage

A downstream DD4hep geometry description that wants to use the plugin needs
only to ensure the shared library is on `LD_LIBRARY_PATH` (or `RPATH`) and to
call `find_package(G4OCCT REQUIRED COMPONENTS DD4hepPlugin)` in its own
`CMakeLists.txt`:

```cmake
find_package(Geant4 REQUIRED)
find_package(G4OCCT REQUIRED)
find_package(DD4hep REQUIRED)

# G4OCCTDD4hep is discovered automatically when installed alongside G4OCCT.
# Ensure the plugin library directory is on LD_LIBRARY_PATH at run-time.
```

---

## 6. CI Considerations

### 6.1 New optional CI job

A new matrix entry (or a separate workflow job) named `dd4hep-plugin` is added
to `.github/workflows/ci.yml`.  It runs only when `BUILD_DD4HEP_PLUGIN=ON` can
be satisfied — i.e., inside an eic-shell image where DD4hep is available (the
standard `eic_xl:nightly` image already includes DD4hep and ROOT).

```yaml
dd4hep-plugin:
  name: DD4hep plugin
  runs-on: ubuntu-latest
  needs: []            # runs independently; build-test-benchmark does not depend on this job
  steps:
    - uses: actions/checkout@v4
    - uses: cvmfs-contrib/github-action-cvmfs@v5
    - uses: eic/run-cvmfs-osg-eic-shell@v1
      with:
        platform-release: "eic_xl:nightly"
        run: |
          cmake -B build \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=ON \
            -DBUILD_DD4HEP_PLUGIN=ON
          cmake --build build --parallel $(nproc)
          ctest --test-dir build -L dd4hep --output-on-failure
```

### 6.2 Independence from core CI

The `dd4hep-plugin` job does **not** block the main `build-test-benchmark` or
`sanitizer` jobs.  A DD4hep availability failure must not prevent merging
changes that affect only core G4OCCT code.

### 6.3 CTest labels

All tests in `src/dd4hep/tests/` are labelled `dd4hep` so they can be run in
isolation with `ctest -L dd4hep`.

---

## 7. G4OCCT API Surface Used by the Plugin

The following G4OCCT classes and methods are used by the plugin.  Changes to
any of these must be considered API-breaking once the plugin exists:

| Class / method | Purpose |
|---|---|
| `G4OCCTSolid::FromSTEP(name, path)` | Import a single solid from a STEP file |
| `G4OCCTSolid(name, shape)` | Construct a solid from an existing `TopoDS_Shape` |
| `G4OCCTAssemblyVolume::FromSTEP(path, materialMap)` | Import a multi-part STEP assembly |
| `G4OCCTMaterialMap::Add(stepName, material)` | Register a STEP material name → `G4Material*` mapping |
| `G4OCCTAssemblyVolume::MakeImprint(motherLV, pos, rot)` | Place the assembly into a logical volume |

These methods are the **stable interface** the plugin relies on.  The G4OCCT
API changelog must call out any modification to these signatures.

---

## 8. Testing Strategy

### 8.1 Integration tests

Each DD4hep compact XML test file (in `src/dd4hep/tests/`) is loaded by a
GTest-based test binary (`test_dd4hep_plugins`).  The tests verify:

- The number of placed volumes in the resulting geometry tree matches the
  expected value for the test STEP fixture.
- Material assignments match the compact XML declarations.
- Placement positions (retrieved via `dd4hep::PlacedVolume::position()`) agree
  with the positions specified in the compact file to within 1 µm.
- No DD4hep or Geant4 exceptions are raised during construction.

### 8.2 Fixture reuse

Where possible, tests reuse existing STEP fixtures from
`src/tests/fixtures/geometry/` to avoid duplication.  For example, the
`compact_step_solid.xml` test compact file references the same simple-box STEP
fixture used by the core navigation tests.

### 8.3 CTest integration

Tests are registered with CTest via `add_test` in `src/dd4hep/tests/CMakeLists.txt`
and labelled `dd4hep`:

```cmake
add_test(NAME dd4hep_step_solid    COMMAND test_dd4hep_plugins --gtest_filter=STEPSolid.*)
add_test(NAME dd4hep_step_assembly COMMAND test_dd4hep_plugins --gtest_filter=STEPAssembly.*)
set_tests_properties(dd4hep_step_solid dd4hep_step_assembly PROPERTIES LABELS dd4hep)
```

---

## 9. Roadmap Entry

The following row should be added to the roadmap table in `docs/goals.md`:

```
| v0.7 | 🔲 Planned | DD4hep plugin (`G4OCCT_STEPSolid`, `G4OCCT_STEPAssembly`) | [DD4hep Plugin Design](dd4hep_plugin.md) |
```

---

## 10. Relationship to Existing Documents

| Document | Relationship |
|---|---|
| [Multi-Shape STEP Assembly Import](step_assembly_import.md) | Defines the `G4OCCTAssemblyBuilder` and `G4OCCTMaterialMap` APIs the plugin uses |
| [Material Bridging](material_bridging.md) | No-fallback material mapping strategy applied by the plugin |
| [Geometry Mapping](geometry_mapping.md) | Defines the Geant4 ↔ OCCT class correspondence |
| [Reference Position Handling](reference_position.md) | Recentering strategy applied to each imported leaf solid |
| [Performance Considerations](performance.md) | Per-solid algorithm choices that affect whole-assembly throughput |
| [Project Goals](goals.md) | Roadmap milestone for v0.7 |
