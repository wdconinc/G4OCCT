<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

# G4OCCT — Geometry Fixture Test Status

This document summarises the current enforcement status of every fixture in
the geometry test suite.  Each fixture either **passes** its full ray-comparison
and volume validation (marked **enforced**) or is registered as an
**expected failure** (xfail) because a known gap prevents the fixture from
being equivalent to the corresponding native Geant4 solid today.

Expected failures are not ignored: the validation binary still exercises them
and reports a diagnostic; the xfail classification merely prevents the overall
test from failing while the underlying work is in progress.

---

## 1. Fixture Families at a Glance

| Family | Total fixtures | Enforced | Xfail |
|---|---|---|---|
| `direct-primitives` | 10 | 9 | 1 |
| `profile-faceted` | 10 | 3 | 7 |
| `twisted-swept` | 6 | 1 | 5 |
| `tessellated` | 1 | 1 | 0 |
| `boolean-compound` | 4 | 4 | 0 |
| `wrapper-decorator` | 2 | 1 | 1 |
| **Total** | **33** | **19** | **14** |

The table counts unique STEP fixtures.  Several fixtures are noted as
`reused_by` one or more `G4U*` thin-wrapper classes; the wrapper classes
inherit the same enforcement status as the parent fixture.

---

## 2. Enforced Fixtures (Strictly Passing)

The following fixtures must pass all ray-origin state checks, ray-intersection
checks, ray-distance checks, and volume comparisons on every CI run.
A regression in any one of them causes the test suite to fail immediately.

### 2.1 `direct-primitives`

| Geant4 class | Fixture slug | Reused by |
|---|---|---|
| `G4Box` | `box-20x30x40-v1` | `G4UBox` |
| `G4Cons` | `cons-r8-r3-z24-v1` | `G4UCons` |
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
| `G4GenericPolycone` | `generic-polycone-nonmonotonic-z-v1` | `G4UGenericPolycone` |
| `G4GenericTrap` | `generic-trap-skewed-z20-v1` | `G4UGenericTrap` |
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

Fixtures in this section are exercised on every CI run, and their diagnostics
are printed, but non-equivalence errors are reclassified as warnings so the
suite does not fail due to these known gaps.  Only the specific
non-equivalence error codes (`fixture.volume_mismatch`,
`fixture.ray_origin_state_mismatch`, `fixture.ray_intersection_mismatch`,
`fixture.ray_distance_mismatch`) are demoted; structural errors such as
missing STEP files or failed STEP reads are always treated as hard failures.

### 3.1 Twisted-solid surrogates

**Affected classes:** `G4TwistedBox`, `G4TwistedTrd`, `G4TwistedTrap`,
`G4TwistedTubs`, `G4VTwistedFaceted`

**Why they fail:**
Geant4's twisted solids have faces that follow a continuous helical or
quadric twist described analytically.  OCCT has no native representation for
such surfaces.  The STEP fixtures in the `twisted-swept` family are generated
by an OCCT ruled-loft utility that approximates the twisted shape with a
sequence of ruled surface patches.  The resulting BRep differs geometrically
from the analytic Geant4 definition, so ray-intersection distances and
inside/outside classifications diverge at the boundary.

**Important context:** G4OCCT's primary goal is to import and navigate
arbitrary STEP geometry — not to provide a round-trip STEP export for every
Geant4 type.  The absence of an exact STEP representation for twisted surfaces
is therefore **not a design gap that must be closed** for the project to be
useful.  The fixtures exist to validate that the OCCT-imported shapes behave
consistently, not to demand that twisted Geant4 solids be expressible in STEP.

**Work required to pass (optional):**
If a use case arises that requires ray-comparison parity for twisted fixtures,
the path forward would be:
1. Parameterise the swept profile at discrete twist angles and build a tight
   B-spline surface approximation through those cross-sections.
2. Regenerate the STEP fixtures with that construction so the BRep boundary
   agrees with the Geant4 analytic solid to within `kCarTolerance`.
