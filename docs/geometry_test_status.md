<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

# G4OCCT — Geometry Fixture Test Status (Milestone v0.5 Tracker)

This document summarises the current validation status of every fixture in the
geometry test suite and records the remaining work required to reach
**Milestone v0.5**: full validation coverage for all tracked G4 primitive
families.

Each fixture has a STEP file plus a `provenance.yaml` describing its
construction parameters. The validation suite now performs both volume checks
and ray-comparison checks. Fixtures are therefore classified as either
**enforced** (strictly passing in CI) or **expected failures** (xfail), where
known non-equivalence diagnostics are reported but do not fail the full suite.
Structural problems such as missing files or STEP read failures are always hard
errors.

---

## 1. Fixture Families at a Glance

| Family | Total fixtures | Enforced | Xfail | Milestone notes |
|---|---|---|---|---|
| `direct-primitives` | 10 | 10 | 0 | All analytic BRep enforced |
| `profile-faceted` | 10 | 8 | 2 | `G4Hype` and `G4Paraboloid` need tighter curved-surface parity |
| `twisted-swept` | 6 | 6 | 0 | All twisted-swept fixtures enforced |
| `tessellated` | 1 | 1 | 0 | Closed-facet tetrahedron passes fully |
| `boolean-compound` | 4 | 4 | 0 | All boolean fixtures enforced |
| `wrapper-decorator` | 2 | 1 | 1 | `G4ScaledSolid` still needs frame alignment |
| **Total** | **33** | **30** | **3** | — |

The table counts unique STEP fixtures. Several fixtures are reused by `G4U*`
thin-wrapper classes; those wrappers inherit the same enforcement status as the
parent `G4*` fixture and are not listed separately.

---

## 2. Enforced Fixtures (Strictly Passing)

The following fixtures must pass all ray-origin state checks, ray-intersection
checks, ray-distance checks, and volume comparisons on every CI run. A
regression in any one of them causes the suite to fail immediately.

### 2.1 `direct-primitives`

| Geant4 class | Fixture slug | Reused by |
|---|---|---|
| `G4Box` | `box-20x30x40-v1` | `G4UBox` |
| `G4Cons` | `cons-r8-r3-z24-v1` | `G4UCons` |
| `G4CutTubs` | `cut-tubs-r12-z40-tilted-x-v1` | `G4UCutTubs` |
| `G4Orb` | `orb-r11-v1` | `G4UOrb` |
| `G4Para` | `para-dx10-dy8-z20-alpha20-v1` | `G4UPara` |
| `G4Sphere` | `sphere-r15-v1` | `G4USphere` |
| `G4Torus` | `torus-rtor20-rmax5-v1` | `G4UTorus` |
| `G4Trap` | `trap-dx7-13-dy9-z18-v1` | `G4UTrap` |
| `G4Trd` | `trd-dx10-16-dy8-14-z20-v1` | `G4UTrd` |
| `G4Tubs` | `tubs-r12-z35-v1` | `G4UTubs` |

### 2.2 `profile-faceted`

| Geant4 class | Fixture slug | Reused by |
|---|---|---|
| `G4Ellipsoid` | `ellipsoid-15x10x20-v1` | `G4UEllipsoid` |
| `G4EllipticalCone` | `elliptical-cone-z30-cut15-v1` | `G4UEllipticalCone` |
| `G4EllipticalTube` | `elliptical-tube-12x7x25-v1` | `G4UEllipticalTube` |
| `G4GenericPolycone` | `generic-polycone-nonmonotonic-z-v1` | `G4UGenericPolycone` |
| `G4GenericTrap` | `generic-trap-skewed-z20-v1` | `G4UGenericTrap` |
| `G4Polycone` | `polycone-z-22p5-2p5-22p5-v1` | `G4UPolycone` |
| `G4Polyhedra` | `polyhedra-hex-r10-r6-z25-v1` | `G4UPolyhedra` |
| `G4Tet` | `tet-right-20x30x40-v1` | `G4UTet` |

### 2.3 `twisted-swept`

| Geant4 class | Fixture slug | Reused by |
|---|---|---|
| `G4ExtrudedSolid` | `extruded-pentagon-z30-v1` | `G4UExtrudedSolid` |

### 2.4 `tessellated`

| Geant4 class | Fixture slug | Reused by |
|---|---|---|
| `G4TessellatedSolid` | `tessellated-tet-right-20x30x40-v1` | `G4UTessellatedSolid` |

### 2.5 `boolean-compound`

| Geant4 class | Fixture slug |
|---|---|
| `G4UnionSolid` | `box-overlap-x10-v1` |
| `G4SubtractionSolid` | `box-core-cut-v1` |
| `G4IntersectionSolid` | `box-overlap-x10-v1` |
| `G4MultiUnion` | `triple-box-chain-v1` |

### 2.6 `wrapper-decorator`

