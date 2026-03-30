<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2026 G4OCCT Contributors -->

# AGENTS.md — Contributor Instructions for G4OCCT

This file describes the coding conventions, tooling requirements, and
design principles for the G4OCCT project.  All contributors (human and
AI agents) must follow these instructions.

> **Keeping this file up to date:** When a PR discussion introduces a new
> convention or overturns an existing one, update this file in the same PR
> so the change is recorded here.  Changes to this file follow the same
> review process as any other code change.

---

## 1. License

- All source files must carry an **SPDX license header** as the very first
  non-blank line(s).
- The required identifier is `LGPL-2.1-or-later`.
- C/C++ style:

  ```cpp
  // SPDX-License-Identifier: LGPL-2.1-or-later
  // Copyright (C) 2026 G4OCCT Contributors
  ```

- CMake `#`-comment style:

  ```cmake
  # cmake-format: off
  # SPDX-License-Identifier: LGPL-2.1-or-later
  # Copyright (C) 2026 G4OCCT Contributors
  # cmake-format: on
  ```

- YAML `#`-comment style:

  ```yaml
  # SPDX-License-Identifier: LGPL-2.1-or-later
  # Copyright (C) 2026 G4OCCT Contributors
  ```

- HTML/Markdown `<!-- -->` style:

  ```html
  <!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
  <!-- Copyright (C) 2026 G4OCCT Contributors -->
  ```

- The CI workflow `.github/workflows/spdx.yml` uses `enarx/spdx@master` to
  enforce headers on every PR.  A new file that fails the SPDX check will
  block merge.
- The SPDX header must be the **very first line(s)** of every file, with one
  exception: executable scripts may have a shebang (`#!/...`) on line 1,
  followed immediately by the SPDX header on subsequent lines.  This is the
  required layout for shell scripts in the repository:

  ```bash
  #!/usr/bin/env bash
  # SPDX-License-Identifier: LGPL-2.1-or-later
  # Copyright (C) 2026 G4OCCT Contributors
  ```

- CMake files must wrap the SPDX header in `# cmake-format: off` / `# cmake-format: on`
  guards so `cmake-format` cannot reflow the comment block:

  ```cmake
  # cmake-format: off
  # SPDX-License-Identifier: LGPL-2.1-or-later
  # Copyright (C) 2026 G4OCCT Contributors
  # cmake-format: on
  ```

- Jinja2 template files (`.html.jinja2`) must use the HTML comment style
  (`<!-- -->`) for the SPDX header, **not** the Jinja comment style (`{# #}`).
- Scripts under `.github/` (Python, shell, `.supp`, `.gitignore`) are also
  subject to the SPDX check and must carry the appropriate header.
- Source files **copied from Geant4 examples** (e.g. `B1`, `B4c`) must retain
  their original Geant4 license block unchanged.  Do **not** replace it with
  the project LGPL header.  Add the example directory to the `ignore-paths`
  list in `.github/workflows/spdx.yml` so the SPDX check skips those files.

---

## 2. Include Style

- **External headers** (Geant4, OCCT, standard library, third-party) must use
  **angle brackets**:

  ```cpp
  #include <G4VSolid.hh>
  #include <TopoDS_Shape.hxx>
  #include <vector>
  ```

- **Internal project headers** (headers that live inside `include/G4OCCT/`)
  must use **double quotes**:

  ```cpp
  #include "G4OCCT/G4OCCTSolid.hh"
  #include "G4OCCT/G4OCCTLogicalVolume.hh"
  ```

- **Include style:** include only headers you use directly.  Do not rely on
  transitive includes from Geant4 or OCCT headers.  Explicitly add
  `<algorithm>`, `<cmath>`, `<cstdlib>`, `<cstddef>`, `<stdexcept>`, etc.
  when using their symbols, and remove unused includes to silence
  compiler/clang-tidy warnings about unused includes.
