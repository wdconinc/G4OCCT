<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->
<!-- Copyright (C) 2024 G4OCCT Contributors -->

# G4OCCT — Material Bridging: Strategies for Mapping OCCT/STEP Materials to Geant4

## 1. The Problem

Geant4 simulations require detailed material descriptions for each detector
volume.  A `G4Material` encodes:

* Chemical composition (elements and their fractional masses/mole-fractions).
* Density (g/cm³).
* Temperature and pressure (for gases).
* State (solid, liquid, gas).
* Derived quantities (radiation length X₀, nuclear interaction length λᵢ,
  mean excitation energy I, …) computed by Geant4 from the above.

STEP files (and OCCT geometry descriptions in general) carry *engineering*
material data:

* A human-readable name (e.g. `"AISI 316L Steel"`, `"Aluminium 6061-T6"`).
* Optional density.
* Optional Young's modulus, Poisson ratio, yield strength (mechanical).
* Optional colour / visual attributes (via XDE).

The physics information required by Geant4 is typically *absent* from STEP
files.  Bridging this gap is therefore a non-trivial step in any CAD-to-Geant4
geometry workflow.

**Design principle:** The material identity for every volume must be
*correct, unique, and unambiguous*.  Heuristics (e.g. guessing a material
from density alone) are explicitly out of scope; they can produce silently
incorrect physics results and are not acceptable in a production simulation
framework.

---

## 2. Strategies

### 2.1 Explicit User-Provided Material Map (Recommended)

**Idea:** The user supplies a mapping file that associates each STEP material
name with an exact `G4Material` definition.  No fallbacks or best-guess logic
is applied; any unmapped material name is a fatal error that must be resolved
before the simulation can run.

**Format example (GDML-inspired XML):**
```xml
<materials>
  <!-- Map STEP name → Geant4 NIST material -->
  <material stepName="AISI 316L" geant4Name="G4_STAINLESS-STEEL"/>
  <material stepName="Al 6061"   geant4Name="G4_Al"/>

  <!-- Custom multi-element material -->
  <material stepName="FR4" density="1.86" unit="g/cm3">
    <fraction n="0.18" ref="Si"/>
    <fraction n="0.39" ref="O"/>
    <fraction n="0.28" ref="C"/>
    <fraction n="0.03" ref="H"/>
    <fraction n="0.12" ref="Br"/>
  </material>
</materials>
```

**Behaviour:**
* Every material name encountered in the STEP file **must** appear in the
  mapping file.
* If a name is absent, G4OCCT aborts with a descriptive error listing the
  unmapped material.
* There are no defaults or catch-all fallbacks.

**Pros:**
* Unambiguous — the user explicitly controls every material assignment.
* User-maintained file separates material physics from CAD geometry.
* Custom multi-element materials can be defined inline.

**Cons:**
* Requires the user to create and maintain the mapping file.

---

### 2.2 GDML Overlay (Primary Long-Term Strategy)

**Idea:** Build the physical geometry from the STEP file, then read an
accompanying GDML file that provides material definitions and material
assignments by volume name.

GDML is the standard Geant4 geometry exchange language and already provides
a complete XML vocabulary for materials (`<material>`, `<element>`,
`<fraction>`, density, state, temperature, pressure, optical properties).
Using GDML for the material side of the bridge leverages existing Geant4
tooling (`G4GDMLParser`), schema validation, and community familiarity.

**Workflow:**
1. Import the STEP file → build G4OCCT geometry tree (solids + placements,
   no materials yet).
2. Parse the GDML file.  Extract:
   * `<materials>` section → construct `G4Material` objects.
   * `<structure>` section → match `<volume>` `name` attributes to OCCT
     XDE label names and assign the corresponding `G4Material*`.
3. Every volume in the OCCT tree must find a corresponding entry in the GDML
   `<structure>`.  Unmatched volumes are a fatal error.

**Pros:**
* GDML is schema-validated; errors are caught before the simulation runs.
* The full Geant4 `G4GDMLParser` material vocabulary is available, including
  isotopes, natural elements, molecules, mixtures, optical properties, and
  surface properties.
* Separates CAD geometry (STEP) from simulation physics (GDML) cleanly.

**Cons:**
* Requires maintaining two separate files (STEP + GDML) in sync.
* Volume naming must be consistent between XDE labels and GDML names.

---

### 2.3 Inline GDML Annotations in the CAD Model

**Idea:** Embed GDML material names directly in the STEP file using
user-defined attributes or the `PRODUCT_DEFINITION_CONTEXT` entity name
field.  G4OCCT reads the embedded name and resolves it via a provided GDML
materials fragment.

**Workflow:**
1. In the CAD tool, set the material name of each part to the exact string
   that appears as a `<material name="...">` entry in the GDML materials
   fragment.
2. G4OCCT parses the GDML fragment to build `G4Material` objects.
3. G4OCCT reads the material name from each XDE label and performs an exact
   lookup — no normalization or matching beyond exact string equality.

**Pros:**
* The complete material identity is encoded in the STEP file itself.
* No separate mapping file is needed.
* Still unambiguous: exact string matching, no guessing.

**Cons:**
* Requires strict CAD tool discipline; automated STEP editors may overwrite
  custom names.

---

## 3. Recommended Phased Approach

| Phase | Strategy | Goal |
|---|---|---|
| v0.1 | Explicit user map (2.1) | First working end-to-end example |
| v0.2 | GDML overlay (2.2) | Full material vocabulary, schema validation |
| v0.3 | Inline GDML annotations (2.3) | Self-contained STEP files |

---

## 4. Open Questions

1. **Unit consistency:** STEP files use metres or millimetres depending on
   the AP version and CAD tool.  G4OCCT must confirm and convert units for
   both geometry and material density.

2. **Composite materials:** Engineering materials such as printed circuit
   board laminates (FR4), carbon-fibre-reinforced polymers (CFRP), and
   aerogels have no NIST equivalent and require custom
   `G4Material::CreateMixture` definitions.  The GDML `<material>` element
   handles these natively.

3. **Optical properties:** Geant4 optical physics (`G4OpticalSurface`,
   `G4MaterialPropertiesTable`) has no STEP equivalent at all.  These must
   always be specified via GDML or user code.

4. **Material de-duplication:** When the same material name appears on
   multiple volumes, G4OCCT must ensure a single `G4Material*` is shared
   rather than creating duplicate objects.

5. **Error handling:** When a STEP material name is not found in the mapping,
   G4OCCT must abort with a clear diagnostic listing the unmapped name(s)
   and the STEP file path from which they were read.  Silent fallbacks are
   not permitted.
