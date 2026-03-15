<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

# Geometry fixtures

This directory owns the repository-checked geometry fixture set used by the
geometry validation helpers under `src/tests/geometry/`.

## Manifest layout and schema

The fixture tree is driven by the repository manifest at `manifest.yaml`:

- `schema_version: geometry-fixture-manifest/v1`
- `fixture_root: src/tests/fixtures/geometry`
- `owner: src/tests`
- shared policy:
  - STEP asset name: `shape.step`
  - provenance metadata name: `provenance.yaml`
  - volume unit: `mm3`
  - validation states: `planned`, `validated`, `rejected`

Each family directory listed in the repository manifest owns a
`<family>/manifest.yaml` file plus zero or more concrete fixtures. On disk,
family manifests currently use:

- `schema_version`
- `family`
- `summary`
- `coverage_classes`
- `fixtures`

Fixture entries record:

- `id`
- `geant4_class`
- `fixture_slug`
- `relative_directory`
- `validation_state`
- `expectations` (currently volume checks in `mm3`)

The parser also accepts optional `family`, `step_file`, `provenance_file`, and
`reused_by` overrides for fixture entries. In the current checked-in tree, all
fixtures use the default `shape.step` and `provenance.yaml` names from the
repository policy, and `reused_by` is now used to record `G4U*` backend-adapter
classes that intentionally reuse a canonical native-solid STEP asset.

Fixtures are organized as:

```text
<family>/<Geant4 class>/<fixture-slug>-vN/
  generate.tcl
  shape.step
  provenance.yaml
```

The checked-in STEP file and provenance file names must stay aligned with the
repository policy in `manifest.yaml`.

## Current fixture families

| Family | Coverage today | Regeneration | Notes |
| --- | --- | --- | --- |
| `direct-primitives/` | Real STEP coverage for 10 validated fixtures | `./direct-primitives/regenerate.sh` | Uses `normalize_step_header.py` to make STEP headers deterministic, including the tilted-end `G4CutTubs` slab-intersection fixture. |
| `profile-faceted/` | Real STEP coverage for 10 validated fixtures | `./profile-faceted/regenerate.sh` | Reuses `../direct-primitives/normalize_step_header.py`; now includes a non-monotonic `G4GenericPolycone` contour revolved with DRAWEXE. |
| `twisted-swept/` | Real STEP coverage for 6 validated fixtures | `./twisted-swept/regenerate.sh` | Uses a shared OCCT loft utility to generate `G4ExtrudedSolid`, `G4TwistedBox`, `G4TwistedTrd`, `G4TwistedTrap`, `G4TwistedTubs`, and `G4VTwistedFaceted`. |
| `tessellated/` | Real STEP coverage for 1 validated fixture | `./tessellated/regenerate.sh` | Covers `G4TessellatedSolid` with a closed tetrahedral shell. |
| `boolean-compound/` | Real STEP coverage for 4 validated fixtures | `./boolean-compound/regenerate.sh` | Deterministic DRAWEXE scripts plus inline timestamp normalization. |
| `wrapper-decorator/` | Real STEP coverage for 2 validated fixtures | `./wrapper-decorator/regenerate.sh` | Deterministic DRAWEXE scripts plus inline timestamp normalization. |

Today the tree contains **33 validated fixtures with checked-in STEP assets**
across all 6 family directories. Canonical native-solid fixtures also carry
explicit `reused_by` mappings for the covered `G4U*` backend-adapter classes.

## Concrete seeded fixtures on disk

### `direct-primitives/`

- `G4Box/box-20x30x40-v1`
- `G4Cons/cons-r8-r3-z24-v1`
- `G4CutTubs/cut-tubs-r12-z40-tilted-x-v1`
- `G4Orb/orb-r11-v1`
- `G4Para/para-dx10-dy8-z20-alpha20-v1`
- `G4Sphere/sphere-r15-v1`
- `G4Torus/torus-rtor20-rmax5-v1`
- `G4Trap/trap-dx7-13-dy9-z18-v1`
- `G4Trd/trd-dx10-16-dy8-14-z20-v1`
- `G4Tubs/tubs-r12-z35-v1`

### `profile-faceted/`