3. Update the fixture `provenance.yaml` files, remove the xfail annotation in
   `ExpectedFailureForFixture`, and confirm CI passes.

### 3.2 Faceted profile approximations

**Affected classes:** `G4Hype`, `G4Paraboloid`

**Why they fail:**
Both solids have analytic curved surfaces (hyperboloid sheet for `G4Hype`,
paraboloid of revolution for `G4Paraboloid`) that OCCT cannot represent
exactly as NURBS or standard B-spline surfaces with finite control points.
The current STEP fixtures are generated by lofting through a discretised set
of profile curves, producing a faceted approximation.  The faceting
introduces systematic differences in `DistanceToIn`/`DistanceToOut` and
`Inside` results compared to the Geant4 analytic solid, especially near the
curved faces.

**Work required to pass:**
1. Evaluate whether a high-degree NURBS approximation within a tight tolerance
   (e.g., `≤ kCarTolerance / 10`) is feasible using OCCT's `GeomConvert`
   or `Approx_SameParameter` utilities.
2. If a suitable approximation is achievable, regenerate fixtures with that
   surface, tighten the ray-comparison tolerance to match, and remove the xfail
   annotation.
3. Alternatively, expose a native Geant4-side fallback so that `G4OCCTSolid`
   delegates `DistanceToIn`/`DistanceToOut` to the Geant4 analytic solid for
   these specific classes.

### 3.3 Ray-frame alignment not implemented

**Affected classes:** `G4CutTubs`, `G4Ellipsoid`, `G4EllipticalCone`,
`G4EllipticalTube`, `G4Polycone`, `G4Polyhedra`, `G4ScaledSolid`

**Why they fail:**
The ray comparison engine fires rays from a representative origin inside the
solid (typically the bounding-box center or solid centroid) and expects the
native Geant4 solid and the imported OCCT solid to agree on which rays
intersect the boundary and at what distance.  For this comparison to be
valid, the local-frame origin of the STEP solid must coincide with the
origin that the native Geant4 solid uses.

Several fixture families currently do not align their STEP shapes to the
Geant4-convention local origin:

- **`G4CutTubs`**: The tilted end-face geometry shifts the centroid away
  from the geometric center.  The STEP origin has not yet been adjusted to
  compensate.
- **`G4Ellipsoid`, `G4EllipticalCone`, `G4EllipticalTube`**: Profile-loft
  construction places the STEP shape at a DRAWEXE-internal coordinate that
  differs from the Geant4 local frame.
- **`G4Polycone`, `G4Polyhedra`**: The z-axis span of the revolved profile
  means Geant4 centers the solid differently from OCCT's placement after the
  STEP export.
- **`G4ScaledSolid`**: The non-uniform scale is applied around the OCCT
  shape's own reference point, which may not match Geant4's expectation that
  scaling is performed around `(0, 0, 0)`.

**Work required to pass:**
1. For each affected class, verify the exact local-frame origin convention
   used by the Geant4 solid (see `G4VSolid` documentation and
   [Reference Position Handling](reference_position.md)).
2. Adjust the STEP fixture generation scripts (or post-process the STEP file
   with an OCCT recentering transform) so that the STEP solid's centroid
   coincides with the Geant4 local-frame origin.
3. Update the fixture `provenance.yaml` files to document the centering
   convention, and remove the xfail annotation in `ExpectedFailureForFixture`
   once ray comparisons pass.

---

## 4. Summary Table

The table below lists every fixture with its enforcement status, the error
code(s) that would be reported if the xfail annotation were removed, and a
reference to the section above that describes the required remediation work.

