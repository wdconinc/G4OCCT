<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

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
  // Copyright (C) 2024 G4OCCT Contributors
  ```
- CMake `#`-comment style:
  ```cmake
  # cmake-format: off
  # SPDX-License-Identifier: LGPL-2.1-or-later
  # Copyright (C) 2024 G4OCCT Contributors
  # cmake-format: on
  ```
- YAML `#`-comment style:
  ```yaml
  # SPDX-License-Identifier: LGPL-2.1-or-later
  # Copyright (C) 2024 G4OCCT Contributors
  ```
- HTML/Markdown `<!-- -->` style:
  ```html
  <!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
  <!-- Copyright (C) 2024 G4OCCT Contributors -->
  ```
- The CI workflow `.github/workflows/spdx.yml` uses `enarx/spdx@master` to
  enforce headers on every PR.  A new file that fails the SPDX check will
  block merge.
- The SPDX header must be the **very first line(s)** of every file.  Never
  place a shebang (`#!/...`) before the SPDX header.
- CMake files must wrap the SPDX header in `# cmake-format: off` / `# cmake-format: on`
  guards so `cmake-format` cannot reflow the comment block:
  ```cmake
  # cmake-format: off
  # SPDX-License-Identifier: LGPL-2.1-or-later
  # Copyright (C) 2024 G4OCCT Contributors
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
  `-Wunused-include` warnings.

---

## 3. Build Requirements

- **C++ standard:** C++20 (`CMAKE_CXX_STANDARD 20`).
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
- The downstream integration test lives in `src/tests/downstream_test/` and
  is registered as a four-step CTest fixture chain:
  `install_g4occt` → `downstream_configure` → `downstream_build` →
  `test_downstream_findpackage`.
- Tests must pass with `ctest --test-dir build --output-on-failure -j$(nproc)`.
- Do not remove or disable existing tests.
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
  specific error codes** (e.g. `fixture.volume_mismatch`,
  `fixture.ray_distance_mismatch`).  Never demote all errors — structural or
  IO failures (missing STEP file, read errors) must remain hard errors.

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
- **CI job (`ci.yml`):** Two jobs:
  1. `build-test-benchmark` — builds with `-DCMAKE_BUILD_TYPE=Release
     -DBUILD_TESTING=ON -DBUILD_BENCHMARKS=ON`, runs tests, and installs.
  2. `sanitizer` — builds with `-DCMAKE_BUILD_TYPE=RelWithDebInfo
     -DBUILD_TESTING=ON -DUSE_ASAN=ON -DUSE_UBSAN=ON` and runs tests.
  Both jobs check out the repository, install CVMFS, and run inside
  `eic/run-cvmfs-osg-eic-shell@v1` with `platform-release: "eic_xl:nightly"`.
- The sanitizer job sets `ASAN_OPTIONS`, `LSAN_OPTIONS`, and `UBSAN_OPTIONS`
  at the workflow level, matching the EICrecon project conventions.
- Suppression files live in `.github/asan.supp`, `.github/lsan.supp`, and
  `.github/ubsan.supp`.
- **Do not split** tests and benchmarks into separate jobs.
- **Docs workflow (`docs.yml`):** Builds Doxygen API docs and deploys the
  `docs/` directory (including generated `docs/api/`) to GitHub Pages.
- **SPDX workflow (`spdx.yml`):** Enforces SPDX headers via `enarx/spdx@master`.

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

## 8. Material Bridging

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

## 9. Geometry and Navigation

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
- `GetPointOnSurface()` must sample from OCCT tessellation.  Returning the
  origin (the `G4VSolid` base-class default) triggers Geant4 warnings.
- Internal helper types (e.g. `FaceBounds`, `ClosestFaceMatch`) must be
  **private nested types** and must not appear in the public header.  Do not
  leak implementation details into the public API surface.
- `G4Polyhedra` STEP fixtures use the **circumradius**, not the apothem.
  Confusing the two produces incorrect vertex positions (PR #130).

---

## 10. Code Style

- Doxygen `/** ... */` block comments before all public API (classes,
  constructors, methods).
- Single-line `///` Doxygen comments for short members.
- Section dividers use `// ── Section name ──...──` (em-dash style) as seen
  in existing source files.
- No trailing whitespace; use 2-space indentation for C++; 100-character column limit.

---

## 11. Code Quality Tools (pre-commit)

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

**Configuration files:**
- `.clang-format` — LLVM-based C++ style, **`Standard: c++20`**, 100-column
  limit.  The standard must not be downgraded to `c++17`.
- `.clang-tidy` — Static-analysis checks: `bugprone-*`, `modernize-*`,
  `readability-*`, and others; several overly-strict checks are disabled.
- `.codespellrc` — Codespell skip patterns; custom ignore list in
  `.codespell-ignore`.
- `.github/cmake-lint.py` — cmake-lint settings.

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

## 12. Sanitizers

- **Scope:** `ASAN_OPTIONS`, `LSAN_OPTIONS`, and `UBSAN_OPTIONS` environment
  variables must be set **only in the `sanitizer` CI job**, not globally or in
  the `build-test-benchmark` job.
- **Suppression files:** `.github/asan.supp`, `.github/lsan.supp`,
  `.github/ubsan.supp` (and `.github/tsan.supp` if a TSAN job exists).  New
  sanitizer failures originating from Geant4 internals or other third-party
  libraries should be suppressed in the appropriate file rather than disabling
  the sanitizer build.
- **Flag probing:** Do **not** use `check_cxx_compiler_flag` to detect
  `-fsanitize=address` or similar flags; it produces false negatives with
  clang.  Add the flags unconditionally inside the relevant CMake option block
  (`if(USE_ASAN) ... endif()`).

---

## 13. Report and Script Conventions

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

## 14. Examples

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

## 15. Updating These Instructions

If a PR discussion establishes a new convention:

1. Add or amend the relevant section in this file (`AGENTS.md`) in the same
   PR commit.
2. Keep `.github/copilot-instructions.md` in sync (it references this file).
3. Note the change in the PR description so reviewers can confirm the
   instructions are accurate.