- `G4Ellipsoid/ellipsoid-15x10x20-v1`
- `G4EllipticalCone/elliptical-cone-z30-cut15-v1`
- `G4EllipticalTube/elliptical-tube-12x7x25-v1`
- `G4GenericPolycone/generic-polycone-nonmonotonic-z-v1`
- `G4GenericTrap/generic-trap-skewed-z20-v1`
- `G4Hype/hype-r8-stereo20-z25-v1`
- `G4Paraboloid/paraboloid-r6-r18-z20-v1`
- `G4Polycone/polycone-z-22p5-2p5-22p5-v1`
- `G4Polyhedra/polyhedra-hex-r10-r6-z25-v1`
- `G4Tet/tet-right-20x30x40-v1`

### `boolean-compound/`

- `G4UnionSolid/box-overlap-x10-v1`
- `G4SubtractionSolid/box-core-cut-v1`
- `G4IntersectionSolid/box-overlap-x10-v1`
- `G4MultiUnion/triple-box-chain-v1`

### `wrapper-decorator/`

- `G4DisplacedSolid/translated-box-offset-v1`
- `G4ScaledSolid/scaled-sphere-nonuniform-v1`

### `twisted-swept/`

- `G4ExtrudedSolid/extruded-pentagon-z30-v1`
- `G4TwistedBox/box-dx10-dy8-z20-phi30-v1`
- `G4TwistedTrd/trd-dx10-16-dy8-14-z20-phi30-v1`
- `G4TwistedTrap/trap-dx7-13-dy9-z18-phi30-v1`
- `G4TwistedTubs/tubs-r6-r12-z20-dphi210-phi30-v1`
- `G4VTwistedFaceted/faceted-dz20-theta8-phi20-v1`

### `tessellated/`

- `G4TessellatedSolid/tessellated-tet-right-20x30x40-v1`

`G4BooleanSolid` is treated as represented by the validated Boolean subclasses
(`G4UnionSolid`, `G4SubtractionSolid`, `G4IntersectionSolid`) rather than by a
separate standalone STEP asset.

## Regenerating fixtures

All populated families use deterministic generators. From this directory:

```bash
./direct-primitives/regenerate.sh
./profile-faceted/regenerate.sh
./twisted-swept/regenerate.sh
./tessellated/regenerate.sh
./boolean-compound/regenerate.sh
./wrapper-decorator/regenerate.sh
```

Each populated fixture directory contains:

- `generate.tcl` or a shared family utility invocation captured by `regenerate.sh`
- `shape.step`: checked-in STEP geometry
- `provenance.yaml`: constructor parameters and expected metadata

By convention, regenerate the family you changed, then review the touched
`shape.step` and `provenance.yaml` files before committing.

## Validating fixtures

There are two useful validation layers:

1. **Manifest/layout sanity checks**
    - confirm every `relative_directory` from each populated family manifest
      exists
    - confirm each populated fixture directory contains `generate.tcl`,
      `shape.step`, and `provenance.yaml`
    - confirm blocked classes are called out explicitly in the relevant manifest
      comments or README notes
2. **OCCT geometry validation**
   - the helpers in `src/tests/geometry/fixture_validation.hh` and
     `src/tests/geometry/test_geometry_validation.cc` parse the manifests,
     import STEP files, and compare geometry observations such as volume

When editing this README or the manifests, at minimum run a path sanity check so
the documented fixture list matches the files on disk.

## Backend-adapter mappings

The following `G4U*` classes are explicitly covered by canonical native-solid
fixtures via the `reused_by` lists in the family manifests:

- `G4UBox`, `G4UCons`, `G4UCutTubs`, `G4UOrb`, `G4UPara`, `G4USphere`,
  `G4UTorus`, `G4UTrap`, `G4UTrd`, `G4UTubs`
- `G4UEllipsoid`, `G4UEllipticalCone`, `G4UEllipticalTube`, `G4UGenericTrap`,
  `G4UGenericPolycone`, `G4UHype`, `G4UParaboloid`, `G4UPolycone`,
  `G4UPolyhedra`, `G4UTet`
- `G4UExtrudedSolid`
- `G4UTessellatedSolid`