| Geant4 class | Family | Status | Failure mode | Remediation |
|---|---|---|---|---|
| `G4Box` | direct-primitives | ✅ Enforced | — | — |
| `G4Cons` | direct-primitives | ✅ Enforced | — | — |
| `G4CutTubs` | direct-primitives | ⚠ Xfail | ray-frame misalignment | §3.3 |
| `G4Orb` | direct-primitives | ✅ Enforced | — | — |
| `G4Para` | direct-primitives | ✅ Enforced | — | — |
| `G4Sphere` | direct-primitives | ✅ Enforced | — | — |
| `G4Torus` | direct-primitives | ✅ Enforced | — | — |
| `G4Trap` | direct-primitives | ✅ Enforced | — | — |
| `G4Trd` | direct-primitives | ✅ Enforced | — | — |
| `G4Tubs` | direct-primitives | ✅ Enforced | — | — |
| `G4Ellipsoid` | profile-faceted | ⚠ Xfail | ray-frame misalignment | §3.3 |
| `G4EllipticalCone` | profile-faceted | ⚠ Xfail | ray-frame misalignment | §3.3 |
| `G4EllipticalTube` | profile-faceted | ⚠ Xfail | ray-frame misalignment | §3.3 |
| `G4GenericPolycone` | profile-faceted | ✅ Enforced | — | — |
| `G4GenericTrap` | profile-faceted | ✅ Enforced | — | — |
| `G4Hype` | profile-faceted | ⚠ Xfail | faceted approximation | §3.2 |
| `G4Paraboloid` | profile-faceted | ⚠ Xfail | faceted approximation | §3.2 |
| `G4Polycone` | profile-faceted | ⚠ Xfail | ray-frame misalignment | §3.3 |
| `G4Polyhedra` | profile-faceted | ⚠ Xfail | ray-frame misalignment | §3.3 |
| `G4Tet` | profile-faceted | ✅ Enforced | — | — |
| `G4ExtrudedSolid` | twisted-swept | ✅ Enforced | — | — |
| `G4TwistedBox` | twisted-swept | ⚠ Xfail | ruled-loft surrogate | §3.1 |
| `G4TwistedTrap` | twisted-swept | ⚠ Xfail | ruled-loft surrogate | §3.1 |
| `G4TwistedTrd` | twisted-swept | ⚠ Xfail | ruled-loft surrogate | §3.1 |
| `G4TwistedTubs` | twisted-swept | ⚠ Xfail | ruled-loft surrogate | §3.1 |
| `G4VTwistedFaceted` | twisted-swept | ⚠ Xfail | ruled-loft surrogate | §3.1 |
| `G4TessellatedSolid` | tessellated | ✅ Enforced | — | — |
| `G4UnionSolid` | boolean-compound | ✅ Enforced | — | — |
| `G4SubtractionSolid` | boolean-compound | ✅ Enforced | — | — |
| `G4IntersectionSolid` | boolean-compound | ✅ Enforced | — | — |
| `G4MultiUnion` | boolean-compound | ✅ Enforced | — | — |
| `G4DisplacedSolid` | wrapper-decorator | ✅ Enforced | — | — |
| `G4ScaledSolid` | wrapper-decorator | ⚠ Xfail | ray-frame misalignment | §3.3 |

> **`G4U*` thin wrappers** (`G4UBox`, `G4UCons`, `G4UCutTubs`, etc.) reuse
> the same STEP fixture as their `G4*` counterpart and therefore inherit the
> same enforcement status without requiring a separate entry.

---

## 5. Adding or Promoting Fixtures

To **add** a new fixture:
1. Generate a STEP file using the appropriate DRAWEXE or OCCT utility script
   in `src/tests/fixtures/geometry/<family>/`.
2. Write a `provenance.yaml` describing the construction parameters.
3. Register the fixture in the family `manifest.yaml`.  Set
   `validation_state: planned` initially; once the fixture is confirmed
   passing (step 4), promote it to `validation_state: validated`.  Use
   `validation_state: rejected` to mark fixtures that are intentionally
   excluded.  Include a volume expectation for `validated` entries.
4. Run `./build/src/tests/test_geometry_validation` and confirm the new
   fixture produces no errors.

To **promote** an xfail fixture to enforced:
1. Address the relevant remediation work described in Section 3.
2. Remove the corresponding `geant4_class` check from
   `ExpectedFailureForFixture` in
   `src/tests/geometry/fixture_validation.cc`.
3. Confirm CI passes with the xfail annotation removed.