- **DD4hep + OCCT include ordering / firewall pattern:** Including DD4hep and
  OpenCASCADE headers in the same translation unit produces two independent
  hard errors:

  1. **`Handle` macro collision** — `Standard_Handle.hxx` defines
     `Handle(Class)` → `opencascade::handle<Class>`.  If OCCT headers are
     included before `DD4hep/Handle.h`, every `Handle(...)` inside that header
     (e.g. `Handle() = default`) is macro-expanded into a broken template
     instantiation.

  2. **`Printf` return-type conflict** — ROOT's `TString.h` (pulled in by
     DD4hep) declares `extern void Printf(...)` while OCCT's
     `Standard_CString.hxx` (pulled in by any OCCT header) declares
     `Standard_EXPORT int Printf(...)`.  These two declarations with different
     return types cannot coexist in the same TU.

  **Required pattern for DD4hep plugins that use G4OCCT:** use a *firewall*:
  keep the DD4hep-facing code and the G4OCCT/OCCT-facing code in separate
  translation units, bridged by a thin header that includes neither.

  ```text
  PluginName.cc          ← includes DD4hep only; calls bridge function
  PluginName_impl.hh     ← bridge header: std::string, plain POD, forward decls only
  PluginName_impl.cc     ← includes G4OCCT/OCCT only; implements bridge function
  ```

  Example bridge header (no DD4hep, no OCCT):

  ```cpp
  #include <string>
  struct MyPluginResult { double halfX, halfY, halfZ; };
  MyPluginResult LoadSTEP(const std::string& name, const std::string& path);
  ```

- The **IWYU workflow** (`.github/workflows/iwyu.yml`) enforces
  include-what-you-use on every PR using `iwyu_tool.py` + `fix_includes.py`.
  The mapping file `.github/iwyu.imp` handles OCCT header aliases.  PRs that
  introduce unnecessary or missing includes will fail this check.

---

## 3. Build Requirements

- **C++ standard:** C++20 (`CMAKE_CXX_STANDARD 20`).  All C++20 features
  (`std::numbers::pi`, designated initialisers, `<ranges>`, etc.) are
  available and preferred.  Prefer `std::numbers::pi` over `M_PI` or
  `acos(-1.0)`.
- **Minimum Geant4 version:** 11.3 — specified as `find_package(Geant4 11.3 REQUIRED)`.
- **Minimum OpenCASCADE version:** 7.8 — specified as
  `find_package(OpenCASCADE 7.8 REQUIRED COMPONENTS ...)`.
- **CMake minimum:** 3.16.

Do not lower these version floors without an explicit project decision.

---

## 4. CMake Conventions

- The top-level `CMakeLists.txt` owns all `find_package` calls.
- Sub-directory `CMakeLists.txt` files (tests, benchmarks) must **not** call
  `find_package` themselves; they link to the already-found targets.
- `target_include_directories` for build-time discovery paths (Geant4, OCCT)
  must use **`PRIVATE`**, not `PUBLIC`, so those paths are not exported into
  the installed `G4OCCTTargets.cmake` and do not pollute downstream consumers.
- `target_link_libraries` for Geant4 and OCCT must use **`PUBLIC`** so
  downstream consumers pick up transitive dependencies automatically.
- Do **not** use `check_cxx_compiler_flag` to probe for sanitizer flags
  (e.g. `-fsanitize=address`); it produces false negatives with clang.
  Add sanitizer flags unconditionally inside the relevant CMake option block.
- `BUILD_TESTING` (CTest) and `BUILD_BENCHMARKS` are both `ON` in CI; they
  must be kept independently buildable with either flag off.
- Install rules live in the top-level file; the exported target is
  `G4OCCT::G4OCCT`.

---

## 5. Testing

- All tests are CTest-integrated (`add_test`).
- Unit tests live in `src/tests/`.
- Tests must pass with `ctest --test-dir build --output-on-failure -j$(nproc)`.
- Avoid removing or disabling existing tests.  Removal is allowed only when
  equivalent or better coverage is provided by other tests in the same PR, and
  the PR description clearly explains the rationale and references the
  replacement coverage.
- Tests that cover multiple geometry fixtures must be split into **per-fixture
  CTest sub-tests** (using `GLOB` discovery), not a single monolithic binary.
