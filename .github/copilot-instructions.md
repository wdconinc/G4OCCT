<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

# GitHub Copilot Instructions for G4OCCT

Full contributor instructions are in [`AGENTS.md`](../AGENTS.md) at the
repository root.  The summary below is provided for quick in-editor context;
always consult `AGENTS.md` for the authoritative, up-to-date rules.

---

## Quick Reference

### License
Every new file must begin with:
```cpp
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors
```
(Use `#` for CMake/YAML, `<!-- -->` for HTML/Markdown.)

### Include Style
```cpp
#include <G4VSolid.hh>          // external: angle brackets
#include <TopoDS_Shape.hxx>     // external: angle brackets
#include "G4OCCT/G4OCCTSolid.hh"  // internal: double quotes
```

### Build Requirements
- C++17, CMake ≥ 3.16
- Geant4 ≥ 11.3 (`find_package(Geant4 11.3 REQUIRED)`)
- OpenCASCADE ≥ 7.8 (`find_package(OpenCASCADE 7.8 REQUIRED ...)`)

### CI
- Two jobs: `build-test-benchmark` (Release + benchmarks) and `sanitizer` (RelWithDebInfo + ASAN + UBSAN).
- Prerequisite: `cvmfs-contrib/github-action-cvmfs@v5` before `eic/run-cvmfs-osg-eic-shell@v1`.
- Platform: `eic_xl:nightly`.
- `build-test-benchmark`: build with `-DBUILD_TESTING=ON -DBUILD_BENCHMARKS=ON`, run tests, install.
- `sanitizer`: build with `-DBUILD_TESTING=ON -DUSE_ASAN=ON -DUSE_UBSAN=ON`, run tests.
- Sanitizer runtime options (`ASAN_OPTIONS`, `LSAN_OPTIONS`, `UBSAN_OPTIONS`) are scoped to the `sanitizer` job.
- Suppression files live in `.github/asan.supp`, `.github/lsan.supp`, `.github/ubsan.supp`.
- `ci.yml` `pull_request` trigger has no branch filter — CI runs for PRs targeting any branch (supports sub-PR workflows).

### Material Bridging
- No heuristics, no silent fallbacks.
- Unmapped STEP material names → fatal error.
- Preferred strategy: GDML overlay (`G4GDMLParser`).

### Documentation
- Public API must have Doxygen `/** ... */` block comments.
- Docs site: docsify (`docs/index.html`) + Doxygen (`Doxyfile` → `docs/api/`).
- Deployed to GitHub Pages by `.github/workflows/docs.yml`.

### Updating Instructions
When a PR establishes a new convention, update **both** `AGENTS.md` **and**
this file in the same commit.

### Code Quality Tools
Install pre-commit hooks:
```bash
pip install pre-commit && pre-commit install
```
- `.clang-format`: LLVM style, `Standard: c++17`, 100-col limit
- `.clang-tidy`: `bugprone-*`, `modernize-*`, `readability-*`
- `.codespellrc` / `.codespell-ignore`: spell checking
- `.github/cmake-lint.py`: cmake-lint settings
- Run all hooks: `pre-commit run --all-files`