| Geant4 class | Fixture slug |
|---|---|
| `G4DisplacedSolid` | `translated-box-offset-v1` |

---

## 3. Expected Failures (Xfail)

Fixtures in this section are exercised on every CI run and their diagnostics
are printed, but known non-equivalence errors are reclassified as warnings so
that the suite does not fail due to these already-understood gaps. Only the
specific non-equivalence codes `fixture.volume_mismatch`,
`fixture.ray_origin_state_mismatch`, `fixture.ray_intersection_mismatch`, and
`fixture.ray_distance_mismatch` are demoted. Structural errors such as missing
STEP files, failed STEP reads, or invalid manifests remain errors.

### 3.1 ~~Twisted-solid STEP fixtures pending regeneration~~ ✅ Resolved

**Affected classes:** `G4TwistedBox`, `G4TwistedTrd`, `G4TwistedTrap`,
`G4TwistedTubs`, `G4VTwistedFaceted`

All five twisted-solid STEP fixtures have been regenerated using the updated
B-spline loft generator (`BRepOffsetAPI_ThruSections` with 64 intermediate
cross-sections). The xfail annotation has been removed and all five classes
are now fully enforced.

### 3.2 Faceted profile approximations

**Affected classes:** `G4Hype`, `G4Paraboloid`

**Why they fail:**
Both solids have analytic curved surfaces (hyperboloid sheet for `G4Hype`,
paraboloid of revolution for `G4Paraboloid`) that OCCT cannot represent
exactly as standard NURBS or finite-control-point B-spline surfaces. The
current STEP fixtures are generated by lofting through a discretised set of
profile curves, producing a faceted approximation. That faceting introduces
systematic differences in `DistanceToIn`/`DistanceToOut` and `Inside` results
compared to the Geant4 analytic solid, especially near curved faces.

**Work required to pass:**
1. Evaluate whether a high-degree NURBS approximation within a tight tolerance
   (for example `<= kCarTolerance / 10`) is feasible using OCCT utilities such
   as `GeomConvert` or `Approx_SameParameter`.
2. If a suitable approximation is achievable, regenerate fixtures with that
   surface, tighten the ray-comparison tolerance to match, and remove the xfail
   annotation.
3. Alternatively, expose a native Geant4-side fallback so that `G4OCCTSolid`
   delegates `DistanceToIn`/`DistanceToOut` to the Geant4 analytic solid for
   these specific classes.

### 3.3 Ray-frame alignment not implemented

**Affected classes:** `G4ScaledSolid`

**Why they fail:**
The ray comparison engine fires rays from a representative origin inside the
solid and expects the native Geant4 solid and the imported OCCT solid to agree
on which rays intersect the boundary and at what distance. For this comparison
to be valid, the STEP solid's local-frame origin must coincide with the origin
used by the corresponding Geant4 solid.

- **`G4ScaledSolid`**: The non-uniform scale is applied around the OCCT shape's
  own reference point, which may not match Geant4's expectation that scaling is
  performed around `(0, 0, 0)`.

**Previously affected classes resolved:** `G4CutTubs`, `G4Ellipsoid`,
`G4EllipticalCone`, `G4EllipticalTube`, and `G4Polycone` have all been aligned
to the Geant4 local-frame origin. Their xfail annotations have been removed and
the fixtures are now fully enforced. For `G4EllipticalTube` and `G4CutTubs`,
minor safety-distance differences may remain due to Geant4's conservative lower
bound property (`DistanceToIn(p)` is permitted to under-estimate the exact
distance), but these fall within the 1 % relative tolerance used by the safety
comparison and do not cause test failures.

**`G4Polyhedra` resolved:** The hexagonal frustum fixture used the wrong vertex
positions: `G4Polyhedra` `rOuter` is the tangent (apothem) distance to the outer
surface, not the circumradius. The STEP polyline vertices were corrected to use
circumradius = apothem / cos(π/6), so the STEP solid now matches the G4Polyhedra
geometry exactly. The xfail annotation has been removed and the fixture is now
fully enforced.

**Work required to pass:**
1. For each affected class, verify the exact local-frame origin convention used
   by the Geant4 solid; see [Reference Position Handling](reference_position.md).
2. Adjust the STEP fixture generation scripts, or post-process the STEP file
   with an OCCT recentering transform, so that the STEP solid is aligned to the
   Geant4 local-frame origin.
3. Update the fixture `provenance.yaml` files to document the centering
   convention, then remove the xfail annotation once ray comparisons pass.

---

## 4. Summary Table

The table below lists every fixture with its current enforcement status, the
failure mode that motivates any xfail annotation, and the remediation section
that describes the remaining work.