- Use **`yaml-cpp`** for all YAML parsing; it is available in the eic-shell
  container.  Hand-rolled YAML parsers have been rejected twice (PRs #20, #36)
  and must not be introduced.
- When a test must be temporarily disabled, add a **programmatic skip condition**
  (e.g. a `DISABLED` CTest property or a runtime skip) with a comment that
  links to the relevant issue or CI run explaining why.  Do not silently delete
  tests or comment them out without explanation.
- Fixture provenance YAML must document the generator script and all parameters
  needed to reproduce the geometry independently.
- After modifying a fixture generation script (`generate.tcl`, `generate.py`),
  always **regenerate and commit** the corresponding `shape.step` file.  CI
  loads the committed STEP file, not the script.  Stale STEP files cause silent
  test mismatches.
- Expected-failure (`xfail`) reclassification must use a **narrow allowlist of
  specific error codes**.  Never demote all errors — structural or IO failures
  (missing STEP file, read errors) must remain hard errors.  The valid
  demotable codes are defined in `src/tests/geometry/fixture_validation.hh`
  and `fixture_validation.cc`; examples include `fixture.volume_mismatch`,
  `fixture.ray_distance_mismatch`, and `fixture.safety_mismatch`.  Consult
  those files to find the current complete list.
- **Assembly fixture generation** must use **TCL/DRAWEXE scripts** (the same
  approach used for single-solid fixtures), not custom C++ code.  Custom C++
  generators do not scale to many assemblies and were rejected (PR #155).
- **GDML files** (`.gdml`) are XML and can carry SPDX headers in an XML
  comment.  Do **not** add `.gdml` files to the `ignore-paths` list in
  `.github/workflows/spdx.yml`.
- **CTest `add_test()` commands** must specify the test executable via the
  generator expression `$<TARGET_FILE:<target>>`, not by the bare target name.
  The bare name is not guaranteed to be on PATH (it varies across generators
  and multi-config builds).  Also set `WORKING_DIRECTORY` to the binary
  directory so that macro files and fixture paths resolve correctly (PRs #314,
  #320):

  ```cmake
  add_test(NAME my_test
           COMMAND $<TARGET_FILE:my_executable> -m ${CMAKE_CURRENT_BINARY_DIR}/init.mac)
  set_tests_properties(my_test PROPERTIES
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  ```

---

## 6. CI

- **CI workflow (`ci.yml`)** triggers on `push` against `main` and on
  `pull_request` with no branch filter, to support sub-PR workflows where
  feature branches target other feature branches.
  Do not add `master` or wildcard branch patterns to the `push` trigger.
  Do not add a `branches:` filter to the `pull_request` trigger.
- **Container:** both CI jobs run inside `eic/run-cvmfs-osg-eic-shell@v1`
  with `platform-release: "eic_xl:nightly"`.  Use `eic_xl`, **not** `eic_ci`.
- **Prerequisite step:** `cvmfs-contrib/github-action-cvmfs@v5` must appear
  as a step *before* `eic/run-cvmfs-osg-eic-shell@v1` in every job that uses
  the eic-shell.  Omitting it silently breaks the shell environment.
- **Git operations inside containers:** Git commands that require commit history
  (e.g. `git diff base..head` for clang-tidy annotation) fail inside the
  Apptainer container when using shallow checkouts.  Generate diffs and any
  git-dependent data **outside the container** before entering eic-shell.
- **Python packages in the container:** The eic-shell container has a
  read-only system Python.  Do **not** use bare `pip install <pkg>`.  Instead
  create a local virtual environment:

  ```bash
  python3 -m venv /tmp/venv && /tmp/venv/bin/pip install <pkg>
  ```

- **FetchContent in the container:** HTTP downloads via FetchContent may fail
  inside the eic-shell container due to network restrictions.  Always attempt
  `find_package` first; use FetchContent as a fallback with a cache key so CI
  doesn't re-download on every run.  Use `actions/cache` to cache the
  FetchContent download directory between runs.
- **FetchContent `actions/cache` scope:** Cache only the downloaded source
  directories and CMake subbuild metadata (e.g., `_fetchcontent/*-src`,
  `_fetchcontent/*-subbuild`, or the dedicated download subdirectory), not
  the entire `_fetchcontent/` tree.  Do **not** cache `_fetchcontent/*-build`
  or any directory that contains compiled artefacts (object files, libraries,
  executables).  FetchContent build artefacts are compiled with job-specific
  flags (Release, ASAN, TSAN, coverage); sharing them across jobs via the
  cache causes cross-contamination and stale instrumentation.  Use a
  job-specific cache key that includes the build mode whenever dependency
  build output is cached (PR #201).
- **CI job (`ci.yml`):** Three main jobs:
  1. `build-test-benchmark` — builds with `-DCMAKE_BUILD_TYPE=Release
     -DBUILD_TESTING=ON -DBUILD_BENCHMARKS=ON`, runs tests, and installs.
  2. `sanitizer` (matrix) — two entries running inside `eic_xl:nightly`:
     - `asan`: builds with `-DCMAKE_BUILD_TYPE=RelWithDebInfo
       -DBUILD_TESTING=ON -DUSE_ASAN=ON -DUSE_UBSAN=ON`, runs tests and B1.
     - `tsan`: builds with `-DCMAKE_BUILD_TYPE=RelWithDebInfo
       -DBUILD_TESTING=ON -DUSE_TSAN=ON`, runs tests and B1.
  All jobs check out the repository, install CVMFS, and run inside
  `eic/run-cvmfs-osg-eic-shell@v1` with `platform-release: "eic_xl:nightly"`.
- The `sanitizer` matrix job also fetches NIST CTC STEP fixtures (`fetch-nist-ctc`
  composite action) to exercise `test_nist_ctc_inside_volume` under ASAN and
  UBSAN.
- **NIST CTC fixtures** (`nist-ctc-01` through `nist-ctc-11`) are AP203
  compound assemblies with no native Geant4 solid equivalent.  They are
  validated via `test_nist_ctc_inside_volume`, which compares a Monte-Carlo
  `Inside()`-based volume estimate to an OCCT reference volume computed from
  the imported shape, and benchmarked independently in `bench_navigator`.  See
  [docs/nist_ctc.md](docs/nist_ctc.md) for details.
- The `sanitizer` matrix job sets `ASAN_OPTIONS`, `LSAN_OPTIONS`, `UBSAN_OPTIONS`,
  and `TSAN_OPTIONS` unconditionally in the job-level `env:` block — do **not**
  put them in the workflow's top-level `env:` or in the `build-test-benchmark`
  job. Inactive sanitizers ignore the unrelated env vars.
- Suppression files live in `.github/asan.supp`, `.github/lsan.supp`,
  `.github/ubsan.supp`, and `.github/tsan.supp`.
- **Do not split** tests and benchmarks into separate jobs.
- **Docs workflow (`docs.yml`):** Builds Doxygen API docs and deploys the
  `docs/` directory (including generated `docs/api/`) to GitHub Pages.
- **SPDX workflow (`spdx.yml`):** Enforces SPDX headers via `enarx/spdx@master`.
- **IWYU workflow (`iwyu.yml`):** Runs include-what-you-use on C++ source files.
  - Triggered on `push` to `main` and `pull_request` (no branch filter).
  - Builds the project in Debug mode with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
    inside `eic_xl:nightly` to produce `build/compile_commands.json`.
  - For PRs: runs `iwyu_tool.py` iteratively until the diff is stable (up to
    5 iterations) on changed C++ files only; fails if IWYU suggests changes.
  - For push: runs `iwyu_tool.py` on all files and uploads the patch as an
    artifact (informational, does not block merge on `main`).
  - Uses `.github/iwyu.imp` for G4OCCT-specific IWYU mappings (STL, etc.).
  - The changed-file list is computed **outside** the container (on the GitHub
    runner, not inside `eic/run-cvmfs-osg-eic-shell@v1`) to avoid shallow-checkout
    `git diff` failures.

---

## 7. Documentation

- Human-readable documentation lives in `docs/`.
- The docs site uses **docsify** (`docs/index.html`, `docs/_sidebar.md`).
- API reference is generated by **Doxygen** using `Doxyfile` at the repo
  root; output goes to `docs/api/`.
- Both are deployed together to GitHub Pages by `.github/workflows/docs.yml`.
- Document headers (Doxygen `/** ... */` style) are required before all
  public functions and classes; existing headers serve as the style reference.
- Doxygen docstrings must **accurately describe the actual implementation**.
  In particular, verify return-value semantics (e.g. "lower bound" vs "exact
  distance"), algorithm descriptions, and preconditions against the code.
  Inaccurate docstrings have been corrected in multiple PRs and are treated as
  bugs.
- Code examples in documentation must be **syntactically valid**.  Do not use
  the Unicode ellipsis `…` in C++ code blocks; use `/* ... */` or real code.
  Do not reference non-existent APIs.
- Performance-related documentation must be **updated in the same PR** as the
  implementation change.  Do not defer doc updates to a follow-up PR.
- Use **British spelling** in all documentation: "visualisation" not
  "visualization", "optimisation" not "optimization".  Match the conventions of
  existing `docs/` pages.

---

## 8. G4Exception Policy

### Severity guidelines

| Situation | Severity |
|---|---|
| Unmapped / invalid input that cannot be recovered from | `FatalException` |
| Degenerate solid geometry (e.g. empty tessellation) | `FatalException` |
| Skippable STEP assembly component (null shape, missing reference) | `JustWarning` |

Never use `JustWarning` for situations where the result returned to the caller
would be incorrect or unusable.  If in doubt, prefer `FatalException`.

### Test-time handler

All GTest-based tests link **`G4OCCTTestSupport`**, which installs a
`G4VExceptionHandler` at suite startup.  The handler converts any
`JustWarning` G4Exception whose code starts with `G4OCCT_` or equals
`GeomMgt1001` into a Google Test `ADD_FAILURE()` call.  This means
unexpected warnings from our own code automatically fail the test.

Geant4-internal warnings (e.g. `mat031` from `G4Material::FillVectors()`)
are passed through silently.

### GDML fixture materials

All GDML fixture files must define materials with fractions that sum to
**exactly 1.0**.  Dry air must be defined as a ternary N+O+Ar mixture
(matching the `string-array-v1` fixture), not a binary N+O mixture.

---

## 9. Material Bridging

- Material assignments must be **correct, unique, and unambiguous**.
- **No heuristics are permitted** (e.g., guessing material from density,
  fuzzy name matching, or silent fallbacks to a default material).
- An unmapped STEP material name is a **fatal error**; the simulation must
  not run with an unknown material.
- The preferred long-term strategy is a **GDML overlay** (using
  `G4GDMLParser`), which provides a schema-validated material vocabulary.
- See `docs/material_bridging.md` for the full strategy description and
  phased implementation plan.

---

## 10. Sensitive Detector Mapping

`G4OCCTSensitiveDetectorMap` is the SD analogue of `G4OCCTMaterialMap`: it
maps volume name patterns to `G4VSensitiveDetector*` pointers and is applied
**after** import, in `ConstructSDandField()`.

### Matching rules (`G4OCCTSensitiveDetectorMap::Resolve`)

Two strategies are checked in insertion order; the first match wins:

1. **Exact match** — `volumeName == pattern`
2. **Prefix match** — `volumeName` starts with `pattern + "_"` **and** the
   remaining suffix consists entirely of decimal digits.  This handles Geant4's
   `MakeUniqueName` deduplication (e.g. `"Absorber_1"`, `"Absorber_2"` both
   match pattern `"Absorber"`).

`Resolve()` returns `nullptr` for unmatched names — this is expected for
non-sensitive volumes and is **not** fatal.  Adding a `nullptr` SD pointer is
fatal (`G4Exception` with code `G4OCCT_SDMap000`).

### Applying the map (`G4OCCTAssemblyVolume::ApplySDMap`)

```cpp
// In ConstructSDandField():
G4OCCTSensitiveDetectorMap sdMap;
sdMap.Add("Absorber", myAbsoSD);
sdMap.Add("Gap",      myGapSD);
// Assigns SDs to all matching logical volumes; returns count of assignments.
std::size_t assigned = assembly->ApplySDMap(sdMap);
```

Call `ApplySDMap()` **after** `FromSTEP()` and after all SDs have been
created and registered with `G4SDManager`.

### G4OCCTAssemblyRegistry (DD4hep / plugin workflows)

`G4OCCTAssemblyRegistry` is a singleton that takes ownership of
`G4OCCTAssemblyVolume` objects by name, keeping them alive past the plugin
build phase.

```cpp
// Build phase (plugin):
auto* assembly = G4OCCTAssemblyVolume::FromSTEP("detector.step", matMap);
G4OCCTAssemblyRegistry::Instance().Register("myDetector", assembly);

// SD field phase (ConstructSDandField):
G4OCCTAssemblyVolume* a = G4OCCTAssemblyRegistry::Instance().Get("myDetector");
a->ApplySDMap(sdMap);
```

Key methods: `Register(name, assembly)`, `Get(name)` (returns `nullptr` if not
found), `Release(name)` (removes from registry and transfers ownership to
caller), `Size()`.

### XML map file (`G4OCCTSensitiveDetectorMapReader`)

```xml
<sensitive_detector_map>
  <volume name="Absorber" sensDet="AbsorberSD"/>
  <volume name="Gap"      sensDet="GapSD"/>
</sensitive_detector_map>
```

```cpp
G4OCCTSensitiveDetectorMapReader reader;
G4OCCTSensitiveDetectorMap sdMap = reader.ReadFile("sd_map.xml");
assembly->ApplySDMap(sdMap);
```

Must be called **after** all SDs are registered in `G4SDManager`.  Missing
`name`/`sensDet` attributes or an unknown SD name are fatal errors (codes
`G4OCCT_SDReader000`–`G4OCCT_SDReader002`).

---

## 11. Geometry and Navigation

- `G4OCCTSolid` wraps a `TopoDS_Shape` and inherits `G4VSolid`.
- `G4OCCTLogicalVolume` inherits `G4LogicalVolume` and carries an optional
  `TopoDS_Shape` for reference.
- `G4OCCTPlacement` inherits `G4PVPlacement` and carries a `TopLoc_Location`.
- Navigation method stubs contain `// TODO:` comments pointing at the
  specific OCCT algorithm to use for the real implementation.
- See `docs/geometry_mapping.md` and `docs/solid_navigation.md` for the full
  design analysis.
- Shape validity (closed solid, no gaps) must be verified with
  `BRepCheck_Analyzer` during `G4OCCTSolid` construction.
- Use **`BRepBndLib::AddOptimal(..., useTriangulation=false)`** (not
  `BRepBndLib::Add`) when computing bounding boxes.  `Add` over-inflates bounds
  when B-spline PCurves are present, causing a systematic ray-distance offset
  in fixture comparisons (PR #119).
- `DistanceToIn(p)` and `DistanceToOut(p)` return a **lower bound** on the
  distance to the surface, not the exact distance.  Document and implement them
  accordingly.  Use the `ExactDistanceToIn/Out` variants when an exact value is
  required.
- **Tiered acceleration pattern for `DistanceToIn/Out(p)`**: both methods use the
  same three-tier structure to avoid expensive `BRepExtrema_DistShapeShape` per-face
  calls for the common (outside, well-separated) case:
  1. **Tier-0** `AABBLowerBound(p)` — O(1), catches points outside the bounding box.
  2. **Tier-1** `BVHLowerBoundDistance(p)` — O(log N_triangles), BVH over the
     triangulated surface; returns `max(0, meshDist − fBVHDeflection)` which is a
     provably conservative lower bound for outside points.  For all-planar solids,
     `DistanceToOut(p)` uses `PlanarFaceLowerBoundDistance` instead.
  3. **Tier-2** exact fallback — `ExactDistanceToIn/Out(p)`, only reached for
     near-surface points where the BVH lower bound is ≤ `IntersectionTolerance()`.
  Do **not** skip Tier-1 in any new DTI/DTO implementation.  Omitting it causes
  O(N_faces × Newton_iterations) behaviour for every point inside the AABB, which
  is catastrophic for curved surfaces (e.g. 2237× slowdown for ellipsoids).
- **BVH-seeded face identification for `SurfaceNormal(p)`**: `SurfaceNormal` must
  identify the nearest face and evaluate the outward normal at the closest (u,v).
  Phase 1 (all-planar solids): find the face with minimum `fb.plane->Distance(p)` and
  return `fb.outwardNormal` directly.  Phase 2 (curved or mixed solids): call
  `BVHLowerBoundDistance(p)` to get a mesh lower bound `bvhLB`, compute
  `seedDist = bvhLB + 2×fBVHDeflection` as a valid upper bound on the true
  distance to the surface, then call `TryFindClosestFace(fFaceBoundsCache, p, seedDist)`.
  The seed prunes distant faces before BRepExtrema, reducing calls for multi-face solids
  while guaranteeing correctness (exact BRepExtrema used on all candidates, unlike pure
  BVH triangle lookup which can return the wrong face near face boundaries due to
  tessellation approximation errors).  Do **not** use a pure BVH triangle→face mapping
  approach (previously `BVHFindNearestFaceBounds`) — tessellation error `fBVHDeflection`
  can cause the nearest tessellated triangle to belong to a different analytical face than
  the true nearest face, producing wrong normals for all non-planar solids.
- **Multi-ray majority vote for `Inside(p)` Tier-2a**: When the primary BVH
  ray-parity cast (+Z direction) is degenerate (ray grazes a triangle edge or
  vertex) or yields zero crossings, do **not** fall back to
  `BRepClass3d_SolidClassifier` immediately.  Instead cast two more orthogonal
  rays (+X, +Y) using the same `TriangleRayCast` object (reset by `SetRay()`).
  Non-degenerate rays vote; degenerate rays abstain.  A strict majority
  (insideVotes > outsideVotes, or vice versa) determines the result.  Fall back
  to the exact classifier only when no majority is obtainable (all three rays
  degenerate, or a genuine 1–1 tie).  The near-surface guard
  (`bvhLB < tolerance`) still routes directly to the exact classifier for
  correctness.  This pattern eliminates `BRepClass3d_SolidClassifier::Perform()`
  calls for the degenerate/zero-crossing cases (the main source of `CSLib_Class2d`
  overhead and NCollection heap churn for complex solids).  This Tier-2
  navigation convention is intentionally documented only here; the
  `.github/copilot-instructions.md` quick reference remains high-level and
  does not list individual Tier-2 variants.
- `GetPointOnSurface()` must sample from OCCT tessellation.  Returning the
  origin (the `G4VSolid` base-class default) triggers Geant4 warnings.
- Internal helper types (e.g. `FaceBounds`, `ClosestFaceMatch`) must be
  **private nested types** and must not appear in the public header.  Do not
  leak implementation details into the public API surface.
- `G4Polyhedra` STEP fixtures use the **circumradius**, not the apothem.
  Confusing the two produces incorrect vertex positions (PR #130).
- **`kInfinity` is not IEEE infinity.**  Geant4's `kInfinity ≈ 1e100` is a
  large finite value; `std::isinf(kInfinity)` is `false`.  Test for it with
  `distance >= kInfinity`, not with `std::isinf()`.
- **Use public accessors for Geant4 class members.**  Never access private
  data members (e.g. `fScale`, `fSolidPtr`) of Geant4 classes directly.  Use
  the provided accessor methods (`GetScaleTransform()`, `GetUnscaledSolid()`,
  etc.).  Private member names are implementation details that change between
  Geant4 versions.

---

## 12. Code Style

- Doxygen `/** ... */` block comments before all public API (classes,
  constructors, methods).
- Single-line `///` Doxygen comments for short members.
- Section dividers use `// ── Section name ──...──` (em-dash style) as seen
  in existing source files.
- No trailing whitespace; use 2-space indentation for C++; 100-character column limit.
- Use **`using`** declarations to alias long OCCT or Geant4 type names at
  local scope rather than repeating the full name at each call site.  Example:

  ```cpp
  using Handle_Geom_Plane = opencascade::handle<Geom_Plane>;
  ```

---

## 13. Code Quality Tools (pre-commit)

The project uses **pre-commit** hooks (`.pre-commit-config.yaml`) to enforce
consistent style automatically before every commit.  Install once with:

```bash
pip install pre-commit
pre-commit install
```

The active hooks are:

| Hook | Purpose |
|---|---|
| `check-yaml` | Validates YAML syntax |
| `end-of-file-fixer` | Ensures files end with a newline |
| `trailing-whitespace` | Removes trailing whitespace |
| `codespell` | Spell-checks source and documentation |
| `clang-format` | Formats C/C++ with `.clang-format` (LLVM style, C++20, 100-col limit) |
| `forbid-crlf` / `remove-crlf` | Enforces LF line endings |
| `forbid-tabs` / `remove-tabs` | Replaces tabs with spaces |
| `cmake-format` | Auto-formats `CMakeLists.txt` files |
| `cmake-lint` | Lints `CMakeLists.txt` (config: `.github/cmake-lint.py`) |
| `markdownlint` | Lints Markdown files (config: `.markdownlint.jsonc`); run with `--fix` |

**Configuration files:**

- `.clang-format` — LLVM-based C++ style, **`Standard: c++20`**, 100-column
  limit.  The standard must not be downgraded to `c++17`.
- `.clang-tidy` — Static-analysis checks: `bugprone-*`, `modernize-*`,
  `readability-*`, and others; several overly-strict checks are disabled.
- `.codespellrc` — Codespell skip patterns; custom ignore list in
  `.codespell-ignore`.
- `.github/cmake-lint.py` — cmake-lint settings.
- `.markdownlint.jsonc` — markdownlint rule configuration.  The hook must
  always pass both `--config .markdownlint.jsonc` **and** `--fix` (PRs #319,
  #321).  Do **not** add markdownlint without an explicit config; the default
  rule set can produce large, unexpected failure sets.

**Codespell notes:**

- Fix genuine prose misspellings in any file, including Geant4-derived sources.
- Do **not** rename Geant4 macro commands that look like misspellings (e.g.
  `/process/inactivate` is a valid Geant4 UI command, not a typo for
  `deactivate`).  Instead, add the word to `.codespell-ignore` so codespell
  stops flagging it.

**CSS / JavaScript in Python scripts:**

- CSS and JavaScript content used by report generators must live in **separate
  tracked files**, not as inline strings inside Python scripts.  Inline content
  creates diff noise and confuses content-type handling.

Run all hooks manually on all files:

```bash
pre-commit run --all-files
```

---

## 14. Sanitizers

- **Scope:** All sanitizer environment variables (`ASAN_OPTIONS`, `LSAN_OPTIONS`,
  `UBSAN_OPTIONS`, `TSAN_OPTIONS`) must be set **only in the `sanitizer` matrix
  CI job**, not globally or in the `build-test-benchmark` job. All four are
  defined unconditionally at job level; inactive sanitizers simply ignore
  the unrelated vars.
- **Suppression files:** `.github/asan.supp`, `.github/lsan.supp`,
  `.github/ubsan.supp`, and `.github/tsan.supp`.  New
  sanitizer failures originating from Geant4 internals or other third-party
  libraries should be suppressed in the appropriate file rather than disabling
  the sanitizer build.
- **Flag probing:** Do **not** use `check_cxx_compiler_flag` to detect
  `-fsanitize=address` or similar flags; it produces false negatives with
  clang.  Add the flags unconditionally inside the relevant CMake option block
  (`if(USE_ASAN) ... endif()`).

---

## 15. Report and Script Conventions

- CI-specific Python and shell scripts belong in **`.github/scripts/`**, not
  in a top-level `scripts/` directory.  Top-level `scripts/` should not exist.
- Shared utility code that is used by multiple `generate_*_report.py` scripts
  must be extracted into a **common module** (e.g.
  `.github/scripts/report_utils.py`) rather than duplicated.
- Benchmark report parsers are tightly coupled to the benchmark output format.
  When the benchmark output format changes (e.g. migrating to Google Benchmark
  JSON), the parser must be updated **in the same PR**.  Failing to do so has
  broken report generation repeatedly (PRs #95, #122, #127).
- Generated reports must use the **America/New_York** timezone.

---

## 16. Examples

- **License:** Source files copied from Geant4 examples (B1, B4c, etc.) must
  retain their **original Geant4 license block** unchanged.  Do not replace it
  with the project LGPL header.  Add the example directory to `ignore-paths`
  in `.github/workflows/spdx.yml`.
- **STEP fixtures:** STEP files used in examples must be **centered at the
  origin** so that placement code in the example does not need to compensate
  for an embedded offset.
- **Spelling:** Fix genuine prose misspellings in example files (comments,
  string literals, variable names), but do **not** alter Geant4 UI macro
  commands that codespell flags incorrectly (e.g. `/process/inactivate`).
  Add such words to `.codespell-ignore` instead.

---

## 17. Application (`src/app/`)

Standalone application targets (not pedagogical examples, not test
infrastructure) live in `src/app/<name>/` and are built unconditionally
(no `BUILD_APP` option).

- **Location:** `src/app/g4occt/` for the `g4occt` interactive tool.
- **Messenger pattern:** Use `G4GenericMessenger` (`DeclareMethod` /
  `DeclareProperty`) rather than the low-level `G4UIcmd*` / `SetNewValue`
  approach.  One `G4GenericMessenger` instance per UI sub-directory; owned
  by the class that controls the state.
- **Macro files:** Committed under `src/app/<name>/macros/`; copied to the
  build directory via `configure_file(...COPYONLY)`.  Template macros
  (with `@VAR@` placeholders) use the `.mac.in` extension and are processed
  with `configure_file(...@ONLY)`.
- **Tests:** CTest integration tests are added in the same `CMakeLists.txt`,
  gated on `BUILD_TESTING`.  They run the executable with pre-built STEP
  fixtures and check the exit code.

---

## 18. Updating These Instructions

If a PR discussion establishes a new convention:

1. Add or amend the relevant section in this file (`AGENTS.md`) in the same
   PR commit.
2. Keep `.github/copilot-instructions.md` in sync (it references this file).
3. Note the change in the PR description so reviewers can confirm the
   instructions are accurate.