| Geant4 class | Family | Status | Failure mode | Remediation |
|---|---|---|---|---|
| `G4Box` | direct-primitives | ✅ Enforced | — | — |
| `G4Cons` | direct-primitives | ✅ Enforced | — | — |
| `G4CutTubs` | direct-primitives | ✅ Enforced | — | — |
| `G4Orb` | direct-primitives | ✅ Enforced | — | — |
| `G4Para` | direct-primitives | ✅ Enforced | — | — |
| `G4Sphere` | direct-primitives | ✅ Enforced | — | — |
| `G4Torus` | direct-primitives | ✅ Enforced | — | — |
| `G4Trap` | direct-primitives | ✅ Enforced | — | — |
| `G4Trd` | direct-primitives | ✅ Enforced | — | — |
| `G4Tubs` | direct-primitives | ✅ Enforced | — | — |
| `G4Ellipsoid` | profile-faceted | ✅ Enforced | — | — |
| `G4EllipticalCone` | profile-faceted | ✅ Enforced | — | — |
| `G4EllipticalTube` | profile-faceted | ✅ Enforced | — | — |
| `G4GenericPolycone` | profile-faceted | ✅ Enforced | — | — |
| `G4GenericTrap` | profile-faceted | ✅ Enforced | — | — |
| `G4Hype` | profile-faceted | ⚠ Xfail | faceted approximation | §3.2 |
| `G4Paraboloid` | profile-faceted | ⚠ Xfail | faceted approximation | §3.2 |
| `G4Polycone` | profile-faceted | ✅ Enforced | — | — |
| `G4Polyhedra` | profile-faceted | ✅ Enforced | — | — |
| `G4Tet` | profile-faceted | ✅ Enforced | — | — |
| `G4ExtrudedSolid` | twisted-swept | ✅ Enforced | — | — |
| `G4TwistedBox` | twisted-swept | ✅ Enforced | — | — |
| `G4TwistedTrap` | twisted-swept | ✅ Enforced | — | — |
| `G4TwistedTrd` | twisted-swept | ✅ Enforced | — | — |
| `G4TwistedTubs` | twisted-swept | ✅ Enforced | — | — |
| `G4VTwistedFaceted` | twisted-swept | ✅ Enforced | — | — |
| `G4TessellatedSolid` | tessellated | ✅ Enforced | — | — |
| `G4UnionSolid` | boolean-compound | ✅ Enforced | — | — |
| `G4SubtractionSolid` | boolean-compound | ✅ Enforced | — | — |
| `G4IntersectionSolid` | boolean-compound | ✅ Enforced | — | — |
| `G4MultiUnion` | boolean-compound | ✅ Enforced | — | — |
| `G4DisplacedSolid` | wrapper-decorator | ✅ Enforced | — | — |
| `G4ScaledSolid` | wrapper-decorator | ⚠ Xfail | ray-frame misalignment | §3.3 |

> **`G4U*` thin wrappers** (`G4UBox`, `G4UCons`, `G4UCutTubs`, etc.) reuse the
> same STEP fixture as their `G4*` counterpart and therefore inherit the same
> enforcement status without requiring a separate entry.

---

## 5. Path to v0.5

The remaining work to eliminate all current xfails is:

| Work item | Affects | Priority |
|---|---|---|
| Fix ray-frame alignment for `G4ScaledSolid` | `G4ScaledSolid` | **Medium** |
| Improve faceted approximation for hyperboloid/paraboloid or add analytic fallback | `G4Hype`, `G4Paraboloid` | **Medium** |

---

## 6. Adding, Updating, or Promoting Fixtures

To **add** a new fixture:
1. Generate a STEP file using the appropriate DRAWEXE or OCCT utility script in
   `src/tests/fixtures/geometry/<family>/`.
2. Write a `provenance.yaml` describing the construction parameters.
3. Register the fixture in the family `manifest.yaml`. Set
   `validation_state: planned` initially; once the fixture is confirmed passing,
   promote it to `validation_state: validated`. Use
   `validation_state: rejected` for fixtures that are intentionally excluded.
   Include a volume expectation for `validated` entries.
4. Run `./build/src/tests/test_geometry_validation` and confirm the new fixture
   produces no errors.

To **update** an existing fixture (for example to fix frame alignment):
1. Regenerate the STEP file using the updated construction script.
2. Normalise the STEP `FILE_NAME` timestamp with
   `src/tests/fixtures/geometry/tools/normalize_step_header.py`.
3. Update `provenance.yaml` to describe the change.
4. Confirm the volume expectation still holds and the validation test passes.

To **promote** an xfail fixture to enforced:
1. Address the relevant remediation work described in Section 3.
2. Remove the corresponding `geant4_class` check from
   `ExpectedFailureForFixture` in `src/tests/geometry/fixture_validation.cc`.
3. Confirm CI passes with the xfail annotation removed.

See [Reference Position Handling](reference_position.md) for recentering
strategies and [Solid Navigation Design](solid_navigation.md) for tolerance and
unit-convention requirements that ray-comparison checks must satisfy.
